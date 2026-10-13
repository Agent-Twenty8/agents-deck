package link

import (
	"bytes"
	"net"
	"strings"
	"testing"
	"time"

	"github.com/brushknight/agents-deck/backend/internal/link/linkpb"
)

// The Go board passes the conformance check, in both modes.
func TestCheckPassesForAGoodBoard(t *testing.T) {
	for _, mode := range []Mode{Binary, JSON} {
		deckEnd, boardEnd := net.Pipe()
		b := NewBoard(boardEnd, &linkpb.Hello{Protocol: 1, BoardId: "check-board", Type: "sim", Firmware: "1"}, 1|4)
		b.Mode = mode
		go func() { _ = b.Run() }()
		var out bytes.Buffer
		fails, err := Check(deckEnd, &out, 5*time.Second)
		deckEnd.Close()
		boardEnd.Close()
		if err != nil || fails != 0 {
			t.Fatalf("mode %d: %d fails, %v\n%s", mode, fails, err, out.String())
		}
		if !strings.Contains(out.String(), "ok   board recovers from garbage") {
			t.Fatalf("report:\n%s", out.String())
		}
	}
}

// A board that never subscribes fails the check.
func TestCheckCatchesASilentBoard(t *testing.T) {
	deckEnd, boardEnd := net.Pipe()
	defer deckEnd.Close()
	defer boardEnd.Close()
	go func() {
		w, _ := Encode(&linkpb.Frame{Seq: 1, Body: &linkpb.Frame_Hello{Hello: &linkpb.Hello{Protocol: 1, BoardId: "lazy", Type: "x", Firmware: "1"}}})
		_, _ = boardEnd.Write(w)
		buf := make([]byte, 1024)
		for {
			if _, err := boardEnd.Read(buf); err != nil {
				return
			}
		}
	}()
	var out bytes.Buffer
	fails, _ := Check(deckEnd, &out, 3*time.Second)
	if fails < 2 {
		t.Fatalf("expected failures:\n%s", out.String())
	}
}
