#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Allt som ska vara grönt innan BRIS (moduler/bris) lämnas över: bygger testverktygen på datorn och kör
#   1. fabriksljuden mot blonda-motor (likhet.py)      2. scenarier mot blonda-motor (scenarier.py)
#   3. SLITEN (ERA), 4. RUM (reverbet) och PUMP mot blonda-motor
#   5. modulen i en låtsas-FM-1 (skärm, knappar, MIDI) 6. lasten på chipet, ungefär (cykler/kor.py)
# Kräver: blonda-motor (BLONDA_MOTOR, byggd med cmake) och fm1-dock (FM1_DOCK). Steg 6 kräver clang, lld, unicorn.
set -e
cd "$(dirname "$0")"
mkdir -p ../../build
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
# modulens källor och sökvägar som fm1-dock läser dem (module.cfg)
SRCS=$(sed -n 's/^sources = //p' module.cfg)
INCS=$(sed -n 's/^includes = //p' module.cfg | tr ' ' '\n' | sed 's/^/-I/' | tr '\n' ' ')
for f in $SRCS; do cc $W $INCS -c "$f" -o /dev/null; done
cc -std=gnu99 -O2 -Wall -Wno-unused-function -I"$FM1_DOCK/firmware/dock" -I"$FM1_DOCK/firmware/src" -I"$FM1_DOCK/firmware/gen" $INCS \
    test/modul_host.c bris_module.c $SRCS -o build/bris-modul -lm

echo "== 1. fabriksljuden mot blonda-motor"
python3 test/likhet.py "$BLONDA_MOTOR" | tail -4
echo "== 2. scenarier mot blonda-motor"
python3 test/scenarier.py | tail -2
echo "== 3. SLITEN (ERA-rattens band och lofi) mot blonda-motor"
cc $W -I../../gemensamt -I../../gemensamt/effekter -c ../../gemensamt/effekter/sliten_fm1.c -o build/sliten_fm1.o
c++ -std=c++17 -O2 -I"$BLONDA_MOTOR/include" -I"$BLONDA_MOTOR/motorer" -I../../gemensamt -I../../gemensamt/effekter \
    -o ../../build/sliten-test ../../gemensamt/test/sliten_test.cpp build/sliten_fm1.o
python3 ../../gemensamt/test/sliten_test.py | tail -1
echo "== 4. RUM (reverbet) mot blonda-motor, och PUMP"
cc $W -I../../gemensamt -I../../gemensamt/effekter -c ../../gemensamt/effekter/rum_fm1.c -o build/rum_fm1.o
c++ -std=c++17 -O2 -I"$BLONDA_MOTOR/include" -I"$BLONDA_MOTOR/motorer" -I../../gemensamt -I../../gemensamt/effekter \
    -o ../../build/rum-test ../../gemensamt/test/rum_test.cpp build/rum_fm1.o
python3 ../../gemensamt/test/rum_test.py | tail -1
cc -std=c99 -O2 -Wall -I../../gemensamt -I../../gemensamt/effekter ../../gemensamt/test/pump_test.c ../../gemensamt/effekter/pump_fm1.c \
    -o ../../build/pump-test -lm
../../build/pump-test | tail -1
echo "== 5. modulen i en låtsas-FM-1"
build/bris-modul build/skarm/s | tail -1
if command -v clang >/dev/null && command -v ld.lld >/dev/null && python3 -c "import unicorn" 2>/dev/null; then
    echo "== 6. last på chipet (uppskattning)"
    python3 test/cykler/kor.py 1 5
fi
echo "allt grönt"
