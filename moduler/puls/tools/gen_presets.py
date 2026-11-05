#!/usr/bin/env python3
"""Skriver src/puls_presets.h från blonda-motor (motorer/puls/puls.json + puls_ljud.h).

PULS parametrar och trumset har en enda sanning: blonda-motor. Den här filen ändras aldrig för hand.
  python3 tools/gen_presets.py [sökväg till blonda-motor]      (standard: $BLONDA_MOTOR, annars blonda-motor bredvid blonda-fm1)
"""
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
motor = sys.argv[1] if len(sys.argv) > 1 else os.environ.get("BLONDA_MOTOR", os.path.join(ROOT, "..", "..", "..", "blonda-motor"))
js = json.load(open(os.path.join(motor, "motorer", "puls", "puls.json"), encoding="utf-8"))
ljud_h = open(os.path.join(motor, "motorer", "puls", "puls_ljud.h"), encoding="utf-8").read()

# keepOnPreset står säkrast i puls_ljud.h (C++-motorn): {"vol", "Volume", 0.0, 1.0, 0.7, false, true}
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


out = ["/* GENERERAD FIL – ändra inte för hand. Källa: blonda-motor/motorer/puls/puls.json (tools/gen_presets.py). */",
       "#ifndef PULS_PRESETS_H", "#define PULS_PRESETS_H", "#include <stdint.h>", "",
       "enum {"]
out += [f"    PP_{p['id']} = {i}," for i, p in enumerate(params)]
out += [f"    PP_N = {len(params)}", "};", "",
        "typedef struct { const char *id, *name; float lo, hi, def; uint8_t integer, keep; } puls_param_t;",
        "static const puls_param_t PULS_PARAMS[PP_N] = {"]
for p in params:
    out.append(f'    {{"{p["id"]}", "{cstr(p["name"])}", {num(p["min"])}, {num(p["max"])}, {num(p["default"])}, '
               f'{1 if p["integer"] else 0}, {1 if p["id"] in keep else 0}}},')
out += ["};", "",
        "typedef struct { uint8_t param; float value; } puls_pv_t;",
        "typedef struct { const char *name; uint16_t first, count; } puls_preset_t;"]
vals, pres = [], []
for pr in js["presets"]:
    first = len(vals)
    for k, v in pr["values"].items():
        if k in ids:
            vals.append((ids.index(k), v, k))
    pres.append((pr["name"], first, len(vals) - first))
out.append(f"static const puls_pv_t PULS_PV[{max(1, len(vals))}] = {{")
out += [f"    {{PP_{k}, {num(v)}}}," for (i, v, k) in vals]
out += ["};", f"#define PULS_NPRESETS {len(pres)}", f"#define PULS_FACTORY {js.get('factoryBank', 0)}",
        "static const puls_preset_t PULS_PRESETS[PULS_NPRESETS] = {"]
out += [f'    {{"{cstr(n)}", {f}, {c}}},' for (n, f, c) in pres]
out += ["};", "", "#endif", ""]
dst = os.path.join(ROOT, "src", "puls_presets.h")
open(dst, "w", encoding="utf-8").write("\n".join(out))
print(f"{dst}: {len(params)} parametrar, {len(pres)} ljud, behåller vid byte: {sorted(keep)}")
