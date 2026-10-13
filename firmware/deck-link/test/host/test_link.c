/* Host tests for the Deck Link core: COBS, CRC, wire bytes, stream
 * resync, the handshake/liveness timers and deck_tiles. Build with make. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cobs.h"
#include "crc16.h"
#include "deck_link.h"
#include "deck_tiles.h"
#include "proto/pb_decode.h"
#include "proto/pb_encode.h"

static int failures, checks;

#define CHECK(cond)                                                              \
	do {                                                                     \
		checks++;                                                        \
		if (!(cond)) {                                                   \
			failures++;                                              \
			fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
		}                                                                \
	} while (0)

/* ---- COBS / CRC ------------------------------------------------------------ */

static void cobs_roundtrip(const uint8_t *in, size_t n)
{
	static uint8_t enc[2048], dec[2048];
	size_t m = deck_cobs_encode(in, n, enc);
	CHECK(m <= DECK_COBS_MAX(n));
	CHECK(memchr(enc, 0, m) == NULL);
	size_t k = 0;
	CHECK(deck_cobs_decode(enc, m, dec, &k));
	CHECK(k == n && memcmp(dec, in, n) == 0);

	/* In place, the way the core encodes. */
	static uint8_t buf[2048];
	size_t off = n / 254 + 2;
	memcpy(buf + off, in, n);
	size_t m2 = deck_cobs_encode(buf + off, n, buf);
	CHECK(m2 == m && memcmp(buf, enc, m) == 0);
	/* Decoding in place too. */
	CHECK(deck_cobs_decode(buf, m2, buf, &k));
	CHECK(k == n && memcmp(buf, in, n) == 0);
}

static void test_cobs(void)
{
	uint8_t b[700];

	cobs_roundtrip(b, 0);
	b[0] = 0;
	cobs_roundtrip(b, 1);
	memset(b, 0, 10);
	cobs_roundtrip(b, 10);

	/* Runs of non-zero bytes around the 254 block size. */
	size_t runs[] = {1, 253, 254, 255, 256, 508, 509, 600};
	for (size_t r = 0; r < sizeof runs / sizeof runs[0]; r++) {
		for (size_t i = 0; i < runs[r]; i++)
			b[i] = (uint8_t)(i % 255 + 1);
		cobs_roundtrip(b, runs[r]);
	}

	/* 254 non-zero bytes: FF <254 bytes> 01, as the Go encoder does. */
	uint8_t enc[300];
	memset(b, 0x11, 254);
	size_t m = deck_cobs_encode(b, 254, enc);
	CHECK(m == 256 && enc[0] == 0xFF && enc[255] == 0x01);

	/* Known small vectors. */
	uint8_t v1[] = {0x11, 0x22, 0x00, 0x33};
	uint8_t e1[] = {0x03, 0x11, 0x22, 0x02, 0x33};
	m = deck_cobs_encode(v1, sizeof v1, enc);
	CHECK(m == sizeof e1 && memcmp(enc, e1, m) == 0);
	uint8_t v2[] = {0x00, 0x00};
	uint8_t e2[] = {0x01, 0x01, 0x01};
	m = deck_cobs_encode(v2, sizeof v2, enc);
	CHECK(m == sizeof e2 && memcmp(enc, e2, m) == 0);

	/* Zeros mixed into long runs. */
	srand(1);
	for (int round = 0; round < 200; round++) {
		size_t n = (size_t)(rand() % 700);
		for (size_t i = 0; i < n; i++)
			b[i] = (rand() % 8 == 0) ? 0 : (uint8_t)rand();
		cobs_roundtrip(b, n);
	}

	/* Malformed input. */
	size_t k;
	uint8_t bad1[] = {0x05, 0x11, 0x22};
	CHECK(!deck_cobs_decode(bad1, sizeof bad1, enc, &k));
	uint8_t bad2[] = {0x02, 0x11, 0x00, 0x22};
	CHECK(!deck_cobs_decode(bad2, sizeof bad2, enc, &k));
}

static void test_crc(void)
{
	CHECK(deck_crc16((const uint8_t *)"123456789", 9) == 0x29B1);
	uint16_t c = deck_crc16_update(DECK_CRC16_INIT, (const uint8_t *)"1234", 4);
	CHECK(deck_crc16_update(c, (const uint8_t *)"56789", 5) == 0x29B1);
}

static void test_ping_wire(void)
{
	static const uint8_t want[] = {0x0C, 0xDE, 0xC1, 0x01, 0x08, 0x01, 0x62, 0x02, 0x08, 0x05, 0x88, 0xB6, 0x00};
	uint8_t out[64];
	agentsdeck_link_v1_Ping p = {5};
	size_t n = deck_link_encode(1, agentsdeck_link_v1_Frame_ping_tag, agentsdeck_link_v1_Ping_fields, &p, out,
				    sizeof out);
	CHECK(n == sizeof want && memcmp(out, want, n) == 0);

	/* The hand-built body equals nanopb encoding the whole Frame. */
	agentsdeck_link_v1_Frame f = agentsdeck_link_v1_Frame_init_zero;
	f.seq = 1;
	f.which_body = agentsdeck_link_v1_Frame_ping_tag;
	f.body.ping.time_ms = 5;
	uint8_t pb[16];
	pb_ostream_t s = pb_ostream_from_buffer(pb, sizeof pb);
	CHECK(pb_encode(&s, agentsdeck_link_v1_Frame_fields, &f));
	CHECK(s.bytes_written == 6 && memcmp(pb, want + 4, 6) == 0);

	/* Too small an output buffer fails cleanly. */
	CHECK(deck_link_encode(1, agentsdeck_link_v1_Frame_ping_tag, agentsdeck_link_v1_Ping_fields, &p, out, 10) == 0);
}

/* ---- a fake board: captures TX, fake clock, records events ------------------ */

static uint8_t tx_buf[8192];
static size_t tx_len;
static uint32_t clock_ms;

static size_t fake_write(const uint8_t *d, size_t n, void *user)
{
	(void)user;
	if (tx_len + n <= sizeof tx_buf) {
		memcpy(tx_buf + tx_len, d, n);
		tx_len += n;
	}
	return n;
}

static uint32_t fake_millis(void *user)
{
	(void)user;
	return clock_ms;
}

static agentsdeck_link_v1_Frame got[16];
static int got_n;
static int ev_welcome, ev_online, ev_offline, ev_result, ev_agents, ev_removed, ev_snap_begin, ev_snap_end;
static uint32_t last_result_code;

static void rec_frame(deck_link_t *c, const agentsdeck_link_v1_Frame *f)
{
	(void)c;
	if (got_n < 16)
		got[got_n] = *f;
	got_n++;
}
static void rec_welcome(deck_link_t *c, const char *n, agentsdeck_link_v1_Scope s, bool a)
{
	(void)c, (void)n, (void)s, (void)a;
	ev_welcome++;
}
static void rec_online(deck_link_t *c) { (void)c, ev_online++; }
static void rec_offline(deck_link_t *c) { (void)c, ev_offline++; }
static void rec_result(deck_link_t *c, uint32_t seq, uint32_t code, const char *e)
{
	(void)c, (void)seq, (void)e;
	ev_result++;
	last_result_code = code;
}
static void rec_agent(deck_link_t *c, const agentsdeck_link_v1_Agent *a) { (void)c, (void)a, ev_agents++; }
static void rec_removed(deck_link_t *c, const char *id) { (void)c, (void)id, ev_removed++; }
static void rec_sb(deck_link_t *c, const agentsdeck_link_v1_Server *s, uint32_t n) { (void)c, (void)s, (void)n, ev_snap_begin++; }
static void rec_se(deck_link_t *c) { (void)c, ev_snap_end++; }

static deck_link_t link;

static void reset_fake(void)
{
	tx_len = 0;
	clock_ms = 1000;
	got_n = 0;
	ev_welcome = ev_online = ev_offline = ev_result = ev_agents = ev_removed = ev_snap_begin = ev_snap_end = 0;
	deck_link_config_t cfg;
	memset(&cfg, 0, sizeof cfg);
	cfg.write = fake_write;
	cfg.millis = fake_millis;
	cfg.board_id = "AA:BB:CC:DD:EE:FF";
	cfg.type = "test";
	cfg.firmware = "0.0.1";
	cfg.fields = agentsdeck_link_v1_FieldGroup_FIELDS_TILE | agentsdeck_link_v1_FieldGroup_FIELDS_PROMPT;
	cfg.on_frame = rec_frame;
	cfg.on_welcome = rec_welcome;
	cfg.on_online = rec_online;
	cfg.on_offline = rec_offline;
	cfg.on_result = rec_result;
	cfg.on_agent = rec_agent;
	cfg.on_agent_removed = rec_removed;
	cfg.on_snapshot_begin = rec_sb;
	cfg.on_snapshot_end = rec_se;
	deck_link_init(&link, &cfg);
}

/* Decodes the frames the board wrote (like the deck would). */
static int sent_frames(agentsdeck_link_v1_Frame *out, int max)
{
	int n = 0;
	size_t start = 0;
	for (size_t i = 0; i < tx_len; i++) {
		if (tx_buf[i] != 0)
			continue;
		static uint8_t raw[8192];
		size_t k;
		if (deck_cobs_decode(tx_buf + start, i - start, raw, &k) && k >= 5 && raw[0] == 0xDE && raw[1] == 0xC1 &&
		    raw[2] == 1 && deck_crc16(raw, k - 2) == (uint16_t)(raw[k - 2] << 8 | raw[k - 1]) && n < max) {
			pb_istream_t s = pb_istream_from_buffer(raw + 3, k - 5);
			memset(&out[n], 0, sizeof out[n]);
			if (pb_decode(&s, agentsdeck_link_v1_Frame_fields, &out[n]))
				n++;
		}
		start = i + 1;
	}
	return n;
}

/* Encodes a deck→board frame from a full Frame struct (like the Go side). */
static size_t encode_frame(const agentsdeck_link_v1_Frame *f, uint8_t *out, size_t size)
{
	static uint8_t raw[8192];
	raw[0] = 0xDE, raw[1] = 0xC1, raw[2] = 1;
	pb_ostream_t s = pb_ostream_from_buffer(raw + 3, sizeof raw - 5);
	if (!pb_encode(&s, agentsdeck_link_v1_Frame_fields, f))
		return 0;
	size_t n = 3 + s.bytes_written;
	uint16_t crc = deck_crc16(raw, n);
	raw[n++] = (uint8_t)(crc >> 8);
	raw[n++] = (uint8_t)crc;
	if (DECK_COBS_MAX(n) + 1 > size)
		return 0;
	size_t m = deck_cobs_encode(raw, n, out);
	out[m++] = 0;
	return m;
}

static void feed_frame(const agentsdeck_link_v1_Frame *f)
{
	static uint8_t w[8192];
	size_t n = encode_frame(f, w, sizeof w);
	CHECK(n > 0);
	deck_link_feed(&link, w, n);
}

static agentsdeck_link_v1_Frame mk(uint32_t seq, pb_size_t which)
{
	agentsdeck_link_v1_Frame f;
	memset(&f, 0, sizeof f);
	f.seq = seq;
	f.which_body = which;
	return f;
}

static agentsdeck_link_v1_Frame mk_agent(const char *id, uint32_t slot, agentsdeck_link_v1_Status st, const char *title)
{
	agentsdeck_link_v1_Frame f = mk(0, agentsdeck_link_v1_Frame_agent_changed_tag);
	agentsdeck_link_v1_Agent *a = &f.body.agent_changed.agent;
	f.body.agent_changed.has_agent = true;
	strcpy(a->id, id);
	a->slot = slot;
	a->status = st;
	strcpy(a->title, title);
	return f;
}

/* ---- stream decoding ------------------------------------------------------- */

static void test_stream(void)
{
	static uint8_t stream[16384];
	size_t n = 0;
#define PUT(p, len)                                \
	do {                                       \
		memcpy(stream + n, (p), (len));    \
		n += (len);                        \
	} while (0)
#define PUTS(s) PUT(s, strlen(s))

	agentsdeck_link_v1_Frame f1 = mk(0, agentsdeck_link_v1_Frame_welcome_tag);
	strcpy(f1.body.welcome.device_name, "tile-1");
	f1.body.welcome.approved = true;
	f1.body.welcome.scope = agentsdeck_link_v1_Scope_SCOPE_ACT;

	/* Result code 10 and a title with newlines: 0x0A inside the frame. */
	agentsdeck_link_v1_Frame f2 = mk(10, agentsdeck_link_v1_Frame_result_tag);
	f2.body.result.code = 10;
	strcpy(f2.body.result.error, "line one\nline two\n");

	agentsdeck_link_v1_Frame f3 = mk_agent("k3f9a2", 3, agentsdeck_link_v1_Status_STATUS_WAITING, "fix\nbug");
	agentsdeck_link_v1_Agent *a3 = &f3.body.agent_changed.agent;
	a3->has_waiting = true;
	strcpy(a3->waiting.id, "p17");
	a3->waiting.options_count = 2;
	strcpy(a3->waiting.options[0].key, "1");
	strcpy(a3->waiting.options[0].label, "Yes");
	a3->waiting.options[0].primary = true;
	strcpy(a3->waiting.options[1].key, "2");
	strcpy(a3->waiting.options[1].label, "No");

	uint8_t w[8192];
	size_t m;

	PUTS("ets Jun  8 2016 00:22:57\r\nrst:0x1 (POWERON_RESET),boot:0x13 (SPI_FAST_FLASH_BOOT)\r\n"
	     "load:0x3fff0030,len:1184\r\nentry 0x400805e4\r\nE (12) boot: something\r\n");
	m = encode_frame(&f1, w, sizeof w);
	PUT(w, m);
	PUTS("{\"seq\":1,\"ping\":{\"timeMs\":5}}\n");
	m = encode_frame(&f2, w, sizeof w);
	CHECK(memchr(w, 0x0A, m) != NULL);
	PUT(w, m);
	static const uint8_t garbage[] = {0xFF, 0xFE, 0x00, 0x13, 0xDE, 0xC1, 0x01, 0x08, 0x09, 0x77, 0x00,
					  0x01, 0x02, 0x03, 0x0A, 0x0D, 0x00, 0x00, 0xC1, 0xDE};
	PUT(garbage, sizeof garbage);
	m = encode_frame(&f3, w, sizeof w);
	PUT(w, m);

	for (int mode = 0; mode < 2; mode++) {
		reset_fake();
		if (mode == 0) {
			deck_link_feed(&link, stream, n);
		} else {
			for (size_t i = 0; i < n; i++)
				deck_link_feed(&link, stream + i, 1);
		}
		CHECK(got_n == 3);
		CHECK(got[0].which_body == agentsdeck_link_v1_Frame_welcome_tag);
		CHECK(strcmp(got[0].body.welcome.device_name, "tile-1") == 0);
		CHECK(got[1].which_body == agentsdeck_link_v1_Frame_result_tag && got[1].seq == 10);
		CHECK(strcmp(got[1].body.result.error, "line one\nline two\n") == 0);
		CHECK(got[2].which_body == agentsdeck_link_v1_Frame_agent_changed_tag);
		CHECK(strcmp(got[2].body.agent_changed.agent.title, "fix\nbug") == 0);
		CHECK(got[2].body.agent_changed.agent.waiting.options_count == 2);
		CHECK(deck_link_stats(&link)->rx_bad == 1); /* the fake frame in the garbage */
		CHECK(ev_welcome == 1 && ev_result == 1 && ev_agents == 1 && last_result_code == 10);
	}
#undef PUT
#undef PUTS
}

/* A largest-possible AgentChanged decodes; an oversized frame is dropped
 * and the next frame still arrives. */
static void test_sizes(void)
{
	static agentsdeck_link_v1_Frame f;
	memset(&f, 0, sizeof f);
	f.which_body = agentsdeck_link_v1_Frame_agent_changed_tag;
	f.body.agent_changed.has_agent = true;
	agentsdeck_link_v1_Agent *a = &f.body.agent_changed.agent;
	memset(a->id, 'a', sizeof a->id - 1);
	memset(a->title, 'b', sizeof a->title - 1);
	memset(a->ai_title, 'c', sizeof a->ai_title - 1);
	memset(a->cwd, 'd', sizeof a->cwd - 1);
	memset(a->last_prompt, 'e', sizeof a->last_prompt - 1);
	memset(a->error, 'f', sizeof a->error - 1);
#define FILL(field, ch) memset((field), (ch), sizeof(field) - 1)
	FILL(a->folder, 'j');
	FILL(a->branch, 'k');
	FILL(a->model, 'l');
	FILL(a->model_label, 'm');
	FILL(a->effort, 'n');
	a->has_activity = true;
	FILL(a->activity.tool, 'o');
	FILL(a->activity.detail, 'p');
	a->has_hat = true;
	FILL(a->hat.shape, 'q');
	FILL(a->hat.color, 'r');
	a->has_waiting = true;
	FILL(a->waiting.id, 's');
	FILL(a->waiting.title, 't');
	FILL(a->waiting.detail, 'g');
	FILL(a->waiting.context, 'u');
	a->waiting.options_count = 6;
	for (int i = 0; i < 6; i++) {
		FILL(a->waiting.options[i].key, 'v');
		FILL(a->waiting.options[i].label, 'h');
	}
	a->subagents_count = 6;
	for (int i = 0; i < 6; i++) {
		FILL(a->subagents[i].id, 'w');
		FILL(a->subagents[i].title, 'i');
		FILL(a->subagents[i].type, 'x');
		FILL(a->subagents[i].tool, 'y');
	}
#undef FILL
	a->status_since_ms = a->started_at_ms = a->updated_at_ms = -1;
	a->has_tokens = a->has_context = true;
	a->tokens.input = a->tokens.output = a->tokens.cache_read = a->tokens.cache_write = -1;
	a->context.used = a->context.window = -1;
	a->cost_usd = 1.5;
	a->turns = a->subagent_count = 0xFFFFFFFFu;
	a->focused = a->attached = a->resumable = a->lost = a->unseen = a->external = true;
	a->tool = agentsdeck_link_v1_Tool_TOOL_SIM;
	a->status = agentsdeck_link_v1_Status_STATUS_EXITED;

	static uint8_t w[8192];
	size_t m = encode_frame(&f, w, sizeof w);
	CHECK(m > 3300); /* agentsdeck_link_v1_Frame_size + 5 + COBS */

	reset_fake();
	deck_link_feed(&link, w, m);
	CHECK(got_n == 1 && ev_agents == 1);
	CHECK(strlen(link.frame.body.agent_changed.agent.waiting.detail) == 511);

	/* A 5000-byte frame (beyond DECK_LINK_MAX_FRAME) is dropped. */
	static uint8_t raw[6000], big[6100];
	raw[0] = 0xDE, raw[1] = 0xC1, raw[2] = 1;
	for (size_t i = 3; i < 5000; i++)
		raw[i] = (uint8_t)(i | 1);
	size_t bn = deck_cobs_encode(raw, 5000, big);
	big[bn++] = 0;
	reset_fake();
	deck_link_feed(&link, big, bn);
	agentsdeck_link_v1_Frame r = mk(4, agentsdeck_link_v1_Frame_result_tag);
	r.body.result.code = 204;
	feed_frame(&r);
	CHECK(got_n == 1 && last_result_code == 204);
	CHECK(deck_link_stats(&link)->rx_overflow == 1);

	/* Lots of noise without a 0x00 in front of a frame. */
	reset_fake();
	static uint8_t noise[12000];
	for (size_t i = 0; i < sizeof noise; i++)
		noise[i] = (uint8_t)('a' + i % 26);
	deck_link_feed(&link, noise, sizeof noise);
	feed_frame(&r);
	CHECK(got_n == 1);

	printf("sizeof(deck_link_t) = %zu, sizeof(agentsdeck_link_v1_Frame) = %zu, sizeof(deck_tiles_t) = %zu, "
	       "max deck frame here = %zu bytes on the wire\n",
	       sizeof(deck_link_t), sizeof(agentsdeck_link_v1_Frame), sizeof(deck_tiles_t), m);
}

/* ---- handshake and timers -------------------------------------------------- */

static void test_handshake(void)
{
	static agentsdeck_link_v1_Frame s[16];
	reset_fake();
	deck_link_begin(&link);
	CHECK(sent_frames(s, 16) == 1);
	CHECK(s[0].which_body == agentsdeck_link_v1_Frame_hello_tag);
	CHECK(s[0].body.hello.protocol == 1 && strcmp(s[0].body.hello.board_id, "AA:BB:CC:DD:EE:FF") == 0);
	CHECK(s[0].body.hello.max_frame == DECK_LINK_MAX_FRAME && !s[0].body.hello.has_display);
	CHECK(deck_link_state(&link) == DECK_LINK_CONNECTING);

	/* Hello again every 2 s until a Welcome. */
	clock_ms += 1999;
	deck_link_poll(&link);
	CHECK(sent_frames(s, 16) == 1);
	clock_ms += 1;
	deck_link_poll(&link);
	CHECK(sent_frames(s, 16) == 2 && s[1].which_body == agentsdeck_link_v1_Frame_hello_tag);
	CHECK(s[1].seq != s[0].seq);

	/* Unapproved Welcome: online, no Subscribe, no more Hellos. */
	agentsdeck_link_v1_Frame w = mk(0, agentsdeck_link_v1_Frame_welcome_tag);
	w.body.welcome.approved = false;
	tx_len = 0;
	feed_frame(&w);
	CHECK(ev_online == 1 && ev_welcome == 1 && !deck_link_approved(&link));
	clock_ms += 3000;
	deck_link_poll(&link);
	CHECK(sent_frames(s, 16) == 0);

	/* Approved: Subscribe with the configured fields. */
	w.body.welcome.approved = true;
	w.body.welcome.scope = agentsdeck_link_v1_Scope_SCOPE_ACT;
	feed_frame(&w);
	CHECK(ev_online == 1 && ev_welcome == 2 && deck_link_approved(&link));
	CHECK(deck_link_scope(&link) == agentsdeck_link_v1_Scope_SCOPE_ACT);
	CHECK(sent_frames(s, 16) == 1 && s[0].which_body == agentsdeck_link_v1_Frame_subscribe_tag);
	CHECK(s[0].body.subscribe.fields == 5);

	/* Ping → Pong, same seq and time. */
	tx_len = 0;
	agentsdeck_link_v1_Frame p = mk(77, agentsdeck_link_v1_Frame_ping_tag);
	p.body.ping.time_ms = 1700000000123ull;
	feed_frame(&p);
	CHECK(sent_frames(s, 16) == 1 && s[0].which_body == agentsdeck_link_v1_Frame_pong_tag);
	CHECK(s[0].seq == 77 && s[0].body.pong.time_ms == 1700000000123ull);

	/* Commands: seqs increase and are echoed. */
	tx_len = 0;
	uint32_t q1 = deck_answer(&link, "k3f9a2", "p17", "1");
	uint32_t q2 = deck_focus(&link, "k3f9a2");
	const char *ids[] = {"a", "b", "c"};
	uint32_t q3 = deck_order(&link, ids, 3);
	uint32_t q4 = deck_set_hat(&link, NULL, "/x/y", "crown", "red", false);
	uint32_t q5 = deck_add_live(&link, "sess-1", -1);
	uint32_t q6 = deck_get_state(&link);
	CHECK(q1 && q2 == q1 + 1 && q3 == q2 + 1 && q4 == q3 + 1 && q5 == q4 + 1 && q6 == q5 + 1);
	CHECK(sent_frames(s, 16) == 6);
	CHECK(s[0].which_body == agentsdeck_link_v1_Frame_answer_tag && s[0].seq == q1);
	CHECK(strcmp(s[0].body.answer.prompt_id, "p17") == 0 && strcmp(s[0].body.answer.key, "1") == 0);
	CHECK(s[1].which_body == agentsdeck_link_v1_Frame_focus_tag && strcmp(s[1].body.focus.agent_id, "k3f9a2") == 0);
	CHECK(s[2].body.order.agent_ids_count == 3 && strcmp(s[2].body.order.agent_ids[2], "c") == 0);
	CHECK(s[3].which_body == agentsdeck_link_v1_Frame_set_hat_tag && strcmp(s[3].body.set_hat.folder, "/x/y") == 0);
	CHECK(s[4].body.add_live.slot == -1);
	CHECK(s[5].which_body == agentsdeck_link_v1_Frame_get_state_tag);

	/* Silence for 15 s: offline, Hello now and every 5 s. */
	tx_len = 0;
	clock_ms += 14999;
	deck_link_poll(&link);
	CHECK(ev_offline == 0 && sent_frames(s, 16) == 0);
	clock_ms += 1;
	deck_link_poll(&link);
	CHECK(ev_offline == 1 && deck_link_state(&link) == DECK_LINK_OFFLINE);
	CHECK(sent_frames(s, 16) == 1 && s[0].which_body == agentsdeck_link_v1_Frame_hello_tag);
	clock_ms += 4999;
	deck_link_poll(&link);
	CHECK(sent_frames(s, 16) == 1);
	clock_ms += 1;
	deck_link_poll(&link);
	CHECK(sent_frames(s, 16) == 2);

	/* The deck answers: online again, subscribed again. */
	tx_len = 0;
	feed_frame(&w);
	CHECK(ev_online == 2 && deck_link_online(&link));
	CHECK(sent_frames(s, 16) == 1 && s[0].which_body == agentsdeck_link_v1_Frame_subscribe_tag);

	/* deck_subscribe replaces what later Welcomes send. */
	deck_subscribe(&link, 2, 4, 1);
	tx_len = 0;
	feed_frame(&w);
	CHECK(sent_frames(s, 16) == 1 && s[0].body.subscribe.first_slot == 2 && s[0].body.subscribe.slot_count == 4);

	/* Long strings are cut on a UTF-8 boundary. */
	tx_len = 0;
	deck_answer(&link, "abcdefghij\xc3\xa9xyz", "p", "k");
	CHECK(sent_frames(s, 16) == 1 && strcmp(s[0].body.answer.agent_id, "abcdefghij") == 0);
}

/* ---- tiles ----------------------------------------------------------------- */

static void test_tiles(void)
{
	static deck_tiles_t t;
	deck_tiles_init(&t);
	agentsdeck_link_v1_Frame f;

	f = mk(0, agentsdeck_link_v1_Frame_snapshot_begin_tag);
	deck_tiles_handle(&t, &f);
	f = mk_agent("b", 2, agentsdeck_link_v1_Status_STATUS_RUNNING, "two");
	deck_tiles_handle(&t, &f);
	f = mk_agent("a", 0, agentsdeck_link_v1_Status_STATUS_IDLE, "zero");
	deck_tiles_handle(&t, &f);
	f = mk_agent("c", 1, agentsdeck_link_v1_Status_STATUS_WAITING, "one");
	f.body.agent_changed.agent.has_waiting = true;
	strcpy(f.body.agent_changed.agent.waiting.id, "p1");
	f.body.agent_changed.agent.waiting.options_count = 2;
	strcpy(f.body.agent_changed.agent.waiting.options[0].key, "n");
	strcpy(f.body.agent_changed.agent.waiting.options[1].key, "y");
	f.body.agent_changed.agent.waiting.options[1].primary = true;
	f.body.agent_changed.agent.has_context = true;
	f.body.agent_changed.agent.context.used = 50000;
	f.body.agent_changed.agent.context.window = 200000;
	deck_tiles_handle(&t, &f);
	f = mk(0, agentsdeck_link_v1_Frame_snapshot_end_tag);
	deck_tiles_handle(&t, &f);

	CHECK(t.count == 3);
	CHECK(strcmp(t.tiles[0].id, "a") == 0 && strcmp(t.tiles[1].id, "c") == 0 && strcmp(t.tiles[2].id, "b") == 0);
	const deck_tile_t *wt = deck_tiles_first_waiting(&t);
	CHECK(wt && strcmp(wt->id, "c") == 0 && deck_tile_primary(wt) == 1 && wt->context_permille == 250);
	CHECK(deck_tiles_count_status(&t, agentsdeck_link_v1_Status_STATUS_RUNNING) == 1);

	/* Move b to slot 0, remove a. */
	f = mk_agent("b", 0, agentsdeck_link_v1_Status_STATUS_RUNNING, "two");
	deck_tiles_handle(&t, &f);
	f = mk(0, agentsdeck_link_v1_Frame_agent_removed_tag);
	strcpy(f.body.agent_removed.id, "a");
	deck_tiles_handle(&t, &f);
	CHECK(t.count == 2 && strcmp(t.tiles[0].id, "b") == 0);

	/* A new snapshot without c drops it. */
	f = mk(0, agentsdeck_link_v1_Frame_snapshot_begin_tag);
	deck_tiles_handle(&t, &f);
	f = mk_agent("b", 0, agentsdeck_link_v1_Status_STATUS_IDLE, "two");
	deck_tiles_handle(&t, &f);
	f = mk(0, agentsdeck_link_v1_Frame_snapshot_end_tag);
	deck_tiles_handle(&t, &f);
	CHECK(t.count == 1 && t.tiles[0].status == agentsdeck_link_v1_Status_STATUS_IDLE);

	/* Overfill: the highest slots are dropped. */
	deck_tiles_clear(&t);
	for (int i = DECK_TILES_MAX + 3; i >= 0; i--) {
		char id[8];
		snprintf(id, sizeof id, "x%d", i);
		f = mk_agent(id, (uint32_t)i, agentsdeck_link_v1_Status_STATUS_IDLE, id);
		deck_tiles_handle(&t, &f);
	}
	CHECK(t.count == DECK_TILES_MAX && t.tiles[0].slot == 0 && t.tiles[DECK_TILES_MAX - 1].slot == DECK_TILES_MAX - 1);
}

int main(void)
{
	test_crc();
	test_cobs();
	test_ping_wire();
	test_stream();
	test_sizes();
	test_handshake();
	test_tiles();
	if (failures) {
		printf("FAIL: %d of %d checks failed\n", failures, checks);
		return 1;
	}
	printf("ok: %d checks passed\n", checks);
	return 0;
}
