// Regenerates hats_gen.go, the hat colours, from the web dashboard's
// (app.js HAT_COLORS): `go generate ./internal/header` (needs node).
const fs = require('fs');
const src = fs.readFileSync('../web/static/app.js', 'utf8');
const i = src.indexOf('const HAT_COLORS'), j = src.indexOf('const HAT_SHAPES');
eval(src.slice(i, j).replace('const HAT_COLORS', 'globalThis.HAT_COLORS'));
const q = JSON.stringify;
let out = '// Code generated from backend/internal/web/static/app.js (HAT_COLORS); DO NOT EDIT.\n\npackage header\n\n// hatColors: each hat colour and its shade.\nvar hatColors = map[string][2]string{\n';
for (const [k, v] of Object.entries(HAT_COLORS)) out += `\t${q(k)}: {${q(v[0])}, ${q(v[1])}},\n`;
fs.writeFileSync('hats_gen.go', out + '}\n');
