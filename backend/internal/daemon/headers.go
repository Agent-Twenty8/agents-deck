package daemon

import (
	"encoding/json"
	"log"
	"os"
	"strconv"
	"time"

	"github.com/brushknight/agents-deck/backend/internal/header"
	"github.com/brushknight/agents-deck/backend/internal/paths"
	"github.com/brushknight/agents-deck/backend/internal/tmux"
)

// Header modes (`agentctl set header …`).
const (
	HeaderOn  = "on"  // a band at the top of each agent terminal: critter, name, status, numbers
	HeaderOff = "off" // no tmux status line, as before
)

// CurrentHeader reads the header mode from config.json (default on).
func CurrentHeader() string {
	var c struct {
		Header string `json:"header"`
	}
	if b, err := os.ReadFile(paths.Config()); err == nil {
		_ = json.Unmarshal(b, &c)
	}
	if c.Header == HeaderOff {
		return HeaderOff
	}
	return HeaderOn
}

// syncHeaders keeps the header of every live agent terminal up to date,
// sending tmux only the lines that changed.
func (d *Daemon) syncHeaders(live map[string]bool) {
	if d.headers == nil {
		d.headers = map[string][header.Lines]string{}
	}
	var cmds [][]string
	on := CurrentHeader() == HeaderOn
	seen := map[string]bool{}
	if on {
		names := map[string]string{} // agent id -> tmux session
		for _, e := range d.Store.Entries() {
			if !e.External && e.TmuxName != "" && live[e.TmuxName] {
				names[e.A.ID] = e.TmuxName
			}
		}
		now := time.Now()
		for _, a := range d.Store.Snapshot().Agents {
			name, ok := names[a.ID]
			if !ok {
				continue
			}
			seen[name] = true
			lines := header.Render(a, now)
			old, had := d.headers[name]
			if !had {
				cmds = append(cmds,
					[]string{"set-option", "-t", name, "status", strconv.Itoa(header.Lines)},
					[]string{"set-option", "-t", name, "status-position", "top"},
					[]string{"set-option", "-t", name, "status-style", "bg=" + header.Bg})
			}
			for i, l := range lines {
				if !had || old[i] != l {
					cmds = append(cmds, []string{"set-option", "-t", name, "status-format[" + strconv.Itoa(i) + "]", l})
				}
			}
			d.headers[name] = lines
		}
	}
	for name := range d.headers {
		if !seen[name] {
			if live[name] { // turned off: back to no status line
				cmds = append(cmds, []string{"set-option", "-t", name, "status", "off"})
			}
			delete(d.headers, name)
		}
	}
	if err := tmux.Batch(cmds); err != nil {
		log.Printf("headers: %v", err)
	}
}
