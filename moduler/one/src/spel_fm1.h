/* SPDX-License-Identifier: GPL-3.0-only */
/* ARP och SEQ för BLONDA ONE: det som står mellan tangenterna och ljudmotorn.
 *
 *   ARP   tangenterna som hålls spelas en i taget, i takt: upp, ned, upp och ned, slumpvis eller i den ordning
 *         de trycktes, över 1-4 oktaver. Pedalen (sustain) håller kvar tonerna i arpeggiot.
 *   SEQ   en sekvens på upp till 64 steg som spelas in steg för steg: med REC på blir varje ton (eller ackord,
 *         upp till sex toner) ett steg när tangenterna släpps. PLAY-knappen under inspelning ger ett tyst steg,
 *         SEQ-knappen förlänger förra steget (bindning). PLAY spelar sekvensen runt; tangenterna flyttar den då
 *         i tonhöjd (TRANSPOSE: tangenten blir sekvensens första ton) eller spelar ovanpå (PLAY).
 *
 * Allt här körs från ljudet (modulen köar tangenter och knappar från huvudloopen och lämnar dem i början av
 * varje anrop), så stegen hamnar på rätt sampel: spel_plan() säger hur långt det är till nästa händelse, modulen
 * räknar ljudet dit och anropar spel_run(). Inställningarna (spel_set) skrivs från huvudloopen: enstaka tal.
 * Ingen libm, inga double, ingen allokering. */
#ifndef SPEL_FM1_H
#define SPEL_FM1_H
#include <stdint.h>

#ifndef SPEL_SR
#define SPEL_SR 44100
#endif
#define SPEL_STEPS 64                           /* längsta sekvensen */
#define SPEL_POLY 6                             /* toner i ett steg (motorernas röster) */
#define SPEL_HELD 16                            /* tangenter i ett arpeggio */
#define SPEL_NRATES 6                           /* 1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32 */
enum { SPEL_UP, SPEL_DOWN, SPEL_UPDOWN, SPEL_RANDOM, SPEL_ORDER, SPEL_NMODES };
enum { SPEL_KEYS_TRANSPOSE, SPEL_KEYS_PLAY };
#define SPEL_TIE 0x80u                          /* spel_step_t.n: steget förlänger det förra */

typedef struct { uint8_t n, vel, note[SPEL_POLY]; } spel_step_t;   /* n: 0 tyst, 1..6 toner, SPEL_TIE */
typedef struct {                                /* sekvensen, som den sparas (516 byte) */
    uint8_t magic, len, root, pad;              /* root: första tonen (TRANSPOSE räknar från den) */
    spel_step_t st[SPEL_STEPS];
} spel_seq_t;
#define SPEL_MAGIC 0x51u

/* dit tonerna går: ljudets motor */
typedef struct {
    void (*note)(int note, int vel);            /* vel 0: släpp */
    void (*sustain)(int down);
    void (*all_off)(void);
} spel_out_t;

/* inställningar: skrivs från huvudloopen, läses i ljudet */
typedef struct {
    volatile int32_t arp_on, amode, arate, aoct, srate, skeys;
    volatile float agate, sgate;                /* 0,05..1: hur stor del av steget tonen hålls (1 = bundet) */
} spel_set_t;
extern spel_set_t spel_set;
extern spel_seq_t spel_seq;                     /* sekvensen (laddas och sparas av modulen när inget spelas in) */
extern volatile uint32_t spel_seq_changed;      /* räknas upp när en inspelning är klar */

enum { SPEL_CMD_PLAY, SPEL_CMD_REC, SPEL_CMD_TIE, SPEL_CMD_START, SPEL_CMD_STOP };

void spel_init(const spel_out_t *out);          /* nollställer allt utom sekvensen och inställningarna */
void spel_defaults(void);                       /* inställningarna och en tom sekvens */
void spel_seq_clear(void);                      /* en tom sekvens */
/* från ljudet: */
void spel_note(int note, int vel);              /* en tangent (vel 0: släppt) */
void spel_pedal(int down);
void spel_panic(void);                          /* alla toner av */
void spel_cmd(int cmd);                         /* PLAY: spela / stanna (tyst steg under inspelning); REC: spela in /
                                                 * klar; TIE: bind (under inspelning); START, STOP: MIDI-klockans */
/* Klockan utifrån (MIDI): beats = hur långt den kommit, i fjärdedelar (heltal på slaget). Stegen dras mot den. */
void spel_sync(float beats);
uint32_t spel_plan(uint32_t max, float bpm);    /* sampel till nästa händelse, 1..max (max: ingen före dess) */
void spel_run(uint32_t n, float bpm, int ext);  /* n sampel har gått: det som nu ska hända händer. ext: klockan
                                                 * kommer utifrån (ett nytt arpeggio väntar då in nästa steg) */
/* läget, för skärmen och lysdioderna */
int spel_playing(void);
int spel_recording(void);
int spel_step(void);                            /* steget som spelas, eller spelas in härnäst (0..) */

#endif
