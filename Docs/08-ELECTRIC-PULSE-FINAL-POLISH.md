# Melodic Atlas — Electric Pulse polish / approved UI 08

**Scope:** existing single working branch `dev/9f-grounded-loop-percussion`. Keep
`main` unchanged; do not rewrite CLAP category assignment, musical register
positions, pads, loop/percussion gates, file metadata or user WAVs.

## Agreed changes in this commit

1. Register palette (exact approved swatches, map dots, islands, legend,
   audition history): **LOW #F52C29 (Racing Red), LOW-MID #3C83C2
   (Steel Blue), HIGH-MID #05B183 (Mint Leaf), HIGH #FFBF29 (Amber Gold)**.
   No per-sound brightness based on attack. Electric Pulse accent tones
   update selected controls; the existing layout and audio workflows remain.
2. Three point sizes at about 88% of the earlier prototype: normal radii
   `1.45 / 1.92 / 2.55px` with 62%/26%/12% deterministic proportions
   keyed by file path. NO glow, shadow or fuzzy halo. Selected and hover
   points use a readable, clean outline instead of luminous blur.
3. Discover button is a compact vector **dice icon**. Inactive at rest;
   subtle rollover, short (~170ms) click indication, no persistent active state.
   Each click samples **up to 36 random eligible WAVs** from the complete
   library, ignoring selected instrument/character filters and One-shot/Loop
   tabs. They move in a brief (ca. 250ms) interpolation into a temporary
   compact central cloud. Re-roll chooses a fresh random sample. Island
   boundaries and register axis labels are hidden while in this mode.
   `ALL` or normal filter/tab selection exits and restores the old
   positions, pan, zoom and filter semantics. Random mode never modifies the
   stored CLAP vectors, atlas map positions, user overrides or sample pads.
4. Character filter retains its **CHAR** label, but offers only four
   measured acoustic extremes: `Bright`, `Dark`, `Pluck`, `Sustain`.
   No mood/emotion guessing. Bright/Dark use existing log-frequency
   spectral-centroid estimate (>=0.75 vs <=0.25); Pluck requires
   transient>=0.55 and sustain<=0.35; Sustain requires sustain>=0.70 and
   transient<=0.35. Ambiguous sounds receive no extreme tag.
   Cached DSP audio descriptors are reevaluated on load/Refresh without
   rescan so old Warm/Clean/Noisy selections don't persist.
5. Map Preview and Bank reverb defaults = **0**, for new sessions with no
   saved reverb settings. Existing explicit saved reverb amounts in DAW
   projects are deliberately preserved.
6. While dragging Tune, Main, Map Rev or Bank Rev knobs, temporary precise
   readout replaces the sample name in the map history/info bar and
   returns after ~1.4 s:
   `TUNE +3 st`; `MAIN -4.0 dB`;
   `MAP REV DRY 100% / WET 14%`; `BANK REV ...`.
   Actual DSP reverb is a parallel wet send: `dry=100%` and wet
   contribution scaling is respectively `amount*0.45` (map) and
   `amount*0.48` (pads), so those numeric percentages are truthful.
7. JUCE native popup style: sans-serif 13.5 pt menu typography,
   graphite background, cooler blue Electric Pulse highlight, subtle border
   and top accent line. No new Settings page or theme selector.
8. C++ macOS AU/VST3/Windows builds and a standalone UI contract test
   are part of the same GitHub Action workflow.

## Regression expectations

- All previously supported audio source categories, dynamic subfilters,
  manual `Shift + right-click → Move to...`, pad functions, audio preview,
  hot-relink, 2-second duration gate and drum loop exclusion remain.
- Random discovery is view-only; normal filtering and source coordinates
  remain exactly as before when exited. No new CLAP inference required.
- The low-frequency palette colour is exactly #F52C29, replacing #CE123E.
- Regression tests check the four source colours, three point-size states,
  random cloud not selecting random filters, character four extremes, dry
  reverb defaults and knob readouts.
- A green CI build confirms compile/tests, NOT that the full large
  library has been scanned. Test with a backup of the working plugin,
  then use Refresh and scan the large library as desired.
