/* board_sim: a Deck Link board on the host, for tests against the real deck.
 *
 * stdin is the board's serial RX and stdout its TX (binary frames only,
 * unbuffered). Everything human-readable goes to stderr.
 *
 * Usage:
 *   board_sim [--board-id ID] [--type TYPE] [--firmware VER]
 *             [--fields N|tile,card,prompt,numbers] [--first-slot N] [--slot-count N]
 *             [--answer-primary] [--seconds N] [--log]
 *
 *   --board-id ID      Hello.board_id (default "sim-0001")
 *   --type TYPE        Hello.type (default "board-sim")
 *   --firmware VER     Hello.firmware (default "0.1.0")
 *   --fields F         Subscribe.fields: a number or names joined by commas
 *                      (default tile; --answer-primary adds prompt)
 *   --first-slot N, --slot-count N   Subscribe slots (default 0, 0 = all)
 *   --answer-primary   when an agent is WAITING with a prompt that has a
 *                      primary option, send Answer with it (once per prompt)
 *   --seconds N        exit after N seconds (default: run until stdin closes)
 *   --log              print every received event to stderr
 *
 * stderr lines (one event per line, fields separated by single spaces; a
 * field marked "rest" takes the rest of the line and may contain spaces;
 * newlines inside text are printed as spaces):
 *   welcome name=<device_name> scope=<0-3> approved=<0|1>
 *   online
 *   offline
 *   snapshot_begin server=<name> agents=<n>
 *   agent id=<id> slot=<n> status=<starting|running|waiting|idle|error|exited|unknown> prompt=<id or -> options=<n> title=<rest>
 *   removed id=<id>
 *   snapshot_end
 *   result seq=<n> code=<n> error=<rest>
 *   ping seq=<n>
 *   hat_list seq=<n> shapes=<n> colors=<n>
 *   live seq=<n> sessions=<n>
 *   restore seq=<n> results=<n>
 *   answer seq=<n> id=<agent id> prompt=<prompt id> key=<key>
 *   stats rx=<n> bad=<n> overflow=<n> tx=<n> tx_errors=<n>
 * With --log every line is printed. Without it only the "answer" lines and
 * the "result" lines replying to them are printed (and errors, prefixed
 * "board_sim: ").
 *
 * Exit status: 0 on --seconds timeout or EOF on stdin, 1 on a write error,
 * 2 on bad arguments.
 */
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "deck_link.h"
#include "deck_tiles.h"

static int opt_log;
static int opt_answer;
static int write_failed;

/* Answers sent: (seq, prompt id), so results can be matched and each prompt
 * is answered once. */
#define MAX_ANSWERS 64
static struct {
	uint32_t seq;
	char prompt[16];
} answers[MAX_ANSWERS];
static int n_answers;

static size_t sim_write(const uint8_t *d, size_t n, void *user)
{
	(void)user;
	size_t done = 0;
	while (done < n) {
		ssize_t w = write(STDOUT_FILENO, d + done, n - done);
		if (w < 0) {
			if (errno == EINTR)
				continue;
			write_failed = 1;
			break;
		}
		done += (size_t)w;
	}
	return done;
}

static uint32_t sim_millis(void *user)
{
	(void)user;
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint32_t)((uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u);
}

/* Prints s with newlines and tabs turned into spaces. */
static void put_text(const char *s)
{
	for (; *s; s++)
		fputc((*s == '\n' || *s == '\r' || *s == '\t') ? ' ' : *s, stderr);
}

static void on_welcome(deck_link_t *c, const char *name, agentsdeck_link_v1_Scope scope, bool approved)
{
	(void)c;
	if (opt_log) {
		fprintf(stderr, "welcome name=");
		put_text(name);
		fprintf(stderr, " scope=%d approved=%d\n", (int)scope, approved ? 1 : 0);
	}
}

static void on_online(deck_link_t *c)
{
	(void)c;
	if (opt_log)
		fprintf(stderr, "online\n");
}

static void on_offline(deck_link_t *c)
{
	(void)c;
	if (opt_log)
		fprintf(stderr, "offline\n");
}

static void on_snapshot_begin(deck_link_t *c, const agentsdeck_link_v1_Server *s, uint32_t n)
{
	(void)c;
	if (opt_log) {
		fprintf(stderr, "snapshot_begin server=");
		put_text(s->name[0] ? s->name : "-");
		fprintf(stderr, " agents=%u\n", (unsigned)n);
	}
}

static int answered(const char *prompt)
{
	for (int i = 0; i < n_answers; i++)
		if (strcmp(answers[i].prompt, prompt) == 0)
			return 1;
	return 0;
}

static void on_agent(deck_link_t *c, const agentsdeck_link_v1_Agent *a)
{
	const agentsdeck_link_v1_Prompt *p = a->has_waiting ? &a->waiting : NULL;
	if (opt_log) {
		fprintf(stderr, "agent id=%s slot=%u status=%s prompt=%s options=%u title=", a->id, (unsigned)a->slot,
			deck_status_name(a->status), p && p->id[0] ? p->id : "-", p ? (unsigned)p->options_count : 0u);
		put_text(a->title);
		fputc('\n', stderr);
	}
	if (!opt_answer || !p || a->status != agentsdeck_link_v1_Status_STATUS_WAITING || !p->id[0] || answered(p->id))
		return;
	for (pb_size_t i = 0; i < p->options_count; i++) {
		if (!p->options[i].primary)
			continue;
		uint32_t seq = deck_answer(c, a->id, p->id, p->options[i].key);
		if (n_answers < MAX_ANSWERS) {
			answers[n_answers].seq = seq;
			snprintf(answers[n_answers].prompt, sizeof answers[0].prompt, "%s", p->id);
			n_answers++;
		}
		fprintf(stderr, "answer seq=%u id=%s prompt=%s key=%s\n", (unsigned)seq, a->id, p->id, p->options[i].key);
		break;
	}
}

static void on_removed(deck_link_t *c, const char *id)
{
	(void)c;
	if (opt_log)
		fprintf(stderr, "removed id=%s\n", id);
}

static void on_snapshot_end(deck_link_t *c)
{
	(void)c;
	if (opt_log)
		fprintf(stderr, "snapshot_end\n");
}

static void on_result(deck_link_t *c, uint32_t seq, uint32_t code, const char *err)
{
	(void)c;
	int ours = 0;
	for (int i = 0; i < n_answers; i++)
		if (answers[i].seq == seq)
			ours = 1;
	if (opt_log || ours) {
		fprintf(stderr, "result seq=%u code=%u error=", (unsigned)seq, (unsigned)code);
		put_text(err);
		fputc('\n', stderr);
	}
}

static void on_ping(deck_link_t *c, uint32_t seq, uint64_t t)
{
	(void)c, (void)t;
	if (opt_log)
		fprintf(stderr, "ping seq=%u\n", (unsigned)seq);
}

static void on_hat_list(deck_link_t *c, uint32_t seq, const agentsdeck_link_v1_HatList *l)
{
	(void)c;
	if (opt_log)
		fprintf(stderr, "hat_list seq=%u shapes=%u colors=%u\n", (unsigned)seq, (unsigned)l->shapes_count,
			(unsigned)l->colors_count);
}

static void on_live(deck_link_t *c, uint32_t seq, const agentsdeck_link_v1_LiveSessions *l)
{
	(void)c;
	if (opt_log)
		fprintf(stderr, "live seq=%u sessions=%u\n", (unsigned)seq, (unsigned)l->sessions_count);
}

static void on_restore(deck_link_t *c, uint32_t seq, const agentsdeck_link_v1_RestoreResult *r)
{
	(void)c;
	if (opt_log)
		fprintf(stderr, "restore seq=%u results=%u\n", (unsigned)seq, (unsigned)r->results_count);
}

static int parse_fields(const char *s, uint32_t *out)
{
	char *end;
	unsigned long v = strtoul(s, &end, 0);
	if (*s && *end == '\0') {
		*out = (uint32_t)v;
		return 1;
	}
	uint32_t f = 0;
	char buf[128];
	snprintf(buf, sizeof buf, "%s", s);
	for (char *tok = strtok(buf, ","); tok; tok = strtok(NULL, ",")) {
		if (strcmp(tok, "tile") == 0)
			f |= agentsdeck_link_v1_FieldGroup_FIELDS_TILE;
		else if (strcmp(tok, "card") == 0)
			f |= agentsdeck_link_v1_FieldGroup_FIELDS_CARD;
		else if (strcmp(tok, "prompt") == 0)
			f |= agentsdeck_link_v1_FieldGroup_FIELDS_PROMPT;
		else if (strcmp(tok, "numbers") == 0)
			f |= agentsdeck_link_v1_FieldGroup_FIELDS_NUMBERS;
		else
			return 0;
	}
	*out = f;
	return 1;
}

static void usage(void)
{
	fprintf(stderr, "usage: board_sim [--board-id ID] [--type TYPE] [--firmware VER] [--fields F]\n"
			"                 [--first-slot N] [--slot-count N] [--answer-primary] [--seconds N] [--log]\n");
	exit(2);
}

int main(int argc, char **argv)
{
	static deck_link_t link;
	deck_link_config_t cfg;
	memset(&cfg, 0, sizeof cfg);
	cfg.board_id = "sim-0001";
	cfg.type = "board-sim";
	cfg.firmware = "0.1.0";
	cfg.fields = agentsdeck_link_v1_FieldGroup_FIELDS_TILE;
	long seconds = 0;

	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];
		const char *v = i + 1 < argc ? argv[i + 1] : NULL;
		if (strcmp(a, "--log") == 0) {
			opt_log = 1;
		} else if (strcmp(a, "--answer-primary") == 0) {
			opt_answer = 1;
		} else if (!v) {
			usage();
		} else if (strcmp(a, "--board-id") == 0) {
			cfg.board_id = v, i++;
		} else if (strcmp(a, "--type") == 0) {
			cfg.type = v, i++;
		} else if (strcmp(a, "--firmware") == 0) {
			cfg.firmware = v, i++;
		} else if (strcmp(a, "--fields") == 0) {
			if (!parse_fields(v, &cfg.fields))
				usage();
			i++;
		} else if (strcmp(a, "--first-slot") == 0) {
			cfg.first_slot = (uint32_t)strtoul(v, NULL, 10), i++;
		} else if (strcmp(a, "--slot-count") == 0) {
			cfg.slot_count = (uint32_t)strtoul(v, NULL, 10), i++;
		} else if (strcmp(a, "--seconds") == 0) {
			seconds = strtol(v, NULL, 10), i++;
		} else {
			usage();
		}
	}
	if (opt_answer)
		cfg.fields |= agentsdeck_link_v1_FieldGroup_FIELDS_TILE | agentsdeck_link_v1_FieldGroup_FIELDS_PROMPT;

	signal(SIGPIPE, SIG_IGN);
	setvbuf(stderr, NULL, _IOLBF, 0);

	cfg.write = sim_write;
	cfg.millis = sim_millis;
	cfg.on_welcome = on_welcome;
	cfg.on_online = on_online;
	cfg.on_offline = on_offline;
	cfg.on_snapshot_begin = on_snapshot_begin;
	cfg.on_agent = on_agent;
	cfg.on_agent_removed = on_removed;
	cfg.on_snapshot_end = on_snapshot_end;
	cfg.on_result = on_result;
	cfg.on_ping = on_ping;
	cfg.on_hat_list = on_hat_list;
	cfg.on_live_sessions = on_live;
	cfg.on_restore_result = on_restore;
	deck_link_init(&link, &cfg);
	deck_link_begin(&link);

	uint32_t start = sim_millis(NULL);
	int status = 0;
	for (;;) {
		if (write_failed) {
			fprintf(stderr, "board_sim: write to stdout failed\n");
			status = 1;
			break;
		}
		if (seconds > 0 && (uint32_t)(sim_millis(NULL) - start) >= (uint32_t)seconds * 1000u)
			break;
		struct pollfd pfd = {STDIN_FILENO, POLLIN, 0};
		int r = poll(&pfd, 1, 50);
		if (r < 0 && errno != EINTR) {
			perror("board_sim: poll");
			status = 1;
			break;
		}
		if (r > 0) {
			uint8_t buf[512];
			ssize_t n = read(STDIN_FILENO, buf, sizeof buf);
			if (n == 0)
				break; /* the deck closed the port */
			if (n < 0 && errno != EINTR && errno != EAGAIN) {
				perror("board_sim: read");
				status = 1;
				break;
			}
			if (n > 0)
				deck_link_feed(&link, buf, (size_t)n);
		}
		deck_link_poll(&link);
	}

	if (opt_log) {
		const deck_link_stats_t *s = deck_link_stats(&link);
		fprintf(stderr, "stats rx=%u bad=%u overflow=%u tx=%u tx_errors=%u\n", (unsigned)s->rx_frames,
			(unsigned)s->rx_bad, (unsigned)s->rx_overflow, (unsigned)s->tx_frames, (unsigned)s->tx_errors);
	}
	return status;
}
