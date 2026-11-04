/* Sequencer tests: timing in samples, read off the external track's MIDI out, plus the synth tracks' sound.
 * cc -O2 -I../engine test_seq.c ../engine/engine.c ../engine/tuning.c ../engine/seq.c -lm */
#include "engine.h"
#include "tuning.h"
#include "seq.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

typedef struct { long frame; int on, note, vel, chan; } ev;
static ev evs[20000];
static int nev;
static long frames;
static int32_t buf[2 * 16];
static double energy;

static void run(double seconds)                    /* render in blocks of 16, collect MIDI with its time */
{
    long end = frames + (long)(seconds * 44100);
    while (frames < end) {
        memset(buf, 0, sizeof buf);
        eng_render(buf, 16);
        for (int i = 0; i < 32; i++) energy += (double)buf[i] * buf[i];
        uint8_t s, a, b;
        while (seq_midi_get(&s, &a, &b))
            if (nev < 20000) { evs[nev].frame = frames; evs[nev].on = (s & 0xF0) == 0x90 && b; evs[nev].note = a; evs[nev].vel = b; evs[nev].chan = (s & 15) + 1; nev++; }
        frames += 16;
    }
}
static void fresh(void)
{
    eng_init(); tun_edo(12, 19200); seq_init(); nev = 0; frames = 0; energy = 0;
    for (uint32_t t = 0; t < SEQ_TRACKS; t++) seq_track_set(0, t, TF_MUTE, t != TR_EXT);  /* only the external track */
}
static void step_on(uint32_t p, uint32_t t, uint32_t s, int deg)
{
    seq_step_set(p, t, s, SF_ON, 1); seq_step_set(p, t, s, SF_DEG0, deg);
}
static int ons(int note) { int c = 0; for (int i = 0; i < nev; i++) if (evs[i].on && (note < 0 || evs[i].note == note)) c++; return c; }
static long on_frame(int k) { for (int i = 0, c = 0; i < nev; i++) if (evs[i].on && c++ == k) return evs[i].frame; return -1; }
static long off_frame(int k) { for (int i = 0, c = 0; i < nev; i++) if (!evs[i].on && c++ == k) return evs[i].frame; return -1; }

int main(void)
{
    const double STEP = 44100.0 * 60 / 120 / 4;    /* 16ths at 120 BPM: 5512.5 frames */

    /* 1. timing and gate: 16 steps on, 120 BPM */
    fresh();
    for (int s = 0; s < 16; s++) step_on(0, TR_EXT, s, s % 12);
    seq_play(); run(2.1);
    CHECK(ons(-1) == 17, "17 notes in 2.1 s, got %d", ons(-1));
    double worst = 0;
    for (int k = 0; k < 16; k++) { double e = fabs(on_frame(k) - k * STEP); if (e > worst) worst = e; }
    CHECK(worst <= 16, "steps on time, worst %.1f frames", worst);
    CHECK(fabs(off_frame(0) - STEP / 2) <= 17, "gate 8/16 ends mid-step: %ld", off_frame(0));
    printf("timing: steps within %.1f frames of the grid (one block is 16)\n", worst);

    /* 2. ratchet 3, probability, condition, swing, speed */
    fresh(); step_on(0, TR_EXT, 0, 0); seq_step_set(0, TR_EXT, 0, SF_RATCHET, 3);
    seq_play(); run(0.12);
    CHECK(ons(-1) == 3 && fabs(on_frame(1) - STEP / 3) <= 16 && fabs(on_frame(2) - 2 * STEP / 3) <= 16, "ratchet 3: %d hits", ons(-1));

    fresh(); for (int s = 0; s < 16; s++) { step_on(0, TR_EXT, s, 0); seq_step_set(0, TR_EXT, s, SF_PROB, 50); }
    seq_play(); run(40);
    { double f = ons(-1) / (40.0 * 44100 / STEP); CHECK(f > 0.45 && f < 0.55, "probability 50 %%: %.3f", f); }

    fresh(); step_on(0, TR_EXT, 0, 0); seq_step_set(0, TR_EXT, 0, SF_COND, (1 << 4) | 4);   /* pass 1 of every 4 */
    seq_play(); run(16 * STEP * 8 / 44100 - 0.01);
    CHECK(ons(-1) == 2, "condition 1 of 4 over 8 passes: %d", ons(-1));

    fresh(); step_on(0, TR_EXT, 0, 0); step_on(0, TR_EXT, 1, 1); seq_track_set(0, TR_EXT, TF_SWING, 75);
    seq_play(); run(0.3);
    CHECK(fabs(on_frame(1) - 1.5 * STEP) <= 16, "swing 75: step 1 at %.2f steps", on_frame(1) / STEP);

    fresh(); for (int s = 0; s < 16; s++) step_on(0, TR_EXT, s, 0); seq_track_set(0, TR_EXT, TF_SPEED, 0);
    seq_play(); run(1.0);
    CHECK(fabs(on_frame(1) - STEP / 2) <= 16, "speed x2: %.2f", on_frame(1) / STEP);
    fresh(); for (int s = 0; s < 16; s++) step_on(0, TR_EXT, s, 0); seq_track_set(0, TR_EXT, TF_SPEED, 4);
    seq_play(); run(1.0);
    CHECK(fabs(on_frame(3) - 3 * STEP * 3 / 4) <= 16, "triplets: step 3 at %.3f", on_frame(3) / STEP);

    /* 3. directions on a 5-step track */
    {
        const char *name[] = {"back", "ping-pong"}; int dir[] = {1, 2};
        int want[2][10] = {{4, 3, 2, 1, 0, 4, 3, 2, 1, 0}, {0, 1, 2, 3, 4, 3, 2, 1, 0, 1}};
        for (int d = 0; d < 2; d++) {
            fresh(); seq_track_set(0, TR_EXT, TF_LEN, 5); seq_track_set(0, TR_EXT, TF_DIR, dir[d]);
            for (int s = 0; s < 5; s++) step_on(0, TR_EXT, s, s);
            seq_play(); run(10 * STEP / 44100 - 0.01);
            int ok = ons(-1) == 10;
            for (int k = 0, c = 0; k < nev && ok; k++) if (evs[k].on) ok = evs[k].note == 60 + want[d][c++];
            CHECK(ok, "direction %s", name[d]);
        }
    }

    /* 4. notes: transpose, base period, the degree read in the current scale, chords */
    fresh(); step_on(0, TR_EXT, 0, 3); seq_step_set(0, TR_EXT, 0, SF_PER0, 1);
    seq_track_set(0, TR_EXT, TF_TRANSPOSE, 2); seq_track_set(0, TR_EXT, TF_BASE_PER, -1);
    tun_edo(19, 19200); seq_play(); run(0.05);
    CHECK(nev && evs[0].note == 60 + (0 * 19 + 3 + 2), "degree, period, transpose in 19-EDO: %d", nev ? evs[0].note : -1);
    fresh(); step_on(0, TR_EXT, 0, 0); seq_step_set(0, TR_EXT, 0, SF_NOTES, 3);
    seq_step_set(0, TR_EXT, 0, SF_DEG1, 4); seq_step_set(0, TR_EXT, 0, SF_DEG2, 7);
    seq_play(); run(0.05);
    CHECK(ons(-1) == 3 && ons(64) == 1 && ons(67) == 1, "a 3-note chord: %d", ons(-1));

    /* 5. ties: a held note played again continues, a new one ends the old after it starts */
    fresh(); step_on(0, TR_EXT, 0, 5); step_on(0, TR_EXT, 1, 5); step_on(0, TR_EXT, 2, 7);
    seq_step_set(0, TR_EXT, 0, SF_GATE, 17); seq_step_set(0, TR_EXT, 1, SF_GATE, 17);
    seq_play(); run(3.2 * STEP / 44100);
    CHECK(ons(65) == 1, "tied note played once: %d", ons(65));
    {
        long on7 = -1, off5 = -1;
        for (int i = 0; i < nev; i++) { if (evs[i].on && evs[i].note == 67 && on7 < 0) on7 = i; if (!evs[i].on && evs[i].note == 65 && off5 < 0) off5 = i; }
        CHECK(on7 >= 0 && off5 > on7 && evs[off5].frame == evs[on7].frame, "legato: the old note ends just after the new one starts");
    }

    /* 6. patterns: queued switch at the end of the master length, the song */
    fresh(); seq_track_set(1, TR_EXT, TF_MUTE, 0);
    for (int s = 0; s < 16; s++) { step_on(0, TR_EXT, s, 0); step_on(1, TR_EXT, s, 5); }
    seq_pattern_set(1, PF_SCALE, 7);
    seq_play(); run(4 * STEP / 44100); seq_queue_pattern(1); run(14 * STEP / 44100);
    {
        int first5 = -1; for (int i = 0, c = 0; i < nev; i++) if (evs[i].on) { if (evs[i].note == 65) { first5 = c; break; } c++; }
        CHECK(first5 == 16, "the queued pattern starts at step 16: at %d", first5);
        CHECK(seq_scale_request() == 7 && seq_scale_request() == -1, "the new pattern asks for its scale, once");
    }
    fresh(); seq_track_set(1, TR_EXT, TF_MUTE, 0); seq_track_set(2, TR_EXT, TF_MUTE, 0);
    for (int p = 0; p < 3; p++) { seq_pattern_set((uint32_t)p, PF_LEN, 4); seq_track_set((uint32_t)p, TR_EXT, TF_LEN, 4); step_on((uint32_t)p, TR_EXT, 0, p); }
    seq_song_set(0, 0, 2); seq_song_set(1, 2, 1); seq_song_set(2, 1, 1); seq_song_mode(1);
    seq_play(); run(5 * 4 * STEP / 44100 - 0.01);
    {
        int want[] = {60, 60, 62, 61, 60}, ok = ons(-1) == 5;
        for (int i = 0, c = 0; i < nev && ok; i++) if (evs[i].on) ok = evs[i].note == want[c++];
        CHECK(ok, "song: p0 twice, p2, p1, then p0 again (%d notes)", ons(-1));
        if (!ok) for (int i = 0; i < nev; i++) if (evs[i].on) printf("  song note %d at step %.2f\n", evs[i].note, evs[i].frame / STEP);
    }

    /* 7. polymeter: a 3-step track against a 4-step one keeps going across pattern loops */
    fresh(); seq_track_set(0, TR_EXT, TF_LEN, 3); seq_pattern_set(0, PF_LEN, 4); step_on(0, TR_EXT, 0, 0);
    seq_play(); run(12 * STEP / 44100 - 0.01);
    CHECK(ons(-1) == 4, "3 against 4: four hits in 12 steps, %d", ons(-1));

    /* 8. mute, solo, stop */
    fresh(); step_on(0, TR_EXT, 0, 0); seq_track_set(0, TR_EXT, TF_MUTE, 1); seq_play(); run(0.2);
    CHECK(ons(-1) == 0, "muted");
    fresh(); for (int s = 0; s < 16; s++) step_on(0, TR_EXT, s, 0); seq_step_set(0, TR_EXT, 0, SF_GATE, 17);
    seq_play(); run(0.05); seq_stop(); run(0.05);
    { int on = 0; for (int i = 0; i < nev; i++) on += evs[i].on ? 1 : -1; CHECK(on == 0, "stop leaves nothing sounding (%d)", on); }

    /* 9. the synth tracks sound; a lock on the step (cutoff to the floor) silences it */
    eng_init(); tun_edo(12, 19200); seq_init(); frames = 0; energy = 0;
    for (int s = 0; s < 16; s++) step_on(0, TR_SYN1, s, 0);
    seq_play(); run(1.0);
    double open = energy; energy = 0;
    for (int s = 0; s < 16; s++) seq_step_lock(0, TR_SYN1, (uint32_t)s, P_VOLUME, 0);
    run(1.0);
    CHECK(open > 0 && energy < open * 0.02, "volume locked to 0 on every step: %.4f of the level", energy / open);
    for (int s = 0; s < 16; s++) seq_step_unlock(0, TR_SYN1, (uint32_t)s, P_VOLUME);
    energy = 0; run(1.0);
    CHECK(energy > open * 0.5, "locks removed: %.3f", energy / open);

    /* 10. editing */
    fresh();
    seq_op(OP_EUCLID, 0, TR_EXT, 0, 5, 0, 7);
    { int c = 0; for (int s = 0; s < 16; s++) c += seq_step_get(0, TR_EXT, (uint32_t)s, SF_ON); CHECK(c == 5, "euclid 5 of 16: %d", c); }
    step_on(0, TR_EXT, 0, 1);
    seq_op(OP_CLEAR_TRACK, 0, TR_EXT, 0, 0, 0, 0); seq_op(OP_UNDO, 0, 0, 0, 0, 0, 0);
    CHECK(seq_step_get(0, TR_EXT, 0, SF_ON) == 1, "undo brings the track back");
    seq_op(OP_CLEAR_TRACK, 0, TR_EXT, 0, 0, 0, 0); step_on(0, TR_EXT, 0, 9);
    seq_op(OP_SHIFT, 0, TR_EXT, 0, 1, 0, 0);
    CHECK(seq_step_get(0, TR_EXT, 1, SF_ON) && seq_step_get(0, TR_EXT, 1, SF_DEG0) == 9, "shift right");
    seq_op(OP_REVERSE, 0, TR_EXT, 0, 0, 0, 0);
    CHECK(seq_step_get(0, TR_EXT, 14, SF_ON), "reverse");
    seq_op(OP_DOUBLE, 0, TR_EXT, 0, 0, 0, 0);
    CHECK(seq_track_get(0, TR_EXT, TF_LEN) == 32 && seq_step_get(0, TR_EXT, 30, SF_ON), "double");
    seq_op(OP_RANDOM, 0, TR_EXT, 0, 100, 3, 20);
    {
        int ok = 1;
        for (int s = 0; s < 32; s++) { int a = seq_step_get(0, TR_EXT, (uint32_t)s, SF_PER0) * 12 + seq_step_get(0, TR_EXT, (uint32_t)s, SF_DEG0); if (a < 3 || a > 20) ok = 0; }
        CHECK(ok, "random degrees stay between 3 and 20");
    }
    seq_op(OP_COPY_STEP, 0, TR_EXT, 4, 0, 0, 0); seq_op(OP_PASTE_STEP, 3, TR_SYN2, 7, 0, 0, 0);
    CHECK(seq_step_get(3, TR_SYN2, 7, SF_DEG0) == seq_step_get(0, TR_EXT, 4, SF_DEG0), "copy a step to another pattern");
    CHECK(seq_step_lock(0, 0, 0, 1, 5) == 0 && seq_step_lock(0, 0, 0, 2, 5) == 0 && seq_step_lock(0, 0, 0, 3, 5) == 0 &&
          seq_step_lock(0, 0, 0, 4, 5) == 0 && seq_step_lock(0, 0, 0, 5, 5) != 0, "four locks a step, no more");
    seq_step_set(0, 0, 0, SF_COND, (5 << 4) | 3);
    CHECK(seq_step_get(0, 0, 0, SF_COND) == 0, "an impossible condition is refused");

    printf("project: %u bytes in RAM\n", seq_project_size());
    printf("%d checks, %d failed\n", checks, fails);
    return fails != 0;
}
