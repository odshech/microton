/* seq.c — see seq.h. Time is counted in steps, Q32 (a step = 2^32), as 64-bit sums: adds and multiplies only. */
#include "seq.h"
#include "engine.h"

typedef uint64_t u64;

/* ------------------------------------------------------------------ data */

typedef struct {
    uint8_t on, nn, deg[SEQ_NOTES];
    int8_t per[SEQ_NOTES], cents;
    uint8_t vel, gate, prob, rat, slide, cond, lid[SEQ_LOCKS];
    uint16_t lval[SEQ_LOCKS];
} step_t;
typedef struct { uint8_t len, speed, dir, swing; int8_t transpose, base_per; uint8_t mute, solo, chan; } track_t;
typedef struct { track_t tr[SEQ_TRACKS]; step_t st[SEQ_TRACKS][SEQ_STEPS]; uint8_t len; int16_t scale; } pattern_t;
typedef struct {
    uint32_t magic, version;
    uint16_t tempo;
    uint8_t song_len, song_mode;
    uint8_t song_pat[SEQ_SONG], song_rep[SEQ_SONG];
    pattern_t pat[SEQ_PATTERNS];
} project_t;

static project_t PJ SEQ_POOL;
static pattern_t undo_pat SEQ_POOL, clip_pat SEQ_POOL;
static int32_t undo_p = -1;
static step_t clip_step, clip_page[16], clip_track[SEQ_STEPS];
static track_t clip_track_set;
static int clip_has_track;

static const step_t STEP0 = {0, 1, {0, 0, 0, 0}, {0, 0, 0, 0}, 0, 100, 8, 100, 1, 0, 0,
                             {SEQ_NO_LOCK, SEQ_NO_LOCK, SEQ_NO_LOCK, SEQ_NO_LOCK}, {0, 0, 0, 0}};
static void track_default(track_t *t, uint32_t i)
{
    t->len = 16; t->speed = 1; t->dir = 0; t->swing = 50; t->transpose = 0; t->base_per = 0;
    t->mute = 0; t->solo = 0; t->chan = i == TR_EXT ? 1 : 10;
}
static void pattern_default(pattern_t *p)
{
    uint32_t t, s;
    for (t = 0; t < SEQ_TRACKS; t++) {
        track_default(&p->tr[t], t);
        for (s = 0; s < SEQ_STEPS; s++) p->st[t][s] = STEP0;
    }
    p->len = 16;
    p->scale = -1;
}

/* ------------------------------------------------------------------ playing state */

#define SLIDE_GLIDE 7250u                     /* the glide a slide locks: about 80 ms */
#define QN 48u
enum { E_ON, E_OFF };
typedef struct { u64 t; int32_t deg, vel, fine; uint8_t type, chan; } tev_t;
typedef struct {
    u64 next;                                 /* track time of the next step boundary */
    uint32_t done;                            /* steps fired since the pattern started */
    int32_t pos, pp, cycles;                  /* position, ping-pong direction, completed passes */
    tev_t q[QN];
    uint32_t nq;
    int32_t sound[16]; uint32_t nsound;       /* what is sounding, for a clean stop */
    int32_t held[SEQ_NOTES]; uint32_t nheld;  /* tied or sliding into the next step */
    int slide_in, locked;
} trs_t;

static const uint32_t SPEED_Q8[6] = {512, 256, 128, 64, 341, 171};
static const u64 SUB_Q32[5] = {0, 4294967296ull, 2147483648ull, 1431655765ull, 1073741824ull};   /* a step / hits */
static struct {
    volatile int play_req, stop_req, cont_req, playing;
    u64 T, start;                             /* master time (x1 steps, Q32) and the pattern's start */
    uint32_t mcount;                          /* master steps counted since the pattern started */
    uint32_t inc;
    volatile uint32_t cur;
    volatile int32_t queued, scale_req;
    uint32_t row, rep;
    trs_t ts[SEQ_TRACKS];
    volatile int32_t pos_out[SEQ_TRACKS];
} RT;

static uint32_t seq_rng = 0x2545F491u;
static uint32_t seq_rnd(void) { seq_rng ^= seq_rng << 13; seq_rng ^= seq_rng >> 17; seq_rng ^= seq_rng << 5; return seq_rng; }

/* MIDI out: the audio interrupt writes head, the main loop tail */
static uint8_t seq_mq[256][3];
static volatile uint32_t seq_mq_head, seq_mq_tail;
static void midi_out(uint8_t s, uint8_t a, uint8_t b)
{
    uint32_t h = seq_mq_head, nh = (h + 1u) & 255u;
    if (nh == seq_mq_tail) return;
    seq_mq[h][0] = s; seq_mq[h][1] = a; seq_mq[h][2] = b;
    seq_mq_head = nh;
}
int seq_midi_get(uint8_t *s, uint8_t *a, uint8_t *b)
{
    uint32_t t = seq_mq_tail;
    if (t == seq_mq_head) return 0;
    *s = seq_mq[t][0]; *a = seq_mq[t][1]; *b = seq_mq[t][2];
    seq_mq_tail = (t + 1u) & 255u;
    return 1;
}

/* one note, to wherever the track goes */
static int32_t midi_note(int32_t abs) { int32_t n = 60 + abs; return n < 0 ? 0 : n > 127 ? 127 : n; }
static void note(uint32_t t, uint32_t on, int32_t deg, int32_t vel, int32_t fine, uint8_t chan)
{
    trs_t *r = &RT.ts[t];
    uint32_t i;
    if (on) {
        if (r->nsound < 16u) r->sound[r->nsound++] = deg;
    } else {
        for (i = 0; i < r->nsound; i++) if (r->sound[i] == deg) { r->sound[i] = r->sound[--r->nsound]; break; }
    }
    if (t <= TR_SYN3) {
        if (on) eng_rt_on(t, deg, vel, fine); else eng_rt_off(t, deg);
    } else if (t == TR_EXT) {
        midi_out((uint8_t)((on ? 0x90 : 0x80) | ((chan - 1u) & 15u)), (uint8_t)midi_note(deg), on ? (uint8_t)vel : 0);
    } else {                                                             /* drums: the engine's, and MIDI too */
        if (on && deg >= 0 && deg < (int32_t)ENG_DRUMS) eng_rt_drum((uint32_t)deg, vel);
        midi_out((uint8_t)((on ? 0x90 : 0x80) | ((chan - 1u) & 15u)), (uint8_t)(36 + deg), on ? (uint8_t)vel : 0);
    }
}
static void enqueue(trs_t *r, u64 t, uint32_t type, int32_t deg, int32_t vel, int32_t fine, uint8_t chan)
{
    if (r->nq >= QN) return;
    r->q[r->nq].t = t; r->q[r->nq].type = (uint8_t)type; r->q[r->nq].deg = deg;
    r->q[r->nq].vel = vel; r->q[r->nq].fine = fine; r->q[r->nq].chan = chan;
    r->nq++;
}
static void track_silence(uint32_t t)
{
    trs_t *r = &RT.ts[t];
    track_t *tr = &PJ.pat[RT.cur].tr[t];
    while (r->nsound) note(t, 0, r->sound[r->nsound - 1], 0, 0, tr->chan);
    r->nq = 0; r->nheld = 0; r->slide_in = 0;
    if (t <= TR_SYN3 && r->locked) { eng_rt_locks_clear(t); r->locked = 0; }
}
static void tracks_restart(void)
{
    uint32_t t;
    for (t = 0; t < SEQ_TRACKS; t++) {
        trs_t *r = &RT.ts[t];
        track_silence(t);
        r->next = 0; r->done = 0; r->pos = -1; r->pp = 1; r->cycles = 0;
        RT.pos_out[t] = -1;
    }
}

static int any_solo(const pattern_t *p)
{
    uint32_t t;
    for (t = 0; t < SEQ_TRACKS; t++) if (p->tr[t].solo) return 1;
    return 0;
}

/* a step boundary at track time tb: work out the position, then schedule the step's notes */
static void fire(uint32_t t, u64 tb)
{
    pattern_t *p = &PJ.pat[RT.cur];
    track_t *tr = &p->tr[t];
    trs_t *r = &RT.ts[t];
    int32_t len = tr->len ? tr->len : 1, n = (int32_t)eng_scale_degrees(), i, nn, plays;
    const step_t *st;
    u64 ton, sub;
    uint32_t k, hold, rat;
    int32_t abs[SEQ_NOTES];
    (void)0;

    /* position */
    if (r->done == 0) r->pos = tr->dir == 1 ? len - 1 : 0;
    else switch (tr->dir) {
        case 1: if (--r->pos < 0) { r->pos = len - 1; r->cycles++; } break;
        case 2:
            if (len == 1) { r->pos = 0; r->cycles++; break; }
            r->pos += r->pp;
            if (r->pos >= len) { r->pp = -1; r->pos = len - 2; }
            else if (r->pos < 0) { r->pp = 1; r->pos = 1; r->cycles++; }
            break;
        case 3: r->pos = (int32_t)(seq_rnd() % (uint32_t)len); if (r->done % (uint32_t)len == 0) r->cycles++; break;
        default: if (++r->pos >= len) { r->pos = 0; r->cycles++; } break;
    }
    if (r->pos >= len) r->pos = 0;
    r->done++;
    RT.pos_out[t] = r->pos;
    st = &p->st[t][r->pos];

    /* does it play: on, not muted, the solo, the chance, the condition */
    plays = st->on && !tr->mute && (!any_solo(p) || tr->solo);
    if (plays && st->prob < 100 && seq_rnd() % 100u >= st->prob) plays = 0;
    if (plays && st->cond) {
        uint32_t a = st->cond >> 4, b = st->cond & 15u;
        if (b >= 2u && a >= 1u && (uint32_t)r->cycles % b != a - 1u) plays = 0;
    }

    /* swing: the odd steps of a pair start late */
    ton = tb + ((r->pos & 1) && tr->swing > 50 ? (u64)(tr->swing - 50) * 85899346u : 0u);

    /* locks: this step's, or none (and a slide's glide into this step) */
    if (t <= TR_SYN3) {
        if (r->locked) { eng_rt_locks_clear(t); r->locked = 0; }
        if (plays) for (k = 0; k < SEQ_LOCKS; k++)
            if (st->lid[k] != SEQ_NO_LOCK) { eng_rt_lock(t, st->lid[k], st->lval[k]); r->locked = 1; }
        if (plays && r->slide_in) { eng_rt_lock(t, P_GLIDE, SLIDE_GLIDE); r->locked = 1; }
    }
    r->slide_in = 0;

    nn = 0;
    if (plays) {
        if (t >= TR_DRUM_A) {
            for (i = 0; i < 4; i++) if (st->deg[0] & (1u << i)) abs[nn++] = i + (t == TR_DRUM_B ? 4 : 0);
        } else {
            for (i = 0; i < (int32_t)st->nn && i < (int32_t)SEQ_NOTES; i++)
                abs[nn++] = (tr->base_per + st->per[i]) * n + st->deg[i] + tr->transpose;
        }
    }

    /* the held notes of the step before (a tie or a slide): one this step plays again simply continues,
     * the rest end as this step starts, after its new notes (so a mono part slides, legato) */
    {
        int keep[SEQ_NOTES] = {0, 0, 0, 0};
        rat = st->rat < 1u ? 1u : st->rat > 4u ? 4u : st->rat;
        for (k = 0; k < r->nheld; k++) {
            int kept = 0;
            for (i = 0; i < nn; i++) if (!keep[i] && abs[i] == r->held[k] && rat == 1u) { keep[i] = 1; kept = 1; break; }
            if (!kept) enqueue(r, ton, E_OFF, r->held[k], 0, 0, tr->chan);
        }
        r->nheld = 0;
        if (!plays) return;

        hold = st->gate >= 17u || st->slide;
        sub = SUB_Q32[rat];
        for (k = 0; k < rat; k++) {
            u64 t0 = ton + sub * k, toff = t0 + ((sub * (st->gate > 16u ? 16u : st->gate)) >> 4) - 1u;
            for (i = 0; i < nn; i++) {
                if (!(k == 0 && keep[i])) enqueue(r, t0, E_ON, abs[i], st->vel, st->cents * 16, tr->chan);
                if (!(hold && k == rat - 1u)) enqueue(r, toff, E_OFF, abs[i], 0, 0, tr->chan);
            }
        }
        if (hold) {
            for (i = 0; i < nn && r->nheld < SEQ_NOTES; i++) r->held[r->nheld++] = abs[i];
            r->slide_in = st->slide;
        }
    }
}

static void run_queue(uint32_t t, u64 now)
{
    trs_t *r = &RT.ts[t];
    uint32_t pass, i;
    for (pass = 0; pass < 2u; pass++) {                                  /* at the same time: on before off */
        for (i = 0; i < r->nq;) {
            tev_t *e = &r->q[i];
            if (e->t <= now && e->type == (pass ? E_OFF : E_ON)) {
                note(t, e->type == E_ON, e->deg, e->vel, e->fine, e->chan);
                r->q[i] = r->q[--r->nq];
            } else i++;
        }
    }
}

/* the end of a master cycle: the queued pattern, or the song's next row */
static void cycle_end(u64 boundary)
{
    uint32_t next = RT.cur;
    if (PJ.song_mode && PJ.song_len) {
        if (++RT.rep >= (PJ.song_rep[RT.row] ? PJ.song_rep[RT.row] : 1u)) {
            RT.rep = 0;
            RT.row = (RT.row + 1u) % PJ.song_len;
            next = PJ.song_pat[RT.row] % SEQ_PATTERNS;
        }
    } else if (RT.queued >= 0) { next = (uint32_t)RT.queued; RT.queued = -1; }
    if (next != RT.cur) {
        tracks_restart();
        RT.cur = next;
        RT.start = boundary;
        RT.mcount = 0;
        if (PJ.pat[next].scale >= 0) RT.scale_req = PJ.pat[next].scale;
    }
}

static void seq_hook(uint32_t frames)
{
    uint32_t t;
    u64 ms;
    (void)frames;
    if (RT.stop_req) {
        RT.stop_req = 0; RT.playing = 0;
        for (t = 0; t < SEQ_TRACKS; t++) { track_silence(t); RT.pos_out[t] = -1; }
    }
    if (RT.play_req) {
        RT.play_req = 0;
        if (!RT.cont_req) {
            tracks_restart();
            RT.T = RT.start = 0; RT.mcount = 0; RT.rep = 0;
            if (PJ.song_mode && PJ.song_len) { RT.row = 0; RT.cur = PJ.song_pat[0] % SEQ_PATTERNS; }
            if (PJ.pat[RT.cur].scale >= 0) RT.scale_req = PJ.pat[RT.cur].scale;
            RT.T = (u64)0 - (u64)RT.inc;                                 /* so that the first block fires step 0 at 0 */
            RT.start = RT.T + RT.inc;
        }
        RT.cont_req = 0;
        RT.playing = 1;
    }
    if (!RT.playing) return;

    RT.T += RT.inc;
    /* master cycles: count x1 steps since the pattern started; at each multiple of the length, a cycle ends */
    ms = (RT.T - RT.start) >> 32;
    while ((u64)RT.mcount < ms) {
        uint32_t len = PJ.pat[RT.cur].len ? PJ.pat[RT.cur].len : 16u;
        RT.mcount++;
        if (RT.mcount % len == 0) {
            u64 b = RT.start + ((u64)RT.mcount << 32);
            uint32_t was = RT.cur;
            cycle_end(b);
            if (RT.cur != was) break;                                    /* a new pattern starts at b */
        }
    }
    for (t = 0; t < SEQ_TRACKS; t++) {
        trs_t *r = &RT.ts[t];
        const track_t *tr = &PJ.pat[RT.cur].tr[t];
        u64 now = ((RT.T - RT.start) * SPEED_Q8[tr->speed < 6 ? tr->speed : 1]) >> 8;
        if (RT.T < RT.start) continue;
        while (now >= r->next) { fire(t, r->next); r->next += (u64)1 << 32; }
        run_queue(t, now);
    }
}

/* ------------------------------------------------------------------ transport */

void seq_init(void)
{
    uint32_t p;
    PJ.magic = 0x51455331u; PJ.version = 1u;                            /* "1SEQ" */
    PJ.tempo = 12000;
    PJ.song_len = 1; PJ.song_mode = 0;
    for (p = 0; p < SEQ_SONG; p++) { PJ.song_pat[p] = 0; PJ.song_rep[p] = 1; }
    for (p = 0; p < SEQ_PATTERNS; p++) pattern_default(&PJ.pat[p]);
    RT.playing = RT.play_req = RT.stop_req = RT.cont_req = 0;
    RT.cur = 0; RT.queued = -1; RT.scale_req = -1;
    seq_set_tempo(PJ.tempo);
    tracks_restart();
    undo_p = -1;
    eng_rt_hook = seq_hook;
}
void seq_play(void) { RT.cont_req = 0; RT.play_req = 1; }
void seq_continue(void) { RT.cont_req = 1; RT.play_req = 1; }
void seq_stop(void) { RT.stop_req = 1; }
int seq_playing(void) { return RT.playing || RT.play_req; }
void seq_set_tempo(uint32_t bpm)
{
    if (bpm < 3000u) bpm = 3000u;
    if (bpm > 30000u) bpm = 30000u;
    PJ.tempo = (uint16_t)bpm;
    RT.inc = (uint32_t)(((u64)bpm * 265944u) >> 8);                     /* Q32 x1 steps per block of 16 frames */
    eng_fx_tempo(bpm);                                                   /* the synced delay */
}
uint32_t seq_tempo(void) { return PJ.tempo; }
void seq_queue_pattern(uint32_t p)
{
    if (p >= SEQ_PATTERNS) return;
    if (!seq_playing()) { RT.cur = p; RT.queued = -1; }
    else RT.queued = (int32_t)p;
}
uint32_t seq_pattern_playing(void) { return RT.cur; }
int32_t seq_pattern_queued(void) { return RT.queued; }
void seq_song_mode(int on) { PJ.song_mode = on ? 1u : 0u; }
int seq_song_on(void) { return PJ.song_mode; }
void seq_song_set(uint32_t row, uint32_t pattern, uint32_t rep)
{
    if (row >= SEQ_SONG) return;
    PJ.song_pat[row] = (uint8_t)(pattern % SEQ_PATTERNS);
    PJ.song_rep[row] = (uint8_t)(rep < 1u ? 1u : rep > 64u ? 64u : rep);
    if (row >= PJ.song_len) PJ.song_len = (uint8_t)(row + 1u);
}
uint32_t seq_song_get(uint32_t row, uint32_t what) { return row >= SEQ_SONG ? 0u : what ? PJ.song_rep[row] : PJ.song_pat[row]; }
void seq_song_length(uint32_t rows) { PJ.song_len = (uint8_t)(rows < 1u ? 1u : rows > SEQ_SONG ? SEQ_SONG : rows); }
uint32_t seq_song_len(void) { return PJ.song_len; }
uint32_t seq_song_row(void) { return RT.row; }
int32_t seq_track_pos(uint32_t t) { return t < SEQ_TRACKS && RT.playing ? RT.pos_out[t] : -1; }
int32_t seq_scale_request(void) { int32_t r = RT.scale_req; RT.scale_req = -1; return r; }

/* ------------------------------------------------------------------ data access, clamped */

static int32_t clampi(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }
static step_t *step_at(uint32_t p, uint32_t t, uint32_t s)
{
    return p < SEQ_PATTERNS && t < SEQ_TRACKS && s < SEQ_STEPS ? &PJ.pat[p].st[t][s] : 0;
}
int32_t seq_step_get(uint32_t p, uint32_t t, uint32_t s, uint32_t f)
{
    const step_t *st = step_at(p, t, s);
    if (!st) return 0;
    switch (f) {
    case SF_ON: return st->on;
    case SF_NOTES: return st->nn;
    case SF_CENTS: return st->cents;
    case SF_VEL: return st->vel;
    case SF_GATE: return st->gate;
    case SF_PROB: return st->prob;
    case SF_RATCHET: return st->rat;
    case SF_SLIDE: return st->slide;
    case SF_COND: return st->cond;
    default:
        if (f >= SF_DEG0 && f <= SF_DEG3) return st->deg[f - SF_DEG0];
        if (f >= SF_PER0 && f <= SF_PER3) return st->per[f - SF_PER0];
        if (f >= SF_LOCK_ID0 && f <= SF_LOCK_ID3) return st->lid[f - SF_LOCK_ID0];
        if (f >= SF_LOCK_VAL0 && f <= SF_LOCK_VAL3) return st->lval[f - SF_LOCK_VAL0];
        return 0;
    }
}
void seq_step_set(uint32_t p, uint32_t t, uint32_t s, uint32_t f, int32_t v)
{
    step_t *st = step_at(p, t, s);
    if (!st) return;
    switch (f) {
    case SF_ON: st->on = v ? 1u : 0u; break;
    case SF_NOTES: st->nn = (uint8_t)clampi(v, t >= TR_DRUM_A ? 0 : 1, t >= TR_DRUM_A ? 15 : (int32_t)SEQ_NOTES); break;
    case SF_CENTS: st->cents = (int8_t)clampi(v, -50, 50); break;
    case SF_VEL: st->vel = (uint8_t)clampi(v, 1, 127); break;
    case SF_GATE: st->gate = (uint8_t)clampi(v, 1, 17); break;
    case SF_PROB: st->prob = (uint8_t)clampi(v, 0, 100); break;
    case SF_RATCHET: st->rat = (uint8_t)clampi(v, 1, 4); break;
    case SF_SLIDE: st->slide = v ? 1u : 0u; break;
    case SF_COND: {
        uint32_t a = (uint32_t)v >> 4 & 15u, b = (uint32_t)v & 15u;
        st->cond = v == 0 || b < 2u || b > 8u || a < 1u || a > b ? 0u : (uint8_t)v;
        break;
    }
    default:
        if (f >= SF_DEG0 && f <= SF_DEG3) st->deg[f - SF_DEG0] = (uint8_t)clampi(v, 0, 254);
        else if (f >= SF_PER0 && f <= SF_PER3) st->per[f - SF_PER0] = (int8_t)clampi(v, -3, 3);
        else if (f >= SF_LOCK_ID0 && f <= SF_LOCK_ID3) st->lid[f - SF_LOCK_ID0] = (uint8_t)(v < 0 || v >= (int32_t)P_COUNT ? (int32_t)SEQ_NO_LOCK : v);
        else if (f >= SF_LOCK_VAL0 && f <= SF_LOCK_VAL3) st->lval[f - SF_LOCK_VAL0] = (uint16_t)clampi(v, 0, 16383);
        break;
    }
}
int seq_step_lock(uint32_t p, uint32_t t, uint32_t s, uint32_t id, uint32_t raw)
{
    step_t *st = step_at(p, t, s);
    uint32_t k, free = SEQ_LOCKS;
    if (!st || id >= P_COUNT) return -1;
    for (k = 0; k < SEQ_LOCKS; k++) {
        if (st->lid[k] == id) { st->lval[k] = (uint16_t)(raw > 16383u ? 16383u : raw); return 0; }
        if (st->lid[k] == SEQ_NO_LOCK && free == SEQ_LOCKS) free = k;
    }
    if (free == SEQ_LOCKS) return -1;                                    /* four locks already */
    st->lval[free] = (uint16_t)(raw > 16383u ? 16383u : raw);
    st->lid[free] = (uint8_t)id;
    return 0;
}
void seq_step_unlock(uint32_t p, uint32_t t, uint32_t s, uint32_t id)
{
    step_t *st = step_at(p, t, s);
    uint32_t k;
    if (st) for (k = 0; k < SEQ_LOCKS; k++) if (st->lid[k] == id) st->lid[k] = SEQ_NO_LOCK;
}
int32_t seq_track_get(uint32_t p, uint32_t t, uint32_t f)
{
    const track_t *tr;
    if (p >= SEQ_PATTERNS || t >= SEQ_TRACKS) return 0;
    tr = &PJ.pat[p].tr[t];
    switch (f) {
    case TF_LEN: return tr->len; case TF_SPEED: return tr->speed; case TF_DIR: return tr->dir;
    case TF_SWING: return tr->swing; case TF_TRANSPOSE: return tr->transpose; case TF_BASE_PER: return tr->base_per;
    case TF_MUTE: return tr->mute; case TF_SOLO: return tr->solo; case TF_CHANNEL: return tr->chan;
    default: return 0;
    }
}
void seq_track_set(uint32_t p, uint32_t t, uint32_t f, int32_t v)
{
    track_t *tr;
    if (p >= SEQ_PATTERNS || t >= SEQ_TRACKS) return;
    tr = &PJ.pat[p].tr[t];
    switch (f) {
    case TF_LEN: tr->len = (uint8_t)clampi(v, 1, SEQ_STEPS); break;
    case TF_SPEED: tr->speed = (uint8_t)clampi(v, 0, 5); break;
    case TF_DIR: tr->dir = (uint8_t)clampi(v, 0, 3); break;
    case TF_SWING: tr->swing = (uint8_t)clampi(v, 50, 75); break;
    case TF_TRANSPOSE: tr->transpose = (int8_t)clampi(v, -64, 64); break;
    case TF_BASE_PER: tr->base_per = (int8_t)clampi(v, -3, 3); break;
    case TF_MUTE: tr->mute = v ? 1u : 0u; break;
    case TF_SOLO: tr->solo = v ? 1u : 0u; break;
    case TF_CHANNEL: tr->chan = (uint8_t)clampi(v, 1, 16); break;
    default: break;
    }
}
int32_t seq_pattern_get(uint32_t p, uint32_t f)
{
    if (p >= SEQ_PATTERNS) return 0;
    return f == PF_LEN ? PJ.pat[p].len : f == PF_SCALE ? PJ.pat[p].scale : 0;
}
void seq_pattern_set(uint32_t p, uint32_t f, int32_t v)
{
    if (p >= SEQ_PATTERNS) return;
    if (f == PF_LEN) PJ.pat[p].len = (uint8_t)clampi(v, 1, SEQ_STEPS);
    else if (f == PF_SCALE) PJ.pat[p].scale = (int16_t)clampi(v, -1, 32767);
}

/* ------------------------------------------------------------------ editing */

static void abs_to_step(step_t *st, uint32_t i, int32_t abs, int32_t n, int32_t base_per)
{
    int32_t per = abs >= 0 ? abs / n : -((-abs + n - 1) / n);
    st->deg[i] = (uint8_t)(abs - per * n);
    st->per[i] = (int8_t)clampi(per - base_per, -3, 3);
}

int seq_op(uint32_t op, uint32_t p, uint32_t t, int32_t s, int32_t a, int32_t b, int32_t c)
{
    pattern_t *pt;
    track_t *tr;
    step_t *row;
    int32_t len, i, n = (int32_t)eng_scale_degrees();
    if (p >= SEQ_PATTERNS) return -1;
    if (op == OP_UNDO) {
        pattern_t tmp;
        if (undo_p < 0) return -1;
        tmp = PJ.pat[undo_p]; PJ.pat[undo_p] = undo_pat; undo_pat = tmp;   /* undo twice = redo */
        return 0;
    }
    pt = &PJ.pat[p];
    if (op == OP_COPY_PATTERN) { clip_pat = *pt; return 0; }
    if (t >= SEQ_TRACKS) return -1;
    tr = &pt->tr[t];
    row = pt->st[t];
    len = tr->len;
    if (op == OP_COPY_STEP) { if (s < 0 || s >= (int32_t)SEQ_STEPS) return -1; clip_step = row[s]; return 0; }
    if (op == OP_COPY_PAGE) { if (a < 0 || a > 3) return -1; for (i = 0; i < 16; i++) clip_page[i] = row[a * 16 + i]; return 0; }
    if (op == OP_COPY_TRACK) { for (i = 0; i < (int32_t)SEQ_STEPS; i++) clip_track[i] = row[i]; clip_track_set = *tr; clip_has_track = 1; return 0; }

    undo_pat = *pt; undo_p = (int32_t)p;                                 /* everything below changes the pattern */
    switch (op) {
    case OP_CLEAR_STEP: if (s < 0 || s >= (int32_t)SEQ_STEPS) return -1; row[s] = STEP0; break;
    case OP_CLEAR_TRACK: for (i = 0; i < (int32_t)SEQ_STEPS; i++) row[i] = STEP0; break;
    case OP_CLEAR_PATTERN: pattern_default(pt); break;
    case OP_PASTE_STEP: if (s < 0 || s >= (int32_t)SEQ_STEPS) return -1; row[s] = clip_step; break;
    case OP_PASTE_PAGE: if (a < 0 || a > 3) return -1; for (i = 0; i < 16; i++) row[a * 16 + i] = clip_page[i]; break;
    case OP_PASTE_TRACK:
        if (!clip_has_track) return -1;
        for (i = 0; i < (int32_t)SEQ_STEPS; i++) row[i] = clip_track[i];
        { uint8_t ch = tr->chan; *tr = clip_track_set; if (t >= TR_EXT) tr->chan = ch; }
        break;
    case OP_PASTE_PATTERN: *pt = clip_pat; break;
    case OP_SHIFT: {
        step_t tmp;
        if (len < 2) break;
        if (a > 0) { tmp = row[len - 1]; for (i = len - 1; i > 0; i--) row[i] = row[i - 1]; row[0] = tmp; }
        else { tmp = row[0]; for (i = 0; i < len - 1; i++) row[i] = row[i + 1]; row[len - 1] = tmp; }
        break;
    }
    case OP_REVERSE: for (i = 0; i < len / 2; i++) { step_t tmp = row[i]; row[i] = row[len - 1 - i]; row[len - 1 - i] = tmp; } break;
    case OP_DOUBLE:
        if (len * 2 > (int32_t)SEQ_STEPS) return -1;
        for (i = 0; i < len; i++) row[len + i] = row[i];
        tr->len = (uint8_t)(len * 2);
        break;
    case OP_EUCLID: {
        int32_t hits = clampi(a, 0, len), rot = b;
        for (i = 0; i < len; i++) {
            int32_t k = ((i + rot) % len + len) % len;
            row[i] = STEP0;
            if (hits && (k * hits) % len < hits) {
                row[i].on = 1;
                if (t >= TR_DRUM_A) { row[i].nn = (uint8_t)(c & 15); row[i].deg[0] = (uint8_t)(c & 15); }
                else abs_to_step(&row[i], 0, c, n, tr->base_per);
            }
        }
        break;
    }
    case OP_RANDOM: {
        int32_t lo = b < c ? b : c, hi = b < c ? c : b, span = hi - lo + 1;
        for (i = 0; i < len; i++) {
            row[i] = STEP0;
            if ((int32_t)(seq_rnd() % 100u) < clampi(a, 0, 100)) {
                row[i].on = 1;
                if (t >= TR_DRUM_A) { uint32_t m = 1u + seq_rnd() % 15u; row[i].nn = (uint8_t)m; row[i].deg[0] = (uint8_t)m; }
                else abs_to_step(&row[i], 0, lo + (int32_t)(seq_rnd() % (uint32_t)span), n, tr->base_per);
                row[i].vel = (uint8_t)(70 + seq_rnd() % 58u);
            }
        }
        break;
    }
    default: return -1;
    }
    return 0;
}

uint8_t *seq_project_bytes(void) { return (uint8_t *)&PJ; }
uint32_t seq_project_size(void) { return sizeof PJ; }
