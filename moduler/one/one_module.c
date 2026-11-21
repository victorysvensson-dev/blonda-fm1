/* SPDX-License-Identifier: GPL-3.0-only */
/* BLONDA ONE som fm1-dock-modul för M-VAVE FM-1: en synth med två ljudmotorer.
 *
 * Varje ljud hör till en motor, och motorn byts med ljudet:
 *   3 OSC   trioscillatormotorn (ONE i blonda-motor: BRIS möter Moog): ljud 0-49
 *   BRIS    BRIS-motorn (efter GS Music Bree6): ljud 50-139
 * Ensemble, eko och utgång är gemensamma (src/bfx_fm1.c), så ekot klingar vidare över ett motorbyte. Efter
 * motorerna, för alla ljud: reverbet RUM, PUMP (ducking i takt) och ERA (band och lofi).
 *
 *   tangenter / MIDI   6 röster (poly stereo, poly, mono, unison), velocity, pitch bend, modhjul (CC1),
 *                      tryck (aftertouch), sustain (CC64), ljudbyte (program change; CC 0 = bank för ljud 128-139)
 *   PRESETS            byt ljud (140 st, laddas direkt)
 *   KNOB1..4           de fyra parametrarna på sidan som visas
 *   SELECT             nästa / förra sidan
 *   ALGORITHM          ERA: 0 MODERN (rent), 1..60 TAPE (kassett som slits mer och mer), 61..100 LOFI (digital:
 *                      färre bitar och lägre samplingsfrekvens). MIDI CC 77.
 *   SEL · EDIT · ENV · LFO · FX · GLO   sidorna (tryck igen: nästa sida). Vilka som finns beror på ljudets motor:
 *                      3 OSC har OSC 1, OSC 2, OSC 3 och MIX på EDIT, BRIS har en OSC-sida.
 *   FX                 FX (ensemble, eko) · RUM, RUM TONE, RUM SPACE (reverbet BLONDA RUM) · PUMP (ducking i
 *                      takt: klangen eller allt viker undan på slaget; egen takt eller MIDI-klocka).
 *                      Reverb och pump hör till modulen, inte till ljudet: de ligger kvar när man byter ljud.
 *                      MIDI CC 91 = reverbets mix, CC 92 = pumpens djup.
 *   ARP                arpeggiot: första trycket visar ARP-sidan och slår på det, nästa slår av. Tangenterna som hålls
 *                      spelas en i taget, i takt (TEMPO, eller MIDI-klockan); pedalen håller dem kvar.
 *   REC                stegsekvensern spelar in: varje ton eller ackord blir ett steg när tangenterna släpps.
 *                      Under inspelning: PLAY = tyst steg, SEQ = bind (förläng förra steget), REC = klar.
 *   PLAY               spelar sekvensen runt (MIDI start och stopp gör detsamma). Tangenterna flyttar den i tonhöjd.
 *   SEQ                SEQ-sidan: takt, tonernas längd, vad tangenterna gör under spel, tempo
 *   SAVE               sparar ljudet som det är nu, med ERA, reverb, pump, ARP och sekvensen; det laddas när ONE
 *                      startar nästa gång
 * Med KNOBS CC påslaget i fm1-dock kommer rattarna som CC 74, 71, 73, 72: cutoff, resonans, EG2 attack, release. */
#include "hb_module.h"
#include "bris_fm1.h"
#include "one_fm1.h"
#include "bfx_fm1.h"
#include "sliten_fm1.h"
#include "rum_fm1.h"
#include "pump_fm1.h"
#include "spel_fm1.h"
#include "ljud.h"
#include "blonda_dot.h"

#ifndef HB_F_MIDI_RT
#define HB_F_MIDI_RT 16u                        /* nyare fm1-dock: MIDI-klockan kommer i ljudavbrottet (annars huvudloopen) */
#endif
#define ONE_VERSION "V2.5"                      /* visas på skärmen; höj när modulen ändras (module.cfg: version) */

static const hb_host_t *H;
static char *put_int(char *s, int32_t v);
static char *put_str(char *s, const char *a);
static float clampf(float x, float lo, float hi);

/* ------------------------------------------------------------------ modulens egna effekter efter motorerna: RUM och PUMP */
/* numreras från XBASE, över motorernas egna parametrar, så att sidorna kan blanda dem */
#define XBASE 64
enum { XP_rmix = XBASE, XP_rsize, XP_rdecay, XP_rtype, XP_rlowc, XP_rhighc, XP_rdamp, XP_rmodd,
       XP_rpredel, XP_rwidth, XP_rduck, XP_rout, XP_pdepth, XP_prate, XP_ptarget, XP_bpm,
       XP_amode, XP_arate, XP_aoct, XP_agate, XP_srate, XP_sgate, XP_skeys, XP_touch, XP_END };
#define PNONE 255                               /* en tom rad på en sida */
#define XN (XP_END - XBASE)
typedef struct { const char *name; float lo, hi, def; uint8_t integer; int8_t rum; } xparam_t;
static const xparam_t XPAR[XN] = {
    {"Reverb Mix", 0.f, 1.f, 0.f, 0, RU_wet}, {"Reverb Size", 0.f, 1.f, 0.55f, 0, RU_size},
    {"Reverb Decay", 0.f, 1.f, 0.5f, 0, RU_decay}, {"Reverb Type", 0.f, 1.f, 0.f, 1, RU_shape},
    {"Low Cut", 0.f, 1.f, 0.25f, 0, RU_lowc}, {"High Cut", 0.f, 1.f, 1.f, 0, RU_highc},
    {"Damping", 0.f, 1.f, 0.45f, 0, RU_damp}, {"Movement", 0.f, 1.f, 0.35f, 0, RU_modd},
    {"Predelay", 0.f, 1.f, 0.15f, 0, RU_predel}, {"Width", 0.f, 1.f, 1.f, 0, RU_width},
    {"Duck", 0.f, 1.f, 0.f, 0, RU_duck}, {"Reverb Level", 0.f, 1.f, 0.75f, 0, RU_outv},
    {"Pump Depth", 0.f, 1.f, 0.f, 0, -1}, {"Pump Rate", 0.f, 4.f, 2.f, 1, -1},
    {"Pump Target", 0.f, 1.f, 0.f, 1, -1}, {"Tempo", 40.f, 240.f, 120.f, 1, -1},
    {"Arp Mode", 0.f, (float)(SPEL_NMODES - 1), 0.f, 1, -1}, {"Arp Rate", 0.f, (float)(SPEL_NRATES - 1), 3.f, 1, -1},
    {"Arp Octaves", 1.f, 4.f, 1.f, 1, -1}, {"Arp Gate", 0.05f, 1.f, 0.5f, 0, -1},
    {"Seq Rate", 0.f, (float)(SPEL_NRATES - 1), 3.f, 1, -1}, {"Seq Gate", 0.05f, 1.f, 0.5f, 0, -1},
    {"Seq Keys", 0.f, 1.f, 0.f, 1, -1},
    {"Pressure", 0.f, 1.f, 0.f, 0, -1},
};
static float xv[XN];
static rum_t rum;
static pump_t pump;
static volatile int rum_on;                     /* reverbet räknas (mixen, eller där den är på väg ner, över 0) */
static volatile int rum_switch;                 /* ett nytt ljud har andra reverbinställningar (render):
                                                 * 1: förra ljudets klang klingar vidare som den var, tills en ton spelas
                                                 *    eller den har klingat ut;
                                                 * 2: klangen tonas ut snabbt, sedan nollställs reverbet och ställs in efter
                                                 *    det nya ljudet, och mixen glider in */
static int loading;                             /* load_preset sätter ljudets effekter (set_x) */
static volatile uint32_t rum_gen;               /* räknas upp när reverbet nollställs: klang räknad i förväg gäller inte längre */

/* ------------------------------------------------------------------ sidor: en uppsättning per motor */
typedef struct { const char *title; uint8_t btn; uint8_t p[4]; } page_t;
/* TOUCH (GLO, efter motorns egna sidor): hur mycket tangenternas tryck verkar. FM-1:ans tangenter känner trycket,
 * och motorerna tar det som modhjulet (vibrato och filter, efter ljudets MW-rattar). Från början 0: trycket når
 * fullt utslag redan vid ett vanligt anslag, och då svajar tonhöjden i många ljud utan att man bett om det. */
#define T_PAGE {"TOUCH", HB_BTN_GLO, {XP_touch, PNONE, PNONE, PNONE}}
/* ARP och SEQ: sist i båda motorerna, på sina egna knappar */
#define S_PAGES \
    {"ARP", HB_BTN_ARP, {XP_amode, XP_arate, XP_aoct, XP_agate}}, \
    {"SEQ", HB_BTN_SEQ, {XP_srate, XP_sgate, XP_skeys, XP_bpm}}
/* reverbets och pumpens sidor, samma för båda motorerna (FX-knappen, efter motorns egen FX-sida) */
#define X_PAGES \
    {"RUM", HB_BTN_FX, {XP_rmix, XP_rsize, XP_rdecay, XP_rtype}}, \
    {"RUM TONE", HB_BTN_FX, {XP_rlowc, XP_rhighc, XP_rdamp, XP_rmodd}}, \
    {"RUM SPACE", HB_BTN_FX, {XP_rpredel, XP_rwidth, XP_rduck, XP_rout}}, \
    {"PUMP", HB_BTN_FX, {XP_pdepth, XP_prate, XP_ptarget, XP_bpm}}
static const page_t PAGES_ONE[] = {
    {"FILTER", HB_BTN_SEL, {OP_cut, OP_res, OP_eg1, OP_drv}},
    {"KEYS", HB_BTN_SEL, {OP_kbd, OP_velf, OP_vela, OP_bendr}},
    {"OSC 1", HB_BTN_EDIT, {OP_o1w, OP_o1r, OP_o1l, OP_pw}},
    {"OSC 2", HB_BTN_EDIT, {OP_o2w, OP_o2r, OP_o2f, OP_o2l}},
    {"OSC 3", HB_BTN_EDIT, {OP_o3w, OP_o3r, OP_o3f, OP_o3l}},
    {"MIX", HB_BTN_EDIT, {OP_o2s, OP_o3s, OP_noi, OP_detune}},
    {"EG2 AMP", HB_BTN_ENV, {OP_aa, OP_ad, OP_asus, OP_ar}},
    {"EG1 FILTER", HB_BTN_ENV, {OP_fa, OP_fd, OP_fs, OP_fr}},
    {"LFO", HB_BTN_LFO, {OP_lwave, OP_lrate, OP_lfoosc, OP_lfocut}},
    {"LFO MOD", HB_BTN_LFO, {OP_lfopwm, OP_lfoamp, OP_mwosc, OP_mwcut}},
    {"FX", HB_BTN_FX, {OP_chorus, OP_dtime, OP_dfb, OP_dmix}},
    X_PAGES,
    {"VOICE", HB_BTN_GLO, {OP_lage, OP_porta, OP_uniint, OP_vol}},
    T_PAGE,
    S_PAGES,
};
static const page_t PAGES_BRIS[] = {
    {"FILTER", HB_BTN_SEL, {BP_cut, BP_res, BP_eg1, BP_kbd}},
    {"OSC", HB_BTN_EDIT, {BP_vform, BP_sub, BP_pw, BP_detune}},
    {"EG2 AMP", HB_BTN_ENV, {BP_aa, BP_ad, BP_asus, BP_ar}},
    {"EG1 FILTER", HB_BTN_ENV, {BP_fa, BP_fd, BP_fs, BP_fr}},
    {"LFO", HB_BTN_LFO, {BP_lwave, BP_lrate, BP_lfoosc, BP_lfocut}},
    {"LFO MOD", HB_BTN_LFO, {BP_lfopwm, BP_lfoamp, BP_mwosc, BP_mwcut}},
    {"FX", HB_BTN_FX, {BP_chorus, BP_dtime, BP_dfb, BP_dmix}},
    X_PAGES,
    {"VOICE", HB_BTN_GLO, {BP_lage, BP_porta, BP_uniint, BP_vol}},
    {"PLAY", HB_BTN_GLO, {BP_velf, BP_vela, BP_bendr, BP_kbda}},
    T_PAGE,
    S_PAGES,
};

/* ------------------------------------------------------------------ motorerna */
/* (ENG_ONE, ENG_BRIS: src/ljud.h) */
typedef struct {
    const char *tag;                            /* på skärmen, före versionen */
    int np;                                     /* antal parametrar */
    void (*render_begin)(uint32_t n);
    void (*render_voices)(uint32_t first, uint32_t last, uint32_t mix);
    void (*render_end)(float *L, float *R);
    void (*note_on)(int note, int vel);
    void (*note_off)(int note);
    void (*all_notes_off)(void);
    void (*sustain)(int down);
    void (*bend)(float v);
    void (*modwheel)(float v);
    void (*aftertouch)(float v);
    void (*set)(int p, float v);
    float (*get)(int p);
    void (*load_preset)(int i);
    void (*reset_voices)(void);
    uint32_t (*meter_voices)(void);
    const page_t *pages;
    int npages;
    uint8_t cc[4];                              /* KNOBS CC 74, 71, 73, 72: cutoff, resonans, EG2 attack, release */
} engine_t;
static const engine_t ENGINES[2] = {
    {"3 OSC", OP_N, one_render_begin, one_render_voices, one_render_end, one_note_on, one_note_off, one_all_notes_off,
     one_sustain, one_bend, one_modwheel, one_aftertouch, one_set, one_get, one_load_preset, one_reset_voices,
     one_meter_voices, PAGES_ONE, (int)(sizeof PAGES_ONE / sizeof PAGES_ONE[0]), {OP_cut, OP_res, OP_aa, OP_ar}},
    {"BRIS", BP_N, bris_render_begin, bris_render_voices, bris_render_end, bris_note_on, bris_note_off, bris_all_notes_off,
     bris_sustain, bris_bend, bris_modwheel, bris_aftertouch, bris_set, bris_get, bris_load_preset, bris_reset_voices,
     bris_meter_voices, PAGES_BRIS, (int)(sizeof PAGES_BRIS / sizeof PAGES_BRIS[0]), {BP_cut, BP_res, BP_aa, BP_ar}},
};
static int eng = ENG_ONE;                       /* motorn som ljudet använder */
static const engine_t *volatile E = &ENGINES[ENG_ONE];
/* motorn som ljudavbrottet räknar. Den följer E: när ljudet byter motor tonar avbrottet ut den gamla motorns
 * röster under ett anrop (inget knäpp), nollställer dem och går över till den nya. */
static const engine_t *AE = &ENGINES[ENG_ONE];
#define PAGES (E->pages)
#define NPAGES (E->npages)

/* ljuden i ordning (src/ljud.h, genererad av tools/gen_ljud.py): 3 OSC 0-49, BRIS 1-89, BRIS init, 3 OSC 50-.
 * Varje ljud har sin motor, sitt ljud i motorn och sina effekter (reverb, pump, ERA). */
#define NPRESETS NLJUD
typedef char ljud_one_ok[ONE_NPRESETS + BRIS_NPRESETS - LJUD_BORT == NLJUD ? 1 : -1];   /* (motorernas ljud, utom de borttagna) */
static int preset_engine(int i) { return LJUD[i].eng; }
static int preset_index(int i) { return LJUD[i].idx; }
/* och tillbaka: ljud nr idx i motorn e, eller -1 */
static int preset_number(int e, int idx)
{
    int i;
    for (i = 0; i < NLJUD; i++) if (LJUD[i].eng == e && LJUD[i].idx == idx) return i;
    return -1;
}
static const char *preset_name(int i)
{
    return preset_engine(i) == ENG_ONE ? ONE_PRESETS[preset_index(i)].name : BRIS_PRESETS[preset_index(i)].name;
}
#define PMAX ((int)OP_N > (int)BP_N ? (int)OP_N : (int)BP_N)        /* flest parametrar i en motor (sparas) */

/* ------------------------------------------------------------------ tillstånd */
#define SET_MAGIC 0x32454E4Fu                   /* "ONE2" */
#define SET_VERSION 2                           /* 1 (ONE 2.0): utan Pressure, x[23] och sedan engine, arp_on */
typedef struct {
    uint32_t magic;
    uint8_t version, preset, page, edited;      /* preset: ljudets nummer i sin motor (inte i hela listan, som kan växa) */
    float p[PMAX];                              /* ljudets parametrar i sin motors ordning */
    float era;
    uint8_t x[XN];                              /* reverb, pump, ARP, SEQ och tryck: 0..100 (heltal och tempo som de är) */
    uint8_t engine, arp_on, pad[2];             /* ljudets motor (ENG_ONE, ENG_BRIS); ARP på */
} settings_t;                                   /* 232 byte (fm1-dock tar upp till 240) */
typedef char settings_fits[sizeof(settings_t) <= 240 ? 1 : -1];
typedef char settings_v1_same_size[sizeof(settings_t) == 232 ? 1 : -1];   /* (version 1 laddas i samma post) */
/* ONE 1.0 (bara trioscillatormotorn, utan reverb och pump): laddas, så att ett sparat ljud följer med */
#define SET1_MAGIC 0x31454E4Fu                  /* "ONE1" */
typedef struct {
    uint32_t magic;
    uint8_t version, preset, page, edited;
    float p[OP_N];
    float era;
} settings1_t;                                  /* 204 byte */

static int preset = 1, page, edited;
static volatile uint32_t saved_ms;              /* när SAVE trycktes (meddelandet visas en stund) */
static uint32_t at_max;                          /* största tangenttrycket just nu */
static float last_bend, last_mw, last_at;       /* böj, hjul och tryck just nu: följer med när motorn byts */
static volatile float at_target;                /* trycket gånger Pressure: dit motorns aftertouch ska */
static float at_now;                            /* där den är (utjämnad i ljudet, så att trycket inte kommer som ett hack) */
static int last_sus, bank;                      /* sustainpedalen; MIDI-banken (CC 0) för ljud över 127 */

/* vad som syns, för att bara rita om det som ändrats */
static struct { int preset, page, edited, saved, era, eng, row[4]; uint32_t voices, spel; int32_t val[4]; } shown;
static uint32_t seq_saved;                      /* spel_seq_changed när sekvensen senast sparades eller laddades */

/* Skärmen i BLONDA-stil: gul prickdisplay. Ljudets namn står i prickar (5 x 7 per tecken, src/blonda_dot.h) i ett
 * fönster med svaga prickar bakom, som displayen i BLONDAs pluggar och SEQ.3; rattarnas värden visas som prickrader.
 * Färgerna är BLONDAs: gult E9C54A, nästan svart 17181A, petrol 227E7A, kräm E8E6DC. Text ritas av fm1-dock, som
 * alltid lägger svart bakom den: därför svart botten utanför namnfönstret. */
static const uint16_t C_YEL = HB_RGB(0xE9, 0xC5, 0x4A),  /* tända prickar, värden, rösterna */
                      C_DIM = HB_RGB(0x40, 0x39, 0x1E),  /* släckta prickar: samma gula, svagt */
                      C_PANEL = HB_RGB(0x17, 0x18, 0x1A),    /* namnfönstrets botten */
                      C_GRID = HB_RGB(0x2C, 0x29, 0x1B), /* fönstrets svaga prickar */
                      C_LBL = HB_RGB(0xC4, 0xC2, 0xB8),  /* parametrarnas namn (kräm, lite dämpad) */
                      C_HEAD = HB_RGB(0x34, 0xA8, 0xA2), /* nummer, motor, sidan (petrol, ljusare för att läsas) */
                      C_RULE = HB_RGB(0x22, 0x7E, 0x7A), /* den prickade linjen */
                      C_BLACK = 0;
#define C_VAL C_YEL
/* platserna på skärmen (under fm1-dock:s rad, HB_TOP = 24) */
#define Y_HEAD 26                               /* nummer, motor och version, ERA */
#define Y_NAME 46                               /* namnfönstret, DOT_H = 48 högt */
#define Y_PAGE 98                               /* sidans namn, rösterna */
#define Y_RULE 117
#define Y_ROW 122                               /* fyra rader om ROW_H */
#define ROW_H 30
#define NDOT 28                                 /* prickar i en rad: 28 x 8 = 224 bildpunkter */

/* ------------------------------------------------------------------ ERA: band och lofi efter motorerna */
static sliten_t tape;
static volatile int32_t era;                    /* 0..100, ALGORITHM-ratten */
#define OUT_GAIN 2.f                            /* utgångens förstärkning (+6 dB, 2.5): ljuden låg lågt mot annat */
#define OUT_CEIL 0.944f                         /* begränsarens tak (-0,5 dBFS) */
#define OUT_REL 0.00015f                        /* begränsaren släpper: omkring 0,15 s */
static float lim_g = 1.f;                       /* begränsarens förstärkning (1: ingen) */
static int tape_on;                             /* SLITEN räknar (även medan den tonar ut) */

/* ratten till SLITEN:s parametrar (BLONDA SLITEN i blonda-motor). Inga avbrott: de digitala mediernas
 * upprepning behöver en längre buffert än modulen har plats för. */
static void era_apply(void)
{
    int32_t e = era;
    if (e <= 0) { sliten_set(&tape, SL_wet, 0.f); return; }
    if (e <= 60) {                              /* TAPE: kassett som slits mer och mer */
        float t = (float)e / 60.f;
        sliten_set(&tape, SL_media, 0.f);
        sliten_set(&tape, SL_age, 0.2f + 0.8f * t);
        sliten_set(&tape, SL_wow, 0.35f);
        sliten_set(&tape, SL_flut, 0.3f);
        sliten_set(&tape, SL_sat, 0.3f + 0.4f * t);
        sliten_set(&tape, SL_brus, 0.f);        /* inget bandbrus (2.5): bandet hörs i svaj, mättnad och ton, men det
                                                 * brusar inte när inget spelas */
        sliten_set(&tape, SL_avbr, 0.15f * t * t);
        sliten_set(&tape, SL_ton, 0.62f - 0.25f * t);
    } else {                                    /* LOFI: digitalt, färre bitar och lägre samplingsfrekvens */
        float t = (float)(e - 60) / 40.f;
        sliten_set(&tape, SL_media, 2.f);
        sliten_set(&tape, SL_age, 0.35f + 0.65f * t);
        sliten_set(&tape, SL_sat, 0.4f);
        sliten_set(&tape, SL_avbr, 0.f);
        sliten_set(&tape, SL_ton, 0.5f - 0.25f * t);
    }
    sliten_set(&tape, SL_bredd, 1.f);
    sliten_set(&tape, SL_outv, 0.75f);
    sliten_set(&tape, SL_wet, 1.f);
}
static void set_era(int32_t e)
{
    era = e < 0 ? 0 : e > 100 ? 100 : e;
    if (era > 0 && !tape_on) { sliten_reset(&tape); tape.sWet = 0.f; }   /* tonar in från rent */
    era_apply();
    if (era > 0) tape_on = 1;
}
static char *era_text(char *s)
{
    if (era <= 0) return put_str(s, "MODERN");
    return put_int(put_str(s, era <= 60 ? "TAPE " : "LOFI "), era);
}

/* ------------------------------------------------------------------ hjälp */
static float clampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }

static char *put_int(char *s, int32_t v)
{
    char t[12];
    int n = 0;
    if (v < 0) { *s++ = '-'; v = -v; }
    do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v && n < 11);
    while (n) *s++ = t[--n];
    *s = 0;
    return s;
}
static char *put_str(char *s, const char *a) { while (*a) *s++ = *a++; *s = 0; return s; }

/* en parameter på en sida: motorns egen (under XBASE) eller modulens (RUM, PUMP) */
static float par_get(int p) { return p < XBASE ? E->get(p) : xv[p - XBASE]; }
static const char *par_name(int p) { return p >= XBASE ? XPAR[p - XBASE].name : eng == ENG_ONE ? ONE_PARAMS[p].name : BRIS_PARAMS[p].name; }
static float par_lo(int p) { return p >= XBASE ? XPAR[p - XBASE].lo : eng == ENG_ONE ? ONE_PARAMS[p].lo : BRIS_PARAMS[p].lo; }
static float par_hi(int p) { return p >= XBASE ? XPAR[p - XBASE].hi : eng == ENG_ONE ? ONE_PARAMS[p].hi : BRIS_PARAMS[p].hi; }
static int par_integer(int p) { return p >= XBASE ? XPAR[p - XBASE].integer : eng == ENG_ONE ? ONE_PARAMS[p].integer : BRIS_PARAMS[p].integer; }

/* takten som visas: klockans när den kommer, annars den egna */
static int32_t tempo_shown(void) { return (int32_t)(pump_tempo(&pump) + 0.5f); }

/* parameterns värde som text, och som 0..1000 för stapeln */
static int32_t value_text(int p, char *s)
{
    static const char *const VF[3] = {"SAW", "PULSE", "SAW+PULSE"};
    static const char *const WF[3] = {"TRIANGLE", "SAW", "PULSE"};
    static const char *const RG[5] = {"32'", "16'", "8'", "4'", "2'"};
    static const char *const LW[5] = {"TRIANGLE", "RAMP UP", "RAMP DOWN", "SQUARE", "S&H"};
    static const char *const MD[4] = {"POLY ST", "POLY", "MONO", "UNISON"};
    static const char *const PR[5] = {"1/1", "1/2", "1/4", "1/8", "1/16"};
    static const char *const AM[SPEL_NMODES] = {"UP", "DOWN", "UP/DOWN", "RANDOM", "ORDER"};
    static const char *const SR_[SPEL_NRATES] = {"1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32"};
    float v = par_get(p), lo = par_lo(p), hi = par_hi(p);
    int32_t bar = (int32_t)((v - lo) / (hi - lo) * 1000.f + 0.5f);
    int32_t iv = (int32_t)(v < 0.f ? v - 0.5f : v + 0.5f);
    int one = eng == ENG_ONE, br = eng == ENG_BRIS;
    if (p == XP_rtype) put_str(s, iv ? "ROOM" : "HALL");
    else if (p == XP_prate) put_str(s, PR[iv < 0 ? 0 : iv > 4 ? 4 : iv]);
    else if (p == XP_ptarget) put_str(s, iv ? "ALL" : "REVERB");
    else if (p == XP_amode) put_str(s, AM[iv < 0 ? 0 : iv >= SPEL_NMODES ? SPEL_NMODES - 1 : iv]);
    else if (p == XP_arate || p == XP_srate) put_str(s, SR_[iv < 0 ? 0 : iv >= SPEL_NRATES ? SPEL_NRATES - 1 : iv]);
    else if (p == XP_aoct) put_int(s, iv);
    else if (p == XP_skeys) put_str(s, iv ? "PLAY" : "TRANSPOSE");
    else if (p == XP_bpm) {                     /* med MIDI-klocka: klockans takt */
        if (pump.ext) { put_int(put_str(s, "EXT "), tempo_shown()); bar = (tempo_shown() - 40) * 1000 / 200; }
        else put_str(put_int(s, iv), " BPM");
    }
    else if (p >= XBASE) put_int(s, (int32_t)(v * 100.f + 0.5f));
    else if (br && p == BP_vform) put_str(s, VF[iv < 0 ? 0 : iv > 2 ? 2 : iv]);
    else if (one && (p == OP_o1w || p == OP_o2w || p == OP_o3w)) put_str(s, WF[iv < 0 ? 0 : iv > 2 ? 2 : iv]);
    else if (one && (p == OP_o1r || p == OP_o2r || p == OP_o3r)) put_str(s, RG[iv < -2 ? 0 : iv > 2 ? 4 : iv + 2]);
    else if ((br && p == BP_lwave) || (one && p == OP_lwave)) put_str(s, LW[iv < 0 ? 0 : iv > 4 ? 4 : iv]);
    else if ((br && p == BP_lage) || (one && p == OP_lage)) put_str(s, MD[iv < 0 ? 0 : iv > 3 ? 3 : iv]);
    else if ((br && p == BP_uniint) || (one && (p == OP_uniint || p == OP_o2s || p == OP_o3s))) { if (iv > 0) *s++ = '+'; put_str(put_int(s, iv), " ST"); }
    else if ((br && p == BP_bendr) || (one && p == OP_bendr)) put_str(put_int(s, iv), " ST");
    else if (one && (p == OP_o2f || p == OP_o3f)) {      /* finstämning i cent: ±50 vid fullt utslag (o2f * |o2f| * 50) */
        float c = v * (v < 0.f ? -v : v) * 50.f;
        int32_t ic = (int32_t)(c < 0.f ? c - 0.5f : c + 0.5f);
        if (ic > 0) *s++ = '+';
        put_str(put_int(s, ic), " CT");
    }
    else put_int(s, (int32_t)(v * 100.f + (v < 0.f ? -0.5f : 0.5f)));   /* 0..100 (kbda -100..100) */
    return bar;
}

/* modulens egna effekter: värdet till reverbet eller pumpen. Reverbet nollställs när det slås på (mix från 0). */
static void set_x(int p, float v)
{
    const xparam_t *d = &XPAR[p - XBASE];
    v = clampf(v, d->lo, d->hi);
    if (d->integer) v = (float)(int32_t)(v + 0.5f);
    xv[p - XBASE] = v;
    if (d->rum >= 0) {
        /* Ett nytt ljud medan reverbet klingar: ljudavbrottet byter inställningar först när klangen tonat ut (ett hopp i
         * storlek, predelay eller filter hörs som ett knäpp eller ett glid i klangen). Ratten: direkt, och mixen glider
         * dit (rum_glide i init). */
        if (loading && rum_on) { if (rum_get(&rum, d->rum) != v || rum_switch) rum_switch = 1; }
        else {
            if (rum_switch == 1) {              /* en ratt vrids medan förra ljudets klang klingar: ljudets reverb nu */
                int k2;
                rum_switch = 0;
                for (k2 = 0; k2 <= XP_rout - XBASE; k2++) rum_set(&rum, XPAR[k2].rum, xv[k2]);
            }
            rum_set(&rum, d->rum, v);
        }
    }
    if (p == XP_rmix && v > 0.f && !rum_on) { rum_reset(&rum); rum_gen++; rum_on = 1; }
    else if (p == XP_pdepth) pump.depth = v;
    else if (p == XP_prate) pump.rate = (int32_t)v;
    else if (p == XP_bpm) pump.bpm = v;
    else if (p == XP_amode) spel_set.amode = (int32_t)v;
    else if (p == XP_arate) spel_set.arate = (int32_t)v;
    else if (p == XP_aoct) spel_set.aoct = (int32_t)v;
    else if (p == XP_agate) spel_set.agate = v;
    else if (p == XP_srate) spel_set.srate = (int32_t)v;
    else if (p == XP_sgate) spel_set.sgate = v;
    else if (p == XP_skeys) spel_set.skeys = (int32_t)v;
    else if (p == XP_touch) at_target = last_at * v;
}

static void set_param(int p, float v)
{
    if (p >= XBASE) {                           /* reverb, pump: hör till ljudet (stjärna); takt, ARP, SEQ: till modulen */
        set_x(p, v);
        if (p <= XP_ptarget) edited = 1;
        return;
    }
    E->set(p, v);
    edited = 1;
}

/* tangenterna: glöder (syns i mörkret); den man spelar på lyser fullt (värden tänder den medan den hålls) */
static void key_glow(uint32_t on)
{
    uint32_t k;
    for (k = 0; k < 27u; k++) H->led(HB_LED_KEY(k), on);
}

static void leds(void)
{
    static const uint8_t PAGE_BTNS[] = {HB_BTN_SEL, HB_BTN_EDIT, HB_BTN_ENV, HB_BTN_LFO, HB_BTN_FX, HB_BTN_GLO};
    unsigned i;
    for (i = 0; i < sizeof PAGE_BTNS; i++) H->led(PAGE_BTNS[i], PAGE_BTNS[i] == PAGES[page].btn ? 1u : 2u);
    H->led(HB_BTN_SAVE, 2u);
    H->led(HB_BTN_ARP, spel_set.arp_on ? 1u : 2u);                   /* lyser: arpeggiot är på */
    H->led(HB_BTN_SEQ, PAGES[page].btn == HB_BTN_SEQ ? 1u : 2u);
    H->led(HB_BTN_PLAY, spel_playing() ? 1u : spel_seq.len ? 2u : 0u);   /* glöder: det finns en sekvens att spela */
    H->led(HB_BTN_REC, spel_recording() ? 1u : 2u);
}

/* Ladda ett ljud. Hör det till den andra motorn byts motorn: den gamla släpper sina toner (ljudavbrottet tonar ut
 * och nollställer dess röster; ensemble, eko och reverb klingar vidare), och böj, hjul, tryck och pedal följer med
 * till den nya. Sidan behålls om den nya motorn har samma knapp (och lika många sidor in), annars motorns första.
 * Ljudet laddas i den nya motorn innan den tas i bruk, så att den aldrig hörs med fel ljud. */
static void load_preset(int i)
{
    const engine_t *old = E, *nw;
    int e, k, at = 0;
    preset = (i % NPRESETS + NPRESETS) % NPRESETS;
    e = preset_engine(preset);
    nw = &ENGINES[e];
    if (nw != old) {
        uint8_t btn = old->pages[page < old->npages ? page : 0].btn;
        for (k = 0; k < old->npages && k < page; k++) if (old->pages[k].btn == btn) at++;   /* vilken av knappens sidor */
        old->all_notes_off();
        nw->bend(last_bend); nw->modwheel(last_mw);     /* (trycket sätter ljudavbrottet, varje anrop) */
        nw->sustain(spel_set.arp_on ? 0 : last_sus);    /* (med ARP på håller pedalen arpeggiot, inte motorns toner) */
        page = 0;
        for (k = 0; k < nw->npages; k++) if (nw->pages[k].btn == btn) { page = k; break; }
        for (; at > 0 && page + 1 < nw->npages && nw->pages[page + 1].btn == btn; at--) page++;
    }
    nw->load_preset(preset_index(preset));
    eng = e;
    E = nw;
    loading = 1;
    for (k = 0; k <= XP_ptarget - XBASE; k++)   /* ljudets effekter: reverb, pump (src/ljud.h) */
        set_x(XBASE + k, XPAR[k].integer ? (float)LJUD[preset].fx[k] : (float)LJUD[preset].fx[k] * 0.01f);
    loading = 0;
    set_era((int32_t)LJUD[preset].fx[LJUD_NFX - 1]);   /* och ERA */
    edited = 0;
    if (nw != old) leds();
}

/* ------------------------------------------------------------------ ljud */

/* Den andra kärnan, när fm1-dock lånar ut den (c1_run), annars allt på kärna 0:
 *   röst 3..5 (i ljudets motor) medan kärna 0 räknar röst 0..2,
 *   sedan reverbets klang: medan kärna 0 räknar ensemble och eko, eller, när bandet (ERA) är på, klangen för
 *   NÄSTA anrop medan kärna 0 räknar bandet. Bandet är det tyngsta kärna 0 gör efter rösterna, så då väntar ingen
 *   kärna på den andra. Reverbet räknar i samma ordning i båda fallen (klang, in, mix, klang, in, mix ...).
 * Klangen för ett block räknas ur ljudet före blocket (rum_tail med min_delay = ett helt anrop, 256 sampel), så
 * den kan gå samtidigt. Det gör en predelay kortare än 5,8 ms till 5,8 ms; längre är exakt.
 *
 * Ett anrop räknas i bitar när ARP eller SEQ har ett steg mitt i det: fram till steget, sedan resten, så att
 * tonen börjar på rätt ställe (inom 16 sampel, 0,36 ms) och inte först vid nästa anrop (upp till 5,8 ms sent).
 *
 * Minnet är trångt (reverbet och ekot tar nästan allt), så anropets buffertar delas med motorerna (bfx_fm1.h):
 * motorns utgång skrivs över rösternas mix (bfx_block får skriva där den läser), och pumpens kurva ligger där
 * LFO:n låg (den behövs inte efter rösterna). */
static int c1_ok;
#define bufL (bfx_mixL[0])
#define bufR (bfx_mixR[0])
#define pgain bfx_lfo
static float wetL[BFX_MAXN], wetR[BFX_MAXN];    /* reverbets klang för anropet (eller redan för nästa: pre_n) */
static uint32_t pre_n, pre_gen;                 /* så många sampel klang är räknade i förväg, och för vilken nollställning */
static const engine_t *RE;                      /* motorn som det här anropet räknar (också på kärna 1) */
static void c1_voices(uint32_t arg) { (void)arg; RE->render_voices(3, 6, 1); }
/* (min_delay är alltid ett helt anrop: anropen är olika långa när ARP och SEQ delar dem, och predelayen ska inte ändras med dem) */
static void c1_tail(uint32_t k) { rum_tail(&rum, wetL, wetR, k, BFX_MAXN); }

/* Tangenter, pedal och knappar till ARP och SEQ: de köas här i huvudloopen och lämnas i början av nästa ljudanrop,
 * så att tonerna bara slås an från ljudet (i takt, på rätt sampel). En kö med en som skriver och en som läser. */
enum { Q_NOTE, Q_PEDAL, Q_CMD, Q_PANIC };
#define QN 64u
static volatile uint16_t q[QN];
static volatile uint32_t q_w, q_r, q_lost;
static volatile uint32_t clk_start, clk_stop;   /* MIDI start och stopp (kan komma i ljudavbrottet) */
static void q_put(uint32_t type, uint32_t a, uint32_t b)
{
    uint32_t w = q_w, nx = (w + 1u) & (QN - 1u);
    if (nx == q_r) { q_lost = 1; return; }      /* full (ljudet går inte?): hellre tyst än en ton som hänger */
    q[w] = (uint16_t)(type << 14 | (a & 127u) << 7 | (b & 127u));
    q_w = nx;
}
static void sp_note(int note, int vel)
{
    const engine_t *e = E;
    if (vel) {
        if (rum_switch == 1) { rum_switch = 2; rum_glide(&rum, 1024); rum_set(&rum, RU_wet, 0.f); }   /* ny ton: förra
                                                 * ljudets klang tonas ut på 23 ms, så att tonen får det nya ljudets reverb */
        e->note_on(note, vel);
    } else e->note_off(note);
}
static void sp_sustain(int down) { E->sustain(down); }
static void sp_all_off(void) { E->all_notes_off(); }
static const spel_out_t SP_OUT = {sp_note, sp_sustain, sp_all_off};

static float r_bpm;                             /* takten i det här anropet */
static int r_ext;
/* nästa bit av anropet: fram till nästa steg i ARP eller SEQ (avrundat uppåt till 16 sampel), högst avail */
static uint32_t plan(uint32_t avail)
{
    uint32_t lim = avail > BFX_MAXN ? BFX_MAXN : avail, k = (spel_plan(lim, r_bpm) + 15u) & ~15u;
    return k > lim ? lim : k;
}

static void render(int32_t *out, uint32_t n)
{
    uint32_t now = H->ticks(), n_call = n;
    float beats = 0.f;
    pump_begin(&pump, now);
    r_ext = pump_clock(&pump, now, &beats);
    r_bpm = pump_tempo(&pump);
    if (r_ext) spel_sync(beats);
    if (q_lost) { q_lost = 0; q_r = q_w; spel_panic(); }
    if (clk_stop) { clk_stop = 0; spel_cmd(SPEL_CMD_STOP); }
    if (clk_start) { clk_start = 0; spel_cmd(SPEL_CMD_START); }
    while (q_r != q_w) {
        uint32_t e = q[q_r], a = e >> 7 & 127u, b = e & 127u;
        q_r = (q_r + 1u) & (QN - 1u);
        switch (e >> 14) {
        case Q_NOTE: spel_note((int)a, (int)b); break;
        case Q_PEDAL: spel_pedal((int)b); break;
        case Q_CMD: spel_cmd((int)b); break;
        default: spel_panic(); break;
        }
    }
    {   /* tangenternas tryck, mjukt: en tredjedel av vägen per anrop (framme på ett par hundradelar) */
        float d = at_target - at_now;
        at_now = d > 0.002f || d < -0.002f ? at_now + d * 0.3f : at_target;
        E->aftertouch(at_now);
    }
    spel_run(0, r_bpm, r_ext);                  /* det som ska börja nu (första tonen i ett arpeggio, PLAY) */
    /* reverbets mix glider (rum_glide), också ner till noll: klangen från förra ljudet tonar ut i stället för att
     * klippas, och reverbet slutar räknas först när den inte hörs */
    if (rum_switch && !rum_on) rum_switch = 0;
    if (rum_switch == 2 && rum_wet_now(&rum) <= 0.f) {   /* förra ljudets klang är uttonad: det nya ljudets reverb */
        int k2;
        for (k2 = 0; k2 <= XP_rout - XBASE; k2++) rum_set(&rum, XPAR[k2].rum, xv[k2]);
        rum_reset(&rum); rum.gsnap = 0;          /* tomt (inget av den gamla klangen med nya längder); torr nivå glider */
        rum_glide(&rum, 8820);
        rum_gen++;
        rum_switch = 0;
    }
    if (rum_on && !rum_switch && xv[XP_rmix - XBASE] <= 0.f && rum_wet_now(&rum) <= 0.f) rum_on = 0;
    while (n) {
        uint32_t k = plan(n), i, have, nk;
        int two, rv = rum_on, tp = tape_on, tail_out = 0, pre_out = 0, pumping, target = (int)xv[XP_ptarget - XBASE];
        const engine_t *ne = E;                 /* ljudets motor nu */
        if (!rv || pre_gen != rum_gen) { pre_n = 0; pre_gen = rum_gen; }   /* reverbet av, eller nollställt sedan sist (set_x) */
        have = pre_n;
        RE = AE;
        RE->render_begin(k);
        two = c1_ok && H->c1_run(c1_voices, 0);
        RE->render_voices(0, two ? 3 : 6, 0);
        if (two && H->c1_wait() != 0) { c1_voices(0); two = 0; }   /* kärna 1 blev inte klar (och är tagen nu): räkna här */
        if (rv && have < k) {                   /* klangen är inte räknad i förväg (eller bara en del: anropen är olika långa) */
            if (have == 0 && two && H->c1_run(c1_tail, k)) tail_out = 1;
            else rum_tail(&rum, wetL + have, wetR + have, k - have, BFX_MAXN);
            have = k;
        }
        if (ne != RE) {                         /* ljudet har bytt motor: den gamla motorns röster tonas ut här */
            float g = 1.f, dg = 1.f / (float)k;
            for (i = 0; i < k; i++) {
                g -= dg;
                bfx_mixL[0][i] *= g; bfx_mixR[0][i] *= g; bfx_mixL[1][i] *= g; bfx_mixR[1][i] *= g;
            }
        }
        RE->render_end(bufL, bufR);
        if (ne != RE) { RE->all_notes_off(); RE->reset_voices(); AE = ne; }   /* (också toner som slagits an sedan bytet) */
        spel_run(k, r_bpm, r_ext);              /* stegen som ska komma nu: tonerna börjar i nästa bit */
        nk = n - k ? plan(n - k) : plan(n_call);    /* så lång blir nästa bit (nästa anrop väntas bli lika långt som det här) */
        pumping = pump.depth > 0.f || pump.g < 0.9999f;
        pump_gain(&pump, pgain, k);
        if (rv) {
            if (tail_out && H->c1_wait() != 0) {                    /* klangen blev inte klar: tyst klang i det här blocket */
                for (i = 0; i < k; i++) wetL[i] = wetR[i] = 0.f;
                rum_resync(&rum, k);
            }
            if (rum_switch == 1) {              /* förra ljudets klang: har den klingat ut (under 16 bitar)? */
                float m = 0.f;
                for (i = 0; i < k; i++) { float a = wetL[i] < 0.f ? -wetL[i] : wetL[i], b = wetR[i] < 0.f ? -wetR[i] : wetR[i]; if (a > m) m = a; if (b > m) m = b; }
                if (m < 3e-5f) { rum_switch = 2; rum_glide(&rum, 1024); rum_set(&rum, RU_wet, 0.f); }
            }
            if (pumping && target == 0) for (i = 0; i < k; i++) { wetL[i] *= pgain[i]; wetR[i] *= pgain[i]; }
            rum_feed(&rum, bufL, bufR, k);
            rum_mix(&rum, bufL, bufR, wetL, wetR, k);
            pre_n = have - k;
            if (pre_n) for (i = 0; i < pre_n; i++) { wetL[i] = wetL[i + k]; wetR[i] = wetR[i + k]; }   /* resten först */
            else if (tp && two && H->c1_run(c1_tail, nk)) pre_out = 1;   /* nästa bits klang, medan bandet räknas här */
        }
        if (pumping && target != 0) for (i = 0; i < k; i++) { bufL[i] *= pgain[i]; bufR[i] *= pgain[i]; }
        if (tp) {
            sliten_process(&tape, bufL, bufR, k);
            if (era <= 0 && tape.sWet < 0.0005f) tape_on = 0;   /* helt uttonad: räkna inte längre */
        }
        for (i = 0; i < k; i++) {               /* utgången: OUT_GAIN högre, och en begränsare som håller topparna
                                                 * under OUT_CEIL (släpper på OUT_REL); så klipper inget */
            float l = bufL[i] * OUT_GAIN, r = bufR[i] * OUT_GAIN, a = l < 0.f ? -l : l, b2 = r < 0.f ? -r : r;
            lim_g += (1.f - lim_g) * OUT_REL;
            if (b2 > a) a = b2;
            if (a * lim_g > OUT_CEIL) lim_g = OUT_CEIL / a;
            l = clampf(l * lim_g, -1.f, 1.f) * 32767.f; r = clampf(r * lim_g, -1.f, 1.f) * 32767.f;
            out[2u * i] += (int32_t)l;
            out[2u * i + 1u] += (int32_t)r;
        }
        if (pre_out) {                          /* jobbet lämnas aldrig kvar efter anropet */
            if (H->c1_wait() != 0) {
                for (i = 0; i < nk; i++) wetL[i] = wetR[i] = 0.f;
                rum_resync(&rum, nk);
            }
            pre_n = nk;
        }
        out += 2u * k;
        n -= k;
    }
}

/* ------------------------------------------------------------------ MIDI */
static void note(uint32_t ch, uint32_t key, uint32_t vel)
{
    (void)ch;
    if (key > 127u) return;
    q_put(Q_NOTE, key, vel > 127u ? 127u : vel);   /* till ARP och SEQ, och därifrån till motorn */
}

static void cc(uint32_t ch, uint32_t num, uint32_t val)
{
    float v = (float)val / 127.f;
    (void)ch;
    switch (num) {
    case 0: bank = val ? 1 : 0; break;          /* bank: program change ger då ljud 128 och uppåt */
    case 1: last_mw = v; E->modwheel(v); break;
    case 64: last_sus = val >= 64u; q_put(Q_PEDAL, 0, (uint32_t)last_sus); break;
    case 120: case 123: q_put(Q_PANIC, 0, 0); break;
    case 74: set_param(E->cc[0], v); break;     /* fm1-dock:s KNOBS CC */
    case 71: set_param(E->cc[1], v); break;
    case 73: set_param(E->cc[2], v); break;
    case 72: set_param(E->cc[3], v); break;
    case 77: set_era((int32_t)(val * 100u + 63u) / 127); edited = 1; break;   /* ERA (hör till ljudet) */
    case 91: set_x(XP_rmix, v); edited = 1; break;     /* reverbets mix */
    case 92: set_x(XP_pdepth, v); edited = 1; break;   /* pumpens djup */
    default: break;
    }
}

static void midi(uint32_t st, uint32_t d1, uint32_t d2)
{
    if (st >= 0xF8u) {                          /* klocka och transport: pumpen, ARP och SEQ följer */
        pump_midi(&pump, st, H->ticks());
        if (st == 0xFAu) clk_start = 1;         /* start: sekvensen från början */
        else if (st == 0xFCu) clk_stop = 1;
        return;
    }
    switch (st & 0xF0u) {
    case 0x90: note(st & 15u, d1, d2); break;
    case 0x80: note(st & 15u, d1, 0); break;
    case 0xB0: cc(st & 15u, d1, d2); break;
    case 0xC0: { int i = (int)d1 + bank * 128; if (i < NPRESETS) load_preset(i); break; }
    case 0xD0: last_at = (float)d1 / 127.f; at_target = last_at * xv[XP_touch - XBASE]; break;
    case 0xA0:                                  /* tryck per tangent (FM-1:ans tangenter): det största gäller */
        at_max = d2 > at_max ? d2 : (d2 == 0u ? 0u : at_max);
        last_at = (float)at_max / 127.f;
        at_target = last_at * xv[XP_touch - XBASE];
        break;
    case 0xE0: last_bend = (float)((int32_t)((d2 << 7) | d1) - 8192) / 8192.f; E->bend(last_bend); break;
    default: break;
    }
}

/* ------------------------------------------------------------------ panelen */
static void turn(int p, int32_t clicks)
{
    float step;
    if (p == PNONE) return;
    step = par_integer(p) ? 1.f : (par_hi(p) - par_lo(p)) * 0.01f;
    set_param(p, par_get(p) + step * (float)clicks);
}

static void ui(uint32_t ev, int32_t arg)
{
    uint32_t id = HB_EV_ID(ev);
    if (HB_EV_KIND(ev) == HB_EV_ENC) {
        if (id >= HB_ENC_KNOB1 && id <= HB_ENC_KNOB4) turn(PAGES[page].p[id - HB_ENC_KNOB1], arg);
        else if (id == HB_ENC_PRESETS) load_preset(preset + arg);
        else if (id == HB_ENC_SELECT) { page = ((page + arg) % NPAGES + NPAGES) % NPAGES; leds(); }
        else if (id == HB_ENC_ALGORITHM) { set_era(era + arg); edited = 1; }
        return;
    }
    if (HB_EV_KIND(ev) != HB_EV_BTN_DOWN) return;
    if (id == HB_BTN_SAVE) {
        settings_t s;
        int i;
        s.magic = SET_MAGIC; s.version = SET_VERSION; s.preset = (uint8_t)preset_index(preset); s.page = (uint8_t)page; s.edited = (uint8_t)edited;
        s.engine = (uint8_t)eng; s.arp_on = (uint8_t)spel_set.arp_on; s.pad[0] = s.pad[1] = 0;
        for (i = 0; i < PMAX; i++) s.p[i] = i < E->np ? E->get(i) : 0.f;
        s.era = (float)era;
        for (i = 0; i < XN; i++) s.x[i] = (uint8_t)(XPAR[i].integer ? xv[i] + 0.5f : xv[i] * 100.f + 0.5f);
        H->save(&s, sizeof s);
        /* sekvensen, om den är ny: i modulens egen flashbank (fm1-dock; ljudet stannar till ~0,1 s medan den skrivs) */
        if (seq_saved != spel_seq_changed && !spel_recording() && HB_HOST_HAS(H, bank_save) && H->bank_save) {
            uint32_t c = spel_seq_changed;
            if (H->bank_save(&spel_seq, sizeof spel_seq) == 0) seq_saved = c;
        }
        saved_ms = H->ms() | 1u;
        return;
    }
    if (id == HB_BTN_PLAY) { q_put(Q_CMD, 0, SPEL_CMD_PLAY); return; }   /* spela / stanna; under inspelning: tyst steg */
    if (id == HB_BTN_REC) {                     /* spela in / klar; inspelningen visas på SEQ-sidan */
        int i;
        if (!spel_recording()) for (i = 0; i < NPAGES; i++) if (PAGES[i].btn == HB_BTN_SEQ) page = i;
        q_put(Q_CMD, 0, SPEL_CMD_REC);
        leds();
        return;
    }
    if (id == HB_BTN_SEQ && spel_recording()) { q_put(Q_CMD, 0, SPEL_CMD_TIE); return; }   /* bind: förra steget ett steg till */
    {   /* sidknapparna: första trycket öppnar sidan, nästa växlar till sidans andra halva */
        int i, first = -1, next = -1;
        for (i = 0; i < NPAGES; i++)
            if (PAGES[i].btn == id) {
                if (first < 0) first = i;
                if (i > page && next < 0 && PAGES[page].btn == id) next = i;
            }
        if (first < 0) return;
        if (id == HB_BTN_ARP) {                 /* första trycket visar sidan och slår på arpeggiot, nästa slår av och på */
            if (page == first) spel_set.arp_on = !spel_set.arp_on;
            else spel_set.arp_on = 1;
        }
        page = next >= 0 ? next : first;
        leds();
    }
}

/* en rund prick (d x d, hörnen bortskurna) på duken */
static void cv_dot(int32_t x, int32_t y, int32_t d, uint16_t c)
{
    H->cv_rect(x + 1, y, d - 2, d, c);
    H->cv_rect(x, y + 1, d, d - 2, c);
}

/* ljudets namn i prickar: ett fönster med svaga prickar, namnet tänt i gult. Långa namn får mindre prickar, och
 * de längsta två rader (dot_layout). */
static void draw_name(const char *s)
{
    int len1, start2, pitch = dot_layout(s, &len1, &start2), lines = start2 ? 2 : 1, d = pitch - 1;
    int cols = DOT_W / pitch, rows = DOT_H / pitch, x0 = 2, y0 = (DOT_H - (rows * pitch - 1)) / 2;
    int line, i, gr, gc, x, y;
    H->cv_begin(DOT_W, DOT_H, C_PANEL);
    for (gr = 0; gr < rows; gr++)
        for (gc = 0; gc < cols - 1; gc++) H->cv_rect(x0 + gc * pitch, y0 + gr * pitch, d, d, C_GRID);
    for (line = 0; line < lines; line++) {
        const char *t = line ? s + start2 : s;
        for (i = 0; t[i] && (line || i < len1); i++) {
            const uint8_t *g = dot_glyph(t[i]);
            if (!g) continue;
            for (gr = 0; gr < 7; gr++)
                for (gc = 0; gc < 5; gc++)
                    if (g[gr] >> (4 - gc) & 1) { dot_at(pitch, lines, line, i, gc, gr, &x, &y); H->cv_rect(x, y, d, d, C_YEL); }
        }
    }
    H->cv_blit(4, Y_NAME);
}

/* en rad: namnet, värdet till höger i gult, och värdet som en rad prickar */
static void draw_meter(int32_t y, int32_t lit)
{
    int i;
    H->cv_begin(NDOT * 8, 6, C_BLACK);
    for (i = 0; i < NDOT; i++) cv_dot(i * 8 + 1, 0, 6, i < lit ? C_YEL : C_DIM);
    H->cv_blit(8, y);
}
static void draw_row(int r)
{
    int p = PAGES[page].p[r];
    int32_t y = Y_ROW + r * ROW_H, bar, lit, tw;
    char s[24];
    if (p == PNONE) { H->fill(8, y, 224, 26, C_BLACK); return; }
    bar = value_text(p, s);
    tw = H->text_w(0, s);
    tw = tw > 90 ? 90 : tw;
    H->text(8, y, 142, 0, par_name(p), C_LBL, 0);
    H->fill(150, y, 82, 16, C_BLACK);           /* värdet högerställt */
    H->text(232 - tw, y, tw, 0, s, C_VAL, 0);
    lit = (bar * NDOT + 500) / 1000;            /* (minst en prick så fort värdet är över noll) */
    if (lit < 1 && bar > 0) lit = 1;
    draw_meter(y + 19, lit < 0 ? 0 : lit > NDOT ? NDOT : lit);
}

/* under inspelning visar SEQ-sidan vad knapparna gör i stället för rattarnas värden */
static void draw_hint(int r)
{
    static const char *const K[4] = {"KEYS", "PLAY", "SEQ", "REC"};
    static const char *const D[4] = {"ADD STEP", "REST", "TIE", "DONE"};
    int32_t y = Y_ROW + r * ROW_H, tw = H->text_w(0, D[r]);
    H->text(8, y, 142, 0, K[r], C_LBL, 0);
    H->fill(150, y, 82, 16, C_BLACK);
    H->text(232 - tw, y, tw, 0, D[r], C_VAL, 0);
    H->fill(8, y + 19, 224, 6, C_BLACK);
}

/* reverbet ska vara igång men är det inte: ljudavbrottet stängde av det (mixen hade glidit ner till noll) samtidigt som
 * en ny mix sattes. Huvudloopen startar det då här (rum_reset är för tung för ljudavbrottet). */
static void rum_check(void)
{
    if (!rum_on && xv[XP_rmix - XBASE] > 0.f) set_x(XP_rmix, xv[XP_rmix - XBASE]);
}

static void draw(uint32_t full)
{
    int r, retitle = 0;
    uint32_t vm = E->meter_voices();
    int saving = saved_ms && H->ms() - saved_ms < 1500u;
    /* ARP och SEQ: läget (spelar, spelar in, ARP på, sekvensens längd) och steget */
    int rec = spel_recording(), play = spel_playing(), step = spel_step();
    uint32_t sp = (uint32_t)play | (uint32_t)rec << 1 | (uint32_t)(spel_set.arp_on != 0) << 2 | (uint32_t)spel_seq.len << 8 | (uint32_t)step << 16;
    int on_arp, on_seq, hint;
    rum_check();
    if (full) {                                 /* (också när väljaren stängs: tangenternas glöd igen) */
        key_glow(2u);
        H->fill(0, HB_TOP, 240, 240 - HB_TOP, C_BLACK);
        shown.preset = shown.page = shown.edited = shown.saved = shown.era = shown.eng = -1;
        shown.voices = ~0u;
        shown.spel = ~sp;
    }
    if (eng != shown.eng) {                     /* en annan motor: andra sidor och parametrar, också med samma nummer */
        shown.page = -1;
        shown.eng = eng;
    }
    on_arp = PAGES[page].btn == HB_BTN_ARP; on_seq = PAGES[page].btn == HB_BTN_SEQ; hint = on_seq && rec;
    if (sp != shown.spel) {
        if ((sp ^ shown.spel) & 0xFFFFu) leds();
        retitle = on_arp || on_seq;
        shown.spel = sp;
    }
    if (preset != shown.preset || edited != shown.edited) {
        char s[40], *e;
        s[0] = (char)('0' + preset / 100); s[1] = (char)('0' + preset / 10 % 10); s[2] = (char)('0' + preset % 10); s[3] = 0;
        H->text(8, Y_HEAD, 40, 0, s, C_HEAD, 0);    /* ljudets nummer, sedan motorn och versionen */
        H->fill(48, Y_HEAD, 112, 16, C_BLACK);
        put_str(put_str(put_str(s, E->tag), " "), ONE_VERSION);
        H->text(48, Y_HEAD, 112, 0, s, C_HEAD, 0);
        e = put_str(s, preset_name(preset));
        if (edited) put_str(e, "*");
        draw_name(s);
        shown.preset = preset; shown.edited = edited;
    }
    if (page != shown.page || saving != shown.saved || retitle) {
        char s[24];
        int i, n = 0, k = 0;
        for (i = 0; i < NPAGES; i++) if (PAGES[i].btn == PAGES[page].btn) { n++; if (i <= page) k = n; }
        if (saving) put_str(s, "SAVED");
        else if (on_arp) put_str(put_str(s, "ARP  "), spel_set.arp_on ? "ON" : "OFF");
        else if (on_seq) {                      /* SEQ  REC STEP 5 · SEQ  PLAY 3/16 · SEQ  16 STEPS · SEQ  EMPTY */
            char *e = put_str(s, "SEQ  ");
            if (rec) put_int(put_str(e, "REC STEP "), step + 1);
            else if (play) { e = put_int(put_str(e, "PLAY "), step + 1); put_int(put_str(e, "/"), spel_seq.len); }
            else if (spel_seq.len) put_str(put_int(e, spel_seq.len), " STEPS");
            else put_str(e, "EMPTY");
        }
        else if (n > 1) { char *e = put_str(s, PAGES[page].title); e = put_str(e, "  "); e = put_int(e, k); e = put_str(e, "/"); put_int(e, n); }
        else put_str(s, PAGES[page].title);
        H->text(8, Y_PAGE, 150, 0, s, saving ? C_VAL : C_HEAD, 0);
        if (page != shown.page) {
            for (i = 0; i < 56; i++) H->fill(8 + i * 4, Y_RULE, 2, 2, C_RULE);   /* den prickade linjen */
            for (r = 0; r < 4; r++) shown.row[r] = -1;
        }
        shown.page = page; shown.saved = saving;
    }
    for (r = 0; r < 4; r++) {
        int p = hint ? 1000 + r : PAGES[page].p[r];
        int32_t v = hint || p == PNONE ? 0 : p == XP_bpm ? tempo_shown() + (pump.ext ? 1000 : 0) : (int32_t)(par_get(p) * 1000.f);
        if (shown.row[r] != p || shown.val[r] != v) {
            if (hint) draw_hint(r); else draw_row(r);
            shown.row[r] = p; shown.val[r] = v;
        }
    }
    if (vm != shown.voices) {                   /* sex prickar: rösterna som låter */
        H->cv_begin(66, 8, C_BLACK);
        for (r = 0; r < 6; r++) cv_dot(r * 11, 0, 8, vm >> r & 1u ? C_YEL : C_DIM);
        H->cv_blit(166, Y_PAGE + 4);
        shown.voices = vm;
    }
    if (era != shown.era) {                     /* ERA uppe till höger: MODERN, TAPE nn, LOFI nn */
        char s[16];
        int32_t tw;
        era_text(s);
        tw = H->text_w(0, s);
        H->fill(160, Y_HEAD, 72, 16, C_BLACK);
        H->text(232 - tw, Y_HEAD, tw, 0, s, era > 0 ? C_VAL : C_LBL, 0);
        shown.era = era;
    }
}

/* ------------------------------------------------------------------ in och ur */
/* sidan TOUCH:s nummer i motorn e (sparat av ONE 2.0, som inte hade den: sidorna efter flyttas ett steg) */
static int eng_pages_before_touch(int e)
{
    const engine_t *g = &ENGINES[e == ENG_BRIS ? ENG_BRIS : ENG_ONE];
    int i;
    for (i = 0; i < g->npages; i++) if (g->pages[i].p[0] == XP_touch) return i;
    return g->npages;
}

static int init(const hb_host_t *h)
{
    settings_t s;
    settings1_t s1;
    int i;
    H = h;
    saved_ms = 0;
    c1_ok = HB_HOST_HAS(H, c1_wait);
    bris_init();                                /* båda motorerna; ensemble och eko nollställs av dem (bfx) */
    one_init();
    eng = ENG_ONE; E = AE = &ENGINES[ENG_ONE];
    last_bend = last_mw = last_at = 0.f; last_sus = 0; bank = 0; at_max = 0;
    at_target = 0.f; at_now = 0.f;
    sliten_init(&tape);
    era = 0; tape_on = 0;
    era_apply();
    rum_init(&rum);
    rum_glide(&rum, 8820);                      /* mixen glider på 0,2 s när ljudet byts */
    pump_init(&pump);
    rum_on = 0; rum_gen++;
    spel_defaults();                            /* ARP av och en tom sekvens; den sparade sekvensen ur flashbanken */
    if (HB_HOST_HAS(H, bank_load) && H->bank_load
        && !(H->bank_load(&spel_seq, sizeof spel_seq) == 0 && spel_seq.magic == SPEL_MAGIC && spel_seq.len <= SPEL_STEPS))
        spel_seq_clear();
    seq_saved = spel_seq_changed;
    q_w = q_r = q_lost = 0; clk_start = clk_stop = 0;
    for (i = 0; i < XN; i++) set_x(XBASE + i, XPAR[i].def);
    i = H->load(&s, sizeof s) == 0 && s.magic == SET_MAGIC;
    if (i && s.version == 1) {                  /* ONE 2.0: en parameter färre (Pressure), så motorn och ARP låg en byte tidigare */
        s.arp_on = s.engine; s.engine = s.x[XN - 1];
        s.x[XN - 1] = 0;                        /* Pressure: av */
        if (s.page >= (eng_pages_before_touch(s.engine))) s.page++;   /* sidan TOUCH är ny, före ARP och SEQ */
        s.version = SET_VERSION;
    }
    if (i && s.version == SET_VERSION && preset_number(s.engine, s.preset) >= 0) {
        load_preset(preset_number(s.engine, s.preset));   /* motorn och det som inte sparas; sedan värdena som de sparades */
        page = s.page < NPAGES ? s.page : 0;
        for (i = 0; i < E->np; i++) E->set(i, s.p[i]);
        edited = s.edited;
        set_era((int32_t)s.era);
        for (i = 0; i < XN; i++) set_x(XBASE + i, XPAR[i].integer ? (float)s.x[i] : (float)s.x[i] * 0.01f);
        spel_set.arp_on = s.arp_on ? 1 : 0;
    } else if (H->load(&s1, sizeof s1) == 0 && s1.magic == SET1_MAGIC && s1.version == 1 && s1.preset < ONE_NPRESETS) {
        load_preset(s1.preset);                 /* ONE 1.0: samma ljud och nummer; reverb och pump på standard (av) */
        page = 0;                               /* samma sidor som då, utom VOICE som nu ligger efter reverbets */
        if (s1.page < 11) page = s1.page;
        else for (i = 0; i < NPAGES; i++) if (PAGES[i].btn == HB_BTN_GLO) { page = i; break; }
        for (i = 0; i < OP_N; i++) one_set(i, s1.p[i]);
        edited = s1.edited;
        set_era((int32_t)s1.era);
    } else {
        page = 0;
        load_preset(1);                         /* one */
    }
    AE = E;                                     /* ljudavbrottet går inte än: ingen uttoning att göra */
    spel_init(&SP_OUT);
    leds();
    key_glow(2u);
    H->log("one: redo");
    return 0;
}

static void release(void)
{
    bris_all_notes_off(); one_all_notes_off();
    bris_reset(); one_reset();
    sliten_reset(&tape);
    key_glow(0u);                               /* nästa modul har sina egna lampor */
    rum_on = 0;
}

HB_DESC = {HB_ABI, "ONE", init, release, note, cc, render, ui, draw, 0, 0, HB_F_MIDI_RT, midi};
