/* drums.c — eight FM drums, included by engine.c (it uses the engine's sine, sat, noise and tables).
 *
 * A drum: a carrier and a modulator (phase modulation, as in the DX7), the modulator with feedback; a pitch
 * envelope on both; an index envelope; filtered noise with its own envelope and, for claps, repeated bursts;
 * a drive, a level, a pan and two effect sends. Each drum is one voice: a new hit restarts it. Drums in the
 * same choke group cut each other (the open hi-hat stops when the closed one plays).
 * Envelopes run at the control rate (every 16 frames), the amplitudes are ramped across the block.
 * Integers only: Q15 audio, Q30 envelopes, 32-bit phases. */

#define DRUM_INC0 796254u                   /* the phase increment of 8.1758 Hz at 44.1 kHz (MIDI note 0) */
#define CLAP_BLOCKS 26u                     /* ~9.4 ms between clap bursts */

/* the parameter table: P(id, key, max, kind) as in params.def; the kit's defaults are below */
static const uint16_t DP_MAX[DP_COUNT] = {
#define D(id, key, max, kind) max,
#include "drums.def"
#undef D
};

/* the kit: kick, snare, closed hat, open hat, clap, low tom, high tom, rim */
static const uint16_t DKIT[ENG_DRUMS][DP_COUNT] = {
/*  tune  p.amt p.dec ratio  fm   fm.dec fb   noise n.tone n.dec decay clap drive level pan   rev   dly  choke */
  { 2600, 9000, 3800,    1, 2400, 2800,    0, 1000, 3000, 1400, 8400,    0, 3000, 11475, 8192,    0,    0,    0 },
  { 6600, 3600, 3000,    1, 3000, 3000,    0, 7400, 13500, 6400, 6200,    0, 2500, 10200, 8192, 2600,    0,    0 },
  { 9800,    0, 2000,   13, 9000, 4800, 9000, 7500, 16383, 5600, 5000,    0,    0, 7650, 9600, 1200,    0,    1 },
  { 9800,    0, 2000,   13, 9000, 8200, 9000, 7500, 16000, 9800, 9400,    0,    0, 7310, 9600, 2400, 2000,    1 },
  { 7000,    0, 2000,    1,    0, 2000,    0, 12500, 13000, 6800, 1200,    3, 2000, 9775, 7400, 4200,    0,    0 },
  { 4700, 5200, 4600,    1, 1600, 3600,    0,  600, 4000, 1800, 8000,    0, 1500, 9775, 6200, 2000,    0,    0 },
  { 5800, 5200, 4400,    1, 1600, 3600,    0,  600, 4000, 1800, 7600,    0, 1500, 9350, 10200, 2000,    0,    0 },
  { 9000, 1500, 1800,    6, 4200, 2600, 2500, 2500, 13500, 3200, 3800,    0, 4000, 8500, 8192, 1800,    0,    0 },
};
static const uint16_t RATIO_Q8[16] = {     /* the modulator's ratio to the carrier: whole, half and inharmonic */
    128, 256, 384, 512, 640, 768, 1024, 1280, 1536, 1792, 361, 706, 804, 1329, 1825, 3584 };

typedef struct {
    int32_t base_c16, pamt_c16;             /* the pitch, in cents x 16 above 8.18 Hz; the pitch envelope's depth */
    int32_t pdec, fdec, ndec, adec;         /* decay coefficients per control tick, Q30 */
    uint32_t ratio;                         /* Q8 */
    int32_t fm, fb, noise, ng, nhp, nflat;  /* index Q15, feedback Q15, noise level Q15, noise filter */
    uint32_t clap;
    int32_t drive, level, gl, gr, rev, dly, choke;
} dderived_t;
typedef struct {
    uint32_t cph, mph;
    int32_t m1, m2;                         /* the modulator's last two outputs (feedback) */
    int32_t aenv, penv, fenv, nenv;         /* Q30 */
    int32_t a, n;                           /* amplitudes now, Q15 (ramped) */
    int32_t vel, lp, bursts, btimer, on;
} dvoice_t;

static uint16_t draw_[ENG_DRUMS][DP_COUNT];
static volatile uint32_t ddirty;
static dderived_t DD[ENG_DRUMS];
static dvoice_t DV[ENG_DRUMS];
static uint32_t rate_corr_q16 = 65536u;    /* 44100 / the real rate (the browser may run at 48 kHz) */

static int32_t time_coef(uint32_t r) { return lerp_tab(TIME_COEF_Q30, r); }
static uint32_t drum_inc(int32_t c16)
{
    int32_t oct = c16 / C16_OCT, r = c16 - oct * C16_OCT;
    uint32_t inc;
    if (r < 0) { r += C16_OCT; oct--; }
    inc = (uint32_t)(((u64)DRUM_INC0 * exp2_q30(r)) >> 30);
    inc = (uint32_t)(((u64)inc * rate_corr_q16) >> 16);
    if (oct > 0) inc = oct > 12 || inc > (MAX_INC >> oct) ? MAX_INC : inc << oct;
    else if (oct < 0) inc = oct < -31 ? 0u : inc >> -oct;
    return inc > MAX_INC ? MAX_INC : inc;
}

static void drum_derive(uint32_t d)
{
    const uint16_t *R = draw_[d];
    dderived_t *X = &DD[d];
    int32_t t = (int32_t)R[DP_NOISE_TONE] - 8192;
    X->base_c16 = (1548 + (int32_t)((R[DP_TUNE] * 9600u) / 16383u)) * 16;    /* 20 Hz .. 5 kHz */
    X->pamt_c16 = (int32_t)((R[DP_PITCH_AMT] * 4800u) / 16383u) * 16;       /* 0 .. 4 octaves down to the tune */
    X->pdec = time_coef(R[DP_PITCH_DEC]);
    X->fdec = time_coef(R[DP_FM_DEC]);
    X->ndec = time_coef(R[DP_NOISE_DEC]);
    X->adec = time_coef(R[DP_DECAY]);
    X->ratio = RATIO_Q8[R[DP_RATIO] & 15u];
    X->fm = q15(R[DP_FM]);
    X->fb = q15(R[DP_FEEDBACK]);
    X->noise = q15(R[DP_NOISE]);
    /* noise tone: below the centre a low-pass closing to 200 Hz, above it a high-pass opening to 12 kHz */
    X->nflat = t > -200 && t < 200;
    X->nhp = t > 0;
    {
        int32_t c = X->nhp ? 1548 + (int32_t)(((uint32_t)t * 9200u) / 8191u)          /* 20 Hz .. 12 kHz */
                           : 13700 - (int32_t)(((uint32_t)-t * 9500u) / 8192u);       /* 22 kHz .. 200 Hz */
        if (c < 0) c = 0;
        if (c > FILT_CENTS_MAX - 1) c = FILT_CENTS_MAX - 1;
        X->ng = (int32_t)FILT_G_Q16[(uint32_t)c / FILT_CENTS_STEP];
    }
    X->clap = R[DP_CLAP] > 3u ? 3u : R[DP_CLAP];
    X->drive = R[DP_DRIVE] ? 4096 + (int32_t)((R[DP_DRIVE] * 28672u) / 16383u) : 0;    /* x1 .. x8 */
    X->level = q15(R[DP_LEVEL]);
    {
        int32_t pan = (int32_t)R[DP_PAN] * 4 - 32768;
        uint32_t ang = (uint32_t)(pan + 32768) << 14;
        X->gr = sine(ang);
        X->gl = sine(ang + 0x40000000u);
    }
    X->rev = q15(R[DP_REV]);
    X->dly = q15(R[DP_DLY]);
    X->choke = R[DP_CHOKE];
}

static void drums_init(void)
{
    uint32_t d, i;
    for (d = 0; d < ENG_DRUMS; d++) {
        for (i = 0; i < DP_COUNT; i++) draw_[d][i] = DKIT[d][i];
        DV[d].on = 0;
        drum_derive(d);
    }
    ddirty = 0;
}
static void drums_commit(void)
{
    uint32_t d, m = ddirty;
    if (!m) return;
    ddirty = 0;
    for (d = 0; d < ENG_DRUMS; d++) if (m & (1u << d)) drum_derive(d);
}

static void drum_hit(uint32_t d, int32_t vel)
{
    dvoice_t *v;
    uint32_t i;
    if (d >= ENG_DRUMS) return;
    drums_commit();
    if (DD[d].choke)
        for (i = 0; i < ENG_DRUMS; i++)
            if (i != d && DD[i].choke == DD[d].choke && DV[i].on) { DV[i].aenv = 0; DV[i].nenv = 0; }
    v = &DV[d];
    v->cph = 0x40000000u;                   /* starts at the sine's top: the same click every hit */
    v->mph = 0; v->m1 = v->m2 = 0;
    v->aenv = v->penv = v->fenv = v->nenv = ONE_Q30;
    v->vel = vel < 1 ? 1 : vel > 127 ? 127 : vel;
    v->bursts = (int32_t)DD[d].clap; v->btimer = CLAP_BLOCKS;
    if (!v->on) { v->a = v->n = 0; v->lp = 0; }
    v->on = 1;
}

static inline int32_t edecay(int32_t e, int32_t c) { return e - (int32_t)(((i64)e * c) >> 30); }

/* one block of every sounding drum: dry into out (L R), and the two sends (mono) */
static void drums_run(int32_t *out, int32_t *rev, int32_t *dly)
{
    uint32_t d, i;
    for (d = 0; d < ENG_DRUMS; d++) {
        dvoice_t *v = &DV[d];
        const dderived_t *X = &DD[d];
        int32_t velq, at, nt, as, ns, idx, a, n, lp, m1, m2, fbk, gl, gr, rv, dl, drv;
        uint32_t cinc, minc, cph, mph;
        if (!v->on) continue;
        /* the control tick */
        v->aenv = edecay(v->aenv, X->adec);
        v->nenv = edecay(v->nenv, X->ndec);
        v->penv = edecay(v->penv, X->pdec);
        v->fenv = edecay(v->fenv, X->fdec);
        if (v->bursts > 0 && --v->btimer == 0) { v->nenv = ONE_Q30; v->bursts--; v->btimer = CLAP_BLOCKS; }
        velq = 8192 + ((v->vel * 24576) / 127);
        at = (int32_t)(((i64)(v->aenv >> 15) * velq) >> 15);
        nt = (int32_t)(((((i64)(v->nenv >> 15) * X->noise) >> 15) * velq) >> 15);
        if (at < 4 && nt < 4 && v->bursts == 0 && v->a < 4 && v->n < 4) { v->on = 0; continue; }
        cinc = drum_inc(X->base_c16 + (int32_t)(((i64)X->pamt_c16 * (v->penv >> 15)) >> 15));
        minc = (uint32_t)(((u64)cinc * X->ratio) >> 8);
        idx = (int32_t)(((i64)X->fm * (v->fenv >> 15)) >> 15) >> 1;   /* Q14: up to 4 pi */
        as = (at - v->a) / (int32_t)ENG_BLOCK;
        ns = (nt - v->n) / (int32_t)ENG_BLOCK;
        a = v->a; n = v->n; lp = v->lp; m1 = v->m1; m2 = v->m2; cph = v->cph; mph = v->mph;
        fbk = X->fb >> 1; gl = X->gl; gr = X->gr; drv = X->drive;
        rv = (int32_t)(((i64)X->rev * X->level) >> 15);
        dl = (int32_t)(((i64)X->dly * X->level) >> 15);
        for (i = 0; i < ENG_BLOCK; i++) {
            int32_t m, c, s, nz;
            m = sine(mph + ((uint32_t)(((m1 + m2) >> 1) * fbk) << 3));     /* feedback: up to 2 pi */
            m2 = m1; m1 = m;
            c = sine(cph + ((uint32_t)(m * idx) << 4));
            mph += minc; cph += cinc;
            s = (c * a) >> 15;
            if (n) {
                nz = rnd_q15();
                if (!X->nflat) {
                    lp += (int32_t)(((i64)X->ng * (nz - lp)) >> 16);
                    nz = X->nhp ? nz - lp : lp;
                }
                s += (nz * n) >> 15;
            }
            if (drv) s = sat((int32_t)(((i64)s * drv) >> 12));
            s = (int32_t)(((i64)s * X->level) >> 15);
            out[2 * i] += (s * gl) >> 15;
            out[2 * i + 1] += (s * gr) >> 15;
            rev[i] += (int32_t)(((i64)s * rv) >> 15);
            dly[i] += (int32_t)(((i64)s * dl) >> 15);
            a += as; n += ns;
        }
        v->a = at; v->n = nt; v->lp = lp; v->m1 = m1; v->m2 = m2; v->cph = cph; v->mph = mph;
    }
}

/* ------------------------------------------------------------------ the drums' public calls */
void eng_drum_set(uint32_t d, uint32_t id, uint32_t raw)
{
    if (d >= ENG_DRUMS || id >= DP_COUNT) return;
    draw_[d][id] = (uint16_t)(raw > DP_MAX[id] ? DP_MAX[id] : raw);
    ddirty |= 1u << d;
}
uint32_t eng_drum_get(uint32_t d, uint32_t id) { return d < ENG_DRUMS && id < DP_COUNT ? draw_[d][id] : 0u; }
uint32_t eng_drum_param_max(uint32_t id) { return id < DP_COUNT ? DP_MAX[id] : 0u; }
uint32_t eng_drum_param_def(uint32_t d, uint32_t id) { return d < ENG_DRUMS && id < DP_COUNT ? DKIT[d][id] : 0u; }
void eng_rt_drum(uint32_t d, int32_t vel) { drum_hit(d, vel); }
void eng_set_rate(uint32_t hz) { if (hz >= 8000u) rate_corr_q16 = (ENG_FS << 16) / hz; ddirty = (1u << ENG_DRUMS) - 1u; }
