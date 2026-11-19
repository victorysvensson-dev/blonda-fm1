#!/usr/bin/env python3
"""Uppskattar BLONDA ONE:s last och minne på FM-1.

Last: motorerna körs i en RISC-V-emulator (rv32imf) och instruktionerna per 256 sampel räknas med sex röster igång.
FM-1:ans pi32v2 är en annan men liknande RISC-kärna (fm1-emu räknar med en instruktion per cykel vid 240 MHz), så
siffran är ett ungefär (den riktiga blir omkring 15 % högre). Den riktiga mätningen görs i fm1-emu när GitHub bygger.

Minne: modulens alla filer byggs för rv32 och .data + .bss räknas ihop. En modul har 272 KiB (278 528 byte); fm1-dock
vägrar bygga en som är större. Skriptet slutar med fel om det blir för trångt, så att det syns här och inte först på
GitHub.
  python3 test/cykler/kor.py [3 OSC-ljud ...] [b<BRIS-ljud> ...]      t.ex.  kor.py 1 26 b1 b5
"""
import os
import struct
import subprocess
import sys

from unicorn import UC_ARCH_RISCV, UC_HOOK_BLOCK, UC_HOOK_MEM_WRITE, UC_MODE_RISCV32, Uc
from unicorn.riscv_const import UC_RISCV_REG_MSTATUS, UC_RISCV_REG_SP

HERE = os.path.dirname(os.path.abspath(__file__))
MOD = os.path.join(HERE, "..", "..")
SRC = os.path.join(MOD, "src")
GEM = os.path.join(MOD, "..", "..", "gemensamt")
DOCK = os.environ.get("FM1_DOCK", os.path.join(MOD, "..", "..", "..", "fm1-dock"))
BUDGET = 5.8e-3 * 240e6            # ljudavbrottets 5,8 ms per 256 sampel vid 240 MHz
RAM = 0x44000                      # en moduls minne (fm1-dock: HB_MOD_RAM_SIZE)
RAM_MARGIN = 512                   # pi32v2-bygget blir några hundra byte större än rv32-bygget
CF = ["clang", "--target=riscv32", "-march=rv32imf", "-mabi=ilp32f", "-O2", "-ffreestanding", "-fno-builtin",
      "-nostdlib", "-ffp-contract=off", f"-I{SRC}", f"-I{GEM}", f"-I{GEM}/effekter"]
OBJS = {"one": os.path.join(SRC, "one_fm1.c"), "bris": os.path.join(SRC, "bris_fm1.c"), "bfx": os.path.join(SRC, "bfx_fm1.c"),
        "sliten": os.path.join(GEM, "effekter", "sliten_fm1.c"), "rum": os.path.join(GEM, "effekter", "rum_fm1.c"),
        "pump": os.path.join(GEM, "effekter", "pump_fm1.c")}


def obj(name):
    return os.path.join(HERE, name + ".o")


def build():
    subprocess.run(CF + ["-c", os.path.join(HERE, "drv.c"), "-o", obj("drv")], check=True)
    for name, src in OBJS.items():
        subprocess.run(CF + ["-c", src, "-o", obj(name)], check=True)
    subprocess.run(["ld.lld", "-T", os.path.join(HERE, "link.ld"), obj("drv")] + [obj(n) for n in OBJS] +
                   ["-o", os.path.join(HERE, "one.elf")], check=True)
    subprocess.run(["llvm-objcopy", "-O", "binary", os.path.join(HERE, "one.elf"), os.path.join(HERE, "one.bin")], check=True)
    syms = {}
    for line in subprocess.run(["llvm-nm", os.path.join(HERE, "one.elf")], capture_output=True, text=True).stdout.splitlines():
        p = line.split()
        if len(p) == 3:
            syms[p[2]] = int(p[0], 16)
    return syms


def memory():
    """modulens .data + .bss (rv32-bygget), eller None utan fm1-dock:s hb_module.h"""
    inc = os.path.join(DOCK, "firmware", "dock")
    if not os.path.exists(os.path.join(inc, "hb_module.h")):
        return None
    subprocess.run(CF + [f"-I{inc}", "-c", os.path.join(MOD, "one_module.c"), "-o", obj("modul")], check=True)
    total = 0
    for name in list(OBJS) + ["modul"]:
        for line in subprocess.run(["llvm-size", "-A", obj(name)], capture_output=True, text=True).stdout.splitlines():
            p = line.split()
            if len(p) >= 2 and p[0] in (".data", ".sdata", ".bss", ".sbss"):
                total += int(p[1])
    return total


def run(syms, engine, preset, tape=0, rum=0, part=0):
    mu = Uc(UC_ARCH_RISCV, UC_MODE_RISCV32)
    mu.mem_map(0x10000, 0x100000)
    mu.mem_write(0x10000, open(os.path.join(HERE, "one.bin"), "rb").read())
    for name, v in (("ENGINE", engine), ("PRESET", preset), ("TAPE", tape), ("RUM", rum), ("PART", part)):
        mu.mem_write(syms[name], struct.pack("<i", v))
    mu.reg_write(UC_RISCV_REG_SP, 0x10000 + 0x100000 - 16)
    mu.reg_write(UC_RISCV_REG_MSTATUS, 0x6000)       # FPU på
    st = {"stage": 0, "n": 0}

    def blk(uc, addr, size, _):
        if st["stage"] == 1:
            st["n"] += size // 4

    def wr(uc, access, addr, size, value, _):
        if addr == syms["stage"]:
            st["stage"] = value
            if value == 2:
                uc.emu_stop()
    mu.hook_add(UC_HOOK_BLOCK, blk)
    mu.hook_add(UC_HOOK_MEM_WRITE, wr, begin=syms["stage"], end=syms["stage"] + 3)
    mu.emu_start(syms["_start"], 0, count=2_000_000_000)
    return st["n"] / 4                                # per block om 256


if __name__ == "__main__":
    syms = build()
    args = sys.argv[1:] or ["1", "26", "28", "b1", "b5", "b38"]
    worst = 0.0
    pct = lambda n: n / BUDGET * 100                  # noqa: E731
    for a in args:
        engine, p = (1, int(a[1:])) if a.startswith("b") else (0, int(a))
        allt = run(syms, engine, p)                   # motorn ensam: sex röster, ensemble och eko
        fx = run(syms, engine, p, part=2)             # utan röster: LFO, ensemble och eko
        half = run(syms, engine, p, part=1)           # röst 0..2 och det
        tail = run(syms, engine, p, rum=1) - allt     # reverbets klang (kärna 1)
        mix = run(syms, engine, p, rum=2) - allt      # reverbets mix och pumpen (kärna 0)
        tape = run(syms, engine, p, tape=1) - allt    # bandet (kärna 0)
        # Så länge kärna 0 är i ljudavbrottet, som modulen lägger arbetet (one_module.c, render):
        #   utan band: röst 0..2, sedan ensemble och eko medan kärna 1 räknar klangen (kärna 0 väntar på den), mix
        #   med band:  röst 0..2, ensemble och eko, mix, sedan bandet medan kärna 1 räknar nästa anrops klang
        k0_utan = (half - fx) + max(fx, tail) + mix
        k0_med = half + mix + max(tape, tail)
        k1 = (allt - half) + tail                     # kärna 1: röst 3..5 och klangen
        worst = max(worst, pct(k0_utan), pct(k0_med), pct(k1))
        print(f"{'BRIS ' if engine else '3 OSC'} ljud {p:2d}: motorn ensam {pct(allt):5.1f} %   med reverb och pump: kärna 0 "
              f"{pct(k0_utan):5.1f} %, med TAPE också {pct(k0_med):5.1f} %; kärna 1 {pct(k1):5.1f} %")
    print(f"högst på en kärna: {worst:.1f} % av ljudavbrottet (fm1-dock:s råd: under 50 %; den riktiga blir omkring 15 % högre)")
    mem = memory()
    if mem is None:
        print("minne: fm1-dock saknas (FM1_DOCK), räknas inte här")
    else:
        print(f"minne: {mem} av {RAM} byte ({RAM - mem} kvar)")
        if mem > RAM - RAM_MARGIN:
            sys.exit(f"FEL: modulen får inte plats i FM-1:ans minne med marginal ({RAM_MARGIN} byte)")
    if worst * 1.15 > 50:
        sys.exit("FEL: lasten på en kärna blir över hälften av ljudavbrottet")
