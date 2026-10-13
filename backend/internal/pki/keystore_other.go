//go:build !darwin

package pki

import "path/filepath"

// DefaultKeystore is the platform's store: a 0600 file for now on Linux and
// Windows (native stores, the Secret Service and DPAPI, can slot in here).
func DefaultKeystore(dataDir string) Keystore {
	return FileKeystore{Dir: filepath.Join(dataDir, "keys")}
}
