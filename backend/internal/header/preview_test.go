package header

import (
	"html"
	"os"
	"regexp"
	"strings"
	"testing"
	"time"

	"github.com/brushknight/agents-deck/backend/internal/model"
)

// HEADER_PREVIEW=file.html go test -run TestPreview ./internal/header
func TestPreview(t *testing.T) {
	out := os.Getenv("HEADER_PREVIEW")
	if out == "" {
		t.Skip("set HEADER_PREVIEW=file.html")
	}
	now := time.Now()
	agents := []model.Agent{
		{Title: "Agents Deck", Folder: "agents-terminal", Branch: "main", ModelLabel: "Opus 5.5", Status: model.Running, StatusSince: now.Add(-3 * time.Minute),
			Activity: &model.Activity{Tool: "Bash", Detail: "go test ./..."}, Context: model.Context{Used: 86000, Window: 200000}, Tokens: model.Tokens{Input: 1200000, Output: 90000}, CostUSD: 4.21, Turns: 14,
			Hat: &model.Hat{Shape: "propeller", Color: "teal"}, Subagents: []model.Subagent{{Title: "Explore hats"}, {Title: "Run goldens"}}},
		{Title: "CMM server", Folder: "cmm", ModelLabel: "Sonnet 5", Status: model.Waiting, StatusSince: now.Add(-40 * time.Minute),
			Waiting: &model.Prompt{Title: "Bash", Detail: "rm -rf build/"}, Context: model.Context{Used: 170000, Window: 200000}, CostUSD: 1.1, Turns: 3, Hat: &model.Hat{Shape: "wizard", Color: "purple"}},
		{Title: "Kore UI", Folder: "kore", Status: model.Idle, Unseen: true, Hat: &model.Hat{Shape: "cap", Color: "blue"}, LastPrompt: "make the knob bigger", Context: model.Context{Used: 20000, Window: 200000}},
		{Title: "SysAdmin", Folder: "sysadmin", Status: model.Idle, Hat: &model.Hat{Shape: "bandana", Color: "pink"}, LastPrompt: "check disk"},
		{Title: "codex thread", Folder: "x", Tool: "codex", Status: model.Running, Hat: &model.Hat{Shape: "top hat", Color: "yellow"}},
	}
	for i, shape := range []string{"hard hat", "cap", "beanie", "helmet", "wizard", "beret", "chef", "crown", "headphones", "propeller", "top hat", "cowboy", "bandana"} {
		cols := []string{"teal", "blue", "yellow", "green", "purple", "pink", "sky", "ink"}
		agents = append(agents, model.Agent{Title: shape, Folder: "every hat", Status: model.Running, Hat: &model.Hat{Shape: shape, Color: cols[i%len(cols)]}})
	}
	re := regexp.MustCompile(`#\[([^\]]*)\]`)
	var b strings.Builder
	b.WriteString(`<html><body style="background:#0A0A0A;margin:20px"><pre style="font:18px/18px Menlo;color:#E8E4DC">`)
	for _, a := range agents {
		for _, l := range Render(a, now) {
			l = strings.ReplaceAll(l, "##", "\x00")
			parts := re.Split(l, -1)
			styles := re.FindAllStringSubmatch(l, -1)
			st := map[string]string{}
			b.WriteString(`<div style="background:` + Bg + `;white-space:pre;height:18px;overflow:hidden">`)
			for i, p := range parts {
				if i > 0 {
					for _, kv := range strings.Split(styles[i-1][1], ",") {
						k, v, _ := strings.Cut(kv, "=")
						switch k {
						case "fg", "bg":
							st[k] = v
						case "bold":
							st["b"] = "1"
						case "nobold":
							delete(st, "b")
						}
					}
				}
				if p == "" {
					continue
				}
				w := ""
				if st["b"] != "" {
					w = "font-weight:bold;"
				}
				for _, r := range p {
					if q := strings.IndexRune(string(quadrants), r); q > 0 && r != ' ' { // exact pixels: the 2×2 quarters
						mask := len([]rune(string(quadrants)[:q]))
						b.WriteString(`<span style="display:inline-grid;grid-template:1fr 1fr/1fr 1fr;width:1ch;height:18px;vertical-align:top">`)
						for bit := range 4 {
							c := st["bg"]
							if mask&(1<<bit) != 0 {
								c = st["fg"]
							}
							b.WriteString(`<i style="background:` + c + `"></i>`)
						}
						b.WriteString(`</span>`)
						continue
					}
					b.WriteString(`<span style="color:` + st["fg"] + `;background:` + st["bg"] + `;` + w + `">` + html.EscapeString(strings.ReplaceAll(string(r), "\x00", "#")) + `</span>`)
				}
			}
			b.WriteString("</div>")
		}
		b.WriteString(`<div style="height:24px"></div>`)
	}
	b.WriteString("</pre></body></html>")
	if err := os.WriteFile(out, []byte(b.String()), 0o644); err != nil {
		t.Fatal(err)
	}
}
