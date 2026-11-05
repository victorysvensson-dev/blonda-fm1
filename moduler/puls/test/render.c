/* SPDX-License-Identifier: GPL-3.0-only */
/* puls-render: spelar FM-1-PULS till WAV på datorn, med samma mönster och tider som blonda-motors
 * `blonda-render puls --monster` (så att likhet.py kan jämföra dem sampel för sampel).
 *   puls-render [--preset N] [--monster "36:0,8;38:4,12"] [--bpm 120] [--len s] [--vel v] [-o fil.wav]
 *   puls-render --all mapp/          alla trumset med blonda-renders standardmönster, "NN namn.wav"
 *   puls-render --bench              tid per 256 sampel med alla åtta trumljud igång */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "puls_fm1.h"

/* samma som kDemoBeat i blonda-motor/verktyg/blonda_render.cpp */
static const char *DEMO = "36:0,6,8;38:4,12;42:0,2,4,6,8,10,14;46:14;39:12;37:10;45:15;56:7";
static double len = 2.4, bpm = 120;
static int vel = 100;

static int write_wav(const char *path, const float *L, const float *R, int n)
{
    FILE *f = fopen(path, "wb");
    int i;
    unsigned int sr = PULS_SR, br = PULS_SR * 8, ds = (unsigned)n * 8, rs = 36 + ds;
    unsigned short fmt = 3, ch = 2, ba = 8, bits = 32;
    unsigned int fl = 16;
    if (!f) return 0;
    fwrite("RIFF", 1, 4, f); fwrite(&rs, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
    fwrite(&fl, 4, 1, f); fwrite(&fmt, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&sr, 4, 1, f);
    fwrite(&br, 4, 1, f); fwrite(&ba, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&ds, 4, 1, f);
    for (i = 0; i < n; i++) { fwrite(&L[i], 4, 1, f); fwrite(&R[i], 4, 1, f); }
    fclose(f);
    return 1;
}

/* mönstret som blonda-render läser det: "ton:steg,steg;ton:steg" (sextondelar) */
static int steps[16][16], nsteps[16];
static void parse(const char *s)
{
    memset(nsteps, 0, sizeof nsteps);
    while (*s) {
        int note = atoi(s);
        const char *c = strchr(s, ':'), *e = strchr(s, ';');
        if (!e) e = s + strlen(s);
        if (c && c < e) {
            const char *q = c + 1;
            while (q < e) {
                int st = atoi(q);
                if (st >= 0 && st < 16 && nsteps[st] < 16) steps[st][nsteps[st]++] = note;
                q = strchr(q, ',');
                if (!q || q > e) break;
                q++;
            }
        }
        s = *e ? e + 1 : e;
    }
}
static void render(float *L, float *R, int total)
{
    int per = (int)lround(PULS_SR * 60.0 / bpm / 4.0), pos, k, i;
    for (pos = 0, k = 0; pos < total; pos += per, ++k) {
        int n = total - pos < per ? total - pos : per;
        for (i = 0; i < nsteps[k % 16]; i++) puls_note_on(steps[k % 16][i], (k % 4 == 0) ? 120 : vel);
        puls_render(L + pos, R + pos, (uint32_t)n);
    }
}

int main(int argc, char **argv)
{
    const char *out = "puls.wav", *all = 0, *mon = DEMO;
    int preset = 0, i, total, bench = 0;
    float *L, *R;
    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--preset") && i + 1 < argc) preset = atoi(argv[++i]);
        else if (!strcmp(a, "--monster") && i + 1 < argc) mon = argv[++i];
        else if (!strcmp(a, "--bpm") && i + 1 < argc) bpm = atof(argv[++i]);
        else if (!strcmp(a, "--len") && i + 1 < argc) len = atof(argv[++i]);
        else if (!strcmp(a, "--vel") && i + 1 < argc) vel = atoi(argv[++i]);
        else if (!strcmp(a, "--all") && i + 1 < argc) all = argv[++i];
        else if (!strcmp(a, "--bench")) bench = 1;
        else if (!strcmp(a, "-o") && i + 1 < argc) out = argv[++i];
        else { fprintf(stderr, "okänt: %s\n", a); return 1; }
    }
    parse(mon);
    total = (int)(len * PULS_SR);
    L = calloc((size_t)total + 256, sizeof *L);
    R = calloc((size_t)total + 256, sizeof *R);
    puls_init();
    if (bench) {
        clock_t t0;
        double us;
        int b, v, blocks = 4000;
        puls_load_preset(7);                    /* Industri: allt på, drivning */
        t0 = clock();
        for (b = 0; b < blocks; b++) {
            if (b % 40 == 0) for (v = 0; v < PULS_NV; v++) puls_hit(v, 1.f);
            puls_render(L, R, 256);
        }
        us = (double)(clock() - t0) / CLOCKS_PER_SEC * 1e6 / blocks;
        printf("%.1f us per 256 sampel (%.2f %% av realtid på den här datorn)\n", us, us / (256.0 / PULS_SR * 1e6) * 100.0);
        return 0;
    }
    if (all) {
        int p;
        for (p = 0; p < PULS_NPRESETS; p++) {
            char path[1024];
            puls_init();
            puls_load_preset(p);
            memset(L, 0, sizeof *L * (size_t)total); memset(R, 0, sizeof *R * (size_t)total);
            render(L, R, total);
            snprintf(path, sizeof path, "%s/%02d %s.wav", all, p, PULS_PRESETS[p].name);
            if (!write_wav(path, L, R, total)) { fprintf(stderr, "kunde inte skriva %s\n", path); return 1; }
        }
        printf("%d trumset i %s\n", PULS_NPRESETS, all);
        return 0;
    }
    puls_load_preset(preset);
    render(L, R, total);
    if (!write_wav(out, L, R, total)) return 1;
    printf("skrev %s\n", out);
    return 0;
}
