package link

import (
	"context"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"sync"
	"testing/fstest"
	"time"

	"github.com/brushknight/agents-deck/backend/internal/demo"
	"github.com/brushknight/agents-deck/backend/internal/devices"
	"github.com/brushknight/agents-deck/backend/internal/link/linkpb"
	"github.com/brushknight/agents-deck/backend/internal/server"
	"github.com/brushknight/agents-deck/backend/internal/store"
)

// lockedRW serialises writes, so Check can inject noise between the
// session's frames without cutting one in half.
type lockedRW struct {
	io.Reader
	w  io.Writer
	mu sync.Mutex
}

func (l *lockedRW) Write(b []byte) (int, error) {
	l.mu.Lock()
	defer l.mu.Unlock()
	return l.w.Write(b)
}

// Check runs a conformance check against a board on rw: it plays a deck
// with the demo fleet (nothing reaches your real agents), approves the board
// with admin scope, and reports what the board does right and wrong. It
// returns the number of failed checks.
func Check(rw io.ReadWriter, out io.Writer, dur time.Duration) (int, error) {
	dir, err := os.MkdirTemp("", "deck-link-check")
	if err != nil {
		return 0, err
	}
	defer os.RemoveAll(dir)
	st := store.New("", "link check", "demo")
	dm, err := demo.Load(st)
	if err != nil {
		return 0, err
	}
	go dm.Run()
	srv := &server.Server{Store: st, Ctl: dm, Static: fstest.MapFS{},
		Devices: &devices.Registry{Path: filepath.Join(dir, "devices.json")},
		Audit:   &devices.Audit{Path: filepath.Join(dir, "audit.log")}}
	deck := ServerDeck{srv}

	// Watch what the board sends, alongside the session.
	pr, pw := io.Pipe()
	tee := io.TeeReader(rw, pw)
	lrw := &lockedRW{Reader: tee, w: rw}
	type seen struct {
		f  *linkpb.Frame
		at time.Time
	}
	fromBoard := make(chan seen, 64)
	go func() {
		d := &Decoder{}
		buf := make([]byte, 1024)
		for {
			n, err := pr.Read(buf)
			for _, in := range d.Feed(buf[:n]) {
				fromBoard <- seen{in.Frame, time.Now()}
			}
			if err != nil {
				return
			}
		}
	}()

	oldPing, oldRecheck := PingEvery, RecheckEvery
	PingEvery, RecheckEvery = time.Second, 300*time.Millisecond
	defer func() { PingEvery, RecheckEvery = oldPing, oldRecheck }()
	ctx, cancel := context.WithTimeout(context.Background(), dur)
	defer cancel()
	go func() { _ = NewSession(deck, lrw, "check").Run(ctx) }()

	fails := 0
	report := func(ok bool, what string) {
		mark := "ok  "
		if !ok {
			mark = "FAIL"
			fails++
		}
		fmt.Fprintf(out, "%s %s\n", mark, what)
	}
	var hello *linkpb.Hello
	var approvedAt, garbageAt time.Time
	subscribed, pongs, pongAfterGarbage := false, 0, false
	garbage := make(chan time.Time, 1)
	fmt.Fprintf(out, "checking for %s; press the board's buttons to try commands\n", dur)
	for {
		select {
		case <-ctx.Done():
			report(hello != nil, "board says Hello")
			report(subscribed, "board subscribes once approved")
			report(pongs > 0, fmt.Sprintf("board answers pings (%d pongs)", pongs))
			report(pongAfterGarbage, "board recovers from garbage on the line")
			return fails, nil
		case t := <-garbage:
			garbageAt = t
		case s := <-fromBoard:
			f := s.f
			switch x := f.Body.(type) {
			case *linkpb.Frame_Hello:
				h := x.Hello
				if hello == nil {
					fmt.Fprintf(out, "hello: board_id %q, type %q, firmware %q, display %v, inputs %v, max_frame %d\n",
						h.BoardId, h.Type, h.Firmware, h.Display, h.Inputs, h.MaxFrame)
					report(h.Protocol == 1, "protocol 1")
					report(h.BoardId != "", "board_id set")
					report(h.Type != "" && h.Firmware != "", "type and firmware set")
					go func() {
						time.Sleep(time.Second)
						for _, d := range srv.Devices.List() {
							_ = srv.Devices.Approve(d.Name)
							_ = srv.Devices.SetScope(d.Name, devices.Admin)
						}
					}()
					approvedAt = time.Now().Add(time.Second)
				}
				hello = h
			case *linkpb.Frame_Subscribe:
				if !subscribed {
					report(!approvedAt.IsZero() && s.at.Sub(approvedAt) < 5*time.Second, fmt.Sprintf("subscribe: slots %d+%d, fields %b", x.Subscribe.FirstSlot, x.Subscribe.SlotCount, x.Subscribe.Fields))
					subscribed = true
					// Garbage on the line, then the next ping must still be answered.
					go func() {
						time.Sleep(1500 * time.Millisecond)
						lrw.mu.Lock()
						_, _ = rw.Write([]byte("\x01\x02garbage\xDE\xC1\x01\x99\x00rst:0x1 (noise)\r\n"))
						lrw.mu.Unlock()
						garbage <- time.Now()
					}()
				}
			case *linkpb.Frame_Pong:
				pongs++
				if !garbageAt.IsZero() && s.at.After(garbageAt) {
					pongAfterGarbage = true
				}
			case *linkpb.Frame_Ping:
			default:
				fmt.Fprintf(out, "command #%d: %v\n", f.Seq, f.Body)
			}
		}
	}
}
