/* SPDX-License-Identifier: GPL-3.0-only */
/* (EMP16 pads) firmware/src/pads.c and pads_bank.c against the real MIDI path (midi_enqueue -> events_block ->
 * midi_event), the sequencer, the launch points and the pattern pool (tests/bank_test.c's simulated flash and helpers):
 *   the EMP16's notes -> bank and pad (its default run D A B C, pad 1 bottom left), notes outside the runs and other
 *   channels untouched; a PATTERN pad stages its entry for the launch point and it comes in there; the pad playing hit
 *   again stops the bank at the launch point (a rest: silence, on no pattern, what went out kept), again before that
 *   cancels it; stopped, at once; an empty pad says so; a SOUND pad plays its note on its track while held (fixed
 *   VELOCITY too), PAD SETUP open: a hit picks, PATTERN pads launch nothing; LEARN; WHOLE BANK; the pads kept with the
 *   project (SKF4's PAD block) and a project without it the defaults; a record of before reads the same. */
#define BANK_TEST_NO_MAIN 1
#include "bank_test.c"

static int msg_is_b(const char *m) { return str_eq(ui.msg, m); }
static int pchk(const char *what, int ok)
{
    printf("pads: %-72s %s\n", what, ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}
static void pmidi(uint32_t st, uint32_t d1, uint32_t d2)   /* a message in, as USB brings it, heard in the next block */
{
    midi_enqueue((st >> 4) | st << 8 | d1 << 16 | d2 << 24, 1);
    events_block(1);
}
static void hit(uint32_t bank, uint32_t pad)      /* the EMP16's default: channel 10, pad 1..16 of bank 0..3 */
{
    pmidi(0x99, PAD_DEF_BASE[bank] + pad - 1u, 100);
    pmidi(0x89, PAD_DEF_BASE[bank] + pad - 1u, 0);
    pads_poll();
}
static int gate(const track_t *t, uint32_t note)
{
    for (uint32_t i = 0; i < NVOICE; i++) if (t->v[i].active && t->v[i].gate && t->v[i].note == note) return 1;
    return 0;
}
static void on_page(uint32_t graph)               /* the page shown (pads_poll reads it: PAD SETUP, LEARN on PADS) */
{
    uint32_t i;
    for (i = 0; i < NPAGES && PAGES[i].graph != graph; i++)
        ;
    ui.home = 0; ui.menu = 0; ui.layer = 0;
    ui.page = (uint8_t)i;
}
static void pfresh(void)
{
    fresh();
    pads_defaults();
    memset(midi_ch, 0, sizeof midi_ch); memset(midi_notes, 0, sizeof midi_notes);
    memset(midi_owners, 0, sizeof midi_owners);
    memset(pad_held, 0, sizeof pad_held);
    mi_r = mi_w = 0;
    padq_r = padq_w = 0;
    pad_setup_on = pad_learn = pad_heard = pad_last = 0;
}

static int mapping(void)
{
    int bad = 0;
    pfresh();
    bad += pchk("defaults: on, channel 10, bank A pad 1 = note 36 (MIDI Monitor C1), B 52, C 68, D 20",
                pads.on && pads.ch == 9u && pad_of(36) == 0 && pad_of(52) == 16 && pad_of(68) == 32 && pad_of(20) == 48);
    bad += pchk("  pad 16 of each: A 51, B 67, C 83, D 35; one run D A B C, 20..83",
                pad_of(51) == 15 && pad_of(67) == 31 && pad_of(83) == 47 && pad_of(35) == 63 && pad_of(19) < 0 &&
                pad_of(84) < 0);
    bad += pchk("  as Yaro's MIDI Monitor read them: A pad 16 (D#2) 51, B pad 1 (E3) 52, D pad 16 (B-1) .. as one run",
                pad_of(36 + 15) == 15 && pad_of(52) == 16);
    bad += pchk("a note of channel 10 outside the runs: not a pad's (midi_event goes on with it)",
                !pad_event(0x90, 9, 90, 100) && !pad_event(0x90, 9, 10, 100));
    bad += pchk("another channel, a CC: not the pads'", !pad_event(0x90, 0, 36, 100) && !pad_event(0xB0, 9, 36, 100));
    pads.on = 0;
    bad += pchk("PADS off: channel 10 as before", !pad_event(0x90, 9, 36, 100));
    return bad;
}

static int launching(void)
{
    int bad = 0, ok;
    track_t *t = &trk[0];
    pfresh();
    simple(0, 60, 8, DIR_FWD);                    /* bank A pad 2 (entry B): 60.. */
    bank_put(0, 1, 0);
    simple(0, 80, 8, DIR_FWD);                    /* bank A pad 3 (entry C): 80.. */
    bank_put(0, 2, 0);
    transport_req = 2;
    events_block(1);
    bank_poll();
    hit(0, 2);                                    /* stopped: B at once */
    ok = bank_cur(0) == 1u && t->step[0].note[0] == 60 && pads_playing(0) == 1u;
    bad += pchk("stopped: bank A pad 2 puts T1 on entry B at once (bank.c entry_go)", ok);
    hit(0, 9);
    bad += pchk("  an empty pad: T1 I EMPTY, nothing changes", msg_is_b("T1 I EMPTY") && bank_cur(0) == 1u);

    ui_launch = LAUNCH_LOOP;
    transport_req = 1;
    events_block(1);
    lv_next(2);
    hit(0, 3);
    ok = pads_staged(0) == 2u && pads_playing(0) == 1u && !pads_stopping(0) && sounds(62);
    lv_next(5);
    ok &= sounds(67) && pads_staged(0) == 2u;
    lv_next(1);
    bank_poll();
    ok &= sounds(80) && pads_playing(0) == 2u && pads_staged(0) == PAD_PER;
    bad += pchk("playing, LOOP: pad 3 staged mid-pass, comes in at the pass end (C's first step sounds)", ok);

    lv_next(2);
    hit(0, 3);                                    /* the pad playing: STOP at the pass end */
    ok = pads_stopping(0) && pads_playing(0) == 2u && msg_is_b("A STOP AT LOOP END");
    hit(0, 3);
    ok &= !pads_stopping(0) && msg_is_b("A STOP CANCELLED");
    bad += pchk("  the pad playing hit again: A STOP AT LOOP END; again before it lands: cancelled", ok);
    {
        uint32_t n, before = 0;
        hit(0, 3);                                /* STOP staged again: the steps go on to C's pass end */
        for (n = 0; n < 16u && launch_q[0].armed; n++) {
            before = t->seq_n ? t->seq_notes[0] : 0u;
            lv_next(1);
        }
        bank_poll();
        ok = n < 16u && before == 87u && !t->seq_n && pads_playing(0) == PAD_PER && !pads_stopping(0) &&
             trk_pid[0] == 0 && seq_is_empty(t);
        lv_next(3);
        ok &= !t->seq_n && ent_pat(0, 2) != PAT_NONE && pool_used(ent_pat(0, 2));
        bad += pchk("  .. its STOP lands at the pass end (after C's last step): silence, on no pattern; C kept", ok);
        hit(0, 3);
        for (n = 0; n < 16u && launch_q[0].armed; n++)
            lv_next(1);
        bank_poll();
        ok = n < 16u && pads_playing(0) == 2u && sounds(80);
        bad += pchk("  stopped bank: pad 3 again, C comes in at the rest's pass end, from its first step", ok);
    }

    hit(0, 2);
    ok = pads_staged(0) == 1u;
    hit(0, 3);
    ok &= pads_stopping(0) && pads_staged(0) == PAD_PER;
    hit(0, 2);
    ok &= pads_staged(0) == 1u && !pads_stopping(0);
    bad += pchk("  another pad replaces what was staged; a STOP replaces it; another pad replaces the STOP", ok);

    transport_req = 2;
    events_block(1);
    bank_poll();
    hit(0, 3);                                    /* (stopped: B came in at the stop? the staged one comes in now) */
    ok = 1;
    if (pads_playing(0) == 2u) {
        hit(0, 3);
        ok = pads_playing(0) == PAD_PER && msg_is_b("A STOPPED") && trk_pid[0] == 0;
    }
    bad += pchk("stopped: the pad playing hit stops the bank at once (A STOPPED)", ok);
    return bad;
}

static int sounds_test(void)
{
    int bad = 0, ok;
    track_t *t = &trk[4];
    pfresh();
    pads.cfg[48][0] = PAD_SOUND | 4u;             /* bank D pad 1: SOUND, T5, KICK (36) */
    pads.cfg[48][1] = 36;
    pmidi(0x99, 20, 100);
    ok = gate(t, 36) && padq_w == padq_r;
    pmidi(0x89, 20, 0);
    ok &= !gate(t, 36);
    bad += pchk("a SOUND pad (D pad 1: T5, note 36) plays while held, lets go on its release, launches nothing", ok);
    pads.cfg[49][0] = (uint8_t)(PAD_SOUND | 2u | 15u << 3);   /* D pad 2: T3, note 62, VELOCITY fixed 127 */
    pads.cfg[49][1] = 62;
    pmidi(0x99, 21, 20);
    ok = gate(&trk[2], 62);
    for (uint32_t i = 0; i < NVOICE; i++)
        if (trk[2].v[i].active && trk[2].v[i].note == 62)
            ok &= trk[2].v[i].vel == 127;
    pads.cfg[49][1] = 64;                         /* (changed while held: its release still ends what it started) */
    pmidi(0x89, 21, 0);
    ok &= !gate(&trk[2], 62);
    bad += pchk("  fixed VELOCITY 127 whatever the hit; changed while held, its release ends the note it started", ok);
    on_page(GR_PADSET);
    pads_poll();
    pmidi(0x99, 36, 100);
    pads_poll();
    ok = padq_w == padq_r && pad_pick == 0u;
    pmidi(0x89, 36, 0);
    pmidi(0x99, 20, 100);
    pads_poll();
    ok &= gate(t, 36) && pad_pick == 48u;
    pmidi(0x89, 20, 0);
    on_page(GR_SONG);
    pads_poll();
    ok &= !pad_setup_on;
    bad += pchk("PAD SETUP open: a PATTERN pad picks, launches nothing; a SOUND pad picks and sounds", ok);
    return bad;
}

static int setup_test(void)
{
    int bad = 0, ok;
    pfresh();
    on_page(GR_PADS);
    pads_learn(1);
    ok = pad_learn == 1u && msg_is_b("LEARN: HIT PAD 1 OF A");
    pmidi(0x99, 48, 100); pads_poll();
    ok &= pads.base[0] == 48 && pad_learn == 2u && msg_is_b("LEARN: HIT PAD 1 OF B");
    pmidi(0x99, 64, 100); pads_poll();
    pmidi(0x99, 80, 100); pads_poll();
    pmidi(0x99, 96, 100); pads_poll();
    ok &= pads.base[1] == 64 && pads.base[2] == 80 && pads.base[3] == 96 && !pad_learn && msg_is_b("PADS LEARNED");
    ok &= pad_of(48) == 0 && pad_of(111) == 63 && pad_of(36) < 0;
    bad += pchk("LEARN: a hit on each bank's pad 1 in turn, A B C D, said as it goes; the runs from them", ok);

    pfresh();
    pad_pick = 18;                                /* B pad 3 */
    pads_knob(0, 1);
    pads_knob(1, 1);
    pads_knob(3, 2);
    ok = (pads.cfg[18][0] & PAD_SOUND) && PAD_TRK(pads.cfg[18][0]) == 5u && PAD_VEL(pads.cfg[18][0]) == 2u;
    pads_knob(2, -2);
    ok &= pads.cfg[18][1] == 37;
    pads_whole_bank();
    for (uint32_t i = 16; i < 32; i++)
        ok &= pads.cfg[i][0] == pads.cfg[18][0] && pads.cfg[i][1] == DRUM_LANE_NOTE[(i - 16u) % NLANE];
    ok &= !(pads.cfg[15][0] & PAD_SOUND) && !(pads.cfg[32][0] & PAD_SOUND);
    bad += pchk("PAD SETUP: TYPE SOUND, TRACK T6, VELOCITY; OCT+ WHOLE BANK: bank B all SOUND, the lanes in turn", ok);
    pads_knob(0, -1);
    bad += pchk("  TYPE back to PATTERN", !(pads.cfg[18][0] & PAD_SOUND));
    return bad;
}

static int project_test_pads(void)
{
    int bad = 0, ok;
    static project_t p, q;
    static project_store_t st;
    pfresh();
    project_capture(&p);
    ok = !p.pads_on && proj_pack2(&st, &p, PROJ_SKF) && st.raw[PROJ_PAD_AT(PROJ_SKF)] == 0u;
    bad += pchk("the defaults: no PAD block written (the record as before)", ok);
    pads.cfg[5][0] = (uint8_t)(PAD_SOUND | 3u);
    pads.cfg[5][1] = 42;
    pads.base[2] = 70;
    pads.ch = 3;
    project_capture(&p);
    ok = p.pads_on && proj_pack2(&st, &p, PROJ_SKF) && st.raw[PROJ_PAD_AT(PROJ_SKF)] == PROJ_PAD_MARK;
    ok &= proj_unpack4(&q, st.raw) && q.pads_on && !memcmp(&q.pads, &pads, sizeof pads);
    bad += pchk("changed: the PAD block written; read back the same", ok);
    pads_defaults();
    (void)project_restore_runtime(&q);
    ok = pads.cfg[5][1] == 42 && pads.base[2] == 70 && pads.ch == 3u;
    st.raw[PROJ_PAD_AT(PROJ_SKF)] = 0;            /* (a record without it: written by Skiff) */
    {
        uint32_t sum = proj_hash(st.raw, PROJ_STORE4 - 4u);
        memcpy(st.raw + PROJ_STORE4 - 4u, &sum, 4);
    }
    ok &= proj_unpack4(&q, st.raw) && !q.pads_on;
    (void)project_restore_runtime(&q);
    ok &= pads_is_default();
    bad += pchk("a project loaded: its pads; one without the block: the defaults", ok);
    return bad;
}

/* the review's: a STOP that sticks (the transport stopped before its launch point; SONG on; a chain), LEARN with a
 * SOUND pad held, the pads giving channel 10 back to a track set to it or to MIDI IN SEL */
static int sticky(void)
{
    int bad = 0, ok;
    track_t *t = &trk[0];
    uint32_t n;
    pfresh();
    simple(0, 60, 8, DIR_FWD);
    bank_put(0, 1, 0);
    simple(0, 80, 8, DIR_FWD);
    bank_put(0, 2, 0);
    transport_req = 2; events_block(1); bank_poll();
    hit(0, 2);
    ui_launch = LAUNCH_LOOP;
    transport_req = 1; events_block(1);
    lv_next(2);
    hit(0, 2);                                    /* STOP staged, then the transport stops before it lands */
    ok = pads_stopping(0);
    transport_req = 2; events_block(1); bank_poll();
    ok &= pads_playing(0) == PAD_PER && trk_pid[0] == 0 && seq_is_empty(t) && !launch_q[0].armed;
    transport_req = 1; events_block(1);
    lv_next(3);
    ok &= !t->seq_n;
    bad += pchk("a STOP staged, the transport stopped first: the bank is stopped (not its pattern again); PLAY: silent", ok);

    lists.song = SONG_ON;                         /* SONG on, B then C: the song does not take a stopped bank on */
    hit(0, 2);
    for (n = 0; n < 24u && pads_playing(0) != 1u; n++) { lv_next(1); bank_poll(); song_poll(); }
    ok = pads_playing(0) == 1u;
    hit(0, 2);
    for (n = 0; n < 40u; n++) { lv_next(1); bank_poll(); song_poll(); }
    ok &= pads_playing(0) == PAD_PER && !t->seq_n && !launch_q[0].armed;
    lists.song = SONG_OFF;
    bad += pchk("  SONG on: a pad's STOP holds (the song stages nothing for the bank until a pad starts it)", ok);
    hit(0, 3);
    for (n = 0; n < 24u && pads_playing(0) != 2u; n++) { lv_next(1); bank_poll(); song_poll(); }
    bad += pchk("  .. a pad starts it again", pads_playing(0) == 2u && !(pad_stopped & 1u));
    transport_req = 2; events_block(1); bank_poll();

    pfresh();
    pads.cfg[48][0] = PAD_SOUND | 4u;
    pads.cfg[48][1] = 36;
    on_page(GR_PADS);
    pmidi(0x99, 20, 100);
    ok = gate(&trk[4], 36);
    pads_learn(1);
    ok &= !gate(&trk[4], 36) && !pad_held[48];
    pmidi(0x89, 20, 0);
    pads_learn(0);
    bad += pchk("LEARN with a SOUND pad held: its note ends at once (the runs may move)", ok);

    pfresh();
    song.g[G_ROUTE] = 0;
    ((uint8_t *)mch_io)[1] = 8;                   /* T2 on CH 10 (MENU > MIDI > T2 CHANNEL): the track's channel */
    ok = !pad_event(0x90, 9, 36, 100) && pads.on;
    ((uint8_t *)mch_io)[1] = 0;
    ok &= pad_event(0x90, 9, 36, 100) == 1;
    padq_r = padq_w;
    song.g[G_ROUTE] = 1;
    ok &= !pad_event(0x90, 9, 36, 100);
    song.g[G_ROUTE] = 0;
    bad += pchk("a track set to CH 10, or MIDI IN SEL: channel 10 is theirs as before; back: the pads'", ok);
    return bad;
}

int main(void)
{
    int bad = 0;
    bad += mapping();
    bad += launching();
    bad += sounds_test();
    bad += setup_test();
    bad += project_test_pads();
    bad += sticky();
    if (bad) {
        printf("pads: %d FAILED\n", bad);
        return 1;
    }
    printf("EMP16 pads tests passed\n");
    return 0;
}
