# EMP16 pads

This fork of Skiff lets a 16-pad controller with 4 banks (the EMP16, on its factory settings) play Skiff live:

- **Banks A–D are tracks 1–4.** Pad 1–16 of a bank is that track's list entry A–P (SEQ › SONG).
- **A PATTERN pad** starts its entry's pattern at the launch point (LOOP, BAR, BEAT or NOW, as PERFORM sets it).
  Stopped, it loads at once.
- **The pad that is playing, hit again,** stops its bank at the launch point. Hit it once more before then and the
  stop is cancelled.
- **A SOUND pad** plays a note on a track (any of T1–T6) for as long as you hold it.
- **The 4 sliders** set the level of tracks 1–4: use MIDI LEARN (MENU › MIDI › MIDI LEARN, turn a LEVEL knob, move the
  slider), or the PADS page's knobs.

Nothing on the EMP16 needs changing. Connect it to the FM-1's MIDI IN (TRS) or through a computer over USB.

## The pads' notes

The pads are the notes of MIDI channel 10. Each bank is a run of 16 notes, pad 1 bottom left, then left to right and
row by row up. The EMP16's factory notes are built in:

| Bank | Pad 1 | Pad 16 |
|------|-------|--------|
| A    | 36    | 51     |
| B    | 52    | 67     |
| C    | 68    | 83     |
| D    | 20    | 35     |

If your controller sends other notes, use **LEARN** (below). Other notes and other channels reach Skiff as before.

## SEQ › PADS

Press SEQ until the header says PADS (after SONG).

- Each bank is a 4 × 4 grid, laid out like the pads. A pattern pad shows its pattern number. The one playing is filled
  in its track's colour. The one waiting to start is outlined. A bank that will stop is dimmed and outlined, and its
  head says STOP.
- A SOUND pad shows its drum (BD SD CP CH OH TM RS CB) framed in the colour of the track it plays, and lights when hit.
- **KNOB 1–4:** the level of tracks 1–4 (banks A–D).
- **OCT+: LEARN PADS.** Hit pad 1 of bank A, then B, C and D. The header asks for each. OCT- stops it.

## SEQ › PAD SETUP

The page after PADS. Here a hit on a pad picks it and does not start a pattern (a SOUND pad still sounds).

- **PRESETS** or a hit on the controller: the pad (its bank's head says PAD n).
- **KNOB 1 TYPE:** PATTERN or SOUND.
- **A SOUND pad:** KNOB 2 TRACK (T1–T6), KNOB 3 NOTE (0–127; on a DRUM track 36 KICK, 38 SNARE, 39 CLAP, 42 CL HAT,
  46 OP HAT, 45 TOM, 37 RIM, 56 BELL), KNOB 4 VELOCITY (as played, or fixed).
- **OCT+:** the picked pad's type on the whole bank. A SOUND bank gets the 8 drums in turn, twice.

Which pattern a PATTERN pad plays is set on SEQ › SONG, as for any entry (its PATTERN knob, and SOUND for a sound
that comes in with it).

## Saved with the project

The pad setup is part of the project. A project that never changed it keeps the defaults, and Skiff without this
change loads every project made here (it does not see the pad setup).

## Good to know

- With 4 banks and SOUND pads playing, notes share Skiff's 8 voices (12 with MENU › SYSTEM › CPU › 2 CORES).
- With SONG on, the song moves the tracks on by itself after a pad's pattern.
- Channel 10 is taken by the pads. With MIDI IN set to SEL, notes 20–83 on channel 10 go to the pads, not the selected
  track.

## Building

`tests/pads_test.c` covers the pads (run by `tests/run_tests.sh`). The build is Skiff's: see [BUILDING.md](BUILDING.md),
or the GitHub Actions workflow in `.github/workflows/build.yml`, which builds the `.fwsc` on every push.
