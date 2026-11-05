# CLAUDE.md – för alla Claude-chattar som jobbar i blonda-fm1

BLONDAs synthar och effekter för M-VAVE FM-1, som moduler för fm1-dock. Victory (ägare) skriver på svenska och vill
ha svar, kommentarer och dokumentation på svenska; kodnamn på engelska.

## Grundregler

1. **En modul per mapp** i `moduler/<namn>/`: `module.cfg` (läses av fm1-dock:s `tools/hbmod.py`; mappnamnet blir
   `<namn>.dock`), `<namn>_module.c` (hb_module.h-gränssnittet), `src/` (ljudet), `test/`, `kolla.sh`, `README.md`.
   Lägg till den i tabellen i README.md. CI bygger alla mappar under `moduler/` av sig själv.
2. **Chipets regler** (fm1-dock `docs/RULES.md`): inga `double`, ingen 64-bitars division, float bara med
   `-mcpu=r3 -mfprev1 -ffp-contract=off` i `cflags`, ingen libm i ljudet, ingen allokering. En modul har 272 KiB RAM.
   Ljudavbrottet har 5,8 ms per 256 sampel vid 44,1 kHz: sikta under hälften (fm1-emu-mätningen i CI).
3. **Ljud från blonda-motor ska låta som i blonda-motor.** En motor som finns där portas hit och testas mot den:
   fabriksljuden (nivå, klang, form) och scenarier sampel för sampel. Parametrar och ljud genereras ur blonda-motor
   (se `moduler/bris/tools/gen_presets.py`) och ändras aldrig för hand här. Ändras ljudet i blonda-motor: gör samma
   ändring här och kör testerna.
4. **Kolla innan överlämning.** `./kolla.sh` måste vara grönt (alla moduler + webbemulatorn) innan något lämnas
   till Victory att prova.
5. **Det som delas ligger i `gemensamt/`**: `snabbmatte.h` (float-approximationer), `effekter/` (BLONDA-effekter
   portade ur blonda-motor, t.ex. SLITEN, med test mot originalet i `gemensamt/test/`), `wasm_host.c` (webbemulatorn).
   En modul tar in dem via `sources =` och `includes =` i sin `module.cfg`; byggskripten läser listan därifrån.
6. **Två kärnor**: tunga motorer delar upp rösterna och räknar en del på kärna 1 med fm1-dock:s `c1_run` / `c1_wait`
   (se `moduler/bris`), med reserv på kärna 0 när den inte lånas ut.
7. **GPL-3.0-only**, som fm1-dock och Felucca.
8. **Webbemulatorn tar med alla moduler** (`web/bygg.sh` bygger varje mapp i `moduler/`, väljaren överst på sidan).
   En modul med `HB_F_PANEL` (som PULS) får tangenterna och OCT-knapparna som händelser och tangenternas lysdioder
   visas; lägg till modulens namn och rubrik i `INFO` och dess hjälptext (`data-mod`) i `web/sida.html`.

## Kommandon

```sh
./kolla.sh                         # allt
sh moduler/bris/kolla.sh           # en modul
sh web/bygg.sh                     # webbemulatorn -> web/index.html
python3 web/test_sida.py           # webbemulatorn i Chrome (Playwright)
```
