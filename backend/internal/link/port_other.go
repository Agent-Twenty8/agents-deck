//go:build !darwin && !linux

package link

import "os"

func openPort(path string, baud int) (*os.File, error) { return nil, ErrUnsupported }
