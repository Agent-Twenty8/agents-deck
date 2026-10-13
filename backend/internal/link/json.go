package link

import (
	"bytes"
	"encoding/json"
)

func compactJSON(dst *bytes.Buffer, src []byte) error { return json.Compact(dst, src) }
