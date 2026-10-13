package link

import (
	"bufio"
	"context"
	"io"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/brushknight/agents-deck/backend/internal/devices"
)

// The C firmware kit's board_sim (firmware/deck-link/test/host) against the
// real session: handshake, approval, snapshot, and an answer through the web
// API. Skipped without make and a C compiler.
func TestFirmwareKitAgainstTheDeck(t *testing.T) {
	host, _ := filepath.Abs(repo + "/firmware/deck-link/test/host")
	if _, err := exec.LookPath("make"); err != nil {
		t.Skip("no make")
	}
	if out, err := exec.Command("make", "-s", "-C", host, "board_sim").CombinedOutput(); err != nil {
		t.Skipf("can't build board_sim: %v\n%s", err, out)
	}
	srv, ctl := testDeck(t)
	RecheckEvery, Debounce = 50*time.Millisecond, 20*time.Millisecond
	cmd := exec.Command(filepath.Join(host, "build", "board_sim"), "--board-id", "AA:BB:CC:00:11:22", "--type", "oled-tile",
		"--answer-primary", "--log", "--seconds", "5")
	in, _ := cmd.StdinPipe()
	out, _ := cmd.StdoutPipe()
	errp, _ := cmd.StderrPipe()
	if err := cmd.Start(); err != nil {
		t.Fatal(err)
	}
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	go func() {
		_ = NewSession(ServerDeck{srv}, struct {
			io.Reader
			io.Writer
		}{out, in}, "board_sim").Run(ctx)
	}()
	var log []string
	approved := false
	sc := bufio.NewScanner(errp)
	for sc.Scan() {
		l := sc.Text()
		log = append(log, l)
		if !approved && strings.HasPrefix(l, "welcome ") && strings.Contains(l, "approved=0") {
			approved = true
			go func() {
				_ = srv.Devices.Approve("oled-tile-001122")
				_ = srv.Devices.SetScope("oled-tile-001122", devices.Act)
			}()
		}
	}
	_ = cmd.Wait()
	all := strings.Join(log, "\n")
	for _, want := range []string{
		"welcome name=oled-tile-001122 scope=1 approved=0",
		"welcome name=oled-tile-001122 scope=2 approved=1",
		"snapshot_begin server=test-mac agents=3",
		"agent id=a1 slot=0 status=waiting prompt=p1 options=2",
		"answer seq=",
		"code=204",
	} {
		if !strings.Contains(all, want) {
			t.Errorf("board_sim log lacks %q:\n%s", want, all)
		}
	}
	if len(ctl.calls) == 0 || ctl.calls[0] != "answer a1 p1 1" {
		t.Errorf("the deck got %v, want the primary option of a1's prompt", ctl.calls)
	}
}
