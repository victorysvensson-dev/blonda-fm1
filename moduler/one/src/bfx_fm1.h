/* SPDX-License-Identifier: GPL-3.0-only */
/* Det som kommer efter rösterna i både BRIS och trioscillatormotorn: stereoensemble, eko, volym och
 * likspänningsfilter (FX_UT i blonda-motor/generator/bris.py, som generator/one.py också använder). En enda
 * uppsättning buffertar: bara en motor låter åt gången, och ekot klingar vidare när man byter ljud mellan dem.
 * Här ligger också arbetsminnet för ett anrop (LFO och rösternas mixar), som motorerna delar av samma skäl. */
#ifndef BFX_FM1_H
#define BFX_FM1_H
#include <stdint.h>

#ifndef BFX_SR
#define BFX_SR 44100                            /* FM-1:ans samplingsfrekvens */
#endif
#define BFX_K 16                                /* sampel per styrblock (som motorerna) */
#define BFX_MAXN 256                            /* längsta anropet */
#define BFX_CHN 512                             /* ensemblens buffert: längsta fördröjningen är 7 + 2,9 ms = 437 sampel
                                                 * (originalets 4096 ger samma ljud, men minnet behövs till reverbet) */
#ifndef BFX_DLN
#define BFX_DLN 36288                           /* ekot: 0,82 s (int16, Q15). Originalet går till 1 s (dtime över 0,91);
                                                 * inget fabriksljud går över 0,64 s. Resten av modulens minne (272 KiB)
                                                 * är reverbet BLONDA RUM, som modulen lägger efter motorerna. */
#endif

void bfx_init(void);                            /* fasta konstanter; nollställer */
void bfx_reset(void);                           /* tystar ensemble och eko */
/* från motorns parametrar (samma i båda): chorus, dtime, dfb, dmix, vol */
void bfx_set(float chorus, float dtime, float dfb, float dmix, float vol);
/* n (högst BFX_K) sampel: rösternas mix xL/xR genom ensemble, eko och utgång till L/R. L/R får vara samma
 * buffertar som xL/xR (varje sampel läses innan det skrivs); modulen använder det för att spara minne. */
void bfx_block(float *L, float *R, const float *xL, const float *xR, uint32_t n);
float bfx_peak(void);

/* arbetsminne för ett anrop: LFO:n och rösternas två mixar (en per kärna) */
extern float bfx_lfo[BFX_MAXN], bfx_mixL[2][BFX_MAXN], bfx_mixR[2][BFX_MAXN];

#endif
