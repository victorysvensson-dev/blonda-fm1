/* SPDX-License-Identifier: GPL-3.0-only */
/* Webbemulatorns FM-1: fm1-dock:s hb_host_t i WebAssembly, med fm1-dock:s eget typsnitt (gfx.c).
 * En modul ur moduler/ (till exempel moduler/bris eller moduler/puls) länkas in och körs oförändrad, som på FM-1:
 * samma init, note, ui, render och draw, och hela panelen som händelser för en modul med HB_F_PANEL.
 * JavaScript-sidan anropar w_*-funktionerna nedan.
 *   web/bygg.sh  ->  web/<modul>.wasm */
#include <stdint.h>
#include "hb_module.h"

void *memcpy(void *d, const void *s, unsigned n);

/* ---- skärmen: 240 x 240 RGB565 */
static uint16_t fb[240 * 240];
static volatile uint32_t dirty;
static void lcd_sync(void) {}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)
{
    uint32_t i, j;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            if (x + i < 240u && y + j < 240u) {
                uint16_t c = px[j * w + i];
                fb[(y + j) * 240u + x + i] = (uint16_t)(c >> 8 | c << 8);
            }
    dirty = 1;
}
#define CV_MAX (240u * 96u)
#include "gfx.c"

static void h_fill(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)
{
    int32_t i, j;
    for (j = y; j < y + h; j++)
        for (i = x; i < x + w; i++)
            if (i >= 0 && i < 240 && j >= 0 && j < 240) fb[j * 240 + i] = c;
    dirty = 1;
}
static void h_text(int32_t x, int32_t y, int32_t w, uint32_t big, const char *s, uint16_t fg, uint32_t center)
{
    const felucca_font_t *f = big ? &FONT_L : &FONT_S;
    if (x < 0 || y < 0 || w <= 0 || x + w > 240 || y + (int32_t)f->h > 240) return;
    draw_text_box((uint32_t)x, (uint32_t)y, (uint32_t)w, f, s, fg, center ? 1 : 0);
}
static void h_cv_begin(int32_t w, int32_t h, uint16_t bg)
{
    if (w < 1) w = 1;
    if (w > 240) w = 240;
    if (h < 1) h = 1;
    if ((uint32_t)(w * h) > CV_MAX) h = (int32_t)(CV_MAX / (uint32_t)w);
    cv_begin((uint32_t)w, (uint32_t)h, bg);
}
static void h_cv_pixel(int32_t x, int32_t y, uint16_t c) { cv_pset(x, y, c); }
static void h_cv_text(int32_t x, int32_t y, uint32_t big, const char *s, uint16_t fg) { cv_text(x, y, big ? &FONT_L : &FONT_S, s, fg); }
static void h_cv_blit(int32_t x, int32_t y)
{
    if (x >= 0 && y >= 0 && x + (int32_t)cv_w <= 240 && y + (int32_t)cv_h <= 240) cv_blit((uint32_t)x, (uint32_t)y);
}
static void h_blit(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t *px)
{
    if (x >= 0 && y >= 0 && w > 0 && h > 0) lcd_blit((uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h, px);
}
static int32_t h_text_w(uint32_t big, const char *s) { return text_w(big ? &FONT_L : &FONT_S, s); }

/* ---- tid, logg, lysdioder, inställningar */
static uint32_t now_ms, rng = 0x2545F491u;
static uint32_t h_ms(void) { return now_ms; }
static uint32_t h_ticks(void) { return now_ms * 24000u; }
static void h_log(const char *s) { (void)s; }
static void h_log_u32(const char *s, uint32_t v) { (void)s; (void)v; }
static uint8_t leds[64];
static void h_led(uint32_t id, uint32_t on) { if (id < 64u) leds[id] = (uint8_t)on; }
static uint32_t h_rand(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static uint8_t store[240];
static uint32_t store_n, saves;
static int h_load(void *p, uint32_t n) { if (!store_n || n > store_n) return -1; memcpy(p, store, n); return 0; }
static void h_save(const void *p, uint32_t n) { if (n <= 240u) { memcpy(store, p, n); store_n = n; saves++; } }
static void h_midi_send(uint32_t st, uint32_t d1, uint32_t d2) { (void)st; (void)d1; (void)d2; }
static uint32_t master_q12 = 4096u;
static uint32_t h_master(void) { return master_q12; }

static const hb_host_t HOST = {
    .abi = HB_ABI, .size = (uint32_t)__builtin_offsetof(hb_host_t, bank_load),
    .ms = h_ms, .ticks = h_ticks, .log = h_log, .log_u32 = h_log_u32, .fill = h_fill, .text = h_text,
    .cv_begin = h_cv_begin, .cv_rect = cv_rect, .cv_line = cv_line, .cv_pixel = h_cv_pixel, .cv_text = h_cv_text,
    .cv_blit = h_cv_blit, .led = h_led, .rand = h_rand, .load = h_load, .save = h_save, .midi_send = h_midi_send,
    .master = h_master, .blit = h_blit, .text_w = h_text_w,
};

extern const hb_module_t hb_module;

/* modulen har hela panelen (HB_F_PANEL): tangenterna och OCT- / OCT+ blir händelser till den, som i fm1-dock */
#define PANEL (hb_module.flags & HB_F_PANEL)

/* värdens rad överst (som fm1-dock: modulens namn, oktaven när värden spelar tangenterna) */
static int32_t oct;
static void top(void)
{
    char s[8] = "OCT  0";
    h_fill(0, 0, 240, HB_TOP, 0);
    draw_text_box(8, 4, 120, &FONT_S, hb_module.name, RGB(150, 150, 150), 0);
    s[4] = oct < 0 ? '-' : oct > 0 ? '+' : ' ';
    s[5] = (char)('0' + (oct < 0 ? -oct : oct));
    if (!PANEL) draw_text_box(160, 4, 72, &FONT_S, s, RGB(150, 150, 150), 2);
    h_fill(0, HB_TOP - 1, 240, 1, RGB(40, 40, 40));
}

/* ---- ljud: Q15 från modulen, float till webbläsaren */
#define MAXN 512
static int32_t q[2 * MAXN];
static float outL[MAXN], outR[MAXN];

/* ---- det JavaScript anropar */
__attribute__((export_name("w_init"))) int w_init(void)
{
    int r;
    palette_set(0);
    r = hb_module.init(&HOST);
    top();
    hb_module.draw(1);
    return r;
}
__attribute__((export_name("w_render"))) void w_render(uint32_t n)
{
    uint32_t i;
    float g = (float)master_q12 * (1.f / 4096.f) * (1.f / 32768.f);
    if (n > MAXN) n = MAXN;
    for (i = 0; i < 2u * n; i++) q[i] = 0;
    hb_module.render(q, n);
    for (i = 0; i < n; i++) {
        int32_t l = q[2u * i], r = q[2u * i + 1u];
        l = l > 32767 ? 32767 : l < -32767 ? -32767 : l;
        r = r > 32767 ? 32767 : r < -32767 ? -32767 : r;
        outL[i] = (float)l * g;
        outR[i] = (float)r * g;
    }
}
__attribute__((export_name("w_outL"))) float *w_outL(void) { return outL; }
__attribute__((export_name("w_outR"))) float *w_outR(void) { return outR; }
/* en av panelens tangenter: k = 0 (F3) .. 26 (G5), vel 0 = släppt. Som ton (oktaven från OCT- / OCT+), eller som
 * händelse när modulen har hela panelen */
__attribute__((export_name("w_key"))) void w_key(uint32_t k, uint32_t vel)
{
    int32_t n = 53 + (int32_t)k + 12 * oct;
    if (PANEL) { hb_module.ui((vel ? HB_EV_KEY_DOWN : HB_EV_KEY_UP) | k, (int32_t)vel); return; }
    if (n >= 0 && n <= 127) hb_module.note(0, (uint32_t)n, vel);
}
__attribute__((export_name("w_note"))) void w_note(uint32_t n, uint32_t vel) { hb_module.note(0, n, vel); }
__attribute__((export_name("w_midi"))) void w_midi(uint32_t st, uint32_t d1, uint32_t d2)
{
    if (hb_module.midi) hb_module.midi(st, d1, d2);
}
__attribute__((export_name("w_btn"))) void w_btn(uint32_t id, uint32_t down)
{
    hb_module.ui((down ? HB_EV_BTN_DOWN : HB_EV_BTN_UP) | id, 0);
}
__attribute__((export_name("w_enc"))) void w_enc(uint32_t id, int32_t clicks) { hb_module.ui(HB_EV_ENC | id, clicks); }
__attribute__((export_name("w_oct"))) void w_oct(int32_t d)
{
    if (PANEL) {
        uint32_t id = d < 0 ? HB_BTN_OCT_DOWN : HB_BTN_OCT_UP;
        hb_module.ui(HB_EV_BTN_DOWN | id, 0);
        hb_module.ui(HB_EV_BTN_UP | id, 0);
        return;
    }
    oct += d;
    oct = oct < -3 ? -3 : oct > 3 ? 3 : oct;
    top();
}
__attribute__((export_name("w_master"))) void w_master(uint32_t q12) { master_q12 = q12 > 8192u ? 8192u : q12; }
__attribute__((export_name("w_draw"))) void w_draw(uint32_t ms, uint32_t full)
{
    now_ms = ms;
    if (full) top();
    hb_module.draw(full);
}
__attribute__((export_name("w_dirty"))) uint32_t w_dirty(void) { uint32_t d = dirty; dirty = 0; return d; }
__attribute__((export_name("w_fb"))) uint16_t *w_fb(void) { return fb; }
__attribute__((export_name("w_leds"))) uint8_t *w_leds(void) { return leds; }     /* 64: knapparna, tangenterna från 14 */
__attribute__((export_name("w_flags"))) uint32_t w_flags(void) { return hb_module.flags; }
__attribute__((export_name("w_store"))) uint8_t *w_store(void) { return store; }
__attribute__((export_name("w_store_n"))) uint32_t w_store_n(void) { return store_n; }
__attribute__((export_name("w_store_set"))) void w_store_set(uint32_t n) { store_n = n <= 240u ? n : 0u; }
__attribute__((export_name("w_saves"))) uint32_t w_saves(void) { return saves; }
