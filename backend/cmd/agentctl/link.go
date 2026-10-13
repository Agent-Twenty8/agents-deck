package main

import (
	"bufio"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"os"
	"strconv"
	"strings"
	"text/tabwriter"
	"time"

	"github.com/brushknight/agents-deck/backend/internal/link"
	"github.com/brushknight/agents-deck/backend/internal/link/linkpb"
	"github.com/brushknight/agents-deck/backend/internal/paths"
)

func linkCmd(args []string) error {
	if len(args) == 0 {
		return linkStatus()
	}
	switch args[0] {
	case "status":
		return linkStatus()
	case "ports":
		return linkPorts()
	case "add":
		return linkAdd(args[1:])
	case "rm", "remove":
		if len(args) != 2 {
			return errors.New("usage: agentctl link rm <port>")
		}
		return editLinkPorts(func(ps []link.PortConfig) ([]link.PortConfig, error) {
			out := ps[:0]
			found := false
			for _, p := range ps {
				if p.Path == args[1] {
					found = true
					continue
				}
				out = append(out, p)
			}
			if !found {
				return nil, fmt.Errorf("%s isn't registered", args[1])
			}
			fmt.Printf("%s removed: the daemon lets go of it within 2 seconds\n", args[1])
			return out, nil
		})
	case "sim":
		return linkSim(args[1:])
	case "check":
		return linkCheck(args[1:])
	}
	return errors.New("usage: agentctl link [status | ports | add <port> [--baud N] | rm <port> | sim | check <port>]")
}

func linkStatus() error {
	var res struct{ Ports []link.PortStatus }
	if err := call("GET", "/local/link", nil, &res); err != nil {
		return err
	}
	if len(res.Ports) == 0 {
		fmt.Println("no serial ports registered: plug a board in, then `agentctl link ports` and `agentctl link add <port>`")
		return nil
	}
	tw := tabwriter.NewWriter(os.Stdout, 0, 2, 2, ' ', 0)
	fmt.Fprintln(tw, "PORT\tBAUD\tSTATE\tBOARD\tSINCE")
	for _, p := range res.Ports {
		state := p.State
		if p.Problem != "" {
			state += ": " + p.Problem
		}
		since := ""
		if !p.Since.IsZero() {
			since = link.Ago(p.Since)
		}
		fmt.Fprintf(tw, "%s\t%d\t%s\t%s\t%s\n", p.Path, p.Baud, state, p.Board, since)
	}
	return tw.Flush()
}

func linkPorts() error {
	registered := map[string]bool{}
	for _, p := range loadConfig().Link.Ports {
		registered[p.Path] = true
	}
	ports := link.CandidatePorts()
	if len(ports) == 0 {
		fmt.Println("no USB serial ports found: plug the board in (and use a data cable)")
		return nil
	}
	for _, p := range ports {
		mark := "  "
		if registered[p] {
			mark = "✓ "
		}
		fmt.Println(mark + p)
	}
	fmt.Println("\nregister one with: agentctl link add <port>   (only registered ports are ever opened)")
	return nil
}

func linkAdd(args []string) error {
	fs := flag.NewFlagSet("link add", flag.ExitOnError)
	baud := fs.Int("baud", 115200, "baud rate (boards with native USB ignore it)")
	var port string
	if len(args) > 0 && !strings.HasPrefix(args[0], "-") {
		port, args = args[0], args[1:]
	}
	_ = fs.Parse(args)
	if port == "" {
		return errors.New("usage: agentctl link add <port> [--baud N]   (see agentctl link ports)")
	}
	if _, err := os.Stat(port); err != nil {
		fmt.Fprintf(os.Stderr, "note: %s isn't there right now; it's used whenever it appears\n", port)
	}
	return editLinkPorts(func(ps []link.PortConfig) ([]link.PortConfig, error) {
		for i, p := range ps {
			if p.Path == port {
				ps[i].Baud = *baud
				fmt.Printf("%s: baud %d\n", port, *baud)
				return ps, nil
			}
		}
		fmt.Printf("%s registered: the daemon opens it within 2 seconds. A new board then waits for `agentctl devices approve <name>`.\n", port)
		return append(ps, link.PortConfig{Path: port, Baud: *baud}), nil
	})
}

// editLinkPorts rewrites config.json "link.ports", keeping every other key.
func editLinkPorts(fn func([]link.PortConfig) ([]link.PortConfig, error)) error {
	if err := paths.Ensure(); err != nil {
		return err
	}
	cfg := map[string]any{}
	if b, err := os.ReadFile(paths.Config()); err == nil {
		if err := json.Unmarshal(b, &cfg); err != nil {
			return fmt.Errorf("config.json is not valid JSON: %v", err)
		}
	}
	ports, err := fn(loadConfig().Link.Ports)
	if err != nil {
		return err
	}
	l, _ := cfg["link"].(map[string]any)
	if l == nil {
		l = map[string]any{}
	}
	l["ports"] = ports
	cfg["link"] = l
	b, err := json.MarshalIndent(cfg, "", "  ")
	if err != nil {
		return err
	}
	tmp := paths.Config() + ".tmp"
	if err := os.WriteFile(tmp, append(b, '\n'), 0o600); err != nil {
		return err
	}
	return os.Rename(tmp, paths.Config())
}

func parseFields(s string) (uint32, error) {
	var f uint32
	for _, part := range strings.Split(s, ",") {
		switch strings.TrimSpace(part) {
		case "tile":
			f |= uint32(linkpb.FieldGroup_FIELDS_TILE)
		case "card":
			f |= uint32(linkpb.FieldGroup_FIELDS_CARD)
		case "prompt":
			f |= uint32(linkpb.FieldGroup_FIELDS_PROMPT)
		case "numbers":
			f |= uint32(linkpb.FieldGroup_FIELDS_NUMBERS)
		case "":
		default:
			return 0, fmt.Errorf("unknown field group %q (tile, card, prompt, numbers)", part)
		}
	}
	return f, nil
}

func linkSim(args []string) error {
	fs := flag.NewFlagSet("link sim", flag.ExitOnError)
	id := fs.String("id", "sim-board", "board id (a new id is a new board to approve)")
	fields := fs.String("fields", "tile,prompt", "field groups: tile, card, prompt, numbers")
	asJSON := fs.Bool("json", false, "speak the legacy JSON-lines format")
	_ = fs.Parse(args)
	f, err := parseFields(*fields)
	if err != nil {
		return err
	}
	conn, err := link.DialSim(paths.Socket())
	if err != nil {
		return fmt.Errorf("is the daemon running? %w", err)
	}
	defer conn.Close()
	b := link.NewBoard(conn, &linkpb.Hello{Protocol: 1, BoardId: *id, Type: "sim", Firmware: version}, f)
	if *asJSON {
		b.Mode = link.JSON
	}
	b.Event = func(f *linkpb.Frame) {
		if line := link.Describe(f); line != "" {
			fmt.Println(line)
		}
	}
	done := make(chan error, 1)
	go func() { done <- b.Run() }()
	fmt.Fprintln(os.Stderr, "a simulated board on the deck. commands: answer <agent> <key>, focus|interrupt|dismiss|resume <agent>, move <agent> <slot>, hats, live, state, quit")
	go func() {
		sc := bufio.NewScanner(os.Stdin)
		for sc.Scan() {
			w := strings.Fields(sc.Text())
			if len(w) == 0 {
				continue
			}
			if err := simCommand(b, w); err != nil {
				fmt.Fprintln(os.Stderr, err)
			}
		}
		done <- nil
	}()
	return <-done
}

func simCommand(b *link.Board, w []string) error {
	agent := func() (*linkpb.Agent, error) {
		if len(w) < 2 {
			return nil, fmt.Errorf("usage: %s <agent id or title>", w[0])
		}
		if a := b.Agent(w[1]); a != nil {
			return a, nil
		}
		return nil, fmt.Errorf("no agent %q on this board", w[1])
	}
	switch w[0] {
	case "quit", "exit":
		os.Exit(0)
	case "state":
		b.Send(&linkpb.Frame{Body: &linkpb.Frame_GetState{GetState: &linkpb.GetState{}}})
	case "hats":
		b.Send(&linkpb.Frame{Body: &linkpb.Frame_ListHats{ListHats: &linkpb.ListHats{}}})
	case "live":
		b.Send(&linkpb.Frame{Body: &linkpb.Frame_ListLive{ListLive: &linkpb.ListLive{}}})
	case "answer":
		a, err := agent()
		if err != nil {
			return err
		}
		if a.Waiting == nil || len(w) < 3 {
			return errors.New("usage: answer <agent> <key> (the agent must be waiting; subscribe with --fields tile,prompt)")
		}
		b.Send(&linkpb.Frame{Body: &linkpb.Frame_Answer{Answer: &linkpb.Answer{AgentId: a.Id, PromptId: a.Waiting.Id, Key: w[2]}}})
	case "focus", "interrupt", "dismiss", "resume":
		a, err := agent()
		if err != nil {
			return err
		}
		frames := map[string]*linkpb.Frame{
			"focus":     {Body: &linkpb.Frame_Focus{Focus: &linkpb.Focus{AgentId: a.Id}}},
			"interrupt": {Body: &linkpb.Frame_Interrupt{Interrupt: &linkpb.Interrupt{AgentId: a.Id}}},
			"dismiss":   {Body: &linkpb.Frame_Dismiss{Dismiss: &linkpb.Dismiss{AgentId: a.Id}}},
			"resume":    {Body: &linkpb.Frame_Resume{Resume: &linkpb.Resume{AgentId: a.Id}}},
		}
		b.Send(frames[w[0]])
	case "move":
		a, err := agent()
		if err != nil {
			return err
		}
		if len(w) < 3 {
			return errors.New("usage: move <agent> <slot>")
		}
		n, err := strconv.Atoi(w[2])
		if err != nil {
			return err
		}
		b.Send(&linkpb.Frame{Body: &linkpb.Frame_Move{Move: &linkpb.Move{AgentId: a.Id, Slot: uint32(n)}}})
	default:
		return fmt.Errorf("unknown command %q", w[0])
	}
	return nil
}

func linkCheck(args []string) error {
	fs := flag.NewFlagSet("link check", flag.ExitOnError)
	baud := fs.Int("baud", 115200, "baud rate")
	secs := fs.Int("seconds", 20, "how long to watch the board")
	var port string
	if len(args) > 0 && !strings.HasPrefix(args[0], "-") {
		port, args = args[0], args[1:]
	}
	_ = fs.Parse(args)
	if port == "" {
		return errors.New("usage: agentctl link check <port> [--baud N] [--seconds N]")
	}
	for _, p := range loadConfig().Link.Ports {
		if p.Path == port {
			return fmt.Errorf("the daemon uses %s: `agentctl link rm %s` first (add it back after)", port, port)
		}
	}
	f, err := link.OpenPort(port, *baud)
	if err != nil {
		return err
	}
	defer f.Close()
	fails, err := link.Check(f, os.Stdout, time.Duration(*secs)*time.Second)
	if err != nil {
		return err
	}
	if fails > 0 {
		return fmt.Errorf("%d checks failed", fails)
	}
	fmt.Println("all checks passed")
	return nil
}
