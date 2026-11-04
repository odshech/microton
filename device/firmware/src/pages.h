/* SPDX-License-Identifier: GPL-3.0-only */
/* pages.h — the edit pages, included by dinghy.c after its drawing helpers.
 *
 * A page button opens its page in place of the wheel; pressing it again (or ALGORITHM) steps through its banks.
 * The four knobs edit the bank's four parameters, shown with their values and a bar. PRESETS picks the sound
 * (1..3) on the synth pages and the drum (1..8) on the drum page; HOME closes the page.
 *
 *   EDIT  oscillators     SEL  filter        ENV  envelopes     LFO  LFOs and modulation
 *   GLO   voice, output   FX   reverb, delay  ARP  drums (the first 8 white keys play them)
 *
 * Values are shown in their units (Hz, ms, %, cents, octaves), computed in integers. */

enum { T_SYN, T_DRUM, T_FX };
enum {
    F_PCT, F_BIP, F_PAN, F_WAVE, F_PW, F_DEG, F_C100, F_X8, F_X4, F_CUT, F_HPF, F_OCT8, F_OCT4, F_TIME, F_TIMEOFF,
    F_CURVE, F_LFO, F_C50, F_C10, F_DTUNE, F_ST, F_NCUT, F_DEC, F_PRE, F_FB110, F_DTIME, F_DTONE, F_DLC, F_X8OFF,
    F_OPT
};
typedef struct { const char *name; uint8_t fmt; uint16_t id; const char *const *opts; } pdesc_t;
typedef struct { const char *title; pdesc_t p[4]; } bank_t;
typedef struct { const char *name; uint8_t target, nbanks, button; const bank_t *banks; } page_t;

static const char *const O_ONOFF[] = {"OFF", "ON"};
static const char *const O_FMODE[] = {"LP 24", "LP 12", "BP 12", "HP 12"};
static const char *const O_LSHAPE[] = {"TRI", "SINE", "SAW", "SQUARE", "S&H"};
static const char *const O_LGLOB[] = {"PER VOICE", "SHARED"};
static const char *const O_VMODE[] = {"POLY", "MONO", "UNISON"};
static const char *const O_UNI[] = {"2", "3", "4"};
static const char *const O_BEND[] = {"1 DEG", "2 DEG", "3 DEG", "4 DEG"};
static const char *const O_SRC[] = {"NONE", "LFO 1", "LFO 2", "ENV 3", "FILT ENV", "VELOCITY", "PITCH", "MOD WHEEL",
                                    "AFTERTOUCH", "RANDOM", "LFO1 x WHL"};
static const char *const O_DST[] = {"NONE", "PITCH", "OSC2 PITCH", "PW 1", "PW 2", "WAVE 1", "WAVE 2", "CUTOFF",
                                    "RESONANCE", "LEVEL", "PAN", "LFO1 RATE", "LFO2 RATE", "NOISE", "OSC2 LEVEL", "DRIVE"};
static const char *const O_SYNC[] = {"FREE", "1/32", "1/16T", "1/16", "1/8T", "1/16.", "1/8", "1/4T", "1/8.", "1/4",
                                     "1/4.", "1/2"};
static const char *const O_RATIO[] = {"0.5", "1", "1.5", "2", "2.5", "3", "4", "5", "6", "7", "1.41", "2.76", "3.14",
                                      "5.19", "7.13", "14"};
static const char *const O_CLAP[] = {"NONE", "1", "2", "3"};
static const char *const O_NMODE[] = {"LOW-PASS", "BAND-PASS", "HIGH-PASS"};
static const char *const O_CHOKE[] = {"NONE", "GROUP 1", "GROUP 2"};
static const char *const DRUM_NAME[8] = {"KICK", "SNARE", "CL HAT", "OP HAT", "CLAP", "LO TOM", "HI TOM", "RIM"};
#define NONE_P {0, 0, 0, 0}

static const bank_t B_OSC[] = {
    {"OSC 1", {{"WAVE", F_WAVE, P_O1_WAVE, 0}, {"PULSE WIDTH", F_PW, P_O1_PW, 0}, {"LEVEL", F_PCT, P_O1_LEVEL, 0}, {"DEGREES", F_DEG, P_O1_DEG, 0}}},
    {"OSC 2", {{"WAVE", F_WAVE, P_O2_WAVE, 0}, {"PULSE WIDTH", F_PW, P_O2_PW, 0}, {"LEVEL", F_PCT, P_O2_LEVEL, 0}, {"FINE", F_C100, P_O2_CENTS, 0}}},
    {"MIX", {{"SUB", F_PCT, P_SUB_LEVEL, 0}, {"NOISE", F_PCT, P_NOISE_LEVEL, 0}, {"RING MOD", F_PCT, P_RING_LEVEL, 0}, {"OSC 2 SYNC", F_OPT, P_O2_SYNC, O_ONOFF}}},
    {"OSC 2 PITCH", {{"DEGREES", F_DEG, P_O2_DEG, 0}, NONE_P, NONE_P, NONE_P}},
};
static const bank_t B_FILT[] = {
    {"FILTER", {{"TYPE", F_OPT, P_FLT_MODE, O_FMODE}, {"CUTOFF", F_CUT, P_CUTOFF, 0}, {"RESONANCE", F_PCT, P_RESO, 0}, {"DRIVE", F_X8, P_DRIVE, 0}}},
    {"FILTER MOD", {{"ENV AMOUNT", F_OCT8, P_FLT_ENV, 0}, {"KEY TRACK", F_PCT, P_FLT_KEYTRK, 0}, {"VELOCITY", F_OCT4, P_FLT_VEL, 0}, {"HIGH-PASS", F_HPF, P_HPF, 0}}},
};
static const bank_t B_ENV[] = {
    {"AMP ENV", {{"ATTACK", F_TIME, P_E1_A, 0}, {"DECAY", F_TIME, P_E1_D, 0}, {"SUSTAIN", F_PCT, P_E1_S, 0}, {"RELEASE", F_TIME, P_E1_R, 0}}},
    {"FILTER ENV", {{"ATTACK", F_TIME, P_E2_A, 0}, {"DECAY", F_TIME, P_E2_D, 0}, {"SUSTAIN", F_PCT, P_E2_S, 0}, {"RELEASE", F_TIME, P_E2_R, 0}}},
    {"ENV 3", {{"ATTACK", F_TIME, P_E3_A, 0}, {"DECAY", F_TIME, P_E3_D, 0}, {"SUSTAIN", F_PCT, P_E3_S, 0}, {"RELEASE", F_TIME, P_E3_R, 0}}},
    {"CURVES", {{"AMP CURVE", F_CURVE, P_E1_CURVE, 0}, {"FILT CURVE", F_CURVE, P_E2_CURVE, 0}, {"ENV 3 CURVE", F_CURVE, P_E3_CURVE, 0}, {"VEL > LEVEL", F_PCT, P_VEL_AMP, 0}}},
};
static const bank_t B_LFO[] = {
    {"LFO 1", {{"SHAPE", F_OPT, P_L1_SHAPE, O_LSHAPE}, {"RATE", F_LFO, P_L1_RATE, 0}, {"DELAY", F_TIMEOFF, P_L1_DELAY, 0}, {"RUNS", F_OPT, P_L1_GLOBAL, O_LGLOB}}},
    {"LFO 2", {{"SHAPE", F_OPT, P_L2_SHAPE, O_LSHAPE}, {"RATE", F_LFO, P_L2_RATE, 0}, {"DELAY", F_TIMEOFF, P_L2_DELAY, 0}, {"RUNS", F_OPT, P_L2_GLOBAL, O_LGLOB}}},
    {"MOD 1", {{"SOURCE", F_OPT, P_M1_SRC, O_SRC}, {"DEST", F_OPT, P_M1_DST, O_DST}, {"AMOUNT", F_BIP, P_M1_AMT, 0}, NONE_P}},
    {"MOD 2", {{"SOURCE", F_OPT, P_M2_SRC, O_SRC}, {"DEST", F_OPT, P_M2_DST, O_DST}, {"AMOUNT", F_BIP, P_M2_AMT, 0}, NONE_P}},
    {"MOD 3", {{"SOURCE", F_OPT, P_M3_SRC, O_SRC}, {"DEST", F_OPT, P_M3_DST, O_DST}, {"AMOUNT", F_BIP, P_M3_AMT, 0}, NONE_P}},
    {"MOD 4", {{"SOURCE", F_OPT, P_M4_SRC, O_SRC}, {"DEST", F_OPT, P_M4_DST, O_DST}, {"AMOUNT", F_BIP, P_M4_AMT, 0}, NONE_P}},
};
static const bank_t B_VOICE[] = {
    {"VOICE", {{"MODE", F_OPT, P_VOICE_MODE, O_VMODE}, {"GLIDE", F_TIMEOFF, P_GLIDE, 0}, {"BEND RANGE", F_OPT, P_BEND_RANGE, O_BEND}, {"UNI VOICES", F_OPT, P_UNI_VOICES, O_UNI}}},
    {"OUTPUT", {{"VOLUME", F_PCT, P_VOLUME, 0}, {"PAN", F_PAN, P_PAN, 0}, {"REVERB SEND", F_PCT, P_REV_SEND, 0}, {"DELAY SEND", F_PCT, P_DLY_SEND, 0}}},
    {"CHARACTER", {{"DRIFT", F_C10, P_DRIFT, 0}, {"VARIANCE", F_PCT, P_VARIANCE, 0}, {"SATURATION", F_PCT, P_OUT_SAT, 0}, {"UNI DETUNE", F_C50, P_UNI_DETUNE, 0}}},
    {"UNISON", {{"UNI SPREAD", F_PCT, P_UNI_SPREAD, 0}, NONE_P, NONE_P, NONE_P}},
};
static const bank_t B_FX[] = {
    {"REVERB", {{"LEVEL", F_PCT, FX_REV_LEVEL, 0}, {"SIZE", F_DEC, FX_REV_SIZE, 0}, {"DAMPING", F_PCT, FX_REV_DAMP, 0}, {"PRE-DELAY", F_PRE, FX_REV_PRE, 0}}},
    {"REVERB 2", {{"BRIGHTNESS", F_PCT, FX_REV_BW, 0}, {"MOVEMENT", F_PCT, FX_REV_MOD, 0}, NONE_P, NONE_P}},
    {"DELAY", {{"LEVEL", F_PCT, FX_DLY_LEVEL, 0}, {"TIME", F_OPT, FX_DLY_SYNC, O_SYNC}, {"FEEDBACK", F_FB110, FX_DLY_FB, 0}, {"FREE TIME", F_DTIME, FX_DLY_TIME, 0}}},
    {"DELAY 2", {{"TONE", F_DTONE, FX_DLY_TONE, 0}, {"LOW CUT", F_DLC, FX_DLY_LOWCUT, 0}, {"PING-PONG", F_PCT, FX_DLY_PING, 0}, {"TO REVERB", F_PCT, FX_DLY_TO_REV, 0}}},
    {"DELAY 3", {{"TAPE WOW", F_PCT, FX_DLY_WOW, 0}, {"SATURATION", F_X4, FX_DLY_DRIVE, 0}, NONE_P, NONE_P}},
};
static const bank_t B_DRUM[] = {
    {"TONE", {{"TUNE", F_DTUNE, DP_TUNE, 0}, {"PITCH SWEEP", F_ST, DP_PITCH_AMT, 0}, {"SWEEP TIME", F_TIME, DP_PITCH_DEC, 0}, {"DECAY", F_TIME, DP_DECAY, 0}}},
    {"FM", {{"RATIO", F_OPT, DP_RATIO, O_RATIO}, {"AMOUNT", F_PCT, DP_FM, 0}, {"FM DECAY", F_TIME, DP_FM_DEC, 0}, {"FEEDBACK", F_PCT, DP_FEEDBACK, 0}}},
    {"NOISE", {{"NOISE", F_PCT, DP_NOISE, 0}, {"METAL", F_PCT, DP_METAL, 0}, {"NOISE DECAY", F_TIME, DP_NOISE_DEC, 0}, {"CLAP", F_OPT, DP_CLAP, O_CLAP}}},
    {"NOISE FILTER", {{"TYPE", F_OPT, DP_NOISE_MODE, O_NMODE}, {"CUTOFF", F_NCUT, DP_NOISE_CUT, 0}, {"RESONANCE", F_PCT, DP_NOISE_RESO, 0}, {"TONE LEVEL", F_PCT, DP_TONE, 0}}},
    {"OUTPUT", {{"LEVEL", F_PCT, DP_LEVEL, 0}, {"PAN", F_PAN, DP_PAN, 0}, {"REVERB SEND", F_PCT, DP_REV, 0}, {"DELAY SEND", F_PCT, DP_DLY, 0}}},
    {"MORE", {{"DRIVE", F_X8OFF, DP_DRIVE, 0}, {"CHOKE", F_OPT, DP_CHOKE, O_CHOKE}, NONE_P, NONE_P}},
};
#define NB(a) (uint8_t)(sizeof a / sizeof a[0])
static const page_t PAGES[] = {
    {"OSC", T_SYN, NB(B_OSC), BTN_EDIT, B_OSC},
    {"FILTER", T_SYN, NB(B_FILT), BTN_SEL, B_FILT},
    {"ENV", T_SYN, NB(B_ENV), BTN_ENV, B_ENV},
    {"LFO", T_SYN, NB(B_LFO), BTN_LFO, B_LFO},
    {"VOICE", T_SYN, NB(B_VOICE), BTN_GLO, B_VOICE},
    {"FX", T_FX, NB(B_FX), BTN_FX, B_FX},
    {"DRUMS", T_DRUM, NB(B_DRUM), BTN_ARP, B_DRUM},
};
#define NPAGES (sizeof PAGES / sizeof PAGES[0])

static int32_t pg = -1;                         /* the open page, -1 = the wheel */
static uint32_t pg_bank, pg_part, pg_drum;

static uint32_t p_get(const pdesc_t *d)
{
    uint32_t t = PAGES[pg].target;
    return t == T_SYN ? eng_get_param(pg_part, d->id) : t == T_DRUM ? eng_drum_get(pg_drum, d->id) : eng_fx_get(d->id);
}
static uint32_t p_max(const pdesc_t *d)
{
    uint32_t t = PAGES[pg].target;
    return t == T_SYN ? eng_param_max(d->id) : t == T_DRUM ? eng_drum_param_max(d->id) : eng_fx_param_max(d->id);
}
static void p_set(const pdesc_t *d, uint32_t v)
{
    uint32_t t = PAGES[pg].target;
    if (t == T_SYN) eng_set_param(pg_part, d->id, v);
    else if (t == T_DRUM) eng_drum_set(pg_drum, d->id, v);
    else eng_fx_set(d->id, v);
}

/* ---- numbers in their units, integers only */
static uint64_t exp2q(uint32_t x_q16)           /* 2^x, Q16 in and out; a quadratic for the fraction (~0.3 %) */
{
    uint32_t ip = x_q16 >> 16, f = x_q16 & 0xFFFFu;
    uint64_t m = 65536u + ((f * (43025u + ((22511u * f) >> 16))) >> 16);
    return ip > 40u ? 0 : m << ip;
}
static char *putsg(char *p, int32_t v) { if (v < 0) { *p++ = '-'; v = -v; } else if (v > 0) *p++ = '+'; return putn(p, (uint32_t)v); }
static char *putd(char *p, uint32_t v, uint32_t dec)    /* v / 10^dec with dec decimals */
{
    uint32_t div = dec == 1u ? 10u : dec == 2u ? 100u : 1u, f = v % div;
    p = putn(p, v / div);
    if (dec) { *p++ = '.'; if (dec == 2u && f < 10u) *p++ = '0'; p = putn(p, f); }
    return p;
}
static char *puthz(char *p, uint32_t cents)     /* 8.1758 Hz x 2^(cents / 1200) */
{
    uint32_t f100 = (uint32_t)((818u * exp2q((cents << 16) / 1200u)) >> 16);
    if (f100 >= 100000u) { p = putd(p, f100 / 1000u, 2); return put(p, " kHz"); }
    if (f100 >= 10000u) { p = putn(p, (f100 + 50u) / 100u); return put(p, " Hz"); }
    p = putd(p, f100 / 10u, 1); return put(p, " Hz");
}
static char *putms(char *p, uint32_t us)
{
    if (us < 10000u) { p = putd(p, us / 100u, 1); return put(p, " ms"); }
    if (us < 1000000u) { p = putn(p, (us + 500u) / 1000u); return put(p, " ms"); }
    p = putd(p, us / 10000u, 2); return put(p, " s");
}
static char *fmt_value(char *p, const pdesc_t *d, uint32_t r)
{
    int32_t b = (int32_t)r - 8192;
    switch (d->fmt) {
    case F_PCT: p = putn(p, (r * 100u + 8191u) / 16383u); *p++ = '%'; break;
    case F_BIP: p = putsg(p, b * 100 / 8192); *p++ = '%'; break;
    case F_PAN: if (b > -82 && b < 82) p = put(p, "CENTRE"); else { p = put(p, b < 0 ? "L " : "R "); p = putn(p, (uint32_t)(b < 0 ? -b : b) * 100u / 8192u); } break;
    case F_WAVE: {
        static const char *const W[4] = {"SINE", "TRI", "SAW", "PULSE"};
        uint32_t s = r * 3u / 16383u, f = (r * 3u % 16383u) * 100u / 16383u;
        if (s >= 3u) p = put(p, W[3]);
        else if (f < 3u) p = put(p, W[s]);
        else if (f > 97u) p = put(p, W[s + 1u]);
        else { p = put(p, W[s]); *p++ = '>'; p = put(p, W[s + 1u]); *p++ = ' '; p = putn(p, f); }
        break; }
    case F_PW: p = putn(p, 2u + r * 96u / 16383u); *p++ = '%'; break;
    case F_DEG: p = putsg(p, (int32_t)r - 24); break;
    case F_C100: p = putsg(p, b * 100 / 8192); p = put(p, " ct"); break;
    case F_X8: case F_X8OFF:
        if (d->fmt == F_X8OFF && !r) { p = put(p, "OFF"); break; }
        *p++ = 'x'; p = putd(p, 100u + r * 700u / 16383u, 2); break;
    case F_X4: *p++ = 'x'; p = putd(p, 100u + r * 300u / 16383u, 2); break;
    case F_CUT: p = puthz(p, r * 13700u / 16383u); break;
    case F_HPF: if (!r) p = put(p, "OFF"); else p = puthz(p, 1548u + r * 7976u / 16383u); break;
    case F_OCT8: case F_OCT4: {
        int32_t v = b * (d->fmt == F_OCT8 ? 800 : 400) / 8192;
        if (v < 0) { *p++ = '-'; v = -v; } else if (v > 0) *p++ = '+';
        p = putd(p, (uint32_t)v, 2); p = put(p, " oct"); break; }
    case F_TIME: case F_TIMEOFF: case F_CURVE:
        if (d->fmt == F_TIMEOFF && !r) { p = put(p, "OFF"); break; }
        if (d->fmt == F_CURVE) { if (!r) p = put(p, "EXP"); else { p = putn(p, r * 100u / 16383u); *p++ = '%'; } break; }
        p = putms(p, (uint32_t)((1000u * exp2q(r * 57155u / 1000u)) >> 16)); break;        /* 1 ms x 20000^(r/16383) */
    case F_LFO: p = putd(p, (uint32_t)((2u * exp2q(r * 43866u / 1000u)) >> 16), 2); p = put(p, " Hz"); break;   /* 0.02 .. 40 Hz */
    case F_C50: p = putd(p, r * 500u / 16383u, 1); p = put(p, " ct"); break;
    case F_C10: p = putd(p, r * 100u / 16383u, 1); p = put(p, " ct"); break;
    case F_DTUNE: p = puthz(p, 1548u + r * 9600u / 16383u); break;
    case F_ST: if (!r) p = put(p, "OFF"); else { *p++ = '+'; p = putd(p, r * 480u / 16383u, 1); p = put(p, " st"); } break;
    case F_NCUT: p = puthz(p, 4335u + r * 8795u / 16383u); break;
    case F_DEC: p = put(p, "0."); p = putn(p, 20u + (r * 78u + 8191u) / 16383u); break;
    case F_PRE: p = putn(p, r * 100u / 16383u); p = put(p, " ms"); break;
    case F_FB110: p = putn(p, (r * 110u + 8191u) / 16383u); *p++ = '%'; break;
    case F_DTIME: p = putms(p, (uint32_t)((10000u * exp2q(r * 27629u / 1000u)) >> 16)); break;   /* 10 ms x 120^(r/16383) */
    case F_DTONE: p = puthz(p, 3000u + r * 10000u / 16383u); break;
    case F_DLC: p = puthz(p, 2400u + r * 6000u / 16383u); break;
    case F_OPT: p = put(p, d->opts[r]); break;
    default: p = putn(p, r); break;
    }
    *p = 0;
    return p;
}

/* ---- the panel: 240 x 120 where the wheel is */
static void draw_panel(void)
{
    const page_t *P = &PAGES[pg];
    const bank_t *B = &P->banks[pg_bank];
    char t[48], *p = t;
    uint32_t k;
    cv_begin(240, 120, C_BLACK);
    if (P->target == T_SYN) { p = put(p, "SOUND "); p = putn(p, pg_part + 1u); *p++ = ' '; }
    else if (P->target == T_DRUM) { p = put(p, DRUM_NAME[pg_drum]); *p++ = ' '; }
    p = put(p, B->title); *p = 0;
    cv_text(6, 1, &FONT_S, t, M_RED);
    p = t; p = putn(p, pg_bank + 1u); *p++ = '/'; p = putn(p, P->nbanks); *p = 0;
    cv_text(234 - text_w(&FONT_S, t), 1, &FONT_S, t, M_DIM);
    for (k = 0; k < 4u; k++) {
        const pdesc_t *d = &B->p[k];
        int32_t y = 22 + (int32_t)k * 24;
        uint32_t v, mx, w;
        if (!d->name) continue;
        v = p_get(d); mx = p_max(d);
        p = t; *p++ = (char)('1' + k); *p++ = ' '; p = put(p, d->name); *p = 0;
        cv_text(6, y, &FONT_S, t, M_DIM);
        fmt_value(t, d, v);
        cv_text(234 - text_w(&FONT_S, t), y, &FONT_S, t, M_TXT);
        cv_rect(6, y + 18, 228, 2, RGB(48, 18, 18));
        w = mx ? (v * 228u) / mx : 0u;
        if (d->fmt == F_BIP || d->fmt == F_PAN || d->fmt == F_C100 || d->fmt == F_OCT8 || d->fmt == F_OCT4) {
            uint32_t c = 114u;                                          /* from the centre */
            if (w >= c) cv_rect(6 + (int32_t)c, y + 18, (int32_t)(w - c) + 1, 2, M_RED);
            else cv_rect(6 + (int32_t)w, y + 18, (int32_t)(c - w), 2, M_RED);
        } else if (w) cv_rect(6, y + 18, (int32_t)w, 2, M_RED);
    }
    cv_blit(0, 26);
}

/* the knobs on the open page: 1 = something changed */
static int pages_knobs(void)
{
    const bank_t *B = &PAGES[pg].banks[pg_bank];
    int changed = 0;
    uint32_t k;
    for (k = 0; k < 4u; k++) {
        const pdesc_t *d = &B->p[k];
        int32_t e = fm1_enc_take(ENC_KNOB1 + k), v, mx;
        if (!e || !d->name) continue;
        mx = (int32_t)p_max(d);
        v = (int32_t)p_get(d) + (mx > 255 ? e * (e > 2 || e < -2 ? 256 : 128) : e);   /* coarse when turned fast */
        p_set(d, (uint32_t)(v < 0 ? 0 : v > mx ? mx : v));
        changed = 1;
    }
    return changed;
}
/* a page button: opens its page, or steps to its next bank. 1 = the panel changed */
static int pages_button(uint32_t edges)
{
    uint32_t i;
    for (i = 0; i < NPAGES; i++)
        if (edges & (1u << PAGES[i].button)) {
            if (pg == (int32_t)i) pg_bank = (pg_bank + 1u) % PAGES[i].nbanks;
            else { pg = (int32_t)i; pg_bank = 0; }
            return 1;
        }
    return 0;
}
