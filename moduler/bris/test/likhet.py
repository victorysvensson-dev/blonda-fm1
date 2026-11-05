#!/usr/bin/env python3
"""Likhetstest: FM-1-BRIS mot BRIS i blonda-motor, för varje fabriksljud.

Spelar alla ljud med båda (samma toner och tider, 44,1 kHz) och jämför:
  nivå   skillnad i RMS (dB)
  klang  medelavvikelse i spektrum (dB, 1/3-oktavband per 46 ms, bara band som hörs)
  form   korrelation sampel för sampel (1 = identiskt; driften och LFO:n gör att den sjunker med tiden)
Gränserna nedan är vad som får passera. Ett ljud som underkänns skrivs ut och testet ger felkod.

  python3 test/likhet.py [blonda-motor] [--spara mapp]
"""
import os
import subprocess
import sys
import tempfile
import wave

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
MOTOR = next((a for a in sys.argv[1:] if not a.startswith("--") and os.path.isdir(a)),
             os.environ.get("BLONDA_MOTOR", os.path.join(ROOT, "..", "..", "..", "blonda-motor")))
SPARA = sys.argv[sys.argv.index("--spara") + 1] if "--spara" in sys.argv else None
SR = 44100

MAX_NIVA = 0.25    # dB
MAX_KLANG = 0.5    # dB
MIN_FORM = 0.98    # korrelation under första 0,3 s


def read_wav(path):
    import struct
    with open(path, "rb") as f:
        d = f.read()
    # flyttals-WAV (format 3) från båda verktygen: hitta data-blocket
    i = d.index(b"data")
    n = struct.unpack("<I", d[i + 4:i + 8])[0]
    a = np.frombuffer(d[i + 8:i + 8 + n], dtype="<f4").reshape(-1, 2)
    return a.astype(np.float64)


def bands(x):
    """energi i 1/3-oktavband per ram, dB"""
    n, hop = 2048, 2048
    win = np.hanning(n)
    fr = np.fft.rfftfreq(n, 1 / SR)
    edges = 25 * 2 ** (np.arange(0, 31) / 3)
    edges = edges[edges < SR / 2]
    out = []
    for s in range(0, len(x) - n, hop):
        sp = np.abs(np.fft.rfft(x[s:s + n] * win)) ** 2
        out.append([sp[(fr >= lo) & (fr < hi)].sum() for lo, hi in zip(edges[:-1], edges[1:])])
    return 10 * np.log10(np.array(out) + 1e-20)


def compare(a, b):
    m = min(len(a), len(b))
    a, b = a[:m], b[:m]
    ra, rb = np.sqrt(np.mean(a ** 2)), np.sqrt(np.mean(b ** 2))
    niva = 20 * np.log10((rb + 1e-12) / (ra + 1e-12))
    ma, mb = a.mean(axis=1), b.mean(axis=1)
    ba, bb = bands(ma), bands(mb)
    hors = ba > ba.max() - 60            # band inom 60 dB från det starkaste
    klang = float(np.mean(np.abs(ba[hors] - bb[hors]))) if hors.any() else 0.0
    k = int(0.3 * SR)
    xa, xb = ma[:k], mb[:k]
    form = float(np.dot(xa, xb) / (np.sqrt(np.dot(xa, xa) * np.dot(xb, xb)) + 1e-20))
    return niva, klang, form, 20 * np.log10(np.abs(a).max() + 1e-12)


def main():
    ref = os.path.join(MOTOR, "build", "blonda-render")
    our = os.path.join(ROOT, "build", "bris-render")
    for exe in (ref, our):
        if not os.path.exists(exe):
            sys.exit(f"saknas: {exe} (bygg först)")
    with tempfile.TemporaryDirectory() as td:
        da, db = SPARA and os.path.join(SPARA, "blonda-motor") or os.path.join(td, "a"), \
                 SPARA and os.path.join(SPARA, "fm1") or os.path.join(td, "b")
        os.makedirs(da, exist_ok=True)
        os.makedirs(db, exist_ok=True)
        subprocess.run([ref, "bris", "--all", da, "--sr", str(SR)], check=True, stdout=subprocess.DEVNULL)
        subprocess.run([our, "--all", db], check=True, stdout=subprocess.DEVNULL)
        fa = sorted(os.listdir(da))
        fb = sorted(os.listdir(db))
        if len(fa) != len(fb):
            sys.exit(f"olika antal ljud: blonda-motor {len(fa)}, FM-1 {len(fb)}")
        bad, rows = [], []
        for x, y in zip(fa, fb):
            if x[:2] != y[:2]:
                sys.exit(f"ordningen skiljer: {x} / {y}")
            niva, klang, form, topp = compare(read_wav(os.path.join(da, x)), read_wav(os.path.join(db, y)))
            ok = abs(niva) <= MAX_NIVA and klang <= MAX_KLANG and form >= MIN_FORM
            rows.append((x[:-4], niva, klang, form, topp, ok))
            if not ok:
                bad.append(x[:-4])
    print(f"{'ljud':28s} {'nivå dB':>8s} {'klang dB':>9s} {'form':>6s} {'topp dBFS':>10s}")
    for name, niva, klang, form, topp, ok in rows:
        print(f"{name[:28]:28s} {niva:+8.2f} {klang:9.2f} {form:6.3f} {topp:10.1f}  {'ok' if ok else 'AVVIKER'}")
    nv = np.array([r[1] for r in rows]); kl = np.array([r[2] for r in rows]); fo = np.array([r[3] for r in rows])
    print(f"\nvärst: nivå {np.abs(nv).max():.2f} dB, klang {kl.max():.2f} dB, form {fo.min():.3f}"
          f"  |  medel: klang {kl.mean():.2f} dB, form {fo.mean():.3f}")
    print(f"gränser: nivå ±{MAX_NIVA} dB, klang {MAX_KLANG} dB, form ≥ {MIN_FORM}")
    if bad:
        print(f"\n{len(bad)} av {len(rows)} avviker: " + ", ".join(bad))
        sys.exit(1)
    print(f"\nalla {len(rows)} ljud ok")


if __name__ == "__main__":
    main()
