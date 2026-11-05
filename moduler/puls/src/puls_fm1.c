/* SPDX-License-Identifier: GPL-3.0-only */
/* BLONDA PULS för M-VAVE FM-1. Se puls_fm1.h. Signalvägen följer blonda-motor/motorer/puls/puls.genexpr rad för
 * rad (samma ordning, samma brus), men allt som bara beror på rattarna räknas i coef() när en ratt har ändrats. */
#include "puls_fm1.h"
#include "snabbmatte.h"

#define SR ((float)PULS_SR)
#define ISR (1.f / (float)PULS_SR)
#define OFF 0.0001f                             /* under det är ett trumljud tyst (som originalet) */

/* ------------------------------------------------------------------ rattar */
static volatile float P[PP_N];
static volatile uint32_t pver;                  /* ökar när en ratt ändras */
static uint32_t cver = ~0u;                     /* versionen som koefficienterna räknades för */

/* ------------------------------------------------------------------ slag (huvudloopen -> ljudavbrottet) */
static volatile uint32_t req[PULS_NV];          /* ökar för varje slag */
static volatile float req_vel[PULS_NV];
static volatile int32_t req_semi;               /* pukans halvtoner för senaste slaget */
static uint32_t seen[PULS_NV];

/* ------------------------------------------------------------------ koefficienter (från rattarna) */
typedef struct { float g, a1, ga1, k; } svf_c;  /* tillståndsvariabelt filter (TPT) */
static struct {
    float bd_base, bd_amt, bd_dpe, bd_dae, bd_dce, bd_clk, bd_drv;
    float sd_f0, sd_dbe, sd_dne, sd_body, sd_nz;
    float rs_i1, rs_i2, rs_de, rs_dce, rs_clk, rs_drv;
    float cp_sp, cp_dbe, cp_dte;
    uint32_t hat_inc[2][6]; float hat_de[2], oh_dck, hat_nz[2], hat_mt[2];
    float tm_base0, tm_amt, tm_dpe, tm_dae, tm_dce;
    uint32_t cb_i1, cb_i2; float cb_d1, cb_d2, cb_de1, cb_de2;
    svf_c sd_f, cp_f, hat_f[2], cb_f;
    float lev[PULS_NV], gl, outg;
} K;

/* ------------------------------------------------------------------ tillstånd */
static struct {
    sm_noise_t nz;
    uint8_t on[PULS_NV];                        /* trumljudet klingar (en byte var: kärnorna skriver var sina) */
    float vel[PULS_NV];
    float bd_ph, bd_ae, bd_pe, bd_ce;
    float sd_p1, sd_p2, sd_be, sd_ne, sd_a, sd_b;
    float rs_p1, rs_p2, rs_e, rs_ce;
    float cp_c, cp_n, cp_be, cp_te, cp_a, cp_b;
    uint32_t hat_q[2][6];                       /* fas 0..1 som Q32: exakt som originalets double, och billigt */
    float hat_e[2], hat_a[2], hat_b[2];
    int oh_ck;
    float tm_ph, tm_ae, tm_pe, tm_ce, tm_base;
    uint32_t cb_q1, cb_q2;                      /* Q32, går hela tiden */
    float cb_e1, cb_e2, cb_a, cb_b;
    float dcxL, dcyL, dcxR, dcyR, pk;
    uint32_t mask;
} S;

/* ------------------------------------------------------------------ matte */
static float f_pow(float a_log2, float x) { return sm_exp2(a_log2 * x); }       /* a^x med a_log2 = log2(a) */
static float decay(float t) { return sm_exp2(-1.4426950f / (SR * t)); }         /* exp(-1 / (sr t)) */
static float tan_pi(float x) { float h = x * 0.5f; return sm_sin2pi(h) / sm_sin2pi(h + 0.25f); }   /* tan(pi x) */
static void svf_set(svf_c *f, float fc, float k)
{
    f->g = tan_pi(sm_clamp(fc, 10.f, SR * 0.45f) * ISR);
    f->a1 = 1.f / (1.f + f->g * (f->g + k));
    f->ga1 = f->g * f->a1;
    f->k = k;
}
/* ett steg; ger bandpass, *hp får högpass */
static inline float svf_bp(const svf_c *f, float *a, float *b, float x)
{
    float v3 = x - *b, v1 = f->a1 * *a + f->ga1 * v3, v2 = *b + f->g * v1;
    *a = 2.f * v1 - *a;
    *b = 2.f * v2 - *b;
    return v1;
}
static inline float svf_hp(const svf_c *f, float *a, float *b, float x)
{
    float v3 = x - *b, v1 = f->a1 * *a + f->ga1 * v3, v2 = *b + f->g * v1;
    *a = 2.f * v1 - *a;
    *b = 2.f * v2 - *b;
    return x - f->k * v1 - v2;
}
static inline float blep(float t, float dt)
{
    if (t < dt) { float u = t / dt; return u * 2.f - u * u - 1.f; }
    if (t > 1.f - dt) { float u = (t - 1.f) / dt; return u * u + u * 2.f + 1.f; }
    return 0.f;
}
static inline float sq(float q) { return q < 0.5f ? 1.f : -1.f; }

static const float HATF[6] = {205.3f, 304.4f, 369.6f, 522.7f, 540.f, 800.f};
#define L2_4 2.f
#define L2_30 4.9068906f
#define L2_18 4.1699250f
#define L2_22 4.4594316f
#define L2_12 3.5849625f

static void coef(void)
{
    int h, j;
    float t;
    /* BD */
    K.bd_base = 30.f * f_pow(L2_4, P[PP_bdtun]);
    K.bd_amt = 1.2f + P[PP_bdton] * 4.5f;
    K.bd_dpe = decay(0.007f + 0.025f * P[PP_bdton]);
    K.bd_dae = decay(0.05f * f_pow(L2_30, P[PP_bddec]));
    K.bd_dce = decay(0.0012f);
    K.bd_clk = P[PP_bdton] * 0.7f;
    K.bd_drv = 1.f + P[PP_bdton] * 2.5f;
    /* SD */
    K.sd_f0 = 160.f * sm_exp2((P[PP_sdtun] - 0.5f) * 2.f);
    K.sd_dbe = decay(0.035f + 0.12f * P[PP_sddec]);
    K.sd_dne = decay(0.04f + 0.32f * P[PP_sddec]);
    K.sd_body = 1.1f - 0.7f * P[PP_sdton];
    K.sd_nz = 0.5f + 1.6f * P[PP_sdton];
    svf_set(&K.sd_f, 2600.f * sm_exp2(P[PP_sdtun] - 0.5f), 1.3f);
    /* RS */
    t = 1650.f * sm_exp2((P[PP_rstun] - 0.5f) * 1.5f);
    K.rs_i1 = t * ISR;
    K.rs_i2 = t * 0.29f * ISR;
    K.rs_de = decay(0.005f + 0.045f * P[PP_rsdec]);
    K.rs_dce = decay(0.0007f);
    K.rs_clk = P[PP_rston] * 1.5f;
    K.rs_drv = 1.5f + P[PP_rston] * 3.f;
    /* CP */
    K.cp_sp = (0.006f + P[PP_cpton] * 0.012f) * SR;
    K.cp_dbe = decay(0.0035f);
    K.cp_dte = decay(0.03f + 0.35f * P[PP_cpdec]);
    svf_set(&K.cp_f, 1000.f * sm_exp2((P[PP_cptun] - 0.5f) * 2.f), 0.55f);
    /* CH, OH */
    for (h = 0; h < 2; h++) {
        int base = h ? PP_ohlev : PP_chlev;     /* lev tun dec ton i följd */
        float fs = sm_exp2((P[base + 1] - 0.5f) * 1.5f);
        for (j = 0; j < 6; j++) K.hat_inc[h][j] = (uint32_t)(HATF[j] * fs * ISR * 4294967296.f);   /* fas i Q32 */
        K.hat_mt[h] = 1.f - P[base + 3];
        K.hat_nz[h] = P[base + 3] * 1.2f;
        svf_set(&K.hat_f[h], 7200.f * fs, 1.1f);
    }
    K.hat_de[0] = decay(0.008f * f_pow(L2_18, P[PP_chdec]));
    K.hat_de[1] = decay(0.06f * f_pow(L2_22, P[PP_ohdec]));
    K.oh_dck = decay(0.006f);
    /* TOM */
    K.tm_base0 = 70.f * f_pow(L2_4, P[PP_tmtun]);
    K.tm_amt = 0.1f + P[PP_tmton] * 1.2f;
    K.tm_dpe = decay(0.045f);
    K.tm_dae = decay(0.07f * f_pow(L2_12, P[PP_tmdec]));
    K.tm_dce = decay(0.002f);
    /* CB */
    t = sm_exp2((P[PP_cbtun] - 0.5f) * 1.5f);
    K.cb_d1 = 540.f * t * ISR;
    K.cb_d2 = 800.f * t * ISR;
    K.cb_i1 = (uint32_t)(K.cb_d1 * 4294967296.f);
    K.cb_i2 = (uint32_t)(K.cb_d2 * 4294967296.f);
    K.cb_de1 = decay(0.009f);
    K.cb_de2 = decay(0.04f * f_pow(L2_12, P[PP_cbdec]));
    svf_set(&K.cb_f, 2640.f * t * sm_exp2(P[PP_cbton] - 0.5f), 0.7f);
    /* nivåer */
    K.lev[PV_BD] = P[PP_bdlev]; K.lev[PV_SD] = P[PP_sdlev]; K.lev[PV_RS] = P[PP_rslev]; K.lev[PV_CP] = P[PP_cplev];
    K.lev[PV_CH] = P[PP_chlev]; K.lev[PV_OH] = P[PP_ohlev]; K.lev[PV_TOM] = P[PP_tmlev]; K.lev[PV_CB] = P[PP_cblev];
    /* summa */
    K.gl = P[PP_vol] * P[PP_vol] * (1.1f + P[PP_drv] * 5.f);
    K.outg = 1.f - P[PP_drv] * 0.3f;
}

/* ------------------------------------------------------------------ slagen börjar */
static void start(int v)
{
    S.vel[v] = req_vel[v];
    S.on[v] = 1;
    switch (v) {
    case PV_BD: S.bd_ae = 1.f; S.bd_pe = 1.f; S.bd_ce = 1.f; S.bd_ph = 0.f; break;
    case PV_SD: S.sd_be = 1.f; S.sd_ne = 1.f; S.sd_p1 = 0.f; S.sd_p2 = 0.f; S.sd_a = 0.f; S.sd_b = 0.f; break;
    case PV_RS: S.rs_e = 1.f; S.rs_ce = 1.f; S.rs_p1 = 0.f; S.rs_p2 = 0.f; break;
    case PV_CP: S.cp_c = 0.f; S.cp_n = 0.f; S.cp_be = 0.f; S.cp_te = 0.f; S.cp_a = 0.f; S.cp_b = 0.f; break;
    case PV_CH: S.hat_e[0] = 1.f; S.hat_a[0] = 0.f; S.hat_b[0] = 0.f; S.oh_ck = 1; break;
    case PV_OH: S.hat_e[1] = 1.f; S.hat_a[1] = 0.f; S.hat_b[1] = 0.f; S.oh_ck = 0; break;
    case PV_TOM:
        S.tm_ae = 1.f; S.tm_pe = 1.f; S.tm_ce = 1.f; S.tm_ph = 0.f;
        S.tm_base = sm_exp2((float)req_semi * (1.f / 12.f));
        break;
    default: S.cb_e1 = 1.f; S.cb_e2 = 1.f; S.cb_a = 0.f; S.cb_b = 0.f; break;
    }
}

/* ------------------------------------------------------------------ ljud
 * Ett block i tre steg: begin (slagen, bruset för hela blocket), trumljuden (vart och ett över hela blocket, i två
 * delar som kan räknas på var sin kärna, till var sin mix), end (summa, drivning, volym). Bruset är samma följd som
 * originalets (ett värde per sampel), så ordningen mellan trumljuden spelar ingen roll. */
static float nzb[PULS_MAXN];
static float mixb[2][2][PULS_MAXN];             /* [mix][vänster/höger][sampel] */
static uint32_t rn;
static const float PAN_L[PULS_NV] = {1.f, 0.95f, 1.f, 0.85f, 0.65f, 0.6f, 1.f, 1.f};
static const float PAN_R[PULS_NV] = {1.f, 1.f, 0.7f, 1.f, 1.f, 1.f, 0.75f, 0.6f};

#define OUT(i, x) do { const float o_ = (x); mL[i] += o_ * pl; mR[i] += o_ * pr; } while (0)

static void v_bd(float *mL, float *mR, float pl, float pr)
{
    float ph = S.bd_ph, pe = S.bd_pe, ae = S.bd_ae, ce = S.bd_ce, g = S.vel[PV_BD] * K.lev[PV_BD];
    const float base = K.bd_base * ISR;
    uint32_t i;
    for (i = 0; i < rn; i++) {
        float x;
        ph += base * (1.f + pe * K.bd_amt);
        if (ph >= 1.f) ph -= 1.f;
        pe *= K.bd_dpe; ae *= K.bd_dae; ce *= K.bd_dce;
        x = sm_sin2pi(ph) + nzb[i] * ce * K.bd_clk;
        OUT(i, sm_tanh(x * K.bd_drv) * ae * g);
        if (ae < OFF) { S.on[PV_BD] = 0; break; }
    }
    S.bd_ph = ph; S.bd_pe = pe; S.bd_ae = ae; S.bd_ce = ce;
}
static void v_sd(float *mL, float *mR, float pl, float pr)
{
    float p1 = S.sd_p1, p2 = S.sd_p2, be = S.sd_be, ne = S.sd_ne, a = S.sd_a, b = S.sd_b, g = S.vel[PV_SD] * K.lev[PV_SD];
    const float f0 = K.sd_f0 * ISR;
    uint32_t i;
    for (i = 0; i < rn; i++) {
        float f = f0 * (1.f + be * be * 0.4f), body, bp;
        p1 += f;
        if (p1 >= 1.f) p1 -= 1.f;
        p2 += f * 1.63f;
        if (p2 >= 1.f) p2 -= 1.f;
        be *= K.sd_dbe; ne *= K.sd_dne;
        body = (sm_sin2pi(p1) * 0.65f + sm_sin2pi(p2) * 0.35f) * be;
        bp = svf_bp(&K.sd_f, &a, &b, nzb[i]);
        OUT(i, (body * K.sd_body + bp * ne * K.sd_nz) * g);
        if (ne < OFF && be < OFF) { S.on[PV_SD] = 0; break; }
    }
    S.sd_p1 = p1; S.sd_p2 = p2; S.sd_be = be; S.sd_ne = ne; S.sd_a = a; S.sd_b = b;
}
static void v_rs(float *mL, float *mR, float pl, float pr)
{
    float p1 = S.rs_p1, p2 = S.rs_p2, e = S.rs_e, ce = S.rs_ce, g = S.vel[PV_RS] * K.lev[PV_RS];
    uint32_t i;
    for (i = 0; i < rn; i++) {
        float x;
        p1 += K.rs_i1;
        if (p1 >= 1.f) p1 -= 1.f;
        p2 += K.rs_i2;
        if (p2 >= 1.f) p2 -= 1.f;
        e *= K.rs_de; ce *= K.rs_dce;
        x = sm_sin2pi(p1) * 0.7f + sm_sin2pi(p2) * 0.6f + nzb[i] * ce * K.rs_clk;
        OUT(i, sm_tanh(x * K.rs_drv) * e * g);
        if (e < OFF) { S.on[PV_RS] = 0; break; }
    }
    S.rs_p1 = p1; S.rs_p2 = p2; S.rs_e = e; S.rs_ce = ce;
}
static void v_cp(float *mL, float *mR, float pl, float pr)
{
    float c = S.cp_c, nb = S.cp_n, be = S.cp_be, te = S.cp_te, a = S.cp_a, b = S.cp_b, g = S.vel[PV_CP] * K.lev[PV_CP] * 2.2f;
    uint32_t i;
    for (i = 0; i < rn; i++) {
        float bp;
        if (nb < 4.f && c >= nb * K.cp_sp) { be = 1.f; nb += 1.f; if (nb > 3.f) te = 1.f; }
        c += 1.f;
        be *= K.cp_dbe; te *= K.cp_dte;
        bp = svf_bp(&K.cp_f, &a, &b, nzb[i]);
        OUT(i, bp * sm_max(be, te * 0.7f) * g);
        if (nb > 3.f && te < OFF && be < OFF) { S.on[PV_CP] = 0; break; }
    }
    S.cp_c = c; S.cp_n = nb; S.cp_be = be; S.cp_te = te; S.cp_a = a; S.cp_b = b;
}
/* fyrkantvåg: +1 första halvan av perioden, -1 andra (översta biten i Q32-fasen) */
static inline int32_t sqi(uint32_t q) { return (q & 0x80000000u) ? -1 : 1; }
static void v_hat(int h, float *mL, float *mR, float pl, float pr)
{
    uint32_t q0 = S.hat_q[h][0], q1 = S.hat_q[h][1], q2 = S.hat_q[h][2], q3 = S.hat_q[h][3], q4 = S.hat_q[h][4], q5 = S.hat_q[h][5];
    const uint32_t *inc = K.hat_inc[h];
    float e = S.hat_e[h], a = S.hat_a[h], b = S.hat_b[h], g = S.vel[PV_CH + h] * K.lev[PV_CH + h] * 1.8f;
    const float d = (h && S.oh_ck) ? K.oh_dck : K.hat_de[h], mt = 0.33f * K.hat_mt[h], nk = K.hat_nz[h];
    uint32_t i;
    for (i = 0; i < rn; i++) {
        float hp;
        int32_t m;
        q0 += inc[0]; q1 += inc[1]; q2 += inc[2]; q3 += inc[3]; q4 += inc[4]; q5 += inc[5];
        m = sqi(q0) + sqi(q1) + sqi(q2) + sqi(q3) + sqi(q4) + sqi(q5);
        hp = svf_hp(&K.hat_f[h], &a, &b, (float)m * mt + nzb[i] * nk);
        e *= d;
        OUT(i, hp * e * g);
        if (e < OFF) {                          /* tyst: oscillatorerna går vidare resten av blocket */
            const uint32_t r = rn - 1u - i;
            S.on[PV_CH + h] = 0;
            q0 += inc[0] * r; q1 += inc[1] * r; q2 += inc[2] * r; q3 += inc[3] * r; q4 += inc[4] * r; q5 += inc[5] * r;
            break;
        }
    }
    S.hat_q[h][0] = q0; S.hat_q[h][1] = q1; S.hat_q[h][2] = q2; S.hat_q[h][3] = q3; S.hat_q[h][4] = q4; S.hat_q[h][5] = q5;
    S.hat_e[h] = e; S.hat_a[h] = a; S.hat_b[h] = b;
}
static void v_ch(float *mL, float *mR, float pl, float pr) { v_hat(0, mL, mR, pl, pr); }
static void v_oh(float *mL, float *mR, float pl, float pr) { v_hat(1, mL, mR, pl, pr); }
static void v_tm(float *mL, float *mR, float pl, float pr)
{
    float ph = S.tm_ph, pe = S.tm_pe, ae = S.tm_ae, ce = S.tm_ce, g = S.vel[PV_TOM] * K.lev[PV_TOM];
    const float base = K.tm_base0 * S.tm_base * ISR;
    uint32_t i;
    for (i = 0; i < rn; i++) {
        float x;
        ph += base * (1.f + pe * K.tm_amt);
        if (ph >= 1.f) ph -= 1.f;
        pe *= K.tm_dpe; ae *= K.tm_dae; ce *= K.tm_dce;
        x = sm_sin2pi(ph) + nzb[i] * ce * 0.35f;
        OUT(i, sm_tanh(x * 1.3f) * ae * g);
        if (ae < OFF) { S.on[PV_TOM] = 0; break; }
    }
    S.tm_ph = ph; S.tm_pe = pe; S.tm_ae = ae; S.tm_ce = ce;
}
static void v_cb(float *mL, float *mR, float pl, float pr)
{
    uint32_t q1 = S.cb_q1, q2 = S.cb_q2;
    float e1 = S.cb_e1, e2 = S.cb_e2, a = S.cb_a, b = S.cb_b, g = S.vel[PV_CB] * K.lev[PV_CB] * 0.8f;
    const float d1 = K.cb_d1, d2 = K.cb_d2;
    const uint32_t i1 = K.cb_i1, i2 = K.cb_i2;
    uint32_t i;
    for (i = 0; i < rn; i++) {
        float t1, t2, t1h, t2h, s, bp;
        q1 += i1; q2 += i2;
        t1 = (float)q1 * 2.3283064e-10f;        /* fasen 0..1 */
        t2 = (float)q2 * 2.3283064e-10f;
        t1h = (float)(q1 + 0x80000000u) * 2.3283064e-10f;
        t2h = (float)(q2 + 0x80000000u) * 2.3283064e-10f;
        s = (float)(sqi(q1) + sqi(q2)) + blep(t1, d1) - blep(t1h, d1) + blep(t2, d2) - blep(t2h, d2);
        bp = svf_bp(&K.cb_f, &a, &b, s);
        e1 *= K.cb_de1; e2 *= K.cb_de2;
        OUT(i, bp * (e1 * 0.55f + e2 * 0.45f) * g);
        if (e2 < OFF) {
            const uint32_t r = rn - 1u - i;
            S.on[PV_CB] = 0;
            q1 += i1 * r; q2 += i2 * r;
            break;
        }
    }
    S.cb_q1 = q1; S.cb_q2 = q2; S.cb_e1 = e1; S.cb_e2 = e2; S.cb_a = a; S.cb_b = b;
}
typedef void (*voice_fn)(float *, float *, float, float);
static const voice_fn VOICE[PULS_NV] = {v_bd, v_sd, v_rs, v_cp, v_ch, v_oh, v_tm, v_cb};

void puls_render_begin(uint32_t n)
{
    uint32_t i, m = 0;
    int v;
    rn = n > PULS_MAXN ? PULS_MAXN : n;
    if (cver != pver) { cver = pver; coef(); }
    for (v = 0; v < PULS_NV; v++) if (req[v] != seen[v]) { seen[v] = req[v]; start(v); }
    for (i = 0; i < rn; i++) nzb[i] = sm_noise(&S.nz);
    for (i = 0; i < rn; i++) { mixb[0][0][i] = 0.f; mixb[0][1][i] = 0.f; mixb[1][0][i] = 0.f; mixb[1][1][i] = 0.f; }
    for (v = 0; v < PULS_NV; v++) if (S.on[v]) m |= 1u << v;
    S.mask = m;
}

void puls_render_voices(uint32_t first, uint32_t last, uint32_t mix)
{
    uint32_t v;
    if (mix > 1u) mix = 1u;
    for (v = first; v < last && v < PULS_NV; v++) {
        if (S.on[v]) VOICE[v](mixb[mix][0], mixb[mix][1], PAN_L[v], PAN_R[v]);
        else if (v == PV_CH || v == PV_OH) {    /* hihattarnas och koskällans oscillatorer går hela tiden */
            int h = (int)v - PV_CH, j;
            for (j = 0; j < 6; j++) S.hat_q[h][j] += K.hat_inc[h][j] * rn;
        } else if (v == PV_CB) { S.cb_q1 += K.cb_i1 * rn; S.cb_q2 += K.cb_i2 * rn; }
    }
}

void puls_render_end(float *L, float *R)
{
    uint32_t i;
    float dxL = S.dcxL, dyL = S.dcyL, dxR = S.dcxR, dyR = S.dcyR, pk = S.pk;
    for (i = 0; i < rn; i++) {
        float oL = sm_tanh((mixb[0][0][i] + mixb[1][0][i]) * K.gl) * K.outg;
        float oR = sm_tanh((mixb[0][1][i] + mixb[1][1][i]) * K.gl) * K.outg;
        dyL = oL - dxL + 0.9995f * dyL; dxL = oL;
        dyR = oR - dxR + 0.9995f * dyR; dxR = oR;
        pk = sm_max(sm_max(sm_abs(dyL), sm_abs(dyR)), pk * 0.9997f);
        L[i] = dyL;
        R[i] = dyR;
    }
    S.dcxL = dxL; S.dcyL = dyL; S.dcxR = dxR; S.dcyR = dyR; S.pk = pk;
}

void puls_render(float *L, float *R, uint32_t n)
{
    while (n) {
        uint32_t k = n > PULS_MAXN ? PULS_MAXN : n;
        puls_render_begin(k);
        puls_render_voices(0, PULS_NV, 0);
        puls_render_end(L, R);
        L += k; R += k; n -= k;
    }
}

/* ------------------------------------------------------------------ spela */
void puls_hit(int voice, float vel)
{
    if (voice < 0 || voice >= PULS_NV) return;
    req_vel[voice] = sm_clamp(vel, 0.05f, 1.f);
    req[voice]++;
}
void puls_hit_tom(float vel, int semi) { req_semi = semi; puls_hit(PV_TOM, vel); }

static const int8_t NOTE_VOICE[12] = {PV_BD, PV_RS, PV_SD, PV_CP, PV_SD, PV_TOM, PV_CH, PV_TOM, PV_CB, PV_TOM, PV_OH, PV_TOM};
static const int8_t TOM_SEMI[12] = {0, 0, 0, 0, 0, -5, 0, -2, 0, 2, 0, 5};
void puls_note_on(int note, int vel)
{
    int k = ((note % 12) + 12) % 12;
    float v = (float)vel * (1.f / 127.f);
    if (vel <= 0) return;
    if (NOTE_VOICE[k] == PV_TOM) puls_hit_tom(v, TOM_SEMI[k]);
    else puls_hit(NOTE_VOICE[k], v);
}

/* ------------------------------------------------------------------ rattar och trumset */
void puls_set(int p, float v)
{
    if (p < 0 || p >= PP_N) return;
    P[p] = sm_clamp(v, PULS_PARAMS[p].lo, PULS_PARAMS[p].hi);
    pver++;
}
float puls_get(int p) { return (p >= 0 && p < PP_N) ? P[p] : 0.f; }

void puls_load_preset(int index)
{
    float nv[PP_N];
    int i;
    const puls_preset_t *pr;
    if (index < 0 || index >= PULS_NPRESETS) return;
    pr = &PULS_PRESETS[index];
    for (i = 0; i < PP_N; i++) nv[i] = PULS_PARAMS[i].keep ? P[i] : PULS_PARAMS[i].def;
    for (i = 0; i < pr->count; i++) {
        const puls_pv_t *v = &PULS_PV[pr->first + i];
        if (!PULS_PARAMS[v->param].keep) nv[v->param] = v->value;
    }
    for (i = 0; i < PP_N; i++) puls_set(i, nv[i]);
}

uint32_t puls_meter_voices(void) { return S.mask; }
float puls_meter_peak(void) { return S.pk; }

/* ------------------------------------------------------------------ in och ur */
void puls_reset(void)
{
    static const uint32_t Q0[6] = {0u, 588410520u, 1176821039u, 1765231559u, 2353642078u, 2942052598u};   /* 0, 0.137 .. 0.685 som Q32 */
    int v, j;
    uint8_t *p = (uint8_t *)&S;
    for (v = 0; v < (int)sizeof S; v++) p[v] = 0;
    S.nz.s = SM_NOISE_SEED;
    for (j = 0; j < 6; j++) { S.hat_q[0][j] = Q0[j]; S.hat_q[1][j] = Q0[j]; }
    for (v = 0; v < PULS_NV; v++) seen[v] = req[v];
}

void puls_init(void)
{
    int i;
    for (i = 0; i < PP_N; i++) P[i] = PULS_PARAMS[i].def;
    pver++;
    puls_reset();
    puls_load_preset(0);
}
