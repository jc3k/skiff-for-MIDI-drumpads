/* SPDX-License-Identifier: GPL-3.0-only */
/* (EMP16 pads) A pad controller's 4 banks of 16 pads (the EMP16's, on its own settings): each pad a PATTERN or a SOUND.
 *
 *   PATTERN  bank A..D is track 1..4, pad 1..16 its list's entry A..P (SEQ > SONG): a hit plays the entry as SONG's
 *            OCT+ and PERFORM's keys do (bank.c entry_go: at once stopped, at the LAUNCH point playing); a hit on the
 *            pad playing stops the bank there (pads_bank.c pads_stop: a rest comes in), another before that keeps it
 *   SOUND    the pad plays a note on a track (TRACK, NOTE, VELOCITY: as played or fixed), held as long as the pad is
 *
 * The pads are the notes of one MIDI channel (CH, 10 by default: the EMP16's) in four runs of 16: bank b's pad 1 is
 * base[b], pad n base[b] + n - 1 (pad 1 bottom left, row by row up: the EMP16's labels). The EMP16's own notes are
 * the defaults (A 36, B 52, C 68, D 20: its banks are one run D A B C); PAD SETUP's LEARN takes the four from the
 * controller (a hit on each bank's pad 1). A note of the channel outside the four runs goes on as any MIDI note.
 *
 * Here, in the audio ISR (seq.c events_block -> midi_control.c midi_event, USB and TRS alike): a SOUND pad plays at
 * once, as a MIDI note on its track (midi_control.c's ownership: a release ends exactly what it started); a PATTERN
 * pad's hit goes to the main loop (padq: pads_bank.c pads_poll, as Program Change goes, ui.c midi_pc_poll). PAD SETUP
 * open (pad_setup_on): a hit picks the pad, a SOUND pad sounds, a PATTERN pad launches nothing.
 *
 * Kept with the project (project.c: the SKF4 record's PAD block, before the name): every pad's setup, CH, the bases. */
#define PAD_BANKS 4u
#define PAD_PER 16u
#define PAD_N (PAD_BANKS * PAD_PER)
#define PAD_SOUND 0x80u                               /* cfg[i][0]: bit 7 SOUND; bits 0..2 the track; bits 3..6 VELOCITY
                                                       * (0 as played, v: v * 8 + 7) */
#define PAD_TRK(c) ((uint32_t)((c) & 7u) < NTRK ? (uint32_t)((c) & 7u) : 4u)
#define PAD_VEL(c) (((uint32_t)(c) >> 3) & 15u)
typedef struct {
    uint8_t on;                                       /* 1: the pads are heard (MENU > MIDI > PADS) */
    uint8_t ch;                                       /* their channel, 0..15 */
    uint8_t base[PAD_BANKS];                          /* bank A..D's pad 1 */
    uint8_t cfg[PAD_N][2];                            /* each pad: PAD_SOUND | VELOCITY << 3 | track, the note */
} pads_t;
static pads_t pads;                                   /* (the ISR reads it: .bss, as midi_ch) */
#define PAD_DEF_CH 9u
static const uint8_t PAD_DEF_BASE[PAD_BANKS] = {36, 52, 68, 20};

/* a SOUND pad's first note: its place's lane (DRUM_LANE_NOTE: KICK SNARE CLAP CL HAT OP HAT TOM RIM BELL), twice */
static uint32_t pad_def_note(uint32_t i) { return DRUM_LANE_NOTE[i % NLANE]; }
static void pads_fill(pads_t *d)
{
    uint32_t i;
    memset(d, 0, sizeof *d);
    d->on = 1;
    d->ch = PAD_DEF_CH;
    memcpy(d->base, PAD_DEF_BASE, PAD_BANKS);
    for (i = 0; i < PAD_N; i++) {
        d->cfg[i][0] = 4u;                            /* (as a SOUND: T5) */
        d->cfg[i][1] = (uint8_t)pad_def_note(i % PAD_PER);
    }
}
static void pads_defaults(void) { pads_fill(&pads); }
/* the setup the defaults (a PATTERN pad's sound part not counted: not kept): 1 (nothing for a project to keep) */
static int pads_is_default(void)
{
    pads_t d, n = pads;
    uint32_t i;
    pads_fill(&d);
    for (i = 0; i < PAD_N; i++)
        if (!(n.cfg[i][0] & PAD_SOUND))
            n.cfg[i][0] = d.cfg[i][0], n.cfg[i][1] = d.cfg[i][1];
    return !memcmp(&d, &n, sizeof d);
}

/* a setup this firmware plays (a project's): 1 */
static int pads_ok(const pads_t *p)
{
    uint32_t i;
    if (p->on > 1u || p->ch > 15u)
        return 0;
    for (i = 0; i < PAD_BANKS; i++)
        if (p->base[i] > 127u)
            return 0;
    for (i = 0; i < PAD_N; i++)
        if ((p->cfg[i][0] & 7u) >= NTRK || p->cfg[i][1] > 127u)
            return 0;
    return 1;
}

/* ---- the ISR's side ---- */
#define PADQ 16u
static volatile uint8_t padq[PADQ];                   /* PATTERN hits: the pad (0..63), for the main loop */
static volatile uint32_t padq_w, padq_r;              /* (the ISR writes w, the main loop r) */
static volatile uint8_t pad_setup_on;                 /* PAD SETUP shown: hits pick, PATTERN pads launch nothing */
static volatile uint8_t pad_learn;                    /* LEARN: 1..4 waiting for bank A..D's pad 1, 0 off (main loop) */
static volatile uint8_t pad_heard;                    /* .. the note that came, + 1 (the ISR; the main loop clears it) */
static volatile uint8_t pad_last;                     /* the pad hit last, + 1 (PAD SETUP picks it) */
static volatile uint32_t pad_hits;                    /* hits so far (the screens redraw on one) */
static volatile uint32_t pad_hit_ms[PAD_N];           /* when each pad was hit (PADS lights a SOUND pad a moment) */
static uint8_t pad_held[PAD_N];                       /* a SOUND pad sounding: its note + 1 (its release ends that) */
static uint8_t pad_held_k[PAD_N];                     /* .. on that track */

/* the pad note is (0..63: bank * 16 + pad - 1), -1 none */
static int32_t pad_of(uint32_t note)
{
    uint32_t b;
    for (b = 0; b < PAD_BANKS; b++)
        if (note - pads.base[b] < PAD_PER)
            return (int32_t)(b * PAD_PER + note - pads.base[b]);
    return -1;
}
/* a note of track k from the pads' channel ch (vel 0: its release), as midi_note_event plays one of its track's: the
 * channel's ownership, so a release (or a panic, a route change) ends exactly what it started */
static void pad_note(uint32_t ch, uint32_t k, uint32_t note, uint32_t vel)
{
    midi_channel_t *c = midi_channel(ch);
    track_t *t = &trk[k % NTRK];
    uint32_t id = midi_notes[ch][note] & 0x7Fu;
    if (!vel) {
        if (id)
            midi_release(ch, note);
        return;
    }
    if (id)
        midi_release(ch, note);                       /* (another pad on the same note: it ends, this one plays) */
    if (t != TSEL)
        midi_hint = (uint8_t)(trk_index(t) + 1u);
    c->targets |= (uint8_t)(1u << trk_index(t));
    midi_play(t, ch, note, vel);
    midi_notes[ch][note] = (uint8_t)(trk_index(t) + 1u);
    midi_owners[trk_index(t)]++;
    c->owned[trk_index(t)]++;
}
static void pad_sound_off(uint32_t i)
{
    if (pad_held[i]) {
        pad_note(pads.ch % 16u, pad_held_k[i], pad_held[i] - 1u, 0);
        pad_held[i] = 0;
    }
}
/* a channel message from MIDI IN (USB, TRS): 1 a pad's (done here), 0 any other (midi_event goes on with it) */
static int pad_event(uint32_t st, uint32_t ch, uint32_t d1, uint32_t d2)
{
    uint32_t on = st == 0x90u && d2, i, c;
    int32_t p;
    if (!pads.on || ch != pads.ch % 16u || (st != 0x90u && st != 0x80u))
        return 0;
    if (on && pad_learn) {                            /* (LEARN: the note is the bank's pad 1, pads_poll) */
        if (!pad_heard)
            pad_heard = (uint8_t)(d1 + 1u);
        return 1;
    }
    if ((p = pad_of(d1)) < 0)
        return 0;
    i = (uint32_t)p;
    c = pads.cfg[i][0];
    if (on) {
        pad_last = (uint8_t)(i + 1u);
        pad_hit_ms[i] = fm1_ms;
        pad_hits++;
    }
    if ((c & PAD_SOUND) || pad_held[i]) {             /* SOUND (or one turned PATTERN while held: its release) */
        if (!on) {
            pad_sound_off(i);
            return 1;
        }
        pad_sound_off(i);
        if (!(c & PAD_SOUND))
            return 1;
        pad_held_k[i] = (uint8_t)PAD_TRK(c);
        pad_held[i] = (uint8_t)(pads.cfg[i][1] % 128u + 1u);
        pad_note(ch, pad_held_k[i], pad_held[i] - 1u, PAD_VEL(c) ? PAD_VEL(c) * 8u + 7u : d2);
        return 1;
    }
    if (on && !pad_setup_on && padq_w - padq_r < PADQ) {   /* PATTERN: the main loop launches it (pads_poll) */
        padq[padq_w % PADQ] = (uint8_t)i;
        RING_PUBLISH();
        padq_w++;
    }
    return 1;
}
/* a project's setup in (0: the defaults), its sounding pads ended: with the interrupts off (project.c project_apply) */
static void pads_load(const pads_t *p)
{
    uint32_t i;
    for (i = 0; i < PAD_N; i++)
        pad_sound_off(i);
    if (p)
        pads = *p;
    else
        pads_defaults();
}

/* ---- for the main loop and the screens (pads_bank.c, ui_*.c) ---- */
static void pads_poll(void);                          /* (pads_bank.c, after bank.c) */
static uint32_t pads_playing(uint32_t b);             /* bank b's pad playing (0..15), PAD_PER none */
static uint32_t pads_staged(uint32_t b);              /* .. the one staged to come in, PAD_PER none */
static int pads_stopping(uint32_t b);                 /* .. a STOP staged */
static uint32_t pads_pattern(uint32_t i);             /* pad i's pattern (its entry's), PAT_NONE none, PAT_REST a REST */
static int pads_entry_sound(uint32_t i);              /* .. its entry carries a sound */
