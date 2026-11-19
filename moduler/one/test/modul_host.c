/* SPDX-License-Identifier: GPL-3.0-only */
/* En låtsas-FM-1 för BLONDA ONE: fm1-dock:s hb_host_t på datorn, med fm1-dock:s egna typsnitt (gfx.c).
 * Modulen (moduler/one/one_module.c) körs oförändrad: init, rattar, knappar, toner, render och draw, som
 * fm1-dock anropar dem. Ut kommer skärmbilder (PPM) och ljudet som modulen lämnar till värden (WAV, Q15),
 * och kontroller av båda motorerna, bytet mellan dem, reverbet, pumpen, ERA, MIDI och SAVE (felkod vid fel).
 *   sh kolla.sh bygger och kör den:  build/one-modul build/skarm/s  -> build/skarm/s-NN.ppm, build/skarm/s.wav */
#include <stdint.h>
#include <fenv.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "hb_module.h"
#include "one_presets.h"
#include "bris_presets.h"
#include "blonda_dot.h"

#define NPRESETS (ONE_NPRESETS + BRIS_NPRESETS)
#define BRIS0 (ONE_NPRESETS - 1)                /* BRIS-ljud nr i (1..89) har nummer BRIS0 + i; BRIS init är sist */
#define NPMAX ((int)OP_N > (int)BP_N ? (int)OP_N : (int)BP_N)
#define NX 24                                   /* modulens egna parametrar i det sparade: reverb 12, pump 4, ARP 4, SEQ 3, tryck 1 */
#define SET_N (8 + NPMAX * 4 + 4 + NX + 4)      /* det sparade: 232 byte */

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
static int seen_ext;                            /* en text som börjar med "EXT " har ritats (pumpen följer MIDI-klockan) */
static const char *look;                        /* en text att leta efter bland det som ritas ... */
static int found;                               /* ... och om den ritades */
static void h_text(int32_t x, int32_t y, int32_t w, uint32_t big, const char *s, uint16_t fg, uint32_t center)
{
    if (!strncmp(s, "EXT ", 4)) seen_ext = 1;
    if (look && !strcmp(s, look)) found = 1;
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
/* modulens flashbank (fm1-dock: 8 KiB med CRC): det som sparats, eller inget */
static uint8_t bank[8176];
static uint32_t bank_n, bank_saves;
static int h_bank_load(void *p, uint32_t n) { if (!bank_n || n != bank_n) return -1; memcpy(p, bank, n); return 0; }
static int h_bank_save(const void *p, uint32_t n) { if (n > sizeof bank) return -1; memcpy(bank, p, n); bank_n = n; bank_saves++; return 0; }
static uint32_t h_master(void) { return 4096u; }

/* den andra kärnan: kör jobbet direkt (samma resultat som parallellt, rösterna delar inget tillstånd) */
static int c1_lend = 1, c1_jobs;
static int c1_drop_at, c1_dropped;              /* test: jobb nummer c1_drop_at (från nu) blir aldrig kört, och c1_wait svarar -1 */
static int h_c1_run(void (*fn)(uint32_t), uint32_t arg)
{
    if (!c1_lend) return 0;
    c1_jobs++;
    if (c1_drop_at && --c1_drop_at == 0) { c1_dropped = 1; return 1; }
    fn(arg);
    return 1;
}
static int h_c1_wait(void) { if (c1_dropped) { c1_dropped = 0; return -1; } return 0; }

static const hb_host_t HOST = {
    .abi = HB_ABI, .size = sizeof(hb_host_t), .ms = h_ms, .ticks = h_ticks, .log = h_log, .log_u32 = h_log_u32,
    .fill = h_fill, .text = h_text, .cv_begin = h_cv_begin, .cv_rect = cv_rect, .cv_line = cv_line, .cv_pixel = h_cv_pixel,
    .cv_text = h_cv_text, .cv_blit = h_cv_blit, .led = h_led, .rand = h_rand, .load = h_load, .save = h_save,
    .midi_send = h_midi_send, .master = h_master, .text_w = h_text_w, .c1_run = h_c1_run, .c1_wait = h_c1_wait,
    .bank_load = h_bank_load, .bank_save = h_bank_save,
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
/* ljud nr p med program change; över 127 med bank 1 (CC 0) */
static void prog(int p)
{
    M->midi(0xB0, 0, (uint32_t)(p >> 7));
    M->midi(0xC0, (uint32_t)(p & 127), 0);
    M->midi(0xB0, 0, 0);
}
/* ritar om allt och svarar om texten t ritades (hela raden) */
static int drawn(const char *t)
{
    look = t; found = 0;
    M->draw(1);
    look = 0;
    return found;
}
/* Står namnet t i prickdisplayen? Varje prick i varje tecken läses av på skärmen (namnfönstret börjar vid 4, 46):
 * tänd där tecknet har en prick, släckt annars, och ingenting tänt efter namnet. */
static int name_shows(const char *t)
{
    const uint16_t yel = HB_RGB(0xE9, 0xC5, 0x4A);
    int len1, start2, pitch = dot_layout(t, &len1, &start2), lines = start2 ? 2 : 1, line, i, gr, gc, x, y, n;
    M->draw(1);
    for (line = 0; line < lines; line++) {
        const char *u = line ? t + start2 : t;
        n = line ? (int)strlen(u) : len1;
        for (i = 0; i <= n; i++) {                           /* (ett tecken till: tomt efter namnet) */
            const uint8_t *g = i < n ? dot_glyph(u[i]) : 0;
            if (i == n && (i * 6 + 5) * pitch + 2 >= DOT_W) break;
            for (gr = 0; gr < 7; gr++)
                for (gc = 0; gc < 5; gc++) {
                    int want = g ? g[gr] >> (4 - gc) & 1 : 0;
                    dot_at(pitch, lines, line, i, gc, gr, &x, &y);
                    if ((fb[(46 + y) * 240 + 4 + x] == yel) != want) return 0;
                }
        }
    }
    return 1;
}
/* som drawn, men ritar bara det som ändrats (som fm1-dock gör mellan knapptryckningarna) */
static int redrawn(const char *t)
{
    look = t; found = 0;
    M->draw(0);
    look = 0;
    return found;
}
static int32_t blocks_peak(int blocks)
{
    int32_t out[512], pk = 0;
    int b, i;
    for (b = 0; b < blocks; b++) {
        memset(out, 0, sizeof out); M->render(out, 256);
        for (i = 0; i < 512; i++) if (abs(out[i]) > pk) pk = abs(out[i]);
    }
    return pk;
}

int main(int argc, char **argv)
{
    const char *base = argc > 1 ? argv[1] : "build/skarm";
    const char *base_name = base;
    char wpath[512];
    int n = 0;
    uint32_t i;
    int32_t peak_quiet;

    palette_set(0);
    CHECK(M->abi == HB_ABI && !strcmp(M->name, "ONE"), "beskrivningen: ABI och namn");
    CHECK(M->init(&HOST) == 0, "init");
    M->draw(1);
    shot(base, n++);                            /* 00: start, one (3 OSC) */
    CHECK(drawn("001") && drawn("3 OSC V2.2"), "start: ljud 001 med trioscillatormotorn, versionen visas");
    CHECK(name_shows("one") && !name_shows("ono") && !name_shows("on"), "namnet står i prickdisplayen, prick för prick (ONE)");

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

    /* ---- trioscillatormotorns sidor */
    enc(HB_ENC_PRESETS, 1);                     /* moogbas */
    M->draw(0);
    shot(base, n++);                            /* 02 */
    enc(HB_ENC_KNOB1, -5); enc(HB_ENC_KNOB4, 30);    /* filtersidan visas från start */
    M->note(0, 36, 120); audio(800); M->note(0, 36, 0); audio(400);
    M->draw(0);
    shot(base, n++);                            /* 03: filtersidan, ändrat (*): cutoff ner, drive upp */
    btn(HB_BTN_SEL); M->draw(0); shot(base, n++);    /* 04: KEYS (SEL igen: sidans andra halva) */
    btn(HB_BTN_EDIT); M->draw(0); shot(base, n++);   /* 05: OSC 1 */
    CHECK(leds[HB_BTN_EDIT] == 1u && leds[HB_BTN_SEL] == 2u, "lysdioderna visar sidan");
    btn(HB_BTN_EDIT); enc(HB_ENC_KNOB1, 1); enc(HB_ENC_KNOB3, 25); M->draw(0); shot(base, n++);   /* 06: OSC 2, puls, finstämd */
    btn(HB_BTN_EDIT); enc(HB_ENC_KNOB2, 3); M->draw(0); shot(base, n++);   /* 07: OSC 3, fotläge upp */
    btn(HB_BTN_EDIT); enc(HB_ENC_KNOB1, 7); enc(HB_ENC_KNOB2, -5); enc(HB_ENC_KNOB3, 40); M->draw(0); shot(base, n++);   /* 08: MIX */
    btn(HB_BTN_EDIT); M->draw(0);                    /* runt till OSC 1 igen */
    M->note(0, 48, 110); audio(500); M->note(0, 48, 0); audio(300);
    btn(HB_BTN_ENV); M->draw(0); shot(base, n++);    /* 09: EG2 */
    btn(HB_BTN_ENV); M->draw(0); shot(base, n++);    /* 10: EG1 */
    btn(HB_BTN_LFO); M->draw(0); shot(base, n++);    /* 11 */
    btn(HB_BTN_LFO); M->draw(0); shot(base, n++);    /* 12 */
    btn(HB_BTN_FX); M->draw(0); shot(base, n++);     /* 13: FX */
    btn(HB_BTN_FX); CHECK(drawn("RUM  2/5"), "3 OSC: reverbets sidor finns på FX");
    btn(HB_BTN_GLO); M->draw(0); shot(base, n++);    /* 14: VOICE */
    CHECK(drawn("VOICE  1/2"), "3 OSC: VOICE och TOUCH på GLO");
    CHECK(name_shows("moogbas*"), "ändrat ljud: stjärnan i prickdisplayen (MOOGBAS*)");

    /* ---- BRIS-motorns sidor, och reverbets och pumpens */
    prog(BRIS0 + 5);                            /* Unison Bass */
    M->draw(0);
    shot(base, n++);                            /* 15 */
    CHECK(drawn("054") && drawn("BRIS V2.2"), "ljud 054 hör till BRIS-motorn");
    CHECK(drawn("VOICE  1/3"), "motorbyte: samma knapp, BRIS-motorns sidor (VOICE 1/3)");
    CHECK(name_shows("Unison Bass"), "elva tecken: mindre prickar, en rad (UNISON BASS)");
    btn(HB_BTN_SEL); enc(HB_ENC_KNOB1, -20); enc(HB_ENC_KNOB2, 30);
    M->note(0, 36, 120); audio(800); M->note(0, 36, 0); audio(400);
    M->draw(0);
    shot(base, n++);                            /* 16: filtersidan, ändrat (*) */
    btn(HB_BTN_ENV); M->draw(0); shot(base, n++);   /* 17: EG2 */
    btn(HB_BTN_ENV); M->draw(0); shot(base, n++);   /* 18: EG1 */
    btn(HB_BTN_LFO); M->draw(0); shot(base, n++);   /* 19 */
    btn(HB_BTN_FX); M->draw(0); shot(base, n++);    /* 20: FX */
    btn(HB_BTN_FX); enc(HB_ENC_KNOB1, 35); enc(HB_ENC_KNOB4, 1); M->draw(0); shot(base, n++);    /* 21: RUM, mix 35, ROOM */
    enc(HB_ENC_KNOB4, -1);
    btn(HB_BTN_FX); M->draw(0); shot(base, n++);    /* 22: RUM TONE */
    prog(40);                                        /* ekopluck (3 OSC): sidan ligger kvar, reverbet också */
    CHECK(drawn("RUM TONE  3/5") && drawn("3 OSC V2.2"), "motorbyte: reverbets sida ligger kvar");
    prog(BRIS0 + 5);
    CHECK(drawn("RUM TONE  3/5"), "och tillbaka");
    btn(HB_BTN_FX); M->draw(0); shot(base, n++);    /* 23: RUM SPACE */
    btn(HB_BTN_FX); enc(HB_ENC_KNOB1, 60); enc(HB_ENC_KNOB2, 1); enc(HB_ENC_KNOB3, 1); enc(HB_ENC_KNOB4, 4); M->draw(0); shot(base, n++);   /* 24: PUMP */
    enc(HB_ENC_KNOB1, -100); enc(HB_ENC_KNOB3, -1);
    btn(HB_BTN_FX);                                  /* runt till FX igen */
    btn(HB_BTN_FX); enc(HB_ENC_KNOB1, -100);         /* reverbet av igen */
    btn(HB_BTN_GLO); M->draw(0); shot(base, n++);   /* 25 */
    btn(HB_BTN_GLO); M->draw(0); shot(base, n++);   /* 26 */
    btn(HB_BTN_EDIT); M->draw(0); shot(base, n++);   /* 27: OSC */
    CHECK(leds[HB_BTN_EDIT] == 1u && leds[HB_BTN_SEL] == 2u, "lysdioderna visar sidan");
    prog(2); btn(HB_BTN_EDIT); btn(HB_BTN_EDIT);     /* moogbas: OSC 3 */
    CHECK(drawn("OSC 3  3/4"), "3 OSC: fyra sidor på EDIT");
    prog(BRIS0 + 5);
    CHECK(drawn("OSC") && leds[HB_BTN_EDIT] == 1u, "motorbyte från OSC 3: BRIS enda OSC-sida");

    /* Samma sidnummer i båda motorerna (FILTER är sida 0) men olika innehåll: allt ritas om vid motorbytet, också
     * när bara det som ändrats ritas. 3 OSC har två sidor på SEL och DRIVE på fjärde raden, BRIS en sida och tangentföljning. */
    prog(2); btn(HB_BTN_SEL); btn(HB_BTN_SEL); btn(HB_BTN_SEL);
    CHECK(drawn("FILTER  1/2") && drawn("Drive"), "3 OSC: FILTER 1/2 med DRIVE");
    prog(BRIS0 + 5);
    CHECK(redrawn("FILTER"), "motorbyte på samma sidnummer: sidans namn ritas om (FILTER, en sida)");
    prog(2); M->draw(0); prog(BRIS0 + 5);
    CHECK(redrawn("Filter Key Track"), "motorbyte på samma sidnummer: raderna ritas om");
    prog(2);
    CHECK(redrawn("FILTER  1/2") , "och tillbaka: FILTER 1/2");

    /* PRESETS-ratten går runt alla 140, och banken (CC 0) når ljuden över 127 */
    prog(0); enc(HB_ENC_PRESETS, -1);
    CHECK(drawn("139") && drawn("BRIS V2.2") && name_shows("init"), "PRESETS bakåt från 000: 139, BRIS init");
    enc(HB_ENC_PRESETS, 1);
    CHECK(drawn("000") && drawn("3 OSC V2.2"), "och framåt: 000 igen");
    M->midi(0xB0, 0, 1); M->midi(0xC0, 10, 0);
    CHECK(drawn("138"), "MIDI: bank 1, program 10 = ljud 138");
    M->midi(0xC0, 12, 0);
    CHECK(drawn("138"), "MIDI: ett nummer över det sista ljudet gör ingenting");
    M->midi(0xB0, 0, 0); M->midi(0xC0, 49, 0);
    CHECK(drawn("049") && drawn("3 OSC V2.2"), "MIDI: bank 0, program 49 = sista ljudet i trioscillatormotorn");
    M->midi(0xC0, 50, 0);
    {   /* de längsta namnen: två rader */
        int p, two = 0, bad = 0;
        for (p = 0; p < NPRESETS; p++) {
            const char *nm = p < ONE_NPRESETS ? ONE_PRESETS[p].name : BRIS_PRESETS[p == NPRESETS - 1 ? 0 : p - ONE_NPRESETS + 1].name;
            int l1, s2;
            prog(p);
            dot_layout(nm, &l1, &s2);
            if (s2) two++;
            if (!name_shows(nm)) { printf("  namnet syns inte rätt: %d %s\n", p, nm); bad++; }
        }
        CHECK(bad == 0 && two >= 2, "alla 140 namn står rätt i prickdisplayen, de längsta på två rader");
        prog(50);
    }
    CHECK(drawn("050") && drawn("BRIS V2.2") && name_shows("Poly Magic"), "MIDI: program 50 = BRIS första ljud");

    for (i = 0; i < 200; i++) enc(HB_ENC_PRESETS, 1);   /* runt och runt */
    for (i = 0; i < 200; i++) enc(HB_ENC_KNOB1 + (i & 3u), (int32_t)(i % 7u) - 3);
    for (i = 0; i < 40; i++) enc(HB_ENC_SELECT, 1);
    M->draw(1);
    CHECK(bad_draw == 0, "allt ritas inom skärmen och under värdens rad");

    /* alla ljud: spelar utan att klippa eller tystna */
    {
        int p, silent = 0, clip0 = clipped;
        for (p = 0; p < NPRESETS; p++) {
            prog(p);
            peak = 0;
            M->note(0, 48, 110); M->note(0, 60, 100); M->note(0, 67, 100);
            audio(300);
            M->note(0, 48, 0); M->note(0, 60, 0); M->note(0, 67, 0);
            if (peak < 300) { printf("  tyst: ljud %d\n", p); silent++; }
            M->midi(0xB0, 123, 0);
            audio(30);
        }
        CHECK(NPRESETS == 140 && silent == 0, "alla 140 ljud hörs");
        CHECK(clipped == clip0, "inget ljud går över full nivå");
    }

    /* MIDI: böj, modhjul, tryck, sustain, program change */
    {
        int e;
        for (e = 0; e < 2; e++) {
            prog(e ? BRIS0 + 64 : 22);          /* sträv lead (3 OSC), MPE Lead (BRIS) */
            M->midi(0x90, 62, 100); M->midi(0xE0, 0, 127); M->midi(0xB0, 1, 127); M->midi(0xD0, 100, 0); audio(400);
            M->midi(0xE0, 0, 0); M->midi(0xB0, 64, 127); M->midi(0x80, 62, 0); audio(300);
            M->midi(0xB0, 64, 0); M->midi(0xE0, 0, 64); M->midi(0xB0, 1, 0); M->midi(0xD0, 0, 0); audio(1500);
        }
        CHECK(1, "MIDI-meddelanden tas emot utan krasch");
    }

    /* ---- byte av motor medan det låter */
    {   /* 1. Inget knäpp: en mjuk, stark baston (init med stängt filter) klipps inte av tvärt. Största steget mellan
         *    två sampel under bytet är inte större än i tonen själv (ett tvärt slut vore hela utslaget på en gång). */
        int32_t out[512], prev = 0, d0 = 0, d1 = 0, pk0 = 0;
        int b, i2;
        M->release(); M->init(&HOST);
        prog(0); M->midi(0xB0, 74, 45);
        M->note(0, 36, 127);
        for (b = 0; b < 80; b++) {
            memset(out, 0, sizeof out); M->render(out, 256);
            for (i2 = 0; i2 < 256; i2++) {
                int32_t v = out[2 * i2];
                if (b >= 40) { if (abs(v - prev) > d0) d0 = abs(v - prev); if (abs(v) > pk0) pk0 = abs(v); }
                prev = v;
            }
        }
        prog(NPRESETS - 1);                     /* BRIS init: tonen hålls kvar på tangenten */
        for (b = 0; b < 4; b++) {
            memset(out, 0, sizeof out); M->render(out, 256);
            for (i2 = 0; i2 < 256; i2++) { int32_t v = out[2 * i2]; if (abs(v - prev) > d1) d1 = abs(v - prev); prev = v; }
        }
        printf("  motorbyte under en ton: topp %d, största steg i tonen %d, under bytet %d; efter 0,3 s: %d\n", pk0, d0, d1, blocks_peak(50));
        CHECK(pk0 > 2000 && d1 <= d0 + d0 / 2, "motorbyte: den gamla motorns ton tonas ut utan knäpp");
        CHECK(blocks_peak(20) == 0, "motorbyte: ingen ton hänger kvar i den gamla motorn (tangenten hölls nere)");
        M->note(0, 36, 0);
        M->note(0, 60, 100);
        CHECK(blocks_peak(20) > 1000, "motorbyte: den nya motorn spelar");
        M->note(0, 60, 0);
        prog(0);
        blocks_peak(4);
        CHECK(blocks_peak(400) >= 0 && blocks_peak(20) == 0, "och tillbaka: den första motorn är tyst (rösterna nollställda)");
    }
    {   /* 2. Reverbet och ekot klingar vidare över bytet */
        int32_t t0, t1;
        M->release(); M->init(&HOST);
        prog(40);                               /* ekopluck (3 OSC) */
        M->midi(0xB0, 91, 80);
        M->note(0, 60, 120); blocks_peak(30); M->note(0, 60, 0);
        prog(BRIS0 + 7);                        /* Ensemble (BRIS) */
        blocks_peak(10);
        t0 = blocks_peak(60);                   /* 60-400 ms efter bytet */
        blocks_peak(6000);
        t1 = blocks_peak(40);
        printf("  motorbyte med klang: 0,06-0,4 s efter bytet %d; efter 35 s %d\n", t0, t1);
        CHECK(t0 > 300 && t1 == 0, "motorbyte: klangen från den gamla motorns ton ligger kvar och dör ut");
    }
    {   /* 3. Böj och modhjul följer med till den nya motorn: samma ljud som om de kom efter bytet */
        static int32_t keep[3][256 * 2 * 40];
        int pass, b, i2;
        double e = 0, d01 = 0, d02 = 0;
        for (pass = 0; pass < 3; pass++) {
            M->release(); M->init(&HOST);
            if (pass == 0) prog(BRIS0 + 64);
            if (pass < 2) { M->midi(0xE0, 0, 127); M->midi(0xB0, 1, 127); }
            if (pass > 0) prog(BRIS0 + 64);
            M->note(0, 62, 100);
            for (b = 0; b < 40; b++) { memset(keep[pass] + b * 512, 0, 512 * 4); M->render(keep[pass] + b * 512, 256); }
        }
        for (i2 = 0; i2 < 256 * 2 * 40; i2++) {
            e += (double)keep[0][i2] * keep[0][i2];
            d01 += (double)(keep[0][i2] - keep[1][i2]) * (keep[0][i2] - keep[1][i2]);
            d02 += (double)(keep[0][i2] - keep[2][i2]) * (keep[0][i2] - keep[2][i2]);
        }
        printf("  böj och hjul före bytet mot efter: %.1f dB; utan böj och hjul: %.1f dB\n", 10 * log10(d01 / e + 1e-30), 10 * log10(d02 / e + 1e-30));
        CHECK(e > 0 && d01 == 0 && d02 > e * 0.01, "motorbyte: böj och modhjul följer med");
    }
    M->release(); M->init(&HOST);

    /* ERA (ALGORITHM): MODERN -> TAPE -> LOFI ändrar ljudet, hörs, klipper inte, och 0 är helt rent igen
     * (efter en stund utan toner: BRIS ensam blir helt tyst, med bandet kvar hörs brus) */
    {
        static int32_t keep[3][256 * 2 * 40];
        int32_t pk[3], quiet[3], i2, clip0 = clipped;
        const int32_t steg[3] = {-100, 35, 50};      /* MODERN, TAPE 35, LOFI 85 */
        int k, b;
        prog(BRIS0 + 1);                             /* Poly Magic */
        for (k = 0; k < 3; k++) {
            int32_t out[512];
            enc(HB_ENC_ALGORITHM, steg[k]);
            M->midi(0xB0, 123, 0);
            for (b = 0; b < 300; b++) { memset(out, 0, sizeof out); M->render(out, 256); }
            M->note(0, 57, 100); M->note(0, 64, 100);
            pk[k] = 0;
            for (b = 0; b < 40; b++) {
                int32_t *o = keep[k] + b * 512;
                memset(o, 0, 512 * 4); M->render(o, 256);
                for (i2 = 0; i2 < 512; i2++) { if (o[i2] > 32767 || o[i2] < -32767) clipped++; if (abs(o[i2]) > pk[k]) pk[k] = abs(o[i2]); }
            }
            M->note(0, 57, 0); M->note(0, 64, 0);
            if (k) { M->draw(0); shot(base_name, n++); }   /* 28: TAPE 35, 29: LOFI 85 */
            for (b = 0; b < 1200; b++) { memset(out, 0, sizeof out); M->render(out, 256); }   /* ~7 s: eko och hölje klingar ut */
            quiet[k] = 0;
            for (b = 0; b < 80; b++) { memset(out, 0, sizeof out); M->render(out, 256); for (i2 = 0; i2 < 512; i2++) if (abs(out[i2]) > quiet[k]) quiet[k] = abs(out[i2]); }
        }
        {
            double d1 = 0, d2 = 0, e0 = 0;
            for (i2 = 0; i2 < 256 * 2 * 40; i2++) {
                double a0 = keep[0][i2];
                e0 += a0 * a0;
                d1 += (a0 - keep[1][i2]) * (a0 - keep[1][i2]);
                d2 += (a0 - keep[2][i2]) * (a0 - keep[2][i2]);
            }
            printf("  ERA: topp MODERN %d, TAPE %d, LOFI %d; skillnad mot rent: TAPE %.1f dB, LOFI %.1f dB; tystnad efteråt: %d, %d, %d\n",
                   pk[0], pk[1], pk[2], 10 * log10(d1 / e0 + 1e-30), 10 * log10(d2 / e0 + 1e-30), quiet[0], quiet[1], quiet[2]);
            CHECK(10 * log10(d1 / e0 + 1e-30) > -20 && 10 * log10(d2 / e0 + 1e-30) > -20, "ERA: TAPE och LOFI låter annorlunda än rent");
            CHECK(pk[1] > 1000 && pk[2] > 1000 && clipped == clip0, "ERA: hörs och klipper inte");
            CHECK(quiet[1] > 0, "ERA TAPE: bandbruset hörs i tystnaden");
        }
        {   /* tillbaka till 0: bandet tonar ut och slutar räknas, tyst som BRIS ensam */
            int32_t out[512], q = 0;
            enc(HB_ENC_ALGORITHM, -100);
            for (b = 0; b < 400; b++) { memset(out, 0, sizeof out); M->render(out, 256); }
            for (b = 0; b < 80; b++) { memset(out, 0, sizeof out); M->render(out, 256); for (i2 = 0; i2 < 512; i2++) if (abs(out[i2]) > q) q = abs(out[i2]); }
            CHECK(quiet[0] == 0 && q == 0, "ERA tillbaka på MODERN: helt rent och tyst igen");
        }
    }

    /* RUM (reverbet): av = motorn orörd (testerna ovan); på = klangen ligger kvar efter tonen, klipper inte, och dör
     * ut till exakt tystnad. Mäts som topp per block efter att tonen släppts. */
    {
        int32_t out[512], after[2] = {0, 0}, late = 0, pk6 = 0, clip0 = clipped;
        int pass, b, i2, e2;
        for (pass = 0; pass < 2; pass++) {
            M->release(); M->init(&HOST);
            prog(BRIS0 + 5);                             /* Unison Bass: kort, torrt ljud utan eko */
            M->midi(0xB0, 91, pass ? 64 : 0);           /* reverbets mix: av, sedan halv */
            M->note(0, 48, 110);
            for (b = 0; b < 40; b++) { memset(out, 0, sizeof out); M->render(out, 256); }
            M->note(0, 48, 0);
            for (b = 0; b < 170; b++) {                  /* 1 s efter släpp */
                memset(out, 0, sizeof out); M->render(out, 256);
                if (b >= 120) for (i2 = 0; i2 < 512; i2++) if (abs(out[i2]) > after[pass]) after[pass] = abs(out[i2]);
            }
            if (pass) {
                for (b = 0; b < 4000; b++) { memset(out, 0, sizeof out); M->render(out, 256); }   /* 23 s till */
                for (b = 0; b < 40; b++) { memset(out, 0, sizeof out); M->render(out, 256); for (i2 = 0; i2 < 512; i2++) if (abs(out[i2]) > late) late = abs(out[i2]); }
                M->midi(0xB0, 91, 127);                  /* bara klang, sex toner hårt, i båda motorerna */
                for (e2 = 0; e2 < 2; e2++) {
                    int32_t pk = 0;
                    prog(e2 ? 28 : BRIS0 + 1);           /* Poly Magic (BRIS), stor poly (3 OSC) */
                    M->note(0, 36, 127); M->note(0, 48, 127); M->note(0, 55, 127); M->note(0, 60, 127); M->note(0, 64, 127); M->note(0, 67, 127);
                    for (b = 0; b < 500; b++) { memset(out, 0, sizeof out); M->render(out, 256); for (i2 = 0; i2 < 512; i2++) { if (out[i2] > 32767 || out[i2] < -32767) clipped++; if (abs(out[i2]) > pk) pk = abs(out[i2]); } }
                    M->midi(0xB0, 123, 0);
                    if (!e2 || pk < pk6) pk6 = pk;       /* den svagaste av de två */
                }
            }
        }
        printf("  RUM: 0,7-1 s efter släpp: utan reverb %d, med %d; efter 24 s: %d; sex toner med full mix: topp %d (svagaste motorn)\n", after[0], after[1], late, pk6);
        CHECK(after[0] < 30 && after[1] > 300, "RUM: klangen ligger kvar efter tonen (och utan reverb är det tyst)");
        CHECK(late == 0, "RUM: klangen dör ut till helt tyst");
        CHECK(pk6 > 3000 && clipped == clip0, "RUM: full mix med sex toner hörs och klipper inte (båda motorerna)");
    }
    /* PUMP: allt duckar på slaget. 120 BPM, fjärdedelar, fullt djup, mål ALL: nivån strax efter varje slag (var
     * 0,5 s från start) är mycket lägre än en kvarts sekund senare. Med mål REVERB rörs inte det torra ljudet. */
    {
        int32_t out[512];
        int target, b, i2;
        double lo[2] = {0, 0}, hi[2] = {0, 0};
        for (target = 1; target >= 0; target--) {
            M->release(); M->init(&HOST);
            prog(0);                                     /* init (3 OSC): en ton som står kvar */
            btn(HB_BTN_FX); btn(HB_BTN_FX); btn(HB_BTN_FX); btn(HB_BTN_FX); btn(HB_BTN_FX);   /* PUMP-sidan */
            enc(HB_ENC_KNOB1, 100); enc(HB_ENC_KNOB3, target ? 1 : -1);
            M->note(0, 60, 100);
            for (b = 0; b < 86 * 6; b++) {               /* sex slag */
                double e = 0;
                memset(out, 0, sizeof out); M->render(out, 256);
                for (i2 = 0; i2 < 512; i2++) e += (double)out[i2] * out[i2];
                if (b >= 86 * 2) {                       /* efter anslaget */
                    int at = b % 86;                     /* block sedan slaget (86 block = 0,499 s) */
                    if (at >= 2 && at <= 4) lo[target] += e;
                    if (at >= 60 && at <= 62) hi[target] += e;
                }
            }
            M->note(0, 60, 0);
            if (target) { M->draw(0); shot(base, n++); }   /* 30: PUMP-sidan, ALL */
        }
        printf("  PUMP: nivå på slaget mot mellan slagen: mål ALL %.1f dB, mål REVERB (reverbet av) %.1f dB\n",
               10 * log10(lo[1] / hi[1] + 1e-30), 10 * log10(lo[0] / hi[0] + 1e-30));
        CHECK(10 * log10(lo[1] / hi[1] + 1e-30) < -12, "PUMP: allt duckar på slaget (mål ALL)");
        CHECK(fabs(10 * log10(lo[0] / hi[0] + 1e-30)) < 1.0, "PUMP: mål REVERB rör inte det torra ljudet");
    }
    /* MIDI-klocka: pumpen följer den, och skärmen visar klockans takt (EXT) */
    {
        int32_t out[512];
        int b, t = 0;
        M->release(); M->init(&HOST);
        btn(HB_BTN_FX); btn(HB_BTN_FX); btn(HB_BTN_FX); btn(HB_BTN_FX); btn(HB_BTN_FX);
        M->midi(0xFA, 0, 0);
        for (b = 0; b < 700; b++) {                      /* 4 s; klockan i 100 BPM räknat i värdens millisekunder */
            while (t <= (int)(b * 6)) { M->midi(0xF8, 0, 0); t += 25; }
            memset(out, 0, sizeof out); M->render(out, 256);
            now_ms += 6;
        }
        seen_ext = 0; M->draw(1);
        CHECK(seen_ext, "MIDI-klocka: skärmen visar klockans takt (EXT)");
        shot(base, n++);                                 /* 31: PUMP med klocka */
        for (b = 0; b < 500; b++) { memset(out, 0, sizeof out); M->render(out, 256); now_ms += 6; }   /* 3 s utan klocka */
        seen_ext = 0; M->draw(1);
        CHECK(!seen_ext, "klockan tystnar: egen takt igen");
    }

    /* två kärnor: samma ljud som allt på en kärna (summan av rösterna i en annan ordning: avrundning bara), i båda
     * motorerna. Med reverbet på räknas klangen också på kärna 1. Då blir skillnaden större: den lilla
     * avrundningsskillnaden i rösterna gör att reverbets 16-bitarslinjer avrundar olika, så de två körningarna
     * skiljer sig med reverbets eget avrundningsbrus (långt under klangen). */
    {
        static int32_t one[2][256 * 2 * 60];
        int pass, b, i2, fx, e2;
        for (e2 = 0; e2 < 2; e2++) for (fx = 0; fx < 2; fx++) {
            double e = 0, d = 0, db;
            int jobs0 = c1_jobs;
            for (pass = 0; pass < 2; pass++) {
                c1_lend = pass == 0;
                M->release(); M->init(&HOST);
                prog(e2 ? BRIS0 + 1 : 1);       /* one (3 OSC), Poly Magic (BRIS) */
                if (fx) { M->midi(0xB0, 91, 80); M->midi(0xB0, 92, 100); }   /* reverb och pump på */
                M->note(0, 48, 100); M->note(0, 55, 100); M->note(0, 60, 100); M->note(0, 64, 100); M->note(0, 67, 100); M->note(0, 72, 100);
                for (b = 0; b < 60; b++) { memset(one[pass] + b * 512, 0, 512 * 4); M->render(one[pass] + b * 512, 256); }
            }
            c1_lend = 1;
            for (i2 = 0; i2 < 256 * 2 * 60; i2++) { e += (double)one[0][i2] * one[0][i2]; d += (double)(one[0][i2] - one[1][i2]) * (one[0][i2] - one[1][i2]); }
            db = 10 * log10(d / (e + 1e-30) + 1e-30);
            printf("  två kärnor, %s%s: %d jobb, skillnad mot en kärna %.1f dB\n", e2 ? "BRIS" : "3 OSC", fx ? " med reverb och pump" : "", c1_jobs - jobs0, db);
            if (!fx) CHECK(e > 0 && c1_jobs - jobs0 == 60 && db < -80, e2 ? "BRIS: två kärnor låter som en" : "3 OSC: två kärnor låter som en");
            else CHECK(c1_jobs - jobs0 == 120 && db < -60, e2 ? "BRIS: också med reverb och pump (klangen på kärna 1)" : "3 OSC: också med reverb och pump (klangen på kärna 1)");
            M->midi(0xB0, 123, 0);
        }
    }

    /* Med bandet (ERA) på räknas klangen för nästa anrop i förväg på kärna 1, medan kärna 0 räknar bandet.
     * Samma ljud som på en kärna (bandets brus och svaj är samma följd i båda), och ett jobb till per anrop. */
    {
        static int32_t one[2][256 * 2 * 60];
        int pass, b, i2;
        double e = 0, d = 0, db;
        int jobs0 = c1_jobs;
        for (pass = 0; pass < 2; pass++) {
            c1_lend = pass == 0;
            M->release(); M->init(&HOST);
            prog(26);                           /* pwm-pad (3 OSC): det tyngsta ljudet */
            M->midi(0xB0, 91, 80); M->midi(0xB0, 92, 100); M->midi(0xB0, 77, 60);
            M->note(0, 48, 100); M->note(0, 55, 100); M->note(0, 60, 100); M->note(0, 64, 100); M->note(0, 67, 100); M->note(0, 72, 100);
            for (b = 0; b < 60; b++) { memset(one[pass] + b * 512, 0, 512 * 4); M->render(one[pass] + b * 512, 256); }
        }
        c1_lend = 1;
        for (i2 = 0; i2 < 256 * 2 * 60; i2++) { e += (double)one[0][i2] * one[0][i2]; d += (double)(one[0][i2] - one[1][i2]) * (one[0][i2] - one[1][i2]); }
        db = 10 * log10(d / (e + 1e-30) + 1e-30);
        printf("  två kärnor med reverb, pump och TAPE: %d jobb, skillnad mot en kärna %.1f dB\n", c1_jobs - jobs0, db);
        CHECK(e > 0 && c1_jobs - jobs0 == 121 && db < -50, "klangen i förväg (TAPE på): två kärnor låter som en, 60 + 1 + 60 jobb");
        M->midi(0xB0, 123, 0);
    }
    /* Kärna 1 hinner inte (fm1-dock tar tillbaka den: c1_wait svarar -1). Rösterna räknas då om på kärna 0: exakt
     * samma ljud. Klangen blir tyst i ett anrop och fortsätter sedan: lika stark som annars, och dör ut till tyst. */
    {
        static int32_t one[3][256 * 2 * 200];
        int pass, b, i2;
        double e[3] = {0, 0, 0}, d = 0;
        int32_t q;
        for (pass = 0; pass < 3; pass++) {
            M->release(); M->init(&HOST);
            prog(1);
            M->midi(0xB0, 91, 80); M->midi(0xB0, 77, 30);
            M->note(0, 48, 100); M->note(0, 60, 100); M->note(0, 67, 100);
            for (b = 0; b < 200; b++) {
                if (b == 20) M->note(0, 48, 0), M->note(0, 60, 0), M->note(0, 67, 0);
                if (b == 10 && pass == 1) c1_drop_at = 1;          /* rösterna på kärna 1 */
                if (b == 10 && pass == 2) c1_drop_at = 2;          /* klangen för nästa anrop */
                memset(one[pass] + b * 512, 0, 512 * 4); M->render(one[pass] + b * 512, 256);
            }
            for (i2 = 256 * 2 * 40; i2 < 256 * 2 * 200; i2++) e[pass] += (double)one[pass][i2] * one[pass][i2];
        }
        for (i2 = 0; i2 < 256 * 2 * 200; i2++) d += (double)(one[0][i2] - one[1][i2]) * (one[0][i2] - one[1][i2]);
        M->midi(0xB0, 77, 0);
        blocks_peak(6000);
        q = blocks_peak(40);
        printf("  kärna 1 hinner inte: röster omräknade, skillnad %.0f; klangen efteråt %.2f dB mot annars; till slut %d\n", d, 10 * log10(e[2] / e[0]), q);
        CHECK(e[0] > 0 && d == 0, "kärna 1 hinner inte med rösterna: de räknas om på kärna 0, samma ljud");
        CHECK(fabs(10 * log10(e[2] / e[0])) < 1.0 && q == 0, "kärna 1 hinner inte med klangen: ett tyst anrop, sedan klingar den vidare och dör ut");
    }
    /* Anrop av olika längd (värden bestämmer): samma ljud som med 256 åt gången. Klangen som räknats i förväg räcker
     * då inte jämnt: resten räknas direkt, eller sparas till nästa anrop. */
    {
        static int32_t one[2][2 * 15360];
        static const int len[8] = {256, 128, 64, 256, 192, 32, 256, 96};
        int pass, tape2, i2;
        for (tape2 = 0; tape2 < 2; tape2++) {
            double e = 0, d = 0, db;
            for (pass = 0; pass < 2; pass++) {
                int at = 0, j = 0;
                M->release(); M->init(&HOST);
                prog(BRIS0 + 1);
                M->midi(0xB0, 91, 80);
                if (tape2) M->midi(0xB0, 77, 40);
                /* predelay över ett anrop (5,8 ms), så att den inte beror på anropets längd (se one_module.c) */
                btn(HB_BTN_FX); btn(HB_BTN_FX); btn(HB_BTN_FX); btn(HB_BTN_FX); enc(HB_ENC_KNOB1, 25);
                M->note(0, 48, 100); M->note(0, 60, 100); M->note(0, 67, 100);
                memset(one[pass], 0, sizeof one[pass]);
                while (at < 15360) {
                    int k = pass ? len[j++ & 7] : 256;
                    if (k > 15360 - at) k = 15360 - at;
                    M->render(one[pass] + 2 * at, (uint32_t)k);
                    at += k;
                }
            }
            for (i2 = 0; i2 < 2 * 15360; i2++) { e += (double)one[0][i2] * one[0][i2]; d += (double)(one[0][i2] - one[1][i2]) * (one[0][i2] - one[1][i2]); }
            db = 10 * log10(d / (e + 1e-30) + 1e-30);
            printf("  anrop av olika längd, reverb%s: skillnad mot 256 åt gången %.1f dB\n", tape2 ? " och TAPE" : "", db);
            CHECK(e > 0 && db < -50, tape2 ? "anrop av olika längd med reverb och TAPE: samma ljud" : "anrop av olika längd med reverb: samma ljud");
        }
    }

    /* SAVE: sparas och kommer tillbaka vid nästa start, med sin motor */
    {
        int e2;
        for (e2 = 0; e2 < 2; e2++) {
            const uint8_t *x = store + 8 + NPMAX * 4 + 4;   /* magic 4, version/preset/page/edited 4, p[] float, era float, sedan reverb och pump (en byte var) */
            float e;
            M->release(); store_n = 0; M->init(&HOST);
            prog(e2 ? BRIS0 + 7 : 7);           /* acid (3 OSC), Ensemble (BRIS) */
            btn(HB_BTN_SEL); enc(HB_ENC_KNOB1, -15);
            enc(HB_ENC_ALGORITHM, 30);          /* TAPE 30 sparas med ljudet */
            M->midi(0xB0, 91, 51); M->midi(0xB0, 92, 76);   /* reverbets mix 0,40 och pumpens djup 0,60 sparas också */
            btn(HB_BTN_FX); btn(HB_BTN_FX); btn(HB_BTN_FX); btn(HB_BTN_FX); btn(HB_BTN_FX); enc(HB_ENC_KNOB4, -12);   /* tempo 108 */
            btn(HB_BTN_SEL);
            btn(HB_BTN_SAVE);
            now_ms += 10; M->draw(0); shot(base, n++);  /* 32, 34: SAVED */
            CHECK(store_n == SET_N && store_n <= 240u, "inställningarna ryms i modulens 240 byte");
            memcpy(&e, store + 8 + NPMAX * 4, 4);
            printf("  sparat (%s): %u byte, version %u, motor %u, ljud %u i motorn, era %.1f, reverbmix %u, pumpdjup %u, tempo %u\n", e2 ? "BRIS" : "3 OSC", store_n, store[4], x[NX], store[5], (double)e, x[0], x[12], x[15]);
            CHECK(e == 30.f, "ERA sparas med ljudet (TAPE 30)");
            CHECK(store[4] == 2 && x[NX] == e2 && store[5] == 7 && x[0] == 40 && x[12] == 60 && x[15] == 108, "ljudets motor och nummer i motorn, reverb, pump och tempo sparas");
            M->release();
            CHECK(M->init(&HOST) == 0, "ny start");
            M->draw(1); shot(base, n++);        /* 33, 35: samma ljud, ändrat (*), TAPE 30 */
            CHECK(drawn(e2 ? "BRIS V2.2" : "3 OSC V2.2") && drawn(e2 ? "056" : "007"), e2 ? "BRIS-ljudet kommer tillbaka med BRIS-motorn" : "3 OSC-ljudet kommer tillbaka med sin motor");
            now_ms += 2000; M->draw(0);
            {   /* kom tillbaka: spara igen utan att röra något ger samma bytes */
                uint8_t was[240]; uint32_t was_n = store_n;
                memcpy(was, store, sizeof was);
                btn(HB_BTN_SAVE);
                CHECK(store_n == was_n && !memcmp(was, store, was_n), "allt kommer tillbaka vid nästa start (sparat igen: samma)");
            }
            M->note(0, 60, 100);
            CHECK(blocks_peak(30) > 1000, "och det låter");
            M->note(0, 60, 0);
        }
    }
    {   /* sparat av ONE 1.0 ("ONE1": 48 parametrar och ERA, 204 byte) laddas: samma ljud, reverb och pump av */
        uint8_t v1[204], p0[48 * 4];
        const uint8_t *x;
        M->release(); store_n = 0; M->init(&HOST);
        prog(9); btn(HB_BTN_SEL); enc(HB_ENC_KNOB1, -15); btn(HB_BTN_SAVE);
        memcpy(p0, store + 8, sizeof p0);
        memcpy(v1, "ONE1", 4); v1[4] = 1; v1[5] = 9; v1[6] = 11; v1[7] = 1;   /* version 1, ljud 9, sidan VOICE, ändrat */
        memcpy(v1 + 8, p0, sizeof p0);
        { float e = 45.f; memcpy(v1 + 8 + 192, &e, 4); }
        memcpy(store, v1, sizeof v1); store_n = sizeof v1;
        M->release();
        CHECK(M->init(&HOST) == 0, "start med inställningar från ONE 1.0");
        CHECK(drawn("009") && drawn("VOICE  1/2") && drawn("3 OSC V2.2"), "ONE 1.0: ljudet och sidan kommer tillbaka");
        btn(HB_BTN_SAVE);
        x = store + 8 + NPMAX * 4 + 4;
        { float e; memcpy(&e, store + 8 + NPMAX * 4, 4);
          CHECK(store_n == SET_N && !memcmp(store, "ONE2", 4) && x[NX] == 0 && store[5] == 9 && store[7] == 1 && !memcmp(store + 8, p0, sizeof p0) && e == 45.f && x[0] == 0 && x[12] == 0 && x[15] == 120,
                "ONE 1.0 laddas: parametrarna och ERA kvar, reverb och pump av, sparas i det nya formatet"); }
    }

    {   /* sparat av ONE 2.0 (version 1: utan Pressure, så motorn och ARP låg en byte tidigare, och sidan TOUCH fanns
         * inte): BRIS-ljudet, ARP på och ARP-sidan kommer tillbaka */
        uint8_t v[240];
        uint8_t *x = v + 8 + NPMAX * 4 + 4;
        M->release(); store_n = 0; M->init(&HOST);
        prog(BRIS0 + 7); M->midi(0xB0, 91, 51); btn(HB_BTN_ARP); btn(HB_BTN_SAVE);
        memcpy(v, store, SET_N);
        v[4] = 1; v[6] = 13;                    /* version 1; ARP var BRIS-motorns sida 13 (nu 14) */
        x[23] = 1; x[24] = 1; x[25] = x[26] = x[27] = 0;   /* motor BRIS, ARP på */
        memcpy(store, v, SET_N);
        M->release();
        CHECK(M->init(&HOST) == 0, "start med inställningar från ONE 2.0");
        CHECK(drawn("056") && drawn("BRIS V2.2") && drawn("ARP  ON") && leds[HB_BTN_ARP] == 1u, "ONE 2.0: ljudet, motorn, ARP och sidan kommer tillbaka");
        btn(HB_BTN_SAVE);
        x = store + 8 + NPMAX * 4 + 4;
        CHECK(store[4] == 2 && x[0] == 40 && x[23] == 0 && x[NX] == 1 && x[NX + 1] == 1, "och sparas i det nya formatet, med Pressure av");
        btn(HB_BTN_ARP);
    }
    {   /* Tangenternas tryck (aftertouch). FM-1:ans tangenter ger fullt tryck redan vid ett vanligt anslag, och motorerna
         * tar trycket som modhjulet (vibrato). Pressure 0, som det är från början: trycket ändrar ingenting. Pressure
         * uppvridet: trycket hörs, och det kommer mjukt (inget språng i ljudet när trycket slår till). */
        static int32_t one[3][256 * 2 * 80];
        int pass, b, i2;
        double e = 0, d1 = 0, d2 = 0;
        for (pass = 0; pass < 3; pass++) {
            M->release(); store_n = 0; M->init(&HOST);
            prog(BRIS0 + 64);                   /* MPE Lead: modhjulet ger vibrato */
            if (pass == 2) { btn(HB_BTN_GLO); btn(HB_BTN_GLO); btn(HB_BTN_GLO); enc(HB_ENC_KNOB1, 100); }   /* TOUCH: Pressure 100 */
            M->note(0, 62, 100);
            for (b = 0; b < 80; b++) {
                if (b == 20 && pass) M->midi(0xA0, 62, 127);
                memset(one[pass] + b * 512, 0, 512 * 4); M->render(one[pass] + b * 512, 256);
            }
            if (pass == 2) CHECK(drawn("TOUCH  3/3") && drawn("Pressure") && drawn("100"), "sidan TOUCH (GLO): Pressure");
            M->midi(0xA0, 62, 0); M->note(0, 62, 0);
        }
        for (i2 = 0; i2 < 256 * 2 * 80; i2++) {
            e += (double)one[0][i2] * one[0][i2];
            d1 += (double)(one[0][i2] - one[1][i2]) * (one[0][i2] - one[1][i2]);
            d2 += (double)(one[0][i2] - one[2][i2]) * (one[0][i2] - one[2][i2]);
        }
        printf("  tryck på tangenten: Pressure 0 skillnad %.1f dB, Pressure 100 skillnad %.1f dB\n", 10 * log10(d1 / e + 1e-30), 10 * log10(d2 / e + 1e-30));
        CHECK(e > 0 && d1 == 0, "Pressure 0 (från början): tangentens tryck ändrar inte ljudet");
        CHECK(d2 > e * 0.01, "Pressure 100: trycket hörs (vibrato efter ljudets MW-rattar)");
        blocks_peak(400);
    }

    /* ---- ARP och SEQ (stegens ordning och takt testas för sig i test/spel_test.c; här: knappar, skärm, lysdioder,
     *      ljudet genom motorerna och att sekvensen sparas) */
    {
        /* Takten i ljudet prövas exakt: samma toner spelas en gång till för hand (utan ARP och SEQ), slagna an och
         * släppta på de sampel där stegen ska ligga: sextondelar i 120 BPM är 5512,5 sampel, och ett steg börjar på
         * närmaste 16 sampel efter sitt läge. Blir ljudet detsamma sampel för sampel ligger varje ton rätt. */
        #define NS 60000
        static int32_t a[2 * NS], bref[2 * NS];
        typedef struct { double step; int note, vel; } tev_t;      /* händelse: vid steg nr step (decimaler), ton, anslag (0: släpp) */
        int k;
        #define PLAIN(buf) do { int at_ = 0; memset(buf, 0, sizeof(int32_t) * 2 * NS); while (at_ < NS) { int n_ = NS - at_ < 256 ? NS - at_ : 256; M->render(buf + 2 * at_, (uint32_t)n_); at_ += n_; } } while (0)
        #define TIMED(buf, evs, nevs) do { int at_ = 0, e_ = 0; memset(buf, 0, sizeof(int32_t) * 2 * NS); \
            while (at_ < NS) { int n_, t_; \
                while (e_ < (nevs) && (t_ = ((int)ceil((evs)[e_].step * 5512.5) + 15) / 16 * 16) <= at_) { M->note(0, (uint32_t)(evs)[e_].note, (uint32_t)(evs)[e_].vel); e_++; } \
                n_ = NS - at_ < 256 ? NS - at_ : 256; \
                if (e_ < (nevs)) { t_ = ((int)ceil((evs)[e_].step * 5512.5) + 15) / 16 * 16; if (t_ - at_ < n_) n_ = t_ - at_; } \
                M->render(buf + 2 * at_, (uint32_t)n_); at_ += n_; } } while (0)
        #define SAME(x, y) (!memcmp(x, y, sizeof(int32_t) * 2 * NS))
        #define ENERGY(x, e) do { int i_; e = 0; for (i_ = 0; i_ < 2 * NS; i_++) e += (double)(x)[i_] * (x)[i_]; } while (0)
        double en;

        M->release(); store_n = 0; bank_n = 0; M->init(&HOST);
        prog(0);
        CHECK(leds[HB_BTN_ARP] == 2u && leds[HB_BTN_REC] == 2u && leds[HB_BTN_PLAY] == 0u, "lysdioder från start: ARP och REC glöder, PLAY släckt (ingen sekvens)");
        btn(HB_BTN_ARP);
        CHECK(leds[HB_BTN_ARP] == 1u && drawn("ARP  ON") && drawn("Arp Mode") && drawn("1/16"), "ARP: sidan visas, arpeggiot på, lysdioden tänd");
        M->draw(1); shot(base, n++);                         /* 36: ARP-sidan */
        M->note(0, 55, 100); M->note(0, 48, 110); M->note(0, 60, 90);
        PLAIN(a);                                            /* 1,36 s: tio steg */
        M->note(0, 48, 0); M->note(0, 55, 0); M->note(0, 60, 0);
        blocks_peak(200);
        CHECK(blocks_peak(20) == 0, "ARP: tangenterna släppta: tyst");
        {
            static tev_t E1[40];
            static const int N3[3] = {48, 55, 60}, V3[3] = {110, 100, 90};
            for (k = 0; k < 11; k++) { E1[2 * k] = (tev_t){k, N3[k % 3], V3[k % 3]}; E1[2 * k + 1] = (tev_t){k + 0.5, N3[k % 3], 0}; }
            M->release(); M->init(&HOST); prog(0);
            TIMED(bref, E1, 22);
            ENERGY(a, en);
            CHECK(en > 1e9 && SAME(a, bref), "ARP UP i ljudet: en ton i taget, på sextondelarna inom 16 sampel (samma ljud som spelat för hand på de samplen)");
            M->midi(0xB0, 123, 0); blocks_peak(200);
            /* och provet märker ett fel: samma toner ett anrop (256 sampel) senare är inte samma ljud */
            for (k = 0; k < 22; k++) E1[k].step += 256.0 / 5512.5;
            M->release(); M->init(&HOST); prog(0);
            TIMED(bref, E1, 22);
            CHECK(!SAME(a, bref), "(provet skiljer på rätt sampel och ett anrop för sent)");
            M->midi(0xB0, 123, 0); blocks_peak(200);
        }
        btn(HB_BTN_ARP);
        prog(BRIS0 + 1);                                     /* motorbyte med ARP på: sidan och arpeggiot följer med */
        CHECK(drawn("ARP  ON") && leds[HB_BTN_ARP] == 1u, "ARP: sidan ligger kvar när ljudet byter motor");
        M->note(0, 60, 100); CHECK(blocks_peak(60) > 1000, "ARP spelar i BRIS-motorn"); M->note(0, 60, 0);
        btn(HB_BTN_SEL); btn(HB_BTN_ARP);
        CHECK(leds[HB_BTN_ARP] == 1u && drawn("ARP  ON"), "ARP från en annan sida: visar sidan, arpeggiot förblir på");
        btn(HB_BTN_ARP);
        CHECK(leds[HB_BTN_ARP] == 2u && redrawn("ARP  OFF"), "ARP igen: av");
        blocks_peak(600);

        /* REC: steg för steg */
        prog(0);
        btn(HB_BTN_REC); blocks_peak(2); M->draw(0);
        CHECK(leds[HB_BTN_REC] == 1u && drawn("SEQ  REC STEP 1") && drawn("ADD STEP") && drawn("REST") && drawn("TIE") && drawn("DONE"),
              "REC: SEQ-sidan visar steget och vad knapparna gör, lysdioden tänd");
        M->draw(1); shot(base, n++);                         /* 37: inspelning */
        M->note(0, 48, 110); CHECK(blocks_peak(10) > 1000, "under inspelning hörs tangenten"); M->note(0, 48, 0); blocks_peak(10);
        CHECK(redrawn("SEQ  REC STEP 2"), "ett steg in: skärmen räknar upp");
        M->note(0, 55, 100); M->note(0, 60, 110); blocks_peak(10); M->note(0, 55, 0); M->note(0, 60, 0); blocks_peak(10);   /* ackord */
        btn(HB_BTN_PLAY); blocks_peak(2);                    /* tyst steg */
        M->note(0, 52, 90); blocks_peak(10); M->note(0, 52, 0); blocks_peak(10);
        btn(HB_BTN_SEQ); blocks_peak(2);                     /* bindning */
        CHECK(drawn("SEQ  REC STEP 6"), "ton, ackord, tyst (PLAY), ton, bindning (SEQ): fem steg inne");
        btn(HB_BTN_REC); blocks_peak(2); M->draw(0);
        CHECK(leds[HB_BTN_REC] == 2u && leds[HB_BTN_PLAY] == 2u && drawn("SEQ  5 STEPS") && drawn("Seq Rate") && drawn("TRANSPOSE"),
              "REC igen: klar, fem steg, PLAY glöder, sidan visar rattarna igen");
        M->draw(1); shot(base, n++);                         /* 38: SEQ-sidan */
        btn(HB_BTN_SAVE);
        CHECK(bank_saves == 1 && bank_n == 516 && store_n == SET_N, "SAVE: sekvensen i flashbanken (516 byte), resten i inställningarna");
        btn(HB_BTN_SAVE);
        CHECK(bank_saves == 1, "SAVE igen utan ny sekvens: banken skrivs inte om (det stannar ljudet en stund)");

        /* PLAY: i ljudet, mot samma toner för hand (ny start emellan: sekvensen kommer ur banken) */
        M->release(); M->init(&HOST); M->draw(1);
        btn(HB_BTN_SEQ);
        CHECK(leds[HB_BTN_PLAY] == 2u && drawn("SEQ  5 STEPS"), "ny start: sekvensens fem steg är kvar");
        btn(HB_BTN_PLAY);
        PLAIN(a);
        M->draw(0);
        CHECK(leds[HB_BTN_PLAY] == 1u && (drawn("SEQ  PLAY 1/5") || drawn("SEQ  PLAY 2/5") || drawn("SEQ  PLAY 3/5") || drawn("SEQ  PLAY 4/5") || drawn("SEQ  PLAY 5/5")),
              "PLAY: lysdioden tänd, skärmen visar steget som spelas");
        M->draw(1); shot(base, n++);                         /* 39: spelar */
        prog(BRIS0 + 5);                                     /* motorbyte medan sekvensen går: den fortsätter i den nya motorn */
        blocks_peak(4);
        CHECK(blocks_peak(200) > 1000 && leds[HB_BTN_PLAY] == 1u, "sekvensen fortsätter när ljudet byter motor");
        M->midi(0xFC, 0, 0); blocks_peak(2); M->draw(0);
        CHECK(leds[HB_BTN_PLAY] == 2u, "MIDI stopp: sekvensen stannar");
        blocks_peak(3000);
        CHECK(blocks_peak(20) == 0, "stoppad: tyst, ingen ton hänger (båda motorerna)");
        M->midi(0xFA, 0, 0); blocks_peak(2); M->draw(0);
        CHECK(leds[HB_BTN_PLAY] == 1u && blocks_peak(100) > 1000, "MIDI start: sekvensen spelar");
        btn(HB_BTN_PLAY); blocks_peak(2); M->draw(0);
        CHECK(leds[HB_BTN_PLAY] == 2u, "PLAY: stannar");
        {
            /* två varv: C · G+C (anslag 110) · paus · E bunden över två steg */
            static tev_t E2[40];
            int m = 0, lap;
            for (lap = 0; lap < 3; lap++) {
                double o = lap * 5.0;
                E2[m++] = (tev_t){o, 48, 110}; E2[m++] = (tev_t){o + 0.5, 48, 0};
                E2[m++] = (tev_t){o + 1, 55, 110}; E2[m++] = (tev_t){o + 1, 60, 110}; E2[m++] = (tev_t){o + 1.5, 55, 0}; E2[m++] = (tev_t){o + 1.5, 60, 0};
                E2[m++] = (tev_t){o + 3, 52, 90}; E2[m++] = (tev_t){o + 4.5, 52, 0};
            }
            M->release(); bank_n = 0; M->init(&HOST);
            TIMED(bref, E2, m);
            ENERGY(a, en);
            CHECK(en > 1e9 && SAME(a, bref), "SEQ i ljudet: ton, ackord, paus och bunden ton på sina steg, varv efter varv (samma ljud som för hand)");
            M->midi(0xB0, 123, 0);
            blocks_peak(600);
        }

        /* ARP-läget och inställningarna sparas */
        btn(HB_BTN_ARP);                                     /* ARP på */
        enc(HB_ENC_KNOB1, 2); enc(HB_ENC_KNOB3, 1);          /* UP/DOWN, två oktaver */
        btn(HB_BTN_SAVE);
        M->release(); M->init(&HOST); M->draw(1);
        CHECK(leds[HB_BTN_ARP] == 1u && drawn("ARP  ON") && drawn("UP/DOWN"), "ny start: ARP på med sina inställningar");
        btn(HB_BTN_ARP);                                     /* av */
        blocks_peak(100);

        /* MIDI-klocka: arpeggiot går i klockans takt, inte i den egna (120). Klockan i 100 BPM räknat i värdens
         * millisekunder (6 per anrop; ljudets anrop är 5,8 ms, så i ljudet blir det 103 BPM): på 4 s ljud 27-28
         * sextondelar, mot 32 i 120 BPM. Tonerna räknas som skurar i ljudet (init, attack och release 0, gate 50 %). */
        {
            int32_t out[512], pk = 0, bp[700];
            int b, i2, t = 0, bursts = 0, low = 1;
            M->release(); store_n = 0; M->init(&HOST);
            prog(0); M->midi(0xB0, 73, 0); M->midi(0xB0, 72, 0);
            btn(HB_BTN_ARP);
            M->midi(0xFA, 0, 0);
            for (b = 0; b < 300; b++) {                      /* klockan går en stund först: takten mäts */
                while (t <= b * 6) { M->midi(0xF8, 0, 0); t += 25; }
                memset(out, 0, sizeof out); M->render(out, 256); now_ms += 6;
            }
            M->note(0, 60, 110);
            for (b = 0; b < 689; b++) {
                while (t <= (300 + b) * 6) { M->midi(0xF8, 0, 0); t += 25; }
                memset(out, 0, sizeof out); M->render(out, 256); now_ms += 6;
                bp[b] = 0;
                for (i2 = 0; i2 < 512; i2++) if (abs(out[i2]) > bp[b]) bp[b] = abs(out[i2]);
                if (bp[b] > pk) pk = bp[b];
            }
            for (b = 0; b < 689; b++) { if (bp[b] < pk / 20) low = 1; else if (low && bp[b] > pk / 2) { bursts++; low = 0; } }
            M->note(0, 60, 0);
            seen_ext = 0; btn(HB_BTN_SEQ); M->draw(1);
            printf("  ARP med MIDI-klocka: %d toner på 4 s (103 BPM ger 27-28, den egna takten 120 ger 32)\n", bursts);
            CHECK(bursts >= 26 && bursts <= 29 && seen_ext, "MIDI-klocka: arpeggiot går i klockans takt, och SEQ-sidan visar den (EXT)");
            btn(HB_BTN_ARP); btn(HB_BTN_ARP);                /* av */
            for (b = 0; b < 500; b++) { memset(out, 0, sizeof out); M->render(out, 256); now_ms += 6; }
        }

        /* fler tangenter än kön rymmer utan att ljudet går: hellre tyst än en ton som hänger */
        for (k = 0; k < 100; k++) M->note(0, (uint32_t)(30 + k % 60), 100);
        blocks_peak(10);
        for (k = 0; k < 60; k++) M->note(0, (uint32_t)(30 + k), 0);
        blocks_peak(3000);
        CHECK(blocks_peak(20) == 0, "kön full: allt tystas, ingen ton hänger");
        M->note(0, 60, 100); CHECK(blocks_peak(20) > 1000, "och sedan spelar den som vanligt"); M->note(0, 60, 0);
        CHECK(bad_draw == 0, "ARP- och SEQ-sidorna ritas inom skärmen");
        #undef PLAIN
        #undef TIMED
        #undef SAME
        #undef ENERGY
        #undef NS
    }

    /* Inga denormala tal i tystnad. FM-1:ans FPU räknar dem mycket långsamt: ett värde som klingat av och fastnat
     * denormalt (x -= x * k blir till slut x) och räknas varje sampel tog en stor del av ljudavbrottet, också när allt
     * var tyst (fm1-dock visade 30-70 % utan toner, och TOO SLOW på pads). Här spelas båda motorerna med allt på
     * (reverb, pump, eko, ensemble, band), sedan tre minuters tystnad: därefter får inget anrop ge underflöde. */
    {
        int32_t out[512];
        int e2, b, k, under = 0, era2, playing = 0;
        for (era2 = 0; era2 < 2; era2++) for (e2 = 0; e2 < 2; e2++) {
            M->release(); store_n = 0; M->init(&HOST);
            prog(e2 ? BRIS0 + 66 : 33);                  /* bla pad i båda motorerna: lång release, eko och ensemble */
            M->midi(0xB0, 91, 90); M->midi(0xB0, 92, 80);
            if (era2) M->midi(0xB0, 77, 30);
            for (k = 0; k < 6; k++) M->note(0, (uint32_t)(48 + 4 * k), 100);
            for (b = 0; b < 172 * 20; b++) {                 /* 3 s ton, sedan 17 s utklingning: räknas också */
                if (b == 516) for (k = 0; k < 6; k++) M->note(0, (uint32_t)(48 + 4 * k), 0);
                feclearexcept(FE_ALL_EXCEPT);
                memset(out, 0, sizeof out); M->render(out, 256);
                if (fetestexcept(FE_UNDERFLOW)) playing++;
            }
            M->midi(0xB0, 92, 0);
            if (era2) M->midi(0xB0, 77, 0);              /* (bandet tonar ut och slutar räknas) */
            for (b = 0; b < 172 * 180; b++) { memset(out, 0, sizeof out); M->render(out, 256); }
            for (b = 0; b < 344; b++) {
                feclearexcept(FE_ALL_EXCEPT);
                memset(out, 0, sizeof out); M->render(out, 256);
                if (fetestexcept(FE_UNDERFLOW)) under++;
            }
        }
        printf("  denormala tal (två pads, båda motorerna, med och utan band): under spel och utklingning i %d av 13 760 anrop, "
               "efter 3 min tystnad i %d av 1376\n", playing, under);
        CHECK(under == 0, "inga denormala tal i tystnad (de är mycket långsamma på FM-1:ans FPU)");
        CHECK(playing <= 4, "nästan inga denormala tal under spel och utklingning");
    }

    snprintf(wpath, sizeof wpath, "%s.wav", base);
    write_wav(wpath);
    printf("%d skärmbilder, ljud i %s\n", n, wpath);
    if (fails) { printf("%d kontroller misslyckades\n", fails); return 1; }
    printf("alla kontroller ok\n");
    return 0;
}
