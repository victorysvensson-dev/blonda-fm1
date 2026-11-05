#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Bygger webbemulatorn: varje modul i moduler/ + webbvärden (gemensamt/wasm_host.c) till WebAssembly, och sidan med
# alla moduler inbakade (välj modul överst på sidan).
#   web/bygg.sh   (FM1_DOCK pekar på fm1-dock, standard ../fm1-dock)  ->  web/<modul>.wasm, web/index.html
set -e
cd "$(dirname "$0")/.."
: "${FM1_DOCK:=../fm1-dock}"
for M in moduler/*/; do
    M=${M%/}; n=$(basename "$M")
    SRCS=$(sed -n 's/^sources = //p' $M/module.cfg | tr ' ' '\n' | sed "s#^#$M/#" | tr '\n' ' ')
    INCS=$(sed -n 's/^includes = //p' $M/module.cfg | tr ' ' '\n' | sed "s#^#-I$M/#" | tr '\n' ' ')
    clang --target=wasm32 -O2 -nostdlib -ffreestanding -fno-builtin -ffp-contract=off -Wall -Wno-unused-function \
        -I"$FM1_DOCK/firmware/dock" -I"$FM1_DOCK/firmware/src" -I"$FM1_DOCK/firmware/gen" $INCS \
        -Wl,--no-entry -Wl,--strip-all -Wl,-z,stack-size=65536 -Wl,--initial-memory=1048576 \
        -o web/$n.wasm gemensamt/wasm_host.c $M/${n}_module.c $SRCS "$FM1_DOCK/modules/lib/hb_lib.c"
done
python3 - <<'PY'
import base64, glob, json, os
mods = {os.path.basename(p)[:-5]: base64.b64encode(open(p, "rb").read()).decode() for p in sorted(glob.glob("web/*.wasm"))}
s = open("web/sida.html", encoding="utf-8").read().replace('"{{MODULES}}"', json.dumps(mods))
open("web/index.html", "w", encoding="utf-8").write(s)
print(f"web/index.html: {len(s) // 1024} KB, moduler: " + ", ".join(f"{k} ({len(v) * 3 // 4 // 1024} KB)" for k, v in mods.items()))
PY
