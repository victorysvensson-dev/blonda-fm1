# BLONDA för M-VAVE FM-1

BLONDAs synthar och effekter för M-VAVE FM-1, som moduler för [fm1-dock](https://github.com/joakimeriksson/fm1-dock):
fm1-dock installeras en gång på FM-1, och modulerna ligger sedan sida vid sida i dess flash. Håll HOME för att byta.

| Modul | Vad | Ljudet kommer från |
|---|---|---|
| [`moduler/bris`](moduler/bris) | BLONDA BRIS – sexstämmig analog poly efter GS Music Bree6, 90 ljud | `blonda-motor` (BRIS), testad mot den |
| [`moduler/puls`](moduler/puls) | BLONDA PULS – trummaskin: åtta syntade trumljud, 16-stegssekvenser, sex mönster, 10 trumset | `blonda-motor` (PULS), testad mot den |

Varje modul kan också provas i webbläsaren: `web/` kör exakt samma modulkod, kompilerad till WebAssembly, med en
spelbar panel (tangenter, rattar, knappar, skärm, datorns tangentbord och MIDI-keyboard).

## Mappar

```
moduler/<namn>/      en modul per synth eller effekt: module.cfg, <namn>_module.c, src/, test/, kolla.sh, README.md
gemensamt/           det modulerna delar: webbemulatorns FM-1 (wasm_host.c)
web/                 webbemulatorn: sida.html + bygg.sh  ->  index.html (allt i en fil)
.github/workflows/   bygger alla moduler till .dock med JieLis kompilator och mäter lasten i fm1-emu
```

## Kontrollera allt

```sh
./kolla.sh            # varje moduls kolla.sh, sedan webbemulatorn
```

`blonda-motor` och `fm1-dock` förväntas ligga bredvid `blonda-fm1` (annars: `BLONDA_MOTOR=…`, `FM1_DOCK=…`).

## Bygga för FM-1

GitHub bygger alla moduler vid varje push och lägger `.dock`-filerna och lastmätningen som artefakt på körningen.
En tagg (`v0.1.0`) gör ett släpp med filerna. På FM-1: installera fm1-dock från dess släpp eller FM-1 Firmware Hub,
släpp sedan `.dock`-filen i fm1-dock:s [konsol](https://joakimeriksson.github.io/fm1-dock/#console) (Chrome eller
Edge, FM-1 med USB-kabel direkt i datorn). Nödutgång: håll OCT− när FM-1 slås på.

## Licens

GPL-3.0-only, som fm1-dock och Felucca som det bygger på. Den som får en modul ska kunna få källkoden.
