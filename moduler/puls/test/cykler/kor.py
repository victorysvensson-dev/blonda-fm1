#!/usr/bin/env python3
"""Uppskattar PULS last på FM-1: kör motorn i en RISC-V-emulator (rv32imf) och räknar instruktioner per
256 sampel med alla åtta trumljud igång (värsta fallet). FM-1:ans pi32v2 är en annan men liknande RISC-kärna (fm1-emu räknar med en
instruktion per cykel vid 240 MHz), så siffran är ett ungefär – den riktiga mätningen görs på FM-1 med
fm1-dock:s probes / render-last när kompilatorn finns.
  python3 test/cykler/kor.py [ljud ...]
"""
import os
import struct
import subprocess
import sys

from unicorn import UC_ARCH_RISCV, UC_HOOK_BLOCK, UC_HOOK_MEM_WRITE, UC_MODE_RISCV32, Uc
from unicorn.riscv_const import UC_RISCV_REG_MSTATUS, UC_RISCV_REG_PC, UC_RISCV_REG_SP

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "..", "src")
GEM = os.path.join(HERE, "..", "..", "..", "..", "gemensamt")
BUDGET = 5.8e-3 * 240e6            # ljudavbrottets 5,8 ms per 256 sampel vid 240 MHz
CF = ["clang", "--target=riscv32", "-march=rv32imf", "-mabi=ilp32f", "-O2", "-ffreestanding", "-fno-builtin",
      "-nostdlib", "-ffp-contract=off", f"-I{SRC}", f"-I{GEM}", f"-I{GEM}/effekter"]


def build():
    subprocess.run(CF + ["-c", os.path.join(HERE, "drv.c"), "-o", os.path.join(HERE, "drv.o")], check=True)
    subprocess.run(CF + ["-c", os.path.join(SRC, "puls_fm1.c"), "-o", os.path.join(HERE, "puls.o")], check=True)
    subprocess.run(CF + ["-c", os.path.join(GEM, "effekter", "sliten_fm1.c"), "-o", os.path.join(HERE, "sliten.o")], check=True)
    subprocess.run(["ld.lld", "-T", os.path.join(HERE, "link.ld"), os.path.join(HERE, "drv.o"), os.path.join(HERE, "puls.o"), os.path.join(HERE, "sliten.o"),
                    "-o", os.path.join(HERE, "puls.elf")], check=True)
    subprocess.run(["llvm-objcopy", "-O", "binary", os.path.join(HERE, "puls.elf"), os.path.join(HERE, "puls.bin")], check=True)
    syms = {}
    for line in subprocess.run(["llvm-nm", os.path.join(HERE, "puls.elf")], capture_output=True, text=True).stdout.splitlines():
        p = line.split()
        if len(p) == 3:
            syms[p[2]] = int(p[0], 16)
    return syms


def run(syms, preset, tape=0, core=0):
    mu = Uc(UC_ARCH_RISCV, UC_MODE_RISCV32)
    mu.mem_map(0x10000, 0x100000)
    mu.mem_write(0x10000, open(os.path.join(HERE, "puls.bin"), "rb").read())
    mu.mem_write(syms["PRESET"], struct.pack("<i", preset))
    mu.mem_write(syms["TAPE"], struct.pack("<i", tape))
    mu.mem_write(syms["CORE"], struct.pack("<i", core))
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
    presets = [int(a) for a in sys.argv[1:]] or [7, 1]
    for p in presets:
        for tape in (0, 1):
            n = run(syms, p, tape)
            c0, c1 = run(syms, p, tape, 1), run(syms, p, tape, 2)
            t = " + TAPE" if tape else "       "
            print(f"trumset {p}{t}: en kärna {n / BUDGET * 100:5.1f} %  |  två kärnor: kärna 0 {c0 / BUDGET * 100:5.1f} %, "
                  f"kärna 1 {c1 / BUDGET * 100:5.1f} %  (av ljudavbrottet)")
    print("fm1-dock:s råd: under halva (50 %) ger marginal för USB och tangenterna")
