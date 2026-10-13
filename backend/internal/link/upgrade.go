package link

import (
	"bufio"
	"context"
	"fmt"
	"io"
	"net"
	"net/http"
	"strings"
)

// UpgradeHandler serves POST /local/link on the daemon's unix socket: the
// connection becomes a Deck Link stream, so `agentctl link sim` (and tests)
// can be a board without hardware.
func UpgradeHandler(deck Deck) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		if !strings.EqualFold(r.Header.Get("Upgrade"), "deck-link") {
			http.Error(w, "send Upgrade: deck-link", http.StatusBadRequest)
			return
		}
		conn, rw, err := http.NewResponseController(w).Hijack()
		if err != nil {
			http.Error(w, err.Error(), http.StatusInternalServerError)
			return
		}
		defer conn.Close()
		_, _ = rw.WriteString("HTTP/1.1 101 Switching Protocols\r\nUpgrade: deck-link\r\nConnection: Upgrade\r\n\r\n")
		_ = rw.Flush()
		stream := struct {
			io.Reader
			io.Writer
		}{rw.Reader, conn}
		_ = NewSession(deck, stream, "sim").Run(context.Background())
	}
}

// DialSim connects to the daemon's unix socket as a Deck Link board.
func DialSim(sock string) (io.ReadWriteCloser, error) {
	conn, err := net.Dial("unix", sock)
	if err != nil {
		return nil, err
	}
	fmt.Fprint(conn, "POST /local/link HTTP/1.1\r\nHost: agentctl\r\nConnection: Upgrade\r\nUpgrade: deck-link\r\nContent-Length: 0\r\n\r\n")
	br := bufio.NewReader(conn)
	status, err := br.ReadString('\n')
	if err != nil {
		conn.Close()
		return nil, err
	}
	if !strings.Contains(status, " 101 ") {
		conn.Close()
		return nil, fmt.Errorf("daemon refused the link: %s", strings.TrimSpace(status))
	}
	for {
		l, err := br.ReadString('\n')
		if err != nil {
			conn.Close()
			return nil, err
		}
		if l == "\r\n" {
			break
		}
	}
	return struct {
		io.Reader
		io.Writer
		io.Closer
	}{br, conn, conn}, nil
}
