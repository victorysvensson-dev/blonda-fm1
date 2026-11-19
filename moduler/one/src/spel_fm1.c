/* SPDX-License-Identifier: GPL-3.0-only */
/* ARP och SEQ för BLONDA ONE, se spel_fm1.h. */
#include "spel_fm1.h"

#define SR ((float)SPEL_SR)
#define LEGATO 0.995f                           /* gate från här: tonerna binds ihop (nästa slås an innan den förra släpps) */
static const float SPB[SPEL_NRATES] = {1.f, 2.f, 3.f, 4.f, 6.f, 8.f};   /* steg per fjärdedel */

spel_set_t spel_set;
spel_seq_t spel_seq;
volatile uint32_t spel_seq_changed;

static spel_out_t O;
enum { M_NONE, M_ARP, M_SEQ };
static struct { uint8_t note, vel, down; } held[SPEL_HELD];   /* arpeggiots tangenter, i den ordning de trycktes */
static int32_t nheld, pedal, arp_was;
static uint8_t direct[16];                      /* tangenter vars ton gick rakt till motorn (släppet ska också dit) */
static uint8_t snd[SPEL_POLY];                  /* tonerna som ARP eller SEQ låter spela just nu */
static int32_t nsnd;
static float ph;                                /* var i steget, 0..1 */
static int32_t mode, gate_done, acount;
static uint32_t rnd;
static volatile int32_t playing, recording, cur;
static int32_t sidx, tr;                        /* nästa steg i sekvensen; flyttning i halvtoner */
static uint8_t rec_note[SPEL_POLY];
static int32_t rec_n, rec_down, rec_vel, rec_root;
static int32_t start;                           /* sekvensen ska börja: 1 nu direkt, 2 på klockans nästa steg */
static int32_t ext_last;                        /* klockan kom utifrån senast */
static float sync_beats;                        /* klockans läge senast (fjärdedelar) */

static int dir_get(int n) { return direct[n >> 3] >> (n & 7) & 1; }
static void dir_set(int n, int on) { if (on) direct[n >> 3] |= (uint8_t)(1u << (n & 7)); else direct[n >> 3] &= (uint8_t)~(1u << (n & 7)); }

static void release_snd(void)
{
    int i;
    for (i = 0; i < nsnd; i++) O.note(snd[i], 0);
    nsnd = 0;
}

void spel_defaults(void)
{
    spel_set.arp_on = 0; spel_set.amode = SPEL_UP; spel_set.arate = 3; spel_set.aoct = 1; spel_set.agate = 0.5f;
    spel_set.srate = 3; spel_set.sgate = 0.5f; spel_set.skeys = SPEL_KEYS_TRANSPOSE;
    spel_seq_clear();
}

void spel_seq_clear(void)
{
    int i;
    spel_seq.magic = SPEL_MAGIC; spel_seq.len = 0; spel_seq.root = 60; spel_seq.pad = 0;
    for (i = 0; i < SPEL_STEPS; i++) {
        int k;
        spel_seq.st[i].n = 0; spel_seq.st[i].vel = 0;
        for (k = 0; k < SPEL_POLY; k++) spel_seq.st[i].note[k] = 0;
    }
}

void spel_init(const spel_out_t *out)
{
    int i;
    O = *out;
    nheld = 0; pedal = 0; arp_was = spel_set.arp_on;
    for (i = 0; i < 16; i++) direct[i] = 0;
    nsnd = 0; ph = 0.f; mode = M_NONE; gate_done = 0; acount = 0;
    rnd = 20240917u;
    playing = recording = cur = 0;
    sidx = 0; tr = 0;
    rec_n = rec_down = rec_vel = rec_root = 0;
    start = 0; ext_last = 0; sync_beats = 0.f;
}

int spel_playing(void) { return playing; }
int spel_recording(void) { return recording; }
int spel_step(void) { return cur; }

/* ARP slogs på eller av sedan sist: arpeggiots toner släpps, och pedalen byter uppgift (håller arpeggiot, eller
 * är motorns vanliga sustain) */
static void arp_switch(void)
{
    int on = spel_set.arp_on;
    if (on == arp_was) return;
    arp_was = on;
    if (mode == M_ARP) { release_snd(); mode = M_NONE; }
    nheld = 0;
    O.sustain(on ? 0 : pedal);
}

static float rate_spb(void);

/* vad som spelar nu: sekvensen går före arpeggiot. Det som börjar spelar sin första ton direkt; med klocka
 * utifrån på klockans nästa steg (utom när klockan själv startar sekvensen). */
static void set_mode(int ext)
{
    int want = playing ? M_SEQ : (spel_set.arp_on && nheld > 0 && !recording) ? M_ARP : M_NONE;
    int grid = ext;
    if (want == mode && !(want == M_SEQ && start)) return;
    if (want == M_SEQ) { grid = start == 2; start = 0; }
    release_snd();
    mode = want;
    gate_done = 0;
    acount = 0;
    if (want == M_NONE) return;
    if (grid) { ph = sync_beats * rate_spb(); ph -= (float)(int32_t)ph; }
    else ph = 1.f;
}

static void held_remove(int i)
{
    for (; i + 1 < nheld; i++) held[i] = held[i + 1];
    nheld--;
}

/* ett inspelat steg är klart: tonerna i rec_note (inga: tyst), eller en bindning */
static void rec_commit(int tie)
{
    spel_step_t *s;
    int i;
    if (cur >= SPEL_STEPS) return;
    s = &spel_seq.st[cur];
    s->n = tie ? (uint8_t)SPEL_TIE : (uint8_t)rec_n;
    s->vel = (uint8_t)rec_vel;
    for (i = 0; i < SPEL_POLY; i++) s->note[i] = i < rec_n && !tie ? rec_note[i] : 0;
    if (!tie && rec_n > 0 && !rec_root) { spel_seq.root = rec_note[0]; rec_root = 1; }   /* sekvensens första ton */
    rec_n = 0; rec_vel = 0;
    cur = cur + 1;
}

static void rec_end(void)
{
    if (rec_n > 0) rec_commit(0);               /* tangenter som ännu hålls: steget räknas */
    if (cur > 0) {                              /* (inget inspelat: den gamla sekvensen är kvar) */
        spel_seq.len = (uint8_t)cur;
        spel_seq_changed++;
    }
    recording = 0;
    cur = 0;
    rec_down = 0;
}

void spel_note(int note, int vel)
{
    if (note < 0 || note > 127) return;
    arp_switch();
    if (vel > 0) {
        if (recording) {                        /* hörs som vanligt, och blir ett steg när alla tangenter är släppta */
            O.note(note, vel); dir_set(note, 1);
            if (rec_n < SPEL_POLY) rec_note[rec_n++] = (uint8_t)note;
            if (vel > rec_vel) rec_vel = vel;
            rec_down++;
            return;
        }
        if (playing && spel_set.skeys == SPEL_KEYS_TRANSPOSE) { tr = note - (int32_t)spel_seq.root; return; }
        if (spel_set.arp_on && !playing) {
            int i;
            for (i = 0; i < nheld; i++) if (held[i].note == note) { held[i].down = 1; held[i].vel = (uint8_t)vel; return; }
            if (nheld > 0) {                    /* en ny tangent efter att alla släppts (pedalen höll dem): nytt ackord */
                int any = 0;
                for (i = 0; i < nheld; i++) any |= held[i].down;
                if (!any) nheld = 0;
            }
            if (nheld == SPEL_HELD) held_remove(0);
            held[nheld].note = (uint8_t)note; held[nheld].vel = (uint8_t)vel; held[nheld].down = 1;
            nheld++;
            return;
        }
        O.note(note, vel); dir_set(note, 1);
        return;
    }
    if (dir_get(note)) { O.note(note, 0); dir_set(note, 0); }
    if (recording) {
        if (rec_down > 0 && --rec_down == 0) {
            rec_commit(0);
            if (cur >= SPEL_STEPS) rec_end();   /* fullt: klart */
        }
        return;
    }
    {
        int i;
        for (i = 0; i < nheld; i++)
            if (held[i].note == note) {
                if (pedal) held[i].down = 0;    /* pedalen håller den kvar i arpeggiot */
                else held_remove(i);
                break;
            }
    }
}

void spel_pedal(int down)
{
    arp_switch();
    pedal = down ? 1 : 0;
    if (!spel_set.arp_on) { O.sustain(pedal); return; }
    if (!pedal) {                               /* släppt: tangenter som inte längre hålls lämnar arpeggiot */
        int i = 0;
        while (i < nheld) { if (!held[i].down) held_remove(i); else i++; }
    }
}

void spel_panic(void)
{
    int i;
    nsnd = 0; nheld = 0; rec_n = 0; rec_down = 0;
    for (i = 0; i < 16; i++) direct[i] = 0;
    O.all_off();
}

void spel_cmd(int cmd)
{
    arp_switch();
    switch (cmd) {
    case SPEL_CMD_PLAY:
        if (recording) { if (rec_down == 0) { rec_n = 0; rec_commit(0); if (cur >= SPEL_STEPS) rec_end(); } }   /* tyst steg */
        else if (playing) { playing = 0; cur = 0; }
        else if (spel_seq.len > 0 && spel_seq.len <= SPEL_STEPS) { playing = 1; sidx = 0; tr = 0; cur = 0; start = ext_last ? 2 : 1; }
        break;
    case SPEL_CMD_REC:
        if (recording) rec_end();
        else {                                  /* en ny sekvens: den gamla skrivs över från första steget */
            playing = 0;
            nheld = 0;
            recording = 1; cur = 0; rec_n = 0; rec_down = 0; rec_vel = 0; rec_root = 0;
        }
        break;
    case SPEL_CMD_TIE:
        if (recording && rec_down == 0 && cur > 0) { rec_commit(1); if (cur >= SPEL_STEPS) rec_end(); }
        break;
    case SPEL_CMD_START:
        if (!recording && spel_seq.len > 0 && spel_seq.len <= SPEL_STEPS) { playing = 1; sidx = 0; tr = 0; cur = 0; start = 1; }
        break;
    case SPEL_CMD_STOP:
        if (playing) { playing = 0; cur = 0; }
        break;
    default: break;
    }
}

static float rate_spb(void)
{
    int32_t r = mode == M_SEQ ? spel_set.srate : spel_set.arate;
    return SPB[r < 0 ? 0 : r >= SPEL_NRATES ? SPEL_NRATES - 1 : r];
}
static float gate_now(void)
{
    float g = mode == M_SEQ ? spel_set.sgate : spel_set.agate;
    return g < 0.05f ? 0.05f : g;
}

void spel_sync(float beats)
{
    float want, err;
    sync_beats = beats;
    if (mode == M_NONE) return;
    want = beats * rate_spb();
    want -= (float)(int32_t)want;
    err = want - (ph - (float)(int32_t)ph);
    if (err > 0.5f) err -= 1.f; else if (err < -0.5f) err += 1.f;
    ph += err > 0.25f || err < -0.25f ? err : err * 0.1f;   /* långt ifrån: hoppa dit; annars dras steget mjukt mot klockan */
    if (ph < 0.f) ph = 0.f;
}

uint32_t spel_plan(uint32_t max, float bpm)
{
    float inc, t, d;
    if (mode == M_NONE || max == 0) return max;
    inc = bpm * rate_spb() * (1.f / (60.f * SR));
    t = !gate_done && nsnd > 0 && gate_now() < LEGATO ? gate_now() : 1.f;
    d = (t - ph) / inc;
    if (d < 1.f) return 1;
    if (d >= (float)max) return max;
    return (uint32_t)d + 1u;
}

/* nästa ton i arpeggiot */
static void arp_step(int legato)
{
    uint8_t ord[SPEL_HELD];
    int32_t n = nheld, oct = spel_set.aoct, len, i, j, m = spel_set.amode, note, vel, same = 0;
    if (n <= 0) return;
    oct = oct < 1 ? 1 : oct > 4 ? 4 : oct;
    for (i = 0; i < n; i++) ord[i] = (uint8_t)i;
    if (m != SPEL_ORDER)                        /* efter tonhöjd */
        for (i = 1; i < n; i++) {
            uint8_t x = ord[i];
            for (j = i; j > 0 && held[ord[j - 1]].note > held[x].note; j--) ord[j] = ord[j - 1];
            ord[j] = x;
        }
    len = n * oct;
    if (m == SPEL_DOWN) i = len - 1 - acount % len;
    else if (m == SPEL_UPDOWN) { int32_t p = len > 1 ? 2 * len - 2 : 1; i = acount % p; if (i >= len) i = p - i; }
    else if (m == SPEL_RANDOM) { rnd = rnd * 1664525u + 1013904223u; i = (int32_t)((rnd >> 8) % (uint32_t)len); }
    else i = acount % len;
    acount = acount + 1 < 0x40000000 ? acount + 1 : 0;
    note = held[ord[i % n]].note + 12 * (i / n);
    while (note > 127) note -= 12;
    vel = held[ord[i % n]].vel;
    for (j = 0; j < nsnd; j++) if (snd[j] == note) same = 1;
    if (legato && !same) { O.note(note, vel); release_snd(); }
    else { release_snd(); O.note(note, vel); }
    snd[0] = (uint8_t)note; nsnd = 1;
}

/* nästa steg i sekvensen */
static void seq_step(int legato)
{
    const spel_step_t *s;
    uint8_t nw[SPEL_POLY];
    int32_t n = 0, i, j;
    if (spel_seq.len == 0 || spel_seq.len > SPEL_STEPS) { playing = 0; return; }
    if (sidx >= spel_seq.len) sidx = 0;
    s = &spel_seq.st[sidx];
    cur = sidx;
    sidx++;
    if (s->n == SPEL_TIE && nsnd > 0) return;   /* tonerna ligger kvar */
    if (s->n != SPEL_TIE)
        for (i = 0; i < s->n && i < SPEL_POLY; i++) {
            int32_t x = (int32_t)s->note[i] + tr;
            if (x >= 0 && x <= 127) nw[n++] = (uint8_t)x;
        }
    if (legato) {                               /* bundet: nya toner slås an innan de gamla släpps; samma ton slås an på nytt */
        for (j = 0; j < nsnd; j++)
            for (i = 0; i < n; i++) if (snd[j] == nw[i]) { O.note(snd[j], 0); snd[j] = 255; break; }
        for (i = 0; i < n; i++) O.note(nw[i], s->vel ? s->vel : 100);
        for (j = 0; j < nsnd; j++) if (snd[j] != 255) O.note(snd[j], 0);
    } else {
        release_snd();
        for (i = 0; i < n; i++) O.note(nw[i], s->vel ? s->vel : 100);
    }
    for (i = 0; i < n; i++) snd[i] = nw[i];
    nsnd = n;
}

void spel_run(uint32_t n, float bpm, int ext)
{
    float g;
    int guard = 0;
    arp_switch();
    ext_last = ext;
    set_mode(ext);
    if (mode == M_NONE) return;
    ph += bpm * rate_spb() * (1.f / (60.f * SR)) * (float)n;
    g = gate_now();
    while (guard++ < 8) {
        if (!gate_done && g < LEGATO && ph >= g) {          /* tonen har varat sin del av steget */
            gate_done = 1;
            if (!(mode == M_SEQ && spel_seq.len > 0 && spel_seq.len <= SPEL_STEPS
                  && spel_seq.st[sidx >= spel_seq.len ? 0 : sidx].n == SPEL_TIE))   /* (nästa steg binder: den ligger kvar) */
                release_snd();
            continue;
        }
        if (ph >= 1.f) {
            ph -= 1.f;
            if (ph >= 1.f) ph -= (float)(int32_t)ph;   /* (långt efter: ett steg i taget, inga skurar) */
            gate_done = 0;
            if (mode == M_ARP) arp_step(g >= LEGATO); else seq_step(g >= LEGATO);
            if (mode == M_SEQ && !playing) { set_mode(ext); return; }
            continue;
        }
        break;
    }
}
