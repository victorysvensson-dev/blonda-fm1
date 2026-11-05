#!/usr/bin/env python3
"""Kör scenariotestet (build/bris-scenarier) och bedömer varje scenario.

Ett scenario är godkänt när antingen
  - differensen sampel för sampel ligger minst 30 dB under signalen, eller
  - det låter likadant: nivån inom ±0,25 dB och klangen inom 0,5 dB (1/3-oktavband, som likhet.py).
Det andra fallet behövs när tonhöjden moduleras hårt (vibrato, självsvängande filter): då glider fasen isär
fast ljudet är detsamma, och en jämförelse sampel för sampel säger inget.
  python3 test/scenarier.py
"""
import os
import re
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import likhet as L  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(ROOT, "build", "bris-scenarier")
MAX_DIFF, MAX_NIVA, MAX_KLANG = -30.0, 0.25, 0.5

with tempfile.TemporaryDirectory() as td:
    out = subprocess.run([EXE, "--wav", td], capture_output=True, text=True).stdout
    rows = [l for l in out.splitlines() if re.search(r"\s-?\d+\.\d\s+-?\d+\.\d\s+(ok|AVVIKER)$", l)]
    bad = 0
    print(f"{'scenario':50s} {'diff dB':>8s} {'nivå dB':>8s} {'klang dB':>9s}")
    for i, line in enumerate(rows):
        name = line[:50].strip()
        nums = re.findall(r"-?\d+\.\d", line[50:])
        diff, topp = float(nums[0]), float(nums[1])
        a = L.read_wav(os.path.join(td, f"{i}-blonda-motor.wav"))
        b = L.read_wav(os.path.join(td, f"{i}-fm1.wav"))
        niva, klang, form, _ = L.compare(a, b)
        ok = topp <= 0.0 and (diff <= MAX_DIFF or (abs(niva) <= MAX_NIVA and klang <= MAX_KLANG))
        bad += not ok
        how = "ok" if diff <= MAX_DIFF else ("ok (låter lika)" if ok else "AVVIKER")
        print(f"{name:50s} {diff:8.1f} {niva:+8.2f} {klang:9.2f}  {how}")
print(f"\ngränser: diff ≤ {MAX_DIFF:.0f} dB, eller nivå ±{MAX_NIVA} dB och klang ≤ {MAX_KLANG} dB")
if bad:
    print(f"{bad} av {len(rows)} scenarier avviker")
    sys.exit(1)
print(f"alla {len(rows)} scenarier ok")
