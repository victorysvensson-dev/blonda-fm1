# BLONDA PULS för M-VAVE FM-1

PULS – BLONDAs trummaskin med åtta syntade trumljud och en 16-stegssekvenser – som modul för
[fm1-dock](https://github.com/joakimeriksson/fm1-dock) på M-VAVE FM-1. Samma trumljud, rattar och trumset som PULS i
`blonda-motor` (bastrumma, virvel, kantslag, handklapp, stängd och öppen hihat, puka, koskälla), skriven om för
FM-1:ans chip. Inga samplingar: allt räknas fram, så det får plats och kan vridas på.

## Så används den på FM-1

PULS tar hela panelen (fm1-dock:s `HB_F_PANEL`): tangenterna blir steg och trumljud.

| | |
|---|---|
| **vita tangenter** (16) | stegen 1–16 för trumljudet som är valt. Lyser = slag. Under spel glöder steget som spelas |
| **svarta tangenter** (de 8 första) | välj och spela trumljud: BD SD RS CP CH OH TOM CB |
| PLAY | start / stopp |
| REC | spela in: det du spelar (svarta tangenter, LIVE eller MIDI) läggs på närmaste steg |
| SEQ | STEG (lyser) eller LIVE: i LIVE spelar alla tangenter trumljud som MIDI (C BD, D SD, F# CH, A# OH …) |
| ARP | accenter: de vita tangenterna sätter accent (starkare slag) i stället för slag |
| FX SEL ENV LFO EDIT GLO | mönster 1–6. Under spel byts mönstret när takten är slut. Håll ett och tryck ett annat: kopiera |
| SELECT | sida: BD … CB, GLOBAL |
| KNOB1–4 | trumljudets nivå, stämning, längd och karaktär (punch, snärt, klick, spridning, brus, svep, ton). GLOBAL: tempo, swing, drivning, volym |
| PRESETS | trumset (10 st: init, Klassisk, Hård, Boom bap, Tajt, Elektro, Djup, Industri, Mjuk, Lofi) |
| ALGORITHM | **ERA**, som i BRIS: 0 MODERN, 1–60 TAPE, 61–100 LOFI (MIDI CC 77) |
| OCT− / OCT+ | tempo −1 / +1 BPM |
| SAVE | sparar allt: trumset och rattar, alla sex mönster, tempo, swing, ERA |
| HOME (håll) | fm1-dock:s väljare: byt till en annan modul |

Skärmen visar trumset och ERA överst, sidan, mönster och tempo, alla åtta trumljudens steg i ett rutnät (det valda
i orange, accenterna i raden under, stegmarkören ovanför) och sidans fyra rattar. MIDI in: toner spelar trumljud,
program change byter trumset. Med fm1-dock:s KNOBS CC styr CC 74, 71, 73, 72 det valda trumljudets fyra rattar.

## Samma ljud som blonda-motor

- **`test/likhet.py`** – alla tio trumset spelar samma trummönster i båda: nivå, klang (1/3-oktavband) och form.
  Senast: 0,00 dB nivå, 0,00 dB klang, form 1,000.
- **`test/scenarier.py`** – elva scenarier sampel för sampel: mönster i olika tempo, varje trumljud med styrka 1 och
  127, strypning av öppen hihat, pukor i fyra höjder, snabba omslag, alla rattar vrids under spel, trumset byts mitt i
  mönstret, full drivning, längsta klangen, ytterlägen. Skillnaden ligger 37–83 dB under signalen.
- **`test/modul_host.c`** – modulen oförändrad i en låtsas-FM-1: att steg 1 och 9 kommer exakt en sekund isär i
  120 BPM (±2 sampel), swing, accenter, REC, mönsterbyte vid taktslut, kopiering, LIVE, alla trumset utan klippning,
  ERA, MIDI, att allt ritas inom skärmen och att SAVE överlever en omstart.
- **Lasten** – `test/cykler/kor.py` uppskattar värsta fallet (alla åtta trumljud klingar samtidigt) till 15 % av
  ljudavbrottet, 27 % med ERA på TAPE. GitHub mäter `puls.dock` i fm1-dock på en emulerad FM-1 vid varje push.

Allt körs med `./kolla.sh` här i mappen.

## Hur porten är gjord

Signalvägen följer `blonda-motor/motorer/puls/puls.genexpr` rad för rad, men i float och utan libm. Allt som bara
beror på rattarna (avklingningar, filterkoefficienter, tonhöjder) räknas när en ratt ändras. Varje trumljud räknas
över hela blocket på en gång. Bruset är samma följd som originalets (ett värde per sampel, delat av alla trumljud
som i en TR-808). Hihattarnas och koskällans oscillatorer går hela tiden, med fas i 32-bitars heltal, så att de
följer originalets double exakt. Sekvensern körs i ljudavbrottet och delar blocket vid varje steg, så slagen hamnar på
rätt sampel.

## Filer

```
puls_module.c, module.cfg   fm1-dock-modulen: sekvensern, skärm, panel, MIDI
src/puls_fm1.c, .h          trummotorn (ren C99, bara float, inga beroenden)
src/puls_presets.h          rattar och trumset – GENERERAD ur blonda-motor (tools/gen_presets.py)
test/                       likhet, scenarier, låtsas-FM-1, cykler
```

Ändras PULS i blonda-motor: kör `./kolla.sh`. Den skriver om `src/puls_presets.h`, och testerna visar om något
låter annorlunda. Ändras själva ljudet i `generator/puls.py` måste samma ändring göras i `src/puls_fm1.c`.

## Licens

GPL-3.0-only, som fm1-dock (se `LICENSE` i repots rot).
