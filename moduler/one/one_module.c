/* SPDX-License-Identifier: GPL-3.0-only */
/* BLONDA ONE som fm1-dock-modul för M-VAVE FM-1: BRIS möter Moog.
 *
 *   tangenter / MIDI   6 röster (poly stereo, poly, mono, unison), velocity, pitch bend, modhjul (CC1),
 *                      tryck (aftertouch), sustain (CC64), ljudbyte (program change)
 *   PRESETS            byt fabriksljud (laddas direkt)
 *   KNOB1..4           de fyra parametrarna på sidan som visas
 *   SELECT             nästa / förra sidan
 *   ALGORITHM          ERA: inbyggt band och lofi efter ONE, som i BRIS. 0 MODERN (rent), 1..60 TAPE (kassett som
 *                      slits mer och mer), 61..100 LOFI (digital: färre bitar och lägre samplingsfrekvens). MIDI CC 77.
 *   SEL FILTER/KEYS · EDIT OSC 1/OSC 2/OSC 3/MIX · ENV EG2/EG1 · LFO 1/2 · FX · GLO VOICE   (tryck igen: nästa sida)
 *   SAVE               sparar ljudet som det är nu; det laddas när ONE startar nästa gång
 * Med KNOBS CC påslaget i fm1-dock kommer rattarna som CC 74, 71, 73, 72: cutoff, resonans, EG2 attack, release. */
#include "hb_module.h"
#include "one_fm1.h"
#include "sliten_fm1.h"

static const hb_host_t *H;
static char *put_int(char *s, int32_t v);
static char *put_str(char *s, const char *a);

/* ------------------------------------------------------------------ sidor */
typedef struct { const char *title; uint8_t btn; int8_t p[4]; } page_t;
static const page_t PAGES[] = {
    {"FILTER", HB_BTN_SEL, {OP_cut, OP_res, OP_eg1, OP_drv}},
    {"KEYS", HB_BTN_SEL, {OP_kbd, OP_velf, OP_vela, OP_bendr}},
    {"OSC 1", HB_BTN_EDIT, {OP_o1w, OP_o1r, OP_o1l, OP_pw}},
    {"OSC 2", HB_BTN_EDIT, {OP_o2w, OP_o2r, OP_o2f, OP_o2l}},
    {"OSC 3", HB_BTN_EDIT, {OP_o3w, OP_o3r, OP_o3f, OP_o3l}},
    {"MIX", HB_BTN_EDIT, {OP_o2s, OP_o3s, OP_noi, OP_detune}},
    {"EG2 AMP", HB_BTN_ENV, {OP_aa, OP_ad, OP_asus, OP_ar}},
    {"EG1 FILTER", HB_BTN_ENV, {OP_fa, OP_fd, OP_fs, OP_fr}},
    {"LFO", HB_BTN_LFO, {OP_lwave, OP_lrate, OP_lfoosc, OP_lfocut}},
    {"LFO MOD", HB_BTN_LFO, {OP_lfopwm, OP_lfoamp, OP_mwosc, OP_mwcut}},
    {"FX", HB_BTN_FX, {OP_chorus, OP_dtime, OP_dfb, OP_dmix}},
    {"VOICE", HB_BTN_GLO, {OP_lage, OP_porta, OP_uniint, OP_vol}},
};
#define NPAGES ((int)(sizeof PAGES / sizeof PAGES[0]))

/* ------------------------------------------------------------------ tillstånd */
#define SET_MAGIC 0x31454E4Fu                   /* "ONE1" */
#define SET_VERSION 1
#define ONE_VERSION "V1.0"                      /* visas på skärmen; höj när modulen ändras (module.cfg: version) */
typedef struct {
    uint32_t magic;
    uint8_t version, preset, page, edited;
    float p[OP_N];
    float era;
} settings_t;                                   /* 204 byte (fm1-dock tar upp till 240) */
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

/* ------------------------------------------------------------------ ERA: band och lofi efter ONE (som i BRIS) */
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

/* parameterns värde som text, och som 0..1000 för stapeln */
static int32_t value_text(int p, char *s)
{
    static const char *const WF[3] = {"TRIANGLE", "SAW", "PULSE"};
    static const char *const RG[5] = {"32'", "16'", "8'", "4'", "2'"};
    static const char *const LW[5] = {"TRIANGLE", "RAMP UP", "RAMP DOWN", "SQUARE", "S&H"};
    static const char *const MD[4] = {"POLY ST", "POLY", "MONO", "UNISON"};
    const one_param_t *d = &ONE_PARAMS[p];
    float v = one_get(p);
    int32_t bar = (int32_t)((v - d->lo) / (d->hi - d->lo) * 1000.f + 0.5f);
    int32_t iv = (int32_t)(v < 0.f ? v - 0.5f : v + 0.5f);
    if (p == OP_o1w || p == OP_o2w || p == OP_o3w) put_str(s, WF[iv < 0 ? 0 : iv > 2 ? 2 : iv]);
    else if (p == OP_o1r || p == OP_o2r || p == OP_o3r) put_str(s, RG[iv < -2 ? 0 : iv > 2 ? 4 : iv + 2]);
    else if (p == OP_lwave) put_str(s, LW[iv < 0 ? 0 : iv > 4 ? 4 : iv]);
    else if (p == OP_lage) put_str(s, MD[iv < 0 ? 0 : iv > 3 ? 3 : iv]);
    else if (p == OP_uniint || p == OP_o2s || p == OP_o3s) { if (iv > 0) *s++ = '+'; put_str(put_int(s, iv), " ST"); }
    else if (p == OP_bendr) put_str(put_int(s, iv), " ST");
    else if (p == OP_o2f || p == OP_o3f) {      /* finstämning i cent: ±50 vid fullt utslag (o2f * |o2f| * 50) */
        float c = v * (v < 0.f ? -v : v) * 50.f;
        int32_t ic = (int32_t)(c < 0.f ? c - 0.5f : c + 0.5f);
        if (ic > 0) *s++ = '+';
        put_str(put_int(s, ic), " CT");
    }
    else put_int(s, (int32_t)(v * 100.f + (v < 0.f ? -0.5f : 0.5f)));   /* 0..100 */
    return bar;
}

static void set_param(int p, float v)
{
    one_set(p, v);
    edited = 1;
}

static void load_preset(int i)
{
    preset = (i % ONE_NPRESETS + ONE_NPRESETS) % ONE_NPRESETS;
    one_load_preset(preset);
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
static float bufL[ONE_MAXN], bufR[ONE_MAXN];

/* den andra kärnan: röst 3..5 där, när fm1-dock lånar ut den (c1_run), annars allt på kärna 0 */
static int c1_ok;
static void c1_voices(uint32_t arg) { (void)arg; one_render_voices(3, ONE_NV, 1); }

static void render(int32_t *out, uint32_t n)
{
    while (n) {
        uint32_t k = n > ONE_MAXN ? ONE_MAXN : n, i;
        int two;
        one_render_begin(k);
        two = c1_ok && H->c1_run(c1_voices, 0);
        one_render_voices(0, two ? 3 : ONE_NV, 0);
        if (two && H->c1_wait() != 0) c1_voices(0);   /* kärna 1 blev inte klar (och är tagen nu): räkna här */
        one_render_end(bufL, bufR);
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
    if (vel) one_note_on((int)key, (int)(vel > 127u ? 127u : vel));
    else one_note_off((int)key);
}

static void cc(uint32_t ch, uint32_t num, uint32_t val)
{
    float v = (float)val / 127.f;
    (void)ch;
    switch (num) {
    case 1: one_modwheel(v); break;
    case 64: one_sustain(val >= 64u); break;
    case 120: case 123: one_all_notes_off(); break;
    case 74: set_param(OP_cut, v); break;       /* fm1-dock:s KNOBS CC */
    case 71: set_param(OP_res, v); break;
    case 73: set_param(OP_aa, v); break;
    case 72: set_param(OP_ar, v); break;
    case 77: set_era((int32_t)(val * 100u + 63u) / 127); break;   /* ERA */
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
    case 0xD0: one_aftertouch((float)d1 / 127.f); break;
    case 0xA0:                                  /* tryck per tangent (FM-1:ans tangenter): det största gäller */
        at_max = d2 > at_max ? d2 : (d2 == 0u ? 0u : at_max);
        one_aftertouch((float)at_max / 127.f);
        break;
    case 0xE0: one_bend((float)((int32_t)((d2 << 7) | d1) - 8192) / 8192.f); break;
    default: break;
    }
}

/* ------------------------------------------------------------------ panelen */
static void turn(int p, int32_t clicks)
{
    const one_param_t *d = &ONE_PARAMS[p];
    float step = d->integer ? 1.f : (d->hi - d->lo) * 0.01f;
    set_param(p, one_get(p) + step * (float)clicks);
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
        for (i = 0; i < OP_N; i++) s.p[i] = one_get(i);
        s.era = (float)era;
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
    H->text(8, y, 142, 0, ONE_PARAMS[p].name, C_LBL, 0);
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
    uint32_t vm = one_meter_voices();
    int saving = saved_ms && H->ms() - saved_ms < 1500u;
    if (full) {
        H->fill(0, HB_TOP, 240, 240 - HB_TOP, C_BLACK);
        shown.preset = shown.page = shown.edited = shown.saved = shown.era = -1;
        shown.voices = ~0u;
    }
    if (preset != shown.preset || edited != shown.edited) {
        char s[40], *e;
        const char *name = ONE_PRESETS[preset].name;
        e = put_int(s, preset);
        if (preset < 10) { s[1] = s[0]; s[0] = '0'; s[2] = 0; e = s + 2; }
        H->text(8, 28, 40, 0, s, C_HEAD, 0);
        H->text(48, 28, 112, 0, "ONE " ONE_VERSION, C_HEAD, 0);
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
        int32_t v = (int32_t)(one_get(p) * 1000.f);
        if (shown.row[r] != p || shown.val[r] != v) { draw_row(r); shown.row[r] = p; shown.val[r] = v; }
    }
    if (vm != shown.voices) {                   /* sex prickar: rösterna som låter */
        for (r = 0; r < ONE_NV; r++) H->fill(170 + r * 11, 86, 8, 8, vm >> r & 1u ? C_VAL : C_TRACK);
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
    H = h;
    saved_ms = 0;
    c1_ok = HB_HOST_HAS(H, c1_wait);
    one_init();
    sliten_init(&tape);
    era = 0; tape_on = 0;
    era_apply();
    if (H->load(&s, sizeof s) == 0 && s.magic == SET_MAGIC && s.version == SET_VERSION && s.preset < ONE_NPRESETS) {
        int i;
        preset = s.preset;
        page = s.page < NPAGES ? s.page : 0;
        for (i = 0; i < OP_N; i++) one_set(i, s.p[i]);
        edited = s.edited;
        set_era((int32_t)s.era);
    } else {
        load_preset(1);                         /* one */
    }
    leds();
    H->log("one: redo");
    return 0;
}

static void release(void)
{
    one_all_notes_off();
    one_reset();
    sliten_reset(&tape);
}

HB_DESC = {HB_ABI, "ONE", init, release, note, cc, render, ui, draw, 0, 0, 0, midi};
