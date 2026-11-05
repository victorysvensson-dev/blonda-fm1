#!/usr/bin/env python3
"""SLITEN för FM-1 mot blonda-motor: kör build/sliten-test och bedömer varje fall.

Godkänt när differensen sampel för sampel ligger minst 30 dB under signalen, eller när det låter likadant:
nivå inom ±0,25 dB och klang inom 0,5 dB (1/3-oktavband, som BRIS likhetstest). Det andra behövs för media där
bruset avgör (en slumpad knäpp som hamnar ett sampel annorlunda ger stor differens men samma ljud).
  python3 gemensamt/test/sliten_test.py
"""
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "moduler", "bris", "test"))
import likhet as L  # noqa: E402

EXE = os.path.join(ROOT, "build", "sliten-test")
MAX_DIFF, MAX_NIVA, MAX_KLANG = -30.0, 0.25, 0.5

with tempfile.TemporaryDirectory() as td:
    out = subprocess.run([EXE, td], capture_output=True, text=True, check=True).stdout
    rows = [l for l in out.splitlines()[1:] if re.search(r"-?\d+\.\d$", l)]
    bad = 0
    print(f"{'fall':34s} {'diff dB':>8s} {'nivå dB':>8s} {'klang dB':>9s}")
    for i, line in enumerate(rows):
        name, diff = line[:34].strip(), float(line[34:])
        a = L.read_wav(os.path.join(td, f"{i}-blonda-motor.wav"))
        b = L.read_wav(os.path.join(td, f"{i}-fm1.wav"))
        niva, klang, _, _ = L.compare(a, b)
        ok = diff <= MAX_DIFF or (abs(niva) <= MAX_NIVA and klang <= MAX_KLANG)
        bad += not ok
        how = "ok" if diff <= MAX_DIFF else ("ok (låter lika)" if ok else "AVVIKER")
        print(f"{name:34s} {diff:8.1f} {niva:+8.2f} {klang:9.2f}  {how}")
print(f"\ngränser: diff ≤ {MAX_DIFF:.0f} dB, eller nivå ±{MAX_NIVA} dB och klang ≤ {MAX_KLANG} dB")
if bad:
    print(f"{bad} av {len(rows)} fall avviker")
    sys.exit(1)
print(f"alla {len(rows)} fall ok")
