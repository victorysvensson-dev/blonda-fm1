/* SPDX-License-Identifier: GPL-3.0-only */
/* Snabb matte för motorerna i BLONDA ONE (BRIS och trioscillatormotorn) och deras gemensamma effekter: float,
 * ingen libm. Samma funktioner i alla tre filerna, så att de räknar lika. */
#ifndef MOTOR_MATTE_H
#define MOTOR_MATTE_H
#include <stdint.h>

/* ------------------------------------------------------------------ snabb matte (ingen libm) */
typedef union { float f; uint32_t u; } fbits_t;

static inline int32_t ifloor(float x) { int32_t i = (int32_t)x; return (x < (float)i) ? i - 1 : i; }
static inline float ffloor(float x) { return (float)ifloor(x); }
static inline float fclamp(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
static inline float fmaxf_(float a, float b) { return a > b ? a : b; }
static inline float fabsf_(float x) { return x < 0.f ? -x : x; }

/* 2^x: avrundning till närmaste heltal + Taylor av grad 6 på [-0.5, 0.5] (relativt fel ~1e-7) */
static inline float exp2_f(float x)
{
    int32_t i;
    float f, p;
    fbits_t r;
    if (x < -126.f) return 0.f;
    if (x > 126.f) x = 126.f;
    i = ifloor(x + 0.5f);
    f = (x - (float)i) * 0.69314718f;          /* ln 2 */
    p = 1.f + f * (1.f + f * (0.5f + f * (0.16666667f + f * (0.041666668f + f * (0.008333334f + f * 0.0013888889f)))));
    r.u = (uint32_t)(i + 127) << 23;
    return r.f * p;
}
static inline float exp_f(float x) { return exp2_f(x * 1.44269504f); }

/* sin(x) för x i [-pi/2, pi/2]: Taylor av grad 11 (fel ~6e-8) */
static inline float sin_half(float x)
{
    float x2 = x * x;
    return x * (1.f + x2 * (-0.16666667f + x2 * (0.008333334f + x2 * (-1.984127e-4f + x2 * (2.7557319e-6f + x2 * -2.5052108e-8f)))));
}
/* sin(2 pi p) för vilket p som helst */
static inline float sin2pi(float p)
{
    float f = p - ffloor(p), x;
    if (f >= 0.5f) f -= 1.f;                   /* [-0.5, 0.5) */
    x = f * 6.2831853f;                        /* [-pi, pi) */
    if (x > 1.5707963f) x = 3.1415927f - x;
    else if (x < -1.5707963f) x = -3.1415927f - x;
    return sin_half(x);
}
/* tan(w) för w i [0, 0.42 pi]: sin / cos med Taylor */
static inline float tan_f(float w)
{
    float w2 = w * w;
    float s = sin_half(w);
    float c = 1.f + w2 * (-0.5f + w2 * (0.041666668f + w2 * (-0.0013888889f + w2 * (2.4801587e-5f + w2 * (-2.7557319e-7f + w2 * 2.0876757e-9f)))));
    return s / c;
}
/* tanh: Padé [7/6], ±1 från |x| = 4.97 där kvoten når 1 */
static inline float tanh_f(float x)
{
    float x2;
    if (x > 4.97f) return 1.f;
    if (x < -4.97f) return -1.f;
    x2 = x * x;
    return x * (135135.f + x2 * (17325.f + x2 * (378.f + x2))) / (135135.f + x2 * (62370.f + x2 * (3150.f + x2 * 28.f)));
}

#endif
