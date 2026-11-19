/* SPDX-License-Identifier: GPL-3.0-only */
/* BLONDA BRIS som fm1-dock-modul för M-VAVE FM-1.
 *
 *   tangenter / MIDI   6 röster (poly stereo, poly, mono, unison), velocity, pitch bend, modhjul (CC1),
 *                      tryck (aftertouch), sustain (CC64), ljudbyte (program change)
 *   PRESETS            byt fabriksljud (90 st, laddas direkt)
 *   KNOB1..4           de fyra parametrarna på sidan som visas
 *   SELECT             nästa / förra sidan
 *   ALGORITHM          ERA: inbyggt band och lofi efter BRIS. 0 MODERN (rent, som BRIS), 1..60 TAPE (kassett som
 *                      slits mer och mer), 61..100 LOFI (digital: färre bitar och lägre samplingsfrekvens). MIDI CC 77.
 *   SEL FILTER · EDIT OSC · ENV EG2/EG1 · LFO 1/2 · FX · GLO VOICE/PLAY   (tryck igen: sidans nästa del)
 *   FX                 FX (ensemble, eko) · RUM, RUM TONE, RUM SPACE (reverbet BLONDA RUM efter BRIS) · PUMP
 *                      (ducking i takt: klangen eller allt viker undan på slaget; egen takt eller MIDI-klocka).
 *                      Reverb och pump hör till modulen, inte till ljudet: de ligger kvar när man byter ljud.
 *                      MIDI CC 91 = reverbets mix, CC 92 = pumpens djup.
 *   SAVE               sparar ljudet som det är nu, med ERA, reverb och pump; det laddas när BRIS startar nästa gång
 * Med KNOBS CC påslaget i fm1-dock kommer rattarna som CC 74, 71, 73, 72: cutoff, resonans, EG2 attack, release. */
#include "hb_module.h"
#include "bris_fm1.h"
#include "sliten_fm1.h"
#include "rum_fm1.h"
#include "pump_fm1.h"

#ifndef HB_F_MIDI_RT
#define HB_F_MIDI_RT 16u                        /* nyare fm1-dock: MIDI-klockan kommer i ljudavbrottet (annars huvudloopen) */
#endif
#define BRIS_VERSION "V3.0"                     /* visas på skärmen; höj när modulen ändras (module.cfg: version) */

static const hb_host_t *H;
static char *put_int(char *s, int32_t v);
static char *put_str(char *s, const char *a);
static float clampf(float x, float lo, float hi);

/* ------------------------------------------------------------------ modulens egna effekter efter BRIS: RUM och PUMP */
/* numreras efter BRIS parametrar, så att sidorna kan blanda dem */
enum { XP_rmix = BP_N, XP_rsize, XP_rdecay, XP_rtype, XP_rlowc, XP_rhighc, XP_rdamp, XP_rmodd,
       XP_rpredel, XP_rwidth, XP_rduck, XP_rout, XP_pdepth, XP_prate, XP_ptarget, XP_bpm, XP_END };
#define XN (XP_END - BP_N)
typedef struct { const char *name; float lo, hi, def; uint8_t integer; int8_t rum; } xparam_t;
static const xparam_t XPAR[XN] = {
    {"Reverb Mix", 0.f, 1.f, 0.f, 0, RU_wet}, {"Reverb Size", 0.f, 1.f, 0.55f, 0, RU_size},
    {"Reverb Decay", 0.f, 1.f, 0.5f, 0, RU_decay}, {"Reverb Type", 0.f, 1.f, 0.f, 1, RU_shape},
    {"Low Cut", 0.f, 1.f, 0.25f, 0, RU_lowc}, {"High Cut", 0.f, 1.f, 1.f, 0, RU_highc},
    {"Damping", 0.f, 1.f, 0.45f, 0, RU_damp}, {"Movement", 0.f, 1.f, 0.35f, 0, RU_modd},
    {"Predelay", 0.f, 1.f, 0.15f, 0, RU_predel}, {"Width", 0.f, 1.f, 1.f, 0, RU_width},
    {"Duck", 0.f, 1.f, 0.f, 0, RU_duck}, {"Reverb Level", 0.f, 1.f, 0.75f, 0, RU_outv},
    {"Pump Depth", 0.f, 1.f, 0.f, 0, -1}, {"Pump Rate", 0.f, 4.f, 2.f, 1, -1},
    {"Pump Target", 0.f, 1.f, 0.f, 1, -1}, {"Tempo", 40.f, 240.f, 120.f, 1, -1},
};
static float xv[XN];
static rum_t rum;
static pump_t pump;
static volatile int rum_on;                     /* reverbet räknas (mix över 0) */

/* ------------------------------------------------------------------ sidor */
typedef struct { const char *title; uint8_t btn; int8_t p[4]; } page_t;
static const page_t PAGES[] = {
    {"FILTER", HB_BTN_SEL, {BP_cut, BP_res, BP_eg1, BP_kbd}},
    {"OSC", HB_BTN_EDIT, {BP_vform, BP_sub, BP_pw, BP_detune}},
    {"EG2 AMP", HB_BTN_ENV, {BP_aa, BP_ad, BP_asus, BP_ar}},
    {"EG1 FILTER", HB_BTN_ENV, {BP_fa, BP_fd, BP_fs, BP_fr}},
    {"LFO", HB_BTN_LFO, {BP_lwave, BP_lrate, BP_lfoosc, BP_lfocut}},
    {"LFO MOD", HB_BTN_LFO, {BP_lfopwm, BP_lfoamp, BP_mwosc, BP_mwcut}},
    {"FX", HB_BTN_FX, {BP_chorus, BP_dtime, BP_dfb, BP_dmix}},
    {"RUM", HB_BTN_FX, {XP_rmix, XP_rsize, XP_rdecay, XP_rtype}},
    {"RUM TONE", HB_BTN_FX, {XP_rlowc, XP_rhighc, XP_rdamp, XP_rmodd}},
    {"RUM SPACE", HB_BTN_FX, {XP_rpredel, XP_rwidth, XP_rduck, XP_rout}},
    {"PUMP", HB_BTN_FX, {XP_pdepth, XP_prate, XP_ptarget, XP_bpm}},
    {"VOICE", HB_BTN_GLO, {BP_lage, BP_porta, BP_uniint, BP_vol}},
    {"PLAY", HB_BTN_GLO, {BP_velf, BP_vela, BP_bendr, BP_kbda}},
};
#define NPAGES ((int)(sizeof PAGES / sizeof PAGES[0]))

/* ------------------------------------------------------------------ tillstånd */
#define SET_MAGIC 0x53495242u                   /* "BRIS" */
#define SET_VERSION 3                           /* 2: era, 3: reverb och pump */
typedef struct {
    uint32_t magic;
    uint8_t version, preset, page, edited;
    float p[BP_N];
    float era;
} settings2_t;                                  /* version 2: 156 byte */
typedef struct {
    uint32_t magic;
    uint8_t version, preset, page, edited;
    float p[BP_N];
    float era;
    uint8_t x[XN];                              /* reverb och pump: 0..100 (heltal och tempo som de är) */
} settings_t;                                   /* 172 byte (fm1-dock tar upp till 240) */
typedef char settings_fits[sizeof(settings_t) <= 240 ? 1 : -1];

static int preset = 1, page, edited;
static volatile uint32_t saved_ms;              /* när SAVE trycktes (meddelandet visas en stund) */
static uint32_t at_max;                          /* största tangenttrycket just nu */

/* vad som syns, för att bara rita om det som ändrats */
static struct { int preset, page, edited, saved, era, row[4]; uint32_t voices; int32_t val[4]; } shown;

/* skärmens färger: varmt elfenben, orange och petrol på svart, som gamla instrumentpaneler */
static const uint16_t C_NAME = HB_RGB(236, 230, 214),    /* ljudets namn */
                      C_VAL = HB_RGB(242, 106, 44),      /* värden, staplar, rösterna */
                      C_LBL = HB_RGB(158, 150, 132),     /* parametrarnas namn */
                      C_HEAD = HB_RGB(52, 150, 144),     /* nummer, bank, sidan */
                      C_TRACK = HB_RGB(56, 40, 30),      /* staplarnas spår */
                      C_RULE = HB_RGB(78, 70, 58),       /* linjen */
                      C_BLACK = 0;

/* ------------------------------------------------------------------ ERA: band och lofi efter BRIS */
static sliten_t tape;
static volatile int32_t era;                    /* 0..100, ALGORITHM-ratten */
static int tape_on;                             /* SLITEN räknar (även medan den tonar ut) */

/* ratten till SLITEN:s parametrar (BLONDA SLITEN i blonda-motor). Inga avbrott: de digitala mediernas
 * upprepning behöver en längre buffert än modulen har plats för. */
static void era_apply(void)
{
    int32_t e = era;
    if (e <= 0) { sliten_set(&tape, SL_wet, 0.f); return; }
    if (e <= 60) {                              /* TAPE: kassett som slits mer och mer */
        float t = (float)e / 60.f;
        sliten_set(&tape, SL_media, 0.f);
        sliten_set(&tape, SL_age, 0.2f + 0.8f * t);
        sliten_set(&tape, SL_wow, 0.35f);
        sliten_set(&tape, SL_flut, 0.3f);
        sliten_set(&tape, SL_sat, 0.3f + 0.4f * t);
        sliten_set(&tape, SL_brus, 0.15f + 0.35f * t);
        sliten_set(&tape, SL_avbr, 0.15f * t * t);
        sliten_set(&tape, SL_ton, 0.62f - 0.25f * t);
    } else {                                    /* LOFI: digitalt, färre bitar och lägre samplingsfrekvens */
        float t = (float)(e - 60) / 40.f;
        sliten_set(&tape, SL_media, 2.f);
        sliten_set(&tape, SL_age, 0.35f + 0.65f * t);
        sliten_set(&tape, SL_sat, 0.4f);
        sliten_set(&tape, SL_avbr, 0.f);
        sliten_set(&tape, SL_ton, 0.5f - 0.25f * t);
    }
    sliten_set(&tape, SL_bredd, 1.f);
    sliten_set(&tape, SL_outv, 0.75f);
    sliten_set(&tape, SL_wet, 1.f);
}
static void set_era(int32_t e)
{
    era = e < 0 ? 0 : e > 100 ? 100 : e;
    if (era > 0 && !tape_on) { sliten_reset(&tape); tape.sWet = 0.f; }   /* tonar in från rent */
    era_apply();
    if (era > 0) tape_on = 1;
}
static char *era_text(char *s)
{
    if (era <= 0) return put_str(s, "MODERN");
    return put_int(put_str(s, era <= 60 ? "TAPE " : "LOFI "), era);
}

/* ------------------------------------------------------------------ hjälp */
static float clampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }

static char *put_int(char *s, int32_t v)
{
    char t[12];
    int n = 0;
    if (v < 0) { *s++ = '-'; v = -v; }
    do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v && n < 11);
    while (n) *s++ = t[--n];
    *s = 0;
    return s;
}
static char *put_str(char *s, const char *a) { while (*a) *s++ = *a++; *s = 0; return s; }

/* en parameter på en sida: BRIS egen (under BP_N) eller modulens (RUM, PUMP) */
static float par_get(int p) { return p < BP_N ? bris_get(p) : xv[p - BP_N]; }
static const char *par_name(int p) { return p < BP_N ? BRIS_PARAMS[p].name : XPAR[p - BP_N].name; }
static float par_lo(int p) { return p < BP_N ? BRIS_PARAMS[p].lo : XPAR[p - BP_N].lo; }
static float par_hi(int p) { return p < BP_N ? BRIS_PARAMS[p].hi : XPAR[p - BP_N].hi; }
static int par_integer(int p) { return p < BP_N ? BRIS_PARAMS[p].integer : XPAR[p - BP_N].integer; }

/* takten som visas: klockans när den kommer, annars den egna */
static int32_t tempo_shown(void) { return (int32_t)(pump_tempo(&pump) + 0.5f); }

/* parameterns värde som text, och som 0..1000 för stapeln */
static int32_t value_text(int p, char *s)
{
    static const char *const VF[3] = {"SAW", "PULSE", "SAW+PULSE"};
    static const char *const LW[5] = {"TRIANGLE", "RAMP UP", "RAMP DOWN", "SQUARE", "S&H"};
    static const char *const MD[4] = {"POLY ST", "POLY", "MONO", "UNISON"};
    static const char *const PR[5] = {"1/1", "1/2", "1/4", "1/8", "1/16"};
    float v = par_get(p), lo = par_lo(p), hi = par_hi(p);
    int32_t bar = (int32_t)((v - lo) / (hi - lo) * 1000.f + 0.5f);
    int32_t iv = (int32_t)(v < 0.f ? v - 0.5f : v + 0.5f);
    if (p == BP_vform) put_str(s, VF[iv < 0 ? 0 : iv > 2 ? 2 : iv]);
    else if (p == BP_lwave) put_str(s, LW[iv < 0 ? 0 : iv > 4 ? 4 : iv]);
    else if (p == BP_lage) put_str(s, MD[iv < 0 ? 0 : iv > 3 ? 3 : iv]);
    else if (p == BP_uniint) { if (iv > 0) *s++ = '+'; put_str(put_int(s, iv), " ST"); }
    else if (p == BP_bendr) put_str(put_int(s, iv), " ST");
    else if (p == XP_rtype) put_str(s, iv ? "ROOM" : "HALL");
    else if (p == XP_prate) put_str(s, PR[iv < 0 ? 0 : iv > 4 ? 4 : iv]);
    else if (p == XP_ptarget) put_str(s, iv ? "ALL" : "REVERB");
    else if (p == XP_bpm) {                     /* med MIDI-klocka: klockans takt */
        if (pump.ext) { put_int(put_str(s, "EXT "), tempo_shown()); bar = (tempo_shown() - 40) * 1000 / 200; }
        else put_str(put_int(s, iv), " BPM");
    }
    else put_int(s, (int32_t)(v * 100.f + (v < 0.f ? -0.5f : 0.5f)));   /* 0..100 (kbda -100..100) */
    return bar;
}

/* modulens egna effekter: värdet till reverbet eller pumpen. Reverbet nollställs när det slås på (mix från 0). */
static void set_x(int p, float v)
{
    const xparam_t *d = &XPAR[p - BP_N];
    v = clampf(v, d->lo, d->hi);
    if (d->integer) v = (float)(int32_t)(v + 0.5f);
    xv[p - BP_N] = v;
    if (d->rum >= 0) rum_set(&rum, d->rum, v);
    if (p == XP_rmix) {
        if (v > 0.f && !rum_on) { rum_reset(&rum); rum_on = 1; }
        else if (v <= 0.f) rum_on = 0;
    }
    else if (p == XP_pdepth) pump.depth = v;
    else if (p == XP_prate) pump.rate = (int32_t)v;
    else if (p == XP_bpm) pump.bpm = v;
}

static void set_param(int p, float v)
{
    if (p >= BP_N) { set_x(p, v); return; }     /* hör till modulen, inte till ljudet: ingen stjärna */
    bris_set(p, v);
    edited = 1;
}

static void load_preset(int i)
{
    preset = (i % BRIS_NPRESETS + BRIS_NPRESETS) % BRIS_NPRESETS;
    bris_load_preset(preset);
    edited = 0;
}

static void leds(void)
{
    static const uint8_t PAGE_BTNS[] = {HB_BTN_SEL, HB_BTN_EDIT, HB_BTN_ENV, HB_BTN_LFO, HB_BTN_FX, HB_BTN_GLO};
    unsigned i;
    for (i = 0; i < sizeof PAGE_BTNS; i++) H->led(PAGE_BTNS[i], PAGE_BTNS[i] == PAGES[page].btn ? 1u : 2u);
    H->led(HB_BTN_SAVE, 2u);
}

/* ------------------------------------------------------------------ ljud */
static float bufL[BRIS_MAXN], bufR[BRIS_MAXN];

/* Den andra kärnan, när fm1-dock lånar ut den (c1_run), annars allt på kärna 0:
 *   röst 3..5 medan kärna 0 räknar röst 0..2,
 *   sedan reverbets klang medan kärna 0 räknar ensemble och eko.
 * Klangen för ett block räknas ur ljudet före blocket (rum_tail med min_delay = blockets längd), så den kan gå
 * samtidigt. Det gör en predelay kortare än ett block (5,8 ms) lika lång som blocket; längre är exakt. */
static int c1_ok;
static float wetL[BRIS_MAXN], wetR[BRIS_MAXN], pgain[BRIS_MAXN];
static void c1_voices(uint32_t arg) { (void)arg; bris_render_voices(3, BRIS_NV, 1); }
static void c1_tail(uint32_t k) { rum_tail(&rum, wetL, wetR, k, k); }

static void render(int32_t *out, uint32_t n)
{
    pump_begin(&pump, H->ticks());
    while (n) {
        uint32_t k = n > BRIS_MAXN ? BRIS_MAXN : n, i;
        int two, rv = rum_on, tail_out = 0, pumping, target = (int)xv[XP_ptarget - BP_N];
        bris_render_begin(k);
        two = c1_ok && H->c1_run(c1_voices, 0);
        bris_render_voices(0, two ? 3 : BRIS_NV, 0);
        if (two && H->c1_wait() != 0) { c1_voices(0); two = 0; }   /* kärna 1 blev inte klar (och är tagen nu): räkna här */
        if (rv) {
            if (two && H->c1_run(c1_tail, k)) tail_out = 1;
            else c1_tail(k);
        }
        bris_render_end(bufL, bufR);
        pumping = pump.depth > 0.f || pump.g < 0.9999f;
        pump_gain(&pump, pgain, k);
        if (rv) {
            if (tail_out && H->c1_wait() != 0) {                    /* klangen blev inte klar: tyst klang i det här blocket */
                for (i = 0; i < k; i++) wetL[i] = wetR[i] = 0.f;
                rum_resync(&rum);
            }
            if (pumping && target == 0) for (i = 0; i < k; i++) { wetL[i] *= pgain[i]; wetR[i] *= pgain[i]; }
            rum_feed(&rum, bufL, bufR, k);
            rum_mix(&rum, bufL, bufR, wetL, wetR, k);
        }
        if (pumping && target != 0) for (i = 0; i < k; i++) { bufL[i] *= pgain[i]; bufR[i] *= pgain[i]; }
        if (tape_on) {
            sliten_process(&tape, bufL, bufR, k);
            if (era <= 0 && tape.sWet < 0.0005f) tape_on = 0;   /* helt uttonad: räkna inte längre */
        }
        for (i = 0; i < k; i++) {
            float l = clampf(bufL[i], -1.f, 1.f) * 32767.f, r = clampf(bufR[i], -1.f, 1.f) * 32767.f;
            out[2u * i] += (int32_t)l;
            out[2u * i + 1u] += (int32_t)r;
        }
        out += 2u * k;
        n -= k;
    }
}

/* ------------------------------------------------------------------ MIDI */
static void note(uint32_t ch, uint32_t key, uint32_t vel)
{
    (void)ch;
    if (key > 127u) return;
    if (vel) bris_note_on((int)key, (int)(vel > 127u ? 127u : vel));
    else bris_note_off((int)key);
}

static void cc(uint32_t ch, uint32_t num, uint32_t val)
{
    float v = (float)val / 127.f;
    (void)ch;
    switch (num) {
    case 1: bris_modwheel(v); break;
    case 64: bris_sustain(val >= 64u); break;
    case 120: case 123: bris_all_notes_off(); break;
    case 74: set_param(BP_cut, v); break;       /* fm1-dock:s KNOBS CC */
    case 71: set_param(BP_res, v); break;
    case 73: set_param(BP_aa, v); break;
    case 72: set_param(BP_ar, v); break;
    case 77: set_era((int32_t)(val * 100u + 63u) / 127); break;   /* ERA */
    case 91: set_x(XP_rmix, v); break;          /* reverbets mix */
    case 92: set_x(XP_pdepth, v); break;        /* pumpens djup */
    default: break;
    }
}

static void midi(uint32_t st, uint32_t d1, uint32_t d2)
{
    if (st >= 0xF8u) { pump_midi(&pump, st, H->ticks()); return; }   /* klocka och transport: pumpen följer */
    switch (st & 0xF0u) {
    case 0x90: note(st & 15u, d1, d2); break;
    case 0x80: note(st & 15u, d1, 0); break;
    case 0xB0: cc(st & 15u, d1, d2); break;
    case 0xC0: load_preset((int)d1); break;
    case 0xD0: bris_aftertouch((float)d1 / 127.f); break;
    case 0xA0:                                  /* tryck per tangent (FM-1:ans tangenter): det största gäller */
        at_max = d2 > at_max ? d2 : (d2 == 0u ? 0u : at_max);
        bris_aftertouch((float)at_max / 127.f);
        break;
    case 0xE0: bris_bend((float)((int32_t)((d2 << 7) | d1) - 8192) / 8192.f); break;
    default: break;
    }
}

/* ------------------------------------------------------------------ panelen */
static void turn(int p, int32_t clicks)
{
    float step = par_integer(p) ? 1.f : (par_hi(p) - par_lo(p)) * 0.01f;
    set_param(p, par_get(p) + step * (float)clicks);
}

static void ui(uint32_t ev, int32_t arg)
{
    uint32_t id = HB_EV_ID(ev);
    if (HB_EV_KIND(ev) == HB_EV_ENC) {
        if (id >= HB_ENC_KNOB1 && id <= HB_ENC_KNOB4) turn(PAGES[page].p[id - HB_ENC_KNOB1], arg);
        else if (id == HB_ENC_PRESETS) load_preset(preset + arg);
        else if (id == HB_ENC_SELECT) { page = ((page + arg) % NPAGES + NPAGES) % NPAGES; leds(); }
        else if (id == HB_ENC_ALGORITHM) set_era(era + arg);
        return;
    }
    if (HB_EV_KIND(ev) != HB_EV_BTN_DOWN) return;
    if (id == HB_BTN_SAVE) {
        settings_t s;
        int i;
        s.magic = SET_MAGIC; s.version = SET_VERSION; s.preset = (uint8_t)preset; s.page = (uint8_t)page; s.edited = (uint8_t)edited;
        for (i = 0; i < BP_N; i++) s.p[i] = bris_get(i);
        s.era = (float)era;
        for (i = 0; i < XN; i++) s.x[i] = (uint8_t)(XPAR[i].integer ? xv[i] + 0.5f : xv[i] * 100.f + 0.5f);
        H->save(&s, sizeof s);
        saved_ms = H->ms() | 1u;
        return;
    }
    {   /* sidknapparna: första trycket öppnar sidan, nästa växlar till sidans andra halva */
        int i, first = -1, next = -1;
        for (i = 0; i < NPAGES; i++)
            if (PAGES[i].btn == id) {
                if (first < 0) first = i;
                if (i > page && next < 0 && PAGES[page].btn == id) next = i;
            }
        if (first < 0) return;
        page = next >= 0 ? next : first;
        leds();
    }
}

static void draw_row(int r)
{
    int p = PAGES[page].p[r];
    int32_t y = 110 + r * 32, bar, w;
    char s[24];
    bar = value_text(p, s);

    int32_t tw = H->text_w(0, s);
    tw = tw > 90 ? 90 : tw;
    H->text(8, y, 142, 0, par_name(p), C_LBL, 0);
    H->fill(150, y, 82, 16, C_BLACK);           /* värdet högerställt */
    H->text(232 - tw, y, tw, 0, s, C_VAL, 0);
    w = bar * 224 / 1000;
    w = w < 0 ? 0 : w > 224 ? 224 : w;
    if (w) H->fill(8, y + 20, w, 5, C_VAL);
    if (w < 224) H->fill(8 + w, y + 20, 224 - w, 5, C_TRACK);
}

static void draw(uint32_t full)
{
    int r;
    uint32_t vm = bris_meter_voices();
    int saving = saved_ms && H->ms() - saved_ms < 1500u;
    if (full) {
        H->fill(0, HB_TOP, 240, 240 - HB_TOP, C_BLACK);
        shown.preset = shown.page = shown.edited = shown.saved = shown.era = -1;
        shown.voices = ~0u;
    }
    if (preset != shown.preset || edited != shown.edited) {
        char s[40], *e;
        const char *name = BRIS_PRESETS[preset].name;
        e = put_int(s, preset);
        if (preset < 10) { s[1] = s[0]; s[0] = '0'; s[2] = 0; e = s + 2; }
        H->text(8, 28, 40, 0, s, C_HEAD, 0);
        H->fill(48, 28, 112, 16, C_BLACK);
        H->text(48, 28, 112, 0, preset == 0 ? "INIT " BRIS_VERSION : preset <= BRIS_FACTORY ? "FACTORY " BRIS_VERSION : "BLONDA " BRIS_VERSION, C_HEAD, 0);
        e = put_str(s, name);
        if (edited) put_str(e, "*");
        H->fill(8, 44, 224, 32, C_BLACK);
        if (H->text_w(1, s) <= 224) H->text(8, 44, 224, 1, s, C_NAME, 0);
        else H->text(8, 52, 224, 0, s, C_NAME, 0);
        shown.preset = preset; shown.edited = edited;
    }
    if (page != shown.page || saving != shown.saved) {
        char s[24];
        int i, n = 0, k = 0;
        for (i = 0; i < NPAGES; i++) if (PAGES[i].btn == PAGES[page].btn) { n++; if (i <= page) k = n; }
        if (saving) put_str(s, "SAVED");
        else if (n > 1) { char *e = put_str(s, PAGES[page].title); e = put_str(e, "  "); e = put_int(e, k); e = put_str(e, "/"); put_int(e, n); }
        else put_str(s, PAGES[page].title);
        H->text(8, 82, 150, 0, s, saving ? C_VAL : C_HEAD, 0);
        H->fill(8, 102, 224, 1, C_RULE);
        for (r = 0; r < 4; r++) shown.row[r] = -1;
        shown.page = page; shown.saved = saving;
    }
    for (r = 0; r < 4; r++) {
        int p = PAGES[page].p[r];
        int32_t v = p == XP_bpm ? tempo_shown() + (pump.ext ? 1000 : 0) : (int32_t)(par_get(p) * 1000.f);
        if (shown.row[r] != p || shown.val[r] != v) { draw_row(r); shown.row[r] = p; shown.val[r] = v; }
    }
    if (vm != shown.voices) {                   /* sex prickar: rösterna som låter */
        for (r = 0; r < BRIS_NV; r++) H->fill(170 + r * 11, 86, 8, 8, vm >> r & 1u ? C_VAL : C_TRACK);
        shown.voices = vm;
    }
    if (era != shown.era) {                     /* ERA uppe till höger: MODERN, TAPE nn, LOFI nn */
        char s[16];
        int32_t tw;
        era_text(s);
        tw = H->text_w(0, s);
        H->fill(160, 28, 72, 16, C_BLACK);
        H->text(232 - tw, 28, tw, 0, s, era > 0 ? C_VAL : C_LBL, 0);
        shown.era = era;
    }
}

/* ------------------------------------------------------------------ in och ur */
static int init(const hb_host_t *h)
{
    settings_t s;
    int i, ok;
    H = h;
    saved_ms = 0;
    c1_ok = HB_HOST_HAS(H, c1_wait);
    bris_init();
    sliten_init(&tape);
    era = 0; tape_on = 0;
    era_apply();
    rum_init(&rum);
    pump_init(&pump);
    rum_on = 0;
    for (i = 0; i < XN; i++) set_x(BP_N + i, XPAR[i].def);
    /* sparat: version 3, eller version 2 (före reverb och pump: de får sina standardvärden) */
    ok = H->load(&s, sizeof s) == 0 && s.magic == SET_MAGIC && s.version == SET_VERSION;
    if (!ok && H->load(&s, sizeof(settings2_t)) == 0 && s.magic == SET_MAGIC && s.version == 2) {
        ok = 1;
        for (i = 0; i < XN; i++) s.x[i] = (uint8_t)(XPAR[i].integer ? XPAR[i].def + 0.5f : XPAR[i].def * 100.f + 0.5f);
    }
    if (ok && s.preset < BRIS_NPRESETS) {
        preset = s.preset;
        page = s.page < NPAGES ? s.page : 0;
        for (i = 0; i < BP_N; i++) bris_set(i, s.p[i]);
        edited = s.edited;
        set_era((int32_t)s.era);
        for (i = 0; i < XN; i++) set_x(BP_N + i, XPAR[i].integer ? (float)s.x[i] : (float)s.x[i] * 0.01f);
    } else {
        load_preset(1);                         /* Poly Magic */
    }
    leds();
    H->log("bris: redo");
    return 0;
}

static void release(void)
{
    bris_all_notes_off();
    bris_reset();
    sliten_reset(&tape);
    rum_on = 0;
}

HB_DESC = {HB_ABI, "BRIS", init, release, note, cc, render, ui, draw, 0, 0, HB_F_MIDI_RT, midi};
