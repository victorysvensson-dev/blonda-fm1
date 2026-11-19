/* SPDX-License-Identifier: GPL-3.0-only */
/* PUMP (gemensamt/effekter/pump_fm1.c): formen, takten, och att den följer en MIDI-klocka.
 *   cc -O2 -Igemensamt -Igemensamt/effekter gemensamt/test/pump_test.c gemensamt/effekter/pump_fm1.c -o build/pump-test -lm */
#include <math.h>
#include <stdio.h>
#include "pump_fm1.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FEL: "); fails++; } else printf("ok: "); printf(__VA_ARGS__); printf("\n"); } while (0)

#define N (PUMP_SR * 8)
static float g[N];

/* kör pumpen i block om 256 sampel; ticks är fm1-dock:s 24 MHz-klocka i takt med ljudet. clock_bpm > 0: en
 * MIDI-klocka i den takten, som börjar (med start) efter t0 sekunder och slutar efter t1. late = 0: pulserna
 * stämplas med sin rätta tid; late = 1: de kommer fram först vid nästa ljudblock (upp till 5,8 ms sent), som när
 * fm1-dock lämnar dem i ljudavbrottet */
static void run(pump_t *p, double clock_bpm, double t0, double t1, int late, uint32_t base)
{
    double tick_s = clock_bpm > 0 ? 60.0 / clock_bpm / 24.0 : 0, next = t0;
    int started = 0, i;
    for (i = 0; i + 256 <= N; i += 256) {
        double t = (double)i / PUMP_SR;
        uint32_t now = base + (uint32_t)(t * PUMP_TICKS_HZ);
        while (clock_bpm > 0 && next <= t && next < t1) {
            uint32_t at = late ? now : base + (uint32_t)(next * PUMP_TICKS_HZ);
            if (!started) { pump_midi(p, 0xFA, at); started = 1; }
            pump_midi(p, 0xF8, at);
            next += tick_s;
        }
        pump_begin(p, now);
        pump_gain(p, g + i, 256);
    }
}
/* tiden (s) för nivåns lägsta punkt i varje period efter t_from: medelperiod och hur långt efter slaget (med
 * slag var beat_s sekund från origo) botten ligger */
static void dips(double t_from, double period_guess, double *period, double *offset, double origo)
{
    int i, k = 0;
    double first = 0, last = 0, off = 0;
    for (i = (int)(t_from * PUMP_SR); i + (int)(period_guess * PUMP_SR) <= N - 512; ) {
        int w = (int)(period_guess * PUMP_SR), j, at = i;
        for (j = i; j < i + w; j++) if (g[j] < g[at]) at = j;
        if (k == 0) first = (double)at / PUMP_SR;
        last = (double)at / PUMP_SR;
        off += fmod((double)at / PUMP_SR - origo + period_guess * 1000, period_guess);
        k++;
        i = at + w / 2;                         /* nästa fönster börjar en halv period efter botten */
    }
    *period = k > 1 ? (last - first) / (k - 1) : 0;
    *offset = k ? off / k : 0;
}

int main(void)
{
    static pump_t p;
    double per, off;
    float lo = 2, hi = 0;
    int i;

    /* av: nivån är 1 hela tiden */
    pump_init(&p);
    run(&p, 0, 0, 0, 0, 0);
    for (i = 0; i < N - 256; i++) { if (g[i] < lo) lo = g[i]; if (g[i] > hi) hi = g[i]; }
    CHECK(lo == 1.f && hi == 1.f, "av (djup 0): nivån orörd");

    /* 120 BPM, fjärdedelar, fullt djup: botten nära 0 var 0,5 s, tillbaka på 1 före nästa slag */
    pump_init(&p); p.depth = 1.f;
    run(&p, 0, 0, 0, 0, 0);
    lo = 2; hi = 0;
    for (i = PUMP_SR; i < N - 256; i++) { if (g[i] < lo) lo = g[i]; if (g[i] > hi) hi = g[i]; }
    dips(1.0, 0.5, &per, &off, 0);
    CHECK(lo < 0.08f && hi > 0.999f, "fullt djup: nivån går mellan %.3f och %.3f", (double)lo, (double)hi);
    CHECK(fabs(per - 0.5) < 0.0005, "120 BPM, 1/4: en puls var %.4f s", per);
    CHECK(off > 0.004 && off < 0.016, "botten kommer %.1f ms efter slaget (mjukt anslag, inget knäpp)", off * 1000);
    {   /* inga språng: största steget mellan två sampel */
        float step = 0;
        for (i = PUMP_SR; i < N - 256; i++) { float d = (float)fabs((double)(g[i] - g[i - 1])); if (d > step) step = d; }
        CHECK(step < 0.01f, "nivån rör sig mjukt (största steg %.4f per sampel)", (double)step);
    }
    /* halvt djup, åttondelar, 90 BPM */
    pump_init(&p); p.depth = 0.5f; p.rate = 3; p.bpm = 90.f;
    run(&p, 0, 0, 0, 0, 0);
    lo = 2;
    for (i = PUMP_SR; i < N - 256; i++) if (g[i] < lo) lo = g[i];
    dips(1.0, 60.0 / 90 / 2, &per, &off, 0);
    CHECK(lo > 0.5f && lo < 0.56f, "halvt djup: botten %.3f", (double)lo);
    CHECK(fabs(per - 60.0 / 90 / 2) < 0.0005, "90 BPM, 1/8: en puls var %.4f s", per);

    /* MIDI-klocka i 97 BPM från 1,013 s: pumpen tar klockans takt och lägger sig på klockans slag.
     * Egen takt 120 och fasen på annat håll från början. Klockan börjar inte på noll (räknaren slår runt). */
    pump_init(&p); p.depth = 1.f; p.ph = 0.37f;
    run(&p, 97.0, 1.013, 100.0, 0, 0xFFF00000u);
    dips(4.0, 60.0 / 97, &per, &off, 1.013);
    CHECK(p.ext && fabs((double)pump_tempo(&p) - 97.0) < 0.2, "klocka 97 BPM: pumpen mäter %.2f BPM", (double)pump_tempo(&p));
    {   /* pump_clock: klockans läge i fjärdedelar, för ARP och SEQ. Körningen slutar vid N - 256 sampel; klockan
         * började vid 1,013 s, så läget där är (t - 1,013) * 97 / 60 fjärdedelar, räknat runt 16. */
        float beats = -1.f;
        double t_end = (double)(N - N % 256) / PUMP_SR, want = fmod((t_end - 1.013) * 97.0 / 60.0, 16.0);
        int ok = pump_clock(&p, 0xFFF00000u + (uint32_t)(t_end * PUMP_TICKS_HZ), &beats);
        CHECK(ok && fabs((double)beats - want) < 0.01, "pump_clock: klockans läge %.3f fjärdedelar (ska vara %.3f)", (double)beats, want);
        pump_init(&p);
        CHECK(!pump_clock(&p, 0, &beats), "pump_clock: ingen klocka, inget läge");
    }
    CHECK(fabs(per - 60.0 / 97) < 0.0005, "pumpen går i klockans takt (%.4f s mot %.4f)", per, 60.0 / 97);
    CHECK(off < 0.016, "botten ligger på klockans slag (%.1f ms efter)", off * 1000);
    /* samma klocka som kommer fram upp till ett ljudblock (5,8 ms) sent, som i ljudavbrottet */
    pump_init(&p); p.depth = 1.f;
    run(&p, 97.0, 1.013, 100.0, 1, 1000u);
    dips(4.0, 60.0 / 97, &per, &off, 1.013);
    CHECK(p.ext && fabs((double)pump_tempo(&p) - 97.0) < 1.5 && fabs(per - 60.0 / 97) < 0.002 && off < 0.022,
          "sen klocka (upp till 5,8 ms): %.2f BPM, puls var %.4f s, botten %.1f ms efter slaget", (double)pump_tempo(&p), per, off * 1000);
    /* klockan tystnar efter 3 s: pumpen går tillbaka till sin egen takt */
    pump_init(&p); p.depth = 1.f; p.bpm = 140.f;
    run(&p, 100.0, 0.5, 3.0, 0, 0);
    dips(6.0, 60.0 / 140, &per, &off, 0);
    CHECK(!p.ext && pump_tempo(&p) == 140.f && fabs(per - 60.0 / 140) < 0.0005, "klockan tystnar: tillbaka på egen takt (%.4f s)", per);
    /* helnoter: perioden är fyra slag och börjar på klockans första slag */
    pump_init(&p); p.depth = 1.f; p.rate = 0;
    run(&p, 120.0, 0.25, 100.0, 0, 0);
    dips(2.5, 2.0, &per, &off, 0.25);
    CHECK(fabs(per - 2.0) < 0.001 && off < 0.025, "1/1 med klocka: en puls per takt, på ettan (%.3f s, %.1f ms)", per, off * 1000);

    if (fails) { printf("%d kontroller misslyckades\n", fails); return 1; }
    printf("alla kontroller ok\n");
    return 0;
}
