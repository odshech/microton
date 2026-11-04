/* seq.h — the sequencer: 16 patterns of 6 tracks of up to 64 steps, a song of up to 64 rows.
 * It runs inside the engine's audio render (eng_rt_hook), block by block, so its timing is the audio clock's.
 * Steps store a scale degree and a period, never a MIDI note: a new scale re-reads the whole pattern.
 * Editing (seq_*_set, seq_op) is for the main loop; the playing side reads what it finds. Integers only. */
#pragma once
#include <stdint.h>

#define SEQ_TRACKS 6u
#define SEQ_STEPS 64u
#ifndef SEQ_PATTERNS
#define SEQ_PATTERNS 16u                      /* the FM-1 build: fewer in RAM (see device/) */
#endif
#define SEQ_NOTES 4u
#define SEQ_LOCKS 4u
#define SEQ_SONG 64u
#define SEQ_NO_LOCK 255u
#ifndef SEQ_POOL
#define SEQ_POOL                              /* the FM-1 build puts the big arrays in its 336 KB pool */
#endif

enum { TR_SYN1, TR_SYN2, TR_SYN3, TR_EXT, TR_DRUM_A, TR_DRUM_B };   /* tracks 0..2 play parts 0..2 */

/* step fields */
enum {
    SF_ON, SF_NOTES,                              /* active; notes in the step 1..4 (drums: a mask of 4 voices) */
    SF_DEG0, SF_DEG1, SF_DEG2, SF_DEG3,           /* degree within the period, 0..254 */
    SF_PER0, SF_PER1, SF_PER2, SF_PER3,           /* period offset, -3..3 */
    SF_CENTS,                                     /* -50..50, all notes of the step */
    SF_VEL,                                       /* 1..127 */
    SF_GATE,                                      /* 1..16 sixteenths of the step (of a ratchet hit); 17 = tie */
    SF_PROB,                                      /* 0..100 % */
    SF_RATCHET,                                   /* 1..4 hits in the step */
    SF_SLIDE,                                     /* 0/1: glide into the next step (legato) */
    SF_COND,                                      /* 0 = always; (a << 4) | b = play on pass a of every b, b 2..8 */
    SF_LOCK_ID0, SF_LOCK_ID1, SF_LOCK_ID2, SF_LOCK_ID3,     /* parameter id, or 255 */
    SF_LOCK_VAL0, SF_LOCK_VAL1, SF_LOCK_VAL2, SF_LOCK_VAL3, /* raw value 0..16383 */
    SF_COUNT
};
/* track fields (per pattern) */
enum {
    TF_LEN,                                       /* 1..64 */
    TF_SPEED,                                     /* 0 x2, 1 x1, 2 x1/2, 3 x1/4, 4 x4/3 (triplets), 5 x2/3 */
    TF_DIR,                                       /* 0 forward, 1 back, 2 ping-pong, 3 random */
    TF_SWING,                                     /* 50..75 % */
    TF_TRANSPOSE,                                 /* -64..64 degrees */
    TF_BASE_PER,                                  /* -3..3: the track's base period */
    TF_MUTE, TF_SOLO,                             /* 0/1 */
    TF_CHANNEL,                                   /* MIDI channel 1..16 (external and drum tracks) */
    TF_COUNT
};
/* pattern fields */
enum { PF_LEN, PF_SCALE, PF_COUNT };              /* master length 1..64 steps at x1; scale (library index, -1 keep) */

/* transport */
void seq_init(void);
void seq_play(void);
void seq_stop(void);
void seq_continue(void);
int seq_playing(void);
void seq_set_tempo(uint32_t bpm_x100);            /* 30.00 .. 300.00 BPM */
uint32_t seq_tempo(void);
void seq_queue_pattern(uint32_t p);               /* plays at the end of the current pattern (now, if stopped) */
uint32_t seq_pattern_playing(void);
int32_t seq_pattern_queued(void);                 /* -1 = none */
void seq_song_mode(int on);
int seq_song_on(void);
void seq_song_set(uint32_t row, uint32_t pattern, uint32_t repeats);   /* repeats 1..64; row >= length grows the song */
uint32_t seq_song_get(uint32_t row, uint32_t what);                    /* what 0 = pattern, 1 = repeats */
void seq_song_length(uint32_t rows);
uint32_t seq_song_len(void);
uint32_t seq_song_row(void);
int32_t seq_track_pos(uint32_t t);                /* the step now playing, -1 when stopped */
/* the scale the playing pattern asks for changed: the main loop applies it (tuning is main-loop only) */
int32_t seq_scale_request(void);                  /* -1 = none; reading clears it */

/* data */
int32_t seq_step_get(uint32_t p, uint32_t t, uint32_t s, uint32_t f);
void seq_step_set(uint32_t p, uint32_t t, uint32_t s, uint32_t f, int32_t v);
int32_t seq_track_get(uint32_t p, uint32_t t, uint32_t f);
void seq_track_set(uint32_t p, uint32_t t, uint32_t f, int32_t v);
int32_t seq_pattern_get(uint32_t p, uint32_t f);
void seq_pattern_set(uint32_t p, uint32_t f, int32_t v);
int seq_step_lock(uint32_t p, uint32_t t, uint32_t s, uint32_t id, uint32_t raw);   /* add or replace; 0 = done */
void seq_step_unlock(uint32_t p, uint32_t t, uint32_t s, uint32_t id);

/* editing, with one level of undo (each op snapshots its pattern first) */
enum {
    OP_CLEAR_STEP, OP_CLEAR_TRACK, OP_CLEAR_PATTERN,
    OP_COPY_STEP, OP_PASTE_STEP, OP_COPY_PAGE, OP_PASTE_PAGE,   /* page: a = 0..3, 16 steps */
    OP_COPY_TRACK, OP_PASTE_TRACK, OP_COPY_PATTERN, OP_PASTE_PATTERN,
    OP_SHIFT,                                     /* a = +1 / -1: the track rotates one step */
    OP_REVERSE, OP_DOUBLE,                        /* double: length x2, the steps repeated */
    OP_EUCLID,                                    /* a = hits, b = rotation, c = degree (drums: voice mask) */
    OP_RANDOM,                                    /* a = density %, b = lowest, c = highest absolute degree */
    OP_UNDO
};
int seq_op(uint32_t op, uint32_t p, uint32_t t, int32_t s, int32_t a, int32_t b, int32_t c);

/* MIDI out (external and drum tracks): the main loop drains it. 1 = a message */
int seq_midi_get(uint8_t *status, uint8_t *d1, uint8_t *d2);

/* the whole project as bytes, for saving */
uint8_t *seq_project_bytes(void);
uint32_t seq_project_size(void);
