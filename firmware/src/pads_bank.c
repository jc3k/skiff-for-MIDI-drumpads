/* SPDX-License-Identifier: GPL-3.0-only */
/* (EMP16 pads) The pads' main-loop side (pads.c: the ISR's, the setup): a PATTERN pad's hit launched or its bank
 * stopped, LEARN, PAD SETUP's values. Included by bank.c (after entry_go, stage_entry). */

/* every SOUND pad let go (a project load, the pads turned off, CH changed: their notes end) */
static void pads_release_all(void)
{
    uint32_t i;
    fm1_irq_off();
    for (i = 0; i < PAD_N; i++)
        pad_sound_off(i);
    fm1_irq_on();
}

/* bank b's pad playing: the entry its track is on, while the track is on that entry's pattern (bank.c bank_cur: a
 * rest, steps of no pattern: none) */
static uint32_t pads_playing(uint32_t b)
{
    uint32_t e = bank_cur(b % PAD_BANKS);
    return e < NENT ? e : PAD_PER;
}
static uint32_t pads_staged(uint32_t b)
{
    const launch_t *q = &launch_q[b % PAD_BANKS];
    uint32_t e = bank_next(b % PAD_BANKS);
    return e < NENT && !(q->armed && q->rest) ? e : PAD_PER;
}
static int pads_stopping(uint32_t b) { return launch_q[b % PAD_BANKS].armed && launch_q[b % PAD_BANKS].rest; }
static uint32_t pads_pattern(uint32_t i)
{
    uint32_t k = (i / PAD_PER) % PAD_BANKS, e = i % PAD_PER;
    return ent_rest(k, e) ? PAT_REST : ent_pat(k, e);
}
static int pads_entry_sound(uint32_t i) { return lists.snd[(i / PAD_PER) % PAD_BANKS][i % PAD_PER][0] != 0u; }

/* the bank's name in what the header says ("A", ..): its track's is said by bank.c's own messages (T1 C ..) */
static void pads_say(uint32_t b, const char *what)
{
    char s[3] = {(char)('A' + b % PAD_BANKS), ' ', 0};
    ui_say(s, what);
}
/* track k on a rest now (stopped): what it had kept first (bank.c ent_go's REST); 4 PATTERNS FULL */
static int pads_rest_now(uint32_t k)
{
    if (trk_keep(k))
        return 4;
    track_defaults_steps(&trk[k]);
    trk[k].p[P_SGATE] = TP[P_SGATE].def;
    trk_pid[k] = 0;
    bk.gen++;
    ui.force = 1;
    return 0;
}
/* bank b's pad playing hit again: the bank stops, at once stopped, at the LAUNCH point playing (a rest staged on the
 * entry it is on: bank.c stage_entry, stage_rest_req). Whatever else was staged for it is dropped */
static void pads_stop(uint32_t b)
{
    launch_t *q = &launch_q[b];
    int rc;
    chain_go[b] = 0;
    if (!song.playing) {
        q->armed = 0;
        RING_PUBLISH();
        bank_poll();
        rc = pads_rest_now(b);
        if (rc == 4)
            ui_way("PATTERNS FULL", WAY_PATTERNS);
        else
            pads_say(b, "STOPPED");
        return;
    }
    q->armed = 0;                                  /* (from here the ISR leaves it alone) */
    RING_PUBLISH();
    bank_poll();                                   /* (a swap just made: kept first) */
    stage_rest_req = 1;
    rc = stage_entry(b, lists.cur[b] % NENT, launch_mode(), 0, 0);
    stage_rest_req = 0;
    if (rc == 4)
        ui_way("PATTERNS FULL", WAY_PATTERNS);
    else {
        char s[24] = "STOP";
        str_cpy(s + 4, LAUNCH_AT[launch_mode()], sizeof s - 4u);
        pads_say(b, s);
    }
    bk.gen++;
}
/* PATTERN pad i hit (pads.c padq) */
static void pads_press(uint32_t i)
{
    uint32_t b = (i / PAD_PER) % PAD_BANKS, e = i % PAD_PER;
    launch_t *q = &launch_q[b];
    if (pads_stopping(b) && e == lists.cur[b] % NENT) {   /* (its STOP staged: hit again, it plays on) */
        q->armed = 0;
        RING_PUBLISH();
        bk.gen++;
        pads_say(b, "STOP CANCELLED");
        return;
    }
    if (pads_playing(b) == e) {
        pads_stop(b);
        return;
    }
    (void)entry_go(b, e);                          /* (an empty one: T1 C EMPTY; staged, or at once stopped) */
    ui.force = 1;
}

/* ---- PAD SETUP (ui_input.c, ui_draw.c, ui_graph.c: GR_PADSET) ---- */
static uint8_t pad_pick;                           /* the pad PAD SETUP shows (0..63) */
/* LEARN on (1: waiting for bank A's pad 1, .. D's), or off */
static void pads_learn(int on)
{
    pad_heard = 0;
    pad_learn = (uint8_t)(on ? 1u : 0u);
    if (on)
        ui_say("LEARN: HIT PAD 1 OF ", "A");
    ui.force = 1;
}
/* every frame (ui_input.c, with bank_poll): the PATTERN hits launched, LEARN's notes taken, a pad hit on PAD SETUP
 * picked */
static void pads_poll(void)
{
    static uint32_t seen;
    while (padq_r != padq_w) {
        uint32_t i = padq[padq_r % PADQ];
        padq_r++;
        if (!(pads.cfg[i % PAD_N][0] & PAD_SOUND))
            pads_press(i % PAD_N);
    }
    if (pad_learn && pad_heard) {                  /* (LEARN: the note bank pad_learn - 1's pad 1; then the next) */
        uint32_t b = (pad_learn - 1u) % PAD_BANKS;
        char s[2] = {(char)('B' + b), 0};
        pads.base[b] = (uint8_t)(pad_heard - 1u);
        pad_heard = 0;
        if (b + 1u < PAD_BANKS) {
            pad_learn = (uint8_t)(b + 2u);
            ui_say("LEARN: HIT PAD 1 OF ", s);
        } else {
            pad_learn = 0;
            ui_message("PADS LEARNED");
        }
        ui.force = 1;
    }
    if (pad_hits != seen) {
        seen = pad_hits;
        if (pad_setup_on && pad_last)
            pad_pick = (uint8_t)((pad_last - 1u) % PAD_N);
        ui.force = 1;
    }
}
/* PAD SETUP's knobs: 1 TYPE, a SOUND pad's 2 TRACK, 3 NOTE, 4 VELOCITY (0 as played, 1..15: 15..127) */
static void pads_knob(uint32_t slot, int32_t steps)
{
    uint8_t *c = pads.cfg[pad_pick % PAD_N];
    int32_t d = steps > 0 ? 1 : -1;
    pads_release_all();
    if (slot == 0u) {
        if (d > 0)
            c[0] |= PAD_SOUND;
        else
            c[0] &= (uint8_t)~PAD_SOUND;
    } else if (!(c[0] & PAD_SOUND)) {
        return;
    } else if (slot == 1u) {
        c[0] = (uint8_t)((c[0] & ~7u) | (uint32_t)clamp((int32_t)PAD_TRK(c[0]) + d, 0, NTRK - 1));
    } else if (slot == 2u) {
        c[1] = (uint8_t)clamp((int32_t)c[1] + steps, 0, 127);
    } else if (slot == 3u) {
        c[0] = (uint8_t)((c[0] & ~(15u << 3)) | (uint32_t)clamp((int32_t)PAD_VEL(c[0]) + steps, 0, 15) << 3);
    }
    ui.force = 1;
}
/* PRESETS on PAD SETUP: the pad, 1..64 (A1 .. D16) */
static void pads_pick_turn(int32_t s)
{
    pad_pick = (uint8_t)clamp((int32_t)pad_pick + s, 0, PAD_N - 1);
    ui.force = 1;
}
/* OCT+ on PAD SETUP: the picked pad's TYPE (and, a SOUND, its TRACK and VELOCITY) on every pad of its bank; a SOUND
 * bank's notes the lanes in turn (KICK SNARE CLAP CL HAT OP HAT TOM RIM BELL, twice) */
static void pads_whole_bank(void)
{
    uint32_t b = (pad_pick / PAD_PER) % PAD_BANKS, i;
    uint8_t c0 = pads.cfg[pad_pick % PAD_N][0];
    pads_release_all();
    for (i = 0; i < PAD_PER; i++) {
        pads.cfg[b * PAD_PER + i][0] = c0;
        if (c0 & PAD_SOUND)
            pads.cfg[b * PAD_PER + i][1] = (uint8_t)pad_def_note(i);
    }
    pads_say(b, c0 & PAD_SOUND ? "ALL SOUND" : "ALL PATTERN");
    ui.force = 1;
}
