/* SPDX-License-Identifier: GPL-3.0-only */
/* BLONDA ONE för M-VAVE FM-1 – BRIS möter Moog: sexstämmig analog poly med tre oscillatorer per röst (triangel,
 * såg, puls; fotlägen 32'–2'), brus, en mixer som driver en 4-polig stege, och BRIS LFO, höljen, ensemble och eko.
 *
 * Samma signalväg, parametrar och fabriksljud som ONE i blonda-motor (generator/one.py), men skriven för
 * FM-1:ans chip (JieLi AC791N): bara float (inga double), ingen libm, snabba approximationer och styrsignaler
 * (tonhöjd, filterkoefficienter) räknade en gång per block om ONE_K sampel. Ljudet är nära originalet,
 * inte bit-identiskt; test/likhet.py mäter skillnaden mot blonda-motor för varje fabriksljud.
 *
 * Trådar: one_render() körs i ljudavbrottet. one_note_on(), one_set() m.fl. körs i huvudloopen och skriver
 * bara enskilda 32-bitarsvärden som render läser en gång per block. */
#ifndef ONE_FM1_H
#define ONE_FM1_H
#include <stdint.h>
#include "one_presets.h"

#ifndef ONE_SR
#define ONE_SR 44100                           /* FM-1:ans samplingsfrekvens */
#endif
#ifndef ONE_K
#define ONE_K 16                               /* sampel per styrblock */
#endif
#define ONE_NV 6
#define ONE_CHN 4096                           /* ensemblens buffert, som originalet */
#define ONE_DLN (ONE_SR + 64)                 /* ekot: 1 s (int16, Q15: originalets 52000 float får inte plats) */

void one_init(void);                           /* nollställer allt och laddar init-ljudet */
void one_reset(void);                          /* tystar och nollställer ljudet, behåller parametrarna */
void one_render(float *L, float *R, uint32_t n);   /* skriver n sampel (n delbart med ONE_K går snabbast) */

/* Samma sak i tre steg, för två kärnor (fm1-dock:s c1_run): begin, sedan rösterna i två delar (den ena på andra
 * kärnan, till mix 1), sedan end. n högst ONE_MAXN. Delarna får inte överlappa. */
#define ONE_MAXN 256
void one_render_begin(uint32_t n);
void one_render_voices(uint32_t first, uint32_t last, uint32_t mix);
void one_render_end(float *L, float *R);

void one_note_on(int note, int vel);           /* vel 1..127 */
void one_note_off(int note);
void one_all_notes_off(void);
void one_sustain(int down);
void one_bend(float v);                        /* -1..1 */
void one_modwheel(float v);                    /* 0..1 */
void one_aftertouch(float v);                  /* 0..1 */

void one_set(int param, float value);          /* begränsas till parameterns intervall */
float one_get(int param);
void one_load_preset(int index);               /* som Max-pluggen: ej nämnda parametrar får standardvärdet */

/* mätare för skärmen */
float one_meter_env(void);                     /* högsta volymhölje 0..1 */
uint32_t one_meter_voices(void);               /* bit i = röst i låter */
float one_meter_peak(void);
float one_meter_lfo(void);

#endif
