/* Drivare för cykeluppskattningen: körs i en RISC-V-emulator (rv32imf, som ett ungefär av FM-1:ans pi32v2).
 * Ett block räknas som modulen räknar det: motorn (ENGINE: 0 trioscillatormotorn, 1 BRIS), sedan reverbet
 * (klangen ur förra blocket), pumpen och bandet. */
#include <stdint.h>
#include "one_fm1.h"
#include "bris_fm1.h"
#include "sliten_fm1.h"
#include "rum_fm1.h"
#include "pump_fm1.h"
static sliten_t tape;
static rum_t rum;
static pump_t pump;
extern int ENGINE, PRESET, TAPE, RUM, PART, SPLIT;
int ENGINE = 0, PRESET = 1, TAPE = 0;
int RUM = 0;                                     /* 1 = klangen (kärna 1), 2 = mix och pump (kärna 0), 3 = båda */
int PART = 0;                                    /* 0 = alla röster, 1 = bara röst 0..2 (det kärna 0 räknar med två kärnor), 2 = inga */
int SPLIT = 0;                                   /* 1 = anropet räknas i två bitar (48 + 208 sampel), som när ARP eller SEQ har ett steg i det */
float L[256], R[256], WL[256], WR[256], PG[256];
volatile int stage;
void *memset(void *d, int c, unsigned n) { unsigned char *p = d; while (n--) *p++ = (unsigned char)c; return d; }
void *memcpy(void *d, const void *s, unsigned n) { unsigned char *p = d; const unsigned char *q = s; while (n--) *p++ = *q++; return d; }
void *memmove(void *d, const void *s, unsigned n) { unsigned char *p = d; const unsigned char *q = s; if (p < q) while (n--) *p++ = *q++; else while (n--) p[n] = q[n]; return d; }

static void part(unsigned n)
{
    unsigned i;
    if (ENGINE) { bris_render_begin(n); bris_render_voices(0, PART == 2 ? 0 : PART ? 3 : 6, 0); bris_render_end(L, R); }
    else { one_render_begin(n); one_render_voices(0, PART == 2 ? 0 : PART ? 3 : 6, 0); one_render_end(L, R); }
    if (RUM & 1) rum_tail(&rum, WL, WR, n, 256);
    if (RUM & 2) {
        pump_gain(&pump, PG, n);
        for (i = 0; i < n; i++) { WL[i] *= PG[i]; WR[i] *= PG[i]; }
        rum_feed(&rum, L, R, n);
        rum_mix(&rum, L, R, WL, WR, n);
    }
    if (TAPE) sliten_process(&tape, L, R, n);
}
static void block(void)
{
    if (SPLIT) { part(48); part(208); } else part(256);
}

void _start(void)
{
    static const int six[6] = {48, 55, 60, 64, 67, 72};
    int i;
    bris_init();
    one_init();
    if (ENGINE) bris_load_preset(PRESET); else one_load_preset(PRESET);
    sliten_init(&tape);
    sliten_set(&tape, SL_age, 0.75f); sliten_set(&tape, SL_sat, 0.6f); sliten_set(&tape, SL_avbr, 0.03f);
    rum_init(&rum); rum_set(&rum, RU_wet, 0.5f); rum_set(&rum, RU_highc, 0.8f); rum_set(&rum, RU_duck, 0.3f);
    pump_init(&pump); pump.depth = 0.8f;
    for (i = 0; i < 6; i++) { if (ENGINE) bris_note_on(six[i], 100); else one_note_on(six[i], 100); }
    for (i = 0; i < 8; i++) block();                  /* in i ljudet */
    stage = 1;                                        /* mätningen börjar */
    for (i = 0; i < 4; i++) block();
    stage = 2;
    for (;;) {}
}
