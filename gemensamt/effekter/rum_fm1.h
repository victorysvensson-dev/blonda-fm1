/* SPDX-License-Identifier: GPL-3.0-only */
/* BLONDA RUM för FM-1: reverbet ur blonda-motor (samma GenExpr som Max for Live-pluggen,
 * blonda-motor/generator/effekter/rum.genexpr), skrivet för chipet: float, ingen libm, linjernas längd och
 * dämpning räknade en gång per block om RUM_K sampel och interpolerade emellan.
 *
 * Uppbyggnad som originalet: fördröjning före (predelay), lågskärning, fyra allpassfilter som sprider ljudet,
 * åtta fördröjningslinjer i ett återkopplat nät (Householder) med dämpning och långsam rörelse, höskärning,
 * bredd, ducking efter insignalen, mix och utnivå.
 *
 * Skillnad mot originalet: linjerna ligger i int16 (Q13, ±4,0: klarar allt utom fyrkant på full nivå med längsta
 * klang, som klipps), predelay i Q15, avrundade med brusformning (felet bärs till nästa sampel). Bufferterna är så långa som 44,1 kHz behöver (originalet 12 000 / 2 000 /
 * 40 000 sampel i double får inte plats i modulens minne): 97 KB i stället för 1,1 MB.
 *
 * rum_process() är originalet. rum_tail() + rum_mix() + rum_feed() är samma sak i tre steg, för moduler som räknar
 * klangen på den andra kärnan eller vill styra klangens nivå (PUMP). */
#ifndef RUM_FM1_H
#define RUM_FM1_H
#include <stdint.h>

#ifndef RUM_SR
#define RUM_SR 44100
#endif
#ifndef RUM_K
#define RUM_K 32
#endif

/* buffertlängder för 44,1 kHz: längsta linjen är 73,1 ms * 2,0 (size = 1) + rörelsen. Varje linje har ett sampel
 * till i slutet, en kopia av det första, så att interpolationen slipper vika runt. */
#define RUM_PDN (11026 + RUM_K + 2)              /* predelay: 0,25 s, och ett block till (rum_feed före rum_tail) */
#define RUM_LN0 2632
#define RUM_LN1 3284
#define RUM_LN2 3636
#define RUM_LN3 3865
#define RUM_LN4 4764
#define RUM_LN5 5416
#define RUM_LN6 5998
#define RUM_LN7 6456
#define RUM_LSUM (RUM_LN0 + RUM_LN1 + RUM_LN2 + RUM_LN3 + RUM_LN4 + RUM_LN5 + RUM_LN6 + RUM_LN7 + 8)
#define RUM_AN0 254
#define RUM_AN1 191
#define RUM_AN2 675
#define RUM_AN3 494
#define RUM_ASUM (RUM_AN0 + RUM_AN1 + RUM_AN2 + RUM_AN3)

/* samma ordning som rum.json */
enum { RU_size, RU_decay, RU_damp, RU_predel, RU_lowc, RU_modd, RU_width, RU_wet, RU_duck, RU_outv, RU_shape, RU_highc,
       RU_NPARAM };

typedef struct {
    float p[RU_NPARAM];
    /* tillstånd, samma namn som i GenExpr */
    float sS, lph, lcs, z[8], hrLs, hrRs, hr2Ls, hr2Rs, env, pk;
    float ln[8], g[8];                          /* linjernas längd nu (interpoleras över blocket) och avklingning */
    float el[8], ea[4], ep;                     /* avrundningsfelet som bärs till nästa sampel, per buffert */
    int32_t pi0, pr, api[4], wi[8], started, dirty, gk;
    /* koefficienter ur parametrarna (räknas när en parameter ändrats) */
    float c_dk, c_lck, c_hrk, c_sck, c_irt, c_dryG, c_wetG, c_ok;
    int32_t c_hc, c_pdd;
    /* arbetsminne för ett block */
    float zb[8][RUM_K], xb[RUM_K], hb[RUM_K];
    int16_t pd[RUM_PDN];
    int16_t ap[RUM_ASUM];
    int16_t dl[RUM_LSUM];
} rum_t;

void rum_init(rum_t *r);                        /* standardvärden (som pluggen) och nollställt */
void rum_reset(rum_t *r);                       /* nollställ tillståndet, behåll parametrarna */
void rum_set(rum_t *r, int param, float v);     /* 0..1 */
float rum_get(const rum_t *r, int param);

/* originalet: klang och mix på plats */
void rum_process(rum_t *r, float *L, float *R, uint32_t n);

/* Samma sak i tre steg, för en modul som räknar klangen på den andra kärnan medan den första gör något annat:
 *   rum_tail  klangen för nästa n sampel, ur det som redan matats in. min_delay = minsta predelay i sampel:
 *             1 om rum_feed för samma sampel redan är gjord (som i rum_process), annars n (klangen räknas då
 *             ur förra anropets ljud, och en predelay kortare än n blir n).
 *   rum_mix   ducking efter insignalen, mix, utnivå – på plats i L/R. Klangen i wetL/wetR får ändras före (PUMP).
 *   rum_feed  mata in ljudet (före rum_mix eller efter, men alltid i samma ordning).
 * rum_tail läser bara det rum_feed redan skrivit och rör inget som rum_feed eller rum_mix rör. */
void rum_tail(rum_t *r, float *wetL, float *wetR, uint32_t n, uint32_t min_delay);
void rum_mix(rum_t *r, float *L, float *R, const float *wetL, const float *wetR, uint32_t n);
void rum_feed(rum_t *r, const float *L, const float *R, uint32_t n);
/* Om ett rum_tail avbröts halvvägs: klangen fortsätter som om det blivit klart. ahead = så många sampel klang som
 * då skulle vara räknade utöver det som matats in med rum_feed (blockets längd, när klangen räknas före rum_feed). */
void rum_resync(rum_t *r, uint32_t ahead);

#endif
