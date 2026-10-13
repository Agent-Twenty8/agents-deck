package server

import (
	"crypto/rand"
	"crypto/subtle"
	"encoding/hex"
	"os"
	"strings"
	"sync"
	"time"
)

// RandomToken returns 32 random bytes as hex.
func RandomToken() string {
	b := make([]byte, 32)
	if _, err := rand.Read(b); err != nil {
		panic(err)
	}
	return hex.EncodeToString(b)
}

// LoadOrCreateSecret reads a 0600 secret file, creating it when missing.
func LoadOrCreateSecret(path string) (string, error) {
	if b, err := os.ReadFile(path); err == nil && len(strings.TrimSpace(string(b))) >= 32 {
		return strings.TrimSpace(string(b)), nil
	}
	return RotateSecret(path)
}

func RotateSecret(path string) (string, error) {
	t := RandomToken()
	return t, os.WriteFile(path, []byte(t+"\n"), 0o600)
}

func equal(a, b string) bool {
	return len(a) > 0 && subtle.ConstantTimeCompare([]byte(a), []byte(b)) == 1
}

// loginCodes are one-time, 60-second codes that turn into a web session cookie.
type loginCodes struct {
	mu    sync.Mutex
	codes map[string]time.Time
}

func (l *loginCodes) issue() string { return l.issueFor(60 * time.Second) }

func (l *loginCodes) issueFor(ttl time.Duration) string {
	l.mu.Lock()
	defer l.mu.Unlock()
	if l.codes == nil {
		l.codes = map[string]time.Time{}
	}
	now := time.Now()
	for c, exp := range l.codes {
		if now.After(exp) {
			delete(l.codes, c)
		}
	}
	c := RandomToken()
	l.codes[c] = now.Add(ttl)
	return c
}

func (l *loginCodes) redeem(code string) bool {
	l.mu.Lock()
	defer l.mu.Unlock()
	for c, exp := range l.codes {
		if equal(c, code) {
			delete(l.codes, c)
			return time.Now().Before(exp)
		}
	}
	return false
}
