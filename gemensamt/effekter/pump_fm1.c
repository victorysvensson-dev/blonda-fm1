/* SPDX-License-Identifier: GPL-3.0-only */
/* BLONDA PUMP för FM-1, se pump_fm1.h. */
#include "pump_fm1.h"
#include "snabbmatte.h"

#define SR ((float)PUMP_SR)
#define RELEASE 0.75f                           /* nivån är tillbaka efter så här stor del av perioden */
#define SMOOTH 0.00753f                         /* 1 - exp(-1 / (sr * 3 ms)): slaget dras ned på några millisekunder */
static const float BEATS[PUMP_NRATES] = {4.f, 2.f, 1.f, 0.5f, 0.25f};   /* periodens längd i fjärdedelar */

void pump_init(pump_t *p)
{
    p->depth = 0.f; p->rate = 2; p->bpm = 120.f;
    p->ph = 0.f; p->g = 1.f;
    p->ext = p->pending = p->beat_at = p->tick_at = p->beat_idx = p->tick_n = p->have_beat = p->fresh = 0;
    p->ext_bpm = 120.f;
}

float pump_tempo(const pump_t *p) { return p->ext ? p->ext_bpm : p->bpm; }

int pump_clock(const pump_t *p, uint32_t now, float *beats)
{
    int32_t el;
    if (!p->ext || !p->have_beat) return 0;
    el = (int32_t)(now - p->beat_at);
    *beats = (float)(p->beat_idx & 15u) + (float)(el > 0 ? el : 0) * (1.f / (float)PUMP_TICKS_HZ) * p->ext_bpm * (1.f / 60.f);
    return 1;
}

/* MIDI-klockan: 24 pulser per fjärdedel. Var 24:e är ett slag: tiden mellan två slag ger tempot. */
void pump_midi(pump_t *p, uint32_t st, uint32_t now)
{
    if (st == 0xFAu) {                          /* start: nästa puls är första slaget */
        p->tick_n = 0; p->beat_idx = 0; p->have_beat = 0; p->fresh = 1;
        return;
    }
    if (st != 0xF8u) return;
    p->tick_at = now;
    if (p->tick_n == 0) {
        if (p->have_beat) {
            uint32_t d = now - p->beat_at;      /* ticks per fjärdedel; 30..300 BPM tas på allvar */
            if (d > PUMP_TICKS_HZ / 5u && d < PUMP_TICKS_HZ * 2u) {
                float b = 60.f * (float)PUMP_TICKS_HZ / (float)d;
                p->ext_bpm = p->ext ? p->ext_bpm + (b - p->ext_bpm) * 0.5f : b;
                p->ext = 1;
            }
            p->beat_idx++;
        }
        p->beat_at = now;
        p->have_beat = 1;
        p->pending = 1;                         /* sist: ljudet läser de andra först när den här är satt */
    }
    p->tick_n = p->tick_n >= 23u ? 0u : p->tick_n + 1u;
}

void pump_begin(pump_t *p, uint32_t now)
{
    /* klockan har tystnat i två sekunder (en puls stämplad efter now räknas som nyss) */
    if (p->ext && (int32_t)(now - p->tick_at) > (int32_t)(PUMP_TICKS_HZ * 2u)) { p->ext = 0; p->have_beat = 0; p->tick_n = 0; }
    if (p->pending) {
        /* var i perioden klockan är nu: slagets nummer, och tiden sedan slaget kom */
        float per = BEATS[p->rate < 0 ? 0 : p->rate >= PUMP_NRATES ? PUMP_NRATES - 1 : p->rate];
        int32_t el = (int32_t)(now - p->beat_at);
        float since = (float)(el > 0 ? el : 0) * (1.f / (float)PUMP_TICKS_HZ) * pump_tempo(p) * (1.f / 60.f);
        float want = ((float)(p->beat_idx & 15u) + since) / per, err;
        want -= sm_floor(want);
        err = want - p->ph;
        if (err > 0.5f) err -= 1.f; else if (err < -0.5f) err += 1.f;
        if (p->fresh || err > 0.2f || err < -0.2f) p->ph = want;         /* långt ifrån (eller nystartad): hoppa dit */
        else { p->ph += err * 0.3f; p->ph -= sm_floor(p->ph); }          /* annars dras fasen mjukt mot klockan */
        p->fresh = 0;
        p->pending = 0;
    }
}

void pump_gain(pump_t *p, float *gain, uint32_t n)
{
    float per = BEATS[p->rate < 0 ? 0 : p->rate >= PUMP_NRATES ? PUMP_NRATES - 1 : p->rate];
    float inc = pump_tempo(p) * (1.f / 60.f) / (per * SR), ph = p->ph, g = p->g, depth = p->depth;
    uint32_t s;
    if (depth <= 0.f && g > 0.9999f) {           /* av: bara fasen går, så att pumpen är i takt när den slås på */
        ph += inc * (float)n;
        p->ph = ph - sm_floor(ph);
        p->g = 1.f;
        if (gain) for (s = 0; s < n; s++) gain[s] = 1.f;
        return;
    }
    for (s = 0; s < n; s++) {
        float c;
        ph += inc; if (ph >= 1.f) ph -= 1.f;
        c = ph < RELEASE ? 1.f - ph * (1.f / RELEASE) : 0.f;
        g += (1.f - depth * c * c - g) * SMOOTH;
        gain[s] = g;
    }
    p->ph = ph; p->g = g;
}
