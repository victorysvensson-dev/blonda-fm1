/* SPDX-License-Identifier: GPL-3.0-only */
/* En låtsas-FM-1 för PULS-modulen: fm1-dock:s hb_host_t på datorn, med fm1-dock:s egna typsnitt (gfx.c).
 * Modulen (moduler/puls/puls_module.c) körs oförändrad: init, rattar, knappar, toner, render och draw, som
 * fm1-dock anropar dem. Ut kommer skärmbilder (PPM) och ljudet som modulen lämnar till värden (WAV, Q15).
 *   cc -O2 -I$FM1_DOCK/firmware/dock -I$FM1_DOCK/firmware/src -I$FM1_DOCK/firmware/gen -Isrc \
 *      test/modul_host.c puls_module.c src/puls_fm1.c ../../gemensamt/effekter/sliten_fm1.c -o build/puls-modul   (i moduler/puls)
 *   build/puls-modul build/skarm      -> build/skarm-NN.ppm, build/skarm.wav, och kontroller (felkod vid fel) */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "hb_module.h"
#include "puls_presets.h"               /* PULS_NPRESETS */

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

/* den andra kärnan: kör jobbet direkt (samma resultat som parallellt, rösterna delar inget tillstånd) */
static int c1_lend = 1, c1_jobs;
static int h_c1_run(void (*fn)(uint32_t), uint32_t arg) { if (!c1_lend) return 0; fn(arg); c1_jobs++; return 1; }
static int h_c1_wait(void) { return 0; }

static const hb_host_t HOST = {
    .abi = HB_ABI, .size = sizeof(hb_host_t), .ms = h_ms, .ticks = h_ticks, .log = h_log, .log_u32 = h_log_u32,
    .fill = h_fill, .text = h_text, .cv_begin = h_cv_begin, .cv_rect = cv_rect, .cv_line = cv_line, .cv_pixel = h_cv_pixel,
    .cv_text = h_cv_text, .cv_blit = h_cv_blit, .led = h_led, .rand = h_rand, .load = h_load, .save = h_save,
    .midi_send = h_midi_send, .master = h_master, .text_w = h_text_w, .c1_run = h_c1_run, .c1_wait = h_c1_wait,
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
    if (!wav) return;
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
static void keyp(uint32_t k, int32_t vel) { M->ui(HB_EV_KEY_DOWN | k, vel); M->ui(HB_EV_KEY_UP | k, 0); }
static const uint32_t WK[16] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};
static const uint32_t BK[8] = {1, 3, 5, 8, 10, 13, 15, 17};

/* ljud till en buffert (vänster kanal), block om 256 */
static int32_t *grab(int blocks, int32_t *pk)
{
    int32_t *b = malloc((size_t)blocks * 256 * sizeof *b), out[512];
    int i, j;
    *pk = 0;
    for (i = 0; i < blocks; i++) {
        memset(out, 0, sizeof out);
        M->render(out, 256);
        for (j = 0; j < 256; j++) {
            b[i * 256 + j] = out[2 * j];
            if (abs(out[2 * j]) > *pk) *pk = abs(out[2 * j]);
            if (abs(out[2 * j + 1]) > *pk) *pk = abs(out[2 * j + 1]);
            if (out[2 * j] > 32767 || out[2 * j] < -32767) clipped++;
        }
        now_ms += 6;
        if (i % 3 == 0) M->draw(0);
    }
    return b;
}
/* var slagen börjar: där ljudet blir starkt efter minst 2000 tysta sampel (tystnaden räknas fram till 16 sampel
 * före, så att slagets första klick inte räknas som att tystnaden redan är bruten) */
static int onsets(const int32_t *b, int n, int *at, int max)
{
    int i, k = 0, quiet = 1 << 30, qh[16], last = -100000;
    for (i = 0; i < 16; i++) qh[i] = 1 << 30;
    for (i = 0; i < n && k < max; i++) {
        if (abs(b[i]) > 1500 && qh[i & 15] > 2000 && i - last > 2000) { at[k++] = i; last = i; }
        quiet = abs(b[i]) < 60 ? quiet + 1 : 0;
        qh[i & 15] = quiet;
    }
    return k;
}
static void silence(int blocks) { int32_t pk; free(grab(blocks, &pk)); }
static void clear_pattern(void)                 /* tömmer det valda mönstret, ett trumljud i taget */
{
    int v, s;
    for (v = 0; v < 8; v++) {
        keyp(BK[v], 1);
        M->draw(0);
        for (s = 0; s < 16; s++) if (leds[HB_LED_KEY(WK[s])] == 1u) keyp(WK[s], 0);
    }
    M->draw(0);
}

int main(int argc, char **argv)
{
    const char *base = argc > 1 ? argv[1] : "build/skarm";
    char wpath[512];
    int n = 0, i, at[64], k;
    int32_t pk, *b;

    palette_set(0);
    CHECK(M->abi == HB_ABI && !strcmp(M->name, "PULS") && (M->flags & HB_F_PANEL), "beskrivningen: ABI, namn, hela panelen");
    CHECK(M->init(&HOST) == 0, "init");
    M->draw(1);
    shot(base, n++);                            /* 00: start, Klassisk, mönster 1 */
    CHECK(leds[HB_BTN_SEQ] == 1u && leds[HB_BTN_FX] == 1u && leds[HB_BTN_SEL] == 2u, "lysdioder: STEG-läge, mönster 1");
    CHECK(leds[HB_LED_KEY(WK[0])] == 1u && leds[HB_LED_KEY(WK[6])] == 1u && leds[HB_LED_KEY(WK[1])] == 0u,
          "de vita tangenterna visar bastrummans steg (1, 7, 9)");
    CHECK(leds[HB_LED_KEY(BK[0])] == 1u && leds[HB_LED_KEY(BK[1])] == 2u, "de svarta: BD vald, de andra glöder");

    /* PLAY: mönstret spelar */
    btn(HB_BTN_PLAY);
    b = grab(350, &pk);                         /* 2 s */
    shot(base, n++);                            /* 01: spelar */
    CHECK(pk > 3000, "PLAY: trummönstret hörs");
    CHECK(leds[HB_BTN_PLAY] == 1u, "PLAY lyser när det spelar");
    free(b);
    btn(HB_BTN_PLAY);
    silence(500);

    /* tiden: bara BD på steg 1 och 9 i det tomma mönstret 6, 120 BPM -> ett slag varje sekund (44100 sampel).
     * Trumset Tajt: bastrumman tystnar innan nästa slag, så slagen syns tydligt */
    M->midi(0xC0, 4, 0);
    btn(HB_BTN_GLO);
    clear_pattern();
    keyp(BK[0], 1);
    keyp(WK[0], 0); keyp(WK[8], 0);
    silence(300);
    btn(HB_BTN_PLAY);
    b = grab(900, &pk);                         /* ~5,2 s */
    btn(HB_BTN_PLAY);
    k = onsets(b, 900 * 256, at, 64);
    printf("  slag vid %d %d %d %d %d (sampel)\n", at[0], at[1], at[2], at[3], at[4]);
    {
        int ok = k >= 5;
        for (i = 1; i < k && i < 5; i++) if (abs(at[i] - at[i - 1] - 44100) > 2) ok = 0;
        CHECK(ok, "120 BPM: steg 1 och 9 kommer exakt en sekund isär (±2 sampel)");
    }
    free(b);
    silence(300);

    /* swing 100: steg 2 kommer 1,5 steg efter steg 1 (75 %); stängd hihat, som tystnar fort */
    clear_pattern();
    keyp(BK[4], 1); keyp(WK[0], 0); keyp(WK[1], 0);
    for (i = 0; i < 5; i++) enc(HB_ENC_SELECT, -1);   /* CH -> GLOBAL (bakåt) */
    enc(HB_ENC_KNOB2, 100);                     /* swing 100 */
    M->draw(0); shot(base, n++);                /* 02: GLOBAL-sidan */
    silence(300);
    btn(HB_BTN_PLAY);
    b = grab(200, &pk);
    btn(HB_BTN_PLAY);
    k = onsets(b, 200 * 256, at, 64);
    printf("  swing: slag vid %d %d (skillnad %d, väntat 8269)\n", at[0], at[1], k > 1 ? at[1] - at[0] : 0);
    CHECK(k >= 2 && abs(at[1] - at[0] - 8269) <= 2, "swing 100: andra sextondelen kommer på 75 %");
    free(b);
    enc(HB_ENC_KNOB2, -100);
    silence(300);

    /* accenter (ARP) */
    btn(HB_BTN_ARP);
    M->draw(0);
    CHECK(leds[HB_BTN_ARP] == 1u && leds[HB_LED_KEY(WK[0])] == 1u && leds[HB_LED_KEY(WK[4])] == 1u && leds[HB_LED_KEY(WK[1])] == 0u,
          "ARP: de vita tangenterna visar accenterna (1, 5, 9, 13)");
    keyp(WK[2], 0); M->draw(0);
    CHECK(leds[HB_LED_KEY(WK[2])] == 1u, "ARP: en vit tangent sätter accent");
    shot(base, n++);                            /* 03: accenter */
    btn(HB_BTN_ARP);

    /* REC: spelas in på närmaste steg */
    clear_pattern();
    keyp(BK[1], 1);                             /* SD vald */
    silence(200);
    btn(HB_BTN_REC); btn(HB_BTN_PLAY);
    silence(10);                                /* strax efter steg 1 */
    keyp(BK[1], 100);
    silence(170);                               /* ~1 s: steg 9 */
    keyp(BK[1], 100);
    M->draw(0);
    CHECK(leds[HB_LED_KEY(WK[0])] == 1u || leds[HB_LED_KEY(WK[1])] == 1u, "REC: virveln spelades in nära steg 1");
    btn(HB_BTN_PLAY); btn(HB_BTN_REC);
    M->draw(0); shot(base, n++);                /* 04: inspelat */
    silence(300);

    /* mönster: byts när takten är slut, kopieras med håll + tryck */
    btn(HB_BTN_FX);                             /* mönster 1 */
    btn(HB_BTN_PLAY);
    silence(40);
    btn(HB_BTN_SEL);                            /* mönster 2 i kö */
    M->draw(0);
    CHECK(leds[HB_BTN_FX] == 1u, "mönster 2 väntar tills takten är slut");
    silence(400);                               /* > 2 s: ny takt */
    M->draw(0);
    CHECK(leds[HB_BTN_SEL] == 1u && leds[HB_BTN_FX] == 2u, "mönster 2 spelar efter taktslutet");
    btn(HB_BTN_PLAY);
    M->ui(HB_EV_BTN_DOWN | HB_BTN_FX, 0);       /* håll 1, tryck 6: kopiera */
    M->ui(HB_EV_BTN_DOWN | HB_BTN_GLO, 0);
    M->ui(HB_EV_BTN_UP | HB_BTN_GLO, 0);
    M->ui(HB_EV_BTN_UP | HB_BTN_FX, 0);
    now_ms += 10; M->draw(0); shot(base, n++);  /* 05: COPIED */
    btn(HB_BTN_GLO); keyp(BK[0], 1); M->draw(0);
    CHECK(leds[HB_LED_KEY(WK[0])] == 1u && leds[HB_LED_KEY(WK[6])] == 1u, "mönster 1 kopierat till 6");
    silence(300);

    /* LIVE: tangenterna spelar trumljud som MIDI */
    btn(HB_BTN_SEQ);
    M->draw(0);
    CHECK(leds[HB_BTN_SEQ] == 2u, "SEQ släckt: LIVE");
    keyp(7, 110);                               /* C4: bastrumma */
    b = grab(60, &pk); free(b);
    CHECK(pk > 3000, "LIVE: C spelar bastrumman");
    shot(base, n++);                            /* 06: LIVE */
    btn(HB_BTN_SEQ);
    silence(300);

    /* alla trumset: mönster 1 spelar utan att klippa eller tystna */
    {
        int p, silent = 0, clip0 = clipped;
        btn(HB_BTN_FX);
        for (p = 0; p < PULS_NPRESETS; p++) {
            M->midi(0xC0, (uint32_t)p, 0);
            btn(HB_BTN_PLAY);
            b = grab(180, &pk); free(b);
            btn(HB_BTN_PLAY);
            if (pk < 2000) { printf("  tyst: trumset %d\n", p); silent++; }
            silence(250);
        }
        CHECK(silent == 0, "alla trumset hörs");
        CHECK(clipped == clip0, "inget går över full nivå");
    }

    /* rattarna: varje sida, runt och runt */
    for (i = 0; i < 9; i++) { enc(HB_ENC_SELECT, 1); enc(HB_ENC_KNOB1 + (uint32_t)(i & 3), 7); M->draw(0); }
    for (i = 0; i < 300; i++) enc(HB_ENC_KNOB1 + (uint32_t)(i & 3), (i % 9) - 4);
    for (i = 0; i < 40; i++) enc(HB_ENC_PRESETS, 1);
    for (i = 0; i < 50; i++) { btn(HB_BTN_OCT_UP); btn(HB_BTN_OCT_DOWN); }
    M->draw(1);
    CHECK(bad_draw == 0, "allt ritas inom skärmen och under värdens rad");

    /* MIDI: toner, program change, CC 77 */
    M->midi(0xC0, 5, 0);
    M->midi(0x90, 38, 120); M->midi(0x90, 42, 100); M->midi(0x80, 38, 0);
    b = grab(60, &pk); free(b);
    CHECK(pk > 2000, "MIDI-toner spelar trumljud");
    silence(300);

    /* ERA: TAPE och LOFI låter annorlunda, klipper inte, och MODERN är helt rent och tyst igen */
    {
        int32_t *e[3], q[3], ek, step[3] = {-100, 35, 50}, clip0 = clipped;
        double d1 = 0, d2 = 0, e0 = 0;
        btn(HB_BTN_FX);
        M->midi(0xC0, 1, 0);
        for (ek = 0; ek < 3; ek++) {
            enc(HB_ENC_ALGORITHM, step[ek]);
            silence(300);
            btn(HB_BTN_PLAY);
            e[ek] = grab(120, &pk);
            btn(HB_BTN_PLAY);
            if (ek) { M->draw(0); shot(base, n++); }   /* 07 TAPE 35, 08 LOFI 85 */
            silence(900);
            free(grab(60, &q[ek]));
        }
        for (i = 0; i < 120 * 256; i++) {
            double a0 = e[0][i];
            e0 += a0 * a0; d1 += (a0 - e[1][i]) * (a0 - e[1][i]); d2 += (a0 - e[2][i]) * (a0 - e[2][i]);
        }
        printf("  ERA: skillnad mot rent: TAPE %.1f dB, LOFI %.1f dB; tystnad efteråt %d, %d, %d\n",
               10 * log10(d1 / e0 + 1e-30), 10 * log10(d2 / e0 + 1e-30), q[0], q[1], q[2]);
        CHECK(10 * log10(d1 / e0 + 1e-30) > -20 && 10 * log10(d2 / e0 + 1e-30) > -20, "ERA: TAPE och LOFI låter annorlunda än rent");
        CHECK(clipped == clip0, "ERA: klipper inte");
        CHECK(q[1] > 0, "ERA TAPE: bandbruset hörs i tystnaden");
        enc(HB_ENC_ALGORITHM, -100);
        silence(400);
        free(grab(60, &q[0]));
        CHECK(q[0] == 0, "ERA tillbaka på MODERN: helt tyst igen");
        for (ek = 0; ek < 3; ek++) free(e[ek]);
    }

    /* SAVE: allt kommer tillbaka vid nästa start */
    M->midi(0xC0, 3, 0);                        /* Boom bap */
    btn(HB_BTN_ENV);                            /* mönster 3 */
    keyp(BK[2], 1); keyp(WK[5], 0);             /* RS på steg 6 */
    for (i = 0; i < 7; i++) btn(HB_BTN_OCT_UP); /* 127 BPM */
    enc(HB_ENC_ALGORITHM, 30);
    btn(HB_BTN_SAVE);
    now_ms += 10; M->draw(0); shot(base, n++);  /* 09: SAVED */
    printf("  sparat: %u byte\n", store_n);
    CHECK(store_n > 0 && store_n <= 240u, "allt ryms i modulens 240 byte");
    M->release();
    CHECK(M->init(&HOST) == 0, "ny start");
    for (i = 0; i < 64; i++) leds[i] = 0;
    M->draw(1); shot(base, n++);                /* 10: samma trumset, mönster, tempo, TAPE 30 */
    CHECK(leds[HB_BTN_ENV] == 1u && leds[HB_LED_KEY(WK[5])] == 1u, "mönster 3 med kantslaget på steg 6 kom tillbaka");

    snprintf(wpath, sizeof wpath, "%s.wav", base);
    write_wav(wpath);
    printf("%d skärmbilder, ljud i %s\n", n, wpath);
    if (fails) { printf("%d kontroller misslyckades\n", fails); return 1; }
    printf("alla kontroller ok\n");
    return 0;
}
