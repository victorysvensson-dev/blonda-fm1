#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Bygger webbemulatorn: BRIS-modulen + motorn + webbvärden (gemensamt/wasm_host.c) till WebAssembly, och sidan med den inbakad.
#   web/bygg.sh   (FM1_DOCK pekar på fm1-dock, standard ../fm1-dock)  ->  web/bris.wasm, web/index.html
set -e
cd "$(dirname "$0")/.."
: "${FM1_DOCK:=../fm1-dock}"
clang --target=wasm32 -O2 -nostdlib -ffreestanding -fno-builtin -ffp-contract=off -Wall -Wno-unused-function \
    -I"$FM1_DOCK/firmware/dock" -I"$FM1_DOCK/firmware/src" -I"$FM1_DOCK/firmware/gen" -Imoduler/bris/src \
    -Wl,--no-entry -Wl,--strip-all -Wl,-z,stack-size=65536 -Wl,--initial-memory=1048576 \
    -o web/bris.wasm gemensamt/wasm_host.c moduler/bris/bris_module.c moduler/bris/src/bris_fm1.c "$FM1_DOCK/modules/lib/hb_lib.c"
python3 - <<'PY'
import base64
b = base64.b64encode(open("web/bris.wasm", "rb").read()).decode()
s = open("web/sida.html", encoding="utf-8").read().replace("{{WASM_B64}}", b)
open("web/index.html", "w", encoding="utf-8").write(s)
print(f"web/index.html: {len(s) // 1024} KB (wasm {len(b) * 3 // 4 // 1024} KB)")
PY
