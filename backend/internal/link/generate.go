package link

// Regenerate linkpb from the contract (needs protoc and protoc-gen-go v1.36.10:
// go install google.golang.org/protobuf/cmd/protoc-gen-go@v1.36.10).
//go:generate protoc -I ../../../proto --go_out=../.. --go_opt=module=github.com/brushknight/agents-deck/backend ../../../proto/agentsdeck/link/v1/link.proto
