# BLONDA BRIS för M-VAVE FM-1

BRIS – BLONDAs sexstämmiga analoga poly efter GS Music Bree6 – som modul för
[fm1-dock](https://github.com/joakimeriksson/fm1-dock) på M-VAVE FM-1. Samma parametrar, samma 90 ljud och samma
signalväg som BRIS i `blonda-motor` (oscillator + sub, 4-polig ladder, 2 × ADSR, LFO, ensemble, eko), skriven om
för FM-1:ans chip.

## Varför en egen version

FM-1:ans JieLi AC791N kan inte räkna med `double`, har ingen snabb `pow`/`exp`/`tanh` och en modul får 272 KiB RAM.
BRIS i blonda-motor räknar allt i varje sampel med double och har en ekobuffert på 416 KB. Därför:

| | blonda-motor | FM-1 |
|---|---|---|
| tal | double | float (single precision, chipets FPU) |
| pow, exp, sin, tan, tanh | libm | egna approximationer (fel ~1e-7) |
| tonhöjd, drift, filterkoefficient | varje sampel | var 16:e sampel, filtret interpolerat linjärt |
| koefficienter från parametrarna | varje sampel | när en parameter ändras |
| ekobuffert | 52 000 × 2 double | 1 s × 2 int16 (Q15) |
| samplingsfrekvens | valfri | 44,1 kHz (FM-1) |

Ljudet ska vara detsamma. Det bevisas mot blonda-motor, inte bara påstås:

- **`test/likhet.py`** – alla 90 fabriksljud med samma toner: nivå (±0,25 dB), klang i 1/3-oktavband (≤ 0,5 dB)
  och vågform (korrelation ≥ 0,98). Senast: värst 0,03 dB nivå, 0,19 dB klang; medel 0,02 dB.
- **`test/scenarier.py`** – 17 scenarier där båda motorerna får exakt samma händelser: röststöld, legato med
  portamento, unison, sustainpedal, pitch bend, modhjul och tryck, filtret vrids, självsvängning, alla LFO-former,
  ljudbyte och röstläge under spel, ekot ändras, ytterlägen, full volym.
- **`test/modul_host.c`** – modulen körs oförändrad i en låtsas-FM-1 med fm1-dock:s eget typsnitt: skärmbilder,
  rattar, knappar, lysdioder, MIDI, alla 90 ljud hörs och ingen klippning, SAVE överlever en omstart.
- **Lasten på chipet** – mäts av GitHub vid varje push: `bris.dock` byggs med JieLis kompilator, skickas in i
  fm1-dock 0.7 på en emulerad FM-1 (fm1-emu) och spelar sex toner med ERA på TAPE (det tyngsta läget). Bygget
  fallerar över 50 % av ljudavbrottet (fm1-dock:s råd) eller om ljudet uteblir. Första mätningen, en kärna och utan
  ERA: 39,3 %. `test/cykler/kor.py` ger ett snabbare ungefär här på datorn.

Allt körs med `./kolla.sh` här i mappen (sätt `BLONDA_MOTOR` och `FM1_DOCK` om de inte ligger bredvid `blonda-fm1`).
Webbemulatorn i `web/` kör samma modul i webbläsaren.

## Så används den på FM-1

| | |
|---|---|
| tangenter / MIDI | 6 röster, velocity, pitch bend, modhjul (CC1), tryck, sustain (CC64), program change = ljud |
| PRESETS | byt ljud (laddas direkt) |
| KNOB1–4 | de fyra parametrarna som visas |
| SELECT | nästa / förra sidan |
| ALGORITHM | **ERA**: 0 MODERN (rent), 1–60 TAPE (kassett som slits mer och mer), 61–100 LOFI (färre bitar, lägre samplingsfrekvens). Visas uppe till höger på skärmen, sparas med SAVE, MIDI CC 77 |
| SEL · EDIT · ENV · LFO · FX · GLO | FILTER · OSC · EG2/EG1 · LFO/LFO MOD · FX · VOICE/PLAY (tryck igen: andra halvan) |
| SAVE | sparar ljudet; BRIS startar med det nästa gång |
| HOME (håll) | fm1-dock:s väljare: byt till en annan modul |

## ERA – inbyggt band och lofi

ALGORITHM-ratten styr BLONDA SLITEN (samma effekt som Max for Live-pluggen, ur blonda-motor) efter BRIS:
kassettläge med mer slitage, mättnad, brus och mörkare ton ju längre upp, och från 61 det digitala läget med
färre bitar och lägre samplingsfrekvens. På 0 tonar bandet ut och slutar räknas, så BRIS låter exakt som förut.
SLITEN ligger i `gemensamt/effekter/` så att fler moduler kan använda den, och testas mot blonda-motor i alla nio
media (`gemensamt/test/sliten_test.py`). Skillnad mot pluggen: bandbufferten är kortare (modulens minne), så de
digitala mediernas avbrott med upprepning används inte här.

## Två kärnor

Rösterna 3–5 räknas på FM-1:ans andra kärna när fm1-dock lånar ut den (`c1_run`), resten och effekterna på den
första. Lånas den inte ut (de första ~0,75 s, eller en enhet som inte klarar kontrollen) räknas allt på kärna 0.
Testet i låtsas-FM-1 visar att det låter likadant (skillnad under −100 dB).

## Filer

```
bris_module.c, module.cfg   fm1-dock-modulen: skärm, panel, MIDI
src/bris_fm1.c, .h          motorn (ren C99, bara float, inga beroenden)
src/bris_presets.h          parametrar och fabriksljud – GENERERAD ur blonda-motor (tools/gen_presets.py)
test/                       likhet, scenarier, låtsas-FM-1, cykler
```

Ändras BRIS i blonda-motor (nya parametrar eller ljud): kör `./kolla.sh`. Den skriver om `src/bris_presets.h` och
testerna visar om något låter annorlunda. Ändras själva ljudet i `generator/bris.py` måste samma ändring göras i
`src/bris_fm1.c` – testerna slår larm tills det är gjort.

## Bygga och installera

GitHub bygger `bris.dock` vid varje push (`.github/workflows/bygg.yml` i repots rot). För hand: JieLis kompilator
(`fm1-dock/tools/get_toolchain.sh`), sedan `python3 tools/hbmod.py ../blonda-fm1/moduler/bris` i fm1-dock. Modulen
installeras i fm1-dock med `tools/dock.py deploy build/modules/bris.dock` eller genom att släppa `.dock`-filen i
fm1-dock:s [konsol](https://joakimeriksson.github.io/fm1-dock/#console) (Chrome/Edge, FM-1 på USB).
fm1-dock själv installeras en gång från [dess släpp](https://github.com/joakimeriksson/fm1-dock/releases) eller
FM-1 Firmware Hub. Nödutgång: håll OCT− när FM-1 slås på.

## Licens

GPL-3.0-only, som fm1-dock (se `LICENSE` i repots rot).
