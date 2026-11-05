/* SPDX-License-Identifier: GPL-3.0-only */
/* BLONDA SLITEN för FM-1: slitet band och andra media (kassett, VHS, digital, vinyl, rulle, stenkaka, diktafon,
 * bilradio, telefon). Samma GenExpr som Max for Live-pluggen (blonda-motor/generator/effekter/sliten.genexpr),
 * skriven för chipet: float, ingen libm, filterkoefficienter per block om SLITEN_K sampel.
 *
 * Skillnad mot originalet: bandbufferten är SLITEN_N sampel (originalet 65 536). Det räcker för wow, flutter och
 * fördröjningen (~25 ms); avbrottens upprepning ("avbr" i digitala media) behöver mer och ska då få större N. */
#ifndef SLITEN_FM1_H
#define SLITEN_FM1_H
#include <stdint.h>

#ifndef SLITEN_SR
#define SLITEN_SR 44100
#endif
#ifndef SLITEN_K
#define SLITEN_K 16
#endif
#ifndef SLITEN_N
#define SLITEN_N 2048                            /* tvåpotens */
#endif

enum { SL_media, SL_age, SL_wow, SL_flut, SL_sat, SL_brus, SL_avbr, SL_ton, SL_bredd, SL_wet, SL_outv, SL_NPARAM };

typedef struct {
    float p[SL_NPARAM];
    /* tillstånd, samma namn som i GenExpr */
    int32_t wi, qc;
    float wph, wdr, wdrk, wdt, fph, fn, dEnv, dLeft, dDepth, frz, rep, pop, popv, lvl, hum, sWet, crk, jit;
    float qhL, qhR, gsm, hpLs, hpRs, lpLs, lpRs, lp2Ls, lp2Rs, bmLs, bmRs, hsAs, hsBs, rbAs, rbBs;
    uint32_t rng;
    float buf[2][SLITEN_N];
} sliten_t;

void sliten_init(sliten_t *s);                  /* standardvärden (som pluggen) och nollställt */
void sliten_reset(sliten_t *s);                 /* nollställ tillståndet, behåll parametrarna */
void sliten_set(sliten_t *s, int param, float v);   /* i parameterns egna enheter (media 0..8, resten 0..1) */
void sliten_process(sliten_t *s, float *L, float *R, uint32_t n);   /* på plats */

#endif
