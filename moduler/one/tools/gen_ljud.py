#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""BLONDA ONE:s ljudlista: ordningen (vilken motor och vilket ljud i den varje nummer är) och varje ljuds effekter
efter motorn (reverbet RUM, PUMP och ERA). Skriver src/ljud.h.

Ljuden själva (parametrar och namn) kommer ur blonda-motor (src/one_presets.h, src/bris_presets.h, genererade av
gen_presets_one.py och gen_presets_bris.py). Det som bestäms här är bara FM-1-modulens: i vilken ordning ljuden
ligger och vilka effekter som hör till varje ljud. Ändra här och kör skriptet, ändra aldrig src/ljud.h för hand.

Ordningen: 3 OSC 0-49 (init först), BRIS 1-89 utan de ljud som tools/namn.py tar bort (BORT: nästan samma ljud
finns redan), BRIS init, sedan 3 OSC 50- (de nya sedan 2.3). Nya ljud i blonda-motor läggs sist i sin motor och
hamnar sist här. Namnen som FM-1 visar (å ä ö, egna namn där två ljud hette lika) står i tools/namn.py. Ett sparat
ljud minns sin motor och sitt nummer i motorn, inte numret här.

  python3 tools/gen_ljud.py
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import namn  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "src")
ONE_OLD = 50                      # så många 3 OSC-ljud fanns före 2.3 (de ligger först)

# Effekterna, i modulens ordning (one_module.c, XP_rmix ..). Värden 0..1 (heltal som de är: typ, takt, mål), ERA 0..100.
FX = ["rmix", "rsize", "rdecay", "rtype", "rlowc", "rhighc", "rdamp", "rmodd", "rpredel", "rwidth", "rduck", "rout",
      "pdepth", "prate", "ptarget", "era"]
BAS = dict(rmix=0, rsize=0.55, rdecay=0.5, rtype=0, rlowc=0.25, rhighc=1, rdamp=0.45, rmodd=0.35, rpredel=0.15,
           rwidth=1, rduck=0, rout=0.75, pdepth=0, prate=2, ptarget=0, era=0)

# per sort: hur ett ljud av den sorten brukar låta i rum (pads stort och långt, bas torrt, lead lite och duckat)
SORT = {
    "init":  {},
    "bas":   {},
    "lead":  dict(rmix=0.18, rsize=0.5, rdecay=0.45, rduck=0.35, rpredel=0.25),
    "pad":   dict(rmix=0.35, rsize=0.75, rdecay=0.7, rlowc=0.3, rhighc=0.85, rmodd=0.45),
    "brass": dict(rmix=0.25, rsize=0.6, rdecay=0.55, rpredel=0.2),
    "poly":  dict(rmix=0.22, rsize=0.55, rdecay=0.5),
    "pluck": dict(rmix=0.25, rsize=0.55, rdecay=0.5, rpredel=0.2),
    "keys":  dict(rmix=0.2, rsize=0.4, rdecay=0.4, rtype=1),
    "orgel": dict(rmix=0.2, rsize=0.45, rdecay=0.45, rtype=1),
    "fx":    dict(rmix=0.4, rsize=0.85, rdecay=0.8, rmodd=0.6),
}

# 3 OSC: sorten efter platsen i listan (one_ljud.py: init, one, bas, lead, poly och pad, tangenter, effekter)
ONE_SORT = ["init", "poly"] + ["bas"] * 13 + ["lead"] * 9 + \
    ["pad", "pad", "pad", "brass", "poly", "orgel", "pad", "pad", "pad", "pad", "pad", "pad", "poly"] + \
    ["pluck", "keys", "keys", "pluck", "keys", "pluck", "pluck"] + ["fx", "pad", "fx", "fx", "fx", "fx"]
# 3 OSC 50-: klassikerna (sedan 2.3), var och en med sina effekter
ONE_NYA = {
    "hoppbrass":   ("brass", dict(rmix=0.2, rsize=0.5, rdecay=0.45, rtype=1)),
    "filmbrass":   ("brass", dict(rmix=0.45, rsize=0.9, rdecay=0.85, rpredel=0.3, rmodd=0.5)),
    "nattpad":     ("pad", dict(rmix=0.4, rsize=0.8, rdecay=0.7, rduck=0.2)),
    "funkmoog":    ("bas", {}),
    "visslead":    ("lead", dict(rmix=0.25, rsize=0.6, rdecay=0.55)),
    "glidlead":    ("lead", dict(rmix=0.2, rsize=0.55, rdecay=0.5)),
    "supersåg":    ("pad", dict(rmix=0.3, rsize=0.8, rdecay=0.65, pdepth=0.55, prate=2, ptarget=1)),
    "trancepluck": ("pluck", dict(rmix=0.3, rsize=0.7, rdecay=0.6, pdepth=0.5, prate=2, ptarget=0)),
    "stråkmaskin": ("pad", dict(rmix=0.35, rsize=0.75, rdecay=0.65)),
    "änglakör":    ("pad", dict(rmix=0.45, rsize=0.9, rdecay=0.8, rpredel=0.25)),
    "arenabrass":  ("brass", dict(rmix=0.3, rsize=0.75, rdecay=0.6)),
    "syntwavebas": ("bas", dict(rmix=0.08, rsize=0.4, rdecay=0.35, rlowc=0.45)),
    "syntwavearp": ("pluck", dict(rmix=0.3, rsize=0.7, rdecay=0.6)),
    "husorgel":    ("orgel", dict(rmix=0.18, pdepth=0.35, prate=2, ptarget=1)),
    "morrsvep":    ("lead", dict(rmix=0.15, rsize=0.5, rdecay=0.45)),
    "slapbas":     ("bas", {}),
    "mjukpwm":     ("pad", dict(rmix=0.3, rsize=0.7, rdecay=0.6)),
    "lofi-keys":   ("keys", dict(rmix=0.25, rsize=0.45, rdecay=0.45, rhighc=0.7, era=30)),
    "svävbas":     ("bas", {}),
    "åttabit":     ("lead", dict(rmix=0.1, rsize=0.35, rdecay=0.3, rtype=1, era=75)),
    # Oberheim-stil (blonda-motor 0.11)
    "glödpad":     ("pad", dict(rmix=0.35, rsize=0.8, rdecay=0.7)),
    "bländpoly":   ("poly", dict(rmix=0.2, rsize=0.6, rdecay=0.45)),
    "kvällsbrass": ("brass", dict(rmix=0.3, rsize=0.7, rdecay=0.6)),
    "rundbas":     ("bas", {}),
    "åskbas":      ("bas", {}),
    "dubbelpuls":  ("lead", dict(rmix=0.15, rsize=0.5, rdecay=0.45)),
    "vågmatta":    ("pad", dict(rmix=0.35, rsize=0.85, rdecay=0.75)),
    "skimmerklav": ("pluck", dict(rmix=0.2, rsize=0.5, rdecay=0.4)),
    "sidenväv":    ("pad", dict(rmix=0.3, rsize=0.75, rdecay=0.65)),
    "fanfar":      ("brass", dict(rmix=0.25, rsize=0.7, rdecay=0.55)),
}


def bris_sort(name):
    """BRIS: sorten efter namnet (engelska fabriksljud och BLONDAs egna)"""
    n = name.lower().replace("å", "a").replace("ä", "a").replace("ö", "o")
    if n == "init":
        return "init"
    for words, sort in ((("riser", "duckling", "audio rate", "s&h", "ringping", "visselfilter", "drone", "svep",
                          "twisted", "fake hi"), "fx"),
                        (("bass", "bas"), "bas"),
                        (("lead", "trumpet"), "lead"),
                        (("pad", "strings", "ensemble", "humana", "kor", "winter", "beauty", "romance", "opening",
                          "strang", "sines", "stor unison"), "pad"),
                        (("brass", "horns", "gladiator"), "brass"),
                        (("arp", "seq", "pluck", "strummer", "steel", "bell", "klocka", "arpeggio", "bits", "mute"),
                         "pluck"),
                        (("keys", "tremolo", "chordz"), "keys")):
        if any(w in n for w in words):
            return sort
    return "poly"


def names(path, table, motor):
    """ljudens namn i blonda-motor (src/*_presets.h har dem som FM-1 visar dem)"""
    t = open(path, encoding="utf-8").read()
    inv = {v: k[1] for k, v in namn.NAMN.items() if k[0] == motor}
    return [inv.get(n, n) for n in (namn.las(c) for c in re.findall(r'\{"((?:[^"\\]|\\.)*)", \d+, \d+\}', t[t.index(table + "["):]))]


def fx_of(sort, extra=None):
    d = dict(BAS)
    d.update(SORT[sort])
    d.update(extra or {})
    return d


def b(name, v):
    if name == "era":
        return int(v)
    if name in ("rtype", "prate", "ptarget"):
        return int(v)
    return int(round(v * 100))


def main():
    one = names(os.path.join(SRC, "one_presets.h"), "ONE_PRESETS", "one")
    bris = names(os.path.join(SRC, "bris_presets.h"), "BRIS_PRESETS", "bris")
    assert len(ONE_SORT) == ONE_OLD, len(ONE_SORT)
    have = {("one", n) for n in one} | {("bris", n) for n in bris}
    for k in list(namn.NAMN) + list(namn.BORT):
        assert k in have, "tools/namn.py: %s finns inte i blonda-motor" % (k,)
    shown = [namn.fm1("one", n) for n in one] + [namn.fm1("bris", n) for n in bris if ("bris", n) not in namn.BORT]
    dup = sorted({n for n in shown if shown.count(n) > 1 and n != "init"})
    assert not dup, "samma namn två gånger på FM-1: %s (tools/namn.py)" % dup
    NYA = ONE_NYA
    rows = []                                      # (motor, index, namn, sort, effekter)
    for i in range(ONE_OLD):
        rows.append(("ENG_ONE", i, namn.fm1("one", one[i]), ONE_SORT[i], fx_of(ONE_SORT[i])))
    for i in range(1, len(bris)):
        if ("bris", bris[i]) in namn.BORT:
            continue
        s = bris_sort(bris[i])
        rows.append(("ENG_BRIS", i, namn.fm1("bris", bris[i]), s, fx_of(s)))
    rows.append(("ENG_BRIS", 0, namn.fm1("bris", bris[0]), "init", fx_of("init")))
    for i in range(ONE_OLD, len(one)):
        s, extra = NYA[one[i]] if one[i] in NYA else ("poly", {})
        if one[i] not in NYA:
            print("OBS: %s (3 OSC %d) har inga egna effekter i ONE_NYA: får det som hör till poly" % (one[i], i))
        rows.append(("ENG_ONE", i, namn.fm1("one", one[i]), s, fx_of(s, extra)))
    assert len(rows) < 256
    out = ["/* SPDX-License-Identifier: GPL-3.0-only */",
           "/* GENERERAD av tools/gen_ljud.py: ändra där, inte här. BLONDA ONE:s ljud i ordning: motor, ljudet i motorn",
           " * och effekterna som hör till ljudet (" + ", ".join(FX) + "; 0..100, heltal som de är). */",
           "#ifndef LJUD_H", "#define LJUD_H", "#include <stdint.h>", "",
           "enum { ENG_ONE, ENG_BRIS };                     /* motorerna (one_module.c: ENGINES) */",
           "#define NLJUD %d" % len(rows),
           "#define LJUD_BORT %d                            /* motorernas ljud som inte är med (tools/namn.py: BORT) */" % len(namn.BORT), "#define LJUD_NFX %d" % len(FX),
           "typedef struct { uint8_t eng, idx, fx[LJUD_NFX]; } ljud_t;",
           "static const ljud_t LJUD[NLJUD] = {"]
    for k, (eng, idx, name, sort, fx) in enumerate(rows):
        vals = ", ".join(str(b(f, fx[f])) for f in FX)
        out.append("    {%s, %d, {%s}},   /* %03d %s (%s) */" % (eng, idx, vals, k, name.replace("*/", "* /"), sort))
    out += ["};", "", "#endif", ""]
    open(os.path.join(SRC, "ljud.h"), "w", encoding="utf-8", newline="\n").write("\n".join(out))
    sorts = {}
    for r in rows:
        sorts[r[3]] = sorts.get(r[3], 0) + 1
    print("src/ljud.h: %d ljud (%s)" % (len(rows), ", ".join("%s %d" % kv for kv in sorted(sorts.items()))))


if __name__ == "__main__":
    main()
