package link

import (
	"errors"
	"os"
)

// ErrUnsupported: serial ports on this platform aren't implemented yet.
var ErrUnsupported = errors.New("serial ports aren't supported on this platform yet")

// OpenPort opens a serial port raw, 8N1, no flow control, at baud.
func OpenPort(path string, baud int) (*os.File, error) { return openPort(path, baud) }
