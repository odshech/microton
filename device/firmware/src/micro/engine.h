/* engine.h — the FM-1 microtonal synth engine: tuning + an 8-voice virtual analog synth.
 * Integers only (the FM-1 has no FPU), no 64-bit division. The same file builds for the FM-1 and for the browser.
 *
 * Threads: the eng_* control calls run in the main loop (or the browser's message handler);
 * eng_render runs in the audio interrupt (or the AudioWorklet). Notes travel through a queue, parameters and
 * scales through stores that are safe to see half-done. */
#pragma once
#include <stdint.h>

#define ENG_FS 44100u
#define ENG_BLOCK 16u                       /* control rate: envelopes, LFOs and pitch every 16 frames */
#define ENG_VOICES 8u
#define ENG_MAX_DEGREES 255u

enum {
#define P(id, key, def, max, kind) P_##id,
#include "params.def"
#undef P
    P_COUNT
};

#define ENG_PARTS 3u                        /* three sounds, one per melodic track, sharing the 8 voices */

void eng_init(void);
/* add n frames (a multiple of ENG_BLOCK) to out, L R L R ..., full scale ±32767 */
void eng_render(int32_t *out, uint32_t n);

/* notes are absolute scale degrees: 0 = the reference degree, n = the same degree one period up */
void eng_note_on(uint32_t part, int32_t degree, uint32_t velocity);   /* velocity 1..127 */
void eng_note_off(uint32_t part, int32_t degree);
void eng_all_off(void);
void eng_part_off(uint32_t part);

/* parameters go through the queue: applied at the start of the next render. 0 = queued */
int eng_set_param(uint32_t part, uint32_t id, uint32_t raw);
uint32_t eng_get_param(uint32_t part, uint32_t id);
uint32_t eng_param_max(uint32_t id);
void eng_bend(uint32_t part, int32_t value);                          /* -8192 .. 8191, in scale degrees */
void eng_modwheel(uint32_t part, uint32_t value);                     /* 0..127 */
void eng_aftertouch(uint32_t part, uint32_t value);                   /* 0..127 */

/* audio context only (inside eng_render, from the hook): the sequencer plays notes and locks parameters */
extern void (*eng_rt_hook)(uint32_t frames);                          /* called before every block of frames */
void eng_rt_on(uint32_t part, int32_t degree, int32_t velocity, int32_t fine_c16);
void eng_rt_off(uint32_t part, int32_t degree);
void eng_rt_part_off(uint32_t part);
void eng_rt_locks_clear(uint32_t part);
void eng_rt_lock(uint32_t part, uint32_t id, uint32_t raw);

void eng_set_ref_chz(uint32_t centihertz);               /* frequency of degree 0, in 0.01 Hz */
void eng_set_ref_degree(int32_t degree, uint32_t centihertz);   /* or of any degree; kept across scale changes */
int32_t eng_scale_c16(uint32_t k);                       /* the scale being played, k = 0 .. n-1 */
/* a scale as cents x 16: c16[k] = degree k+1 above the root, c16[n-1] = the period. 0 = loaded */
int eng_set_scale_c16(uint32_t n, const int32_t *c16);
/* one scale of an MTS1 library (see tools/scl-codec.js). 0 = loaded */
int eng_scale_from_lib(const uint8_t *lib, uint32_t len, uint32_t index);
uint32_t eng_scale_degrees(void);
uint32_t eng_voices_active(void);
int eng_rt_shed(void);
/* the load by section: set eng_clock (any free-running counter) and read eng_prof, ticks; zero it after reading */
enum { ENG_PROF_SYNTH, ENG_PROF_DRUMS, ENG_PROF_DELAY, ENG_PROF_REVERB, ENG_PROF_N };
extern uint32_t (*eng_clock)(void);
extern uint32_t eng_prof[ENG_PROF_N];                        /* audio context: drop one voice (overload); 1 = done */

/* ---- drums: eight FM drums (drums.c); parameters apply at the next block */
#define ENG_DRUMS 8u
enum {
#define D(id, key, max, kind) DP_##id,
#include "drums.def"
#undef D
    DP_COUNT
};
void eng_drum_hit(uint32_t drum, uint32_t velocity);                    /* queued, like a note */
void eng_drum_set(uint32_t drum, uint32_t id, uint32_t raw);
uint32_t eng_drum_get(uint32_t drum, uint32_t id);
uint32_t eng_drum_param_max(uint32_t id);
uint32_t eng_drum_param_def(uint32_t drum, uint32_t id);               /* the built-in kit */
void eng_rt_drum(uint32_t drum, int32_t velocity);                      /* audio context (the sequencer) */
void eng_set_rate(uint32_t hz);                                         /* the real output rate (browser) */

/* ---- effects: reverb and delay (fx.c), one of each, fed by sends */
enum {
#define F(id, key, def, max, kind) FX_##id,
#include "fx.def"
#undef F
    FX_COUNT
};
void eng_fx_set(uint32_t id, uint32_t raw);
uint32_t eng_fx_get(uint32_t id);
uint32_t eng_fx_param_max(uint32_t id);
uint32_t eng_fx_param_def(uint32_t id);
void eng_fx_tempo(uint32_t bpm_x100);                                   /* for the synced delay times */
