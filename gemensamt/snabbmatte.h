/* SPDX-License-Identifier: GPL-3.0-only */
/* Snabb matte för FM-1:ans chip: bara float, ingen libm. Samma approximationer som i moduler/bris/src/bris_fm1.c
 * (testade där mot blonda-motor). Allt är static inline så att varje modul får sin egen kopia. */
#ifndef BLONDA_SNABBMATTE_H
#define BLONDA_SNABBMATTE_H
#include <stdint.h>

typedef union { float f; uint32_t u; } sm_bits_t;

static inline int32_t sm_ifloor(float x) { int32_t i = (int32_t)x; return (x < (float)i) ? i - 1 : i; }
static inline float sm_floor(float x) { return (float)sm_ifloor(x); }
static inline float sm_clamp(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
static inline float sm_max(float a, float b) { return a > b ? a : b; }
static inline float sm_abs(float x) { return x < 0.f ? -x : x; }
static inline float sm_wrap1(float x) { return x - sm_floor(x); }

/* 2^x: närmaste heltal + Taylor av grad 6 på [-0.5, 0.5] (relativt fel ~1e-7) */
static inline float sm_exp2(float x)
{
    int32_t i;
    float f, p;
    sm_bits_t r;
    if (x < -126.f) return 0.f;
    if (x > 126.f) x = 126.f;
    i = sm_ifloor(x + 0.5f);
    f = (x - (float)i) * 0.69314718f;
    p = 1.f + f * (1.f + f * (0.5f + f * (0.16666667f + f * (0.041666668f + f * (0.008333334f + f * 0.0013888889f)))));
    r.u = (uint32_t)(i + 127) << 23;
    return r.f * p;
}
static inline float sm_exp(float x) { return sm_exp2(x * 1.44269504f); }

/* sin(x) för x i [-pi/2, pi/2] (fel ~6e-8) */
static inline float sm_sin_half(float x)
{
    float x2 = x * x;
    return x * (1.f + x2 * (-0.16666667f + x2 * (0.008333334f + x2 * (-1.984127e-4f + x2 * (2.7557319e-6f + x2 * -2.5052108e-8f)))));
}
/* sin(2 pi p) för vilket p som helst */
static inline float sm_sin2pi(float p)
{
    float f = p - sm_floor(p), x;
    if (f >= 0.5f) f -= 1.f;
    x = f * 6.2831853f;
    if (x > 1.5707963f) x = 3.1415927f - x;
    else if (x < -1.5707963f) x = -3.1415927f - x;
    return sm_sin_half(x);
}
/* tanh: Padé [7/6], ±1 från |x| = 4.97 */
static inline float sm_tanh(float x)
{
    float x2;
    if (x > 4.97f) return 1.f;
    if (x < -4.97f) return -1.f;
    x2 = x * x;
    return x * (135135.f + x2 * (17325.f + x2 * (378.f + x2))) / (135135.f + x2 * (62370.f + x2 * (3150.f + x2 * 28.f)));
}

/* deterministiskt brus som blonda-motors gen::Noise (xorshift32), -1..1 */
typedef struct { uint32_t s; } sm_noise_t;
#define SM_NOISE_SEED 2463534242u
static inline float sm_noise(sm_noise_t *n)
{
    n->s ^= n->s << 13;
    n->s ^= n->s >> 17;
    n->s ^= n->s << 5;
    return (float)n->s * 4.656613e-10f - 1.f;
}

#endif
