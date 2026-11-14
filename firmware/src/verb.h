/* SPDX-License-Identifier: GPL-3.0-only */
/* The reverb: a plate in the manner of Jon Dattorro's ("Effect Design, Part 1", JAES 1997), at a low sample rate,
 * in integers, early-digital style: 22 kHz or 11 kHz, the output held between its samples (the grit), the tank's
 * words optionally crushed to fewer bits. Its own output can be fed back into its input through a soft saturator:
 * at most it rings, as a Midiverb pushed to about two o'clock, never more.
 *
 * Included by dinghy.c after sine() and cl(). Runs in the audio interrupt: verb_run() takes a block of mono input
 * (the oscillators' sends) and adds its stereo return to the output. */

#define V_NAP 4                                 /* the input diffusers */
static const uint16_t V_AP_LEN[V_NAP] = {142, 107, 379, 277};
static const int16_t V_AP_G[V_NAP] = {24576, 24576, 20480, 20480};   /* 0.75, 0.75, 0.625, 0.625 */
/* the tank, at Dattorro's 29761 Hz lengths (here they run slower, so the plate is bigger) */
#define V_MAPL 672u
#define V_MAPR 908u
#define V_EXC 16u                               /* the modulated allpasses' excursion */
#define V_D1L 4453u
#define V_APL 1800u
#define V_D2L 3720u
#define V_D1R 4217u
#define V_APR 2656u
#define V_D2R 3163u
#define V_PRE 4096u                             /* the pre-delay, up to ~190 ms at 22 kHz */

typedef struct { int16_t *b; uint32_t len, w; } vline_t;
/* SPACE: a second reverb in the same memory (the plate's tank): four long lines with a Householder matrix between
 * them (a feedback delay network), each swept by its own phase of a slow LFO and damped on its way round. Wide,
 * dense and dark, a space rather than a plate; inspired by the spatial effects of small synths (Korg's "Space Out"
 * on the volca modular), not a copy of one. The same knobs as the plate. */
#define S_NL 4u
static const uint16_t S_LEN[S_NL] = {2203, 2687, 3229, 3917};   /* (at 22 kHz: 100 .. 178 ms, mutually prime-ish) */
#define S_EXC 40u                               /* the lines' sweep at MOD 127: +-20 samples */

static int16_t v_mem[142 + 107 + 379 + 277 + (V_MAPL + V_EXC + 2) + (V_MAPR + V_EXC + 2) + V_D1L + V_APL + V_D2L
                     + V_D1R + V_APR + V_D2R + V_PRE] __attribute__((section(".pool")));
static vline_t v_ap[V_NAP], v_mapl, v_mapr, v_d1l, v_apl, v_d2l, v_d1r, v_apr, v_d2r, v_pre;
static vline_t s_ln[S_NL];                      /* SPACE's lines: over the plate's tank (one or the other runs) */
static int32_t s_lp[S_NL];
static int16_t *v_tank_lo, *v_tank_hi;          /* the shared memory: cleared when the type changes */
static volatile uint8_t v_type;                 /* 0 PLATE, 1 SPACE: set by the main loop */
static uint8_t v_type_run;

typedef struct {                                /* set by the main loop (knob units 0..127) */
    volatile uint8_t size, decay, damp, pre, mod, crush, ret, fb, rate, freeze;
} verb_par_t;
static verb_par_t vp;
static volatile int32_t v_level;                /* the return's level now, for the screen */
static volatile uint32_t v_gate_freeze;         /* the gate matrix's FREEZE */
static volatile int32_t eng_mask = ~15;         /* the engine's word length: ~15 = 12 bits, ~255 = 8 (on 16) */
static volatile uint32_t v_ready;               /* set by verb_init: the audio interrupt runs before app_main does */
static int32_t v_damp_l, v_damp_r, v_bw, v_last_l, v_last_r, v_out_l, v_out_r, v_fb;
static uint32_t v_lfo, v_phase;

static void vline_init(vline_t *l, int16_t **mem, uint32_t len)
{
    l->b = *mem;
    l->len = len;
    l->w = 0;
    *mem += len;
}
static void verb_init(void)
{
    int16_t *m = v_mem;
    uint32_t i;
    for (i = 0; i < V_NAP; i++) vline_init(&v_ap[i], &m, V_AP_LEN[i]);
    vline_init(&v_mapl, &m, V_MAPL + V_EXC + 2u);
    vline_init(&v_mapr, &m, V_MAPR + V_EXC + 2u);
    vline_init(&v_d1l, &m, V_D1L); vline_init(&v_apl, &m, V_APL); vline_init(&v_d2l, &m, V_D2L);
    vline_init(&v_d1r, &m, V_D1R); vline_init(&v_apr, &m, V_APR); vline_init(&v_d2r, &m, V_D2R);
    v_tank_hi = m;                              /* (the tank ends here: the pre-delay follows) */
    vline_init(&v_pre, &m, V_PRE);
    v_tank_lo = v_mapl.b;
    {
        int16_t *m2 = v_tank_lo;
        for (i = 0; i < S_NL; i++) vline_init(&s_ln[i], &m2, S_LEN[i] + S_EXC + 2u);
    }
    v_ready = 1;                                /* (last: the lines are all in place) */
}

static inline __attribute__((always_inline)) int16_t sat16(int32_t v) { return (int16_t)(v > 32767 ? 32767 : v < -32767 ? -32767 : v); }
static inline __attribute__((always_inline)) int32_t vread(const vline_t *l, uint32_t d)      /* d samples ago, 1..len */
{
    uint32_t i = l->w + l->len - d;
    if (i >= l->len) i -= l->len;
    return l->b[i];
}
static inline __attribute__((always_inline)) int16_t qr16(int32_t v, int32_t mask)   /* to 16 bits, then to the mask's */
{
    int32_t h = (int32_t)(((uint32_t)~mask + 1u) >> 1);   /* half a step: rounded, not chopped (no DC creeping in) */
    return (int16_t)(sat16(v + h) & mask);
}
static inline __attribute__((always_inline)) void vwrite(vline_t *l, int32_t v, int32_t mask)
{
    l->b[l->w] = qr16(v, mask);                 /* (the crush: to fewer bits, rounded) */
    if (++l->w >= l->len) l->w = 0;
}
static inline __attribute__((always_inline)) int32_t vallpass(vline_t *l, uint32_t d, int32_t g, int32_t x, int32_t mask)   /* g Q15 */
{
    int32_t del = vread(l, d), u = x - ((del * g) >> 15);
    vwrite(l, u, mask);
    return del + ((u * g) >> 15);
}
static inline __attribute__((always_inline)) int32_t vallpass_frac(vline_t *l, uint32_t d_q8, int32_t g, int32_t x, int32_t mask)   /* a moving length */
{
    uint32_t d = d_q8 >> 8, f = d_q8 & 255u;
    int32_t a = vread(l, d), b = vread(l, d + 1u), del = (a * (int32_t)(256u - f) + b * (int32_t)f) >> 8;
    int32_t u = x - ((del * g) >> 15);
    vwrite(l, u, mask);
    return del + ((u * g) >> 15);
}
static inline __attribute__((always_inline)) int32_t vread_frac(const vline_t *l, uint32_t d_q8)   /* d Q8 samples ago */
{
    uint32_t d = d_q8 >> 8, f = d_q8 & 255u;
    return (vread(l, d) * (int32_t)(256u - f) + vread(l, d + 1u) * (int32_t)f) >> 8;
}
static inline __attribute__((always_inline)) int32_t softclip(int32_t x)              /* x - x^3 / 3 on the clamped input: never more than 2/3 full scale */
{
    int32_t t;
    x = x > 32767 ? 32767 : x < -32767 ? -32767 : x;
    t = (x * x) >> 15;
    return x - ((x * t) >> 15) / 3;
}

/* the knobs as they stand this block, after the mod matrix (Q8 knob offsets in m[]) */
typedef struct { int32_t size, decay, damp, ret, fb; } verb_mod_t;

#ifndef FB_MAX
#define FB_MAX 15000                            /* the feedback at full: calibrated (host tests) to just ring at a medium decay */
#endif
#ifndef FB_LIM
#define FB_LIM 6000                             /* the level the feedback backs off toward: a ring, not a scream */
#endif
static int32_t v_fbenv;
static int32_t v_hl[4], v_hr[4];                /* HI-FI: the last four reverb samples, for the interpolation */
/* HI-FI: Catmull-Rom between the reverb's samples (in place of holding them), weights /128 for t = 0, 1/4, 1/2,
 * 3/4 of the way from v_h?[1] to v_h?[2] (two reverb samples late) */
static const int16_t V_CR[4][4] = {{0, 128, 0, 0}, {-9, 111, 29, -3}, {-8, 72, 72, -8}, {-3, 29, 111, -9}};

static void verb_run(const int32_t *in, int32_t *out, uint32_t n, const verb_mod_t *m, int32_t *wet)
{
    uint32_t i, div = vp.rate ? 4u : 2u, k, frz = vp.freeze | v_gate_freeze, hifi = !eng_half;
    int32_t size_k = cl(vp.size + (m->size >> 8), 0, 127), dec_k = cl(vp.decay + (m->decay >> 8), 0, 127);
    int32_t damp_k = cl(vp.damp + (m->damp >> 8), 0, 127), ret_k = cl(vp.ret + (m->ret >> 8), 0, 127);
    int32_t fb_k = cl(vp.fb + (m->fb >> 8), 0, 127);
    int32_t sz = 9830 + size_k * 179;           /* the lengths x 0.3 .. 1.0, Q15 */
    int32_t decay = frz ? 32767 : 6554 + dec_k * 199;   /* 0.2 .. 0.97 */
    int32_t damp = frz ? 0 : damp_k * 200;              /* the share of the old value kept: 0 .. 0.78 */
    int32_t ret = ret_k * ret_k * 2;            /* the return, squared: 0 .. ~1.0 */
    int32_t fbg = frz ? 0 : fb_k * (FB_MAX / 127);   /* linear: the tail thickens, then (near the top) rings */
    int32_t ingain = frz ? 0 : 32767;
    int32_t mask = ~((1 << (vp.crush > 10u ? 10u : vp.crush)) - 1) & eng_mask;   /* crush 0 .. 10 bits off */
    uint32_t exc = (uint32_t)vp.mod * V_EXC * 2u;   /* the excursion, Q8 samples (0 .. 16) */
    uint32_t pre = 1u + (uint32_t)vp.pre * (V_PRE - 2u) / 127u;
    uint32_t dmapl = (V_MAPL * (uint32_t)sz >> 15), dmapr = (V_MAPR * (uint32_t)sz >> 15);
    uint32_t d1l = V_D1L * (uint32_t)sz >> 15, apl = V_APL * (uint32_t)sz >> 15, d2l = V_D2L * (uint32_t)sz >> 15;
    uint32_t d1r = V_D1R * (uint32_t)sz >> 15, apr = V_APR * (uint32_t)sz >> 15, d2r = V_D2R * (uint32_t)sz >> 15;
#define TAP(len) (((len) * (uint32_t)sz >> 15) + 1u)
    int32_t lvl = 0;
    if (!v_ready) { for (i = 0; i < n; i++) wet[i] = 0; return; }   /* (not set up yet: the sound starts first) */
    if (v_type != v_type_run) {                 /* a new type: the shared memory from silence (once) */
        int16_t *p;
        for (p = v_tank_lo; p < v_tank_hi; p++) *p = 0;
        v_type_run = v_type;
        v_last_l = v_last_r = v_damp_l = v_damp_r = 0;
        for (k = 0; k < S_NL; k++) s_lp[k] = 0;
    }

    for (i = 0; i < n; i++) {
        uint32_t j = v_phase & (div - 1u);    /* (where this frame sits between the reverb's samples) */
        if ((v_phase++ & (div - 1u)) == 0u) {          /* a reverb sample: the input averaged over div frames */
            int32_t x = 0, y, lm, rm;
            for (k = 0; k < div && i + k < n; k++) x += in[i + k];
            x = (x / (int32_t)div) >> 1;
            x = softclip(((x * ingain) >> 15) + v_fb);
            vwrite(&v_pre, x, -1);
            x = vread(&v_pre, pre);
            v_bw += ((x - v_bw) * 24576) >> 15;    /* (the input band: a gentle low-pass) */
            x = v_bw;
            for (k = 0; k < V_NAP; k++) x = vallpass(&v_ap[k], V_AP_LEN[k], V_AP_G[k], x, -1);
            v_lfo += 0x00A00000u;               /* the tank's slow wobble (~0.6 Hz at 22 kHz) */
            if (v_type_run) {                   /* SPACE */
                int32_t yy[S_NL], sum;
                uint32_t sx = (uint32_t)vp.mod * 81u;   /* the sweep, Q8 samples: 0 .. ~40 */
                for (k = 0; k < S_NL; k++) {
                    uint32_t mk = ((uint32_t)(sine(v_lfo + k * 0x40000000u + (k & 1u) * 0x13000000u) + 32768) * sx) >> 16;
                    yy[k] = vread_frac(&s_ln[k], ((S_LEN[k] * (uint32_t)sz >> 15) << 8) + mk + 256u);
                    s_lp[k] = yy[k] + (((s_lp[k] - yy[k]) * damp) >> 15);
                }
                sum = (s_lp[0] + s_lp[1] + s_lp[2] + s_lp[3]) >> 1;   /* Householder: each line less half the sum */
                for (k = 0; k < S_NL; k++) {
                    int32_t in = k & 1u ? -x : x;
                    vwrite(&s_ln[k], softclip(((in * 3) >> 2) + (((s_lp[k] - sum) * decay) >> 15)), mask);
                }
                v_out_l = (yy[0] + yy[2] - (yy[1] >> 1)) * 5 >> 3;
                v_out_r = (yy[1] + yy[3] - (yy[2] >> 1)) * 5 >> 3;
                goto v_fbk;
            }
            lm = (int32_t)((((uint32_t)(sine(v_lfo) + 32768) * exc) >> 16));
            rm = (int32_t)((((uint32_t)(sine(v_lfo + 0x40000000u) + 32768) * exc) >> 16));
            /* the left half of the tank */
            y = softclip(x + ((v_last_r * decay) >> 15));
            y = vallpass_frac(&v_mapl, (dmapl << 8) + (uint32_t)lm + 256u, -22938, y, mask);   /* -0.7 */
            vwrite(&v_d1l, y, mask);
            y = vread(&v_d1l, d1l);
            v_damp_l = y + (((v_damp_l - y) * damp) >> 15);
            y = (v_damp_l * decay) >> 15;
            y = vallpass(&v_apl, apl, 16384, y, mask);                          /* 0.5 */
            vwrite(&v_d2l, y, mask);
            v_last_l = vread(&v_d2l, d2l);
            /* the right half */
            y = softclip(x + ((v_last_l * decay) >> 15));
            y = vallpass_frac(&v_mapr, (dmapr << 8) + (uint32_t)rm + 256u, -22938, y, mask);
            vwrite(&v_d1r, y, mask);
            y = vread(&v_d1r, d1r);
            v_damp_r = y + (((v_damp_r - y) * damp) >> 15);
            y = (v_damp_r * decay) >> 15;
            y = vallpass(&v_apr, apr, 16384, y, mask);
            vwrite(&v_d2r, y, mask);
            v_last_r = vread(&v_d2r, d2r);
            /* the output taps (Dattorro's) */
            v_out_l = (vread(&v_d1r, TAP(266)) + vread(&v_d1r, TAP(2974)) - vread(&v_apr, TAP(1913))
                       + vread(&v_d2r, TAP(1996)) - vread(&v_d1l, TAP(1990)) - vread(&v_apl, TAP(187))
                       - vread(&v_d2l, TAP(1066))) * 3 >> 3;
            v_out_r = (vread(&v_d1l, TAP(353)) + vread(&v_d1l, TAP(3627)) - vread(&v_apl, TAP(1228))
                       + vread(&v_d2l, TAP(2673)) - vread(&v_d1r, TAP(2111)) - vread(&v_apr, TAP(335))
                       - vread(&v_d2r, TAP(121))) * 3 >> 3;
        v_fbk:
            {                                   /* back into the input next time, less as it rings louder */
                int32_t f = (v_out_l + v_out_r) >> 1, a = f < 0 ? -f : f, room;
                if (a > v_fbenv) v_fbenv += (a - v_fbenv) >> 4; else v_fbenv -= (v_fbenv - a) >> 11;
                room = cl(FB_LIM - v_fbenv, 0, FB_LIM);
                v_fb = (((f * fbg) >> 15) * ((room << 15) / FB_LIM)) >> 15;
            }
            v_hl[0] = v_hl[1]; v_hl[1] = v_hl[2]; v_hl[2] = v_hl[3]; v_hl[3] = v_out_l;
            v_hr[0] = v_hr[1]; v_hr[1] = v_hr[2]; v_hr[2] = v_hr[3]; v_hr[3] = v_out_r;
        }
        if (hifi) {                             /* HI-FI: a smooth curve through the reverb's samples */
            const int16_t *c = V_CR[div == 2u ? j * 2u : j];
            int32_t yl = (v_hl[0] * c[0] + v_hl[1] * c[1] + v_hl[2] * c[2] + v_hl[3] * c[3]) >> 7;
            int32_t yr = (v_hr[0] * c[0] + v_hr[1] * c[1] + v_hr[2] * c[2] + v_hr[3] * c[3]) >> 7;
            out[2u * i] += (yl * ret) >> 15;
            out[2u * i + 1u] += (yr * ret) >> 15;
            wet[i] = (yl + yr) >> 1;
        } else {
        out[2u * i] += (v_out_l * ret) >> 15;   /* held between the reverb's samples: the grit */
        out[2u * i + 1u] += (v_out_r * ret) >> 15;
        wet[i] = (v_out_l + v_out_r) >> 1;      /* (for the next in the chain) */
        }
        {
            int32_t a = v_out_l < 0 ? -v_out_l : v_out_l;
            if (a > lvl) lvl = a;
        }
    }
#undef TAP
    v_level = lvl;
}

/* ---- the delay: a shift register of DL_N cells, clocked: the clock sets the time, so a turned TIME bends the
 * repeats' pitch, and a slow clock is a low sample rate (darker, grainier). The input is sampled at each tick,
 * the output held between ticks and smoothed a little (a cutoff that follows the clock, as a BBD's filters do).
 * Safe from the first interrupt: a static buffer, no setup. */
#define DL_N 2048u
static int16_t dl_buf[DL_N] __attribute__((section(".pool")));
typedef struct { volatile uint8_t time, fb, crush, ret, toverb; } dl_par_t;
static dl_par_t dlp;
static uint32_t dl_ph, dl_w;
static int32_t dl_hold, dl_lp, dl_in;
static volatile uint32_t dl_w_v;                /* the write head, for the screen */
#define DL_FB_MAX 29500                         /* ~0.9: long, never runaway (and the loop's saturated) */

/* one frame: in = the sends' sum; returns the delayed signal (before the return level) */
static inline __attribute__((always_inline)) int32_t dl_frame(int32_t in, uint32_t inc, int32_t fbg, int32_t mask,
                                                             int32_t lpc)
{
    uint32_t ph = dl_ph + inc;
    dl_in = in;
    if (ph < dl_ph || inc == 0xFFFFFFFFu) {     /* a clock tick: the oldest cell out, the input (and feedback) in */
        int32_t old = dl_buf[dl_w];
        dl_buf[dl_w] = qr16(softclip((dl_in >> 1) + ((old * fbg) >> 15)), mask);
        if (++dl_w >= DL_N) dl_w = 0;
        dl_hold = old;
    }
    dl_ph = ph;
    dl_lp += ((dl_hold - dl_lp) * lpc) >> 15;
    return dl_lp;
}

/* ---- HI-FI: the delay as a clean line. DLH_N cells at 22.05 kHz (1.18 s); longer times slow the clock (2 s: 13 kHz,
 * still clean). The read head is between cells (cubic), the input averaged into each cell, the output never
 * held. The time glides (~0.1 s): a turned TIME bends the pitch like tape. In the loop a gentle high cut (~6.6 kHz)
 * and the saturator: repeats that darken a little as they go, and never run away. */
#define DLH_N 26000u
#define DLH_ONE (1u << 16)                      /* one cell, Q16 */
static int16_t dlh_buf[DLH_N] __attribute__((section(".pool")));
static uint32_t dlh_w, dlh_ph, dlh_c, dlh_d, dlh_n;   /* write cell; clock phase, step (Q16 cells a frame); distance (Q16) */
static int32_t dlh_acc, dlh_lp;
static inline __attribute__((always_inline)) int32_t dlh_read(uint32_t pos)   /* pos: Q16 cells, 0 .. N */
{
    uint32_t i = pos >> 16, f = (pos >> 4) & 4095u, i0 = i ? i - 1u : DLH_N - 1u, i2 = i + 1u, i3 = i + 2u;
    int32_t y0, y1, y2, y3, a, b, c, ff = (int32_t)f;
    if (i2 >= DLH_N) i2 -= DLH_N;
    if (i3 >= DLH_N) i3 -= DLH_N;
    y0 = dlh_buf[i0]; y1 = dlh_buf[i]; y2 = dlh_buf[i2]; y3 = dlh_buf[i3];
    a = (3 * (y1 - y2) + y3 - y0) >> 1;          /* Catmull-Rom */
    b = 2 * y2 + y0 - ((5 * y1 + y3) >> 1);
    c = (y2 - y0) >> 1;
    return y1 + ((((((a * ff) >> 12) + b) * ff >> 12) + c) * ff >> 12);   /* (f Q12: no overflow) */
}
/* the target for a time in ms (the LUT's): the clock's step and the distance */
static void dlh_target(uint32_t ms, uint32_t *c, uint32_t *d)
{
    uint32_t t = ms * 441u / 10u;               /* frames */
    if (t <= 2u * (DLH_N - 8u)) { *c = 32768u; *d = t << 15; }   /* 22.05 kHz, the distance in cells */
    else { *c = ((DLH_N - 8u) << 16) / t; *d = (DLH_N - 8u) << 16; }   /* the whole line, a slower clock */
}
static void dlh_run(const int32_t *in, int32_t *out, int32_t *wet, uint32_t n, uint32_t ms, int32_t fbg, int32_t mask,
                    int32_t ret)
{
    uint32_t i, ct, dt;
    dlh_target(ms, &ct, &dt);
    if (!dlh_c) { dlh_c = ct; dlh_d = dt; }     /* (the first block: no glide in from nothing) */
    dlh_c += (uint32_t)((int32_t)(ct - dlh_c) >> 7);   /* ~0.1 s glides */
    dlh_d += (uint32_t)((int32_t)(dt - dlh_d) >> 7);
    for (i = 0; i < n; i++) {
        uint32_t now, pos;
        int32_t y, w;
        dlh_acc += in[i]; dlh_n++;
        dlh_ph += dlh_c;
        now = (dlh_w << 16) + (dlh_ph & 0xFFFFu);  /* where the clock is, in cells */
        pos = now + (DLH_N << 16) - dlh_d;
        if (pos >= (DLH_N << 16)) pos -= DLH_N << 16;
        y = dlh_read(pos);
        if (dlh_ph >= DLH_ONE) {               /* a tick: a new cell, the input averaged since the last, + feedback */
            int32_t x = dlh_acc / (int32_t)dlh_n;
            dlh_ph -= DLH_ONE;
            dlh_lp += ((y - dlh_lp) * 27853) >> 15;
            dlh_buf[dlh_w] = qr16(softclip((x >> 1) + ((dlh_lp * fbg) >> 15)), mask);
            if (++dlh_w >= DLH_N) dlh_w = 0;
            dlh_acc = 0; dlh_n = 0;
        }
        w = (y * ret) >> 15;
        out[2u * i] += w;
        out[2u * i + 1u] += w;
        wet[i] = y;
    }
    dl_w_v = dlh_w * DL_N / DLH_N;              /* (the screen's head, on its 2048) */
}

/* ---- the flanger: a short delay swept by its own triangle LFO, at 22 or 11 kHz (11: twice the time, grittier),
 * held between its samples as the others. Two taps swept in opposite directions: stereo. Feedback -..+ through the
 * saturator (- hollow and metallic, + the jet), at most ~0.9. Safe from the first interrupt: a static buffer. */
#define FL_N 1024u
static int16_t fl_buf[2][FL_N] __attribute__((section(".pool")));   /* left and right: each its own comb */
typedef struct { volatile uint8_t rate, depth, manual, fb, ret, slow; } fl_par_t;
static fl_par_t flp;
static uint32_t fl_w, fl_lfo, fl_phase;
static int32_t fl_hold_l, fl_hold_r, fl_prev_l, fl_prev_r;
static volatile int32_t fl_lfo_v;               /* the LFO's position, -32768..32767, for the screen */

static inline __attribute__((always_inline)) int32_t fl_read(const int16_t *b, uint32_t d_q8)   /* d samples ago, Q8 */
{
    uint32_t d = d_q8 >> 8, f = d_q8 & 255u, i0 = fl_w + FL_N - d, i1 = i0 + FL_N - 1u;
    if (i0 >= FL_N) i0 -= FL_N;
    if (i1 >= 2u * FL_N) i1 -= 2u * FL_N;
    if (i1 >= FL_N) i1 -= FL_N;
    return (b[i0] * (int32_t)(256u - f) + b[i1] * (int32_t)f) >> 8;
}
static int32_t fl_oct(int32_t base_q8, int32_t oct_q8)   /* base x 2^(oct / 256): the sweep, even in pitch */
{
    int32_t ip = oct_q8 >> 8;                   /* (an arithmetic shift: floor) */
    uint32_t m = ((uint32_t)base_q8 * (LUT_OCTFRAC[oct_q8 & 255] >> 4)) >> 12;   /* 32-bit: base < 2^18 */
    if (ip > 12) ip = 12;
    if (ip < -31) return 0;
    return ip >= 0 ? (int32_t)(m << ip) : (int32_t)(m >> -ip);
}
static int32_t fl_manual(uint32_t k)            /* MANUAL 0..127 -> the centre, Q8 samples: 1 .. ~390, even in pitch */
{
    return fl_oct(256, (int32_t)k * 2200 / 127);
}

/* in: the block's input (sends + the chain); wet: its output, mono, for the next in the chain; out: + the return */
static void fl_run(const int32_t *in, int32_t *wet, int32_t *out, uint32_t n, const uint32_t *lut_rate)
{
    uint32_t hifi = !eng_half, i, k, div = (flp.slow ? 4u : 2u) >> hifi;   /* (HI-FI: twice the rate, twice the taps) */
    uint32_t inc = lut_rate[flp.rate & 127u] >> ((flp.slow ? 3u : 4u) + hifi);   /* the LUT is a step per 32 frames */
    int32_t man = fl_manual(flp.manual) << hifi;
    int32_t span = (int32_t)flp.depth * 512 / 127;                     /* the sweep: up to +-2 octaves, Q8 */
    int32_t fbg = ((int32_t)flp.fb - 64) * (32100 / 63), ret = (int32_t)flp.ret * flp.ret * 2;
    const int32_t lo = 256, hi = (int32_t)(FL_N - 4u) << 8;
    for (i = 0; i < n; i++) {
        uint32_t j = fl_phase & (div - 1u);
        if ((fl_phase++ & (div - 1u)) == 0u) {
            int32_t x = 0, t, tri, e, yl, yr;
            fl_prev_l = fl_hold_l; fl_prev_r = fl_hold_r;
            for (k = 0; k < div && i + k < n; k++) x += in[i + k];
            x /= (int32_t)div;
            fl_lfo += inc;
            t = (int32_t)(fl_lfo >> 16);
            tri = (t < 32768 ? t : 65535 - t) * 2 - 32768;   /* a triangle, -32768 .. 32766 */
            e = (tri * span) >> 15;
            yl = fl_read(fl_buf[0], (uint32_t)cl(fl_oct(man, e), lo, hi));   /* the two taps swept apart */
            yr = fl_read(fl_buf[1], (uint32_t)cl(fl_oct(man, -e), lo, hi));
            fl_buf[0][fl_w] = qr16(softclip((x >> 2) + ((yl * fbg) >> 15)), eng_mask);   /* each fed back on its own: it rings */
            fl_buf[1][fl_w] = qr16(softclip((x >> 2) + ((yr * fbg) >> 15)), eng_mask);   /* (in at a quarter: room to ring) */
            if (++fl_w >= FL_N) fl_w = 0;
            fl_hold_l = softclip(4 * yl);         /* the swept echo alone: it combs against the dry voices */
            fl_hold_r = softclip(4 * yr);         /* in the main mix, so the notches go all the way down */
            fl_lfo_v = tri;
        }
        if (hifi && div == 2u) {                /* HI-FI, SLOW: halfway between the samples, not held */
            int32_t yl = j ? (fl_prev_l + fl_hold_l) >> 1 : fl_prev_l, yr = j ? (fl_prev_r + fl_hold_r) >> 1 : fl_prev_r;
            out[2u * i] += (yl * ret) >> 15;
            out[2u * i + 1u] += (yr * ret) >> 15;
            wet[i] = (yl + yr) >> 1;
            continue;
        }
        out[2u * i] += (fl_hold_l * ret) >> 15;
        out[2u * i + 1u] += (fl_hold_r * ret) >> 15;
        wet[i] = (fl_hold_l + fl_hold_r) >> 1;
    }
}
