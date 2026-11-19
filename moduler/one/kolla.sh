#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Allt som ska vara grönt innan BLONDA ONE (moduler/one) lämnas över: bygger testverktygen på datorn och kör
#   1. fabriksljuden mot blonda-motor, båda motorerna (likhet.py)   2. scenarier mot blonda-motor (scenarier.py)
#   3. SLITEN (ERA), 4. RUM (reverbet) och PUMP mot blonda-motor
#   5. ARP och SEQ, och modulen i en låtsas-FM-1 (skärm, knappar, MIDI, motorbyte)
#   6. last och minne på chipet, ungefär (cykler/kor.py)
# Kräver: blonda-motor (BLONDA_MOTOR, byggd med cmake) och fm1-dock (FM1_DOCK). Steg 6 kräver clang, lld, unicorn.
set -e
cd "$(dirname "$0")"
mkdir -p ../../build
: "${BLONDA_MOTOR:=../../../blonda-motor}"
: "${FM1_DOCK:=../../../fm1-dock}"
export FM1_DOCK
mkdir -p build/skarm

python3 tools/gen_dotfont.py
python3 tools/gen_presets_one.py "$BLONDA_MOTOR"
python3 tools/gen_presets_bris.py "$BLONDA_MOTOR"
if git rev-parse --git-dir >/dev/null 2>&1 && ! git diff --quiet -- src/one_presets.h src/bris_presets.h; then
    echo "OBS: src/one_presets.h eller src/bris_presets.h ändrades – blonda-motor har nya parametrar eller ljud. Kontrollera och checka in."
fi

[ -x "$BLONDA_MOTOR/build/blonda-render" ] || { cmake -S "$BLONDA_MOTOR" -B "$BLONDA_MOTOR/build" -DCMAKE_BUILD_TYPE=Release >/dev/null && cmake --build "$BLONDA_MOTOR/build" -j4 >/dev/null; }

W="-std=c99 -O2 -Wall -Wextra -Werror -Wdouble-promotion -Wfloat-conversion"
for f in one_fm1 bris_fm1 bfx_fm1 spel_fm1; do cc $W -Isrc -c src/$f.c -o build/$f.o; done
MI="-I$BLONDA_MOTOR/include -I$BLONDA_MOTOR/motorer"
cc -std=c99 -O2 -Isrc -o build/one-render test/render.c build/one_fm1.o build/bfx_fm1.o
cc -std=c99 -O2 -Isrc -o build/bris-render test/bris/render.c build/bris_fm1.o build/bfx_fm1.o
c++ -std=c++17 -O2 $MI -Isrc -o build/one-scenarier test/scenarier.cpp build/one_fm1.o build/bfx_fm1.o
c++ -std=c++17 -O2 $MI -Isrc -o build/bris-scenarier test/bris/scenarier.cpp build/bris_fm1.o build/bfx_fm1.o
# modulens källor och sökvägar som fm1-dock läser dem (module.cfg)
SRCS=$(sed -n 's/^sources = //p' module.cfg)
INCS=$(sed -n 's/^includes = //p' module.cfg | tr ' ' '\n' | sed 's/^/-I/' | tr '\n' ' ')
for f in $SRCS; do cc $W $INCS -c "$f" -o /dev/null; done
cc -std=gnu99 -O2 -Wall -Werror -Wno-unused-function -I"$FM1_DOCK/firmware/dock" -I"$FM1_DOCK/firmware/src" -I"$FM1_DOCK/firmware/gen" $INCS \
    test/modul_host.c one_module.c $SRCS -o build/one-modul -lm

echo "== 1. fabriksljuden mot blonda-motor: trioscillatormotorn (3 OSC), sedan BRIS"
python3 test/likhet.py "$BLONDA_MOTOR" | tail -4
python3 test/bris/likhet.py "$BLONDA_MOTOR" | tail -4
echo "== 2. scenarier mot blonda-motor: 3 OSC, sedan BRIS"
python3 test/scenarier.py | tail -2
python3 test/bris/scenarier.py | tail -2
echo "== 3. SLITEN (ERA-rattens band och lofi) mot blonda-motor"
cc $W -I../../gemensamt -I../../gemensamt/effekter -c ../../gemensamt/effekter/sliten_fm1.c -o build/sliten_fm1.o
c++ -std=c++17 -O2 $MI -I../../gemensamt -I../../gemensamt/effekter \
    -o ../../build/sliten-test ../../gemensamt/test/sliten_test.cpp build/sliten_fm1.o
python3 ../../gemensamt/test/sliten_test.py | tail -1
echo "== 4. RUM (reverbet) mot blonda-motor, och PUMP"
cc $W -I../../gemensamt -I../../gemensamt/effekter -c ../../gemensamt/effekter/rum_fm1.c -o build/rum_fm1.o
c++ -std=c++17 -O2 $MI -I../../gemensamt -I../../gemensamt/effekter \
    -o ../../build/rum-test ../../gemensamt/test/rum_test.cpp build/rum_fm1.o
python3 ../../gemensamt/test/rum_test.py | tail -1
cc -std=c99 -O2 -Wall -I../../gemensamt -I../../gemensamt/effekter ../../gemensamt/test/pump_test.c ../../gemensamt/effekter/pump_fm1.c \
    -o ../../build/pump-test -lm
../../build/pump-test | tail -1
echo "== 5. ARP och SEQ för sig (ordning, takt, inspelning), sedan modulen i en låtsas-FM-1"
cc -std=c99 -O2 -Wall -Wextra -Werror -Isrc test/spel_test.c build/spel_fm1.o -o build/spel-test -lm
build/spel-test | tail -1
build/one-modul build/skarm/s | tail -1
if command -v clang >/dev/null && command -v ld.lld >/dev/null && python3 -c "import unicorn" 2>/dev/null; then
    echo "== 6. last och minne på chipet (uppskattning)"
    python3 test/cykler/kor.py 1 26 b1 b5
fi
echo "allt grönt"
