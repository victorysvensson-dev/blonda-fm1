/* SPDX-License-Identifier: GPL-3.0-only */
/* BLONDA SLITEN för FM-1 – rad för rad efter blonda-motor/motorer/sliten/sliten_gen.h (samma GenExpr som
 * Max for Live-pluggen), med samma ordning på brusanropen så att jämförelsen sampel för sampel håller.
 * Skillnader: float, egna approximationer, koefficienter som bara beror på parametrarna räknas när de ändras och
 * filterkoefficienterna en gång per block. Testas mot blonda-motor i moduler/bris/test/sliten_test.cpp. */
#include "sliten_fm1.h"
#include "snabbmatte.h"

#define SR ((float)SLITEN_SR)
#define INV_SR (1.f / (float)SLITEN_SR)
#define TWOPI 6.2831853f

/* mediernas tabeller, i GenExpr-ordning (0 kassett ... 8 telefon) */
static const float T_WOW[9]  = {1.0f, 1.3f, 0.0f, 0.8f, 0.5f, 1.1f, 1.5f, 1.3f, 0.0f};
static const float T_FLUT[9] = {1.0f, 1.5f, 0.0f, 0.35f, 0.6f, 0.4f, 2.2f, 1.1f, 0.0f};
static const float T_SAT[9]  = {1.0f, 0.8f, 0.15f, 0.45f, 1.25f, 0.7f, 1.3f, 1.1f, 0.9f};
static const float T_MO[9]   = {0.0f, 0.75f, 0.0f, 0.15f, 0.0f, 1.0f, 1.0f, 0.3f, 1.0f};
static const float T_WHZ[9]  = {0.7f, 0.9f, 0.5f, 0.55f, 0.35f, 1.3f, 1.1f, 0.8f, 0.5f};
static const float T_LP[9]   = {9000.f, 6500.f, 15000.f, 12000.f, 16000.f, 4800.f, 4200.f, 7000.f, 3400.f};
static const float T_HP[9]   = {45.f, 90.f, 20.f, 30.f, 28.f, 160.f, 260.f, 75.f, 300.f};
static const float T_MK[9]   = {1.1f, 1.3f, 1.0f, 1.1f, 1.1f, 1.6f, 2.0f, 1.15f, 2.4f};
static const float T_HISS[9] = {1.0f, 1.2f, 0.0f, 0.35f, 0.65f, 0.6f, 1.7f, 0.9f, 0.35f};

static const float DEF[SL_NPARAM] = {0.f, 0.5f, 0.35f, 0.3f, 0.4f, 0.3f, 0.2f, 0.5f, 1.f, 1.f, 0.75f};
static const float LO[SL_NPARAM] = {0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f};
static const float HI[SL_NPARAM] = {8.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f};

static inline float onepole(float hz) { return 1.f - sm_exp(-TWOPI * hz / SR); }

void sliten_reset(sliten_t *s)
{
    int i;
    s->wi = 0; s->qc = 0;
    s->wph = s->wdr = s->wdrk = s->wdt = s->fph = s->fn = s->dEnv = s->dLeft = s->dDepth = s->frz = s->rep = 0.f;
    s->pop = s->popv = s->lvl = s->hum = s->crk = s->jit = 0.f;
    s->sWet = 1.f;
    s->qhL = s->qhR = s->gsm = s->hpLs = s->hpRs = s->lpLs = s->lpRs = s->lp2Ls = s->lp2Rs = 0.f;
    s->bmLs = s->bmRs = s->hsAs = s->hsBs = s->rbAs = s->rbBs = 0.f;
    s->rng = SM_NOISE_SEED;
    for (i = 0; i < SLITEN_N; i++) s->buf[0][i] = s->buf[1][i] = 0.f;
}

void sliten_init(sliten_t *s)
{
    int i;
    for (i = 0; i < SL_NPARAM; i++) s->p[i] = DEF[i];
    sliten_reset(s);
}

void sliten_set(sliten_t *s, int i, float v)
{
    if (i < 0 || i >= SL_NPARAM) return;
    s->p[i] = sm_clamp(v, LO[i], HI[i]);
}

static inline float rnd(sliten_t *s) { sm_noise_t n = {s->rng}; float r = sm_noise(&n); s->rng = n.s; return r; }

static void block(sliten_t *s, float *L, float *R, uint32_t n)
{
    const float *p = s->p;
    int32_t md = sm_ifloor(p[SL_media] + 0.5f);
    int dig, vin;
    float ag, eWow, eFlut, eSat, eHiss, eDrop, mo, pDrop, loopL, dr, idr, t08, satk, qs, iqs, lpf, hpk, lpk, lp2k, hsAk, bump, mk, popRate, outk;
    int32_t qn;
    uint32_t i;
    static const float BMK_HZ = 90.f;
    float bmk = onepole(BMK_HZ), hsBk = onepole(800.f), rbAk = onepole(70.f), rbBk = onepole(140.f);

    md = md < 0 ? 0 : md > 8 ? 8 : md;
    dig = md == 2 || md == 8;
    vin = md == 3 || md == 5;
    ag = sm_clamp(p[SL_age], 0.f, 1.f) * 2.f;
    eWow = sm_clamp(p[SL_wow] * ag, 0.f, 2.f) * T_WOW[md];
    eFlut = sm_clamp(p[SL_flut] * ag, 0.f, 2.f) * T_FLUT[md];
    eSat = sm_clamp(p[SL_sat] * ag, 0.f, 2.f) * T_SAT[md];
    eHiss = sm_clamp(p[SL_brus] * ag, 0.f, 2.f);
    eDrop = sm_clamp(p[SL_avbr] * ag, 0.f, 2.f);
    mo = sm_clamp(T_MO[md] + (1.f - p[SL_bredd]) * 0.5f, 0.f, 1.f);
    pDrop = eDrop * eDrop * 2.2f / SR;
    loopL = SR * (md == 8 ? 0.02f : 0.045f);
    dr = 1.f + eSat * 3.5f;
    idr = 1.f / dr;
    t08 = sm_tanh(0.08f * eSat);
    satk = (1.f + eSat * 0.35f) / dr;
    qn = md == 8 ? sm_ifloor(SR / 8000.f + 0.5f) : (md == 2 ? sm_ifloor(1.f + ag * ag * 1.2f) : 1);
    if (qn < 1) qn = 1;
    qs = sm_exp2((md == 8 ? 11.f - ag * 2.f : 16.f - (md == 2 ? ag * ag * 2.5f : 0.f)) - 1.f);
    iqs = 1.f / qs;
    popRate = vin ? (eHiss * 6.f + eDrop * 14.f) * (md == 5 ? 2.5f : 1.f) * INV_SR : 0.f;
    outk = p[SL_outv] * 1.33f * 0.95f;
    /* filtren: lågpasset följer avbrottens hölje, en gång per block */
    lpf = sm_clamp(T_LP[md] * sm_exp2((p[SL_ton] - 0.5f) * 2.f * 2.f) * sm_exp2(ag * 0.5f * -0.86249648f)   /* 4^x, 0.55^x */
                   * (1.f - s->dEnv * 0.75f * ((dig || vin) ? 0.f : 1.f)), 300.f, SR * 0.45f);
    hpk = onepole(T_HP[md] * (1.f + ag * 0.6f));
    lpk = onepole(lpf);
    lp2k = onepole(lpf * 1.3f);
    hsAk = onepole(lpf * 0.9f);
    bump = md == 7 ? 0.9f : ((md == 0 || md == 4 || md == 6) ? 0.35f * (0.4f + eSat * 0.3f) : 0.f);
    mk = T_MK[md];

    for (i = 0; i < n; i++) {
        float inL = L[i], inR = R[i], inM, xL, xR, wHz, wv, fv, dTarget, dly, rIdx, yL, yR, sL, sR;
        float hpLo, hpRo, zL, zR, wn, hsBo, hiss, humv = 0.f, vc = 0.f, road, gsmOn = 0.f, burst, nz, dg, oL, oR, winc;
        int32_t ri, a;
        float f, ra, rb, rc, rdd;

        s->sWet += (p[SL_wet] - s->sWet) * 0.001f;
        inM = (inL + inR) * 0.5f;
        xL = inL + (inM - inL) * mo;
        xR = inR + (inM - inR) * mo;

        wHz = T_WHZ[md] * (0.6f + s->wdr * 0.8f);
        winc = wHz * INV_SR;
        s->wph = sm_wrap1(s->wph + winc);
        if (s->wph < winc) s->wdt = sm_abs(rnd(s));
        {   /* wdr glider mycket långsamt: Kahan-summa så att float inte fastnar före målet */
            float y = ((s->wdt - s->wdr) + s->wdrk) * 0.00002f - s->wdrk, tt = s->wdr + y;
            s->wdrk = (tt - s->wdr) - y;
            s->wdr = tt;
        }
        wv = sm_sin2pi(s->wph) * 0.75f + (s->wdr - 0.5f) * 0.5f;
        s->fph = sm_wrap1(s->fph + (7.f + s->wdr * 4.f) * INV_SR);
        s->fn += (rnd(s) - s->fn) * 0.002f;
        fv = sm_sin2pi(s->fph) * 0.6f + s->fn * 6.f;

        if (s->dLeft <= 0.f && rnd(s) * 0.5f + 0.5f < pDrop) {
            s->dLeft = (0.025f + sm_abs(rnd(s)) * 0.22f) * SR * (dig ? 0.6f : 1.f);
            s->dDepth = 0.45f + sm_abs(rnd(s)) * 0.55f;
            s->frz = (float)s->wi;
            s->rep = 0.f;
        }
        dTarget = s->dLeft > 0.f ? s->dDepth : 0.f;
        s->dEnv += (dTarget - s->dEnv) * (dTarget > s->dEnv ? 0.004f : 0.0015f);
        s->dLeft = sm_max(0.f, s->dLeft - 1.f);
        s->jit += ((md == 1 && s->dLeft > 0.f ? rnd(s) * 2.5f : 0.f) - s->jit) * 0.003f;

        s->buf[0][s->wi] = xL;
        s->buf[1][s->wi] = xR;
        dly = (9.f + wv * eWow * 3.2f + fv * eFlut * 0.22f + s->jit * s->dEnv) * (SR * 0.001f);
        rIdx = (float)s->wi - sm_max(dly, 4.f);
        s->rep += 1.f;
        if (dig && s->dLeft > 0.f) rIdx = s->frz - loopL + (s->rep - sm_floor(s->rep / loopL) * loopL);
        ri = sm_ifloor(rIdx);
        f = rIdx - (float)ri;
        a = ri - 1;
        /* kubisk interpolation (Catmull-Rom), som originalet */
        ra = s->buf[0][a & (SLITEN_N - 1)]; rb = s->buf[0][(a + 1) & (SLITEN_N - 1)];
        rc = s->buf[0][(a + 2) & (SLITEN_N - 1)]; rdd = s->buf[0][(a + 3) & (SLITEN_N - 1)];
        yL = (((0.5f * (rdd - ra) + 1.5f * (rb - rc)) * f + (ra - 2.5f * rb + 2.f * rc - 0.5f * rdd)) * f + 0.5f * (rc - ra)) * f + rb;
        ra = s->buf[1][a & (SLITEN_N - 1)]; rb = s->buf[1][(a + 1) & (SLITEN_N - 1)];
        rc = s->buf[1][(a + 2) & (SLITEN_N - 1)]; rdd = s->buf[1][(a + 3) & (SLITEN_N - 1)];
        yR = (((0.5f * (rdd - ra) + 1.5f * (rb - rc)) * f + (ra - 2.5f * rb + 2.f * rc - 0.5f * rdd)) * f + 0.5f * (rc - ra)) * f + rb;
        s->wi = (s->wi + 1) & (SLITEN_N - 1);

        if (dig) {
            sL = sm_clamp(yL * dr, -1.f, 1.f) * idr;
            sR = sm_clamp(yR * dr, -1.f, 1.f) * idr;
        } else {
            sL = (sm_tanh(yL * dr + 0.08f * eSat) - t08) * satk;
            sR = (sm_tanh(yR * dr + 0.08f * eSat) - t08) * satk;
        }
        if (++s->qc >= qn) { s->qc = 0; s->qhL = sL; s->qhR = sR; }
        if (dig) {
            sL = sm_floor(s->qhL * qs + 0.5f) * iqs;
            sR = sm_floor(s->qhR * qs + 0.5f) * iqs;
        }

        s->hpLs += (sL - s->hpLs) * hpk; hpLo = sL - s->hpLs;
        s->hpRs += (sR - s->hpRs) * hpk; hpRo = sR - s->hpRs;
        s->lpLs += (hpLo - s->lpLs) * lpk;
        s->lpRs += (hpRo - s->lpRs) * lpk;
        s->lp2Ls += (s->lpLs - s->lp2Ls) * lp2k;
        s->lp2Rs += (s->lpRs - s->lp2Rs) * lp2k;
        s->bmLs += (s->lp2Ls - s->bmLs) * bmk;
        s->bmRs += (s->lp2Rs - s->bmRs) * bmk;
        zL = (s->lp2Ls + s->bmLs * bump) * mk;
        zR = (s->lp2Rs + s->bmRs * bump) * mk;

        wn = rnd(s);
        s->hsAs += (wn - s->hsAs) * hsAk;
        s->hsBs += (s->hsAs - s->hsBs) * hsBk;
        hsBo = s->hsAs - s->hsBs;
        hiss = hsBo * eHiss * T_HISS[md] * 0.011f;
        s->hum = sm_wrap1(s->hum + 60.f * INV_SR);
        if (md == 1)
            humv = (sm_sin2pi(s->hum) * 0.6f + sm_sin2pi(s->hum * 3.f) * 0.3f + sm_sin2pi(s->hum * 5.f) * 0.15f) * eHiss * 0.0012f;
        if (rnd(s) * 0.5f + 0.5f < popRate) { s->popv = rnd(s) * (0.07f + eDrop * 0.12f); s->pop = 1.f; }
        s->crk *= 0.82f;
        if (s->pop > 0.5f) s->crk = s->popv;
        s->pop = 0.f;
        if (vin) vc = s->crk + (rnd(s) * 0.5f + 0.5f < eHiss * (md == 5 ? 0.012f : 0.002f) ? rnd(s) * 0.02f : 0.f);
        s->rbAs += (wn - s->rbAs) * rbAk;
        s->rbBs += (s->rbAs - s->rbBs) * rbBk;
        road = md == 7 ? s->rbBs * eHiss * 0.06f : 0.f;
        s->gsm = sm_wrap1(s->gsm + 217.f * INV_SR);
        if (md == 8) gsmOn = (sm_sin2pi(s->wph * 6.f) > 0.3f ? 1.f : 0.f) * (s->gsm < 0.12f ? 1.f : -0.15f) * eHiss * 0.006f * (0.3f + s->dEnv * 3.f);
        burst = md == 1 ? wn * s->dEnv * 0.025f : 0.f;
        nz = hiss + humv + vc + burst + road + gsmOn;

        dg = (dig || vin) ? 1.f : 1.f - s->dEnv;
        oL = zL * dg + nz;
        oR = zR * dg + nz * (md == 1 ? 1.f : 0.85f) + ((md == 0 || md == 4) ? hiss * 0.4f : 0.f);
        oL = inL + (oL - inL) * s->sWet;
        oR = inR + (oR - inR) * s->sWet;
        L[i] = sm_tanh(oL * outk) * (1.f / 0.95f);
        R[i] = sm_tanh(oR * outk) * (1.f / 0.95f);
        s->lvl = sm_max(sm_abs(L[i]) + sm_abs(R[i]), s->lvl * 0.9995f);
    }
}

void sliten_process(sliten_t *s, float *L, float *R, uint32_t n)
{
    while (n) {
        uint32_t k = n < SLITEN_K ? n : SLITEN_K;
        block(s, L, R, k);
        L += k; R += k; n -= k;
    }
}
