/* SPDX-License-Identifier: GPL-3.0-only */
/* BLONDA PUMP för FM-1: ducking i takt, som en kompressor med sidokedja från en bastrumma som inte hörs.
 * På varje slag dras nivån ned (depth) och släpps sedan mjukt tillbaka under tre fjärdedelar av perioden.
 * Takten är den egna (bpm) eller MIDI-klockans när den kommer: tempot mäts mellan slagen och fasen dras mot
 * klockans slag, så att pumpen ligger kvar på ettan.
 *
 * pump_begin() och pump_gain() anropas från ljudet, pump_midi() där MIDI kommer in (huvudloopen eller ljudet).
 * Finns inte i blonda-motor: det här är FM-1-modulernas egen effekt. */
#ifndef PUMP_FM1_H
#define PUMP_FM1_H
#include <stdint.h>

#ifndef PUMP_SR
#define PUMP_SR 44100
#endif
#define PUMP_TICKS_HZ 24000000u                 /* fm1-dock:s ticks(): 24 MHz */
#define PUMP_NRATES 5                           /* 1/1, 1/2, 1/4, 1/8, 1/16 */

typedef struct {
    /* inställningar */
    float depth;                                /* 0..1: hur djupt nivån dras ned på slaget */
    int32_t rate;                               /* 0..4: en puls per 1/1, 1/2, 1/4, 1/8, 1/16 */
    float bpm;                                  /* egen takt, 40..240 */
    /* tillstånd */
    float ph, g;                                /* fas 0..1 i perioden, nivån just nu (utjämnad) */
    /* MIDI-klockan */
    volatile uint32_t ext, pending, beat_at, tick_at, beat_idx, tick_n, have_beat, fresh;
    volatile float ext_bpm;
} pump_t;

void pump_init(pump_t *p);
void pump_midi(pump_t *p, uint32_t status, uint32_t now_ticks);    /* F8 klocka, FA start, FB fortsätt, FC stopp */
void pump_begin(pump_t *p, uint32_t now_ticks);                    /* i början av varje ljudblock */
void pump_gain(pump_t *p, float *gain, uint32_t n);                /* nivån för n sampel (1 = orörd); stegar fasen */
float pump_tempo(const pump_t *p);                                 /* takten som gäller nu (klockans när den finns) */
/* MIDI-klockans läge, för det som ska gå i takt med den (ARP, SEQ): 0 ingen klocka; annars 1 och *beats =
 * fjärdedelar sedan klockans slag nummer (beat_idx & 15), med decimaler (heltal precis på slaget) */
int pump_clock(const pump_t *p, uint32_t now_ticks, float *beats);

#endif
