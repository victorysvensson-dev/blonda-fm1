/* Drivare för cykeluppskattningen: körs i en RISC-V-emulator (rv32imf, som ett ungefär av FM-1:ans pi32v2). */
#include <stdint.h>
#include "bris_fm1.h"
#include "sliten_fm1.h"
#include "rum_fm1.h"
#include "pump_fm1.h"
static sliten_t tape;
static rum_t rum;
static pump_t pump;
extern int TAPE, RUM;
int TAPE = 0, RUM = 0;                           /* RUM: 1 = klangen (kärna 1), 2 = mix och pump (kärna 0), 3 = båda */
float L[256], R[256], WL[256], WR[256], PG[256];
/* ett block som modulen räknar det: BRIS, sedan reverbet (klangen ur förra blocket), pumpen och bandet */
static void block(void)
{
    int i;
    bris_render(L, R, 256);
    if (RUM & 1) rum_tail(&rum, WL, WR, 256, 256);
    if (RUM & 2) {
        pump_gain(&pump, PG, 256);
        for (i = 0; i < 256; i++) { WL[i] *= PG[i]; WR[i] *= PG[i]; }
        rum_feed(&rum, L, R, 256);
        rum_mix(&rum, L, R, WL, WR, 256);
    }
    if (TAPE) sliten_process(&tape, L, R, 256);
}
volatile int stage;
void *memset(void *d, int c, unsigned n) { unsigned char *p = d; while (n--) *p++ = (unsigned char)c; return d; }
void *memcpy(void *d, const void *s, unsigned n) { unsigned char *p = d; const unsigned char *q = s; while (n--) *p++ = *q++; return d; }
void *memmove(void *d, const void *s, unsigned n) { unsigned char *p = d; const unsigned char *q = s; if (p < q) while (n--) *p++ = *q++; else while (n--) p[n] = q[n]; return d; }
extern int PRESET;
int PRESET = 1;
void _start(void)
{
    static const int six[6] = {48, 55, 60, 64, 67, 72};
    int i;
    bris_init();
    bris_load_preset(PRESET);
    sliten_init(&tape);
    sliten_set(&tape, SL_age, 0.75f); sliten_set(&tape, SL_sat, 0.6f); sliten_set(&tape, SL_avbr, 0.03f);
    rum_init(&rum); rum_set(&rum, RU_wet, 0.5f); rum_set(&rum, RU_highc, 0.8f); rum_set(&rum, RU_duck, 0.3f);
    pump_init(&pump); pump.depth = 0.8f;
    for (i = 0; i < 6; i++) bris_note_on(six[i], 100);
    for (i = 0; i < 8; i++) block();                  /* in i ljudet */
    stage = 1;                                        /* mätningen börjar */
    for (i = 0; i < 4; i++) block();
    stage = 2;
    for (;;) {}
}
