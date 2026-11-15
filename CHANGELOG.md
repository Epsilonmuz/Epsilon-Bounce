# Changelog

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
