package link

import (
	"github.com/brushknight/agents-deck/backend/internal/devices"
	"github.com/brushknight/agents-deck/backend/internal/model"
	"github.com/brushknight/agents-deck/backend/internal/server"
)

// ServerDeck is the daemon behind a session: its store, its device registry
// and its web API, through Server.Dispatch (same scopes, same audit log).
type ServerDeck struct{ S *server.Server }

func (d ServerDeck) Snapshot() model.State { return d.S.Store.Snapshot() }

func (d ServerDeck) Changes() (<-chan struct{}, func()) {
	ch, unsub := d.S.Store.Subscribe()
	return ch, unsub
}

func (d ServerDeck) Board(id, typ, fw string) (devices.Device, error) {
	return d.S.Devices.Board(id, typ, fw)
}

func (d ServerDeck) Device(name string) (devices.Device, bool) { return d.S.Devices.Get(name) }

func (d ServerDeck) Do(dev devices.Device, method, path string, body []byte) (int, []byte) {
	return d.S.Dispatch(dev, method, path, body)
}
