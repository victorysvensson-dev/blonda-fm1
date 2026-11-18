# BLONDA ONE för M-VAVE FM-1

ONE – BRIS möter Moog – som modul för [fm1-dock](https://github.com/joakimeriksson/fm1-dock) på M-VAVE FM-1.
Version 1.0 (står överst på skärmen som `ONE V1.0`).

**Från Moog:** tre oscillatorer per röst (triangel, såg eller puls, i fotlägen 32'–2'; oscillator 2 och 3 kan stämmas
±7 halvtoner och finstämmas ±50 cent), brus, en mixer som driver filtret (DRIVE) och en 4-polig stege där
resonansen tunnar ut basen och på fullt självsvänger.

**Från BRIS:** sex röster med poly stereo, poly, mono och unison, LFO med fem former, två ADSR, stereoensemble, eko
och ERA (band och lofi på ALGORITHM-ratten).

Samma parametrar, samma 49 ljud och samma signalväg som ONE i `blonda-motor` (`generator/one.py`), skriven om för
FM-1:ans chip på samma sätt som BRIS (se `moduler/bris/README.md`): float i stället för double, egna
approximationer i stället för libm, styrsignaler var 16:e sampel. Varje oscillator räknas för sig över blocket med
sin vågform, och en oscillator på nivå 0 räknas inte alls.

## Ljudet är testat mot blonda-motor

- **`test/likhet.py`** – alla 50 ljud (init + 49) med samma toner: nivå (±0,25 dB), klang i 1/3-oktavband (≤ 0,5 dB)
  och vågform (korrelation ≥ 0,98). Senast: värst 0,02 dB nivå, 0,20 dB klang. Två ljud där filtret självsvänger
  utan oscillator (visselfilter, laser) bedöms på nivå och klang: svängningen växer fram ur grundbruset och får en
  annan fas vid minsta avrundningsskillnad.
- **`test/scenarier.py`** – 22 scenarier där båda motorerna får exakt samma händelser: röststöld, legato med
  portamento, unison, sustainpedal, pitch bend, modhjul och tryck, filtret vrids, självsvängning, alla LFO-former,
  ljudbyte och röstläge under spel, ekot ändras, ytterlägen, full volym och full drivning, och det som är ONE:s
  eget: vågform, fotläge, halvtoner och finstämning ändras under spel, brus och drivning vrids upp, höga toner
  mot frekvenstaket, pulsbredd och PWM, S&H-LFO.
- **`test/modul_host.c`** – modulen körs oförändrad i en låtsas-FM-1 med fm1-dock:s eget typsnitt: skärmbilder av
  alla tolv sidor, rattar, knappar, lysdioder, MIDI, alla ljud hörs och ingen klippning, två kärnor låter som en,
  SAVE överlever en omstart.
- **Lasten på chipet** – mäts av GitHub vid varje push, som för BRIS (sex toner, ERA på TAPE, gräns 50 % av
  ljudavbrottet). `test/cykler/kor.py` ger ett ungefär på datorn: ONE med tre oscillatorer ligger 10–20 % över BRIS.

Allt körs med `./kolla.sh` här i mappen (sätt `BLONDA_MOTOR` och `FM1_DOCK` om de inte ligger bredvid `blonda-fm1`).
Webbemulatorn i `web/` kör samma modul i webbläsaren.

## Så används den på FM-1

| | |
|---|---|
| tangenter / MIDI | 6 röster, velocity, pitch bend, modhjul (CC1), tryck, sustain (CC64), program change = ljud |
| PRESETS | byt ljud (laddas direkt) |
| KNOB1–4 | de fyra parametrarna som visas |
| SELECT | nästa / förra sidan |
| ALGORITHM | **ERA**, som i BRIS: 0 MODERN (rent), 1–60 TAPE, 61–100 LOFI. Sparas med SAVE, MIDI CC 77 |
| SEL | FILTER (cutoff, resonans, EG1, drive) · KEYS (filtrets tangentföljning, anslag till filter och volym, böjomfång) |
| EDIT | OSC 1 · OSC 2 · OSC 3 (vågform, fotläge, finstämning, nivå; pulsbredd på OSC 1) · MIX (halvtoner för 2 och 3, brus, detune) |
| ENV · LFO · FX · GLO | EG2/EG1 · LFO/LFO MOD · ensemble och eko · röstläge, portamento, unisonintervall, volym |
| SAVE | sparar ljudet; ONE startar med det nästa gång |
| HOME (håll) | fm1-dock:s väljare: byt till en annan modul |

Ljuden i ordning: one (signaturljudet) · 13 basar · 9 lead · 13 poly och pad · 7 tangenter och pluck · 6 effektljud.

## Två kärnor

Som BRIS: rösterna 3–5 räknas på FM-1:ans andra kärna när fm1-dock lånar ut den (`c1_run`), resten och effekterna
på den första. Testet i låtsas-FM-1 visar att det låter likadant (skillnad under −100 dB).

## Filer

```
one_module.c, module.cfg    fm1-dock-modulen: skärm, panel, MIDI
src/one_fm1.c, .h           motorn (ren C99, bara float, inga beroenden)
src/one_presets.h           parametrar och fabriksljud – GENERERAD ur blonda-motor (tools/gen_presets.py)
test/                       likhet, scenarier, låtsas-FM-1, cykler
```

Efter rösterna (ensemble, eko, utgång) och röstfördelningen är ONE samma som BRIS, i blonda-motor och här. Ändras ONE
i blonda-motor (nya parametrar eller ljud): kör `./kolla.sh`. Den skriver om `src/one_presets.h` och testerna visar
om något låter annorlunda. Ändras själva ljudet i `generator/one.py` måste samma ändring göras i `src/one_fm1.c` –
testerna slår larm tills det är gjort. Höj då också `ONE_VERSION` i `one_module.c` och `version` i `module.cfg`.

## Bygga och installera

GitHub bygger `one.dock` vid varje push (`.github/workflows/bygg.yml` i repots rot). Modulen installeras genom att
släppa `.dock`-filen i fm1-dock:s [konsol](https://joakimeriksson.github.io/fm1-dock/#console) (Chrome/Edge, FM-1
på USB), eller med `tools/dock.py deploy build/modules/one.dock` i fm1-dock. Nödutgång: håll OCT− när FM-1 slås på.

## Licens

GPL-3.0-only, som fm1-dock (se `LICENSE` i repots rot).
