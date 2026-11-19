# BLONDA ONE för M-VAVE FM-1

BLONDA ONE är en synth med två ljudmotorer, som modul för [fm1-dock](https://github.com/joakimeriksson/fm1-dock) på
M-VAVE FM-1. Varje ljud hör till en motor, och motorn byts med ljudet. Version 2.0 (står överst på skärmen, efter
motorns namn: `3 OSC V2.0` eller `BRIS V2.0`).

| motor | ljud | vad den är |
|---|---|---|
| **3 OSC** | 000–049 | BRIS möter Moog: tre oscillatorer per röst (triangel, såg eller puls, fotlägen 32'–2'; oscillator 2 och 3 stäms ±7 halvtoner och ±50 cent), brus, en mixer som driver filtret (DRIVE) och en 4-polig stege där resonansen tunnar ut basen och på fullt självsvänger. ONE i `blonda-motor`. |
| **BRIS** | 050–139 | BLONDAs sexstämmiga analoga poly efter GS Music Bree6: oscillator + sub, 4-polig stege. BRIS i `blonda-motor`. |

Gemensamt för båda: sex röster (poly stereo, poly, mono, unison), LFO med fem former, två ADSR, stereoensemble och
eko. Efter motorerna, för alla ljud: reverbet **RUM**, **PUMP** (ducking i takt) och **ERA** (band och lofi).

Ljuden i ordning: 000 init (3 OSC) · 001 one (signaturljudet) · 13 basar · 9 lead · 13 poly och pad · 7 tangenter
och pluck · 6 effektljud · 050–138 BRIS 89 ljud (Poly Magic först) · 139 init (BRIS).

Fram till version 1.0 var det två moduler (BRIS 3.0 och ONE 1.0). Nu är allt en modul: ett ljud som sparats i
ONE 1.0 följer med, BRIS-modulen kan tas bort från FM-1:an.

## Byta mellan motorerna

Det märks inte mer än ett vanligt ljudbyte. Den gamla motorns toner tonas ut på ett anrop (5,8 ms, inget knäpp),
ekot och reverbet klingar vidare, och böj, modhjul, tryck och pedal följer med till den nya motorn. Skärmen behåller
sidan om den nya motorn har samma (reverbets sidor, FILTER, EG, LFO), annars knappens första sida. Ensemble, eko
och utgång är en enda uppsättning för båda motorerna (`src/bfx_fm1.c`).

## Varför en egen version av motorerna

FM-1:ans JieLi AC791N kan inte räkna med `double`, har ingen snabb `pow`/`exp`/`tanh` och en modul får 272 KiB RAM.
Motorerna i blonda-motor räknar allt i varje sampel med double och har en ekobuffert på 416 KB. Därför:

| | blonda-motor | FM-1 |
|---|---|---|
| tal | double | float (single precision, chipets FPU) |
| pow, exp, sin, tan, tanh | libm | egna approximationer (fel ~1e-7), `src/motor_matte.h` |
| tonhöjd, drift, filterkoefficient | varje sampel | var 16:e sampel, filtret interpolerat linjärt |
| koefficienter från parametrarna | varje sampel | när en parameter ändras |
| ekobuffert | 52 000 × 2 double (1 s) | 0,82 s × 2 int16 (Q15) |
| ensemblens buffert | 4096 × 2 | 512 × 2 (längsta fördröjningen är 437 sampel: samma ljud) |
| samplingsfrekvens | valfri | 44,1 kHz (FM-1) |

**Ekot går till 0,82 s här, mot 1 s i blonda-motor** (DELAY TIME över 91 ger 0,82 s). Minnet som blev över är
reverbets. Inget fabriksljud har längre eko än 0,64 s. I 3 OSC räknas varje oscillator för sig över blocket med sin
vågform, och en oscillator på nivå 0 räknas inte alls.

## Ljudet är testat mot blonda-motor

- **`test/likhet.py`, `test/bris/likhet.py`** – alla 140 ljud med samma toner, varje motor mot sitt original: nivå
  (±0,25 dB), klang i 1/3-oktavband (≤ 0,5 dB) och vågform (korrelation ≥ 0,98). Senast: värst 0,03 dB nivå,
  0,20 dB klang. Två 3 OSC-ljud där filtret självsvänger utan oscillator (visselfilter, laser) bedöms på nivå och
  klang: svängningen växer fram ur grundbruset och får en annan fas vid minsta avrundningsskillnad.
- **`test/scenarier.py`, `test/bris/scenarier.py`** – 22 + 17 scenarier där FM-1-motorn och originalet får exakt
  samma händelser: röststöld, legato med portamento, unison, sustainpedal, pitch bend, modhjul och tryck, filtret
  vrids, självsvängning, alla LFO-former, ljudbyte och röstläge under spel, ekot ändras, ytterlägen, full volym;
  för 3 OSC också vågform, fotläge, stämning, brus, drivning, pulsbredd och PWM under spel.
- **`test/modul_host.c`** – modulen körs oförändrad i en låtsas-FM-1 med fm1-dock:s eget typsnitt: skärmbilder av
  båda motorernas sidor, rattar, knappar, lysdioder, MIDI med bank, alla 140 ljud hörs och ingen klippning,
  motorbyte under en ton (inget knäpp, ingen ton hänger kvar, klangen ligger kvar, böj och hjul följer med),
  reverbet dör ut till tystnad, pumpen duckar på slaget och följer MIDI-klocka, två kärnor låter som en, anrop av
  olika längd, kärna 1 som inte hinner, och SAVE som överlever en omstart (också det som sparats av ONE 1.0).
- **Last och minne på chipet** – mäts av GitHub vid varje push: `one.dock` byggs med JieLis kompilator, skickas in
  i fm1-dock 0.7 på en emulerad FM-1 (fm1-emu) och spelar sex toner med ERA på TAPE, reverbet och pumpen på (det
  tyngsta läget). Bygget fallerar över 50 % av ljudavbrottet (fm1-dock:s råd), om ljudet uteblir eller om modulen
  inte ryms i minnet. `test/cykler/kor.py` ger ett snabbare ungefär här på datorn, för båda motorerna, och räknar
  minnet (senast 277 KB av 278,5).

Allt körs med `./kolla.sh` här i mappen (sätt `BLONDA_MOTOR` och `FM1_DOCK` om de inte ligger bredvid `blonda-fm1`).
Webbemulatorn i `web/` kör samma modul i webbläsaren.

## Så används den på FM-1

| | |
|---|---|
| tangenter / MIDI | 6 röster, velocity, pitch bend, modhjul (CC1), tryck, sustain (CC64). Program change = ljud 0–127; CC 0 = 1 först ger ljud 128–139 |
| PRESETS | byt ljud (140 st, laddas direkt) |
| KNOB1–4 | de fyra parametrarna som visas |
| SELECT | nästa / förra sidan |
| ALGORITHM | **ERA**: 0 MODERN (rent), 1–60 TAPE (kassett som slits mer och mer), 61–100 LOFI (färre bitar, lägre samplingsfrekvens). Visas uppe till höger på skärmen, sparas med SAVE, MIDI CC 77 |
| SEL | 3 OSC: FILTER (cutoff, resonans, EG1, drive) · KEYS (tangentföljning, anslag till filter och volym, böjomfång). BRIS: FILTER |
| EDIT | 3 OSC: OSC 1 · OSC 2 · OSC 3 (vågform, fotläge, finstämning, nivå; pulsbredd på OSC 1) · MIX (halvtoner för 2 och 3, brus, detune). BRIS: OSC |
| ENV · LFO | EG2 AMP / EG1 FILTER · LFO / LFO MOD |
| FX | FX (ensemble, eko) · RUM · RUM TONE · RUM SPACE · PUMP (tryck igen: nästa sida) |
| GLO | VOICE (röstläge, portamento, unisonintervall, volym); BRIS också PLAY (anslag, böjomfång, tangentföljning) |
| SAVE | sparar ljudet med ERA, reverb och pump; ONE startar med det nästa gång |
| HOME (håll) | fm1-dock:s väljare: byt till en annan modul |

Med KNOBS CC påslaget i fm1-dock kommer rattarna som CC 74, 71, 73, 72: cutoff, resonans, EG2 attack, release.

## RUM – reverbet

BLONDA RUM (samma reverb som Max for Live-pluggen, ur blonda-motor) ligger efter motorerna och före ERA: predelay,
lågskärning, fyra allpassfilter som sprider ljudet, åtta fördröjningslinjer i ett återkopplat nät med dämpning och
långsam rörelse, höskärning, bredd och ducking efter det man spelar.

| sida | rattar |
|---|---|
| RUM | REVERB MIX (0 = av, motorn låter exakt som utan) · SIZE · DECAY · TYPE (HALL / ROOM) |
| RUM TONE | LOW CUT · HIGH CUT (filtren på klangen) · DAMPING · MOVEMENT |
| RUM SPACE | PREDELAY · WIDTH · DUCK (klangen viker undan medan man spelar) · REVERB LEVEL |

MIDI CC 91 är mixen. Reverbet hör till modulen, inte till ljudet: det ligger kvar när man byter ljud, och sparas med
SAVE. Det ligger i `gemensamt/effekter/rum_fm1.c` och testas mot blonda-motor i 15 fall
(`gemensamt/test/rum_test.py`: skillnad −50 till −78 dB när inget vrids). Skillnader mot pluggen: linjerna ligger i
16 bitar med brusformning (avrundningsbruset läggs i diskanten, där linjernas dämpning tar bort det) i stället för
double, och klangen räknas ur ljudet före varje block om 256 sampel, så en predelay under 5,8 ms blir 5,8 ms.

## PUMP – ducking i takt

Som en kompressor med sidokedja från en bastrumma som inte hörs: på varje slag dras nivån ned och släpps mjukt
tillbaka. PUMP-sidan: PUMP DEPTH (0 = av) · PUMP RATE (1/1 till 1/16) · PUMP TARGET (REVERB: bara klangen pumpar,
ALL: allt) · TEMPO (40–240 BPM). Kommer MIDI-klocka följer pumpen den i stället, i takt och på slaget, och skärmen
visar `EXT` och klockans tempo; tystnar klockan i två sekunder gäller den egna takten igen. MIDI CC 92 är djupet.
PUMP finns bara här (inte i blonda-motor): `gemensamt/effekter/pump_fm1.c`, test i `gemensamt/test/pump_test.c`.

## ERA – inbyggt band och lofi

ALGORITHM-ratten styr BLONDA SLITEN (samma effekt som Max for Live-pluggen, ur blonda-motor) sist i kedjan:
kassettläge med mer slitage, mättnad, brus och mörkare ton ju längre upp, och från 61 det digitala läget med
färre bitar och lägre samplingsfrekvens. På 0 tonar bandet ut och slutar räknas, så motorn låter exakt som utan.
SLITEN ligger i `gemensamt/effekter/` så att fler moduler kan använda den, och testas mot blonda-motor i alla nio
media (`gemensamt/test/sliten_test.py`). Skillnad mot pluggen: bandbufferten är kortare (modulens minne), så de
digitala mediernas avbrott med upprepning används inte här.

## Två kärnor

Rösterna 3–5 räknas på FM-1:ans andra kärna när fm1-dock lånar ut den (`c1_run`), medan den första räknar röst
0–2. Sedan räknar den andra kärnan reverbets klang: medan den första räknar ensemble och eko, eller, när bandet
(ERA) är på, klangen för nästa anrop medan den första räknar bandet. Så väntar ingen kärna på den andra i det
tyngsta läget. Lånas den andra kärnan inte ut (de första ~0,75 s, eller en enhet som inte klarar kontrollen) räknas
allt på kärna 0; med reverb, ERA och sex röster blir det runt 60–70 % av ljudavbrottet. Testet i låtsas-FM-1 visar
att det låter likadant (skillnad under −100 dB utan reverb; med reverb skiljer bara reverbets eget avrundningsbrus,
under −70 dB).

## Filer

```
one_module.c, module.cfg    fm1-dock-modulen: skärm, panel, MIDI, motorbytet, reverb, pump och ERA
src/one_fm1.c, .h           trioscillatormotorn (ren C99, bara float, inga beroenden)
src/bris_fm1.c, .h          BRIS-motorn
src/bfx_fm1.c, .h           ensemble, eko och utgång, gemensamma för motorerna
src/motor_matte.h           snabb matte utan libm, gemensam
src/one_presets.h           parametrar och fabriksljud – GENERERADE ur blonda-motor (tools/gen_presets_one.py)
src/bris_presets.h          likadant för BRIS (tools/gen_presets_bris.py)
test/, test/bris/           likhet och scenarier per motor; låtsas-FM-1 (modul_host.c); cykler (last och minne)
```

Efter rösterna (ensemble, eko, utgång) och i röstfördelningen är motorerna samma, i blonda-motor och här. Ändras en
motor i blonda-motor (nya parametrar eller ljud): kör `./kolla.sh`. Den skriver om `src/*_presets.h` och testerna
visar om något låter annorlunda. Ändras själva ljudet i `generator/one.py` eller `generator/bris.py` måste samma
ändring göras i `src/one_fm1.c`, `src/bris_fm1.c` eller `src/bfx_fm1.c` – testerna slår larm tills det är gjort.
Höj då också `ONE_VERSION` i `one_module.c` och `version` i `module.cfg`. Nya ljud läggs sist i sin motor: då
flyttas BRIS-ljudens nummer om 3 OSC får fler (program change och sparat ljud pekar på numret).

## Bygga och installera

GitHub bygger `one.dock` vid varje push (`.github/workflows/bygg.yml` i repots rot). För hand: JieLis kompilator
(`fm1-dock/tools/get_toolchain.sh`), sedan `python3 tools/hbmod.py ../blonda-fm1/moduler/one` i fm1-dock. Modulen
installeras genom att släppa `.dock`-filen i fm1-dock:s
[konsol](https://joakimeriksson.github.io/fm1-dock/#console) (Chrome/Edge, FM-1 på USB), eller med
`tools/dock.py deploy build/modules/one.dock` i fm1-dock. fm1-dock själv installeras en gång från
[dess släpp](https://github.com/joakimeriksson/fm1-dock/releases) eller FM-1 Firmware Hub. Nödutgång: håll OCT−
när FM-1 slås på.

## Licens

GPL-3.0-only, som fm1-dock (se `LICENSE` i repots rot).
