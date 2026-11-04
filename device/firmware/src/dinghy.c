/* SPDX-License-Identifier: GPL-3.0-only */
/* MICROTON 0.4 for the M-VAVE FM-1: the microtonal engine (engine/, the same C as the browser lab) on Dinghy's platform.
 * A test firmware: it plays, and it measures. The screen shows the battery and the audio engine's CPU load.
 *
 *   white keys     the 16 degrees of the window; 3 sounds share 8 voices (part 1 on the keys)
 *   black keys     1 window -  2 window +  3 period -  4 period +  5 hold  7 mode -  8 mode +
 *                  9 scale -  10 scale +  11 all notes off          (left to right; 6 is free)
 *   OCT- OCT+      period - / +
 *   PLAY           the sequencer: the demo pattern, three parts and the drums
 *   REC            the load test: 8 voices of the heaviest sound, held
 *   HOME           clears the peak load and the count of voices dropped for load (CUT n)
 *   KNOB 1..4      cutoff, resonance, envelope 1 release, drive (part 1)
 *   FX             the knobs switch to: reverb level, reverb size, delay level, delay feedback (FX lit), PRESETS
 *                  to the delay's time (free, 1/32 .. 1/2 of the tempo); the screen shows the values
 *   PRESETS        the scale          SELECT  the tempo
 *   MIDI in        notes on the white-key map (C4 = the window's first degree), bend, mod wheel, NRPN = parameters
 *
 * Integers only: the chip has no FPU and no libgcc. tests/host.c runs this file on a computer. */
#define APP_NAME "MICROTON"
static void app_idle(void);
#define LCD_IDLE app_idle
#include "platform.c"

#ifdef DINGHY_HOST                              /* the host emulator has no timer counter and no battery */
#include <time.h>
#define FM1_ADC_BATT 3
static uint32_t fm1_ticks(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint32_t)((uint64_t)t.tv_sec * 24000000u + (uint64_t)t.tv_nsec * 24u / 1000u); }
static int32_t fm1_adc_read(int ch) { return ch == FM1_ADC_BATT ? 572 : -1; }
#endif
#include "micro/config.h"
#ifdef DINGHY_HOST
#include "micro_unit.c"                         /* one file on a computer */
#else
#include "micro/engine.h"                       /* on the FM-1: its own unit at -O2 (micro_unit.c, tools/build.py) */
#include "micro/tuning.h"
#include "micro/seq.h"
#endif
#include "micro/scales_lib.h"
#include "micro/atan_table.h"

/* ---- the audio interrupt: the engine (the sequencer runs inside it), and how long it took */
#define BUDGET_PER_FRAME 544u                   /* 24 MHz ticks per frame at 44.1 kHz (24 000 000 / 44 100) */
static volatile uint32_t load_avg, load_peak, load_last;   /* per mille of the time one buffer lasts */
static volatile uint32_t shed_count;            /* voices dropped for load since HOME */
#define SHED_AT 850u                            /* over 85 %: one voice less, so the main loop keeps running */

static void app_audio(int32_t *out, uint32_t n)
{
    uint32_t t0 = fm1_ticks(), dt, pm;
    eng_render(out, n);
    dt = fm1_ticks() - t0;
    pm = (dt * 1000u) / (n * BUDGET_PER_FRAME);
    load_last = pm;
    load_avg += (int32_t)(pm - load_avg) / 16;  /* a smooth average, ~0.1 s */
    if (pm > load_peak) load_peak = pm;
    if (pm > SHED_AT && eng_rt_shed()) shed_count++;
}

/* ---- state (main loop) */
static uint32_t scale_i = 3;                    /* in LIB_ORDER: 22-EDO */
static int32_t win = 0;                         /* the first degree on the white keys */
static int hold, stress, playing_seq, fx_page;
static int32_t key_deg[27];                     /* the degree each key started */
static uint8_t held_deg[256];                   /* degrees sounding from the keys (mod n), for the wheel */
static const uint8_t WHITE_KEY[16] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};
static const uint8_t BLACK_KEY[11] = {1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25};

static int32_t key_white(uint32_t k) { uint32_t i; for (i = 0; i < 16u; i++) if (WHITE_KEY[i] == k) return (int32_t)i; return -1; }
static int32_t key_black(uint32_t k) { uint32_t i; for (i = 0; i < 11u; i++) if (BLACK_KEY[i] == k) return (int32_t)i; return -1; }
static int32_t imod(int32_t a, int32_t n) { int32_t r = a % n; return r < 0 ? r + n : r; }

static void scale_load(void)
{
    tun_from_lib(LIB, sizeof LIB, LIB_ORDER[scale_i]);
    tun_ref(0, 26163u);
}
static const char *scale_name(uint32_t idx, char *buf)  /* the short name stored in the library */
{
    uint32_t ix = 8u + 8u * idx, off = LIB[ix] | (uint32_t)LIB[ix + 1] << 8 | (uint32_t)LIB[ix + 2] << 16, nl = LIB[off], i;
    for (i = 0; i < nl && i < 16u; i++) buf[i] = (char)LIB[off + 1u + i];
    buf[i] = 0;
    return buf;
}

/* ---- the demo pattern: an arpeggio (part 1), a bass that slides (part 2, mono), chords (part 3) */
static void demo(void)
{
    uint32_t s, n = tun_degrees();
    static const uint8_t ARP[16] = {0, 4, 7, 11, 13, 11, 7, 4, 0, 4, 7, 13, 16, 13, 11, 7};
    for (s = 0; s < 16u; s++) {
        int32_t a = (int32_t)(ARP[s] * n / 22u);
        seq_step_set(0, TR_SYN1, s, SF_ON, 1);
        seq_step_set(0, TR_SYN1, s, SF_DEG0, a % (int32_t)n);
        seq_step_set(0, TR_SYN1, s, SF_PER0, a / (int32_t)n);
        seq_step_set(0, TR_SYN1, s, SF_GATE, 6);
    }
    for (s = 0; s < 16u; s += 2u) {
        seq_step_set(0, TR_SYN2, s, SF_ON, 1);
        seq_step_set(0, TR_SYN2, s, SF_DEG0, (int32_t)((s % 8u == 6u ? 9u : 0u) * n / 22u));
        seq_step_set(0, TR_SYN2, s, SF_PER0, -1);
        seq_step_set(0, TR_SYN2, s, SF_SLIDE, s % 8u == 4u);
    }
    for (s = 0; s < 16u; s += 4u) {
        seq_step_set(0, TR_SYN3, s, SF_ON, 1);
        seq_step_set(0, TR_SYN3, s, SF_NOTES, 3);
        seq_step_set(0, TR_SYN3, s, SF_DEG0, 0);
        seq_step_set(0, TR_SYN3, s, SF_DEG1, (int32_t)(7u * n / 22u));
        seq_step_set(0, TR_SYN3, s, SF_DEG2, (int32_t)(13u * n / 22u));
        seq_step_set(0, TR_SYN3, s, SF_GATE, 16);
        seq_step_set(0, TR_SYN3, s, SF_VEL, 70);
    }
    /* drums: kick, snare, closed and open hat on track A (a mask of 4), clap and rim on track B */
    for (s = 0; s < 16u; s++) {
        uint32_t a = (s % 4u == 0u ? 1u : 0u) | (s % 8u == 4u ? 2u : 0u) | (s % 2u == 0u && s != 14u ? 4u : 0u) | (s == 14u ? 8u : 0u);
        uint32_t bm = (s == 12u ? 1u : 0u) | (s == 3u || s == 7u || s == 11u ? 8u : 0u);
        if (s == 10u) a |= 1u;
        if (a) { seq_step_set(0, TR_DRUM_A, s, SF_ON, 1); seq_step_set(0, TR_DRUM_A, s, SF_NOTES, (int32_t)a); seq_step_set(0, TR_DRUM_A, s, SF_DEG0, (int32_t)a);
                 seq_step_set(0, TR_DRUM_A, s, SF_VEL, s % 4u == 2u ? 80 : 110); }
        if (bm) { seq_step_set(0, TR_DRUM_B, s, SF_ON, 1); seq_step_set(0, TR_DRUM_B, s, SF_NOTES, (int32_t)bm); seq_step_set(0, TR_DRUM_B, s, SF_DEG0, (int32_t)bm);
                  seq_step_set(0, TR_DRUM_B, s, SF_VEL, 90); }
    }
    eng_set_param(1, P_VOICE_MODE, 1);          /* the bass: mono, so its slides glide */
    eng_set_param(1, P_CUTOFF, 6500);
    eng_set_param(1, P_O1_WAVE, 16383);
    eng_set_param(2, P_E1_A, 6000);             /* the chords: slower */
    eng_set_param(2, P_E1_R, 9000);
    eng_set_param(2, P_VOLUME, 9000);
}
/* the load test: the most expensive voice there is, eight times */
static void stress_set(int on)
{
    static const int32_t DEG[8] = {0, 3, 7, 10, 13, 17, 20, 24};
    uint32_t i;
    if (on) {
        eng_set_param(0, P_O1_WAVE, 10240);         /* saw → pulse, both computed */
        eng_set_param(0, P_O2_WAVE, 6144);          /* triangle → saw */
        eng_set_param(0, P_SUB_LEVEL, 9000);
        eng_set_param(0, P_NOISE_LEVEL, 3000);
        eng_set_param(0, P_RING_LEVEL, 3000);
        eng_set_param(0, P_HPF, 2000);
        eng_set_param(0, P_DRIVE, 6000);
        eng_set_param(0, P_DRIFT, 8000);
        for (i = 0; i < 8u; i++) eng_note_on(0, DEG[i], 100);
    } else {
        for (i = 0; i < 8u; i++) eng_note_off(0, DEG[i]);
    }
    load_peak = 0; shed_count = 0;
}

/* ---- the battery: a slow average of the raw voltage; four levels as Felucca reads them */
static int32_t batt_raw;
static int batt_level(void) { return batt_raw >= 591 ? 3 : batt_raw >= 561 ? 2 : batt_raw >= 531 ? 1 : 0; }
static int on_usb(void)
{
#ifdef DINGHY_HOST
    return 0;
#else
    return usb.config == 1u;
#endif
}

/* ---- the screen */
#define M_RED RGB(224, 40, 40)
#define M_DIM RGB(120, 120, 120)
#define M_TXT RGB(220, 220, 220)
#define M_AMB RGB(226, 180, 92)
#define M_OK RGB(92, 192, 168)

static uint32_t text_num(char *p, uint32_t v)   /* decimal, no leading zeros; returns the length */
{
    char t[10];
    uint32_t n = 0, i;
    do { t[n++] = (char)('0' + v % 10u); v /= 10u; } while (v && n < 10u);
    for (i = 0; i < n; i++) p[i] = t[n - 1u - i];
    return n;
}
static char *put(char *p, const char *s) { while (*s) *p++ = *s++; return p; }
static char *putn(char *p, uint32_t v) { return p + text_num(p, v); }

static void draw_header(void)
{
    int lv = batt_level(), usbp = on_usb(), i;
    cv_begin(240, 24, C_BLACK);
    cv_text(6, 4, &FONT_S, "MICROTON", M_RED);
    /* the battery: a body with four cells, and USB when powered from it */
    cv_rect(196, 6, 30, 13, M_DIM);
    cv_rect(197, 7, 28, 11, C_BLACK);
    cv_rect(226, 10, 3, 5, M_DIM);
    for (i = 0; i < 4; i++) if (usbp || i <= lv) cv_rect(199 + i * 7, 9, 5, 7, usbp ? M_OK : lv == 0 ? M_RED : M_TXT);
    if (usbp) cv_text(160, 4, &FONT_S, "USB", M_OK);
    cv_blit(0, 0);
}

/* the wheel: each degree's slice, red from light (degree 0) to dark; the degrees the keys hold in amber */
static uint32_t angle_of(int32_t dx, int32_t dy)   /* 0 = up, clockwise, 65536 = a turn */
{
    int32_t ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
    uint32_t a;
    if (!ax && !ay) return 0;
    if (ax <= ay) a = ATAN_T[(uint32_t)(ax * 256) / (uint32_t)ay];        /* within 45° of the vertical */
    else a = 16384u - ATAN_T[(uint32_t)(ay * 256) / (uint32_t)ax];
    if (dx >= 0 && dy >= 0) return a;                                     /* up right */
    if (dx >= 0) return 32768u - a;                                       /* down right */
    if (dy < 0) return 32768u + a;                                        /* down left */
    return (65536u - a) & 65535u;                                         /* up left */
}
static void draw_wheel(void)
{
    static uint16_t col[256];
    static uint32_t bnd[256];                    /* where each degree's slice ends, in 1/65536 turns */
    static uint8_t isq[58 * 58 + 1];             /* integer square roots, for the radius */
    uint32_t n = tun_degrees(), d, x, y, p = (uint32_t)tun_c16(n - 1u);
    const int32_t R2 = 58 * 58, H2 = 13 * 13;
    for (d = 0; d < n; d++) {
        uint32_t a = d ? (uint32_t)tun_c16(d - 1u) : 0u, b = d + 1u < n ? (uint32_t)tun_c16(d) : p;
        uint32_t f = n > 1u ? (d * 256u) / (n - 1u) : 128u;
        {
            uint32_t sh = 0;                     /* scale both down until the product fits 32 bits: no 64-bit division */
            while ((p >> sh) >= 65536u) sh++;
            bnd[d] = (((a + b) >> sh) << 15) / ((p >> sh) ? (p >> sh) : 1u);
        }
        col[d] = held_deg[d] ? M_AMB : RGB(250 - (180 * f >> 8), 140 - (134 * f >> 8), 140 - (130 * f >> 8));
    }
    cv_begin(120, 120, C_BLACK);
    if (!isq[R2]) { uint32_t i, q = 0; for (i = 0; i <= (uint32_t)R2; i++) { while ((q + 1u) * (q + 1u) <= i) q++; isq[i] = (uint8_t)q; } }
    for (y = 0; y < 120u; y++)
        for (x = 0; x < 120u; x++) {
            int32_t dx = (int32_t)x - 60, dy = 60 - (int32_t)y, r2 = dx * dx + dy * dy;
            uint32_t a, lo = 0, hi, r = isq[r2];
            if (r2 > R2 || r2 < H2) continue;
            a = angle_of(dx, dy);
            /* the slice of the first boundary past the angle; past the last one it is degree 0 again */
            hi = n;
            while (lo < hi) { uint32_t m = (lo + hi) >> 1; if (bnd[m] <= a) lo = m + 1u; else hi = m; }
            {
                uint32_t dg = lo >= n ? 0u : lo;
                /* a thin dark line between slices */
                /* an arc of (gap / 65536) x 2 pi r pixels: under ~0.8 px is the line between two slices */
                int edge = lo < n && (bnd[lo] - a) * r < 8300u;
                cv_pset((int32_t)x, (int32_t)y, edge ? C_BLACK : col[dg]);
            }
        }
    cv_blit(60, 26);
}

static void draw_line(uint32_t y, const char *s, uint16_t c) { draw_text_box(0, (int32_t)y, 240, &FONT_S, s, c, 1); }

/* the FX page: the effects' values where the scale's lines are */
static char *putpct(char *p, uint32_t raw, uint32_t full) { p = putn(p, (raw * full + 8191u) / 16383u); *p++ = '%'; return p; }
static void draw_fx(void)
{
    static const char *const SYNC[12] = {"", "1/32", "1/16T", "1/16", "1/8T", "1/16.", "1/8", "1/4T", "1/8.", "1/4", "1/4.", "1/2"};
    char b[48], *p = b;
    uint32_t sz = 20u + (eng_fx_get(FX_REV_SIZE) * 78u + 8191u) / 16383u, sy = eng_fx_get(FX_DLY_SYNC);
    p = put(p, "REV "); p = putpct(p, eng_fx_get(FX_REV_LEVEL), 100u);
    p = put(p, "  SIZE 0."); p = putn(p, sz);
    *p = 0;
    draw_line(150, b, M_AMB);
    p = b;
    p = put(p, "DLY "); p = putpct(p, eng_fx_get(FX_DLY_LEVEL), 100u);
    p = put(p, "  FB "); p = putpct(p, eng_fx_get(FX_DLY_FB), 110u); p = put(p, "  ");
    if (sy && sy < 12u) p = put(p, SYNC[sy]);
    else {                                      /* free: 10 ms x 120^(r/16383), 2^x by a quadratic */
        uint32_t x = (eng_fx_get(FX_DLY_TIME) * 452665u) / 16383u, ip = x >> 16, f = x & 0xFFFFu;
        uint32_t m = 65536u + ((f * (43025u + ((22511u * f) >> 16))) >> 16);
        p = putn(p, ((10u * m) << ip) >> 16); p = put(p, "MS");
    }
    *p = 0;
    draw_line(170, b, M_TXT);
}
static void draw_scale(void)
{
    char b[48], nm[20], *p = b;
    if (fx_page) { draw_fx(); return; }
    p = put(p, scale_name(LIB_ORDER[scale_i], nm));
    *p = 0;
    draw_line(150, b, M_TXT);
    p = b;
    p = putn(p, tun_degrees()); p = put(p, " DEG  MODE "); p = putn(p, (uint32_t)tun_mode());
    p = put(p, "  WIN "); if (win < 0) *p++ = '-'; p = putn(p, (uint32_t)(win < 0 ? -win : win));
    if (hold) p = put(p, "  HOLD");
    *p = 0;
    draw_line(170, b, M_DIM);
}
static void draw_seq(uint32_t voices)
{
    char b[48], *p = b;
    uint32_t bpm = seq_tempo();
    p = put(p, playing_seq ? "PLAY " : "STOP "); p = putn(p, bpm / 100u); *p++ = '.'; p = putn(p, bpm / 10u % 10u);
    p = put(p, "  "); p = putn(p, voices); p = put(p, stress ? "/8 TEST" : fx_page ? "/8  FX" : "/8 VOICES");
    *p = 0;
    draw_line(190, b, stress ? M_AMB : M_DIM);
}
static void draw_load(uint32_t avg, uint32_t peak)
{
    char b[48], *p = b;
    uint32_t us = (avg * 5805u) / 1000u;         /* of the 5805 us a 256-frame buffer lasts */
    p = put(p, "DSP "); p = putn(p, avg / 10u); p = put(p, "%  PEAK "); p = putn(p, peak / 10u);
    if (shed_count) { p = put(p, "%  CUT "); p = putn(p, shed_count); }     /* voices dropped for load */
    else { p = put(p, "%  "); p = putn(p, us / 1000u); *p++ = '.'; p = putn(p, us / 100u % 10u); p = put(p, " MS"); }
    *p = 0;
    draw_line(212, b, peak >= 800u ? M_RED : peak >= 500u ? M_AMB : M_OK);
}

/* ---- notes from the keys and from MIDI */
static void note_on(int32_t deg, uint32_t vel)
{
    uint32_t n = tun_degrees();
    eng_note_on(0, deg, vel);
    held_deg[imod(deg, (int32_t)n)]++;
}
static void note_off(int32_t deg)
{
    uint32_t n = tun_degrees(), m = (uint32_t)imod(deg, (int32_t)n);
    if (hold) return;
    eng_note_off(0, deg);
    if (held_deg[m]) held_deg[m]--;
}
static void all_off(void)
{
    uint32_t i;
    eng_all_off();
    for (i = 0; i < 256u; i++) held_deg[i] = 0;
}

static uint32_t idle_ok;
static int32_t midi_deg[128];
static uint8_t nrpn_msb = 127, nrpn_lsb = 127, nrpn_dm;
static int32_t white_index(uint32_t note)       /* C4 (60) = 0; black keys: -1000 */
{
    static const int8_t W[12] = {0, -1, 1, -1, 2, 3, -1, 4, -1, 5, -1, 6};
    int32_t o = (int32_t)note / 12 - 5, w = W[note % 12u];
    return w < 0 ? -1000 : o * 7 + w;
}
static void midi_in(void)
{
    uint8_t st, d1, d2;
    while (midi_get(&st, &d1, &d2)) {
        uint32_t ty = st & 0xF0u;
        if (ty == 0x90u && d2) {
            int32_t w = white_index(d1);
            if (w > -1000 && d1 < 128u) { midi_deg[d1] = win + w; note_on(midi_deg[d1], d2); }
        } else if ((ty == 0x80u || ty == 0x90u) && d1 < 128u && white_index(d1) > -1000) note_off(midi_deg[d1]);
        else if (ty == 0xE0u) eng_bend(0, (int32_t)((uint32_t)d2 << 7 | d1) - 8192);
        else if (ty == 0xD0u) eng_aftertouch(0, d1);
        else if (ty == 0xB0u) {
            if (d1 == 1u) eng_modwheel(0, d2);
            else if (d1 == 99u) nrpn_msb = d2;
            else if (d1 == 98u) nrpn_lsb = d2;
            else if (d1 == 6u || d1 == 38u) {
                uint32_t id = (uint32_t)nrpn_msb << 7 | nrpn_lsb;
                if (d1 == 6u) nrpn_dm = d2;
                if (id < P_COUNT) eng_set_param(0, id, d1 == 6u ? (uint32_t)d2 << 7 : (uint32_t)nrpn_dm << 7 | d2);
            } else if (d1 == 123u) all_off();
        }
    }
}
static void app_idle(void) { if (idle_ok) midi_in(); }

/* the four knobs: detents step a parameter of part 1 */
static int fxknob(uint32_t enc, uint32_t id, int32_t step)   /* 1 = turned */
{
    int32_t e = fm1_enc_take(enc), v;
    if (!e) return 0;
    v = (int32_t)eng_fx_get(id) + e * step;
    if (v < 0) v = 0;
    if (v > (int32_t)eng_fx_param_max(id)) v = (int32_t)eng_fx_param_max(id);
    eng_fx_set(id, (uint32_t)v);
    return 1;
}
static void knob(uint32_t enc, uint32_t id, int32_t step)
{
    int32_t e = fm1_enc_take(enc), v;
    if (!e) return;
    v = (int32_t)eng_get_param(0, id) + e * step;
    if (v < 0) v = 0;
    if (v > (int32_t)eng_param_max(id)) v = (int32_t)eng_param_max(id);
    eng_set_param(0, id, (uint32_t)v);
}

static void app_main(void)
{
    uint32_t prev = 0, k, b, rel, t_draw = 0, t_hdr = 0, voices_drawn = ~0u, seq_drawn = ~0u, load_drawn = ~0u;
    int scale_dirty = 1, wheel_dirty = 1;
    eng_init();
    seq_init();
    scale_load();
    demo();
    lcd_fill(0, 0, 240, 240, C_BLACK);

    for (;;) {
        uint32_t keys, down, up, n = tun_degrees();
        int32_t e;
        platform_poll();

        /* keys */
        keys = fm1_in.notes;
        down = keys & ~prev;
        up = prev & ~keys;
        prev = keys;
        for (k = 0; k < 27u; k++) {
            int32_t w = key_white(k), bk;
            if (down >> k & 1u) {
                if (w >= 0) { key_deg[k] = win + w; note_on(key_deg[k], 100); wheel_dirty = 1; }
                else if ((bk = key_black(k)) >= 0) {
                    switch (bk) {
                    case 0: win--; break;
                    case 1: win++; break;
                    case 2: win -= (int32_t)n; break;
                    case 3: win += (int32_t)n; break;
                    case 4: hold = !hold; if (!hold) all_off(); break;
                    case 6: all_off(); tun_rotate(tun_mode() - 1); break;
                    case 7: all_off(); tun_rotate(tun_mode() + 1); break;
                    case 8: all_off(); scale_i = (scale_i + sizeof LIB_ORDER - 1u) % sizeof LIB_ORDER; scale_load(); win = 0; break;
                    case 9: all_off(); scale_i = (scale_i + 1u) % sizeof LIB_ORDER; scale_load(); win = 0; break;
                    case 10: all_off(); break;
                    default: break;
                    }
                    scale_dirty = wheel_dirty = 1;
                }
            }
            if (up >> k & 1u && w >= 0) { note_off(key_deg[k]); wheel_dirty = 1; }
        }

        /* buttons */
        b = fm1_input_edges(&rel);
        if (b & (1u << BTN_OCT_DOWN)) { win -= (int32_t)n; scale_dirty = 1; }
        if (b & (1u << BTN_OCT_UP)) { win += (int32_t)n; scale_dirty = 1; }
        if (b & (1u << BTN_PLAY)) { if (playing_seq) seq_stop(); else seq_play(); playing_seq = !playing_seq; }
        if (b & (1u << BTN_REC)) { stress = !stress; stress_set(stress); }
        if (b & (1u << BTN_HOME)) { load_peak = 0; shed_count = 0; }

        /* encoders */
        if (b & (1u << BTN_FX)) { fx_page = !fx_page; scale_dirty = 1; }
        if (fx_page) {                              /* FX: the reverb's and the delay's main controls, shown */
            scale_dirty |= fxknob(ENC_KNOB1, FX_REV_LEVEL, 200) | fxknob(ENC_KNOB2, FX_REV_SIZE, 200)
                         | fxknob(ENC_KNOB3, FX_DLY_LEVEL, 200) | fxknob(ENC_KNOB4, FX_DLY_FB, 200);
        } else {
            knob(ENC_KNOB1, P_CUTOFF, 160);
            knob(ENC_KNOB2, P_RESO, 160);
            knob(ENC_KNOB3, P_E1_R, 160);
            knob(ENC_KNOB4, P_DRIVE, 160);
        }
        if (fx_page && (e = fm1_enc_take(ENC_PRESETS)) != 0) {           /* FX: PRESETS sets the delay's time */
            int32_t sy = (int32_t)eng_fx_get(FX_DLY_SYNC) + e;
            eng_fx_set(FX_DLY_SYNC, (uint32_t)(sy < 0 ? 0 : sy > 11 ? 11 : sy));
            scale_dirty = 1;
        } else if ((e = fm1_enc_take(ENC_PRESETS)) != 0) {
            all_off();
            scale_i = (uint32_t)imod((int32_t)scale_i + e, (int32_t)sizeof LIB_ORDER);
            scale_load(); win = 0; scale_dirty = wheel_dirty = 1;
        }
        if ((e = fm1_enc_take(ENC_SELECT)) != 0) seq_set_tempo((uint32_t)((int32_t)seq_tempo() + e * 100));

        /* the sequencer's MIDI out and its scale requests */
        {
            uint8_t s, d1, d2;
            int32_t req;
            while (seq_midi_get(&s, &d1, &d2)) midi_send(s, d1, d2);
            if ((req = seq_scale_request()) >= 0 && (uint32_t)req < LIB_SCALES) { tun_from_lib(LIB, sizeof LIB, (uint32_t)req); scale_dirty = wheel_dirty = 1; }
        }
        midi_in();

        /* the battery, slowly */
        {
            int32_t bv = fm1_adc_read(FM1_ADC_BATT);
            if (bv > 0) batt_raw = batt_raw ? batt_raw + (bv - batt_raw) / 32 : bv;
        }

        /* LEDs: the white keys that sound, PLAY, REC */
        for (k = 0; k < 16u; k++) fm1_led_key(KEY_LED(WHITE_KEY[k]), held_deg[imod(win + (int32_t)k, (int32_t)n)] != 0);
        fm1_led_key(BTN_PLAY, playing_seq);
        fm1_led_key(BTN_REC, stress);
        fm1_led_key(BTN_FX, fx_page);

        /* the screen: only what changed, the numbers ten times a second */
        idle_ok = 1;
        if (fm1_ms - t_hdr > 2000u || !t_hdr) { t_hdr = fm1_ms | 1u; draw_header(); }
        if (wheel_dirty) { wheel_dirty = 0; draw_wheel(); }
        if (scale_dirty) { scale_dirty = 0; draw_scale(); }
        if (fm1_ms - t_draw > 100u) {
            uint32_t v = eng_voices_active(), sq = (uint32_t)playing_seq | (uint32_t)stress << 1 | (uint32_t)fx_page << 2 | seq_tempo() << 3;
            uint32_t ld = (load_avg / 10u) | (load_peak / 10u) << 10 | (shed_count & 0xFFu) << 20;
            t_draw = fm1_ms;
            if (v != voices_drawn || sq != seq_drawn) { voices_drawn = v; seq_drawn = sq; draw_seq(v); }
            if (ld != load_drawn) { load_drawn = ld; draw_load(load_avg, load_peak); }
        }
        idle_ok = 0;
    }
}
