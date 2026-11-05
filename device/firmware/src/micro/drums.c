/* drums.c — eight FM drums, included by engine.c (it uses the engine's sine, noise and tables).
 *
 * A port of the fmdrum voice (a 32 kHz floating-point design) to integers at 44.1 kHz, sample for sample the
 * same algorithm:
 *   - two envelopes per hit, both exponential per sample: the main one, and the modulation one (a hair faster);
 *     the main one is smoothed (a one-pole) so the attack does not click;
 *   - the carrier's pitch: the base, plus a drop of up to 400 Hz that falls with the square of the modulation
 *     envelope (a fast sweep, then the tone);
 *   - three operators: operator 2 with feedback modulates operator 1, which modulates the carrier; both indices
 *     follow the modulation envelope;
 *   - the models: KICK a sine with a short FM click and its own drive (our addition; the original: a pure sine), SNARE the FM tone and band-passed noise, HAT and METAL
 *     high-passed noise, PERC the FM tone ring-modulated by noise;
 *   - a DC blocker, the level and the drive, and at the end of the drum bus a soft saturation y / (1 + |y|).
 * The per-sample constants of the original are converted to 44.1 kHz (tables.h, tools/gen_tables.py), so a
 * setting sounds the same here. Decay 0..100 is the original's range; 101..127 go on to longer sounds.
 * Each drum is one voice: a new hit restarts it. Drums in the same choke group cut each other. */

static const uint8_t CLAP_GAP[3] = {22, 30, 26};   /* blocks between a clap's bursts: 8, 11, 9.4 ms (uneven, as hands) */
#define CLAP_BURST_DECAY 93u                /* each burst short; the last one rings at the set decay */
#define INC_PITCH 46748u                    /* the phase increment of 0.48 Hz (1 % of 48 Hz) at 44.1 kHz */
#define INC_SWEEP 389566u                   /* of 4 Hz (one step of sweep) */
#define RAD_Q12 166886u                     /* 2^32 / (2 pi), for a phase offset in radians Q12 */
#define SMOOTH_Q30 39229698                 /* 0.05 per sample at 32 kHz, at 44.1 kHz */
#define BP_Q15 7472                         /* 0.3 at 32 kHz */
#define HP_Q15 12952                        /* 0.5 at 32 kHz */
#define DC_Q15 32649                        /* 0.995 at 32 kHz */
#define NOISE_LP_Q15 26840                  /* three one-poles: the original's noise band (it ran at 32 kHz) */
#define DRUM_BUS_GAIN 4                     /* into the bus' saturation (the original mixed four drums at x 0.2) */
enum { DM_KICK, DM_SNARE, DM_HAT, DM_METAL, DM_PERC };

static const uint16_t DP_MAX[DP_COUNT] = {
#define D(id, key, max, kind) max,
#include "drums.def"
#undef D
};

/* the kit: kick, snare, closed hat, open hat as in the fmdrum project (with decays to give them body), then a clap,
 * two toms and a rim */
static const uint16_t DKIT[ENG_DRUMS][DP_COUNT] = {
/* model pitch sweep decay op1 op2 r1  r2  fb  noise drive level pan   rev   dly  choke clap */
  { 0,   120,  80,  110,  70,  0, 60,  0,  0,   6,  140,  100,  8192, 0,    0,    0,    0 },   /* kick: 58 Hz, a beater click, driven */
  { 1,   380,  45,  101,  60, 50, 15, 15, 10,  20,   80,  100,  8192, 2600, 0,    0,    0 },   /* snare: 180 Hz body, less noise */
  { 2,   140,  10,   99,  40, 45, 50, 80, 30,  80,   20,   80,  9600, 1200, 0,    1,    0 },
  { 3,    90,  10,  110,  30, 40, 60, 90, 50,  80,   20,   85,  9600, 2400, 2000, 1,    0 },
  { 1,    10,   0,  103,   0,  0, 10, 10,  0, 100,   35,  100,  7400, 4200, 0,    0,    3 },   /* clap: uneven bursts, a tail */
  { 0,   200,  60,  108,  30,  0, 20,  0,  0,   5,   60,   40,  6200, 2000, 0,    0,    0 },
  { 0,   300,  60,  106,  30,  0, 20,  0,  0,   5,   60,   38,  10200, 2000, 0,   0,    0 },
  { 1,  1000,  30,   96,  40, 10, 35, 50,  0,  10,   80,   90,  8192, 1800, 0,    0,    0 },   /* rim: 480 Hz wood, 1.7 kHz ring */
};

typedef struct {
    uint32_t model, inc, sweep, inc1, inc2;         /* increments: base, the sweep at full, operators 1 and 2 */
    uint32_t dec, decm;                             /* per-sample envelope factors, Q30 */
    int32_t i1, i2, fb, noise, gain, gl, gr, rev, dly, choke, clap, kdrv;
} dderived_t;
typedef struct {
    uint32_t pc, p1, p2;                            /* phases: carrier, operators */
    int32_t em, emod, sm;                           /* envelopes, Q30: main, modulation, the smoothed main */
    int32_t last2;                                  /* operator 2's last output, radians Q12 */
    int32_t hp, bp1, bp2, dcx, dcy, nl1, nl2, nl3;  /* filter states */
    int32_t vel, bursts, btimer, on;
} dvoice_t;

static uint16_t draw_[ENG_DRUMS][DP_COUNT];
static volatile uint32_t ddirty;
static dderived_t DD[ENG_DRUMS];
static dvoice_t DV[ENG_DRUMS];
static uint32_t rate_corr_q16 = 65536u;            /* 44100 / the real rate (the browser may run at 48 kHz) */
static int32_t dbus[2 * ENG_BLOCK];                /* the drum bus, before its saturation */

static uint32_t corr(uint32_t inc) { return (uint32_t)(((u64)inc * rate_corr_q16) >> 16); }
static void drum_derive(uint32_t d)
{
    const uint16_t *R = draw_[d];
    dderived_t *X = &DD[d];
    uint32_t pitch = R[DP_PITCH] < 10u ? 10u : R[DP_PITCH];
    X->model = R[DP_MODEL] > 4u ? 4u : R[DP_MODEL];
    X->inc = corr(pitch * INC_PITCH);
    X->sweep = corr(R[DP_SWEEP] * INC_SWEEP);
    X->inc1 = (uint32_t)(((u64)X->inc * (R[DP_OP1_RATIO] * 6554u)) >> 16);   /* x ratio / 10, no division */
    X->inc2 = (uint32_t)(((u64)X->inc * (R[DP_OP2_RATIO] * 6554u)) >> 16);
    X->dec = DRUM_DEC_Q30[R[DP_DECAY] > 127u ? 127u : R[DP_DECAY]];
    X->decm = DRUM_DECM_Q30[R[DP_DECAY] > 127u ? 127u : R[DP_DECAY]];
    X->i1 = (int32_t)R[DP_OP1_INDEX] * 922;                             /* x 15 x 1.5 radians, Q12 */
    X->i2 = (int32_t)R[DP_OP2_INDEX] * 410;                             /* x 10 radians, Q12 */
    X->fb = (int32_t)R[DP_FEEDBACK] * 655;                              /* x 2, Q15 */
    X->noise = (int32_t)R[DP_NOISE] * 655;                              /* x 2, Q15 */
    /* level x (1 + drive / 20) x 0.2 (the mix) x the bus gain, Q15 (the voice's 0.45 is in drums_run) */
    if (X->model == DM_KICK) {                                          /* the kick's drive shapes its own sine */
        X->kdrv = (int32_t)R[DP_DRIVE] * 8192 / 10;                     /* x (1 + drive / 40) at the peak's knee */
        X->gain = (int32_t)((32768u * R[DP_LEVEL] * DRUM_BUS_GAIN * 7u) / (100u * 5u));
    } else {
        X->kdrv = 0;
        X->gain = (int32_t)((32768u * R[DP_LEVEL] * (20u + R[DP_DRIVE]) * DRUM_BUS_GAIN) / (100u * 20u * 5u));
    }
    {
        int32_t pan = (int32_t)R[DP_PAN] * 4 - 32768;
        uint32_t ang = (uint32_t)(pan + 32768) << 14;
        X->gr = sine(ang);
        X->gl = sine(ang + 0x40000000u);
    }
    X->rev = q15(R[DP_REV]);
    X->dly = q15(R[DP_DLY]);
    X->choke = R[DP_CHOKE];
    X->clap = R[DP_CLAP] > 3u ? 3 : (int32_t)R[DP_CLAP];
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
    if (DD[d].choke)                                                    /* the original's choke: at once */
        for (i = 0; i < ENG_DRUMS; i++)
            if (i != d && DD[i].choke == DD[d].choke && DV[i].on) { DV[i].em = DV[i].emod = DV[i].sm = 0; DV[i].hp = DV[i].dcx = DV[i].dcy = 0; }
    v = &DV[d];
    v->em = v->emod = ONE_Q30;                                          /* the gate: both envelopes to 1 */
    v->vel = vel < 1 ? 1 : vel > 127 ? 127 : vel;
    v->bursts = DD[d].clap; v->btimer = CLAP_GAP[0];
    if (!v->on) { v->sm = 0; v->pc = v->p1 = v->p2 = 0; v->last2 = 0; v->hp = v->bp1 = v->bp2 = v->dcx = v->dcy = v->nl1 = v->nl2 = v->nl3 = 0; }
    v->on = 1;
}

static inline int32_t m15(int32_t a, int32_t b) { return (int32_t)(((i64)a * b) >> 15); }
static inline int32_t m15z(int32_t a, int32_t b) { i64 p = (i64)a * b; return (int32_t)(p < 0 ? -((-p) >> 15) : p >> 15); }

/* one block of every sounding drum: dry onto out (L R) through the bus' saturation, and the two sends (mono) */
static void drums_run(int32_t *out, int32_t *rev, int32_t *dly)
{
    uint32_t d, i, any = 0;
    for (i = 0; i < 2u * ENG_BLOCK; i++) dbus[i] = 0;
    for (d = 0; d < ENG_DRUMS; d++) {
        dvoice_t *v = &DV[d];
        const dderived_t *X = &DD[d];
        int32_t g, gl, gr, rv, dl, em, emod, sm, last2, hp, bp1, bp2, dcx, dcy, nl1, nl2, nl3;
        uint32_t pc, p1, p2;
        if (!v->on) continue;
        if (v->bursts > 0 && --v->btimer == 0) {
            v->em = ONE_Q30; v->bursts--;
            v->btimer = CLAP_GAP[(X->clap - v->bursts) % 3];
        }
        if (v->em < (ONE_Q30 >> 16) && v->sm < (ONE_Q30 >> 16) && v->bursts == 0) { v->on = 0; continue; }
        any = 1;
        g = m15(X->gain, 8192 + (v->vel * 24576) / 127);               /* velocity */
        gl = m15(g, X->gl); gr = m15(g, X->gr);
        rv = m15(g, X->rev); dl = m15(g, X->dly);
        em = v->em; emod = v->emod; sm = v->sm; last2 = v->last2;
        hp = v->hp; bp1 = v->bp1; bp2 = v->bp2; dcx = v->dcx; dcy = v->dcy; nl1 = v->nl1; nl2 = v->nl2; nl3 = v->nl3;
        pc = v->pc; p1 = v->p1; p2 = v->p2;
        for (i = 0; i < ENG_BLOCK; i++) {
            int32_t e15 = emod >> 15, s15, nz, out1, y;
            sm += (int32_t)(((i64)(em - sm) * SMOOTH_Q30) >> 30);      /* first, as in the original */
            s15 = sm >> 15;
            nz = rnd_q15();                                             /* white, then held to ~16 kHz like the */
            nl1 += m15(nz - nl1, NOISE_LP_Q15); nl2 += m15(nl1 - nl2, NOISE_LP_Q15); nl3 += m15(nl2 - nl3, NOISE_LP_Q15);
            nz = m15(nl3 + (nl3 >> 1), X->noise);                       /* noise x 2 x level: up to +-2 */
            pc += X->inc + (uint32_t)(((u64)(uint32_t)m15(e15, e15) * X->sweep) >> 15);   /* the sweep: env^2 */
            if (X->model == DM_KICK) {
                /* the sine, struck: operator 1 bends it for the first milliseconds only (index x env^4: the
                 * beater's click), then the drive rounds it toward a square (harmonics a small speaker can play) */
                int32_t e2 = m15(e15, e15), c, a;
                p1 += X->inc1;
                c = sine(pc + (uint32_t)m15(m15(sine(p1), X->i1), m15(e2, e2)) * RAD_Q12);
                a = c < 0 ? -c : c;
                c = (c * ((32768 + X->kdrv) >> 4)) / ((32768 + ((a * (X->kdrv >> 4)) >> 11)) >> 4);   /* 32-bit */
                out1 = c + m15(m15(nz, e15), 983);                      /* + noise x env x 0.03 */
            } else {
                int32_t m2, m1, fm;
                p2 += X->inc2;
                m2 = m15(m15(sine(p2 + (uint32_t)m15(last2, X->fb) * RAD_Q12), X->i2), e15);   /* radians Q12 */
                last2 = m2;
                p1 += X->inc1;
                m1 = m15(m15(sine(p1 + (uint32_t)m2 * RAD_Q12), X->i1), e15);
                fm = sine(pc + (uint32_t)m1 * RAD_Q12);
                if (X->model == DM_SNARE) {
                    bp1 += m15(nz - bp1, BP_Q15); bp2 += m15(bp1 - bp2, BP_Q15);
                    out1 = (fm >> 1) + m15(bp1 - bp2, 55706);           /* fm x 0.5 + band x 1.7 */
                } else if (X->model == DM_PERC) {
                    out1 = m15(fm, nz);
                } else {                                                /* hat, metal */
                    hp += m15(nz - hp, HP_Q15);
                    out1 = nz - hp;
                }
            }
            out1 = m15(out1, s15);                                      /* x the smoothed envelope */
            y = out1 - dcx + m15z(dcy, DC_Q15);                         /* DC blocker */
            dcx = out1; dcy = y;
            y = m15(y, 14746);                                          /* x 0.45 */
            dbus[2 * i] += m15(y, gl);
            dbus[2 * i + 1] += m15(y, gr);
            rev[i] += m15(y, rv);
            dly[i] += m15(y, dl);
            em = (int32_t)(((i64)em * (v->bursts > 0 ? DRUM_DEC_Q30[CLAP_BURST_DECAY] : X->dec)) >> 30);
            emod = (int32_t)(((i64)emod * X->decm) >> 30);
        }
        v->em = em; v->emod = emod; v->sm = sm; v->last2 = last2;
        v->hp = hp; v->bp1 = bp1; v->bp2 = bp2; v->dcx = dcx; v->dcy = dcy; v->nl1 = nl1; v->nl2 = nl2; v->nl3 = nl3;
        v->pc = pc; v->p1 = p1; v->p2 = p2;
    }
    if (!any) return;
    for (i = 0; i < 2u * ENG_BLOCK; i++) {                              /* the bus' soft saturation: y / (1 + |y|) */
        int32_t y = dbus[i] >> 1, a;                                    /* Q14: up to 4 into the curve */
        if (y > 65535) y = 65535;
        if (y < -65535) y = -65535;
        a = y < 0 ? -y : y;
        out[i] += ((y * 32768) / (16384 + a)) * 32000 / 32768;
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
