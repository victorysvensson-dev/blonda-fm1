/* SPDX-License-Identifier: GPL-3.0-only */
/* Ensemble, eko och utgång för motorerna i BLONDA ONE, se bfx_fm1.h. Koden är densamma som låg i varje motor
 * (rad för rad efter FX_UT i blonda-motor), nu på ett ställe. */
#include "bfx_fm1.h"
#include "motor_matte.h"

#define SR ((float)BFX_SR)

float bfx_lfo[BFX_MAXN], bfx_mixL[2][BFX_MAXN], bfx_mixR[2][BFX_MAXN];

static struct {                                 /* räknas när en parameter ändras */
    float cdry, cwet, cd;                       /* ensemble */
    float sdtT, fbk, dmix, gv;                  /* eko och utgång */
} F;
static float sdtc, dgc, cbase, chinc[4];        /* fasta konstanter (beror bara på BFX_SR) */

static float cp[4], sdt, sdtk, dlpL, dlpR, dcxL, dcyL, dcxR, dcyR, pk;
static int32_t ci, dw;
static float chb[2][BFX_CHN];
static int16_t dlb[2][BFX_DLN];

/* till Q15 mot noll (inte avrundat): annars kan ekot fastna på ±1 steg i stället för att dö ut */
static inline int16_t q15(float x)
{
    x *= 32767.f;
    if (x > 32767.f) x = 32767.f;
    if (x < -32767.f) x = -32767.f;
    return (int16_t)x;
}

void bfx_reset(void)
{
    int i;
    cp[0] = 0.f; cp[1] = 0.25f; cp[2] = 0.5f; cp[3] = 0.75f;
    sdt = 0.3f; sdtk = 0.f;
    dlpL = dlpR = dcxL = dcyL = dcxR = dcyR = pk = 0.f;
    ci = 0; dw = 0;
    for (i = 0; i < BFX_CHN; i++) chb[0][i] = chb[1][i] = 0.f;
    for (i = 0; i < BFX_DLN; i++) dlb[0][i] = dlb[1][i] = 0;
}

void bfx_init(void)
{
    static const float chr[4] = {0.31f, 0.47f, 0.39f, 0.53f};
    int i;
    sdtc = 1.f - exp_f(-1.f / (SR * 0.12f));
    dgc = 1.f - exp_f(-6.2831853f * 6500.f / SR);
    cbase = 7.f * SR / 1000.f;
    for (i = 0; i < 4; i++) chinc[i] = chr[i] / SR;
    bfx_reset();
}

void bfx_set(float chorus, float dtime, float dfb, float dmix, float vol)
{
    F.cdry = 1.f - chorus * 0.35f;
    F.cwet = chorus * 0.55f;
    F.cd = (0.7f + chorus * 2.2f) * SR / 1000.f;
    F.sdtT = 0.001f + dtime * dtime * 0.999f;
    F.fbk = dfb * 1.02f;
    F.dmix = dmix;
    F.gv = vol * vol * 1.25f;
}

float bfx_peak(void) { return pk; }

void bfx_block(float *L, float *R, const float *xL, const float *xR, uint32_t n)
{
    float cdry = F.cdry, cwet = F.cwet;
    uint32_t s, i;

    /* stereoensemble: fyra fördröjningar, två per kanal; läspositionen interpoleras över blocket */
    {
        float off0[4], doff[4];
        static const float dep[4] = {1.f, 0.8f, 1.f, 0.8f};
        for (i = 0; i < 4; i++) {
            float a = cp[i] + chinc[i], b;
            cp[i] = a - ffloor(a);
            b = cp[i] + chinc[i] * (float)(n - 1);
            off0[i] = cbase + sin2pi(cp[i]) * F.cd * dep[i];
            doff[i] = (cbase + sin2pi(b) * F.cd * dep[i] - off0[i]) / (float)(n > 1 ? n - 1 : 1);
            cp[i] = cp[i] + chinc[i] * (float)(n - 1);
            cp[i] -= ffloor(cp[i]);
        }
        for (s = 0; s < n; s++) {
            float tp[4], cL, cR, wL, wR, oL, oR, dd, df, rf;
            int32_t k, di, ri, rj;
            static const uint8_t ch[4] = {0, 1, 1, 0};
            ci = (ci + 1) & (BFX_CHN - 1);
            chb[0][ci] = xL[s];
            chb[1][ci] = xR[s];
            for (k = 0; k < 4; k++) {
                float r = (float)ci - (off0[k] + doff[k] * (float)s);
                int32_t i0, i1;
                float f;
                if (r < 0.f) r += (float)BFX_CHN;
                i0 = ifloor(r); f = r - (float)i0;
                i0 &= BFX_CHN - 1; i1 = (i0 + 1) & (BFX_CHN - 1);
                tp[k] = chb[ch[k]][i0] + (chb[ch[k]][i1] - chb[ch[k]][i0]) * f;
            }
            cL = xL[s] * cdry + (tp[0] + tp[1] * 0.6f) * cwet;
            cR = xR[s] * cdry + (tp[2] + tp[3] * 0.6f) * cwet;

            /* digitalt eko upp till 0,82 s (originalet 1 s: bufferten BFX_DLN sätter gränsen) */
            {   /* sdt glider mot målet; Kahan-summa så att float inte fastnar strax före (som double inte gör) */
                float y = ((F.sdtT - sdt) + sdtk) * sdtc - sdtk, tt = sdt + y;
                sdtk = (tt - sdt) - y;
                sdt = tt;
            }
            if (++dw >= BFX_DLN) dw = 0;
            dd = fclamp((sdt - sdtk) * SR, 2.f, (float)(BFX_DLN - 10));
            di = (int32_t)dd; df = dd - (float)di;
            ri = dw - di; if (df > 0.f) { ri -= 1; rf = 1.f - df; } else rf = 0.f;
            if (ri < 0) ri += BFX_DLN;
            rj = ri + 1; if (rj >= BFX_DLN) rj = 0;
            wL = ((float)dlb[0][ri] + (float)(dlb[0][rj] - dlb[0][ri]) * rf) * (1.f / 32767.f);
            wR = ((float)dlb[1][ri] + (float)(dlb[1][rj] - dlb[1][ri]) * rf) * (1.f / 32767.f);
            dlpL += (wL - dlpL) * dgc;
            dlpR += (wR - dlpR) * dgc;
            dlb[0][dw] = q15(tanh_f(cL + dlpL * F.fbk));
            dlb[1][dw] = q15(tanh_f(cR + dlpR * F.fbk));
            oL = tanh_f((cL + dlpL * F.dmix) * F.gv);
            oR = tanh_f((cR + dlpR * F.dmix) * F.gv);
            /* likspänningsfilter */
            dcyL = oL - dcxL + 0.9995f * dcyL; dcxL = oL;
            dcyR = oR - dcxR + 0.9995f * dcyR; dcxR = oR;
            L[s] = dcyL;
            R[s] = dcyR;
            pk = fmaxf_(fmaxf_(fabsf_(dcyL), fabsf_(dcyR)), pk * 0.9997f);
        }
    }
}
