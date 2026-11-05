/* SPDX-License-Identifier: GPL-3.0-only */
/* En låtsas-FM-1 för BRIS-modulen: fm1-dock:s hb_host_t på datorn, med fm1-dock:s egna typsnitt (gfx.c).
 * Modulen (moduler/bris/bris_module.c) körs oförändrad: init, rattar, knappar, toner, render och draw, som
 * fm1-dock anropar dem. Ut kommer skärmbilder (PPM) och ljudet som modulen lämnar till värden (WAV, Q15).
 *   cc -O2 -I$FM1_DOCK/firmware/dock -I$FM1_DOCK/firmware/src -I$FM1_DOCK/firmware/gen -Isrc \
 *      test/modul_host.c bris_module.c src/bris_fm1.c -o build/bris-modul   (i moduler/bris)
 *   build/bris-modul build/skarm      -> build/skarm-NN.ppm, build/skarm.wav, och kontroller (felkod vid fel) */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hb_module.h"

/* ---- skärmen: 240 x 240 RGB565, som panelen får den (gfx.c skickar byten omvända) */
static uint16_t fb[240 * 240];
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
}
#define CV_MAX (240u * 96u)
#include "gfx.c"

static void h_fill(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)
{
    int32_t i, j;
    for (j = y; j < y + h; j++)
        for (i = x; i < x + w; i++)
            if (i >= 0 && i < 240 && j >= 0 && j < 240) fb[j * 240 + i] = c;
}
static int bad_draw;
static void h_text(int32_t x, int32_t y, int32_t w, uint32_t big, const char *s, uint16_t fg, uint32_t center)
{
    const felucca_font_t *f = big ? &FONT_L : &FONT_S;
    if (x < 0 || y < 0 || w <= 0 || x + w > 240 || y + (int32_t)f->h > 240) { bad_draw++; return; }
    if (y < HB_TOP) bad_draw++;
    draw_text_box((uint32_t)x, (uint32_t)y, (uint32_t)w, f, s, fg, center ? 1 : 0);
}
static void h_cv_begin(int32_t w, int32_t h, uint16_t bg) { cv_begin((uint32_t)w, (uint32_t)h, bg); }
static void h_cv_pixel(int32_t x, int32_t y, uint16_t c) { cv_pset(x, y, c); }
static void h_cv_text(int32_t x, int32_t y, uint32_t big, const char *s, uint16_t fg) { cv_text(x, y, big ? &FONT_L : &FONT_S, s, fg); }
static void h_cv_blit(int32_t x, int32_t y) { cv_blit((uint32_t)x, (uint32_t)y); }
static int32_t h_text_w(uint32_t big, const char *s) { return text_w(big ? &FONT_L : &FONT_S, s); }

/* ---- resten av värden */
static uint32_t now_ms;
static uint32_t h_ms(void) { return now_ms; }
static uint32_t h_ticks(void) { return now_ms * 24000u; }
static void h_log(const char *s) { printf("  log: %s\n", s); }
static void h_log_u32(const char *s, uint32_t v) { printf("  log: %s%08x\n", s, v); }
static uint32_t leds[64];
static void h_led(uint32_t id, uint32_t on) { if (id < 64u) leds[id] = on; }
static uint32_t h_rand(void) { return (uint32_t)rand(); }
static uint8_t store[240];
static uint32_t store_n;
static int h_load(void *p, uint32_t n) { if (!store_n || n > store_n) return -1; memcpy(p, store, n); return 0; }
static void h_save(const void *p, uint32_t n) { if (n <= 240u) { memcpy(store, p, n); store_n = n; } else bad_draw += 1000; }
static void h_midi_send(uint32_t st, uint32_t d1, uint32_t d2) { (void)st; (void)d1; (void)d2; }
static uint32_t h_master(void) { return 4096u; }

static const hb_host_t HOST = {
    HB_ABI, (uint32_t)__builtin_offsetof(hb_host_t, blit) + sizeof(void *) * 2, h_ms, h_ticks, h_log, h_log_u32, h_fill, h_text,
    h_cv_begin, cv_rect, cv_line, h_cv_pixel, h_cv_text, h_cv_blit, h_led, h_rand, h_load, h_save,
    h_midi_send, h_master, 0, h_text_w,
};

extern const hb_module_t hb_module;
static const hb_module_t *M = &hb_module;

static void shot(const char *base, int n)
{
    char path[512];
    FILE *f;
    int i;
    snprintf(path, sizeof path, "%s-%02d.ppm", base, n);
    f = fopen(path, "wb");
    fprintf(f, "P6\n240 240\n255\n");
    for (i = 0; i < 240 * 240; i++) {
        uint16_t c = fb[i];
        unsigned char rgb[3] = {(unsigned char)((c >> 11) << 3), (unsigned char)(((c >> 5) & 63) << 2), (unsigned char)((c & 31) << 3)};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

/* ljud: block om 256 sampel, som ljudavbrottet */
static int16_t *wav;
static int wav_n, wav_cap, clipped;
static int32_t peak;
static void audio(int ms)
{
    int blocks = ms * 44100 / 1000 / 256, b, i;
    for (b = 0; b < blocks; b++) {
        int32_t out[512] = {0};
        M->render(out, 256);
        for (i = 0; i < 512; i++) {
            int32_t v = out[i];
            if (v > 32767 || v < -32767) clipped++;
            if (v > peak) peak = v;
            if (-v > peak) peak = -v;
            if (wav_n >= wav_cap) { wav_cap = wav_cap ? wav_cap * 2 : 1 << 20; wav = realloc(wav, (size_t)wav_cap * 2); }
            wav[wav_n++] = (int16_t)(v > 32767 ? 32767 : v < -32767 ? -32767 : v);
        }
        now_ms += 6;
        if (b % 3 == 0) M->draw(0);
    }
}
static void write_wav(const char *path)
{
    FILE *f = fopen(path, "wb");
    uint32_t ds = (uint32_t)wav_n * 2, rs = 36 + ds, sr = 44100, br = 44100 * 4, fl = 16;
    uint16_t fmt = 1, ch = 2, ba = 4, bits = 16;
    fwrite("RIFF", 1, 4, f); fwrite(&rs, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&fl, 4, 1, f);
    fwrite(&fmt, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&sr, 4, 1, f); fwrite(&br, 4, 1, f); fwrite(&ba, 2, 1, f);
    fwrite(&bits, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&ds, 4, 1, f); fwrite(wav, 2, (size_t)wav_n, f);
    fclose(f);
}

static int fails;
#define CHECK(c, msg) do { if (!(c)) { printf("FEL: %s\n", msg); fails++; } else printf("ok: %s\n", msg); } while (0)

static void enc(uint32_t e, int32_t c) { M->ui(HB_EV_ENC | e, c); }
static void btn(uint32_t b) { M->ui(HB_EV_BTN_DOWN | b, 0); M->ui(HB_EV_BTN_UP | b, 0); }

int main(int argc, char **argv)
{
    const char *base = argc > 1 ? argv[1] : "build/skarm";
    char wpath[512];
    int n = 0;
    uint32_t i;
    int32_t peak_quiet;

    palette_set(0);
    CHECK(M->abi == HB_ABI && !strcmp(M->name, "BRIS"), "beskrivningen: ABI och namn");
    CHECK(M->init(&HOST) == 0, "init");
    M->draw(1);
    shot(base, n++);                            /* 00: start, Poly Magic */

    M->note(0, 60, 100); M->note(0, 64, 90); M->note(0, 67, 80);
    audio(600);
    shot(base, n++);                            /* 01: tre röster låter */
    CHECK(peak > 1000, "ljud kommer ut när tre toner spelas");
    M->note(0, 60, 0); M->note(0, 64, 0); M->note(0, 67, 0);
    audio(3000);
    peak_quiet = 0;
    {
        int32_t out[512] = {0};
        M->render(out, 256);
        for (i = 0; i < 512; i++) if (abs(out[i]) > peak_quiet) peak_quiet = abs(out[i]);
    }
    CHECK(peak_quiet < 50, "tyst igen efter släpp och eko");

    enc(HB_ENC_PRESETS, 4);                     /* Unison Bass */
    M->draw(0);
    shot(base, n++);                            /* 02 */
    btn(HB_BTN_SEL); enc(HB_ENC_KNOB1, -20); enc(HB_ENC_KNOB2, 30);
    M->note(0, 36, 120); audio(800); M->note(0, 36, 0); audio(400);
    M->draw(0);
    shot(base, n++);                            /* 03: filtersidan, ändrat (*) */
    btn(HB_BTN_ENV); M->draw(0); shot(base, n++);   /* 04: EG2 */
    btn(HB_BTN_ENV); M->draw(0); shot(base, n++);   /* 05: EG1 */
    btn(HB_BTN_LFO); M->draw(0); shot(base, n++);   /* 06 */
    btn(HB_BTN_FX); M->draw(0); shot(base, n++);    /* 07 */
    btn(HB_BTN_GLO); M->draw(0); shot(base, n++);   /* 08 */
    btn(HB_BTN_GLO); M->draw(0); shot(base, n++);   /* 09 */
    btn(HB_BTN_EDIT); enc(HB_ENC_ALGORITHM, 1); M->draw(0); shot(base, n++);   /* 10: OSC, puls */
    CHECK(leds[HB_BTN_EDIT] == 1u && leds[HB_BTN_SEL] == 2u, "lysdioderna visar sidan");

    for (i = 0; i < 200; i++) enc(HB_ENC_PRESETS, 1);   /* runt och runt */
    for (i = 0; i < 200; i++) enc(HB_ENC_KNOB1 + (i & 3u), (int32_t)(i % 7u) - 3);
    for (i = 0; i < 40; i++) enc(HB_ENC_SELECT, 1);
    M->draw(1);
    CHECK(bad_draw == 0, "allt ritas inom skärmen och under värdens rad");

    /* alla ljud: spelar utan att klippa eller tystna */
    {
        int p, silent = 0, clip0 = clipped;
        for (p = 0; p < 90; p++) {
            M->midi(0xC0, (uint32_t)p, 0);
            peak = 0;
            M->note(0, 48, 110); M->note(0, 60, 100); M->note(0, 67, 100);
            audio(300);
            M->note(0, 48, 0); M->note(0, 60, 0); M->note(0, 67, 0);
            if (peak < 300) { printf("  tyst: ljud %d\n", p); silent++; }
            M->midi(0xB0, 123, 0);
            audio(30);
        }
        CHECK(silent == 0, "alla 90 ljud hörs");
        CHECK(clipped == clip0, "inget ljud går över full nivå");
    }

    /* MIDI: böj, modhjul, tryck, sustain, program change */
    M->midi(0xC0, 64, 0);                       /* MPE Lead */
    M->midi(0x90, 62, 100); M->midi(0xE0, 0, 127); M->midi(0xB0, 1, 127); M->midi(0xD0, 100, 0); audio(400);
    M->midi(0xE0, 0, 0); M->midi(0xB0, 64, 127); M->midi(0x80, 62, 0); audio(300);
    M->midi(0xB0, 64, 0); M->midi(0xE0, 0, 64); M->midi(0xB0, 1, 0); M->midi(0xD0, 0, 0); audio(1500);
    CHECK(1, "MIDI-meddelanden tas emot utan krasch");

    /* SAVE: sparas och kommer tillbaka vid nästa start */
    M->midi(0xC0, 7, 0);
    btn(HB_BTN_SEL); enc(HB_ENC_KNOB1, -15);
    btn(HB_BTN_SAVE);
    now_ms += 10; M->draw(0); shot(base, n++);  /* 11: SAVED */
    CHECK(store_n > 0 && store_n <= 240u, "inställningarna ryms i modulens 240 byte");
    M->release();
    CHECK(M->init(&HOST) == 0, "ny start");
    M->draw(1); shot(base, n++);                /* 12: samma ljud, ändrat (*) */
    now_ms += 2000; M->draw(0);

    snprintf(wpath, sizeof wpath, "%s.wav", base);
    write_wav(wpath);
    printf("%d skärmbilder, ljud i %s\n", n, wpath);
    if (fails) { printf("%d kontroller misslyckades\n", fails); return 1; }
    printf("alla kontroller ok\n");
    return 0;
}
