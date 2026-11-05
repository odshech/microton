/* test_fx.c — the drums and the effects: cc -O2 -Iengine tests/test_fx.c engine/engine.c -lm && ./a.out */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "engine.h"

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static int32_t buf[2 * 256];
/* n frames; returns the peak, and the energy (mean square) in *ms */
static int32_t run(uint32_t frames, double *ms)
{
    int32_t peak = 0; double e = 0; uint32_t f, i;
    for (f = 0; f < frames; f += 256) {
        for (i = 0; i < 512; i++) buf[i] = 0;
        eng_render(buf, 256);
        for (i = 0; i < 512; i++) { int32_t a = abs(buf[i]); if (a > peak) peak = a; e += (double)buf[i] * buf[i]; }
    }
    if (ms) *ms = e / (frames * 2.0);
    return peak;
}
static void dry(void) { eng_init(); eng_fx_set(FX_REV_LEVEL, 0); eng_fx_set(FX_DLY_LEVEL, 0); }

int main(void)
{
    static const char *NAME[8] = {"kick", "snare", "closed hat", "open hat", "clap", "low tom", "high tom", "rim"};
    uint32_t d, k;
    double ms, ms2;
    int32_t pk;

    /* every drum sounds, stays in range, and ends */
    for (d = 0; d < ENG_DRUMS; d++) {
        dry();
        eng_drum_hit(d, 127);
        pk = run(4410, &ms);
        CHECK(ms > 1e4, "%s: silent (%.0f)", NAME[d], ms);
        CHECK(pk < 32768 * 2, "%s: peak %d", NAME[d], pk);
        run(44100 * 4, NULL);
        pk = run(4410, NULL);
        CHECK(pk == 0, "%s: still sounding after 4 s (%d)", NAME[d], pk);
        printf("  %-10s rms %5.0f\n", NAME[d], sqrt(ms));
    }
    /* velocity: softer is quieter */
    dry(); eng_drum_hit(0, 127); run(2205, &ms);
    dry(); eng_drum_hit(0, 30); run(2205, &ms2);
    CHECK(ms2 < ms * 0.6, "velocity: %.0f vs %.0f", ms2, ms);
    /* choke: the closed hat stops the open one */
    dry(); eng_drum_hit(3, 127); run(2048, NULL); run(4096, &ms);
    dry(); eng_drum_hit(3, 127); run(2048, NULL); eng_drum_hit(2, 127); run(2048, NULL); run(4096, &ms2);
    CHECK(ms2 < ms * 0.2, "choke: open hat %.0f, choked %.0f", ms, ms2);
    /* parameters reach the sound: a higher tune is a different sound, and the kit default is restorable */
    dry(); eng_drum_set(0, DP_TUNE, 12000); CHECK(eng_drum_get(0, DP_TUNE) == 12000, "drum param set");
    CHECK(eng_drum_param_def(0, DP_TUNE) == 3000, "kit default");
    eng_drum_set(0, DP_RATIO, 99); CHECK(eng_drum_get(0, DP_RATIO) == 15, "clamped to max");

    /* the delay: a click comes back at the set time (free, 250 ms), then fades */
    eng_init();
    eng_fx_set(FX_REV_LEVEL, 0); eng_fx_set(FX_DLY_SYNC, 0); eng_fx_set(FX_DLY_WOW, 0);
    eng_fx_set(FX_DLY_TO_REV, 0); eng_fx_set(FX_DLY_FB, 8000); eng_fx_set(FX_DLY_LEVEL, 16383);
    /* 10 ms x 120^(r/16383) = 250 ms → r = 16383 log(25)/log(120) */
    eng_fx_set(FX_DLY_TIME, (uint32_t)(16383.0 * log(25.0) / log(120.0) + 0.5));
    eng_drum_set(7, DP_DLY, 16383); eng_drum_set(7, DP_LEVEL, 16383);
    run(44100, NULL);                                        /* the delay's time glides in */
    eng_drum_hit(7, 127);
    {
        int32_t best = 0; uint32_t at = 0, f, i;
        for (f = 0; f < 44100 / 2; f += 256) {
            for (i = 0; i < 512; i++) buf[i] = 0;
            eng_render(buf, 256);
            if (f > 4410) for (i = 0; i < 512; i++) if (abs(buf[i]) > best) { best = abs(buf[i]); at = f + i / 2; }
        }
        CHECK(best > 500 && fabs(at / 44.1 - 250) < 15, "delay echo at %.1f ms (level %d), expected 250", at / 44.1, best);
    }
    /* synced: a quarter at 120 BPM = 500 ms */
    eng_fx_set(FX_DLY_SYNC, 9); eng_fx_tempo(12000); run(44100 * 2, NULL);
    eng_drum_hit(7, 127);
    {
        int32_t best = 0; uint32_t at = 0, f, i;
        for (f = 0; f < 44100 * 3 / 4; f += 256) {
            for (i = 0; i < 512; i++) buf[i] = 0;
            eng_render(buf, 256);
            if (f > 4410) for (i = 0; i < 512; i++) if (abs(buf[i]) > best) { best = abs(buf[i]); at = f + i / 2; }
        }
        CHECK(fabs(at / 44.1 - 500) < 15, "synced echo at %.1f ms, expected 500", at / 44.1);
    }
    /* full feedback and drive: it builds, but stays bounded */
    eng_fx_set(FX_DLY_FB, 16383); eng_fx_set(FX_DLY_DRIVE, 16383);
    for (k = 0; k < 8; k++) eng_drum_hit(1, 127);
    pk = run(44100 * 10, NULL);
    CHECK(pk < 32768 * 3, "runaway delay: peak %d", pk);

    /* the reverb: an impulse gives a tail that lasts, then dies; the longest setting stays stable */
    eng_init();
    eng_fx_set(FX_DLY_LEVEL, 0); eng_fx_set(FX_REV_LEVEL, 16383); eng_fx_set(FX_REV_SIZE, 11000);
    eng_drum_set(7, DP_REV, 16383);
    eng_drum_hit(7, 127); run(4410, NULL);
    run(22050, &ms);                                          /* 0.6 .. 1.1 s: the tail */
    CHECK(ms > 100, "reverb tail too quiet: %.1f", ms);
    run(44100 * 12, NULL); run(4410, &ms2);
    CHECK(ms2 < 1.0, "reverb never ends: %.2f", ms2);
    eng_fx_set(FX_REV_SIZE, 16383); eng_fx_set(FX_REV_DAMP, 0); eng_fx_set(FX_REV_MOD, 16383);
    for (k = 0; k < 4; k++) { eng_drum_hit(0, 127); eng_drum_hit(1, 127); run(11025, NULL); }
    pk = run(44100 * 20, &ms);
    CHECK(pk < 32768 * 2 && ms < 1e8, "reverb blows up at the longest setting: peak %d", pk);
    run(44100 * 40, NULL); run(4410, &ms);
    CHECK(ms < 1e4, "the longest reverb should still fade: %.0f", ms);

    /* a synth part's send reaches the reverb */
    eng_init(); eng_fx_set(FX_DLY_LEVEL, 0); eng_set_param(0, P_REV_SEND, 16383); eng_set_param(0, P_E1_R, 0);
    eng_note_on(0, 0, 100); run(4410, NULL); eng_note_off(0, 0); run(2205, NULL);
    run(4410, &ms);
    eng_init(); eng_fx_set(FX_DLY_LEVEL, 0); eng_set_param(0, P_REV_SEND, 0); eng_set_param(0, P_E1_R, 0);
    eng_note_on(0, 0, 100); run(4410, NULL); eng_note_off(0, 0); run(2205, NULL);
    run(4410, &ms2);
    CHECK(ms > ms2 * 10 + 10, "part send: with %.1f, without %.1f", ms, ms2);

    printf("%d checks, %d failed\n", checks, fails);
    return fails != 0;
}
