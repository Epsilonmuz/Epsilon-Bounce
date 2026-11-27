# Pocket research: modern DSP for locking a melody into a drum loop

Epsilon Bounce 2.0 · Epsilon Sounds · November 2026

The goal: drop any melody or flipped sample and any drum loop, and have them sit **in the same pocket**, the way a good producer would line them up by ear. This brief covers what the research says, what Bounce 2.0 uses, and what's left for later.

## 1. What "the pocket" is (and isn't)

- **Microtiming matters, but more isn't better.** In listening tests with expert drum-and-bass recordings (swing and funk), listeners rated the original microtiming and a fully quantised version *equally* high, and rated exaggerated microtiming lower (Senn et al., *Frontiers in Psychology*, 2016). Jazz trio research found listeners preferred asynchronies reduced to under ~19 ms over both the original and fully quantised timing.
  → **Bounce:** Depth stops at 100 % of the leader's feel. At 0 % the follower is quantised, which is also a valid, good-sounding result.
- **Musicians lean per role.** Drummers place snares, hats and kicks systematically early or late to make a timing "feel" (Câmara et al., *Music Perception*). A pocket is a per-position pattern, not one swing number.
  → **Bounce:** the leader's pocket is kept per position in the bar, with level-weighted averaging so a flam counts as its loudest hit.

## 2. Where a note "is": perceptual centres

- A sound is perceived to land at its **P-centre**, not at its acoustic onset. Attack (rise time) and duration are the main cues: fast, short sounds are heard right at the onset, while slow, long sounds are heard later and less precisely ("beat bins") (Danielsen et al., *J. Exp. Psychology: HPP* 2019; London et al., *Attention, Perception & Psychophysics* 2019).
- For saxophone, articulation and dynamics both move the perceived attack time (Bechtold & Senn, *Frontiers in Psychology* 2018).
  → **Bounce:** each note's heard moment is estimated as the point where its envelope reaches half its rise (capped at 40 ms). Heard moments are lined up, melody to drums. That's why a pad now starts *before* the snare it belongs to.

## 3. Finding notes in a melody: SuperFlux

- Plain spectral flux fires on vibrato and slides. **SuperFlux** (Böck & Widmer, DAFx 2013) takes the flux on a log-frequency filterbank and compares each frame with a frequency-max-filtered frame from 2 hops back. That cuts false detections by up to 60 % on vibrato-heavy music without missing real notes.
  → **Bounce:** SuperFlux at 200 frames/s, 24 bands/octave, with the paper's peak-picking windows, plus a slow 40 ms flux to catch pads and swells. Attack positions are refined to the sample in two bands (full band and above 2 kHz, so a hat under an 808 tail is still found).

## 4. Moving audio without wrecking it: time-scale modification

- Phase vocoders smear transients; time-domain overlap-add keeps transients but warbles tones. **Harmonic-percussive TSM** (Driedger, Müller & Ewert, *IEEE SPL* 2014) separates the signal with median filtering (FitzGerald, DAFx 2010) and stretches the harmonic part with a phase vocoder and the percussive part with OLA. A 2016 review by Driedger & Müller covers the field.
- Fraunhofer's transient-handling patents describe the same idea differently: cut out the transient, stretch the rest, and put the transient back untouched.
  → **Bounce:** the melody is warped piecewise between its notes using the 1.6 phase vocoder (identity phase locking, stereo-coherent). Each note's first 12 ms is copied untouched (the "re-insert the transient" idea), and pieces join with 3 ms raised-cosine crossfades. Moves are a few ms, so stretch factors stay near 1.

## 5. Chords: harmonic change detection

- **HCDF** (Harte, Sandler & Gasser 2006) detects chord changes as peaks in the distance between the tonal content just before and just after each moment (tonal centroid / Tonnetz space). Later work showed the choice of features and distance matters a lot (ICME 2015; Ramoneda 2020).
  → **Epsilon Chop 0.7:** CHORD mode already used a before/after harmonic distance. 0.7 adds the same comparison at a fine time scale around each change (23 ms frames every 1.5 ms: "does this sound more like the old chord or the new one?") and cuts at the decisive crossing. Chord changes with no hit on them are now cut within 5 ms instead of ~40 ms late.

## 6. Beat tracking: where neural models stand

- The state of the art is transformer-based: **Beat This!** (Foscarin, Schlüter & Widmer, ISMIR 2024) tracks beats and downbeats without the DBN post-processing older systems needed.
- Bounce doesn't ship a neural model yet. Loops are short and already tempo-matched, so the classic onset + tempo pipeline is reliable and runs in milliseconds with nothing extra to install. Downbeat finding is the place a neural model would help most (for example the ~41 MB GGUF conversion of Beat This!).

## 7. What's next

1. **HPSS before the warp:** when a flipped sample has drums baked in, stretch its harmonic part with the phase vocoder and its drums with OLA (Driedger 2014).
2. **Per-bar pocket:** keep the leader's microtiming bar by bar (a drummer's fill, a 2-bar hat phrase) instead of one averaged bar.
3. **Neural downbeats** (Beat This!) to find bar 1 in long flips.
4. **P-centre model per instrument class:** the research shows frequency and duration matter too, not just attack.

## Sources

- Senn, O. et al. (2016). The Effect of Expert Performance Microtiming on Listeners' Experience of Groove in Swing or Funk Music. *Frontiers in Psychology*.
- Danielsen, A. et al. (2019). Where is the beat in that note? *J. Exp. Psychology: Human Perception and Performance* 45(3). doi:10.1037/xhp0000611
- London, J. et al. (2019). A comparison of methods for investigating the perceptual center of musical sounds. *Attention, Perception & Psychophysics*.
- Bechtold, T. A. & Senn, O. (2018). Articulation and Dynamics Influence the Perceptual Attack Time of Saxophone Sounds. *Frontiers in Psychology*. doi:10.3389/fpsyg.2018.01692
- Câmara, G. S. et al. Timing and sound in drumming performance (timing styles). *Music Perception*.
- Böck, S. & Widmer, G. (2013). Maximum Filter Vibrato Suppression for Onset Detection. DAFx-13.
- FitzGerald, D. (2010). Harmonic/Percussive Separation using Median Filtering. DAFx-10.
- Driedger, J., Müller, M. & Ewert, S. (2014). Improving Time-Scale Modification of Music Signals using Harmonic-Percussive Separation. *IEEE Signal Processing Letters* 21(1).
- Driedger, J. & Müller, M. (2016). A Review of Time-Scale Modification of Music Signals. *Applied Sciences*.
- Harte, C., Sandler, M. & Gasser, M. (2006). Detecting Harmonic Change in Musical Audio.
- Foscarin, F., Schlüter, J. & Widmer, G. (2024). Beat this! Accurate beat tracking without DBN postprocessing. ISMIR 2024.
