package devices

import (
	"encoding/json"
	"os"
	"sync"
	"time"
)

// Audit appends one JSON line per action a device took (answer, interrupt,
// dismiss…) to Path, so you can see who did what.
type Audit struct {
	Path string
	mu   sync.Mutex
}

type AuditEntry struct {
	Time   time.Time `json:"time"`
	Device string    `json:"device"` // device name, "web" or "local"
	Action string    `json:"action"` // e.g. "POST /v1/agents/k3f9a2/answer"
	Status int       `json:"status"` // HTTP-style result code
	Detail string    `json:"detail,omitempty"`
}

func (a *Audit) Log(e AuditEntry) {
	if a == nil || a.Path == "" {
		return
	}
	if e.Time.IsZero() {
		e.Time = time.Now().UTC()
	}
	b, err := json.Marshal(e)
	if err != nil {
		return
	}
	a.mu.Lock()
	defer a.mu.Unlock()
	f, err := os.OpenFile(a.Path, os.O_APPEND|os.O_CREATE|os.O_WRONLY, 0o600)
	if err != nil {
		return
	}
	defer f.Close()
	_, _ = f.Write(append(b, '\n'))
}
