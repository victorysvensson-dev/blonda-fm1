/* Drivare för cykeluppskattningen: körs i en RISC-V-emulator (rv32imf, som ett ungefär av FM-1:ans pi32v2).
 * Värsta fallet: alla åtta trumljud slås an i början av mätningen och klingar under den (trumset PRESET). */
#include <stdint.h>
#include "puls_fm1.h"
#include "sliten_fm1.h"
static sliten_t tape;
extern int TAPE, PRESET, CORE;
int TAPE = 0, PRESET = 7, CORE = 0;           /* CORE 1: bara kärna 0:s del (trumljud 0..3 + summan), 2: kärna 1:s */
float L[256], R[256];
volatile int stage;
void *memset(void *d, int c, unsigned n) { unsigned char *p = d; while (n--) *p++ = (unsigned char)c; return d; }
void *memcpy(void *d, const void *s, unsigned n) { unsigned char *p = d; const unsigned char *q = s; while (n--) *p++ = *q++; return d; }
static void block(void)
{
    puls_render_begin(256);
    if (CORE != 2) puls_render_voices(0, CORE ? 4 : PULS_NV, 0);
    if (CORE != 1) puls_render_voices(CORE ? 4 : PULS_NV, PULS_NV, 1);
    if (CORE != 2) { puls_render_end(L, R); if (TAPE) sliten_process(&tape, L, R, 256); }
}
void _start(void)
{
    int i, v;
    puls_init();
    puls_load_preset(PRESET);
    puls_set(PP_chdec, 1.f); puls_set(PP_rsdec, 1.f); puls_set(PP_cpdec, 1.f);   /* allt klingar länge */
    sliten_init(&tape);
    sliten_set(&tape, SL_age, 0.75f); sliten_set(&tape, SL_sat, 0.6f); sliten_set(&tape, SL_avbr, 0.03f);
    for (i = 0; i < 4; i++) block();
    for (v = 0; v < PULS_NV; v++) puls_hit(v, 1.f);
    block();
    stage = 1;                                        /* mätningen börjar */
    for (i = 0; i < 4; i++) block();
    stage = 2;
    for (;;) {}
}
