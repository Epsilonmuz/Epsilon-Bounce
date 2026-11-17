# Changelog

## 1.5.0 · "Two lanes" (2026-11-16)

Built on 1.4.0. Plugin codes and existing parameter IDs are unchanged; 1.3 / 1.4 sessions open as before (their loop is the melody lane).

**Melody + drums, each draggable in and out**
- Bounce now holds two loops: a **MELODY** lane (melody, sample, chops) and a **DRUMS** lane (drums or percussion). Drop a file on a lane, or use Load melody / Load drums (the drums button still takes a dropped file).
- Drag a lane, or its **Drag out** button, into the DAW to take that loop out as its own WAV. Files are named for what was done: "locked to <drums>", "original feel", or the groove name. Both drag-outs pre-render in the background on hover, as in 1.4.
- **Pocket** switch, for when both lanes are loaded:
  - **DRUMS LEAD** (default): the drums play with their own feel, untouched; the melody is locked into their pocket (the 1.4 lock: Tighten 100 %, the drums' microtiming at 100 %, nothing added). Markers along the top of the melody lane show where the drums hit.
  - **MELODY LEADS**: the melody keeps its feel; the drums are locked to it.
  - **GROOVE**: both lanes ride the groove / preset you pick.
- While a lane leads, the groove picker and Amount / Swing / Tighten / Humanize / Feel are dimmed (the lock replaces them); Accent still applies.
- Per-lane **Level** (fully down = off, e.g. when your drums already play in the DAW) and **Clear**.
- Both loops and the Pocket setting are saved in the session, each compressed once in the background (the 1.4 lag fix applies to both).
- The window is 1160 x 780.

**Tests**
- 17 new host-bench checks per sample rate: both link modes land the follower on the leader's pocket to within 0.04 of a 16th, the leader is untouched, per-lane drag-out, session round-trip with both loops, Clear.

## 1.4.0 · "No lag, any loop" (2026-11-15)

Reviewed by a senior engineer, a UI specialist, an FL Studio producer, a product strategist and a DSP specialist. Plugin codes (`EpsS` / `EpBn`) and parameter IDs are unchanged, so 1.3 sessions open as before.

**Lag fixed**
- FL Studio saves plugin state often (undo history, autosave). Bounce re-compressed the whole loop every time: up to a quarter-second freeze per save with a long sample. The loop is now compressed once, in the background, and saved in a binary format: 1.5 ms per save.
- Melody analysis is about 2.5× faster (exact FFT twiddles, chroma computed where it's needed), runs off the UI thread, and a run made out of date by a knob that's still moving is dropped instead of finished.
- The grid is opaque: playback frames repaint only the grid, not the panel behind it.
- Drag out renders in the background when the mouse reaches the button, so the drag starts at once.

**Any melody, any drums, in the pocket**
- Drums or melody is detected on load, and the cut mode is picked for you (GRID stays a manual choice).
- Match drums now *locks*: Tighten 100 %, Amount 100 %, no extra swing, humanize or feel. Your loop plays in the drums' pocket, nothing else.
- Drop your drum loop straight onto the **Match drums** button.
- Loops are read on the project tempo when they were made in it: a reverb tail or trailing silence no longer drifts the grid (up to half a 16th by bar 4 before), and the tail isn't cut as a hit. A sample at another tempo keeps its own grid.
- Loops up to 32 bars. A 16-bar melody at 140 was read as 8 bars at 70 before, and played back at the wrong speed.
- Note starts are found when the previous note is still ringing (legato melodies, hats over an 808). They were placed up to 10 ms early.
- Auto-align anchors on beats 1 and 3, so a heavily swung loop or a laid-back snare is no longer "corrected" as if it were an offset. This also made Match drums learn the swing exactly.
- Messages (what was detected, what it locked to, problems) appear in the footer instead of over the Cut on switch.

**Tests**
- 13 new engine checks and a lag profile in the bench that fails if any of the above regresses. pluginval passes at strictness 10.
