/* SPDX-License-Identifier: GPL-3.0-only */
/* BLONDA PULS för M-VAVE FM-1 – trummaskin med åtta syntade trumljud.
 *
 * Samma signalväg, parametrar och trumset som PULS i blonda-motor (generator/puls.py), skriven för FM-1:ans chip:
 * bara float, ingen libm, snabba approximationer. Allt som bara beror på rattarna (avklingningar, filter, tonhöjd)
 * räknas när en ratt ändras, inte i varje sampel. Bruset är samma följd som i blonda-motor (ett värde per sampel,
 * delat av alla trumljud), så test/likhet.py och test/scenarier.py kan jämföra sampel för sampel.
 *
 * Trådar: puls_render() körs i ljudavbrottet. puls_hit(), puls_set() m.fl. får anropas från huvudloopen (eller
 * från ljudavbrottet före puls_render, som sekvensern gör); ett slag gäller från början av nästa puls_render. */
#ifndef PULS_FM1_H
#define PULS_FM1_H
#include <stdint.h>
#include "puls_presets.h"

#ifndef PULS_SR
#define PULS_SR 44100                           /* FM-1:ans samplingsfrekvens */
#endif
#define PULS_NV 8                               /* BD SD RS CP CH OH TOM CB */
enum { PV_BD = 0, PV_SD, PV_RS, PV_CP, PV_CH, PV_OH, PV_TOM, PV_CB };

void puls_init(void);                           /* nollställer allt och laddar init */
void puls_reset(void);                          /* tystnad, tillståndet nollat, rattarna kvar */
void puls_render(float *L, float *R, uint32_t n);   /* skriver (ersätter) n sampel */

/* Samma sak i tre steg, för två kärnor (fm1-dock:s c1_run): begin, sedan trumljuden i två delar (den ena på andra
 * kärnan, till mix 1), sedan end. n högst PULS_MAXN. Delarna får inte överlappa. */
#define PULS_MAXN 256
void puls_render_begin(uint32_t n);
void puls_render_voices(uint32_t first, uint32_t last, uint32_t mix);
void puls_render_end(float *L, float *R);

void puls_hit(int voice, float vel);            /* slå an trumljud 0..7, styrka 0..1 (minst 0,05) */
void puls_hit_tom(float vel, int semi);         /* pukan i en annan höjd (halvtoner) */
void puls_note_on(int note, int vel);           /* tonnummer % 12 -> trumljud, som blonda-motors Puls::noteOn */

void puls_set(int param, float value);          /* begränsas till 0..1 */
float puls_get(int param);
void puls_load_preset(int index);               /* ej nämnda parametrar får standardvärdet, utom volymen */

uint32_t puls_meter_voices(void);               /* bit i = trumljud i klingar */
float puls_meter_peak(void);

#endif
