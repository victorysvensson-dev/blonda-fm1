"""Ljudens namn på FM-1: å ä ö i prickdisplayen, och FM-1:ans egna namn där två ljud annars heter lika.

BLONDA ONE har två motorer i en lista; i blonda-motor är de två pluggar, och där får BRIS och 3 OSC ha ljud med samma
namn. Här får varje ljud ett eget namn (NAMN), och ljud som låter nästan som ett annat i listan tas inte med (BORT,
läses av gen_ljud.py). Ljuden själva ändras inte.
"""

# (motor, namn i blonda-motor) -> namn på FM-1
NAMN = {
    ("one", "poly synth"): "trepoly",           # BRIS har Poly Synth
    ("bris", "init"): "init bris",
    ("bris", "blå pad"): "blå dimma",
    ("bris", "unison bas"): "tjockbas",
    ("bris", "oktavbas"): "studsbas",
    ("bris", "sub-bas"): "mullerbas",
    ("bris", "kvintlead"): "kvintsång",
    ("bris", "mjuk lead"): "flöjtlead",
    ("bris", "svep"): "tidvatten",
    ("bris", "s&h-bubbel"): "slumpbubbel",
    ("bris", "visselfilter"): "pipfilter",
    ("bris", "stor unison"): "ljudmur",
    ("bris", "kör"): "munkkör",
    ("bris", "mörk drone"): "mörker",
    ("bris", "arpeggio"): "kaskad",
    ("bris", "Pluck 1"): "Bright Pluck",
    ("bris", "Riser 2"): "Lift Off",
    ("bris", "Riser 3"): "Ascend",
    ("bris", "Riser 4"): "Updraft",
    ("bris", "Riser 5"): "Skyward",
    ("bris", "Juno Low Strings 1"): "Low Strings",
    ("bris", "Juno Low Strings 2"): "Dark Strings",
}

# ljud som inte tas med på FM-1: nästan samma ljud finns redan i listan (inom parentes)
BORT = {
    ("bris", "pluck"),          # (3 OSC pluck)
    ("bris", "ekopluck"),       # (3 OSC ekopluck)
    ("bris", "brass"),          # (Synth Brass)
    ("bris", "syntbrass"),      # (Synth Brass)
    ("bris", "Synth Horns"),    # (Synth Brass)
    ("bris", "klocka"),         # (Bellish)
    ("bris", "Strings"),        # (Ensemble)
    ("bris", "Winter"),         # (Ensemble)
}

KOD = {"å": "\\001", "Å": "\\001", "ä": "\\002", "Ä": "\\002", "ö": "\\003", "Ö": "\\003"}   # prickdisplayens tecken


def fm1(motor, namn):
    """namnet som FM-1 visar det"""
    return NAMN.get((motor, namn), namn)


def cstr(s):
    """till en C-sträng för prickdisplayen: å ä ö som egna tecken (tools/gen_dotfont.py), é som e"""
    s = s.replace("é", "e")
    out = []
    for c in s:
        if c in KOD:
            out.append(KOD[c])
        elif c == "\\" or c == '"':
            out.append("\\" + c)
        elif 32 <= ord(c) < 127:
            out.append(c)
        else:
            out.append("?")
    return "".join(out)


def las(c):
    """en C-sträng ur src/*_presets.h tillbaka till läsbar text (för kommentarer och jämförelser)"""
    return c.replace("\\001", "å").replace("\\002", "ä").replace("\\003", "ö").replace('\\"', '"').replace("\\\\", "\\")
