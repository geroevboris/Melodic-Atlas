# Melodic Atlas — 09: MIDI continuity, Random third mode, rounder layout

Implementation in the **same** branch `dev/9f-grounded-loop-percussion`.
No modifications to `main`, CLAP classification, existing audio files,
dynamic taxonomy, user correction cache or pad assignments.

## MIDI voice continuity

Original bug: the editor calls `releasePadFromUI(-1)` in mouse-up and
destructor. That function released **every** synth voice, including held
MIDI notes. This also explained stuck/aborted playback during ordinary
filter and tab changes or GUI reopening.

Each synth voice now stores `triggeredByUI`. A mouse click on a pad
creates a UI-owned voice; all host MIDI-created voices are MIDI-owned.
`releasePadFromUI` only releases UI-owned voices. MIDI Note Off still
releases MIDI-owned voices. The renderer continues without requiring the
editor window to exist. Do not replace this with a global "all notes off"
on any UI transition.

The bank still has a finite 32-voice limit; normal voice stealing and
actual MIDI Note Off/host stop may still end notes. A host that sends
Note Off when a clip ends is expected to stop sustained playback.
Adjusting a sample's parameters while audio plays may alter its sound,
but must not cause an involuntary note-off.

## Random as a third navigation mode

In the main header: `ONE-SHOTS | LOOPS | [dice]`. Pressing dice replaces
the two main tabs with `< RANDOM` while the die remains available for
rerolls. Clicking `< RANDOM` returns to the exact previous Loops or
One-shots selection; category and character filters are never changed
by the dice. Filtering or tab switching can leave Random as well.

Each reroll chooses up to 200 distinct, currently eligible sounds from
the entire indexed library (both tabs, every instrument family) excluding
only user-hidden samples, short samples and rejected percussion.
The view draws 60 at overview, about 100 at 1.5x, 140 at 2x, 180 at 3x,
and at most 200 at 4x+. A single shuffled pool is sampled once, then
incrementally disclosed at zoom levels; previous points do not change
position on zoom. The cloud has deliberately no island hulls or musical
frequency/character axes.

On exit, the currently shown Random points animate from their central
cloud positions back to their original individual atlas positions over
roughly 300ms. Only then are the original islands and filtered marker
subset shown again. Stored CLAP/UMAP coordinates, pan and pre-Random
zoom and filter settings remain unchanged.

## Shape/height of ordinary sound islands

Display-only map transform: x = .5 + .86*(x-.5),
y = .5 + 1.38*(y-.5), both clipped to [0,1]. This intentionally uses
more vertical map space and makes islands less horizontally stretched.
The transform applies consistently to dots, island outlines, semantic
zoom collision checks, point hit-testing and Random return targets.
Underlying spectral ordering (left=low/right=high) is preserved;
nothing is stored to or read from new map cache schema. Thus **no
full-library re-analysis is required** to try this layout.

## Regression checks and test procedure

CI includes the existing 08 tests (updated for 200-dot dice) and 09
navigation contracts, plus macOS AU/VST3 and Windows builds. It is not
a substitute for an Ableton host test.

1. In Ableton, sustain a MIDI note or loop with a long sample, then tweak
   pad tuning/volume/ADSR, select main categories, switch One-shots/Loops,
   close and reopen the plugin editor. Note should survive UI actions
   until the actual MIDI Note Off or sample/voice completion.
2. Click die in main header and verify `< RANDOM`, ~60 markers, no
   islands, and no change to the underlying current instrument filter.
   Click zoom +/- and reach 200 at 4x. Roll again. Click `< RANDOM`
   and verify animated scattering plus previous filters/zoom restored.
3. Confirm the normal atlas is taller and rounder in both tabs and that
   clicking/scrubbing markers still previews the correct files.
4. Test on a large library as planned, watching GUI responsiveness and
   memory while rerolling 200 marker pools.

Revert the single commit if the host check reveals instability rather
than continuing to change otherwise stable CLAP and audio code.
