/* SPDX-License-Identifier: GPL-3.0-only */
/* seqpage.h — the sequencer on the FM-1, included by dinghy.c after pages.h. Elektron-style step entry:
 *
 *   SEQ             opens it (again: closes); HOME closes it too
 *   white keys      the 16 steps of the page: a press turns a step on, a press on a lit step turns it off
 *   hold a step     the knobs edit it: note (scale degree), velocity, length, probability;
 *                   ALGORITHM switches to cents, ratchet, slide, condition (drums: velocity, probability, ratchet,
 *                   condition)
 *   no step held    the knobs edit the track: length, speed, swing, transpose; ALGORITHM: pattern, pattern length,
 *                   direction, mute
 *   PRESETS         the track: sound 1..3, external MIDI, drums A (kick snare hats), drums B (clap toms rim)
 *   OCT- OCT+       the page of 16 steps (1-16 .. 49-64)
 *   black keys 1..4 on a drum track: which drum the step keys write
 *
 * New steps of a melodic track get the last note set here (KNOB 1 on a held step). */

static const char *const TRACK_NAME[SEQ_TRACKS] = {"SOUND 1", "SOUND 2", "SOUND 3", "MIDI OUT", "DRUMS A", "DRUMS B"};
static const char *const SPEED_NAME[6] = {"x2", "x1", "x1/2", "x1/4", "x4/3", "x2/3"};
static const char *const DIR_NAME[4] = {"FWD", "BACK", "PING", "RAND"};
static const char *const DRUMV_NAME[2][4] = {{"KICK", "SNARE", "CL HAT", "OP HAT"}, {"CLAP", "LO TOM", "HI TOM", "RIM"}};

static int sq_open;
static uint32_t sq_track, sq_page, sq_bank, sq_voice;
static int32_t sq_held = -1, sq_was_on, sq_edited, sq_deg = 0;
static int32_t sq_pos_drawn = -2;

static int sq_drum(void) { return sq_track >= TR_DRUM_A; }
static uint32_t sq_pat(void) { return seq_pattern_playing(); }
static int32_t sg(int32_t s, uint32_t f) { return seq_step_get(sq_pat(), sq_track, (uint32_t)s, f); }
static void ss(int32_t s, uint32_t f, int32_t v) { seq_step_set(sq_pat(), sq_track, (uint32_t)s, f, v); }

/* the condition's choices: always, then "a of b" for b = 2..8 */
static int32_t cond_value(uint32_t i)
{
    uint32_t b, a;
    if (i == 0) return 0;
    for (b = 2; b <= 8u; b++) for (a = 1; a <= b; a++) if (--i == 0) return (int32_t)((a << 4) | b);
    return 0;
}
static uint32_t cond_index(int32_t v)
{
    uint32_t i;
    for (i = 0; i < 36u; i++) if (cond_value(i) == v) return i;
    return 0;
}

/* a step is on for the current drum (drum tracks) or at all (melodic) */
static int step_lit(int32_t s)
{
    if (!sg(s, SF_ON)) return 0;
    return sq_drum() ? (sg(s, SF_DEG0) & (1 << sq_voice)) != 0 : 1;
}
static void step_on(int32_t s)
{
    uint32_t n = tun_degrees();
    if (sq_drum()) {
        int32_t m = (sg(s, SF_ON) ? sg(s, SF_DEG0) : 0) | (1 << sq_voice);
        ss(s, SF_NOTES, m); ss(s, SF_DEG0, m); ss(s, SF_ON, 1);
    } else {
        int32_t per = sq_deg >= 0 ? sq_deg / (int32_t)n : -((-sq_deg + (int32_t)n - 1) / (int32_t)n);
        ss(s, SF_NOTES, 1); ss(s, SF_DEG0, sq_deg - per * (int32_t)n); ss(s, SF_PER0, per); ss(s, SF_ON, 1);
    }
}
static void step_off(int32_t s)
{
    if (sq_drum()) {
        int32_t m = sg(s, SF_DEG0) & ~(1 << sq_voice);
        ss(s, SF_NOTES, m); ss(s, SF_DEG0, m);
        if (!m) ss(s, SF_ON, 0);
    } else ss(s, SF_ON, 0);
}
static int32_t step_note(int32_t s) { return sg(s, SF_PER0) * (int32_t)tun_degrees() + sg(s, SF_DEG0); }

/* a white key on the sequencer page: press and release */
static void sq_key(uint32_t w, int down)
{
    int32_t s = (int32_t)(sq_page * 16u + w);
    if (down) {
        sq_held = s; sq_edited = 0; sq_was_on = step_lit(s);
        if (!sq_was_on) step_on(s);
    } else if (s == sq_held) {
        if (sq_was_on && !sq_edited) step_off(s);
        sq_held = -1;
    }
}

/* the knobs: a held step, or the track. 1 = changed */
static int sq_knobs(void)
{
    int32_t e[4], k, any = 0;
    for (k = 0; k < 4; k++) { e[k] = fm1_enc_take(ENC_KNOB1 + (uint32_t)k); any |= e[k]; }
    if (!any) return 0;
    if (sq_held >= 0) {
        int32_t s = sq_held;
        sq_edited = 1;
        if (sq_drum() || sq_bank) {
            if (sq_drum()) { if (e[0]) ss(s, SF_VEL, sg(s, SF_VEL) + e[0] * 4); if (e[1]) ss(s, SF_PROB, sg(s, SF_PROB) + e[1] * 5); }
            else {
                if (e[0]) ss(s, SF_CENTS, sg(s, SF_CENTS) + e[0]);
                if (e[2]) ss(s, SF_SLIDE, e[2] > 0);
            }
            if (sq_drum() ? e[2] : e[1]) ss(s, SF_RATCHET, sg(s, SF_RATCHET) + (sq_drum() ? e[2] : e[1]));
            if (e[3]) {
                int32_t i = (int32_t)cond_index(sg(s, SF_COND)) + e[3];
                ss(s, SF_COND, cond_value((uint32_t)(i < 0 ? 0 : i > 35 ? 35 : i)));
            }
        } else {
            if (e[0]) {                                                 /* the note: degree and period together */
                int32_t n = (int32_t)tun_degrees(), v = step_note(s) + e[0], per;
                if (v < -3 * n) v = -3 * n;
                if (v > 4 * n - 1) v = 4 * n - 1;
                per = v >= 0 ? v / n : -((-v + n - 1) / n);
                ss(s, SF_DEG0, v - per * n); ss(s, SF_PER0, per); sq_deg = v;
            }
            if (e[1]) ss(s, SF_VEL, sg(s, SF_VEL) + e[1] * 4);
            if (e[2]) ss(s, SF_GATE, sg(s, SF_GATE) + e[2]);
            if (e[3]) ss(s, SF_PROB, sg(s, SF_PROB) + e[3] * 5);
        }
    } else {
        uint32_t p = sq_pat(), t = sq_track;
        if (!sq_bank) {
            if (e[0]) seq_track_set(p, t, TF_LEN, seq_track_get(p, t, TF_LEN) + e[0]);
            if (e[1]) seq_track_set(p, t, TF_SPEED, seq_track_get(p, t, TF_SPEED) + e[1]);
            if (e[2]) seq_track_set(p, t, TF_SWING, seq_track_get(p, t, TF_SWING) + e[2]);
            if (e[3]) seq_track_set(p, t, TF_TRANSPOSE, seq_track_get(p, t, TF_TRANSPOSE) + e[3]);
        } else {
            if (e[0]) { int32_t q = (int32_t)p + e[0]; seq_queue_pattern((uint32_t)(q < 0 ? 0 : q >= (int32_t)SEQ_PATTERNS ? (int32_t)SEQ_PATTERNS - 1 : q)); }
            if (e[1]) seq_pattern_set(p, PF_LEN, seq_pattern_get(p, PF_LEN) + e[1]);
            if (e[2]) seq_track_set(p, t, TF_DIR, seq_track_get(p, t, TF_DIR) + e[2]);
            if (e[3]) seq_track_set(p, t, TF_MUTE, e[3] > 0);
        }
    }
    return 1;
}

/* ---- the screen: a title, 16 cells (2 rows of 8), then four values in two columns */
static void sq_cell(uint32_t i, int32_t pos)
{
    int32_t s = (int32_t)(sq_page * 16u + i), x = 3 + (int32_t)(i % 8u) * 30, y = 20 + (int32_t)(i / 8u) * 30;
    int lit = step_lit(s), any = sg(s, SF_ON), len = s < seq_track_get(sq_pat(), sq_track, TF_LEN);
    uint16_t bg = lit ? M_RED : any ? RGB(90, 30, 30) : len ? RGB(40, 24, 24) : RGB(16, 12, 12);
    cv_rect(x, y, 27, 27, s == pos ? M_AMB : s == sq_held ? M_TXT : bg);
    cv_rect(x + 2, y + 2, 23, 23, bg);
    if (lit && !sq_drum()) {
        char t[8]; int32_t d = sg(s, SF_DEG0);
        t[putn(t, (uint32_t)d) - t] = 0;
        cv_text(x + 14 - text_w(&FONT_S, t) / 2, y + 6, &FONT_S, t, C_BLACK);
    }
}
static void sq_value(int32_t x, int32_t y, const char *label, const char *val)
{
    char t[24], *p = t;
    p = put(p, val); *p = 0; upcase(t);
    cv_text(x, y, &FONT_S, label, M_DIM);
    if (text_w(&FONT_L, t) <= 112) cv_text(x, y + 11, &FONT_L, t, M_TXT);
    else cv_text(x, y + 20, &FONT_S, t, M_TXT);
}
static void draw_seqpage(void)
{
    char t[48], v[4][16], *p = t;
    const char *lab[4];
    uint32_t i, pt = sq_pat(), tr = sq_track;
    int32_t pos = seq_track_pos(tr);
    cv_begin(240, 96, C_BLACK);
    p = put(p, "SEQ "); p = put(p, TRACK_NAME[tr]);
    if (sq_drum()) { *p++ = ' '; p = put(p, DRUMV_NAME[tr - TR_DRUM_A][sq_voice]); }
    *p = 0;
    cv_text(6, 1, &FONT_S, t, M_RED);
    p = t; p = put(p, "P"); p = putn(p, pt + 1u); *p++ = ' '; p = putn(p, sq_page * 16u + 1u); *p++ = '-'; p = putn(p, sq_page * 16u + 16u); *p = 0;
    cv_text(234 - text_w(&FONT_S, t), 1, &FONT_S, t, M_DIM);
    for (i = 0; i < 16u; i++) sq_cell(i, pos);
    p = t; p = put(p, sq_held >= 0 ? "STEP " : sq_bank ? "PATTERN" : "TRACK");
    if (sq_held >= 0) p = putn(p, (uint32_t)sq_held + 1u);
    *p = 0;
    cv_text(6, 80, &FONT_S, t, M_AMB);
    cv_blit(0, PANEL_Y);
    sq_pos_drawn = pos;

    /* the values: the held step's, or the track's */
    for (i = 0; i < 4u; i++) v[i][0] = 0;
    if (sq_held >= 0) {
        int32_t s = sq_held;
        if (sq_drum() || sq_bank) {
            if (sq_drum()) {
                lab[0] = "VELOCITY"; v[0][putn(v[0], (uint32_t)sg(s, SF_VEL)) - v[0]] = 0;
                lab[1] = "PROB"; p = putn(v[1], (uint32_t)sg(s, SF_PROB)); *p++ = '%'; *p = 0;
                lab[2] = "RATCHET";
            } else {
                lab[0] = "CENTS"; p = putsg(v[0], sg(s, SF_CENTS)); *p = 0;
                lab[1] = "RATCHET";
                lab[2] = "SLIDE"; p = put(v[2], sg(s, SF_SLIDE) ? "ON" : "OFF"); *p = 0;
            }
            p = sq_drum() ? v[2] : v[1]; p = put(p, "x"); p = putn(p, (uint32_t)sg(s, SF_RATCHET)); *p = 0;
            lab[3] = "CONDITION";
            {
                int32_t c = sg(s, SF_COND);
                if (!c) p = put(v[3], "ALWAYS"); else { p = putn(v[3], (uint32_t)(c >> 4)); *p++ = ':'; p = putn(p, (uint32_t)(c & 15)); }
                *p = 0;
            }
        } else {
            int32_t g = sg(s, SF_GATE);
            lab[0] = "NOTE"; p = putn(v[0], (uint32_t)sg(s, SF_DEG0));
            if (sg(s, SF_PER0)) { *p++ = ' '; *p++ = 'P'; p = putsg(p, sg(s, SF_PER0)); }
            *p = 0;
            lab[1] = "VELOCITY"; v[1][putn(v[1], (uint32_t)sg(s, SF_VEL)) - v[1]] = 0;
            lab[2] = "LENGTH"; if (g >= 17) p = put(v[2], "TIE"); else { p = putn(v[2], (uint32_t)g); p = put(p, "/16"); } *p = 0;
            lab[3] = "PROB"; p = putn(v[3], (uint32_t)sg(s, SF_PROB)); *p++ = '%'; *p = 0;
        }
    } else if (!sq_bank) {
        int32_t tp = seq_track_get(pt, tr, TF_TRANSPOSE);
        lab[0] = "LENGTH"; v[0][putn(v[0], (uint32_t)seq_track_get(pt, tr, TF_LEN)) - v[0]] = 0;
        lab[1] = "SPEED"; p = put(v[1], SPEED_NAME[seq_track_get(pt, tr, TF_SPEED)]); *p = 0;
        lab[2] = "SWING"; p = putn(v[2], (uint32_t)seq_track_get(pt, tr, TF_SWING)); *p++ = '%'; *p = 0;
        lab[3] = "TRANSPOSE"; p = putsg(v[3], tp); *p = 0;
    } else {
        int32_t q = seq_pattern_queued();
        lab[0] = "PATTERN"; p = putn(v[0], pt + 1u); if (q >= 0 && (uint32_t)q != pt) { *p++ = '>'; p = putn(p, (uint32_t)q + 1u); } *p = 0;
        lab[1] = "PAT LENGTH"; v[1][putn(v[1], (uint32_t)seq_pattern_get(pt, PF_LEN)) - v[1]] = 0;
        lab[2] = "DIRECTION"; p = put(v[2], DIR_NAME[seq_track_get(pt, tr, TF_DIR)]); *p = 0;
        lab[3] = "MUTE"; p = put(v[3], seq_track_get(pt, tr, TF_MUTE) ? "ON" : "OFF"); *p = 0;
    }
    cv_begin(240, PANEL_H - 96, C_BLACK);
    sq_value(6, 0, lab[0], v[0]); sq_value(124, 0, lab[1], v[1]);
    sq_value(6, 44, lab[2], v[2]); sq_value(124, 44, lab[3], v[3]);
    cv_blit(0, PANEL_Y + 96);
}
/* only the cells, when the playhead moved */
static void draw_seqcells(void)
{
    int32_t pos = seq_track_pos(sq_track);
    uint32_t i;
    if (pos == sq_pos_drawn) return;
    cv_begin(240, 78, C_BLACK);
    for (i = 0; i < 16u; i++) sq_cell(i, pos);
    cv_blit_from(0, PANEL_Y, 18);
    sq_pos_drawn = pos;
}
