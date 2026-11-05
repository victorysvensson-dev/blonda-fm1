#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Allt som ska vara grönt innan BRIS (moduler/bris) lämnas över: bygger testverktygen på datorn och kör
#   1. fabriksljuden mot blonda-motor (likhet.py)      2. scenarier mot blonda-motor (scenarier.py)
#   3. modulen i en låtsas-FM-1 (skärm, knappar, MIDI) 4. lasten på chipet, ungefär (cykler/kor.py)
# Kräver: blonda-motor (BLONDA_MOTOR, byggd med cmake) och fm1-dock (FM1_DOCK). Steg 4 kräver clang, lld, unicorn.
set -e
cd "$(dirname "$0")"
: "${BLONDA_MOTOR:=../../../blonda-motor}"
: "${FM1_DOCK:=../../../fm1-dock}"
mkdir -p build/skarm

python3 tools/gen_presets.py "$BLONDA_MOTOR"
if git rev-parse --git-dir >/dev/null 2>&1 && ! git diff --quiet -- src/bris_presets.h; then
    echo "OBS: src/bris_presets.h ändrades – blonda-motor har nya parametrar eller ljud. Kontrollera och checka in."
fi

[ -x "$BLONDA_MOTOR/build/blonda-render" ] || { cmake -S "$BLONDA_MOTOR" -B "$BLONDA_MOTOR/build" -DCMAKE_BUILD_TYPE=Release >/dev/null && cmake --build "$BLONDA_MOTOR/build" -j4 >/dev/null; }

W="-std=c99 -O2 -Wall -Wextra -Werror -Wdouble-promotion -Wfloat-conversion"
cc $W -Isrc -c src/bris_fm1.c -o build/bris_fm1.o
cc -std=c99 -O2 -Isrc -o build/bris-render test/render.c build/bris_fm1.o
c++ -std=c++17 -O2 -I"$BLONDA_MOTOR/include" -I"$BLONDA_MOTOR/motorer" -Isrc -o build/bris-scenarier test/scenarier.cpp build/bris_fm1.o
cc -std=gnu99 -O2 -Wall -Wno-unused-function -I"$FM1_DOCK/firmware/dock" -I"$FM1_DOCK/firmware/src" -I"$FM1_DOCK/firmware/gen" -Isrc \
    test/modul_host.c bris_module.c build/bris_fm1.o -o build/bris-modul

echo "== 1. fabriksljuden mot blonda-motor"
python3 test/likhet.py "$BLONDA_MOTOR" | tail -4
echo "== 2. scenarier mot blonda-motor"
python3 test/scenarier.py | tail -2
echo "== 3. modulen i en låtsas-FM-1"
build/bris-modul build/skarm/s | tail -1
if command -v clang >/dev/null && command -v ld.lld >/dev/null && python3 -c "import unicorn" 2>/dev/null; then
    echo "== 4. last på chipet (uppskattning)"
    python3 test/cykler/kor.py 1 5
fi
echo "allt grönt"
