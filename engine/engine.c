/* engine.c — see engine.h. Integers only: Q15 audio, Q30 envelopes, 32-bit phases. */
#include "engine.h"
#include "tables.h"

typedef int64_t i64;
typedef uint64_t u64;

#define ONE_Q30 (1 << 30)
#define C16_OCT 19200                       /* cents x 16 in an octave */
#define MAX_INC 1932735283u                 /* 0.45 x 2^32: just under Nyquist */

/* ------------------------------------------------------------------ parameters */

static const uint16_t P_DEF[P_COUNT] = {
#define P(id, key, def, max, kind) def,
#include "params.def"
#undef P
};
static const uint16_t P_MAX[P_COUNT] = {
#define P(id, key, def, max, kind) max,
#include "params.def"
#undef P
};
static uint16_t raw[ENG_PARTS][P_COUNT];                 /* owned by the audio interrupt (see the queue) */
static uint16_t lock_val[ENG_PARTS][P_COUNT];            /* sequencer parameter locks, for one step */
static uint8_t lock_on[ENG_PARTS][P_COUNT];
static uint8_t dirty[ENG_PARTS];

typedef struct { int32_t a, d, s, r, curve; } envp_t;      /* coefficients Q30, sustain Q30, curve offset Q30 */
typedef struct { uint32_t shape, inc, delay, global; } lfop_t;
typedef struct { uint32_t src, dst; int32_t amt; } modp_t; /* amt Q15, signed, squared response */

/* derived from raw (and the locks), per part: written and read in the audio interrupt only */
typedef struct {
    int32_t wave[2], lvl[2], deg[2];
    uint32_t pw[2];
    int32_t o2_c16, sync, sub, noise, ring, drive;
    int32_t fmode, cut_c, reso, keytrk, fenv_c, fvel_c, hpf_c;
    envp_t env[3];
    lfop_t lfo[2];
    modp_t mod[8];
    int32_t vmode, uni_n, uni_det_c16, uni_spread, glide, bend_range, vel_amp;
    int32_t drift_c16, variance, out_sat, volume, pan;
} derived_t;
static derived_t D[ENG_PARTS];
void eng_rt_commit(void);

static int32_t lerp_tab(const int32_t *t, uint32_t r)       /* raw 0..16383 over a 257-point table, step 64 */
{
    uint32_t i = r >> 6, f = r & 63u;
    return t[i] + (int32_t)(((i64)(t[i + 1] - t[i]) * (int32_t)f) >> 6);
}
static uint32_t lerp_utab(const uint32_t *t, uint32_t r)
{
    uint32_t i = r >> 6, f = r & 63u;
    return t[i] + (uint32_t)((i64)((i64)t[i + 1] - (i64)t[i]) * (i64)f >> 6);
}
static int32_t bip(uint32_t r) { return (int32_t)r - 8192; }               /* -8192 .. 8191 */
static int32_t amt_curve(uint32_t r)                                       /* signed, squared, Q15 */
{
    int32_t a = bip(r), m = a < 0 ? -a : a;
    return (a < 0 ? -1 : 1) * ((a * m) >> 11);
}
static int32_t q15(uint32_t r) { return (int32_t)((r * 32768u) / 16383u); }  /* 0..16383 → 0..32768 */

static void derive(uint32_t part)
{
    uint32_t o, k;
    derived_t *X = &D[part];
    uint16_t R[P_COUNT];
    for (k = 0; k < P_COUNT; k++) R[k] = lock_on[part][k] ? lock_val[part][k] : raw[part][k];
    for (o = 0; o < 2; o++) {
        uint32_t b = o ? P_O2_WAVE : P_O1_WAVE;
        X->wave[o] = (int32_t)((R[b] * 12288u) / 16383u);                /* 0..3 x 4096 */
        X->pw[o] = 85899346u + (uint32_t)R[o ? P_O2_PW : P_O1_PW] * 251675u;   /* 2 % .. 98 % */
        X->lvl[o] = q15(R[o ? P_O2_LEVEL : P_O1_LEVEL]);
        X->deg[o] = (int32_t)R[o ? P_O2_DEG : P_O1_DEG] - 24;
    }
    X->o2_c16 = (bip(R[P_O2_CENTS]) * 1600) / 8192;
    X->sync = R[P_O2_SYNC];
    X->sub = q15(R[P_SUB_LEVEL]);
    X->noise = q15(R[P_NOISE_LEVEL]);
    X->ring = q15(R[P_RING_LEVEL]);
    X->drive = 4096 + (int32_t)((R[P_DRIVE] * 7u * 4096u) / 16383u);    /* x1 .. x8, Q12 */
    X->fmode = R[P_FLT_MODE];
    X->cut_c = (int32_t)((R[P_CUTOFF] * 13700u) / 16383u);              /* cents above 8.18 Hz: .. ~22 kHz */
    X->reso = (int32_t)R[P_RESO];                                       /* Q12 feedback: 0 .. 4 */
    X->keytrk = q15(R[P_FLT_KEYTRK]);
    X->fenv_c = (bip(R[P_FLT_ENV]) * 9600) / 8192;
    X->fvel_c = (bip(R[P_FLT_VEL]) * 4800) / 8192;
    X->hpf_c = R[P_HPF] ? 1548 + (int32_t)((R[P_HPF] * 7976u) / 16383u) : -1;   /* 20 Hz .. 2 kHz */
    for (k = 0; k < 3; k++) {
        uint32_t b = P_E1_A + k * (P_E2_A - P_E1_A);
        X->env[k].a = lerp_tab(TIME_COEF_Q30, R[b]);
        X->env[k].d = lerp_tab(TIME_COEF_Q30, R[b + 1]);
        X->env[k].s = (int32_t)R[b + 2] * 65540;                          /* Q30: 16383 x 65540 = 2^30 - 4 */
        X->env[k].r = lerp_tab(TIME_COEF_Q30, R[b + 3]);
        X->env[k].curve = (int32_t)R[b + 4] * 65537;                      /* 0 .. ~1.0 Q30 below the target */
    }
    for (k = 0; k < 2; k++) {
        uint32_t b = P_L1_SHAPE + k * (P_L2_SHAPE - P_L1_SHAPE);
        X->lfo[k].shape = R[b];
        X->lfo[k].inc = lerp_utab(LFO_INC, R[b + 1]);
        X->lfo[k].delay = R[b + 2] ? (uint32_t)lerp_tab(TIME_COEF_Q30, R[b + 2]) : 0u;
        X->lfo[k].global = R[b + 3];
    }
    for (k = 0; k < 8; k++) {
        uint32_t b = P_M1_SRC + 3 * k;
        X->mod[k].src = R[b];
        X->mod[k].dst = R[b + 1];
        X->mod[k].amt = amt_curve(R[b + 2]);
    }
    X->vmode = R[P_VOICE_MODE];
    X->uni_n = 2 + R[P_UNI_VOICES];
    X->uni_det_c16 = (int32_t)((R[P_UNI_DETUNE] * 800u) / 16383u);
    X->uni_spread = q15(R[P_UNI_SPREAD]);
    X->glide = R[P_GLIDE] ? lerp_tab(TIME_COEF_Q30, R[P_GLIDE]) : 0;
    X->bend_range = 1 + R[P_BEND_RANGE];
    X->vel_amp = q15(R[P_VEL_AMP]);
    X->drift_c16 = (int32_t)((R[P_DRIFT] * 160u) / 16383u);
    X->variance = q15(R[P_VARIANCE]);
    X->out_sat = q15(R[P_OUT_SAT]);
    X->volume = q15(R[P_VOLUME]);
    X->pan = bip(R[P_PAN]) * 4;                                         /* Q15 */
}

/* ------------------------------------------------------------------ tuning */

typedef struct { uint32_t n; int32_t c16[ENG_MAX_DEGREES]; } scale_t;
static scale_t SC[2];
static volatile uint32_t sc_active;
static volatile uint32_t ref_inc;                                        /* phase increment of degree 0 */

static int32_t deg_c16(const scale_t *s, int32_t deg)                   /* cents x 16 above degree 0 */
{
    int32_t n = (int32_t)s->n, k, d;
    if (n < 1) return 0;                                                 /* no scale yet: never divide by 0 */
    k = deg / n; d = deg - k * n;
    if (d < 0) { d += n; k--; }
    return k * s->c16[n - 1] + (d ? s->c16[d - 1] : 0);
}
/* with the bend: a position in degrees, Q13; the pitch moves along the scale */
static int32_t pos_c16(const scale_t *s, int32_t pos_q13)
{
    int32_t fl = pos_q13 >> 13, fr = pos_q13 & 8191;
    int32_t a = deg_c16(s, fl);
    return fr ? a + (int32_t)(((i64)(deg_c16(s, fl + 1) - a) * fr) >> 13) : a;
}
static uint32_t exp2_q30(int32_t frac_c16)                              /* 2^(c/19200), 0 <= c < 19200 */
{
    uint32_t x = ((uint32_t)frac_c16 << 16) / C16_OCT, i = x >> 8, f = x & 255u;
    return EXP2_Q30[i] + (uint32_t)(((u64)(EXP2_Q30[i + 1] - EXP2_Q30[i]) * f) >> 8);
}
static uint32_t inc_of(int32_t c16)                                     /* phase increment, c16 from degree 0 */
{
    int32_t oct = c16 / C16_OCT, r = c16 - oct * C16_OCT;
    uint32_t inc;
    if (r < 0) { r += C16_OCT; oct--; }
    inc = (uint32_t)(((u64)ref_inc * exp2_q30(r)) >> 30);
    if (oct > 0) inc = oct > 12 || inc > (MAX_INC >> oct) ? MAX_INC : inc << oct;
    else if (oct < 0) inc = oct < -31 ? 0u : inc >> -oct;
    return inc > MAX_INC ? MAX_INC : inc;
}
static uint32_t scale_by_c16(uint32_t inc, int32_t c16)                 /* inc x 2^(c16/19200), small c16 */
{
    int32_t oct = c16 / C16_OCT, r = c16 - oct * C16_OCT;
    if (r < 0) { r += C16_OCT; oct--; }
    inc = (uint32_t)(((u64)inc * exp2_q30(r)) >> 30);
    if (oct >= 0) return oct > 31 || inc > (0xFFFFFFFFu >> oct) ? 0xFFFFFFFFu : inc << oct;
    return oct < -31 ? 0u : inc >> -oct;
}

/* the reference: degree ref_deg sounds at ref_chz; ref_inc is then the increment of degree 0 */
static int32_t ref_deg;
static uint32_t ref_chz = 26163u;
static void ref_update(void)
{
    uint32_t inc = (uint32_t)(((u64)ref_chz * INC_PER_CHZ_Q24) >> 24);
    int32_t c = deg_c16(&SC[sc_active], ref_deg);
    ref_inc = c ? scale_by_c16(inc, -c) : inc;
}
void eng_set_ref_degree(int32_t degree, uint32_t chz)
{
    if (chz < 100u) chz = 100u;
    if (chz > 2000000u) chz = 2000000u;
    if (degree < -1024) degree = -1024;
    if (degree > 1024) degree = 1024;
    ref_deg = degree;
    ref_chz = chz;
    ref_update();
}
void eng_set_ref_chz(uint32_t chz) { eng_set_ref_degree(0, chz); }
int32_t eng_scale_c16(uint32_t k) { const scale_t *s = &SC[sc_active]; return k < s->n ? s->c16[k] : 0; }
int eng_set_scale_c16(uint32_t n, const int32_t *c16)
{
    uint32_t k, next = sc_active ^ 1u;
    if (n < 1u || n > ENG_MAX_DEGREES || c16[n - 1] <= 0) return -1;
    for (k = 0; k < n; k++) {
        if (c16[k] < 0) return -1;
        SC[next].c16[k] = c16[k];
    }
    SC[next].n = n;
    sc_active = next;                                                    /* one store: the switch */
    ref_update();
    return 0;
}
int eng_scale_from_lib(const uint8_t *lib, uint32_t len, uint32_t index)
{
    static int32_t c[ENG_MAX_DEGREES];
    uint32_t count, ix, off, n, type, period, unit, k, nl;
    if (len < 8u || lib[0] != 'M' || lib[1] != 'T' || lib[2] != 'S' || lib[3] != '1') return -1;
    count = lib[4] | (uint32_t)lib[5] << 8;
    if (index >= count || 8u + 8u * count > len) return -1;
    ix = 8u + 8u * index;
    off = lib[ix] | (uint32_t)lib[ix + 1] << 8 | (uint32_t)lib[ix + 2] << 16 | (uint32_t)lib[ix + 3] << 24;
    n = lib[ix + 4]; type = lib[ix + 5]; period = lib[ix + 6] | (uint32_t)lib[ix + 7] << 8;
    if (n < 1u || type > 2u || off >= len) return -1;
    nl = lib[off];
    off += 1u + nl;
    unit = type == 2u ? 4u : 1u;                                         /* to cents x 16 */
    if (type == 1u) {
        for (k = 0; k < n; k++) c[k] = (int32_t)((period * (k + 1u)) / n);
    } else {
        if (off + 2u * n > len) return -1;
        for (k = 0; k < n; k++) c[k] = (int32_t)((lib[off + 2u * k] | (uint32_t)lib[off + 2u * k + 1u] << 8) * unit);
    }
    return eng_set_scale_c16(n, c);
}
uint32_t eng_scale_degrees(void) { return SC[sc_active].n; }

/* ------------------------------------------------------------------ oscillators */

static uint32_t eng_rng_s = 0x9E3779B9u;
static inline uint32_t eng_rnd(void) { eng_rng_s ^= eng_rng_s << 13; eng_rng_s ^= eng_rng_s >> 17; eng_rng_s ^= eng_rng_s << 5; return eng_rng_s; }
static inline int32_t rnd_q15(void) { return (int32_t)(eng_rnd() >> 16) - 32768; }

static inline int32_t sine(uint32_t p)
{
    uint32_t i = p >> 22, f = (p >> 6) & 0xFFFFu;
    return SINE_Q15[i] + (((SINE_Q15[i + 1] - SINE_Q15[i]) * (int32_t)f) >> 16);
}
static inline int32_t tri(uint32_t p)
{
    int32_t t = (int32_t)(p >> 15);
    return (t < 65536 ? t : 131071 - t) - 32768;
}
/* the residual of a 4-point B-spline BLEP: r(t) = F(t + 2) - H(t), t in samples since the step, Q15.
 * r is odd; for t in [0, 2): r = -1/2 + 2t/3 - t^3/3 + t^4/8 on [0, 1), -(2 - t)^4 / 24 on [1, 2).
 * Two samples on each side of every wrap: much less aliasing than the 2-point polyBLEP. */
static inline int32_t blep4(int32_t t)
{
    if (t < 32768) {
        int32_t t2 = (t * t) >> 15, t3 = (t2 * t) >> 15, t4 = (t3 * t) >> 15;
        return -16384 + ((21845 * t) >> 15) - ((10923 * t3) >> 15) + ((4096 * t4) >> 15);
    } else {
        int32_t u = 65536 - t, u2 = (u * u) >> 15, u4 = (u2 * u2) >> 15;
        return -((u4 * 1365) >> 15);
    }
}
/* a saw, band-limited at its wrap (a step of -2): rinv = 2^31 / (inc >> 8).
 * The correction touches two samples on each side of the wrap: out of line, so the plain ramp stays a few
 * instructions inline (the FM-1 build is -Os: a call per sample per saw cost more than the rest of the voice). */
#if defined(__GNUC__) || defined(__clang__)
#define ENG_INLINE static inline __attribute__((always_inline))
#define ENG_NOINLINE static __attribute__((noinline))
#else
#define ENG_INLINE static inline
#define ENG_NOINLINE static
#endif
ENG_NOINLINE int32_t saw_edge(uint32_t p, uint32_t two, uint32_t rinv)
{
    int32_t c = 0;
    uint32_t q = 0u - p;
    if (p < two) {                                                       /* just after the wrap */
        u64 t = ((u64)(p >> 8) * rinv) >> 16;
        if (t < 65536u) c -= 2 * blep4((int32_t)t);
    }
    if (p && q < two) {                                                  /* just before the next one */
        u64 t = ((u64)(q >> 8) * rinv) >> 16;
        if (t < 65536u) c += 2 * blep4((int32_t)t);
    }
    return c;
}
ENG_INLINE int32_t saw(uint32_t p, uint32_t inc, uint32_t rinv)
{
    int32_t s = (int32_t)(p >> 16) - 32768;
    uint32_t two = inc << 1;
    if (p < two || 0u - p < two) s += saw_edge(p, two, rinv);
    return s;
}
ENG_INLINE int32_t pulse(uint32_t p, uint32_t inc, uint32_t rinv, uint32_t pw)
{
    return saw(p, inc, rinv) - saw(p + pw, inc, rinv) + ((int32_t)(pw >> 16) - 32768);
}
ENG_INLINE int32_t shape(uint32_t s, uint32_t p, uint32_t inc, uint32_t rinv, uint32_t pw)
{
    switch (s) {
    case 0: return sine(p);
    case 1: return tri(p);
    case 2: return saw(p, inc, rinv);
    default: return pulse(p, inc, rinv, pw);
    }
}
/* the continuous wave: 0 sine, 4096 triangle, 8192 saw, 12288 pulse */
ENG_INLINE int32_t osc(int32_t pos, uint32_t p, uint32_t inc, uint32_t rinv, uint32_t pw)
{
    uint32_t s = (uint32_t)pos >> 12, f = (uint32_t)pos & 4095u;
    int32_t a = shape(s, p, inc, rinv, pw), b;
    if (!f || s >= 3u) return a;
    b = shape(s + 1u, p, inc, rinv, pw);
    return a + (((b - a) * (int32_t)f) >> 12);
}
static inline uint32_t rinv_of(uint32_t inc) { uint32_t d = inc >> 8; return 0x80000000u / (d ? d : 1u); }

/* soft clip: slope 1 at 0, reaches ±32768 at ±49152 */
static inline int32_t sat(int32_t x)
{
    int32_t u, u3;
    if (x >= 49152) return 32768;
    if (x <= -49152) return -32768;
    u = (x * 21845) >> 15;                                               /* x / 49152, Q15: |u| < 2^15 */
    u3 = (((u * u) >> 15) * u) >> 15;                                    /* every product under 2^30 */
    return (49152 * (u - ((u3 * 10923) >> 15))) >> 15;                   /* u - u^3/3, no division */
}

/* ------------------------------------------------------------------ voices */

enum { ST_IDLE, ST_ATT, ST_DEC, ST_SUS, ST_REL };
typedef struct { int32_t level; uint32_t stage; } env_t;

typedef struct {
    int32_t part, mono, uni;                /* which part; held by its mono voice; unison index or -1 */
    int32_t fine;                           /* cents x 16 on top of the degree (a sequencer step's offset) */
    int32_t note, gate, vel, age;
    int32_t cur[2];                         /* glide: c16 of each oscillator */
    uint32_t p[3], inc[3], rinv[3];         /* osc 1, osc 2, sub */
    int32_t y1, y2, y3, y4, hp;             /* filter */
    int32_t g, g_step, k;                   /* ladder pole and feedback */
    int32_t g_hp;
    env_t env[3];
    uint32_t lph[2];
    int32_t lval[2], lfade[2], lsh[2];
    int32_t amp, amp_step, gl, gr;
    int32_t drift, vcut, vtime, rand_note;
    int32_t uni_c16, uni_pan;
    int32_t m_wave[2], m_pw[2], m_lvl2, m_noise, m_drive;
} voice_t;

static voice_t V[ENG_VOICES];
static int32_t age_clock;
static uint32_t glph[ENG_PARTS][2];
static int32_t glval[ENG_PARTS][2], glsh[ENG_PARTS][2];
static volatile int32_t bend_in[ENG_PARTS], wheel_in[ENG_PARTS], at_in[ENG_PARTS];

/* note stack for mono and unison, per part */
static int32_t stack[ENG_PARTS][16];
static uint32_t nstack[ENG_PARTS];

/* the event queue: the main loop writes head, the audio interrupt writes tail */
enum { EV_ON, EV_OFF, EV_ALL, EV_PARAM };
typedef struct { int32_t type, part, a, b; } ev_t;
#define EVQ 512u                                                         /* a whole sound's parameters fit */
static ev_t evq[EVQ];
static volatile uint32_t ev_head, ev_tail;
static int push(int32_t type, uint32_t part, int32_t a, int32_t b)
{
    uint32_t h = ev_head, nh = (h + 1u) & (EVQ - 1u);
    if (nh == ev_tail || part >= ENG_PARTS) return -1;                   /* full: dropped */
    evq[h].type = type; evq[h].part = (int32_t)part; evq[h].a = a; evq[h].b = b;
    ev_head = nh;
    return 0;
}

static void env_gate(env_t *e, int on) { e->stage = on ? ST_ATT : (e->stage == ST_IDLE ? ST_IDLE : ST_REL); }
static int32_t tmul(int32_t c, int32_t m) { i64 r = ((i64)c * m) >> 15; return r > ONE_Q30 ? ONE_Q30 : (int32_t)r; }
static void env_tick(env_t *e, const envp_t *p, int32_t m)
{
    int32_t t;
    switch (e->stage) {
    case ST_ATT:
        e->level += (int32_t)(((i64)(ONE_Q30 + ONE_Q30 / 4 - e->level) * tmul(p->a, m)) >> 30);
        if (e->level >= ONE_Q30) { e->level = ONE_Q30; e->stage = ST_DEC; }
        break;
    case ST_DEC:
        t = p->s - p->curve;
        e->level += (int32_t)(((i64)(t - e->level) * tmul(p->d, m)) >> 30);
        if (e->level <= p->s) { e->level = p->s; e->stage = ST_SUS; }
        break;
    case ST_SUS:
        e->level = p->s;
        break;
    case ST_REL:
        t = -p->curve;
        e->level += (int32_t)(((i64)(t - e->level) * tmul(p->r, m)) >> 30);
        if (e->level <= (ONE_Q30 >> 14)) { e->level = 0; e->stage = ST_IDLE; }
        break;
    default:
        break;
    }
}

static int32_t lfo_out(uint32_t shp, uint32_t ph, int32_t sh)
{
    switch (shp) {
    case 0: return tri(ph);
    case 1: return sine(ph);
    case 2: return (int32_t)(ph >> 16) - 32768;
    case 3: return ph < 0x80000000u ? 32767 : -32768;
    default: return sh;
    }
}
static void lfo_step(uint32_t *ph, int32_t *val, int32_t *sh, uint32_t shp, uint32_t inc)
{
    uint32_t old = *ph;
    *ph = old + inc;
    if (*ph < old) *sh = rnd_q15();
    *val = lfo_out(shp, *ph, *sh);
}

static void voice_start(voice_t *v, uint32_t part, int32_t note, int32_t vel, int retrig, int32_t fine)
{
    uint32_t k;
    int was_idle = v->env[0].stage == ST_IDLE || v->part != (int32_t)part;
    if (v->part != (int32_t)part) { for (k = 0; k < 3; k++) { v->env[k].stage = ST_IDLE; v->env[k].level = 0; } }
    v->part = (int32_t)part;
    v->note = note; v->vel = vel; v->gate = 1; v->age = ++age_clock; v->fine = fine;
    if (retrig || was_idle) {
        for (k = 0; k < 3; k++) env_gate(&v->env[k], 1);
        v->lfade[0] = v->lfade[1] = 0;
        v->rand_note = rnd_q15();
    }
    if (was_idle || !D[part].glide) v->cur[0] = v->cur[1] = 0x7FFFFFFF;  /* jump to the target */
}
static void voice_stop(voice_t *v)
{
    uint32_t k;
    v->gate = 0;
    for (k = 0; k < 3; k++) env_gate(&v->env[k], 0);
}
/* a free voice for any part: idle first, then the oldest released, then the oldest held */
static voice_t *allocate(void)
{
    uint32_t i, best = 0;
    int32_t ba = 0x7FFFFFFF, score;
    for (i = 0; i < ENG_VOICES; i++) {
        score = V[i].env[0].stage == ST_IDLE ? V[i].age - 0x40000000 : V[i].gate ? V[i].age + 0x20000000 : V[i].age;
        if (score < ba) { ba = score; best = i; }
    }
    V[best].mono = 0; V[best].uni = -1;
    return &V[best];
}

static void do_on(uint32_t part, int32_t note, int32_t vel, int32_t fine)
{
    const derived_t *X = &D[part];
    uint32_t i, n;
    int held;
    if (X->vmode == 0) {                                                 /* poly */
        for (i = 0; i < ENG_VOICES; i++)
            if (V[i].part == (int32_t)part && V[i].gate && V[i].note == note) { voice_start(&V[i], part, note, vel, 1, fine); return; }
        voice_start(allocate(), part, note, vel, 1, fine);
        return;
    }
    /* mono and unison: the note stack; a note over a held one is legato (no retrigger: the glide slides) */
    held = nstack[part] > 0;
    for (i = 0; i < nstack[part]; i++)
        if (stack[part][i] == note) { for (; i + 1 < nstack[part]; i++) stack[part][i] = stack[part][i + 1]; nstack[part]--; break; }
    if (nstack[part] == 16u) { for (i = 0; i + 1 < 16u; i++) stack[part][i] = stack[part][i + 1]; nstack[part]--; }
    stack[part][nstack[part]++] = note;
    n = X->vmode == 1 ? 1u : (uint32_t)X->uni_n;
    for (i = 0; i < n; i++) {
        voice_t *v = 0;
        uint32_t j;
        for (j = 0; j < ENG_VOICES; j++)
            if (V[j].part == (int32_t)part && V[j].mono && V[j].uni == (int32_t)i && V[j].env[0].stage != ST_IDLE) { v = &V[j]; break; }
        if (!v) { v = allocate(); held = 0; }
        v->mono = 1; v->uni = X->vmode == 2 ? (int32_t)i : 0;
        voice_start(v, part, note, vel, !held, fine);
    }
}
static void do_off(uint32_t part, int32_t note)
{
    uint32_t i;
    if (D[part].vmode == 0) {
        for (i = 0; i < ENG_VOICES; i++) if (V[i].part == (int32_t)part && V[i].gate && V[i].note == note) voice_stop(&V[i]);
        return;
    }
    for (i = 0; i < nstack[part]; i++)
        if (stack[part][i] == note) { for (; i + 1 < nstack[part]; i++) stack[part][i] = stack[part][i + 1]; nstack[part]--; break; }
    for (i = 0; i < ENG_VOICES; i++) {
        voice_t *v = &V[i];
        if (v->part != (int32_t)part || !v->mono || !v->gate) continue;
        if (nstack[part]) v->note = stack[part][nstack[part] - 1];       /* back to the held note, no retrigger */
        else voice_stop(v);
    }
}
static void do_all(int32_t part)
{
    uint32_t i;
    for (i = 0; i < ENG_PARTS; i++) if (part < 0 || part == (int32_t)i) nstack[i] = 0;
    for (i = 0; i < ENG_VOICES; i++) if (part < 0 || V[i].part == part) voice_stop(&V[i]);
}

/* the 11 modulation sources, Q15 */
static int32_t mod_src(const voice_t *v, uint32_t s)
{
    switch (s) {
    case 1: return v->lval[0];
    case 2: return v->lval[1];
    case 3: return v->env[2].level >> 15;
    case 4: return v->env[1].level >> 15;
    case 5: return v->vel * 258;
    case 6: { int32_t c; if (v->cur[0] == 0x7FFFFFFF) return 0; c = (v->cur[0] * 11) >> 5;      /* ±5 oct */
              return c > 32767 ? 32767 : c < -32768 ? -32768 : c; }
    case 7: return wheel_in[v->part];
    case 8: return at_in[v->part];
    case 9: return v->rand_note;
    case 10: return (v->lval[0] * wheel_in[v->part]) >> 15;
    default: return 0;
    }
}

/* once per control tick */
static void voice_tick(voice_t *v, const scale_t *sc, uint32_t vi)
{
    const derived_t *X = &D[v->part];
    int32_t md[16] = {0}, k, tm, c16, pitch, tgt, cut, a_tgt, pan;
    uint32_t j;
    tm = 32768 + (((v->vtime * X->variance) >> 15) / 5);                  /* ±20 % envelope times */
    for (j = 0; j < 3; j++) env_tick(&v->env[j], &X->env[j], tm);
    for (j = 0; j < 8; j++)
        if (X->mod[j].src && X->mod[j].dst) md[X->mod[j].dst] += (mod_src(v, X->mod[j].src) * X->mod[j].amt) >> 15;

    /* LFOs, after the sources were read */
    for (j = 0; j < 2; j++) {
        if (X->lfo[j].global) { v->lval[j] = glval[v->part][j]; continue; }
        lfo_step(&v->lph[j], &v->lval[j], &v->lsh[j], X->lfo[j].shape,
                 md[11 + j] ? scale_by_c16(X->lfo[j].inc, (md[11 + j] * 3 * C16_OCT) >> 15) : X->lfo[j].inc);
        if (X->lfo[j].delay) {
            v->lfade[j] += (int32_t)(((i64)(ONE_Q30 - v->lfade[j]) * (int32_t)X->lfo[j].delay) >> 30);
            v->lval[j] = (int32_t)(((i64)v->lval[j] * v->lfade[j]) >> 30);
        }
    }

    /* analog drift: a slow random walk, per voice */
    v->drift += (rnd_q15() >> 6) - (v->drift >> 8);
    if (v->drift > 32767) v->drift = 32767;
    if (v->drift < -32768) v->drift = -32768;

    /* pitch: along the scale, with the bend in degrees */
    pitch = ((md[1] * C16_OCT) >> 15) + ((v->drift * X->drift_c16) >> 15) + v->uni_c16 + v->fine;
    for (j = 0; j < 2; j++) {
        int32_t pos = (v->note + X->deg[j]) * 8192 + bend_in[v->part] * X->bend_range;
        tgt = pos_c16(sc, pos) + (j ? X->o2_c16 + ((md[2] * C16_OCT) >> 15) : 0);
        if (v->cur[j] == 0x7FFFFFFF || !X->glide) v->cur[j] = tgt;
        else v->cur[j] += (int32_t)(((i64)(tgt - v->cur[j]) * X->glide) >> 30);
        c16 = v->cur[j] + pitch;
        v->inc[j] = inc_of(c16);
        v->rinv[j] = rinv_of(v->inc[j]);
    }
    v->inc[2] = v->inc[0] >> 1;
    v->rinv[2] = rinv_of(v->inc[2]);

    /* per-voice shape modulation */
    for (j = 0; j < 2; j++) {
        k = X->wave[j] + ((md[5 + j] * 12288) >> 15);
        v->m_wave[j] = k < 0 ? 0 : k > 12288 ? 12288 : k;
        k = (int32_t)(X->pw[j] >> 16) + ((md[3 + j] * 31457) >> 15);    /* ±48 % */
        v->m_pw[j] = k < 1311 ? 1311 : k > 64225 ? 64225 : k;
    }
    k = X->lvl[1] + md[14]; v->m_lvl2 = k < 0 ? 0 : k > 32768 ? 32768 : k;
    k = X->noise + md[13]; v->m_noise = k < 0 ? 0 : k > 32768 ? 32768 : k;
    k = X->drive + ((md[15] * 7 * 4096) >> 15); v->m_drive = k < 4096 ? 4096 : k > 32768 ? 32768 : k;

    /* filter: cutoff in cents above 8.18 Hz */
    cut = X->cut_c + (int32_t)(((i64)(v->cur[0] >> 4) * X->keytrk) >> 15)
        + (int32_t)(((i64)X->fenv_c * (v->env[1].level >> 15)) >> 15)
        + ((X->fvel_c * (v->vel - 64)) >> 6)
        + ((((v->vcut * X->variance) >> 15) * 300) >> 15)
        + ((md[7] * 9600) >> 15);
    if (cut < 0) cut = 0;
    if (cut > FILT_CENTS_MAX - 1) cut = FILT_CENTS_MAX - 1;
    {
        uint32_t i = (uint32_t)cut / FILT_CENTS_STEP, f = (uint32_t)cut % FILT_CENTS_STEP;
        int32_t g = (int32_t)(FILT_G_Q16[i] + ((FILT_G_Q16[i + 1] - FILT_G_Q16[i]) * f) / FILT_CENTS_STEP);
        v->g_step = (g - v->g) / (int32_t)ENG_BLOCK;
    }
    k = X->reso + ((md[8] * 16384) >> 15);
    v->k = k < 0 ? 0 : k > 16383 ? 16383 : k;
    if (X->hpf_c >= 0) {
        uint32_t i = (uint32_t)X->hpf_c / FILT_CENTS_STEP;
        v->g_hp = (int32_t)FILT_G_Q16[i];
    } else v->g_hp = 0;

    /* amplitude, ramped across the block */
    {
        int32_t velf = 32768 - X->vel_amp + ((X->vel_amp * v->vel) / 127);
        int32_t ampm = 32768 + md[9];
        if (ampm < 0) ampm = 0;
        if (ampm > 65536) ampm = 65536;
        a_tgt = (int32_t)(((((i64)(v->env[0].level >> 15) * velf) >> 15) * ampm) >> 15);
        a_tgt >>= 1;                                                     /* headroom for 8 voices */
        a_tgt = (int32_t)(((i64)a_tgt * X->volume) >> 15);               /* the part's volume */
        if (a_tgt == 0 && v->amp < (int32_t)ENG_BLOCK) v->amp = 0;      /* land on silence exactly */
        v->amp_step = (a_tgt - v->amp) / (int32_t)ENG_BLOCK;
    }

    /* pan: constant power */
    pan = X->pan + v->uni_pan + md[10];
    if (pan > 32767) pan = 32767;
    if (pan < -32768) pan = -32768;
    {
        uint32_t ang = (uint32_t)(pan + 32768) << 14;                    /* 0 .. 2^30: a quarter turn */
        v->gr = sine(ang);
        v->gl = sine(ang + 0x40000000u);
    }
    (void)vi;
}

/* one oscillator over a block, from its phases: one tight loop per wave, so no per-sample switch.
 * pos: 0 sine, 4096 triangle, 8192 saw, 12288 pulse; between two waves both are made and crossfaded. */
static void wave_block(int32_t *d, const uint32_t *ph, uint32_t s, uint32_t inc, uint32_t rinv, uint32_t pw)
{
    uint32_t i;
    switch (s) {
    case 0: for (i = 0; i < ENG_BLOCK; i++) d[i] = sine(ph[i]); break;
    case 1: for (i = 0; i < ENG_BLOCK; i++) d[i] = tri(ph[i]); break;
    case 2: for (i = 0; i < ENG_BLOCK; i++) d[i] = saw(ph[i], inc, rinv); break;
    default: {
        int32_t c = (int32_t)(pw >> 16) - 32768;
        for (i = 0; i < ENG_BLOCK; i++) d[i] = saw(ph[i], inc, rinv) - saw(ph[i] + pw, inc, rinv) + c;
    }
    }
}
static void osc_block(int32_t *d, const uint32_t *ph, int32_t pos, uint32_t inc, uint32_t rinv, uint32_t pw)
{
    uint32_t s = (uint32_t)pos >> 12, f = (uint32_t)pos & 4095u, i;
    int32_t tmp[ENG_BLOCK];
    wave_block(d, ph, s, inc, rinv, pw);
    if (!f || s >= 3u) return;
    wave_block(tmp, ph, s + 1u, inc, rinv, pw);
    for (i = 0; i < ENG_BLOCK; i++) d[i] += ((tmp[i] - d[i]) * (int32_t)f) >> 12;
}

/* a voice's block, in passes (phases, oscillators, mix, filter): each loop small enough to stay in registers */
static void voice_run(voice_t *v, int32_t *bus)
{
    const derived_t *X = &D[v->part];
    uint32_t i, inc0 = v->inc[0], inc1 = v->inc[1], incs = v->inc[2];
    uint32_t ph0[ENG_BLOCK], ph1[ENG_BLOCK];
    int32_t o0[ENG_BLOCK], o1[ENG_BLOCK];
    int32_t l0 = X->lvl[0] >> 1, l1 = v->m_lvl2 >> 1, lsub = X->sub >> 1, lno = v->m_noise >> 1, lring = X->ring >> 1;

    /* phases: osc 2 restarts on osc 1's wrap when synced */
    {
        uint32_t p0 = v->p[0], p1 = v->p[1];
        if (X->sync) {
            for (i = 0; i < ENG_BLOCK; i++) { uint32_t n0 = p0 + inc0; ph0[i] = p0; ph1[i] = p1; p1 = n0 < p0 ? 0u : p1 + inc1; p0 = n0; }
        } else {
            for (i = 0; i < ENG_BLOCK; i++) { ph0[i] = p0; ph1[i] = p1; p0 += inc0; p1 += inc1; }
        }
        v->p[0] = p0; v->p[1] = p1;
    }
    osc_block(o0, ph0, v->m_wave[0], inc0, v->rinv[0], (uint32_t)v->m_pw[0] << 16);
    osc_block(o1, ph1, v->m_wave[1], inc1, v->rinv[1], (uint32_t)v->m_pw[1] << 16);

    /* the mix (in o0 from here); each product is kept under 2^28 so that five of them fit in 32 bits */
    if (lring) for (i = 0; i < ENG_BLOCK; i++) o1[i] = ((o1[i] * l1) >> 2) + ((((((o0[i] >> 1) * (o1[i] >> 1)) >> 13)) * lring) >> 2);
    else for (i = 0; i < ENG_BLOCK; i++) o1[i] = (o1[i] * l1) >> 2;
    for (i = 0; i < ENG_BLOCK; i++) o0[i] = ((o0[i] * l0) >> 2) + o1[i];
    if (lsub) {
        uint32_t ps = v->p[2], rs = v->rinv[2];
        for (i = 0; i < ENG_BLOCK; i++) { o0[i] += ((saw(ps, incs, rs) - saw(ps + 0x80000000u, incs, rs)) * lsub) >> 2; ps += incs; }
        v->p[2] = ps;
    } else v->p[2] += incs * ENG_BLOCK;
    if (lno) for (i = 0; i < ENG_BLOCK; i++) o0[i] += (rnd_q15() * lno) >> 2;

    /* drive, the ladder, the high-pass, the amplitude and the pan */
    {
        int32_t y1 = v->y1, y2 = v->y2, y3 = v->y3, y4 = v->y4, hp = v->hp, g = v->g, k = v->k, amp = v->amp;
        int32_t drv = v->m_drive, ghp = v->g_hp, mode = X->fmode, gl = v->gl, gr = v->gr;
        int32_t as = v->amp_step, gs = v->g_step;
        for (i = 0; i < ENG_BLOCK; i++) {
            int32_t x = (int32_t)(((i64)(o0[i] >> 12) * drv) >> 12), u;
            if (x > 98304) x = 98304;
            if (x < -98304) x = -98304;
            u = sat(x + ((k * x) >> 14) - ((k * y4) >> 12));
            y1 += (int32_t)(((i64)g * (u - y1)) >> 16);
            y2 += (int32_t)(((i64)g * (y1 - y2)) >> 16);
            y3 += (int32_t)(((i64)g * (y2 - y3)) >> 16);
            y4 += (int32_t)(((i64)g * (y3 - y4)) >> 16);
            o1[i] = mode == 0 ? y4 : mode == 1 ? y2 : mode == 2 ? 2 * (y1 - y2) : u - 2 * y1 + y2;
            g += gs;
        }
        if (ghp) for (i = 0; i < ENG_BLOCK; i++) { hp += (int32_t)(((i64)ghp * (o1[i] - hp)) >> 16); o1[i] -= hp; }
        for (i = 0; i < ENG_BLOCK; i++) {
            int32_t y = (o1[i] * amp) >> 15;
            bus[2 * i] += (y * gl) >> 15;
            bus[2 * i + 1] += (y * gr) >> 15;
            amp += as;
        }
        v->y1 = y1; v->y2 = y2; v->y3 = y3; v->y4 = y4; v->hp = hp; v->g = g; v->amp = amp;
    }
}

/* ------------------------------------------------------------------ public */

void eng_init(void)
{
    uint32_t i, p;
    static const int32_t edo12[12] = {1600, 3200, 4800, 6400, 8000, 9600, 11200, 12800, 14400, 16000, 17600, 19200};
    for (p = 0; p < ENG_PARTS; p++) {
        for (i = 0; i < P_COUNT; i++) { raw[p][i] = P_DEF[i]; lock_on[p][i] = 0; }
        derive(p);
        nstack[p] = 0;
        bend_in[p] = wheel_in[p] = at_in[p] = 0;
    }
    eng_set_ref_chz(26163u);
    eng_set_scale_c16(12u, edo12);
    for (i = 0; i < ENG_VOICES; i++) {
        voice_t *v = &V[i];
        uint32_t z, *w = (uint32_t *)v;
        for (z = 0; z < sizeof(voice_t) / 4u; z++) w[z] = 0u;
        v->p[0] = eng_rnd(); v->p[1] = eng_rnd(); v->p[2] = eng_rnd();             /* free running, like analog */
        v->vcut = rnd_q15(); v->vtime = rnd_q15();
        v->g = 30000; v->uni = -1;
    }
    ev_head = ev_tail = 0;
    eng_rt_hook = 0;
}

void eng_note_on(uint32_t part, int32_t degree, uint32_t velocity)
{
    if (velocity < 1u) velocity = 1u;
    if (velocity > 127u) velocity = 127u;
    push(EV_ON, part, degree, (int32_t)velocity);
}
void eng_note_off(uint32_t part, int32_t degree) { push(EV_OFF, part, degree, 0); }
void eng_all_off(void) { push(EV_ALL, 0, -1, 0); }
void eng_part_off(uint32_t part) { push(EV_ALL, 0, (int32_t)part, 0); }
void eng_bend(uint32_t part, int32_t value) { if (part < ENG_PARTS) bend_in[part] = value < -8192 ? -8192 : value > 8191 ? 8191 : value; }
void eng_modwheel(uint32_t part, uint32_t value) { if (part < ENG_PARTS) wheel_in[part] = (int32_t)(value > 127u ? 127u : value) * 258; }
void eng_aftertouch(uint32_t part, uint32_t value) { if (part < ENG_PARTS) at_in[part] = (int32_t)(value > 127u ? 127u : value) * 258; }

/* parameters travel through the queue, so only the audio interrupt writes them (no race with a lock) */
int eng_set_param(uint32_t part, uint32_t id, uint32_t r)
{
    if (id >= P_COUNT || part >= ENG_PARTS) return -1;
    return push(EV_PARAM, part, (int32_t)id, (int32_t)(r > P_MAX[id] ? P_MAX[id] : r));
}
uint32_t eng_get_param(uint32_t part, uint32_t id) { return id < P_COUNT && part < ENG_PARTS ? raw[part][id] : 0u; }
uint32_t eng_param_max(uint32_t id) { return id < P_COUNT ? P_MAX[id] : 0u; }

/* audio context only: the sequencer plays and locks from inside eng_render */
void (*eng_rt_hook)(uint32_t frames);
void eng_rt_on(uint32_t part, int32_t degree, int32_t velocity, int32_t fine_c16)
{
    if (part >= ENG_PARTS) return;
    eng_rt_commit();                                                     /* this step's locks, before its notes */
    do_on(part, degree, velocity < 1 ? 1 : velocity > 127 ? 127 : velocity, fine_c16);
}
void eng_rt_off(uint32_t part, int32_t degree) { if (part < ENG_PARTS) do_off(part, degree); }
void eng_rt_part_off(uint32_t part) { if (part < ENG_PARTS) do_all((int32_t)part); }
void eng_rt_locks_clear(uint32_t part)
{
    uint32_t i, any = 0;
    if (part >= ENG_PARTS) return;
    for (i = 0; i < P_COUNT; i++) { any |= lock_on[part][i]; lock_on[part][i] = 0; }
    if (any) dirty[part] = 1;
}
void eng_rt_lock(uint32_t part, uint32_t id, uint32_t r)
{
    if (part >= ENG_PARTS || id >= P_COUNT) return;
    lock_val[part][id] = (uint16_t)(r > P_MAX[id] ? P_MAX[id] : r);
    lock_on[part][id] = 1;
    dirty[part] = 1;
}
void eng_rt_commit(void)
{
    uint32_t p;
    for (p = 0; p < ENG_PARTS; p++) if (dirty[p]) { dirty[p] = 0; derive(p); }
}

/* overload: the audio interrupt asks for one voice less. The quietest released voice goes, else the oldest;
 * its amplitude ramps to zero within the next block (no click). Audio context only. 1 = a voice went */
int eng_rt_shed(void)
{
    uint32_t i, best = ENG_VOICES;
    int32_t bs = 0x7FFFFFFF, score;
    for (i = 0; i < ENG_VOICES; i++) {
        voice_t *v = &V[i];
        if (v->env[0].stage == ST_IDLE) continue;
        score = v->gate ? v->age : (v->env[0].level >> 2) - 0x40000000;
        if (score < bs) { bs = score; best = i; }
    }
    if (best == ENG_VOICES) return 0;
    for (i = 0; i < 3u; i++) { V[best].env[i].stage = ST_IDLE; V[best].env[i].level = 0; }
    V[best].gate = 0;
    return 1;
}

uint32_t eng_voices_active(void)
{
    uint32_t i, n = 0;
    for (i = 0; i < ENG_VOICES; i++) n += V[i].env[0].stage != ST_IDLE;
    return n;
}

void eng_render(int32_t *out, uint32_t n)
{
    static int32_t bus[ENG_PARTS][2 * ENG_BLOCK];
    static int32_t dc[2];                                                /* DC blocker state, Q8 */
    const scale_t *sc = &SC[sc_active];
    uint32_t b, i, p, live;

    while (ev_tail != ev_head) {
        const ev_t *e = &evq[ev_tail];
        uint32_t part = (uint32_t)e->part;
        if (e->type != EV_PARAM) eng_rt_commit();                       /* a note sees the parameters sent before it */
        if (e->type == EV_ON) do_on(part, e->a, e->b, 0);
        else if (e->type == EV_OFF) do_off(part, e->a);
        else if (e->type == EV_ALL) do_all(e->a);
        else if (raw[part][e->a] != (uint16_t)e->b) {
            raw[part][e->a] = (uint16_t)e->b;
            if (e->a == P_VOICE_MODE || e->a == P_UNI_VOICES) do_all((int32_t)part);
            dirty[part] = 1;
        }
        ev_tail = (ev_tail + 1u) & (EVQ - 1u);
    }
    eng_rt_commit();

    for (b = 0; b + ENG_BLOCK <= n; b += ENG_BLOCK) {
        if (eng_rt_hook) { eng_rt_hook(ENG_BLOCK); eng_rt_commit(); }   /* the sequencer, block by block */
        live = 0;                                                        /* the parts with a voice this block */
        for (p = 0; p < ENG_PARTS; p++)
            for (i = 0; i < 2u; i++) lfo_step(&glph[p][i], &glval[p][i], &glsh[p][i], D[p].lfo[i].shape, D[p].lfo[i].inc);
        for (i = 0; i < ENG_VOICES; i++) {
            voice_t *v = &V[i];
            const derived_t *X = &D[v->part];
            if (v->env[0].stage == ST_IDLE && v->amp <= 0) { v->amp = 0; continue; }
            /* unison: spread the part's voices in pitch and pan */
            if (X->vmode == 2 && v->mono && v->uni >= 0 && X->uni_n > 1) {
                int32_t j = 2 * v->uni - (X->uni_n - 1);
                v->uni_c16 = (X->uni_det_c16 * j) / (X->uni_n - 1);
                v->uni_pan = ((X->uni_spread * j) / (X->uni_n - 1)) >> 1;
            } else v->uni_c16 = v->uni_pan = 0;
            if (!(live & (1u << v->part))) {                             /* its part's first voice: clear the bus */
                int32_t *bp = bus[v->part];
                live |= 1u << v->part;
                for (p = 0; p < 2u * ENG_BLOCK; p++) bp[p] = 0;
            }
            voice_tick(v, sc, i);
            voice_run(v, bus[v->part]);
        }
        for (p = 0; p < ENG_PARTS; p++)                                  /* each part's output stage, once per sample */
            if ((live & (1u << p)) && D[p].out_sat) {
                int32_t *bp = bus[p], os = D[p].out_sat;
                for (i = 0; i < 2u * ENG_BLOCK; i++) { int32_t y = bp[i]; bp[i] = y + (int32_t)(((i64)(sat(y + (y >> 1)) - y) * os) >> 15); }
            }
        for (i = 0; i < ENG_BLOCK; i++) {
            uint32_t c;
            for (c = 0; c < 2u; c++) {
                int32_t x = 0;
                for (p = 0; p < ENG_PARTS; p++) if (live & (1u << p)) x += bus[p][2 * i + c];
                if (x > 2097151) x = 2097151;                                    /* keeps x << 8 in 32 bits */
                if (x < -2097152) x = -2097152;
                dc[c] += ((x << 8) - dc[c]) >> 10;                               /* coupling cap: ~7 Hz */
                if (x == 0 && dc[c] > -2048 && dc[c] < 2048) dc[c] = 0;         /* settle on true silence */
                x -= dc[c] >> 8;
                out[2u * (b + i) + c] += x;
            }
        }
    }
}
