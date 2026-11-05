/* SPDX-License-Identifier: GPL-3.0-only */
/* BLONDA BRIS som fm1-dock-modul för M-VAVE FM-1.
 *
 *   tangenter / MIDI   6 röster (poly stereo, poly, mono, unison), velocity, pitch bend, modhjul (CC1),
 *                      tryck (aftertouch), sustain (CC64), ljudbyte (program change)
 *   PRESETS            byt fabriksljud (90 st, laddas direkt)
 *   KNOB1..4           de fyra parametrarna på sidan som visas
 *   SELECT             nästa / förra sidan
 *   ALGORITHM          vågform (såg, puls, båda)
 *   SEL FILTER · EDIT OSC · ENV EG2/EG1 · LFO 1/2 · FX · GLO VOICE/PLAY   (tryck igen: sidans andra halva)
 *   SAVE               sparar ljudet som det är nu; det laddas när BRIS startar nästa gång
 * Med KNOBS CC påslaget i fm1-dock kommer rattarna som CC 74, 71, 73, 72: cutoff, resonans, EG2 attack, release. */
#include "hb_module.h"
#include "bris_fm1.h"

static const hb_host_t *H;

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
    {"VOICE", HB_BTN_GLO, {BP_lage, BP_porta, BP_uniint, BP_vol}},
    {"PLAY", HB_BTN_GLO, {BP_velf, BP_vela, BP_bendr, BP_kbda}},
};
#define NPAGES ((int)(sizeof PAGES / sizeof PAGES[0]))

/* ------------------------------------------------------------------ tillstånd */
#define SET_MAGIC 0x53495242u                   /* "BRIS" */
typedef struct {
    uint32_t magic;
    uint8_t version, preset, page, edited;
    float p[BP_N];
} settings_t;                                   /* 152 byte (värden upp till 240) */

static int preset = 1, page, edited;
static volatile uint32_t saved_ms;              /* när SAVE trycktes (meddelandet visas en stund) */
static uint32_t at_max;                          /* största tangenttrycket just nu */

/* vad som syns, för att bara rita om det som ändrats */
static struct { int preset, page, edited, saved, row[4]; uint32_t voices; int32_t val[4]; } shown;

static const uint16_t C_YEL = HB_RGB(255, 214, 0), C_YDIM = HB_RGB(110, 92, 0), C_GRAY = HB_RGB(120, 120, 120),
                      C_BAR0 = HB_RGB(48, 40, 0), C_BLACK = 0;

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

/* parameterns värde som text, och som 0..1000 för stapeln */
static int32_t value_text(int p, char *s)
{
    static const char *const VF[3] = {"SAW", "PULSE", "SAW+PULSE"};
    static const char *const LW[5] = {"TRIANGLE", "RAMP UP", "RAMP DOWN", "SQUARE", "S&H"};
    static const char *const MD[4] = {"POLY ST", "POLY", "MONO", "UNISON"};
    const bris_param_t *d = &BRIS_PARAMS[p];
    float v = bris_get(p);
    int32_t bar = (int32_t)((v - d->lo) / (d->hi - d->lo) * 1000.f + 0.5f);
    int32_t iv = (int32_t)(v < 0.f ? v - 0.5f : v + 0.5f);
    if (p == BP_vform) put_str(s, VF[iv < 0 ? 0 : iv > 2 ? 2 : iv]);
    else if (p == BP_lwave) put_str(s, LW[iv < 0 ? 0 : iv > 4 ? 4 : iv]);
    else if (p == BP_lage) put_str(s, MD[iv < 0 ? 0 : iv > 3 ? 3 : iv]);
    else if (p == BP_uniint) { if (iv > 0) *s++ = '+'; put_str(put_int(s, iv), " ST"); }
    else if (p == BP_bendr) put_str(put_int(s, iv), " ST");
    else put_int(s, (int32_t)(v * 100.f + (v < 0.f ? -0.5f : 0.5f)));   /* 0..100 (kbda -100..100) */
    return bar;
}

static void set_param(int p, float v)
{
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
static float bufL[256], bufR[256];

static void render(int32_t *out, uint32_t n)
{
    while (n) {
        uint32_t k = n > 256u ? 256u : n, i;
        bris_render(bufL, bufR, k);
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
    default: break;
    }
}

static void midi(uint32_t st, uint32_t d1, uint32_t d2)
{
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
    const bris_param_t *d = &BRIS_PARAMS[p];
    float step = d->integer ? 1.f : (d->hi - d->lo) * 0.01f;
    set_param(p, bris_get(p) + step * (float)clicks);
}

static void ui(uint32_t ev, int32_t arg)
{
    uint32_t id = HB_EV_ID(ev);
    if (HB_EV_KIND(ev) == HB_EV_ENC) {
        if (id >= HB_ENC_KNOB1 && id <= HB_ENC_KNOB4) turn(PAGES[page].p[id - HB_ENC_KNOB1], arg);
        else if (id == HB_ENC_PRESETS) load_preset(preset + arg);
        else if (id == HB_ENC_SELECT) { page = ((page + arg) % NPAGES + NPAGES) % NPAGES; leds(); }
        else if (id == HB_ENC_ALGORITHM) turn(BP_vform, arg);
        return;
    }
    if (HB_EV_KIND(ev) != HB_EV_BTN_DOWN) return;
    if (id == HB_BTN_SAVE) {
        settings_t s;
        int i;
        s.magic = SET_MAGIC; s.version = 1; s.preset = (uint8_t)preset; s.page = (uint8_t)page; s.edited = (uint8_t)edited;
        for (i = 0; i < BP_N; i++) s.p[i] = bris_get(i);
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
    H->text(8, y, 142, 0, BRIS_PARAMS[p].name, C_GRAY, 0);
    H->fill(150, y, 82, 16, C_BLACK);           /* värdet högerställt */
    H->text(232 - tw, y, tw, 0, s, C_YEL, 0);
    w = bar * 224 / 1000;
    w = w < 0 ? 0 : w > 224 ? 224 : w;
    if (w) H->fill(8, y + 20, w, 5, C_YEL);
    if (w < 224) H->fill(8 + w, y + 20, 224 - w, 5, C_BAR0);
}

static void draw(uint32_t full)
{
    int r;
    uint32_t vm = bris_meter_voices();
    int saving = saved_ms && H->ms() - saved_ms < 1500u;
    if (full) {
        H->fill(0, HB_TOP, 240, 240 - HB_TOP, C_BLACK);
        shown.preset = shown.page = shown.edited = shown.saved = -1;
        shown.voices = ~0u;
    }
    if (preset != shown.preset || edited != shown.edited) {
        char s[40], *e;
        const char *name = BRIS_PRESETS[preset].name;
        e = put_int(s, preset);
        if (preset < 10) { s[1] = s[0]; s[0] = '0'; s[2] = 0; e = s + 2; }
        H->text(8, 28, 40, 0, s, C_YDIM, 0);
        H->text(48, 28, 184, 0, preset == 0 ? "INIT" : preset <= BRIS_FACTORY ? "BRIS FACTORY" : "BLONDA", C_YDIM, 0);
        e = put_str(s, name);
        if (edited) put_str(e, "*");
        H->fill(8, 44, 224, 32, C_BLACK);
        if (H->text_w(1, s) <= 224) H->text(8, 44, 224, 1, s, C_YEL, 0);
        else H->text(8, 52, 224, 0, s, C_YEL, 0);
        shown.preset = preset; shown.edited = edited;
    }
    if (page != shown.page || saving != shown.saved) {
        char s[24];
        int i, n = 0, k = 0;
        for (i = 0; i < NPAGES; i++) if (PAGES[i].btn == PAGES[page].btn) { n++; if (i <= page) k = n; }
        if (saving) put_str(s, "SAVED");
        else if (n > 1) { char *e = put_str(s, PAGES[page].title); e = put_str(e, "  "); e = put_int(e, k); e = put_str(e, "/"); put_int(e, n); }
        else put_str(s, PAGES[page].title);
        H->text(8, 82, 150, 0, s, saving ? C_YEL : C_GRAY, 0);
        H->fill(8, 102, 224, 1, C_YDIM);
        for (r = 0; r < 4; r++) shown.row[r] = -1;
        shown.page = page; shown.saved = saving;
    }
    for (r = 0; r < 4; r++) {
        int p = PAGES[page].p[r];
        int32_t v = (int32_t)(bris_get(p) * 1000.f);
        if (shown.row[r] != p || shown.val[r] != v) { draw_row(r); shown.row[r] = p; shown.val[r] = v; }
    }
    if (vm != shown.voices) {                   /* sex prickar: rösterna som låter */
        for (r = 0; r < BRIS_NV; r++) H->fill(170 + r * 11, 86, 8, 8, vm >> r & 1u ? C_YEL : C_BAR0);
        shown.voices = vm;
    }
}

/* ------------------------------------------------------------------ in och ur */
static int init(const hb_host_t *h)
{
    settings_t s;
    H = h;
    saved_ms = 0;
    bris_init();
    if (H->load(&s, sizeof s) == 0 && s.magic == SET_MAGIC && s.version == 1 && s.preset < BRIS_NPRESETS) {
        int i;
        preset = s.preset;
        page = s.page < NPAGES ? s.page : 0;
        for (i = 0; i < BP_N; i++) bris_set(i, s.p[i]);
        edited = s.edited;
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
}

HB_DESC = {HB_ABI, "BRIS", init, release, note, cc, render, ui, draw, 0, 0, 0, midi};
