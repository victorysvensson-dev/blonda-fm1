#!/usr/bin/env python3
"""Skriver src/blonda_dot.h: BLONDA ONE:s prickdisplay (tecken om 5 x 7 prickar) ur teckningarna här nedanför.

Ett tecken ritas med # för en tänd prick. Ändra teckningen och kör skriptet; filen i src/ ändras aldrig för hand.
  python3 tools/gen_dotfont.py
"""
import os

GLYPHS = r"""
A      B      C      D      E      F      G      H      I      J      K      L      M
.###.  ####.  .###.  ####.  #####  #####  .###.  #...#  .###.  ..###  #...#  #....  #...#
#...#  #...#  #...#  #...#  #....  #....  #...#  #...#  ..#..  ...#.  #..#.  #....  ##.##
#...#  #...#  #....  #...#  #....  #....  #....  #...#  ..#..  ...#.  #.#..  #....  #.#.#
#####  ####.  #....  #...#  ####.  ####.  #.###  #####  ..#..  ...#.  ##...  #....  #.#.#
#...#  #...#  #....  #...#  #....  #....  #...#  #...#  ..#..  ...#.  #.#..  #....  #...#
#...#  #...#  #...#  #...#  #....  #....  #...#  #...#  ..#..  #..#.  #..#.  #....  #...#
#...#  ####.  .###.  ####.  #####  #....  .###.  #...#  .###.  .##..  #...#  #####  #...#

N      O      P      Q      R      S      T      U      V      W      X      Y      Z
#...#  .###.  ####.  .###.  ####.  .####  #####  #...#  #...#  #...#  #...#  #...#  #####
##..#  #...#  #...#  #...#  #...#  #....  ..#..  #...#  #...#  #...#  #...#  #...#  ....#
#.#.#  #...#  #...#  #...#  #...#  #....  ..#..  #...#  #...#  #...#  .#.#.  .#.#.  ...#.
#..##  #...#  ####.  #...#  ####.  .###.  ..#..  #...#  #...#  #.#.#  ..#..  ..#..  ..#..
#...#  #...#  #....  #.#.#  #.#..  ....#  ..#..  #...#  #...#  #.#.#  .#.#.  ..#..  .#...
#...#  #...#  #....  #..#.  #..#.  ....#  ..#..  #...#  .#.#.  ##.##  #...#  ..#..  #....
#...#  .###.  #....  .##.#  #...#  ####.  ..#..  .###.  ..#..  #...#  #...#  ..#..  #####

0      1      2      3      4      5      6      7      8      9      &      -      *
.###.  ..#..  .###.  ####.  ...#.  #####  .###.  #####  .###.  .###.  .##..  .....  .....
#...#  .##..  #...#  ....#  ..##.  #....  #....  ....#  #...#  #...#  #..#.  .....  ..#..
#..##  ..#..  ....#  ....#  .#.#.  ####.  #....  ...#.  #...#  #...#  #.#..  .....  #.#.#
#.#.#  ..#..  ...#.  .###.  #..#.  ....#  ####.  ..#..  .###.  .####  .#...  #####  .###.
##..#  ..#..  ..#..  ....#  #####  ....#  #...#  ..#..  #...#  ....#  #.#.#  .....  #.#.#
#...#  ..#..  .#...  ....#  ...#.  #...#  #...#  ..#..  #...#  ....#  #..#.  .....  ..#..
.###.  .###.  #####  ####.  ...#.  .###.  .###.  ..#..  .###.  .###.  .##.#  .....  .....

+      .      /      '      !      :      ?
.....  .....  ....#  ..#..  ..#..  .....  .###.
..#..  .....  ....#  ..#..  ..#..  ..#..  #...#
..#..  .....  ...#.  .....  ..#..  .....  ....#
#####  .....  ..#..  .....  ..#..  .....  ...#.
..#..  .....  .#...  .....  ..#..  .....  ..#..
..#..  ..#..  #....  .....  .....  ..#..  .....
.....  .....  #....  .....  ..#..  .....  ..#..
"""

glyphs = {}
blocks = [b for b in GLYPHS.strip("\n").split("\n\n")]
for b in blocks:
    lines = b.split("\n")
    names = lines[0].split()
    rows = [ln.split() for ln in lines[1:]]
    assert len(rows) == 7, b
    for i, ch in enumerate(names):
        g = [r[i] for r in rows]
        assert all(len(x) == 5 and set(x) <= set("#.") for x in g), (ch, g)
        glyphs[ch] = [int(x.replace("#", "1").replace(".", "0"), 2) for x in g]

chars = sorted(glyphs)
out = ["/* GENERERAD FIL – ändra inte för hand. Källa: tools/gen_dotfont.py (teckningarna står där). */",
       "/* BLONDA ONE:s prickdisplay: tecken om 5 x 7 prickar, en byte per rad (bit 4 = vänstra pricken). */",
       "#ifndef BLONDA_DOT_H", "#define BLONDA_DOT_H", "#include <stdint.h>", "",
       f"#define DOT_NGLYPHS {len(chars)}",
       "static const char DOT_CHARS[DOT_NGLYPHS + 1] = \"" + "".join("\\\\" if c == "\\" else "\\\"" if c == '"' else c for c in chars) + "\";",
       "static const uint8_t DOT_ROWS[DOT_NGLYPHS][7] = {"]
for c in chars:
    out.append("    {" + ", ".join(f"0x{v:02x}" for v in glyphs[c]) + "},   /* " + c + " */")
out += ["};", "",
        "/* tecknets sju rader, eller 0 (ett tecken som inte finns ritas som mellanrum; små bokstäver som stora) */",
        "static const uint8_t *dot_glyph(char c)",
        "{",
        "    int i;",
        "    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');",
        "    for (i = 0; i < DOT_NGLYPHS; i++) if (DOT_CHARS[i] == c) return DOT_ROWS[i];",
        "    return 0;",
        "}",
        "",
        "/* Namnets plats i fönstret (DOT_W x DOT_H bildpunkter): prickarnas avstånd (4: stora prickar, högst 9 tecken;",
        " * 3: mindre, högst 13 på en rad) och, för längre namn, var andra raden börjar (annars 0). */",
        "#define DOT_W 232",
        "#define DOT_H 48",
        "static int dot_layout(const char *s, int *len1, int *start2)",
        "{",
        "    int n = 0, i, cut = 0;",
        "    while (s[n]) n++;",
        "    *len1 = n; *start2 = 0;",
        "    if (n <= 9) return 4;",
        "    if (n <= 13) return 3;",
        "    for (i = 1; i <= 13 && i < n; i++) if (s[i] == ' ') cut = i;   /* dela vid sista mellanrummet som ryms */",
        "    if (cut) { *len1 = cut; *start2 = cut + 1; }",
        "    else { *len1 = 13; *start2 = 13; }",
        "    return 3;",
        "}",
        "/* prickens övre vänstra hörn i fönstret: tecken nr ci på rad line (0, 1), prick (gc, gr) i tecknet */",
        "static void dot_at(int pitch, int lines, int line, int ci, int gc, int gr, int *x, int *y)",
        "{",
        "    int rows = DOT_H / pitch, top = lines == 1 ? (rows - 7) / 2 : (rows - 15) / 2 + line * 8;",
        "    *x = 2 + (ci * 6 + gc) * pitch;",
        "    *y = (DOT_H - (rows * pitch - 1)) / 2 + (top + gr) * pitch;",
        "}",
        "", "#endif", ""]
here = os.path.dirname(os.path.abspath(__file__))
path = os.path.join(here, "..", "src", "blonda_dot.h")
open(path, "w", encoding="utf-8", newline="\n").write("\n".join(out))
print(os.path.normpath(path) + f": {len(chars)} tecken")
