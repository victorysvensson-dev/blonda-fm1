/* SPDX-License-Identifier: GPL-3.0-only */
/* BLONDA ONE för M-VAVE FM-1. Signalvägen följer blonda-motor/generator/one.py rad för rad; skillnaderna
 * är bara hur det räknas (samma sätt som BRIS-motorn, src/bris_fm1.c):
 *   - float i stället för double, egna approximationer i stället för libm (pow, exp, sin, tan, tanh)
 *   - tonhöjd, drift, filterkoefficient och tangeringsfaktorer en gång per block om ONE_K sampel
 *   - koefficienter som bara beror på parametrarna räknas när en parameter ändras, inte i varje sampel
 *   - varje oscillator räknas för sig över blocket med sin vågform; en oscillator på nivå 0 räknas inte alls
 *   - ensemble, eko och utgång delas med BRIS-motorn (bfx_fm1.c): ekobufferten i int16 (Q15) och 0,82 s lång
 * Efter rösterna (ensemble, eko, utgång) och röstfördelningen är ONE samma som BRIS, här som i blonda-motor.
 * Allt som påverkar ljudet hör egentligen hemma i generatorn: ändras ONE där, ändra här också och kör
 * test/likhet.py. */
#include "one_fm1.h"
#include "bfx_fm1.h"
#include "motor_matte.h"

#define SR ((float)ONE_SR)

/* ------------------------------------------------------------------ parametrar och koefficienter */
static float P[OP_N];                           /* parametrarnas värden (som Max-pluggen) */

/* konstanter ur generator/one.py */
#define ONE_COMP 0.3f                           /* stegens nivåkompensation vid resonans */
#define ONE_PULS 0.8f                           /* pulsens nivå mot såg och triangel */
#define ONE_BRUS 1.0f                           /* brusets nivå vid noi = 1 */
#define ONE_GRUNDBRUS 0.0001f                   /* brus som alltid finns, så att filtret kan börja självsvänga */
#define ONE_DRIFT 0.02f                         /* oscillator 2 och 3 driver så här många halvtoner */

static struct {                                 /* räknas i huvudloopen när en parameter ändras */
    float fAinc, fD, fR, aAinc, aD, aR;         /* höljen */
    float cgK, cgH;                             /* portamento per block och till mitten av blocket */
    float lrinc;                                /* LFO-fasens steg */
    int32_t lw;                                 /* LFO-form 0..4 */
    float vib0, vibw;                           /* vibrato = (vib0 + (mw + at) * vibw) * lfo */
    float pwmc, pwbase, bendr;
    int32_t w[3], md;                           /* vågform per oscillator: 0 triangel, 1 såg, 2 puls */
    float r[3];                                 /* stämning per oscillator i halvtoner (fotläge, halvtoner, fin) */
    float lv[3];                                /* nivå per oscillator gånger mixerns drivning */
    float nzc;                                  /* brusets nivå gånger mixerns drivning */
    float og;                                   /* utnivå efter filtret (håller nivån jämn när drv vrids) */
    float kk, comp, cutB, spread, ug, detc;
    float egc, velf, lfocut3, kbd12, mwcut4, vela, lfoamp5;
} C;

/* fasta konstanter (beror bara på ONE_SR), räknade i one_init */
static float lsmc, drinc[ONE_NV];

static const float DET[ONE_NV] = {-1.f, 1.f, -0.6f, 0.6f, -0.2f, 0.2f};
static const float PANL[ONE_NV] = {0.f, 0.85f, 0.f, 0.5f, 0.f, 0.17f};       /* max(0, pan) */
static const float PANR[ONE_NV] = {0.85f, 0.f, 0.5f, 0.f, 0.17f, 0.f};       /* max(0, -pan) */
static const float CUTS[ONE_NV] = {0.97f, 0.982f, 0.994f, 1.006f, 1.018f, 1.03f};   /* 1 + (i - 2.5) * 0.012 */

/* tc(x) = 1 - exp(-1 / (sr * 0.0012 * 10000^x)) */
static float tc(float x) { return 1.f - exp_f(-1.f / (SR * 0.0012f * exp2_f(x * 13.287712f))); }

static int32_t iround(float x, int32_t lo, int32_t hi)
{
    int32_t i = ifloor(x + 0.5f);
    return i < lo ? lo : i > hi ? hi : i;
}

static void coeffs(void)
{
    float fA = 0.001f * exp2_f(P[OP_fa] * 13.287712f);       /* 10000^fa */
    float aA = 0.001f * exp2_f(P[OP_aa] * 13.287712f);
    float gin = 0.35f * (1.f + P[OP_drv] * P[OP_drv] * 5.f);
    int32_t md = iround(P[OP_lage], 0, 3);
    C.fAinc = 1.f / (SR * fA);
    C.fD = tc(P[OP_fd]);
    C.fR = tc(P[OP_fr]);
    C.aAinc = 1.f / (SR * aA);
    C.aD = tc(P[OP_ad]);
    C.aR = tc(P[OP_ar]);
    /* portamento: 1 - (1 - cg)^K = 1 - exp(-K / (sr * 0.0015 * 1300^porta)) */
    if (P[OP_porta] > 0.001f) {
        float tau = SR * 0.0015f * exp2_f(P[OP_porta] * 10.344296f);
        C.cgK = 1.f - exp_f(-(float)ONE_K / tau);
        C.cgH = 1.f - exp_f(-((float)ONE_K * 0.5f + 0.5f) / tau);          /* mittsamplet */
    } else C.cgK = C.cgH = 1.f;
    C.lrinc = 0.1f * exp2_f(P[OP_lrate] * 9.9657843f) / SR;  /* 0.1 * 1000^lrate */
    C.lw = iround(P[OP_lwave], 0, 4);
    C.vib0 = P[OP_lfoosc] * P[OP_lfoosc] * 12.f;
    C.vibw = P[OP_mwosc] * 0.35f * 12.f;
    C.pwmc = P[OP_lfopwm] * 0.45f;
    C.pwbase = P[OP_pw] * 0.96f + 0.02f;
    C.bendr = P[OP_bendr];
    C.w[0] = iround(P[OP_o1w], 0, 2);
    C.w[1] = iround(P[OP_o2w], 0, 2);
    C.w[2] = iround(P[OP_o3w], 0, 2);
    C.r[0] = 12.f * (float)iround(P[OP_o1r], -2, 2);
    C.r[1] = 12.f * (float)iround(P[OP_o2r], -2, 2) + ffloor(P[OP_o2s] + 0.5f) + P[OP_o2f] * fabsf_(P[OP_o2f]) * 0.5f;
    C.r[2] = 12.f * (float)iround(P[OP_o3r], -2, 2) + ffloor(P[OP_o3s] + 0.5f) + P[OP_o3f] * fabsf_(P[OP_o3f]) * 0.5f;
    C.lv[0] = P[OP_o1l] * gin;
    C.lv[1] = P[OP_o2l] * gin;
    C.lv[2] = P[OP_o3l] * gin;
    C.nzc = (P[OP_noi] * ONE_BRUS + ONE_GRUNDBRUS) * gin;
    C.og = 0.85f / (1.f + P[OP_drv] * P[OP_drv] * 1.7f);
    C.kk = P[OP_res] * P[OP_res] * 4.3f;
    C.comp = 1.f + C.kk * ONE_COMP;
    C.cutB = 20.f * exp2_f(P[OP_cut] * 9.9657843f);          /* 20 * 1000^cut */
    C.md = md;
    C.spread = md == 0 ? 1.f : (md == 3 ? 0.55f : 0.f);
    C.ug = md == 3 ? 0.42f : (md == 2 ? 1.5f : 1.f);   /* mono: en ensam röst får ta mer plats */
    C.detc = P[OP_detune] * P[OP_detune] * 0.6f;
    C.egc = P[OP_eg1] * 7.f;
    C.velf = P[OP_velf];
    C.lfocut3 = P[OP_lfocut] * 3.f;
    C.kbd12 = P[OP_kbd] / 12.f;
    C.mwcut4 = P[OP_mwcut] * 4.f;
    C.vela = P[OP_vela];
    C.lfoamp5 = P[OP_lfoamp] * 0.5f;
    /* ensemble, eko och utgång ligger i bfx_fm1.c (delas av motorerna) */
    bfx_set(P[OP_chorus], P[OP_dtime], P[OP_dfb], P[OP_dmix], P[OP_vol]);
}

/* ------------------------------------------------------------------ ljudets tillstånd */
static volatile float vn[ONE_NV], vg[ONE_NV];  /* ton och gate (velocity 0..1) som huvudloopen sätter */
static volatile uint32_t vt[ONE_NV];           /* omstartsräknare */
static volatile float g_bend, g_mw, g_at;

typedef struct {
    uint32_t lt;
    int32_t s1, s2;
    float e1, e2, vel, pn, ph[3], f1, f2, f3, f4, dp;
} voice_t;
static voice_t V[ONE_NV];

static float lph, lsh, lsm;
static uint32_t rng, lrs;

static float m_env, m_peak, m_lfo;
static uint32_t m_mask;

static inline float noise(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return (float)rng * 4.656613e-10f - 1.f;    /* 2 / 2^32 */
}

/* S&H-LFO:ns egen slumpföljd (one.py: lrs * 16807 mod 2^31 - 1), utan 64-bitars division */
static inline float sh_next(void)
{
    uint64_t p = (uint64_t)lrs * 16807u;
    uint32_t x = (uint32_t)(p & 0x7fffffffu) + (uint32_t)(p >> 31);
    if (x >= 0x7fffffffu) x -= 0x7fffffffu;
    lrs = x;
    return (float)x * (1.f / 1073741823.5f) - 1.f;
}

/* polyBLEP: rundar ett språng vid fas 0 (byggklossar.polyblep) */
static inline float blep(float t, float dt, float idt)
{
    float x;
    if (t < dt) { x = t * idt; return x + x - x * x - 1.f; }
    if (t > 1.f - dt) { x = (t - 1.f) * idt; return x * x + x + x + 1.f; }
    return 0.f;
}
/* polyBLAMP: rundar ett hörn vid fas 0 (byggklossar.polyblamp) */
static inline float blamp(float t, float dt, float idt)
{
    float x;
    if (t < dt) { x = 1.f - t * idt; return x * x * x * (1.f / 6.f); }
    if (t > 1.f - dt) { x = 1.f + (t - 1.f) * idt; return x * x * x * (1.f / 6.f); }
    return 0.f;
}

static float frac_f(float x) { return x - ffloor(x); }

/* rösterna tystas och LFO:n börjar om; ensemble och eko rörs inte (de klingar vidare när modulen byter motor) */
void one_reset_voices(void)
{
    int i;
    for (i = 0; i < ONE_NV; i++) {
        voice_t *v = &V[i];
        v->lt = 0; v->s1 = v->s2 = 0;
        v->e1 = v->e2 = v->vel = v->f1 = v->f2 = v->f3 = v->f4 = 0.f;
        /* oscillator 2 och 3 börjar i egna faser (History v_p2, v_p3 i one.py, två decimaler) */
        v->ph[0] = 0.f;
        v->ph[1] = (float)((37 + i * 21) % 100) * 0.01f;
        v->ph[2] = (float)((71 + i * 13) % 100) * 0.01f;
        v->pn = 60.f;
        v->dp = (float)i * 0.173f;
        vn[i] = 60.f; vg[i] = 0.f; vt[i] = 0;
    }
    lph = lsh = lsm = 0.f;
    rng = 2463534242u;
    lrs = 20240917u;
    g_bend = g_mw = g_at = 0.f;
    m_env = m_peak = m_lfo = 0.f; m_mask = 0;
}

void one_reset(void)
{
    one_reset_voices();
    bfx_reset();
}

/* ------------------------------------------------------------------ ljud */
/* Ett anrop delas i tre pass så att rösterna kan räknas på två kärnor: LFO, pulsbredd och brus för hela anropet,
 * rösterna (vilka som helst, i vilken ordning som helst: varje röst har sitt eget tillstånd och skriver till sin
 * egen mix), sedan ensemble, eko och utgång. Blocken om ONE_K räknas från anropets början. */
#define lfo_all bfx_lfo                         /* arbetsminnet delas med BRIS-motorn (bfx_fm1.h) */
#define mixL bfx_mixL
#define mixR bfx_mixR
static float nz_all[ONE_MAXN];
static uint32_t call_n;
static float call_mwat, call_bs, part_me[2];
static uint32_t part_mask[2];

void one_render_begin(uint32_t n)
{
    uint32_t s;
    float nzc = C.nzc;
    call_n = n > ONE_MAXN ? ONE_MAXN : n;
    call_mwat = g_mw + g_at;
    call_bs = g_bend * C.bendr;
    /* LFO: triangel, ramp upp, ramp ned, fyrkant, sample & hold (egen slumpföljd), och röstbruset i varje sampel */
    for (s = 0; s < call_n; s++) {
        float raw;
        lph += C.lrinc;
        if (lph >= 1.f) { lph -= ffloor(lph); lsh = sh_next(); }
        switch (C.lw) {
        case 0: raw = 1.f - 4.f * fabsf_(lph - 0.5f); break;
        case 1: raw = 2.f * lph - 1.f; break;
        case 2: raw = 1.f - 2.f * lph; break;
        case 3: raw = lph < 0.5f ? 1.f : -1.f; break;
        default: raw = lsh; break;
        }
        lsm += (raw - lsm) * lsmc;
        lfo_all[s] = lsm;
        nz_all[s] = noise() * nzc;
        mixL[0][s] = mixR[0][s] = mixL[1][s] = mixR[1][s] = 0.f;
    }
    part_me[0] = part_me[1] = 0.f;
    part_mask[0] = part_mask[1] = 0;
}

/* En oscillator över ett block: lägger vågen gånger lv till mb[0..n-1] och ger tillbaka den nya fasen.
 * w: 0 triangel, 1 såg, 2 puls (pulsbredden följer LFO:n sampel för sampel). dt är fasökningen, samma för hela blocket. */
static float osc_block(float *mb, uint32_t n, float ph, float dt, int32_t w, float lv, const float *lfo)
{
    float idt = 1.f / dt;
    uint32_t s;
    if (w == 1) {                               /* såg */
        for (s = 0; s < n; s++) {
            ph += dt; if (ph >= 1.f) ph -= 1.f;
            mb[s] += (ph + ph - 1.f - blep(ph, dt, idt)) * lv;
        }
    } else if (w == 2) {                        /* puls */
        lv *= ONE_PULS;
        for (s = 0; s < n; s++) {
            float q, pw = fclamp(C.pwbase + lfo[s] * C.pwmc, 0.02f, 0.98f);
            ph += dt; if (ph >= 1.f) ph -= 1.f;
            q = ph + 1.f - pw; if (q >= 1.f) q -= 1.f;
            mb[s] += ((ph < pw ? 1.f : -1.f) + blep(ph, dt, idt) - blep(q, dt, idt)) * lv;
        }
    } else {                                    /* triangel, hörnen rundade */
        float d8 = 8.f * dt;
        for (s = 0; s < n; s++) {
            float q;
            ph += dt; if (ph >= 1.f) ph -= 1.f;
            q = ph + 0.5f; if (q >= 1.f) q -= 1.f;
            mb[s] += (1.f - 4.f * fabsf_(ph - 0.5f) + d8 * (blamp(ph, dt, idt) - blamp(q, dt, idt))) * lv;
        }
    }
    return ph;
}

static void voices_block(uint32_t first, uint32_t last, const float *lfo, const float *nz,
                         float *xL, float *xR, uint32_t n, float *mep, uint32_t *maskp)
{
    float mwat = call_mwat, bs = call_bs, me = *mep;
    uint32_t s, i, mask = *maskp;

    for (i = first; i < last; i++) {
        voice_t *v = &V[i];
        uint32_t t = vt[i];
        float g = vg[i];
        float e1b[ONE_K], e2b[ONE_K], mb[ONE_K];
        float dt[3], lG, dlG, iden, diden, kk, comp, velg, ampk, gL, gR, fmk, span;
        float e1, e2, f1, f2, f3, f4;
        int32_t s1, s2, k;
        uint32_t act;

        if (t != v->lt) { v->lt = t; v->s1 = 1; v->s2 = 1; v->vel = g; }
        if (v->s2 == 0) {                       /* tyst röst: EG1 klingar ändå av, som i originalet */
            if (v->s1 == 3) for (s = 0; s < n; s++) v->e1 -= v->e1 * C.fR;
            continue;
        }

        /* höljena för blocket först: då vet vi filtrets läge i början och slutet av blocket */
        e1 = v->e1; e2 = v->e2; s1 = v->s1; s2 = v->s2;
        act = n;
        for (s = 0; s < n; s++) {
            if (g <= 0.f && s2 > 0 && s2 < 3) { s1 = 3; s2 = 3; }
            if (s1 == 1) { e1 += C.fAinc; if (e1 >= 1.f) { e1 = 1.f; s1 = 2; } }
            else if (s1 == 2) e1 += (P[OP_fs] - e1) * C.fD;
            else if (s1 == 3) e1 -= e1 * C.fR;
            if (s2 == 1) { e2 += C.aAinc; if (e2 >= 1.f) { e2 = 1.f; s2 = 2; } }
            else if (s2 == 2) e2 += (P[OP_asus] - e2) * C.aD;
            else if (s2 == 3) { e2 -= e2 * C.aR; if (e2 < 0.0002f) { e2 = 0.f; s2 = 0; } }
            if (s2 == 0) {                      /* tyst från och med det här samplet */
                act = s;
                if (s1 == 3) for (s++; s < n; s++) e1 -= e1 * C.fR;
                break;
            }
            e1b[s] = e1; e2b[s] = e2;
        }
        v->e1 = e1; v->e2 = e2; v->s1 = s1; v->s2 = s2;
        if (act == 0) continue;

        /* styrsignaler för blocket */
        /* tonhöjden gäller mitt i blocket: glidning, drift och vibrato tas i mittsamplet */
        {
            float tgt = vn[i] + C.detc * DET[i], dpm, pnm, vib, pnn;
            pnm = v->pn + (tgt - v->pn) * C.cgH;
            v->pn += (tgt - v->pn) * C.cgK;
            dpm = v->dp + drinc[i] * 0.5f;
            v->dp += drinc[i];
            v->dp -= ffloor(v->dp);
            vib = (C.vib0 + mwat * C.vibw) * lfo[n >> 1];
            pnn = pnm + bs + vib + sin2pi(dpm) * 0.045f + sin2pi(dpm * 2.7f) * 0.02f - 69.f;
            /* tre oscillatorer: var sin stämning, och 2 och 3 driver lite kring 1. En som står på 0 behöver bara fasen. */
            dt[0] = fclamp(440.f * exp2_f((pnn + C.r[0]) * (1.f / 12.f)), 4.f, SR * 0.45f) * (1.f / SR);
            dt[1] = fclamp(440.f * exp2_f((pnn + C.r[1] + sin2pi(dpm * 2.f + 0.25f) * ONE_DRIFT) * (1.f / 12.f)), 4.f, SR * 0.45f) * (1.f / SR);
            dt[2] = fclamp(440.f * exp2_f((pnn + C.r[2] + sin2pi(dpm * 3.f + 0.6f) * ONE_DRIFT) * (1.f / 12.f)), 4.f, SR * 0.45f) * (1.f / SR);
        }
        velg = 1.f - C.velf + C.velf * v->vel;
        kk = C.kk; comp = C.comp;
        /* filtrets koefficienter i första och sista samplet, linjärt emellan */
        fmk = (v->pn - 60.f) * C.kbd12 + mwat * C.mwcut4;
        {
            float lGa, lGb, ia, ib, l4;
            float fca = fclamp(C.cutB * CUTS[i] * exp2_f(e1b[0] * C.egc * velg + lfo[0] * C.lfocut3 + fmk), 18.f, SR * 0.42f);
            float lg = tan_f(3.1415927f * fca / SR);
            lGa = lg / (1.f + lg);
            l4 = lGa * lGa; l4 *= l4;
            ia = 1.f / (1.f + kk * l4);
            if (act > 1) {
                float fcb = fclamp(C.cutB * CUTS[i] * exp2_f(e1b[act - 1] * C.egc * velg + lfo[act - 1] * C.lfocut3 + fmk), 18.f, SR * 0.42f);
                lg = tan_f(3.1415927f * fcb / SR);
                lGb = lg / (1.f + lg);
                l4 = lGb * lGb; l4 *= l4;
                ib = 1.f / (1.f + kk * l4);
                span = 1.f / (float)(act - 1);
            } else { lGb = lGa; ib = ia; span = 0.f; }
            lG = lGa; dlG = (lGb - lGa) * span;
            iden = ia; diden = (ib - ia) * span;
        }
        ampk = (1.f - C.vela + C.vela * v->vel) * C.ug * C.og;
        gL = 1.f - C.spread * PANL[i];
        gR = 1.f - C.spread * PANR[i];

        /* mixern: bruset, sedan varje oscillator som hörs (nivåerna är redan gånger drivningen) */
        for (s = 0; s < act; s++) mb[s] = nz[s];
        for (k = 0; k < 3; k++) {
            if (C.lv[k] > 0.f) v->ph[k] = osc_block(mb, act, v->ph[k], dt[k], C.w[k], C.lv[k], lfo);
            else v->ph[k] = frac_f(v->ph[k] + dt[k] * (float)act);
        }

        f1 = v->f1; f2 = v->f2; f3 = v->f3; f4 = v->f4;
        for (s = 0; s < act; s++) {
            float x, b1l, b2l, b3l, b4l, y1, y2, y3, y4, u, sv;
            float ilg, lG2, lG3, lG4;
            /* ZDF-stege (byggklossar.ladder); 1 / (1 + g) = 1 - G */
            ilg = 1.f - lG; lG2 = lG * lG; lG3 = lG2 * lG; lG4 = lG2 * lG2;
            x = mb[s];
            b1l = f1 * ilg; b2l = f2 * ilg; b3l = f3 * ilg; b4l = f4 * ilg;
            u = tanh_f(x - kk * ((lG4 * x + lG3 * b1l + lG2 * b2l + lG * b3l + b4l) * iden));
            y1 = lG * u + b1l; f1 = 2.f * y1 - f1;
            y2 = lG * y1 + b2l; f2 = 2.f * y2 - f2;
            y3 = lG * y2 + b3l; f3 = 2.f * y3 - f3;
            y4 = lG * y3 + b4l; f4 = 2.f * y4 - f4;
            lG += dlG; iden += diden;

            sv = y4 * comp * e2b[s] * ampk * (1.f - C.lfoamp5 * (1.f - lfo[s]));
            xL[s] += sv * gL;
            xR[s] += sv * gR;
        }
        v->f1 = f1; v->f2 = f2; v->f3 = f3; v->f4 = f4;
        if (s2 > 0) { me = fmaxf_(me, e2); mask |= 1u << i; }
    }
    *mep = me; *maskp = mask;
}

void one_render_voices(uint32_t first, uint32_t last, uint32_t mix)
{
    uint32_t off;
    if (last > ONE_NV) last = ONE_NV;
    mix &= 1u;
    for (off = 0; off < call_n; off++) mixL[mix][off] = mixR[mix][off] = 0.f;   /* (en omkörning börjar om rent) */
    part_me[mix] = 0.f; part_mask[mix] = 0;
    for (off = 0; off < call_n; off += ONE_K) {
        uint32_t k = call_n - off < ONE_K ? call_n - off : ONE_K;
        voices_block(first, last, lfo_all + off, nz_all + off, mixL[mix] + off, mixR[mix] + off, k, &part_me[mix], &part_mask[mix]);
    }
}

/* mixarna ihop, sedan ensemble, eko och utgång till L och R (call_n sampel) */
void one_render_end(float *L, float *R)
{
    uint32_t off, s;
    for (s = 0; s < call_n; s++) { mixL[0][s] += mixL[1][s]; mixR[0][s] += mixR[1][s]; }
    for (off = 0; off < call_n; off += ONE_K) {
        uint32_t k = call_n - off < ONE_K ? call_n - off : ONE_K;
        bfx_block(L + off, R + off, mixL[0] + off, mixR[0] + off, k);
    }
    m_env = fmaxf_(part_me[0], part_me[1]);
    m_mask = part_mask[0] | part_mask[1];
    m_peak = bfx_peak();
    if (call_n) m_lfo = lfo_all[call_n - 1];
}

void one_render(float *L, float *R, uint32_t n)
{
    while (n) {
        uint32_t k = n < ONE_MAXN ? n : ONE_MAXN;
        one_render_begin(k);
        one_render_voices(0, ONE_NV, 0);
        one_render_end(L, R);
        L += k; R += k; n -= k;
    }
}

/* ------------------------------------------------------------------ röster (samma som BRIS: bris_motor.h / bris_voices.js) */
typedef struct { int note; uint8_t on, sus; uint32_t age; } vstate_t;
static vstate_t VS[ONE_NV];
static int stack_[32], nstack, mode_, uni_, mono_on, sus_down;
static float mono_vel = 0.8f;
static uint32_t clock_, tc_;

static void trig(int i, int n, float vel, int retrig)
{
    VS[i].note = n; VS[i].on = 1; VS[i].sus = 0; VS[i].age = ++clock_;
    vn[i] = (float)n;                           /* tonen före gaten */
    if (retrig) { vg[i] = vel; vt[i] = ++tc_; }
}
static void stack_remove(int n)
{
    int i, j = 0;
    for (i = 0; i < nstack; i++) if (stack_[i] != n) stack_[j++] = stack_[i];
    nstack = j;
}
static void play_stack(int retrig)
{
    int n, i;
    if (!nstack) return;
    n = stack_[nstack - 1];
    if (mode_ == 2) { trig(0, n, mono_vel, retrig); return; }
    for (i = 0; i < ONE_NV; i++) trig(i, n + (i >= 3 ? uni_ : 0), mono_vel, retrig);
}

void one_all_notes_off(void)
{
    int i;
    nstack = 0; mono_on = 0;
    for (i = 0; i < ONE_NV; i++) { VS[i].on = 0; VS[i].sus = 0; vg[i] = 0.f; }
}

void one_note_on(int n, int vel)
{
    float v = (float)vel / 127.f;
    int i, k = -1;
    if (v < 0.05f) v = 0.05f;
    if (mode_ >= 2) {
        int retrig = !mono_on;
        stack_remove(n);
        if (nstack == 32) { int j; for (j = 1; j < 32; j++) stack_[j - 1] = stack_[j]; nstack = 31; }
        stack_[nstack++] = n;
        mono_on = 1; mono_vel = v;
        play_stack(retrig);
        return;
    }
    for (i = 0; i < ONE_NV; i++) if (VS[i].note == n && (VS[i].on || VS[i].sus)) { k = i; break; }
    if (k < 0) for (i = 0; i < ONE_NV; i++)
        if (!VS[i].on && !VS[i].sus && (k < 0 || VS[i].age < VS[k].age)) k = i;
    if (k < 0) { k = 0; for (i = 1; i < ONE_NV; i++) if (VS[i].age < VS[k].age) k = i; }
    trig(k, n, v, 1);
}

void one_note_off(int n)
{
    int i;
    if (mode_ >= 2) {
        stack_remove(n);
        if (nstack) { play_stack(0); return; }
        mono_on = 0;
        for (i = 0; i < ONE_NV; i++) if (VS[i].on) { VS[i].on = 0; if (sus_down) VS[i].sus = 1; else vg[i] = 0.f; }
        return;
    }
    for (i = 0; i < ONE_NV; i++)
        if (VS[i].on && VS[i].note == n) { VS[i].on = 0; VS[i].age = ++clock_; if (sus_down) VS[i].sus = 1; else vg[i] = 0.f; }
}

void one_sustain(int down)
{
    int i;
    sus_down = down;
    if (!down) for (i = 0; i < ONE_NV; i++) if (VS[i].sus) { VS[i].sus = 0; vg[i] = 0.f; }
}
void one_bend(float v) { g_bend = fclamp(v, -1.f, 1.f); }
void one_modwheel(float v) { g_mw = fclamp(v, 0.f, 1.f); }
void one_aftertouch(float v) { g_at = fclamp(v, 0.f, 1.f); }

/* ------------------------------------------------------------------ parametrar och ljud */
void one_set(int i, float x)
{
    const one_param_t *p;
    if (i < 0 || i >= OP_N) return;
    p = &ONE_PARAMS[i];
    x = fclamp(x, p->lo, p->hi);
    if (p->integer) x = (float)ifloor(x + 0.5f);
    P[i] = x;
    if (i == OP_lage) {
        int m = ifloor(x + 0.5f);
        m = m < 0 ? 0 : m > 3 ? 3 : m;
        /* (läget först: tonerna kan slås an från ljudavbrottet mitt i det här, och en ton som kommer emellan ska
         * tystas av raden efter, inte bli kvar i ett läge som inte längre gäller) */
        if (m != mode_) { mode_ = m; one_all_notes_off(); }
    } else if (i == OP_uniint) {
        uni_ = ifloor(x + 0.5f);
        if (mode_ == 3 && mono_on) play_stack(0);
    }
    coeffs();
}
float one_get(int i) { return (i >= 0 && i < OP_N) ? P[i] : 0.f; }

void one_load_preset(int index)
{
    float nv[OP_N];
    int i, j;
    const one_preset_t *pr;
    if (index < 0 || index >= ONE_NPRESETS) return;
    pr = &ONE_PRESETS[index];
    for (i = 0; i < OP_N; i++) nv[i] = ONE_PARAMS[i].keep ? P[i] : ONE_PARAMS[i].def;
    for (j = 0; j < pr->count; j++) {
        const one_pv_t *v = &ONE_PV[pr->first + j];
        if (!ONE_PARAMS[v->param].keep) nv[v->param] = v->value;
    }
    for (i = 0; i < OP_N; i++) one_set(i, nv[i]);
}

void one_init(void)
{
    int i;
    lsmc = 1.f - exp_f(-1.f / (SR * 0.0007f));
    bfx_init();
    for (i = 0; i < ONE_NV; i++) drinc[i] = (0.07f + (float)i * 0.023f) / SR * (float)ONE_K;
    mode_ = 0; uni_ = 0; nstack = 0; mono_on = 0; sus_down = 0; clock_ = 0; tc_ = 0;
    for (i = 0; i < ONE_NV; i++) { VS[i].note = -1; VS[i].on = VS[i].sus = 0; VS[i].age = 0; }
    for (i = 0; i < OP_N; i++) P[i] = ONE_PARAMS[i].def;
    one_reset();
    one_load_preset(0);
}

float one_meter_env(void) { return m_env; }
uint32_t one_meter_voices(void) { return m_mask; }
float one_meter_peak(void) { return m_peak; }
float one_meter_lfo(void) { return m_lfo; }
