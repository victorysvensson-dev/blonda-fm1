/* Drivare för cykeluppskattningen: körs i en RISC-V-emulator (rv32imf, som ett ungefär av FM-1:ans pi32v2). */
#include <stdint.h>
#include "bris_fm1.h"
float L[256], R[256];
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
    for (i = 0; i < 6; i++) bris_note_on(six[i], 100);
    for (i = 0; i < 8; i++) bris_render(L, R, 256);   /* in i ljudet */
    stage = 1;                                        /* mätningen börjar */
    for (i = 0; i < 4; i++) bris_render(L, R, 256);
    stage = 2;
    for (;;) {}
}
