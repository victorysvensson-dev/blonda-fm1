/* SPDX-License-Identifier: GPL-3.0-only */
/* bris-render: spelar FM-1-BRIS till WAV på datorn, med samma tider som blonda-motors blonda-render
 * (så att likhet.py kan jämföra dem sampel för sampel).
 *   bris-render [--preset N] [--notes 60,64,67] [--hold s] [--len s] [--vel v] [--set id=värde] [-o fil.wav]
 *   bris-render --all mapp/          alla fabriksljud (monoljud spelar ton 48), "NN namn.wav"
 *   bris-render --bench              tid per 256 sampel med 6 röster igång */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "bris_fm1.h"

static int notes[16] = {60, 64, 67}, nnotes = 3, vel = 100;
static double hold = 1.2, len = 2.4;

static int write_wav(const char *path, const float *L, const float *R, int n)
{
    FILE *f = fopen(path, "wb");
    int i;
    unsigned int sr = BRIS_SR, br = BRIS_SR * 8, ds = (unsigned)n * 8, rs = 36 + ds;
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

/* som blonda-render: block om 256, tonerna släpps mitt i blocket vid hold */
static void render(float *L, float *R, int total, const int *nt, int nn)
{
    int pos, i, h = (int)(hold * BRIS_SR), blk = 256;
    for (i = 0; i < nn; i++) bris_note_on(nt[i], vel);
    for (pos = 0; pos < total; pos += blk) {
        int k = total - pos < blk ? total - pos : blk;
        if (pos <= h && pos + blk > h) {
            int pre = h - pos, rest;
            if (pre > 0) bris_render(L + pos, R + pos, (uint32_t)pre);
            for (i = 0; i < nn; i++) bris_note_off(nt[i]);
            rest = k - pre;
            if (rest > 0) bris_render(L + pos + pre, R + pos + pre, (uint32_t)rest);
            continue;
        }
        bris_render(L + pos, R + pos, (uint32_t)k);
    }
}

static int find_param(const char *id)
{
    int i;
    for (i = 0; i < BP_N; i++) if (!strcmp(BRIS_PARAMS[i].id, id)) return i;
    return -1;
}

int main(int argc, char **argv)
{
    const char *out = "bris.wav", *all = 0;
    int preset = -1, i, total, bench = 0;
    struct { int p; float v; } sets[32];
    int nsets = 0;
    float *L, *R;
    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--preset") && i + 1 < argc) preset = atoi(argv[++i]);
        else if (!strcmp(a, "--notes") && i + 1 < argc) {
            const char *s = argv[++i];
            nnotes = 0;
            while (*s && nnotes < 16) { notes[nnotes++] = atoi(s); s = strchr(s, ','); if (!s) break; s++; }
        } else if (!strcmp(a, "--hold") && i + 1 < argc) hold = atof(argv[++i]);
        else if (!strcmp(a, "--len") && i + 1 < argc) len = atof(argv[++i]);
        else if (!strcmp(a, "--vel") && i + 1 < argc) vel = atoi(argv[++i]);
        else if (!strcmp(a, "--set") && i + 1 < argc) {
            char kv[64], *e;
            strncpy(kv, argv[++i], 63); kv[63] = 0;
            e = strchr(kv, '=');
            if (!e) { fprintf(stderr, "--set id=värde\n"); return 1; }
            *e = 0;
            sets[nsets].p = find_param(kv); sets[nsets].v = (float)atof(e + 1);
            if (sets[nsets].p < 0) { fprintf(stderr, "okänd parameter: %s\n", kv); return 1; }
            if (nsets < 31) nsets++;
        } else if (!strcmp(a, "--all") && i + 1 < argc) all = argv[++i];
        else if (!strcmp(a, "--bench")) bench = 1;
        else if (!strcmp(a, "-o") && i + 1 < argc) out = argv[++i];
        else { fprintf(stderr, "okänt: %s\n", a); return 1; }
    }
    total = (int)(len * BRIS_SR);
    L = calloc((size_t)total + 256, sizeof *L);
    R = calloc((size_t)total + 256, sizeof *R);
    bris_init();
    if (bench) {
        static const int six[6] = {48, 55, 60, 64, 67, 72};
        clock_t t0;
        double us;
        int b, blocks = 2000;
        bris_load_preset(1);                    /* Poly Magic: sågtand+puls, chorus, eko */
        for (b = 0; b < 6; b++) bris_note_on(six[b], 100);
        t0 = clock();
        for (b = 0; b < blocks; b++) bris_render(L, R, 256);
        us = (double)(clock() - t0) / CLOCKS_PER_SEC * 1e6 / blocks;
        printf("%.1f us per 256 sampel (%.1f %% av realtid på den här datorn)\n", us, us / (256.0 / BRIS_SR * 1e6) * 100.0);
        return 0;
    }
    if (all) {
        int p;
        for (p = 0; p < BRIS_NPRESETS; p++) {
            char path[1024];
            int mono48 = 48;
            bris_init();
            bris_load_preset(p);
            memset(L, 0, sizeof *L * (size_t)total); memset(R, 0, sizeof *R * (size_t)total);
            if (bris_get(BP_lage) >= 2.f) render(L, R, total, &mono48, 1);
            else render(L, R, total, notes, nnotes);
            snprintf(path, sizeof path, "%s/%02d %s.wav", all, p, BRIS_PRESETS[p].name);
            if (!write_wav(path, L, R, total)) { fprintf(stderr, "kunde inte skriva %s\n", path); return 1; }
        }
        printf("%d ljud i %s\n", BRIS_NPRESETS, all);
        return 0;
    }
    if (preset >= 0) bris_load_preset(preset);
    for (i = 0; i < nsets; i++) bris_set(sets[i].p, sets[i].v);
    render(L, R, total, notes, nnotes);
    if (!write_wav(out, L, R, total)) return 1;
    printf("skrev %s\n", out);
    return 0;
}
