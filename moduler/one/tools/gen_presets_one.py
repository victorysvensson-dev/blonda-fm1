#!/usr/bin/env python3
"""Skriver src/one_presets.h från blonda-motor (motorer/one/one.json + one_ljud.h).

ONE parametrar och fabriksljud har en enda sanning: blonda-motor. Den här filen ändras aldrig för hand.
  python3 tools/gen_presets_one.py [sökväg till blonda-motor]      (standard: $BLONDA_MOTOR, annars blonda-motor bredvid blonda-fm1)
"""
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import namn  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
motor = sys.argv[1] if len(sys.argv) > 1 else os.environ.get("BLONDA_MOTOR", os.path.join(ROOT, "..", "..", "..", "blonda-motor"))
js = json.load(open(os.path.join(motor, "motorer", "one", "one.json"), encoding="utf-8"))
ljud_h = open(os.path.join(motor, "motorer", "one", "one_ljud.h"), encoding="utf-8").read()

# keepOnPreset står säkrast i one_ljud.h (C++-motorn): {"vol", "Volume", 0.0, 1.0, 0.7, false, true}
keep = set(re.findall(r'\{"(\w+)",\s*"[^"]*",\s*[-\d.]+,\s*[-\d.]+,\s*[-\d.]+,\s*(?:true|false),\s*true\}', ljud_h))
params = [p for p in js["params"] if p["id"] not in js.get("uiOnly", [])]
ids = [p["id"] for p in params]


def cstr(s):
    """UTF-8 till det FM-1:ans typsnitt klarar: å ä ö blir a a o, versaler för skärmen."""
    t = s.replace("å", "a").replace("ä", "a").replace("ö", "o").replace("Å", "A").replace("Ä", "A").replace("Ö", "O")
    t = t.replace("é", "e").encode("ascii", "replace").decode()
    return t.replace("\\", "\\\\").replace('"', '\\"')


def num(x):
    return repr(float(x)) + "f"


out = ["/* GENERERAD FIL – ändra inte för hand. Källa: blonda-motor/motorer/one/one.json (tools/gen_presets_one.py). */",
       "#ifndef ONE_PRESETS_H", "#define ONE_PRESETS_H", "#include <stdint.h>", "",
       "enum {"]
out += [f"    OP_{p['id']} = {i}," for i, p in enumerate(params)]
out += [f"    OP_N = {len(params)}", "};", "",
        "typedef struct { const char *id, *name; float lo, hi, def; uint8_t integer, keep; } one_param_t;",
        "static const one_param_t ONE_PARAMS[OP_N] = {"]
for p in params:
    out.append(f'    {{"{p["id"]}", "{cstr(p["name"])}", {num(p["min"])}, {num(p["max"])}, {num(p["default"])}, '
               f'{1 if p["integer"] else 0}, {1 if p["id"] in keep else 0}}},')
out += ["};", "",
        "typedef struct { uint8_t param; float value; } one_pv_t;",
        "typedef struct { const char *name; uint16_t first, count; } one_preset_t;"]
vals, pres = [], []
for pr in js["presets"]:
    first = len(vals)
    for k, v in pr["values"].items():
        if k in ids:
            vals.append((ids.index(k), v, k))
    pres.append((pr["name"], first, len(vals) - first))
out.append(f"static const one_pv_t ONE_PV[{max(1, len(vals))}] = {{")
out += [f"    {{OP_{k}, {num(v)}}}," for (i, v, k) in vals]
out += ["};", f"#define ONE_NPRESETS {len(pres)}", f"#define ONE_FACTORY {js.get('factoryBank', 0)}",
        "static const one_preset_t ONE_PRESETS[ONE_NPRESETS] = {"]
out += [f'    {{"{namn.cstr(namn.fm1("one", n))}", {f}, {c}}},' for (n, f, c) in pres]   # (namnen som FM-1 visar dem: tools/namn.py)
out += ["};", "", "#endif", ""]
dst = os.path.join(ROOT, "src", "one_presets.h")
open(dst, "w", encoding="utf-8").write("\n".join(out))
print(f"{dst}: {len(params)} parametrar, {len(pres)} ljud, behåller vid byte: {sorted(keep)}")
