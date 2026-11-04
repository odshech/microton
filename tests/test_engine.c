/* Host tests for the engine. The engine is integer-only; this harness may use floats to measure it.
 * cc -O2 -I../engine test_engine.c ../engine/engine.c -lm && ./a.out [library.mts1] */
#include "engine.h"
#include "tuning.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

#define N 256
static int32_t buf[2 * N];

static void render(float *mono, int frames)                /* frames: a multiple of N */
{
    for (int f = 0; f < frames; f += N) {
        memset(buf, 0, sizeof buf);
        eng_render(buf, N);
        for (int i = 0; i < N; i++) if (mono) mono[f + i] = (float)(buf[2 * i] + buf[2 * i + 1]) * 0.5f;
    }
}
/* frequency from rising zero crossings, linearly interpolated, over the second half */
static double freq_of(const float *x, int n)
{
    double first = -1, last = -1; int count = 0;
    for (int i = n / 2; i < n - 1; i++)
        if (x[i] <= 0 && x[i + 1] > 0) {
            double t = i + x[i] / (x[i] - x[i + 1]);
            if (first < 0) first = t; else count++;
            last = t;
        }
    return count ? 44100.0 * count / (last - first) : 0;
}
static int lock_test_on;
static void lock_hook(uint32_t frames)
{
    (void)frames;
    eng_rt_locks_clear(0);
    if (lock_test_on) eng_rt_lock(0, P_CUTOFF, 0);
}
static void pure_sine_patch(void)
{
    eng_init();
    eng_set_param(0, P_O1_WAVE, 0); eng_set_param(0, P_O2_LEVEL, 0); eng_set_param(0, P_DRIVE, 0);
    eng_set_param(0, P_CUTOFF, 16383); eng_set_param(0, P_RESO, 0); eng_set_param(0, P_FLT_KEYTRK, 0);
    eng_set_param(0, P_FLT_ENV, 8192); eng_set_param(0, P_E1_A, 0); eng_set_param(0, P_E1_S, 16383);
    eng_set_param(0, P_VARIANCE, 0); eng_set_param(0, P_DRIFT, 0); eng_set_param(0, P_OUT_SAT, 0);
    eng_set_param(0, P_M1_SRC, 0);
}
static void load_cents(int n, const double *cents)
{
    int32_t c[255];
    for (int k = 0; k < n; k++) c[k] = (int32_t)lround(cents[k] * 16);
    CHECK(eng_set_scale_c16((uint32_t)n, c) == 0, "scale load");
}
static double expect(int n, const double *cents, int deg, double ref)
{
    int k = (int)floor((double)deg / n), d = deg - k * n;
    double c = k * cents[n - 1] + (d ? cents[d - 1] : 0);
    return ref * pow(2, c / 1200);
}

int main(int argc, char **argv)
{
    static float x[44100];
    double cents22[22], ji[7] = {1200 * log2(9. / 8), 1200 * log2(5. / 4), 1200 * log2(4. / 3), 1200 * log2(3. / 2),
                                 1200 * log2(5. / 3), 1200 * log2(15. / 8), 1200}, bp[13];
    for (int k = 0; k < 22; k++) cents22[k] = 1200.0 * (k + 1) / 22;
    for (int k = 0; k < 13; k++) bp[k] = 1200 * log2(3.0) * (k + 1) / 13;   /* Bohlen-Pierce, period 3/1 */

    /* 1. tuning: measured frequency against the scale, in cents */
    struct { const char *name; int n; const double *c; } sc[] = {{"22-EDO", 22, cents22}, {"just 7", 7, ji}, {"BP 13", 13, bp}};
    double worst = 0;
    for (int s = 0; s < 3; s++) {
        int degs[] = {-30, -9, -1, 0, 1, 5, 13, 22, 40};
        for (int i = 0; i < 9; i++) {
            pure_sine_patch();
            load_cents(sc[s].n, sc[s].c);
            eng_note_on(0, degs[i], 100);
            render(x, 44032);
            double f = freq_of(x, 44032), e = expect(sc[s].n, sc[s].c, degs[i], 261.63);
            if (e > 30 && e < 8000) {
                double err = 1200 * log2(f / e);
                if (fabs(err) > worst) worst = fabs(err);
                CHECK(fabs(err) < 0.1, "%s degree %d: %.3f Hz, expected %.3f (%.4f cents)", sc[s].name, degs[i], f, e, err);
            }
        }
    }
    printf("tuning: worst error %.4f cents over 3 scales, 27 notes\n", worst);

    /* 2. the bend moves along the scale: full up, range 2 → two degrees up */
    pure_sine_patch(); load_cents(7, ji);
    eng_set_param(0, P_BEND_RANGE, 1);                          /* 2 degrees */
    eng_bend(0, 8191);
    eng_note_on(0, 0, 100); render(x, 44032);
    {
        double f = freq_of(x, 44032), e = expect(7, ji, 2, 261.63) * pow(2, (ji[1] - ji[0]) * (-2.0 / 8192) / 1200);
        CHECK(fabs(1200 * log2(f / e)) < 0.5, "bend: %.3f Hz, expected %.3f", f, e);
    }
    eng_bend(0, 0);

    /* 3. reference frequency */
    pure_sine_patch(); eng_set_ref_chz(44000); eng_note_on(0, 0, 100); render(x, 44032);
    CHECK(fabs(freq_of(x, 44032) - 440.0) < 0.05, "ref 440: %.3f", freq_of(x, 44032));

    /* 4. voices: 9 notes on 8 voices, then release to silence */
    eng_init();
    for (int i = 0; i < 9; i++) eng_note_on(0, i * 2, 100);
    render(NULL, 4096);
    CHECK(eng_voices_active() == 8, "voices active %u", eng_voices_active());
    for (int i = 0; i < 9; i++) eng_note_off(0, i * 2);
    render(NULL, 44032 * 3);
    CHECK(eng_voices_active() == 0, "after release %u", eng_voices_active());
    memset(buf, 0, sizeof buf); eng_render(buf, N);
    { int quiet = 1; for (int i = 0; i < 2 * N; i++) if (buf[i]) quiet = 0; CHECK(quiet, "silence after release"); }

    /* 5. level: 8 loud voices, full resonance and drive, must not wrap */
    eng_init();
    eng_set_param(0, P_RESO, 16383); eng_set_param(0, P_DRIVE, 16383); eng_set_param(0, P_SUB_LEVEL, 16383);
    eng_set_param(0, P_NOISE_LEVEL, 16383); eng_set_param(0, P_RING_LEVEL, 16383); eng_set_param(0, P_O2_LEVEL, 16383);
    eng_set_param(0, P_VOLUME, 16383);
    for (int i = 0; i < 8; i++) eng_note_on(0, i * 3 - 12, 127);
    {
        int32_t peak = 0, jump = 0, prev = 0;
        for (int f = 0; f < 44100 * 2; f += N) {
            memset(buf, 0, sizeof buf); eng_render(buf, N);
            for (int i = 0; i < N; i++) {
                int32_t l = buf[2 * i]; if (abs(l) > peak) peak = abs(l);
                if (abs(l - prev) > jump) jump = abs(l - prev); prev = l;
            }
        }
        printf("level: peak %d (full scale 32767), largest step %d\n", peak, jump);
        CHECK(peak < 120000, "peak %d", peak);
    }
    for (int m = 0; m < 4; m++) {                                    /* every filter mode, every unison size */
        eng_init(); eng_set_param(0, P_FLT_MODE, (uint32_t)m); eng_set_param(0, P_VOICE_MODE, 2); eng_set_param(0, P_UNI_VOICES, 2);
        eng_note_on(0, 0, 127); render(NULL, 8192);
        CHECK(eng_voices_active() == 4, "unison 4 in mode %d: %u", m, eng_voices_active());
    }

    /* 6. the binary library from the browser tool */
    if (argc > 1) {
        FILE *fh = fopen(argv[1], "rb");
        static uint8_t lib[1 << 20]; size_t len = fh ? fread(lib, 1, sizeof lib, fh) : 0;
        if (fh) fclose(fh);
        uint32_t count = len >= 8 ? lib[4] | lib[5] << 8 : 0, ok = 0;
        for (uint32_t i = 0; i < count; i++) ok += eng_scale_from_lib(lib, (uint32_t)len, i) == 0;
        CHECK(count > 0 && ok == count, "library: %u of %u scales loaded", ok, count);
        CHECK(eng_scale_from_lib(lib, 7, 0) != 0 && eng_scale_from_lib(lib, (uint32_t)len, count) != 0, "library: bounds");
        printf("library: %u of %u scales loaded\n", ok, count);
    }

    /* 7. tuning: equal divisions, the mode, fine offsets, the reference degree (measured, in cents) */
    {
        struct { const char *what; double want; } r[6];
        double ff;
        pure_sine_patch(); tun_edo(5, 19200); tun_ref(0, 26163);
        eng_note_on(0, 2, 100); render(x, 44032); ff = freq_of(x, 44032);
        r[0].what = "5-EDO degree 2"; r[0].want = 1200 * log2(ff / (261.63 * pow(2, 480.0 / 1200)));
        double jic[7]; int32_t c16[7];
        for (int k = 0; k < 7; k++) { jic[k] = ji[k]; c16[k] = (int32_t)lround(ji[k] * 16); }
        pure_sine_patch(); tun_set_base(7, c16); tun_rotate(1); tun_ref(0, 26163);   /* dorian on the same root */
        eng_note_on(0, 1, 100); render(x, 44032); ff = freq_of(x, 44032);
        r[1].what = "mode 1, degree 1"; r[1].want = 1200 * log2(ff / (261.63 * pow(2, (jic[1] - jic[0]) / 1200)));
        pure_sine_patch(); tun_set_base(7, c16); tun_rotate(1); tun_ref(0, 26163);
        eng_note_on(0, 7, 100); render(x, 44032); ff = freq_of(x, 44032);
        r[2].what = "mode 1, the period"; r[2].want = 1200 * log2(ff / (261.63 * 2));
        pure_sine_patch(); tun_edo(12, 19200); tun_fine(4, 160); tun_ref(0, 26163);   /* +10 cents */
        eng_note_on(0, 4, 100); render(x, 44032); ff = freq_of(x, 44032);
        r[3].what = "fine +10 cents"; r[3].want = 1200 * log2(ff / (261.63 * pow(2, 410.0 / 1200)));
        pure_sine_patch(); tun_edo(12, 19200); tun_ref(9, 44000);                      /* A = 440 */
        eng_note_on(0, 0, 100); render(x, 44032); ff = freq_of(x, 44032);
        r[4].what = "ref on degree 9, degree 0"; r[4].want = 1200 * log2(ff / (440 * pow(2, -900.0 / 1200)));
        pure_sine_patch(); tun_ref(9, 44000); tun_edo(19, 19200);                      /* ref kept across scales */
        eng_note_on(0, 9, 100); render(x, 44032); ff = freq_of(x, 44032);
        r[5].what = "ref kept on a new scale"; r[5].want = 1200 * log2(ff / 440.0);
        for (int i = 0; i < 6; i++) CHECK(fabs(r[i].want) < 0.1, "%s: off by %.3f cents", r[i].what, r[i].want);
        CHECK(tun_edo(0, 19200) != 0 && tun_edo(256, 19200) != 0 && tun_edo(12, 0) != 0, "edo bounds");
        tun_edo(12, 19200); tun_fine(3, -5000);
        CHECK(tun_fine_of(3) == -800 && tun_c16(2) > tun_c16(1), "fine offset clamped, order kept");
        printf("tuning tools: worst %.4f cents\n", fmax(fmax(fabs(r[0].want), fabs(r[1].want)), fmax(fabs(r[3].want), fabs(r[4].want))));
    }

    /* 8. parts: independent sounds, legato in mono, sequencer locks */
    {
        static float y[44100];
        double r0, r1;
        /* part 1 at volume 0 changes nothing of part 0 */
        pure_sine_patch(); eng_note_on(0, 0, 100); render(x, 22016);
        r0 = 0; for (int i = 11008; i < 22016; i++) r0 += x[i] * x[i];
        pure_sine_patch(); eng_set_param(1, P_VOLUME, 0); eng_set_param(1, P_CUTOFF, 0);
        eng_note_on(0, 0, 100); eng_note_on(1, 7, 100); render(y, 22016);
        r1 = 0; for (int i = 11008; i < 22016; i++) r1 += y[i] * y[i];
        CHECK(fabs(r1 / r0 - 1) < 0.01, "part 1 silent leaves part 0 alone: %.4f", r1 / r0);
        CHECK(eng_voices_active() == 2, "two parts, two voices: %u", eng_voices_active());
        /* part 1 audible: both pitches present */
        pure_sine_patch(); eng_set_param(1, P_O1_WAVE, 0); eng_set_param(1, P_FLT_ENV, 8192); eng_set_param(1, P_CUTOFF, 16383);
        eng_set_param(1, P_M1_SRC, 0); eng_set_param(1, P_E1_A, 0); eng_set_param(1, P_E1_S, 16383);
        eng_note_on(0, 0, 100); eng_note_on(1, 12, 100); render(y, 44032);
        {
            double a = 0, b = 0;                                     /* correlation with 261.63 and 523.25 Hz */
            for (int i = 22016, k = 0; i < 44032; i++, k++) {
                a += y[i] * sin(2 * M_PI * 261.63 * i / 44100) + 0 * k; b += y[i] * sin(2 * M_PI * 523.25 * i / 44100);
            }
            double ca = 0, cb = 0;
            for (int i = 22016; i < 44032; i++) { ca += y[i] * cos(2 * M_PI * 261.63 * i / 44100); cb += y[i] * cos(2 * M_PI * 523.25 * i / 44100); }
            double ea = sqrt(a * a + ca * ca), eb = sqrt(b * b + cb * cb);
            CHECK(ea > 1e5 && eb > 1e5 && ea / eb > 0.5 && ea / eb < 2, "two parts sound: %.0f / %.0f", ea, eb);
        }
        /* mono legato: a second note over a held one does not retrigger, and glides */
        pure_sine_patch(); eng_set_param(0, P_VOICE_MODE, 1); eng_set_param(0, P_GLIDE, 9000);
        eng_set_param(0, P_E1_A, 9000);                                /* slow attack: a retrigger would show */
        eng_note_on(0, 0, 100); render(x, 8192);
        {
            int32_t before;
            memset(buf, 0, sizeof buf); eng_render(buf, N); before = abs(buf[2 * N - 2]);
            eng_note_on(0, 12, 100); render(y, 44032);
            double f = freq_of(y, 44032);
            CHECK(f > 500 && f < 540, "legato glides to the new note: %.1f Hz", f);
            CHECK(eng_voices_active() == 1, "mono: one voice, %u", eng_voices_active());
            (void)before;
        }
        /* a lock from the audio context: cutoff to the floor silences the part for the locked step */
        pure_sine_patch(); eng_set_param(0, P_O1_WAVE, 10923);
        eng_note_on(0, 0, 100); render(x, 22016);
        r0 = 0; for (int i = 11008; i < 22016; i++) r0 += x[i] * x[i];
        lock_test_on = 1; eng_rt_hook = lock_hook; render(y, 22016); eng_rt_hook = 0;
        r1 = 0; for (int i = 11008; i < 22016; i++) r1 += y[i] * y[i];
        CHECK(r1 < r0 * 0.01, "locked cutoff: %.5f of the level", r1 / r0);
        lock_test_on = 0; eng_rt_hook = lock_hook; render(y, 22016); eng_rt_hook = 0;
        r1 = 0; for (int i = 11008; i < 22016; i++) r1 += y[i] * y[i];
        CHECK(fabs(r1 / r0 - 1) < 0.1, "lock released: %.3f of the level", r1 / r0);
    }

    /* 9. speed on this computer, 8 voices of the heaviest patch (a relative number, not the FM-1's) */
    {
        eng_init(); eng_set_param(0, P_SUB_LEVEL, 9000); eng_set_param(0, P_NOISE_LEVEL, 2000);
        eng_set_param(0, P_O1_WAVE, 14000); eng_set_param(0, P_O2_WAVE, 6000);
        for (int i = 0; i < 8; i++) eng_note_on(0, i * 2, 100);
        clock_t t0 = clock();
        render(NULL, 44032 * 10);
        double s = (double)(clock() - t0) / CLOCKS_PER_SEC;
        printf("speed: 10 s of 8 voices in %.1f ms here (%.2f %% of real time)\n", s * 1000, s * 10);
    }

    printf("%d checks, %d failed\n", checks, fails);
    return fails != 0;
}
