/* SPDX-License-Identifier: GPL-3.0-only */
/* BLONDA PULS som fm1-dock-modul för M-VAVE FM-1: trummaskin med åtta syntade trumljud och 16-stegssekvenser.
 *
 * Tangenterna (modulen har hela panelen, HB_F_PANEL):
 *   STEG (SEQ lyser)   de 16 vita tangenterna är stegen för trumljudet som är valt (lyser = slag);
 *                      de 8 första svarta väljer och spelar trumljud: BD SD RS CP CH OH TOM CB
 *   LIVE (SEQ släckt)  alla tangenter spelar trumljud efter tonnummer, som MIDI: C BD, C# RS, D SD, D# CP, E SD,
 *                      F G A B pukor, F# CH, G# CB, A# OH
 * Knappar och rattar:
 *   PLAY               start / stopp          REC   spela in det du spelar (kvantiserat till närmaste steg)
 *   SEQ                STEG / LIVE            ARP   accenter: de vita tangenterna sätter accent i stället för slag
 *   FX SEL ENV LFO EDIT GLO   mönster 1–6 (under spel byts det när takten är slut). Håll ett och tryck ett
 *                      annat: kopierar det hållna dit
 *   OCT- / OCT+        tempo -1 / +1 BPM      SAVE  sparar allt (trumset, rattar, mönster, tempo, ERA)
 *   SELECT             sida: BD .. CB, GLOBAL KNOB1..4  sidans rattar: nivå, stämning, längd, karaktär
 *                      (GLOBAL: tempo, swing, drivning, volym)
 *   PRESETS            trumset (10 st)        ALGORITHM  ERA: 0 MODERN, 1–60 TAPE, 61–100 LOFI (MIDI CC 77)
 * MIDI in: toner spelar trumljud (och spelas in med REC), program change = trumset, CC 77 = ERA. */
#include "hb_module.h"
#include "puls_fm1.h"
#include "sliten_fm1.h"

static const hb_host_t *H;
static char *put_int(char *s, int32_t v);
static char *put_str(char *s, const char *a);

/* ------------------------------------------------------------------ mönster och tillstånd */
#define NPAT 6
#define NSTEP 16
#define PAGE_GLOBAL PULS_NV
static uint16_t steps[NPAT][PULS_NV];           /* bit s = slag på steg s */
static uint16_t acc[NPAT];                      /* accent per steg */
static int kit = 1, voice, page, live, acc_edit, edited;
static volatile int playing, rec, pat, pat_next = -1;
static volatile int32_t bpm = 120, swing;
static volatile uint32_t saved_ms, copied_ms;
static int held_pat = -1;

/* tangenterna: 16 vita (stegen) och 11 svarta, k = 0 (F3) .. 26 (G5) */
static const uint8_t WHITE[NSTEP] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};
static const uint8_t BLACK[11] = {1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25};
static const uint8_t PAT_BTN[NPAT] = {HB_BTN_FX, HB_BTN_SEL, HB_BTN_ENV, HB_BTN_LFO, HB_BTN_EDIT, HB_BTN_GLO};

static const char *const VNAME[PULS_NV] = {"BD  KICK", "SD  SNARE", "RS  RIM", "CP  CLAP", "CH  CLOSED HAT", "OH  OPEN HAT",
                                           "TOM", "CB  COWBELL"};
static const char *const TONNAME[PULS_NV] = {"PUNCH", "SNAPPY", "CLICK", "SPREAD", "NOISE", "NOISE", "SWEEP", "TONE"};

/* standardmönstren (sextondelar): BD SD RS CP CH OH TOM CB */
static uint16_t bits(const int8_t *s) { uint16_t b = 0; while (*s >= 0) b |= (uint16_t)(1u << *s++); return b; }
static const int8_t DEF[NPAT][PULS_NV][17] = {
    {{0, 6, 8, -1}, {4, 12, -1}, {10, -1}, {12, -1}, {0, 2, 4, 6, 8, 10, -1}, {14, -1}, {15, -1}, {-1}},          /* grund */
    {{0, 4, 8, 12, -1}, {-1}, {3, 11, -1}, {4, 12, -1}, {-1}, {2, 6, 10, 14, -1}, {-1}, {-1}},                    /* fyra på golvet */
    {{0, 7, 10, -1}, {4, 12, -1}, {15, -1}, {-1}, {0, 2, 4, 6, 8, 10, 12, 14, -1}, {-1}, {-1}, {-1}},             /* boom bap */
    {{0, 3, 10, -1}, {4, 12, -1}, {-1}, {12, -1}, {2, 6, 10, -1}, {14, -1}, {13, 15, -1}, {3, 7, 11, -1}},        /* elektro */
    {{0, 2, 10, 11, -1}, {4, 7, 9, 12, 15, -1}, {-1}, {-1}, {0, 2, 4, 6, 8, 10, 12, 14, -1}, {-1}, {-1}, {-1}},   /* breakbeat */
    {{-1}, {-1}, {-1}, {-1}, {-1}, {-1}, {-1}, {-1}},                                                            /* tomt */
};
static void default_patterns(void)
{
    int p, v;
    for (p = 0; p < NPAT; p++) {
        for (v = 0; v < PULS_NV; v++) steps[p][v] = bits(DEF[p][v]);
        acc[p] = 0x1111u;                       /* 1, 5, 9, 13 */
    }
}

/* sparat: 192 byte (värden upp till 240) */
#define SET_MAGIC 0x534C5550u                   /* "PULS" */
#define SET_VERSION 1
typedef struct {
    uint32_t magic;
    uint8_t version, kit, voice, page, live, bpm, swing, era, pat, edited, pad[2];
    uint16_t p[PP_N];                           /* rattarna x 10000 */
    uint16_t steps[NPAT][PULS_NV];
    uint16_t acc[NPAT];
} settings_t;

/* skärmens färger: som BRIS (varmt elfenben, orange och petrol på svart) */
static const uint16_t C_NAME = HB_RGB(236, 230, 214), C_VAL = HB_RGB(242, 106, 44), C_LBL = HB_RGB(158, 150, 132),
                      C_HEAD = HB_RGB(52, 150, 144), C_TRACK = HB_RGB(56, 40, 30), C_RULE = HB_RGB(78, 70, 58),
                      C_DIM = HB_RGB(156, 92, 54), C_BEAT = HB_RGB(70, 52, 40), C_BLACK = 0;

/* ------------------------------------------------------------------ ERA: band och lofi (som BRIS) */
static sliten_t tape;
static volatile int32_t era;
static int tape_on;
static void era_apply(void)
{
    int32_t e = era;
    if (e <= 0) { sliten_set(&tape, SL_wet, 0.f); return; }
    if (e <= 60) {
        float t = (float)e / 60.f;
        sliten_set(&tape, SL_media, 0.f);
        sliten_set(&tape, SL_age, 0.2f + 0.8f * t);
        sliten_set(&tape, SL_wow, 0.35f);
        sliten_set(&tape, SL_flut, 0.3f);
        sliten_set(&tape, SL_sat, 0.3f + 0.4f * t);
        sliten_set(&tape, SL_brus, 0.15f + 0.35f * t);
        sliten_set(&tape, SL_avbr, 0.15f * t * t);
        sliten_set(&tape, SL_ton, 0.62f - 0.25f * t);
    } else {
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
    if (era > 0 && !tape_on) { sliten_reset(&tape); tape.sWet = 0.f; }
    era_apply();
    if (era > 0) tape_on = 1;
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
static int32_t clampi(int32_t x, int32_t lo, int32_t hi) { return x < lo ? lo : x > hi ? hi : x; }

static void load_kit(int i)
{
    kit = (i % PULS_NPRESETS + PULS_NPRESETS) % PULS_NPRESETS;
    puls_load_preset(kit);
    edited = 0;
}

/* ------------------------------------------------------------------ sekvensern (i ljudavbrottet) */
static volatile int32_t rem16;                  /* tid kvar till nästa steg, sampel x 65536 */
static volatile int32_t cur_len16 = 1;          /* längden på steget som pågår */
static volatile int cur = -1, next_step;        /* steget som pågår (-1: inget), nästa att spela */
static volatile uint16_t skip[PULS_NV];         /* inspelat precis före sitt steg: spela inte det en gång till */

static int32_t step_len16(int s)
{
    float per = (float)PULS_SR * 60.f / (float)bpm / 4.f;
    float off = (float)swing * 0.005f * per;    /* swing 0..100: udda steg kommer senare (50 % .. 75 %) */
    return (int32_t)(((s & 1) ? per - off : per + off) * 65536.f);
}
static void fire(void)
{
    int s = next_step, v;
    uint16_t bit = (uint16_t)(1u << s);
    if (s == 0 && pat_next >= 0) { pat = pat_next; pat_next = -1; }
    for (v = 0; v < PULS_NV; v++) {
        if (skip[v] & bit) { skip[v] &= (uint16_t)~bit; continue; }
        if (steps[pat][v] & bit) puls_hit(v, (acc[pat] & bit) ? 1.f : 0.72f);
    }
    cur = s;
    cur_len16 = step_len16(s);
    rem16 += cur_len16;
    next_step = (s + 1) & (NSTEP - 1);
}
static void start_stop(void)
{
    if (playing) { playing = 0; cur = -1; return; }
    next_step = 0; rem16 = 0; cur = -1;
    playing = 1;
}

/* ett slag som spelas in med REC: till steget som pågår, eller nästa om vi är mer än halvvägs */
static void record(int v)
{
    int s = cur;
    if (!rec || !playing || s < 0 || v < 0 || v >= PULS_NV) return;
    if (rem16 < cur_len16 / 2) {
        s = (s + 1) & (NSTEP - 1);
        skip[v] |= (uint16_t)(1u << s);         /* nyss spelat: steget ska inte slå igen */
    }
    steps[pat][v] |= (uint16_t)(1u << s);
    edited = 1;
}

/* ------------------------------------------------------------------ ljud */
static float bufL[PULS_MAXN], bufR[PULS_MAXN];
static void render(int32_t *out, uint32_t n)
{
    while (n) {
        uint32_t k = n > PULS_MAXN ? PULS_MAXN : n, done = 0, i;
        while (done < k) {                      /* bitar fram till nästa steg, så slagen hamnar på rätt sampel */
            uint32_t m = k - done;
            if (playing) {
                int32_t until;
                if (rem16 <= 0) { fire(); continue; }
                until = (rem16 + 65535) >> 16;
                if ((uint32_t)until < m) m = (uint32_t)until;
            }
            puls_render(bufL + done, bufR + done, m);
            if (playing) rem16 -= (int32_t)(m << 16);
            done += m;
        }
        if (tape_on) {
            sliten_process(&tape, bufL, bufR, k);
            if (era <= 0 && tape.sWet < 0.0005f) tape_on = 0;
        }
        for (i = 0; i < k; i++) {
            out[2u * i] += (int32_t)(clampf(bufL[i], -1.f, 1.f) * 32767.f);
            out[2u * i + 1u] += (int32_t)(clampf(bufR[i], -1.f, 1.f) * 32767.f);
        }
        out += 2u * k;
        n -= k;
    }
}

/* ------------------------------------------------------------------ spela och MIDI */
static const int8_t NOTE_VOICE[12] = {PV_BD, PV_RS, PV_SD, PV_CP, PV_SD, PV_TOM, PV_CH, PV_TOM, PV_CB, PV_TOM, PV_OH, PV_TOM};
static void play_note(int note, int vel)
{
    if (vel <= 0) return;
    puls_note_on(note, vel);
    record(NOTE_VOICE[((note % 12) + 12) % 12]);
}

static void set_param(int p, float v) { puls_set(p, v); edited = 1; }

static void cc(uint32_t ch, uint32_t num, uint32_t val)
{
    float v = (float)val / 127.f;
    (void)ch;
    switch (num) {
    case 74: set_param(voice * 4 + 0, v); break;   /* fm1-dock:s KNOBS CC: det valda trumljudets rattar */
    case 71: set_param(voice * 4 + 1, v); break;
    case 73: set_param(voice * 4 + 2, v); break;
    case 72: set_param(voice * 4 + 3, v); break;
    case 77: set_era((int32_t)(val * 100u + 63u) / 127); break;
    default: break;
    }
}
static void note(uint32_t ch, uint32_t key, uint32_t vel)
{
    (void)ch;
    if (key <= 127u && vel) play_note((int)key, (int)(vel > 127u ? 127u : vel));
}
static void midi(uint32_t st, uint32_t d1, uint32_t d2)
{
    switch (st & 0xF0u) {
    case 0x90: note(st & 15u, d1, d2); break;
    case 0xB0: cc(st & 15u, d1, d2); break;
    case 0xC0: load_kit((int)d1); break;
    default: break;
    }
}

/* ------------------------------------------------------------------ panelen */
static void save_all(void)
{
    settings_t s;
    int i, v;
    s.magic = SET_MAGIC; s.version = SET_VERSION;
    s.kit = (uint8_t)kit; s.voice = (uint8_t)voice; s.page = (uint8_t)page; s.live = (uint8_t)live;
    s.bpm = (uint8_t)bpm; s.swing = (uint8_t)swing; s.era = (uint8_t)era; s.pat = (uint8_t)pat; s.edited = (uint8_t)edited;
    s.pad[0] = s.pad[1] = 0;
    for (i = 0; i < PP_N; i++) s.p[i] = (uint16_t)(puls_get(i) * 10000.f + 0.5f);
    for (i = 0; i < NPAT; i++) { for (v = 0; v < PULS_NV; v++) s.steps[i][v] = steps[i][v]; s.acc[i] = acc[i]; }
    H->save(&s, sizeof s);
    saved_ms = H->ms() | 1u;
}

static void turn(int knob, int32_t clicks)
{
    if (page == PAGE_GLOBAL) {
        if (knob == 0) bpm = clampi(bpm + clicks, 40, 240);
        else if (knob == 1) swing = clampi(swing + clicks, 0, 100);
        else set_param(knob == 2 ? PP_drv : PP_vol, puls_get(knob == 2 ? PP_drv : PP_vol) + 0.01f * (float)clicks);
        return;
    }
    set_param(page * 4 + knob, puls_get(page * 4 + knob) + 0.01f * (float)clicks);
}

static void select_voice(int v) { voice = v; page = v; }

static void key(int k, int down, int vel)
{
    int i;
    if (!down) return;
    if (vel <= 0) vel = 100;
    if (live) { play_note(53 + k, vel); return; }
    for (i = 0; i < NSTEP; i++)
        if (WHITE[i] == k) {
            uint16_t bit = (uint16_t)(1u << i);
            if (acc_edit) acc[pat] ^= bit;
            else steps[pat][voice] ^= bit;
            edited = 1;
            return;
        }
    for (i = 0; i < PULS_NV; i++)
        if (BLACK[i] == k) {
            select_voice(i);
            puls_hit(i, (float)vel / 127.f);
            record(i);
            return;
        }
}

static void pattern_button(int p, int down)
{
    if (!down) { if (held_pat == p) held_pat = -1; return; }
    if (held_pat >= 0 && held_pat != p) {       /* håll ett mönster och tryck ett annat: kopiera */
        int v;
        for (v = 0; v < PULS_NV; v++) steps[p][v] = steps[held_pat][v];
        acc[p] = acc[held_pat];
        copied_ms = H->ms() | 1u;
        edited = 1;
        return;
    }
    held_pat = p;
    if (playing) pat_next = p == pat ? -1 : p;  /* byts när takten är slut */
    else pat = p;
}

static void ui(uint32_t ev, int32_t arg)
{
    uint32_t id = HB_EV_ID(ev), kind = HB_EV_KIND(ev);
    int i;
    if (kind == HB_EV_ENC) {
        if (id >= HB_ENC_KNOB1 && id <= HB_ENC_KNOB4) turn((int)(id - HB_ENC_KNOB1), arg);
        else if (id == HB_ENC_PRESETS) load_kit(kit + arg);
        else if (id == HB_ENC_SELECT) {
            page = ((page + arg) % (PULS_NV + 1) + PULS_NV + 1) % (PULS_NV + 1);
            if (page < PULS_NV) voice = page;
        } else if (id == HB_ENC_ALGORITHM) set_era(era + arg);
        return;
    }
    if (kind == HB_EV_KEY_DOWN || kind == HB_EV_KEY_UP) { key((int)id, kind == HB_EV_KEY_DOWN, arg); return; }
    if (kind != HB_EV_BTN_DOWN && kind != HB_EV_BTN_UP) return;
    for (i = 0; i < NPAT; i++) if (PAT_BTN[i] == id) { pattern_button(i, kind == HB_EV_BTN_DOWN); return; }
    if (kind != HB_EV_BTN_DOWN) return;
    switch (id) {
    case HB_BTN_PLAY: start_stop(); break;
    case HB_BTN_REC: rec = !rec; break;
    case HB_BTN_SEQ: live = !live; break;
    case HB_BTN_ARP: acc_edit = !acc_edit; break;
    case HB_BTN_SAVE: save_all(); break;
    case HB_BTN_OCT_DOWN: bpm = clampi(bpm - 1, 40, 240); break;
    case HB_BTN_OCT_UP: bpm = clampi(bpm + 1, 40, 240); break;
    default: break;
    }
}

/* ------------------------------------------------------------------ lysdioderna */
static uint8_t led_shown[64];
static void led(uint32_t id, uint32_t on)
{
    if (id < 64u && led_shown[id] != (uint8_t)(on + 1u)) { led_shown[id] = (uint8_t)(on + 1u); H->led(id, on); }
}
static void leds(void)
{
    int i, ph = playing ? cur : -1;
    uint16_t row = live ? 0 : acc_edit ? acc[pat] : steps[pat][voice];
    for (i = 0; i < NSTEP; i++) {
        uint32_t on = (row >> i & 1u) ? 1u : 0u;
        if (i == ph) on = on ? 0u : 2u;         /* stegmarkören: släckt på ett slag, glöd annars */
        if (live) on = 2u;
        led(HB_LED_KEY(WHITE[i]), on);
    }
    for (i = 0; i < 11; i++) {
        uint32_t on = 0;
        if (live) on = 2u;
        else if (i < PULS_NV) on = i == voice ? 1u : 2u;
        led(HB_LED_KEY(BLACK[i]), on);
    }
    for (i = 0; i < NPAT; i++) led(PAT_BTN[i], i == pat ? 1u : (i == pat_next && (H->ms() & 256u)) ? 1u : 2u);
    led(HB_BTN_PLAY, playing ? 1u : 2u);
    led(HB_BTN_REC, rec ? 1u : 2u);
    led(HB_BTN_SEQ, live ? 2u : 1u);
    led(HB_BTN_ARP, acc_edit ? 1u : 2u);
    led(HB_BTN_SAVE, 2u);
}

/* ------------------------------------------------------------------ skärmen */
static struct {
    int kit, edited, page, voice, era, msg, playing, rec, pat, live, acc_edit, cur, tag, acc_mode;
    int32_t bpm, swing, val[4];
    uint16_t row[PULS_NV], acc;
} shown;

#define GX 8                                    /* rutnätet: 16 steg x 8 trumljud */
#define GY 108
#define CW 14
#define RH 8

static void draw_cell(int v, int s)
{
    uint16_t bit = (uint16_t)(1u << s), c;
    int on = steps[pat][v] & bit;
    if (on) c = v == voice ? C_VAL : C_DIM;
    else c = (s & 3) == 0 ? C_BEAT : C_TRACK;
    H->fill(GX + s * CW, GY + v * RH, CW - 2, RH - 2, c);
}

static void value_text(int knob, char *s, int32_t *bar)
{
    if (page == PAGE_GLOBAL) {
        if (knob == 0) { put_str(put_int(s, bpm), " BPM"); *bar = (bpm - 40) * 1000 / 200; return; }
        if (knob == 1) { put_int(s, swing); *bar = swing * 10; return; }
    }
    {
        int p = page == PAGE_GLOBAL ? (knob == 2 ? PP_drv : PP_vol) : page * 4 + knob;
        float v = puls_get(p);
        put_int(s, (int32_t)(v * 100.f + 0.5f));
        *bar = (int32_t)(v * 1000.f + 0.5f);
    }
}
static const char *knob_name(int knob)
{
    static const char *const G[4] = {"TEMPO", "SWING", "DRIVE", "VOLUME"};
    static const char *const V[3] = {"LEVEL", "TUNE", "DECAY"};
    if (page == PAGE_GLOBAL) return G[knob];
    return knob < 3 ? V[knob] : TONNAME[page];
}
static void draw_knob(int knob)
{
    int32_t x = 8 + (knob & 1) * 116, y = 182 + (knob >> 1) * 29, bar, w, tw;
    char s[16];
    value_text(knob, s, &bar);
    H->fill(x, y, 108, 26, C_BLACK);
    H->text(x, y, 64, 0, knob_name(knob), C_LBL, 0);
    tw = H->text_w(0, s);
    H->text(x + 108 - tw, y, tw, 0, s, C_VAL, 0);
    w = clampi(bar, 0, 1000) * 108 / 1000;
    if (w) H->fill(x, y + 19, w, 4, C_VAL);
    if (w < 108) H->fill(x + w, y + 19, 108 - w, 4, C_TRACK);
}

static void draw(uint32_t full)
{
    int v, s, k;
    uint32_t now = H->ms();
    int msg = saved_ms && now - saved_ms < 1500u ? 1 : copied_ms && now - copied_ms < 1500u ? 2 : 0;
    int ph = playing ? cur : -1;
    leds();
    if (full) {
        H->fill(0, HB_TOP, 240, 240 - HB_TOP, C_BLACK);
        shown.kit = shown.edited = shown.page = shown.voice = shown.era = shown.msg = shown.playing = shown.rec = -1;
        shown.pat = shown.live = shown.acc_edit = shown.cur = shown.tag = shown.acc_mode = -2;
        shown.bpm = -1;
        for (v = 0; v < PULS_NV; v++) shown.row[v] = (uint16_t)~steps[pat][v];
        shown.acc = (uint16_t)~acc[pat];
        for (k = 0; k < 4; k++) shown.val[k] = -999999;
    }
    if (kit != shown.kit || edited != shown.edited) {
        char t[40], *e;
        e = put_int(t, kit);
        if (kit < 10) { t[1] = t[0]; t[0] = '0'; t[2] = 0; }
        (void)e;
        H->fill(8, 28, 106, 16, C_BLACK);
        H->text(8, 28, 32, 0, t, C_HEAD, 0);
        H->text(40, 28, 72, 0, kit == 0 ? "INIT" : "PULS KIT", C_HEAD, 0);
        e = put_str(t, PULS_PRESETS[kit].name);
        if (edited) put_str(e, "*");
        H->fill(8, 44, 224, 32, C_BLACK);
        if (H->text_w(1, t) <= 224) H->text(8, 44, 224, 1, t, C_NAME, 0);
        else H->text(8, 52, 224, 0, t, C_NAME, 0);
        shown.kit = kit; shown.edited = edited;
    }
    if (era != shown.era) {
        char t[16];
        int32_t tw;
        if (era <= 0) put_str(t, "MODERN");
        else put_int(put_str(t, era <= 60 ? "TAPE " : "LOFI "), era);
        tw = H->text_w(0, t);
        H->fill(160, 28, 72, 16, C_BLACK);
        H->text(232 - tw, 28, tw, 0, t, era > 0 ? C_VAL : C_LBL, 0);
        shown.era = era;
    }
    /* raden under namnet: sidan till vänster; mönster, tempo, läge till höger */
    if (page != shown.page || msg != shown.msg || live != shown.live || acc_edit != shown.acc_edit) {
        const char *t = msg == 1 ? "SAVED" : msg == 2 ? "COPIED" : page == PAGE_GLOBAL ? "GLOBAL" : VNAME[page];
        H->fill(8, 82, 136, 16, C_BLACK);
        H->text(8, 82, 136, 0, t, msg ? C_VAL : C_HEAD, 0);
        shown.page = page; shown.msg = msg; shown.live = live; shown.acc_edit = acc_edit;
        shown.pat = -2;                         /* lägestexten till höger ritas om */
    }
    {   /* läget i rubrikraden: LIVE eller ACC */
        int tag = live ? 1 : acc_edit ? 2 : 0;
        if (tag != shown.tag) {
            H->fill(116, 28, 40, 16, C_BLACK);
            if (tag) H->text(116, 28, 40, 0, tag == 1 ? "LIVE" : "ACC", C_VAL, 0);
            shown.tag = tag;
        }
    }
    if (pat != shown.pat || bpm != shown.bpm || playing != shown.playing || rec != shown.rec) {
        char t[16], *e;
        int32_t tw;
        H->fill(146, 82, 86, 16, C_BLACK);
        e = put_int(put_str(t, "P"), pat + 1);
        e = put_str(e, " ");
        put_int(e, bpm);
        tw = H->text_w(0, t);
        H->text(232 - tw, 82, tw, 0, t, C_LBL, 0);
        if (playing) for (k = 0; k < 10; k++) H->fill(150, 85 + k, k < 5 ? k + 1 : 10 - k, 1, C_VAL);   /* spelar */
        else H->fill(150, 86, 8, 8, C_LBL);                                                            /* stopp */
        if (rec) H->fill(162, 86, 8, 8, HB_RGB(230, 40, 40));
        shown.pat = pat; shown.bpm = bpm; shown.playing = playing; shown.rec = rec;
        shown.cur = -2;
    }
    /* rutnätet */
    if (voice != shown.voice) {
        for (v = 0; v < PULS_NV; v++) shown.row[v] = (uint16_t)~steps[pat][v];
        shown.voice = voice;
    }
    for (v = 0; v < PULS_NV; v++) {
        uint16_t d = (uint16_t)(steps[pat][v] ^ shown.row[v]);
        if (!d) continue;
        for (s = 0; s < NSTEP; s++) if (d >> s & 1u) draw_cell(v, s);
        shown.row[v] = steps[pat][v];
    }
    if (acc[pat] != shown.acc || acc_edit != shown.acc_mode) {   /* accenterna: en rad under rutnätet */
        for (s = 0; s < NSTEP; s++)
            H->fill(GX + s * CW, GY + PULS_NV * RH + 1, CW - 2, 3, (acc[pat] >> s & 1u) ? (acc_edit ? C_VAL : C_HEAD) : C_TRACK);
        shown.acc = acc[pat]; shown.acc_mode = acc_edit;
    }
    if (ph != shown.cur) {                      /* stegmarkören ovanför rutnätet */
        H->fill(GX, GY - 5, 224, 3, C_BLACK);
        if (ph >= 0) H->fill(GX + ph * CW, GY - 5, CW - 2, 3, C_NAME);
        shown.cur = ph;
    }
    H->fill(8, 102, 224, 1, C_RULE);
    for (k = 0; k < 4; k++) {
        int32_t bar;
        char t[16];
        value_text(k, t, &bar);
        bar += page * 100000;                   /* byts sidan ritas allt om */
        if (bar != shown.val[k]) { draw_knob(k); shown.val[k] = bar; }
    }
}

/* ------------------------------------------------------------------ in och ur */
static int init(const hb_host_t *h)
{
    settings_t s;
    int i, v;
    H = h;
    saved_ms = copied_ms = 0;
    for (i = 0; i < 64; i++) led_shown[i] = 0;
    puls_init();
    sliten_init(&tape);
    era = 0; tape_on = 0;
    era_apply();
    playing = 0; rec = 0; cur = -1; pat_next = -1; held_pat = -1; acc_edit = 0;
    for (v = 0; v < PULS_NV; v++) skip[v] = 0;
    default_patterns();
    if (H->load(&s, sizeof s) == 0 && s.magic == SET_MAGIC && s.version == SET_VERSION && s.kit < PULS_NPRESETS) {
        kit = s.kit;
        for (i = 0; i < PP_N; i++) puls_set(i, (float)s.p[i] * 0.0001f);
        for (i = 0; i < NPAT; i++) { for (v = 0; v < PULS_NV; v++) steps[i][v] = s.steps[i][v]; acc[i] = s.acc[i]; }
        voice = s.voice < PULS_NV ? s.voice : 0;
        page = s.page <= PAGE_GLOBAL ? s.page : voice;
        live = s.live ? 1 : 0;
        bpm = clampi(s.bpm, 40, 240);
        swing = clampi(s.swing, 0, 100);
        pat = s.pat < NPAT ? s.pat : 0;
        edited = s.edited;
        set_era(s.era);
    } else {
        load_kit(1);                            /* Klassisk */
        voice = 0; page = 0; live = 0; bpm = 120; swing = 0; pat = 0;
    }
    H->log("puls: redo");
    return 0;
}

static void release(void)
{
    playing = 0;
    puls_reset();
    sliten_reset(&tape);
}

HB_DESC = {HB_ABI, "PULS", init, release, note, cc, render, ui, draw, 0, 0, HB_F_PANEL, midi};
