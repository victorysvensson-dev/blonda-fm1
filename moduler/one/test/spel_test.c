/* SPDX-License-Identifier: GPL-3.0-only */
/* Test av ARP och SEQ (src/spel_fm1.c) på datorn, utan ljudmotor: tonerna som kommer ut skrivs upp med
 * sampelnummer och jämförs med det som ska komma. Körs som modulen kör den: tangenter och knappar i början av
 * ett anrop om 256 sampel, sedan spel_plan / spel_run fram till nästa händelse (avrundat uppåt till 16 sampel).
 *   cc -std=c99 -O2 -Wall -Isrc test/spel_test.c src/spel_fm1.c -o build/spel-test -lm && build/spel-test */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "spel_fm1.h"

typedef struct { long t; int note, vel; } ev_t;
static ev_t ev[200000];
static int nev;
static long now;
static int sounding[128], sus, alloffs, doubles;
static void o_note(int n, int v)
{
    if (nev < 200000) { ev[nev].t = now; ev[nev].note = n; ev[nev].vel = v; nev++; }
    if (v) { if (sounding[n]) doubles++; sounding[n] = 1; } else sounding[n] = 0;
}
static void o_sus(int d) { sus = d; }
static void o_all(void) { alloffs++; memset(sounding, 0, sizeof sounding); }
static const spel_out_t OUT = {o_note, o_sus, o_all};

static float bpm = 120.f;
static int ext;
static double ext_beats;                        /* klockans läge (när ext) */
/* ett anrop om n sampel, som modulen gör det */
static void block(uint32_t n)
{
    if (ext) spel_sync((float)fmod(ext_beats, 16.0));
    spel_run(0, bpm, ext);
    while (n) {
        uint32_t d = spel_plan(n, bpm), k = (d + 15u) & ~15u;
        if (k > n) k = n;
        now += k;
        if (ext) ext_beats += (double)k * bpm / 60.0 / SPEL_SR;
        spel_run(k, bpm, ext);
        n -= k;
    }
}
static void run_ms(int ms) { long end = now + (long)ms * SPEL_SR / 1000; while (now < end) block(256); }
static void fresh(void)
{
    spel_defaults(); spel_init(&OUT);
    nev = 0; now = 0; sus = 0; alloffs = 0; doubles = 0; ext = 0; bpm = 120.f; ext_beats = 0;
    memset(sounding, 0, sizeof sounding);
}

static int fails;
#define CHECK(c, msg) do { if (!(c)) { printf("FEL: %s\n", msg); fails++; } else printf("ok: %s\n", msg); } while (0)

/* tonerna som slagits an, i ordning (högst n), som text "60 64 67 ..." */
static const char *ons(int n)
{
    static char s[2048];
    int i, k = 0;
    s[0] = 0;
    for (i = 0; i < nev && n > 0; i++) if (ev[i].vel) { k += sprintf(s + k, k ? " %d" : "%d", ev[i].note); n--; }
    return s;
}
/* största felet (sampel) mellan anslagen och ett jämnt raster med steget step, från första anslaget */
static double grid_err(double step, int count)
{
    int i, k = 0;
    long t0 = -1;
    double worst = 0;
    for (i = 0; i < nev && k < count; i++)
        if (ev[i].vel) {
            if (t0 < 0) t0 = ev[i].t;
            { double e = fabs((double)(ev[i].t - t0) - k * step); if (e > worst) worst = e; }
            k++;
        }
    return k == count ? worst : 1e9;
}

int main(void)
{
    const double STEP16 = SPEL_SR * 60.0 / 120.0 / 4.0;    /* en sextondel i 120 BPM: 5512,5 sampel */
    int i;

    /* ---- ARP av: tangenterna går rakt igenom, pedalen också */
    fresh();
    spel_note(60, 100); block(256); spel_pedal(1); spel_note(60, 0); block(256);
    CHECK(nev == 2 && ev[0].note == 60 && ev[0].vel == 100 && ev[1].vel == 0 && sus == 1, "utan ARP: tonen och pedalen går rakt till motorn");

    /* ---- ARP upp, tre toner, sextondelar, gate 50 % */
    fresh();
    spel_set.arp_on = 1;
    spel_note(64, 90); spel_note(60, 100); spel_note(67, 80);
    run_ms(1000);
    CHECK(!strncmp(ons(7), "60 64 67 60 64 67 60", 20), "ARP UP: C E G i tonhöjdsordning, runt");
    CHECK(ev[0].t == 0 && ev[0].vel == 100, "ARP: första tonen direkt, med tangentens anslag");
    printf("  raster: största fel %.1f sampel på 8 steg; första tonens längd %ld sampel (halva steget %.0f)\n", grid_err(STEP16, 8), ev[1].t - ev[0].t, STEP16 / 2);
    CHECK(grid_err(STEP16, 8) <= 16.0, "ARP: stegen ligger på sextondelarna (inom 16 sampel = 0,36 ms)");
    CHECK(ev[1].vel == 0 && ev[1].note == 60 && labs(ev[1].t - (long)(STEP16 / 2)) <= 16, "ARP: gate 50 % släpper tonen efter halva steget");
    { int over = 0, snd = 0; for (i = 0; i < nev; i++) { snd += ev[i].vel ? 1 : -1; if (snd > 1) over++; } CHECK(over == 0 && doubles == 0, "ARP: en ton i taget"); }
    spel_note(60, 0); spel_note(64, 0); spel_note(67, 0);
    { int before; run_ms(100); before = nev; run_ms(500); CHECK(before == nev && !memchr(sounding, 1, sizeof sounding), "ARP: tangenterna släppta: tyst, ingen ton hänger"); }

    /* ---- lägen och oktaver */
    fresh(); spel_set.arp_on = 1; spel_set.amode = SPEL_DOWN;
    spel_note(60, 100); spel_note(64, 100); spel_note(67, 100); run_ms(800);
    CHECK(!strncmp(ons(5), "67 64 60 67 64", 14), "ARP DOWN");
    fresh(); spel_set.arp_on = 1; spel_set.amode = SPEL_UPDOWN;
    spel_note(60, 100); spel_note(64, 100); spel_note(67, 100); run_ms(1200);
    CHECK(!strncmp(ons(9), "60 64 67 64 60 64 67 64 60", 26), "ARP UP/DOWN: vänder utan att upprepa ändtonerna");
    fresh(); spel_set.arp_on = 1; spel_set.amode = SPEL_ORDER;
    spel_note(67, 100); spel_note(60, 100); spel_note(64, 100); run_ms(800);
    CHECK(!strncmp(ons(5), "67 60 64 67 60", 14), "ARP ORDER: i den ordning tangenterna trycktes");
    fresh(); spel_set.arp_on = 1; spel_set.aoct = 2;
    spel_note(60, 100); spel_note(64, 100); run_ms(800);
    CHECK(!strncmp(ons(5), "60 64 72 76 60", 14), "ARP två oktaver");
    fresh(); spel_set.arp_on = 1; spel_set.amode = SPEL_RANDOM;
    spel_note(60, 100); spel_note(64, 100); spel_note(67, 100); run_ms(3000);
    { int c[3] = {0, 0, 0}, bad = 0; for (i = 0; i < nev; i++) if (ev[i].vel) { if (ev[i].note == 60) c[0]++; else if (ev[i].note == 64) c[1]++; else if (ev[i].note == 67) c[2]++; else bad++; }
      CHECK(!bad && c[0] > 2 && c[1] > 2 && c[2] > 2, "ARP RANDOM: bara de hållna tonerna, alla kommer"); }
    fresh(); spel_set.arp_on = 1; spel_set.arate = 1;      /* åttondelar */
    spel_note(60, 100); spel_note(64, 100); run_ms(1500);
    CHECK(grid_err(STEP16 * 2, 5) <= 16.0, "ARP RATE 1/8");
    fresh(); spel_set.arp_on = 1; spel_set.arate = 4; bpm = 90.f;   /* sextondelstrioler i 90 BPM */
    spel_note(60, 100); spel_note(64, 100); run_ms(1500);
    CHECK(grid_err(SPEL_SR * 60.0 / 90.0 / 6.0, 10) <= 16.0, "ARP RATE 1/16T i 90 BPM");

    /* ---- gate 100 %: bundet (nästa ton slås an innan den förra släpps) */
    fresh(); spel_set.arp_on = 1; spel_set.agate = 1.f;
    spel_note(60, 100); spel_note(64, 100); run_ms(400);
    CHECK(ev[0].note == 60 && ev[1].note == 64 && ev[1].vel && ev[2].note == 60 && !ev[2].vel && ev[1].t == ev[2].t, "ARP gate 100 %: bundet, ny ton före släppet");
    fresh(); spel_set.arp_on = 1; spel_set.agate = 1.f;
    spel_note(60, 100); run_ms(400);
    CHECK(ev[1].note == 60 && !ev[1].vel && ev[2].note == 60 && ev[2].vel && doubles == 0, "ARP gate 100 % med en ton: den slås an på nytt varje steg");

    /* ---- pedalen håller arpeggiot */
    fresh(); spel_set.arp_on = 1;
    spel_note(60, 100); spel_note(64, 100); run_ms(100);
    spel_pedal(1); spel_note(60, 0); spel_note(64, 0);
    { int n0; run_ms(100); n0 = nev; run_ms(600); CHECK(nev > n0 + 6 && sus == 0, "pedalen håller arpeggiot (och går inte till motorn)"); }
    spel_note(72, 100); run_ms(50); nev = 0; run_ms(700);
    CHECK(!strncmp(ons(3), "72 72 72", 8), "en ny tangent efter att alla släppts: nytt arpeggio");
    spel_note(72, 0); spel_pedal(0);
    { int n0; run_ms(100); n0 = nev; run_ms(400); CHECK(nev == n0 && !memchr(sounding, 1, sizeof sounding), "pedalen släppt: tyst"); }

    /* ---- ARP slås på medan en tangent hålls: dess släpp går till motorn; ARP av: arpeggiots ton släpps */
    fresh();
    spel_note(60, 100); block(256);
    spel_set.arp_on = 1; spel_note(64, 100); run_ms(300);
    spel_note(60, 0); block(256);
    { int off60 = 0; for (i = 0; i < nev; i++) if (ev[i].note == 60 && !ev[i].vel) off60++; CHECK(off60 == 1, "ARP på medan en tangent hålls: tangentens släpp når motorn"); }
    spel_set.arp_on = 0; block(256); run_ms(300);
    CHECK(!memchr(sounding, 1, sizeof sounding), "ARP av: arpeggiots ton släpps");

    /* ---- SEQ: spela in steg för steg */
    fresh();
    spel_cmd(SPEL_CMD_REC);
    CHECK(spel_recording() && spel_step() == 0, "REC: inspelning, steg 1 väntar");
    spel_note(60, 100); block(256); spel_note(60, 0); block(256);                   /* steg 1: C */
    spel_note(64, 90); spel_note(67, 110); block(256); spel_note(64, 0); block(256);
    CHECK(spel_step() == 1, "ett ackord är ett steg: det räknas först när alla tangenter är släppta");
    spel_note(67, 0); block(256);                                                    /* steg 2: E + G */
    spel_cmd(SPEL_CMD_PLAY);                                                         /* steg 3: tyst */
    spel_note(62, 100); block(256); spel_note(62, 0); block(256);                   /* steg 4: D */
    spel_cmd(SPEL_CMD_TIE);                                                          /* steg 5: D binds */
    CHECK(spel_step() == 5 && nev == 8 && doubles == 0, "under inspelning hörs tangenterna som vanligt (4 toner, 4 släpp)");
    spel_cmd(SPEL_CMD_REC);
    CHECK(!spel_recording() && spel_seq.len == 5 && spel_seq.root == 60 && spel_seq_changed == 1, "REC igen: klar, fem steg, första tonen C");
    CHECK(spel_seq.st[0].n == 1 && spel_seq.st[0].note[0] == 60 && spel_seq.st[0].vel == 100 && spel_seq.st[1].n == 2 && spel_seq.st[1].note[0] == 64
          && spel_seq.st[1].note[1] == 67 && spel_seq.st[1].vel == 110 && spel_seq.st[2].n == 0 && spel_seq.st[3].n == 1 && spel_seq.st[3].note[0] == 62
          && spel_seq.st[4].n == SPEL_TIE, "stegen: C, E+G, tyst, D, bindning");

    /* ---- SEQ: spela */
    nev = 0; now = 0;
    spel_cmd(SPEL_CMD_PLAY);
    run_ms(1500);                                /* 5 steg = 0,625 s: drygt två varv */
    CHECK(spel_playing() && !strncmp(ons(9), "60 64 67 62 60 64 67 62 60", 26), "PLAY: sekvensen spelas runt");
    {
        long tC = -1, tE = -1, tD = -1, tDoff = -1, tC2 = -1;
        for (i = 0; i < nev; i++) {
            if (ev[i].note == 60 && ev[i].vel && tC < 0) tC = ev[i].t;
            else if (ev[i].note == 60 && ev[i].vel && tC2 < 0) tC2 = ev[i].t;
            if (ev[i].note == 64 && ev[i].vel && tE < 0) tE = ev[i].t;
            if (ev[i].note == 62 && ev[i].vel && tD < 0) tD = ev[i].t;
            if (ev[i].note == 62 && !ev[i].vel && tDoff < 0) tDoff = ev[i].t;
        }
        printf("  SEQ: C vid %ld, E vid %ld, D vid %ld, D släpps vid %ld, C igen vid %ld (steg %.1f)\n", tC, tE, tD, tDoff, tC2, STEP16);
        CHECK(tC == 0 && labs(tE - (long)STEP16) <= 16 && labs(tD - (long)(3 * STEP16)) <= 16, "SEQ: stegen i takt, det tysta steget hoppas över");
        CHECK(labs(tDoff - (long)(4.5 * STEP16)) <= 16, "SEQ: bindningen håller D över två steg (släpps efter halva det andra)");
        CHECK(labs(tC2 - (long)(5 * STEP16)) <= 16, "SEQ: varvet börjar om efter fem steg");
    }
    nev = 0;
    spel_note(65, 100); spel_note(65, 0);        /* F: sekvensen upp en kvart (första tonen var C) */
    run_ms(700);
    CHECK(!strcmp(ons(4), "67 65 69 72") && doubles == 0, "SEQ TRANSPOSE: tangenten flyttar sekvensen från nästa steg (F: D C E+G blir G F A+C)");
    spel_cmd(SPEL_CMD_PLAY); block(256);
    CHECK(!spel_playing() && !memchr(sounding, 1, sizeof sounding), "PLAY igen: stannar, inga toner hänger");

    /* KEYS = PLAY: tangenterna spelar ovanpå sekvensen */
    spel_set.skeys = SPEL_KEYS_PLAY; nev = 0;
    spel_cmd(SPEL_CMD_PLAY); block(256);
    spel_note(84, 100); block(256);
    { int got = 0; for (i = 0; i < nev; i++) if (ev[i].note == 84 && ev[i].vel) got = 1; CHECK(got, "SEQ KEYS PLAY: tangenten spelar ovanpå"); }
    spel_note(84, 0); spel_cmd(SPEL_CMD_PLAY); block(256);
    CHECK(!memchr(sounding, 1, sizeof sounding), "och släpps");

    /* REC utan toner: den gamla sekvensen är kvar. 64 steg: klart av sig självt. */
    spel_cmd(SPEL_CMD_REC); spel_cmd(SPEL_CMD_REC);
    CHECK(spel_seq.len == 5 && spel_seq.st[0].note[0] == 60 && spel_seq_changed == 1, "REC och REC utan toner: den gamla sekvensen är kvar");
    spel_cmd(SPEL_CMD_REC);
    for (i = 0; i < 70; i++) { spel_note(40 + i, 100); spel_note(40 + i, 0); }
    CHECK(!spel_recording() && spel_seq.len == 64 && spel_seq.st[63].note[0] == 103 && spel_seq.root == 40, "64 steg: inspelningen avslutas av sig själv");
    spel_panic();

    /* ---- MIDI-klocka: START spelar från början, stegen följer klockan, STOP stannar */
    fresh();
    spel_cmd(SPEL_CMD_REC); for (i = 0; i < 4; i++) { spel_note(60 + i, 100); spel_note(60 + i, 0); } spel_cmd(SPEL_CMD_REC);
    nev = 0; now = 0; ext = 1; bpm = 100.f; ext_beats = 0.0;
    spel_cmd(SPEL_CMD_START);
    run_ms(2000);
    CHECK(!strncmp(ons(5), "60 61 62 63 60", 14) && grid_err(SPEL_SR * 60.0 / 100.0 / 4.0, 12) <= 24.0, "MIDI START: sekvensen från början, i klockans takt");
    spel_cmd(SPEL_CMD_STOP); block(256);
    CHECK(!spel_playing() && !memchr(sounding, 1, sizeof sounding), "MIDI STOP: stannar");
    /* PLAY mitt i klockans takt: första steget på klockans nästa sextondel */
    nev = 0;
    run_ms(137);
    spel_cmd(SPEL_CMD_PLAY); run_ms(600);
    {
        double step = SPEL_SR * 60.0 / 100.0 / 4.0, at = fmod(ext_beats, 16.0) * 4.0;   /* klockans läge i steg nu */
        double first = (double)ev[0].t / step + (fmod(0.0, 1.0));
        (void)at;
        first = ((double)ev[0].t - 0.0) / step;  /* now räknas från noll tillsammans med klockan (ext_beats = now * bpm / 60 / sr) */
        printf("  PLAY med klocka: första steget vid %.3f sextondelar\n", first);
        CHECK(fabs(first - floor(first + 0.5)) < 0.01 && spel_playing(), "PLAY med klocka: börjar på klockans nästa sextondel");
    }
    spel_cmd(SPEL_CMD_PLAY); block(256);
    /* ARP med klocka: första tonen på nästa steg, sedan på rastret */
    spel_set.arp_on = 1; nev = 0; run_ms(71);
    spel_note(60, 100); run_ms(500);
    { double step = SPEL_SR * 60.0 / 100.0 / 4.0, first = (double)ev[0].t / step; CHECK(nev > 2 && fabs(first - floor(first + 0.5)) < 0.01, "ARP med klocka: tonerna på klockans sextondelar"); }
    spel_note(60, 0); run_ms(200);

    /* ---- slumpat: tangenter, pedal, knappar och lägen huller om buller; efteråt hänger ingen ton */
    {
        unsigned r = 12345u;
        int k, held2[128];
        fresh(); memset(held2, 0, sizeof held2);
        for (k = 0; k < 60000; k++) {
            int what;
            r = r * 1103515245u + 12345u; what = (int)(r >> 16) % 100;
            if (what < 45) { int n2 = 36 + (int)(r >> 8) % 36; if (!held2[n2]) { spel_note(n2, 1 + (int)(r >> 3) % 127); held2[n2] = 1; } }
            else if (what < 85) { int n2 = 36 + (int)(r >> 8) % 36; if (held2[n2]) { spel_note(n2, 0); held2[n2] = 0; } }
            else if (what < 89) spel_pedal((int)(r >> 9) & 1);
            else if (what < 92) spel_set.arp_on = (int)(r >> 9) & 1;
            else if (what < 94) spel_cmd(SPEL_CMD_PLAY);
            else if (what < 95) spel_cmd(SPEL_CMD_REC);
            else if (what < 96) spel_cmd(SPEL_CMD_TIE);
            else if (what < 97) { spel_set.amode = (int)(r >> 9) % SPEL_NMODES; spel_set.aoct = 1 + (int)(r >> 12) % 4; spel_set.arate = (int)(r >> 5) % SPEL_NRATES; }
            else if (what < 98) { spel_set.agate = 0.05f + (float)((r >> 9) % 96) * 0.01f; spel_set.sgate = 0.05f + (float)((r >> 4) % 96) * 0.01f; spel_set.skeys = (int)(r >> 20) & 1; }
            else if (what < 99) bpm = 40.f + (float)((r >> 9) % 200);
            else spel_cmd((r >> 9) & 1 ? SPEL_CMD_START : SPEL_CMD_STOP);
            if ((r >> 24) % 3 == 0) block(256);
        }
        for (k = 0; k < 128; k++) if (held2[k]) spel_note(k, 0);
        spel_pedal(0);
        if (spel_recording()) spel_cmd(SPEL_CMD_REC);
        if (spel_playing()) spel_cmd(SPEL_CMD_PLAY);
        spel_set.arp_on = 0;
        run_ms(200);
        { int stuck = 0; for (k = 0; k < 128; k++) stuck += sounding[k]; printf("  slumpat: %d händelser ut, %d toner hänger, sekvensen %d steg\n", nev, stuck, spel_seq.len);
          CHECK(stuck == 0 && sus == 0 && spel_seq.len <= SPEL_STEPS, "slumpat spel i 60 000 drag: ingen ton hänger kvar"); }
    }

    if (fails) { printf("%d kontroller misslyckades\n", fails); return 1; }
    printf("alla kontroller ok\n");
    return 0;
}
