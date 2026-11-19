/* SPDX-License-Identifier: GPL-3.0-only */
/* BLONDA RUM för FM-1 – rad för rad efter blonda-motor/motorer/rum/rum_gen.h (samma GenExpr som Max for
 * Live-pluggen). Skillnader: float, egna approximationer, int16-buffertar, och linjernas längd, dämpning och
 * allpassens längd en gång per block om RUM_K sampel (längden interpoleras över blocket, så att storleken kan vridas
 * utan knaster), och varje buffert räknad för sig över blocket.
 * Testas mot blonda-motor i gemensamt/test/rum_test.cpp. */
#include "rum_fm1.h"
#include "snabbmatte.h"

#define SR ((float)RUM_SR)
#define TWOPI 6.2831853f
#define QL 8192.f                               /* linjer och allpass: Q13 */
#define IQL (1.f / 8192.f)
#define RUM_KEEP 0.98412f                        /* (1 - 0.0005)^RUM_K */

/* mycket små tal nollas i slutet av varje block: annars klingar filtren av ner i tal som chipet räknar långsamt med */
#define FLUSH(x) (((x) < 1e-18f && (x) > -1e-18f) ? 0.f : (x))

static const float DEF[RU_NPARAM] = {0.55f, 0.5f, 0.45f, 0.15f, 0.25f, 0.35f, 1.f, 0.3f, 0.f, 0.75f, 0.f, 1.f};

/* linjernas längd (ms vid size-faktorn 1), rörelsens takt, fas och djup (sampel) – ur GenExpr */
static const float L_MS[8] = {29.7f, 37.1f, 41.1f, 43.7f, 53.9f, 61.3f, 67.9f, 73.1f};
static const float L_RATE[8] = {1.00f, 1.13f, 1.26f, 1.39f, 1.52f, 1.65f, 1.78f, 1.91f};
static const float L_PH[8] = {0.f, 0.125f, 0.25f, 0.375f, 0.5f, 0.625f, 0.75f, 0.875f};
static const float L_DEP[8] = {9.0f, 8.4f, 7.8f, 7.2f, 6.6f, 6.0f, 5.4f, 4.8f};
static const int32_t L_N[8] = {RUM_LN0, RUM_LN1, RUM_LN2, RUM_LN3, RUM_LN4, RUM_LN5, RUM_LN6, RUM_LN7};
/* var varje linje börjar i r->dl (varje har ett sampel extra i slutet) */
#define AT1 (RUM_LN0 + 1)
#define AT2 (AT1 + RUM_LN1 + 1)
#define AT3 (AT2 + RUM_LN2 + 1)
#define AT4 (AT3 + RUM_LN3 + 1)
#define AT5 (AT4 + RUM_LN4 + 1)
#define AT6 (AT5 + RUM_LN5 + 1)
#define AT7 (AT6 + RUM_LN6 + 1)
static const int32_t L_AT[8] = {0, AT1, AT2, AT3, AT4, AT5, AT6, AT7};
static const float A_MS[4] = {4.77f, 3.59f, 12.73f, 9.31f};
static const int32_t A_N[4] = {RUM_AN0, RUM_AN1, RUM_AN2, RUM_AN3};
static const int32_t A_AT[4] = {0, RUM_AN0, RUM_AN0 + RUM_AN1, RUM_AN0 + RUM_AN1 + RUM_AN2};

static inline float onepole(float hz) { return 1.f - sm_exp(-TWOPI * hz / SR); }

/* Till int16, avrundat, med felet buret till nästa sampel (första ordningens brusformning): avrundningsbruset
 * hamnar i diskanten, där linjernas dämpning och interpolation tar bort det, i stället för i hela registret.
 * e är felet från förra samplet i samma buffert; klipps värdet bärs inget fel. */
#define QWRITE(dst, val, scale, e) do { \
    float qv_ = (val) * (scale) + (e); \
    int32_t qi_ = (int32_t)(qv_ + 32768.5f) - 32768; \
    if (qv_ > 32767.f) { qi_ = 32767; qv_ = 32767.f; } \
    else if (qv_ < -32767.f) { qi_ = -32767; qv_ = -32767.f; } \
    (e) = qv_ - (float)qi_; \
    (dst) = (int16_t)qi_; \
} while (0)

void rum_reset(rum_t *r)
{
    int i;
    r->sS = 0.55f;
    r->lph = r->lcs = r->hrLs = r->hrRs = r->hr2Ls = r->hr2Rs = r->env = r->pk = 0.f;
    r->pi0 = 0; r->pr = 0; r->started = 0;
    for (i = 0; i < 8; i++) { r->z[i] = 0.f; r->wi[i] = 0; r->ln[i] = 0.f; r->el[i] = 0.f; r->g[i] = 0.f; }
    for (i = 0; i < 4; i++) { r->api[i] = 0; r->ea[i] = 0.f; }
    r->ep = 0.f; r->dirty = 1; r->gk = 0;
    for (i = 0; i < RUM_PDN; i++) r->pd[i] = 0;
    for (i = 0; i < RUM_ASUM; i++) r->ap[i] = 0;
    for (i = 0; i < RUM_LSUM; i++) r->dl[i] = 0;
}

void rum_init(rum_t *r)
{
    int i;
    for (i = 0; i < RU_NPARAM; i++) r->p[i] = DEF[i];
    rum_reset(r);
}

void rum_set(rum_t *r, int i, float v)
{
    if (i < 0 || i >= RU_NPARAM) return;
    r->p[i] = sm_clamp(v, 0.f, 1.f);
    r->dirty = 1;
}

float rum_get(const rum_t *r, int i) { return (i < 0 || i >= RU_NPARAM) ? 0.f : r->p[i]; }

/* koefficienter som bara beror på parametrarna: räknas när en parameter ändrats */
static void coeffs(rum_t *r)
{
    const float *p = r->p;
    float hrf = 20000.f * sm_exp2((1.f - p[RU_highc]) * -5.0588937f);                     /* 20000 * 0.03^(1 - highc) */
    float pds = sm_clamp(p[RU_predel] * p[RU_predel] * 0.25f * SR, 1.f, (float)(RUM_PDN - 2));
    float wet = p[RU_wet];
    if (hrf > SR * 0.45f) hrf = SR * 0.45f;
    r->c_dk = onepole(1500.f + (1.f - p[RU_damp]) * (1.f - p[RU_damp]) * 16000.f);
    r->c_lck = onepole(20.f + p[RU_lowc] * p[RU_lowc] * 600.f);
    r->c_hrk = onepole(hrf);
    r->c_hc = p[RU_highc] > 0.995f ? 0 : 1;
    r->c_sck = (p[RU_shape] > 0.5f ? 0.75f : 1.f) * (SR / 1000.f);
    r->c_irt = 1.f / (SR * 0.25f * sm_exp2(p[RU_decay] * 5.3219281f) * (p[RU_shape] > 0.5f ? 0.8f : 1.f));   /* rt = 0.25 * 40^decay */
    /* predelay: originalet läser vid floor(pi0 - pds), alltså ceil(pds) sampel bakåt */
    r->c_pdd = sm_ifloor(pds);
    if ((float)r->c_pdd < pds) r->c_pdd++;
    r->c_dryG = wet < 0.5f ? 1.f : 2.f - 2.f * wet;
    r->c_wetG = wet < 0.5f ? 2.f * wet : 1.f;
    r->c_ok = p[RU_outv] * 1.33f * 0.9f;
    r->dirty = 0;
}

/* Ett block om högst RUM_K sampel. Alla fördröjningar i nätet är längre än ett block (kortaste linjen 335 sampel,
 * kortaste allpass 94), så varje buffert kan räknas för sig över hela blocket: läsningarna rör bara det som skrevs
 * före blocket. Det ger korta slingor som chipet räknar fort. */
static void tail_block(rum_t *r, float *wetL, float *wetR, uint32_t n, uint32_t min_delay)
{
    const float *p = r->p;
    float lnT[8], in = 1.f / (float)n, *xb = r->xb, *hb = r->hb;
    int32_t apn[4], i;
    uint32_t s;

    if (r->dirty) coeffs(r);
    /* storleken glider mot ratten: m_sS += (size - m_sS) * 0.0005 per sampel, här n sampel i ett steg */
    r->sS = p[RU_size] + (r->sS - p[RU_size]) * (n == RUM_K ? RUM_KEEP : sm_exp2((float)n * -0.00072152f));
    r->lph += 0.37f / SR * (float)n;
    if (r->lph >= 1.f) r->lph -= 1.f;
    /* linjernas längd i slutet av blocket (GenExpr: m_ln0..7), rörelsen med */
    {
        float sc = (0.35f + r->sS * 1.65f) * r->c_sck, modd = p[RU_modd];
        for (i = 0; i < 8; i++)
            lnT[i] = sm_clamp(L_MS[i] * sc + sm_sin2pi(sm_wrap1(r->lph * L_RATE[i] + L_PH[i])) * modd * L_DEP[i],
                              (float)(RUM_K + 3), (float)(L_N[i] - 3));
    }
    if (!r->started) { for (i = 0; i < 8; i++) r->ln[i] = lnT[i]; r->started = 1; r->gk = 0; }
    /* avklingningen per varv, 10^(-3 * ln / sr / rt): två linjer per block räcker (längden rör sig långsamt),
     * alla när en parameter just ändrats */
    if (r->gk < 8) { for (i = 0; i < 8; i++) r->g[i] = sm_exp2(-9.9657843f * lnT[i] * r->c_irt); r->gk = 8; }
    else {
        i = r->gk & 3;
        r->g[i] = sm_exp2(-9.9657843f * lnT[i] * r->c_irt);
        r->g[i + 4] = sm_exp2(-9.9657843f * lnT[i + 4] * r->c_irt);
        r->gk = 8 + ((r->gk + 1) & 3);
    }
    {
        float af = (SR / 1000.f) * (0.6f + r->sS * 0.6f);
        for (i = 0; i < 4; i++) { apn[i] = sm_ifloor(A_MS[i] * af); if (apn[i] > A_N[i] - 1) apn[i] = A_N[i] - 1; }
    }

    /* predelay (det rum_feed skrivit) och lågskärning */
    {
        int32_t pdd = r->c_pdd < (int32_t)min_delay ? (int32_t)min_delay : r->c_pdd, rd = r->pr - pdd;
        float lcs = r->lcs, lck = r->c_lck;
        const int16_t *pd = r->pd;
        if (rd < 0) rd += RUM_PDN;
        for (s = 0; s < n; s++) {
            float x = (float)pd[rd] * (1.f / 32767.f);
            if (++rd >= RUM_PDN) rd = 0;
            lcs += (x - lcs) * lck;
            xb[s] = x - lcs;
        }
        r->lcs = FLUSH(lcs);
        r->pr += (int32_t)n; if (r->pr >= RUM_PDN) r->pr -= RUM_PDN;
    }
    /* fyra allpass (koefficient 0,6) sprider ljudet */
    for (i = 0; i < 4; i++) {
        int16_t *b = r->ap + A_AT[i];
        int32_t N = A_N[i], w = r->api[i], k = w - apn[i];
        float e = r->ea[i];
        if (k < 0) k += N;
        if (k + (int32_t)n <= N && w + (int32_t)n <= N) {       /* ingen av dem går runt i det här blocket */
            const int16_t *rd = b + k;
            int16_t *wr = b + w;
            for (s = 0; s < n; s++) {
                float d = (float)rd[s] * IQL, v = xb[s] + d * 0.6f;
                QWRITE(wr[s], v, QL, e);
                xb[s] = d - v * 0.6f;
            }
            w += (int32_t)n; if (w >= N) w = 0;
        } else {
            for (s = 0; s < n; s++) {
                float d = (float)b[k] * IQL, v = xb[s] + d * 0.6f;
                QWRITE(b[w], v, QL, e);
                xb[s] = d - v * 0.6f;
                if (++k >= N) k = 0;
                if (++w >= N) w = 0;
            }
        }
        r->api[i] = w; r->ea[i] = e;
    }
    /* åtta linjer: läs interpolerat ln sampel bakåt, klinga av (g), dämpa (dk) */
    for (i = 0; i < 8; i++) {
        const int16_t *b = r->dl + L_AT[i];
        float N = (float)L_N[i], dln = (lnT[i] - r->ln[i]) * in, inc = 1.f - dln;
        float rp = (float)r->wi[i] - (r->ln[i] + dln), z = r->z[i], gq = r->g[i] * IQL, dk = r->c_dk, *zb = r->zb[i];
        if (rp < 0.f) rp += N;
        {
            float end = rp + inc * (float)n;
            if (end >= 0.f && end < N - 1.f) {  /* läsningen går inte runt i det här blocket */
                for (s = 0; s < n; s++) {
                    int32_t a = (int32_t)rp;
                    float fr = rp - (float)a, y = (float)b[a];
                    y += ((float)b[a + 1] - y) * fr;
                    z += (y * gq - z) * dk;
                    zb[s] = z;
                    rp += inc;
                }
            } else {
                for (s = 0; s < n; s++) {
                    int32_t a = (int32_t)rp;
                    float fr = rp - (float)a, y = (float)b[a];
                    y += ((float)b[a + 1] - y) * fr;
                    z += (y * gq - z) * dk;
                    zb[s] = z;
                    rp += inc;
                    if (rp >= N) rp -= N; else if (rp < 0.f) rp += N;
                }
            }
        }
        r->z[i] = FLUSH(z);
        r->ln[i] += dln * (float)n;
    }
    /* blanda (Householder: varje linje får sig själv minus en fjärdedel av summan), ta ut vänster och höger */
    {
        const float *z0 = r->zb[0], *z1 = r->zb[1], *z2 = r->zb[2], *z3 = r->zb[3], *z4 = r->zb[4], *z5 = r->zb[5],
                    *z6 = r->zb[6], *z7 = r->zb[7];
        float hrLs = r->hrLs, hrRs = r->hrRs, hr2Ls = r->hr2Ls, hr2Rs = r->hr2Rs, hrk = r->c_hrk, width = p[RU_width] * 0.5f;
        int32_t hc = r->c_hc;
        for (s = 0; s < n; s++) {
            float wl, wr, mid, side;
            hb[s] = (z0[s] + z1[s] + z2[s] + z3[s] + z4[s] + z5[s] + z6[s] + z7[s]) * 0.25f;
            xb[s] *= 0.35f;
            wl = (z0[s] - z2[s] + z4[s] - z6[s] + z1[s] * 0.5f) * 0.6f;
            wr = (z1[s] - z3[s] + z5[s] - z7[s] + z2[s] * 0.5f) * 0.6f;
            /* höskärning, två poler (tillståndet räknas alltid, som i originalet) */
            hrLs += (wl - hrLs) * hrk;
            hrRs += (wr - hrRs) * hrk;
            hr2Ls += (hrLs - hr2Ls) * hrk;
            hr2Rs += (hrRs - hr2Rs) * hrk;
            if (hc) { wl = hr2Ls; wr = hr2Rs; }
            mid = (wl + wr) * 0.5f;
            side = (wl - wr) * width;
            wetL[s] = mid + side;
            wetR[s] = mid - side;
        }
        r->hrLs = FLUSH(hrLs); r->hrRs = FLUSH(hrRs); r->hr2Ls = FLUSH(hr2Ls); r->hr2Rs = FLUSH(hr2Rs);
    }
    /* skriv tillbaka i linjerna, med insignalen (varannan linje med omvänt tecken) */
    for (i = 0; i < 8; i++) {
        int16_t *b = r->dl + L_AT[i];
        const float *zb = r->zb[i];
        int32_t N = L_N[i], w = r->wi[i];
        float e = r->el[i];
        if (w + (int32_t)n <= N) {              /* går inte runt i det här blocket */
            int16_t *wr = b + w;
            if (i & 1) for (s = 0; s < n; s++) QWRITE(wr[s], zb[s] - hb[s] - xb[s], QL, e);
            else for (s = 0; s < n; s++) QWRITE(wr[s], zb[s] - hb[s] + xb[s], QL, e);
            if (w == 0) b[N] = b[0];            /* kopian i slutet, för interpolationen */
            w += (int32_t)n; if (w >= N) w = 0;
        } else {
            float sg = (i & 1) ? -1.f : 1.f;
            for (s = 0; s < n; s++) {
                QWRITE(b[w], zb[s] - hb[s] + xb[s] * sg, QL, e);
                if (w == 0) b[N] = b[0];
                if (++w >= N) w = 0;
            }
        }
        r->wi[i] = w; r->el[i] = e;
    }
}

void rum_tail(rum_t *r, float *wetL, float *wetR, uint32_t n, uint32_t min_delay)
{
    while (n) {
        uint32_t k = n > RUM_K ? RUM_K : n;
        tail_block(r, wetL, wetR, k, min_delay);
        wetL += k; wetR += k; n -= k;
    }
}

void rum_resync(rum_t *r) { r->pr = r->pi0; }

void rum_feed(rum_t *r, const float *L, const float *R, uint32_t n)
{
    int16_t *pd = r->pd;
    int32_t w = r->pi0;
    float e = r->ep;
    uint32_t s;
    for (s = 0; s < n; s++) {
        QWRITE(pd[w], (L[s] + R[s]) * 0.5f, 32767.f, e);
        if (++w >= RUM_PDN) w = 0;
    }
    r->pi0 = w; r->ep = e;
}

void rum_mix(rum_t *r, float *L, float *R, const float *wetL, const float *wetR, uint32_t n)
{
    float dryG, wetG, ok, pk = r->pk, env = r->env, duck8 = r->p[RU_duck] * 8.f;
    uint32_t s;
    if (r->dirty) coeffs(r);
    dryG = r->c_dryG; wetG = r->c_wetG; ok = r->c_ok;
    for (s = 0; s < n; s++) {
        float l = L[s], rr = R[s], a = wetL[s], b = wetR[s], m, ia = sm_abs(l) + sm_abs(rr);
        /* ducking: klangen viker undan för insignalen */
        env = ia > env ? env + (ia - env) * 0.01f : env * 0.99985f;
        if (duck8 > 0.f) { float dkk = 1.f / (1.f + env * duck8); a *= dkk; b *= dkk; }
        L[s] = sm_tanh((l * dryG + a * wetG) * ok) * (1.f / 0.9f);
        R[s] = sm_tanh((rr * dryG + b * wetG) * ok) * (1.f / 0.9f);
        m = sm_abs(a) + sm_abs(b);
        pk *= 0.9997f;
        if (m > pk) pk = m;
    }
    r->pk = FLUSH(pk); r->env = FLUSH(env);
}

void rum_process(rum_t *r, float *L, float *R, uint32_t n)
{
    float wl[RUM_K], wr[RUM_K];
    while (n) {
        uint32_t k = n > RUM_K ? RUM_K : n;
        rum_feed(r, L, R, k);
        tail_block(r, wl, wr, k, 1);
        rum_mix(r, L, R, wl, wr, k);
        L += k; R += k; n -= k;
    }
}
