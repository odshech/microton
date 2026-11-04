/* fx.c — reverb and delay, included by engine.c. Integers only, 16-bit delay memory.
 *
 * Reverb: Jon Dattorro's plate ("Effect Design, Part 1", JAES 1997): a pre-delay, a band-limit, four input
 * diffusers, then a figure-eight tank of two halves (a modulated allpass, a delay, damping, the decay, an allpass,
 * a delay), read by seven taps per side. His lengths are for 29.761 kHz; here they are x 1.4818 for 44.1 kHz.
 * ~76 KB of memory.
 * Delay: stereo at half the rate (22.05 kHz), up to 1.2 s; low-pass, high-pass and a soft drive in the loop,
 * ping-pong, a little wow, and the time glides when changed (as a tape's would). ~106 KB.
 * Both take mono sends; the reverb also takes some of the delay. */

#ifndef ENG_POOL
#define ENG_POOL                                /* the FM-1 build puts the memory in its pool (micro/config.h) */
#endif

/* reverb lengths at 44.1 kHz */
#define RV_PRE 4412u
#define RV_I1 210u
#define RV_I2 159u
#define RV_I3 562u
#define RV_I4 410u
#define RV_EXC 24u                              /* the tank allpasses' excursion, either way */
#define RV_LM 996u
#define RV_LD1 6598u
#define RV_LA2 2667u
#define RV_LD2 5512u
#define RV_RM 1345u
#define RV_RD1 6249u
#define RV_RA2 3936u
#define RV_RD2 4687u
#define DL_N 26464u                             /* the delay: 1.2 s at 22.05 kHz, per side */
#define FX_MEM (RV_PRE + RV_I1 + RV_I2 + RV_I3 + RV_I4 + (RV_LM + 2u * RV_EXC + 4u) + RV_LD1 + RV_LA2 + RV_LD2 \
              + (RV_RM + 2u * RV_EXC + 4u) + RV_RD1 + RV_RA2 + RV_RD2 + 2u * DL_N)

static int16_t FXMEM[FX_MEM] ENG_POOL;

typedef struct { int16_t *b; uint32_t n, w; } dl_t; /* w: the next write, which is also the oldest sample */
static dl_t rv_pre, rv_i[4], rv_lm, rv_ld1, rv_la2, rv_ld2, rv_rm, rv_rd1, rv_ra2, rv_rd2, dl_l, dl_r;

static const uint16_t FX_DEF[FX_COUNT] = {
#define F(id, key, def, max, kind) def,
#include "fx.def"
#undef F
};
static const uint16_t FX_MAX[FX_COUNT] = {
#define F(id, key, def, max, kind) max,
#include "fx.def"
#undef F
};
static uint16_t fxraw[FX_COUNT];
static volatile uint32_t fxdirty;
static uint32_t fx_bpm = 12000u;

static struct {
    int32_t decay, dd2, damp, bwc, mod, rlevel; uint32_t pre;
    int32_t fb, tone_g, lc_g, ping, wow, drive, dlevel, to_rev; int32_t dly_q8;   /* the delay's target, Q8 */
} fxd;
static struct {
    int32_t bw, lpl, lpr; uint32_t lfo;
    int32_t dq, lp[2], hp[2], prev[2]; uint32_t wph;
} fxs;

static inline int32_t clamp16(int32_t x) { return x > 32767 ? 32767 : x < -32768 ? -32768 : x; }
/* a Q15 product rounded towards zero: the tank's losses must not hold a one-bit limit cycle forever */
static inline int32_t mulz(int32_t a, int32_t g) { int32_t p = a * g; return p < 0 ? -((-p) >> 15) : p >> 15; }
static inline int32_t dl_tap(const dl_t *d, uint32_t k)    /* k samples ago, 1 .. n */
{
    uint32_t i = d->w + d->n - k;
    if (i >= d->n) i -= d->n;
    return d->b[i];
}
static inline int32_t dl_push(dl_t *d, int32_t x)          /* writes x, returns what was n samples ago */
{
    int32_t z = d->b[d->w];
    d->b[d->w] = (int16_t)clamp16(x);
    if (++d->w == d->n) d->w = 0;
    return z;
}
static inline int32_t ap(dl_t *d, int32_t x, int32_t g)    /* an allpass of the line's length, g Q15 */
{
    int32_t z = d->b[d->w], v = clamp16(x - mulz(z, g));
    d->b[d->w] = (int16_t)v;
    if (++d->w == d->n) d->w = 0;
    return z + mulz(v, g);
}
static inline int32_t dl_frac(const dl_t *d, int32_t k_q8)  /* k samples ago, fractional (Q8), linear */
{
    uint32_t k = (uint32_t)k_q8 >> 8, f = (uint32_t)k_q8 & 255u;
    int32_t a = dl_tap(d, k), b = dl_tap(d, k + 1u);
    return a + (((b - a) * (int32_t)f) >> 8);
}
static inline int32_t apmod(dl_t *d, int32_t x, int32_t g, int32_t k_q8)
{
    int32_t z = dl_frac(d, k_q8), v = clamp16(x - mulz(z, g));
    d->b[d->w] = (int16_t)v;
    if (++d->w == d->n) d->w = 0;
    return z + mulz(v, g);
}

static void fx_derive(void)
{
    const uint16_t *R = fxraw;
    static const uint16_t BEATS_Q8[12] = {0, 32, 43, 64, 85, 96, 128, 171, 192, 256, 384, 512};
    int32_t c;
    fxd.decay = 6554 + (int32_t)((R[FX_REV_SIZE] * 25559u) / 16383u);          /* 0.2 .. 0.98 */
    c = fxd.decay + 4915;                                                         /* decay diffusion 2: +0.15 */
    fxd.dd2 = c < 8192 ? 8192 : c > 16384 ? 16384 : c;                            /* 0.25 .. 0.5 */
    fxd.damp = (int32_t)((R[FX_REV_DAMP] * 29491u) / 16383u);                   /* 0 .. 0.9 */
    fxd.bwc = 9830 + (int32_t)((R[FX_REV_BW] * 22938u) / 16383u);                /* 0.3 .. 1 */
    fxd.mod = q15(R[FX_REV_MOD]);
    fxd.rlevel = q15(R[FX_REV_LEVEL]);
    fxd.pre = 1u + (R[FX_REV_PRE] * (RV_PRE - 2u)) / 16383u;
    fxd.fb = (int32_t)((R[FX_DLY_FB] * 36045u) / 16383u);                        /* 0 .. 1.1 */
    c = 3000 + (int32_t)((R[FX_DLY_TONE] * 10000u) / 16383u) + 1200;            /* cents; +1 octave: half rate */
    fxd.tone_g = (int32_t)FILT_G_Q16[(uint32_t)(c > FILT_CENTS_MAX - 1 ? FILT_CENTS_MAX - 1 : c) / FILT_CENTS_STEP];
    c = 2400 + (int32_t)((R[FX_DLY_LOWCUT] * 6000u) / 16383u) + 1200;           /* 33 Hz .. 1 kHz */
    fxd.lc_g = (int32_t)FILT_G_Q16[(uint32_t)c / FILT_CENTS_STEP];
    fxd.ping = q15(R[FX_DLY_PING]);
    fxd.wow = q15(R[FX_DLY_WOW]);
    fxd.drive = 4096 + (int32_t)((R[FX_DLY_DRIVE] * 12288u) / 16383u);           /* x1 .. x4 */
    fxd.dlevel = q15(R[FX_DLY_LEVEL]);
    fxd.to_rev = q15(R[FX_DLY_TO_REV]);
    if (R[FX_DLY_SYNC] && R[FX_DLY_SYNC] < 12u) {
        uint32_t bpm = fx_bpm < 3000u ? 3000u : fx_bpm;
        fxd.dly_q8 = (int32_t)((132300000u / bpm) * BEATS_Q8[R[FX_DLY_SYNC]]);  /* half-rate samples per beat, Q8 */
    } else {
        int32_t cc = (int32_t)((R[FX_DLY_TIME] * 132619u) / 16383u);            /* log2(120) octaves, c16 */
        int32_t oct = cc / C16_OCT;
        fxd.dly_q8 = (int32_t)((((u64)56448u * exp2_q30(cc - oct * C16_OCT)) >> 30) << oct);   /* 10 ms .. 1.2 s */
    }
    if (fxd.dly_q8 > (int32_t)((DL_N - 80u) << 8)) fxd.dly_q8 = (int32_t)((DL_N - 80u) << 8);
    if (fxd.dly_q8 < 256 * 64) fxd.dly_q8 = 256 * 64;
}

static void fx_init(void)
{
    uint32_t i;
    int16_t *m = FXMEM;
    dl_t *L[] = {&rv_pre, &rv_i[0], &rv_i[1], &rv_i[2], &rv_i[3], &rv_lm, &rv_ld1, &rv_la2, &rv_ld2,
                 &rv_rm, &rv_rd1, &rv_ra2, &rv_rd2, &dl_l, &dl_r};
    const uint32_t N[] = {RV_PRE, RV_I1, RV_I2, RV_I3, RV_I4, RV_LM + 2u * RV_EXC + 4u, RV_LD1, RV_LA2, RV_LD2,
                          RV_RM + 2u * RV_EXC + 4u, RV_RD1, RV_RA2, RV_RD2, DL_N, DL_N};
    for (i = 0; i < FX_MEM; i++) FXMEM[i] = 0;                   /* the pool is not cleared at boot */
    for (i = 0; i < sizeof N / sizeof N[0]; i++) { L[i]->b = m; L[i]->n = N[i]; L[i]->w = 0; m += N[i]; }
    for (i = 0; i < FX_COUNT; i++) fxraw[i] = FX_DEF[i];
    fx_derive();
    fxs.bw = fxs.lpl = fxs.lpr = 0; fxs.lfo = 0;
    fxs.dq = fxd.dly_q8; fxs.lp[0] = fxs.lp[1] = fxs.hp[0] = fxs.hp[1] = fxs.prev[0] = fxs.prev[1] = 0; fxs.wph = 0;
    fxdirty = 0;
}

/* one block: rev and dly are the mono sends; the returns are added to out (L R). The sends are used up. */
static void fx_run(int32_t *out, int32_t *rev, const int32_t *dly)
{
    uint32_t i;
    if (fxdirty) { fxdirty = 0; fx_derive(); }

    /* the delay, at half rate: 8 steps per block */
    {
        int32_t fb = fxd.fb, ping = fxd.ping, nping = 32768 - fxd.ping, lvl = fxd.dlevel, tr = fxd.to_rev;
        int32_t wow = (((sine(fxs.wph) * (fxd.wow >> 1)) >> 14) * 6144) >> 15;   /* up to ±24 samples, Q8 */
        fxs.wph += 64000000u;                                              /* ~0.66 Hz */
        for (i = 0; i < ENG_BLOCK / 2u; i++) {
            int32_t x = clamp16((dly[2 * i] + dly[2 * i + 1]) >> 1), s[2], f[2], c;
            int32_t k = fxs.dq + wow;
            fxs.dq += (fxd.dly_q8 - fxs.dq) >> 9;                            /* the time glides */
            s[0] = dl_frac(&dl_l, k); s[1] = dl_frac(&dl_r, k);
            for (c = 0; c < 2; c++) {                                     /* the loop's filters and drive */
                int32_t y = s[c];
                fxs.lp[c] += (int32_t)(((i64)fxd.tone_g * (y - fxs.lp[c])) >> 16);
                y = fxs.lp[c];
                fxs.hp[c] += (int32_t)(((i64)fxd.lc_g * (y - fxs.hp[c])) >> 16);
                y -= fxs.hp[c];
                f[c] = sat((int32_t)(((i64)y * fxd.drive) >> 12));
            }
            dl_push(&dl_l, x + (int32_t)(((i64)(((f[1] * ping) >> 15) + ((f[0] * nping) >> 15)) * fb) >> 15));
            dl_push(&dl_r, ((x * nping) >> 15) + (int32_t)(((i64)(((f[0] * ping) >> 15) + ((f[1] * nping) >> 15)) * fb) >> 15));
            for (c = 0; c < 2; c++) {                                     /* back to full rate, linearly */
                int32_t y1 = (s[c] * lvl) >> 15;
                out[4 * i + c] += (fxs.prev[c] + y1) >> 1;
                out[4 * i + 2 + c] += y1;
                fxs.prev[c] = y1;
            }
            if (tr) { int32_t m = (((s[0] + s[1]) >> 1) * tr) >> 15; rev[2 * i] += m; rev[2 * i + 1] += m; }
        }
    }

    /* the plate */
    {
        int32_t dec = fxd.decay, dd2 = fxd.dd2, damp = fxd.damp, ndamp = 32768 - fxd.damp, bwc = fxd.bwc, lvl = fxd.rlevel;
        int32_t ml, mr, base = (int32_t)RV_EXC << 8;
        ml = (int32_t)RV_LM * 256 + base + (int32_t)(((i64)((sine(fxs.lfo) * fxd.mod) >> 15) * base) >> 15);
        mr = (int32_t)RV_RM * 256 + base + (int32_t)(((i64)((sine(fxs.lfo + 0x40000000u) * fxd.mod) >> 15) * base) >> 15);
        fxs.lfo += 1090000u;                                               /* ~0.7 Hz */
        for (i = 0; i < ENG_BLOCK; i++) {
            int32_t x, l, r, lfb, rfb, ol, orr;
            dl_push(&rv_pre, rev[i]);
            x = dl_tap(&rv_pre, fxd.pre);
            fxs.bw += mulz(x - fxs.bw, bwc);
            x = ap(&rv_i[0], fxs.bw, 24576);
            x = ap(&rv_i[1], x, 24576);
            x = ap(&rv_i[2], x, 20480);
            x = ap(&rv_i[3], x, 20480);
            lfb = rv_rd2.b[rv_rd2.w]; rfb = rv_ld2.b[rv_ld2.w];          /* the ends of the two halves, crossed */
            l = x + mulz(lfb, dec);
            r = x + mulz(rfb, dec);
            l = apmod(&rv_lm, l, -22938, ml);
            l = dl_push(&rv_ld1, l);
            fxs.lpl = mulz(l, ndamp) + mulz(fxs.lpl, damp);
            l = ap(&rv_la2, mulz(fxs.lpl, dec), dd2);
            dl_push(&rv_ld2, l);
            r = apmod(&rv_rm, r, -22938, mr);
            r = dl_push(&rv_rd1, r);
            fxs.lpr = mulz(r, ndamp) + mulz(fxs.lpr, damp);
            r = ap(&rv_ra2, mulz(fxs.lpr, dec), dd2);
            dl_push(&rv_rd2, r);
            ol = dl_tap(&rv_rd1, 394) + dl_tap(&rv_rd1, 4407) - dl_tap(&rv_ra2, 2835) + dl_tap(&rv_rd2, 2958)
               - dl_tap(&rv_ld1, 2949) - dl_tap(&rv_la2, 277) - dl_tap(&rv_ld2, 1580);
            orr = dl_tap(&rv_ld1, 523) + dl_tap(&rv_ld1, 5374) - dl_tap(&rv_la2, 1820) + dl_tap(&rv_ld2, 3961)
                - dl_tap(&rv_rd1, 3128) - dl_tap(&rv_ra2, 496) - dl_tap(&rv_rd2, 179);
            out[2 * i] += (int32_t)(((i64)ol * lvl) >> 16);                /* x 0.5: seven taps add up */
            out[2 * i + 1] += (int32_t)(((i64)orr * lvl) >> 16);
        }
    }
}

/* ------------------------------------------------------------------ the effects' public calls */
void eng_fx_set(uint32_t id, uint32_t raw)
{
    if (id >= FX_COUNT) return;
    fxraw[id] = (uint16_t)(raw > FX_MAX[id] ? FX_MAX[id] : raw);
    fxdirty = 1;
}
uint32_t eng_fx_get(uint32_t id) { return id < FX_COUNT ? fxraw[id] : 0u; }
uint32_t eng_fx_param_max(uint32_t id) { return id < FX_COUNT ? FX_MAX[id] : 0u; }
uint32_t eng_fx_param_def(uint32_t id) { return id < FX_COUNT ? FX_DEF[id] : 0u; }
void eng_fx_tempo(uint32_t bpm_x100) { fx_bpm = bpm_x100; fxdirty = 1; }
