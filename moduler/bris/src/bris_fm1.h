/* SPDX-License-Identifier: GPL-3.0-only */
/* BLONDA BRIS för M-VAVE FM-1 – sexstämmig analog poly efter GS Music Bree6.
 *
 * Samma signalväg, parametrar och fabriksljud som BRIS i blonda-motor (generator/bris.py), men skriven för
 * FM-1:ans chip (JieLi AC791N): bara float (inga double), ingen libm, snabba approximationer och styrsignaler
 * (tonhöjd, filterkoefficienter) räknade en gång per block om BRIS_K sampel. Ljudet är nära originalet,
 * inte bit-identiskt; test/likhet.py mäter skillnaden mot blonda-motor för varje fabriksljud.
 *
 * Trådar: bris_render() körs i ljudavbrottet. bris_note(), bris_set() m.fl. körs i huvudloopen och skriver
 * bara enskilda 32-bitarsvärden som render läser en gång per block. */
#ifndef BRIS_FM1_H
#define BRIS_FM1_H
#include <stdint.h>
#include "bris_presets.h"

#ifndef BRIS_SR
#define BRIS_SR 44100                           /* FM-1:ans samplingsfrekvens */
#endif
#ifndef BRIS_K
#define BRIS_K 16                               /* sampel per styrblock */
#endif
#define BRIS_NV 6
#define BRIS_CHN 4096                           /* ensemblens buffert, som originalet */
#define BRIS_DLN (BRIS_SR + 64)                 /* ekot: 1 s (int16, Q15: originalets 52000 float får inte plats) */

void bris_init(void);                           /* nollställer allt och laddar init-ljudet */
void bris_reset(void);                          /* tystar och nollställer ljudet, behåller parametrarna */
void bris_render(float *L, float *R, uint32_t n);   /* skriver n sampel (n delbart med BRIS_K går snabbast) */

void bris_note_on(int note, int vel);           /* vel 1..127 */
void bris_note_off(int note);
void bris_all_notes_off(void);
void bris_sustain(int down);
void bris_bend(float v);                        /* -1..1 */
void bris_modwheel(float v);                    /* 0..1 */
void bris_aftertouch(float v);                  /* 0..1 */

void bris_set(int param, float value);          /* begränsas till parameterns intervall */
float bris_get(int param);
void bris_load_preset(int index);               /* som Max-pluggen: ej nämnda parametrar får standardvärdet */

/* mätare för skärmen */
float bris_meter_env(void);                     /* högsta volymhölje 0..1 */
uint32_t bris_meter_voices(void);               /* bit i = röst i låter */
float bris_meter_peak(void);
float bris_meter_lfo(void);

#endif
