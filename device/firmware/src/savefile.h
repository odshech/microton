/* SPDX-License-Identifier: GPL-3.0-only */
/* savefile.h — SAVE on the FM-1, included by dinghy.c after seqpage.h.
 *
 * SAVE writes everything to flash: the three sounds, the drum kit, the effects, the scale (with its mode and the
 * window) and the tempo in blob 0, and the sequencer's patterns in slots 0..2 (46 KB in 16 KB pieces). At power-on
 * it is all read back; nothing saved, or a save from another version, starts from the defaults.
 * Flash erases hold the interrupts off (~50 ms a sector), so a save first stops the sequencer and lets the sound
 * die away: the drop-out falls in silence. */

#define SAVE_VERSION 0x4D540001u                /* "MT", 1: change it when the layout below changes */
typedef struct {
    uint32_t version;
    uint16_t syn[ENG_PARTS][P_COUNT];
    uint16_t drum[ENG_DRUMS][DP_COUNT];
    uint16_t fx[FX_COUNT];
    uint16_t scale_i, mode, tempo, pad;
    int32_t win;
    uint32_t project_size;                      /* the patterns' size as saved: another layout is not read */
} save_t;
_Static_assert(sizeof(save_t) <= SECTOR_MAX, "save_t: more than one blob");
#define PROJ_SLOTS(n) (((n) + SLOT_MAX - 1u) / SLOT_MAX)            /* 46 KB: 3 */

static save_t SV;
/* a word in the title bar for a moment: SAVED, LOADED */
static uint32_t msg_t;
static int msg_on;
static void header_msg(const char *t, uint16_t col)
{
    cv_begin(110, 18, C_BLACK);
    if (t) cv_text(0, 0, &FONT_S, t, col);
    cv_blit(96, 4);
    msg_on = t != 0; msg_t = fm1_ticks();
}
static void header_msg_tick(void) { if (msg_on && fm1_ticks() - msg_t > 36000000u) header_msg(0, 0); }   /* 1.5 s */

static void save_gather(void)
{
    uint32_t i, k;
    SV.version = SAVE_VERSION;
    for (i = 0; i < ENG_PARTS; i++) for (k = 0; k < P_COUNT; k++) SV.syn[i][k] = (uint16_t)eng_get_param(i, k);
    for (i = 0; i < ENG_DRUMS; i++) for (k = 0; k < DP_COUNT; k++) SV.drum[i][k] = (uint16_t)eng_drum_get(i, k);
    for (k = 0; k < FX_COUNT; k++) SV.fx[k] = (uint16_t)eng_fx_get(k);
    SV.scale_i = (uint16_t)scale_i; SV.mode = (uint16_t)tun_mode(); SV.win = win;
    SV.tempo = (uint16_t)(seq_tempo() / 10u);   /* in tenths of a BPM: 300.0 fits */
    SV.project_size = seq_project_size();
}

/* SAVE: 0 = done */
static int save_all(void)
{
    uint32_t i, n = seq_project_size(), t0;
    const uint8_t *pj = seq_project_bytes();
    int bad = 0;
    seq_stop();
    eng_all_off();
    t0 = fm1_ticks();                                                   /* let the tails fade (at most 0.4 s) */
    while (eng_voices_active() && fm1_ticks() - t0 < 9600000u) platform_poll();
    save_gather();
    bad |= blob_save(0, &SV, sizeof SV) != 0;
    if (PROJ_SLOTS(n) > NSLOT) bad = 1;
    else for (i = 0; i < PROJ_SLOTS(n); i++) {
        uint32_t off = i * SLOT_MAX, len = n - off < SLOT_MAX ? n - off : SLOT_MAX;
        bad |= slot_save(i, pj + off, len) != 0;
    }
    return bad ? -1 : 0;
}

/* at power-on, after eng_init and seq_init: 1 = something was loaded */
static int load_all(void)
{
    uint32_t i, k, n = seq_project_size(), ok = 1;
    uint8_t *pj = seq_project_bytes();
    if (blob_load(0, &SV, sizeof SV) != 0 || SV.version != SAVE_VERSION) return 0;
    for (i = 0; i < ENG_PARTS; i++) for (k = 0; k < P_COUNT; k++) eng_set_param(i, k, SV.syn[i][k]);
    for (i = 0; i < ENG_DRUMS; i++) for (k = 0; k < DP_COUNT; k++) eng_drum_set(i, k, SV.drum[i][k]);
    for (k = 0; k < FX_COUNT; k++) eng_fx_set(k, SV.fx[k]);
    if (SV.scale_i < sizeof LIB_ORDER) { scale_i = SV.scale_i; scale_load(); tun_rotate(SV.mode); win = SV.win; }
    if (SV.project_size == n && PROJ_SLOTS(n) <= NSLOT) {
        for (i = 0; i < PROJ_SLOTS(n) && ok; i++) {
            uint32_t off = i * SLOT_MAX, len = n - off < SLOT_MAX ? n - off : SLOT_MAX;
            ok = slot_load(i, pj + off, len) == 0;
        }
        if (!ok || *(const uint32_t *)pj != 0x51455331u) seq_init();     /* a piece missing: empty patterns */
    }
    if (SV.tempo >= 300u && SV.tempo <= 3000u) seq_set_tempo((uint32_t)SV.tempo * 10u);
    return 1;
}
