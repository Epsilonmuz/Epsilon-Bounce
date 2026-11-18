# Changelog

## 1.6.0 · "Tempo first" (2026-11-18)

Built on 1.5.1. Plugin codes, parameter IDs and sessions are unchanged: 1.5 sessions open as before, and their loops' tempos are read once on load.

**Step 1 is now tempo match: the two loops are brought to one tempo before any groove work**
- Each lane reads its loop's own tempo from the rhythm (onset autocorrelation), not just from the file length, so a sample cut a little long or short, a flip that starts mid-bar, or a loop with a reverb tail still reads right. When the length is a whole number of bars at that tempo, the exact tempo is used.
- Of the equivalent readings (70 / 140 / 280), the one nearest the project tempo wins, so a half-time loop is never stretched to double length. A loop made at the project tempo keeps it exactly, as in 1.5.1.
- Melodies are **stretched to the project tempo with their pitch kept**: a phase vocoder with identity phase locking, one shared phase rotation for both channels (the stereo image stays put), cut into pieces on the loop's own attacks so each attack lands exactly where it belongs, and the first 12 ms of every attack copied untouched (no soft, late hits).
- Drums keep being re-timed hit by hit (the punchiest way to change a drum loop's tempo); a loop's slices are on its own grid and land on the project's.
- The loop is playable as soon as it's cut; the stretch is made in the background and replaces it (a 59 s sample: playable in 1.7 s, stretched in 4.9 s on the Linux test bench).
- When the project tempo changes (and stays put for half a second), a stretched lane is stretched again from its original audio.
- New **TEMPO** strip on each lane: the loop's tempo -> your tempo and what was done. Click the tempo to type it, **½** / **×2** re-read the same rhythm at half or double speed, **Auto** detects it again. **Fit**: AUTO (melodies stretched, drums re-timed), SLICES or STRETCH.
- The panels are numbered for the flow: **1 Tempo -> 2 Pocket -> 3 Feel**. The window is 1160 x 840.
- Learn groove reads a reference loop at its own tempo too (a pocket is only right on the right grid).
- The tempo read, a typed tempo and the Fit mode are saved in the session, so a loop never flips between readings when a project reopens.
- Fix: closing the plugin while a long analysis is running no longer waits for it (the run is told to stop).

**Tests**
- New `BounceTempoTests` (78 checks): tempo found for drum loops at 72-174 BPM and melodies at 80-140 BPM, with tails and odd cuts; stretch keeps exact length, pitch (440 Hz stays 440 Hz), level and stereo image; 90 -> 140 drums: hits within 1 ms of the grid and attacks within 0.7 ms; a 92 BPM melody into 140: every cut note within 0.4 ms, attacks within 3 ms.
- 12 new host-bench checks per sample rate: a 92 BPM melody and 140 BPM drums in a 140 project (melody found, stretched, all 32 notes as tight on the grid as the same melody made at 140), the project moved to 150 (re-stretched), ½, Fit SLICES, Auto, and a session reopened with each loop's tempo and fit. All 157 bench checks pass.

## 1.5.1 · "Tempo-proof" (2026-11-17)

Fix. Plugin codes, parameter IDs and sessions are unchanged.

**Changing the project tempo no longer pulls the melody and drums apart**
- Each loop now remembers the project tempo it was loaded at and is always read on that tempo. Change the project tempo afterwards and both lanes just play faster or slower on the new grid, together.
- Before, every tempo change re-read both loops on the *new* tempo. Speed a project up by up to ~12 % and a loop with a tail (reverb, delay, a little silence) was mistaken for one made at the new tempo, so its grid shrank. The other lane, with a different length, was read differently, and the two drifted apart. Example from the new test: 4-bar drums and an 8-bar melody made at 140, project moved to 150 → drums were read as 4 bars at 150, melody as 8 bars at 134.9.
- Bounce now reads the project tempo even before any loop is loaded, so a new loop is cut on the right grid straight away (before, it was cut on its raw length and only corrected at the first play).
- The made-at tempo is saved in the session. 1.5.0 sessions take the project tempo once when reopened, as before, and keep it from then on.
- Saving the session reserves its memory in one go instead of growing a multi-megabyte buffer step by step, so FL Studio's frequent saves stay quick.

**Tests**
- The audio-thread lag check now gates on the 99.9th-percentile block (< 0.6 ms) plus "no block ever misses its 2.67 ms deadline". The raw worst block swung from 0.08 to 1.7 ms between runs of the same code on shared CI Macs.
- 8 new host-bench checks per sample rate: two loops made at 140 with different tails stay on the grid at 140, 150 and 120 BPM (worst 0.4 ms), are still read as made at 140 after the tempo change, and a session reopened at 150 keeps that.

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
