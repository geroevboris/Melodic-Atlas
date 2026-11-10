# Melodic Atlas 9A — единая архитектура поиска (design baseline)

**Status: design-only. No new classification logic is integrated in this branch yet.**
Parent: `dev/8p-explicit-drum-loop-gate`. The old working builds and project/pad state remain untouched.
This document supersedes the incremental threshold-based approach of 8N/8O/8P for any further classifier work.

## What was actually observed

- The saved v7 cache contains 1,373 records; 805 are `Unidentified`, 14 `Ambiguous`, 554 `Identified`. After the minimum-2-second policy, 624 of 1,050 old cached records have no confirmed category (about 59%). This is not a live accuracy measurement of 8P.
- Among 95 records identified as Bass in that previous cache, 23 were beyond X=0.5. E.g. `Øneheart - Neurobass - F` at X=0.896 with 0 tonal windows and spectral centroid ~4,559 Hz. This illustrates the register-axis failure; v8 may reflow coordinates.
- The 8O `PercussionAudit_8O.csv` contained 1,052 duration-eligible candidates, but only five were marked percussive. None of the nine manual confirmed percussion matches in that audit were rejected.
- 8M documents a separate controlled-label baseline: old classifier strict agreement 16/40 on a hand-selected blind review; previous model bake-off instrument Top-1 results are not interchangeable with deployment accuracy.
- `ClapLibraryClassifier.h` propagates metadata labels from up to the first 32 exemplars per family, requiring similarity >0.80 and margin >0.065. It is not a validated instrument recognizer.
- `LoopStructureAnalyzer.h` uses filename/folder tokens, BPM/grid and onset heuristics; it is not a robust one-shot/phrase classifier. The percussion gate currently uses `isLoop` to decide one of its rules, so both errors reinforce one another.
- `SoundIslandLayout::frequencyPosition` only makes the fundamental dominant when pitch windows >=2, periodicity >=0.65, stability >=0.58; distorted bass or moving basslines often fail the guard, leaving a centroid-biased X.
- `getSpectralRegionColour` currently darkens/lightens one of four hues using attack. The user rejects mixed dot brightness and wants visually coherent colour/hue.

## Non-negotiable principles

1. Exactly one bundled heavy audio model: existing native CLAP. No PANNs, Essentia, or secondary CLAP runtime.
2. A file with `durationSec < 2.0` is excluded from discovery before CLAP; never delete the file. Previously used pads/saved projects keep referencing their WAV.
3. Keep **source identity**, **one-shot vs loop structure**, **percussion dominance**, **sound character**, **2D spatial position** and **display colour** as independent outputs. No classifier label may silently determine another classifier's ground truth.
4. Filename and narrowly scoped category-folder labels can be evidence and trusted *user* corrections remain authoritative; pack names are weak, not sufficient proof.
5. Multiple source labels may be valid for layered loops, including `Strings` and `Winds` as separate hypotheses. `Unknown` is a healthy discoverable state, not a reason to lose a sample from ALL or Character.
6. Every automatic exclusion or label needs an observable reason and evidence version. Saved manual overrides take priority.
7. Preserve current CLAP embeddings, sample-packs, preview, pads, drag-and-drop, zoom/pan and library cache; migrate with versioned fields and a reversible feature flag. Do not casually reset UMAP or overwrite user corrections.

## Proposed pipeline

```
WAV metadata -> hard duration check >=2s
             -> cached audio + CLAP vector (scanner thread)
             -> independent measurements:
                A. dominant-percussion score (+ explicit metadata)
                B. structure score (one-shot / musical sequence / uncertain)
                C. per-family source scores (multi-label / unknown)
                D. timbre/attack/tonal-register DSP features
             -> deterministic eligibility policy (percussion gate, manual hide)
             -> filter index (Source, Character, Structure, Unknown)
             -> CLAP similarity + register/attack map
             -> high-contrast categorical hue / legible dots
```

## A. Instrument family (do this before claiming useful category filters)

- Stop using `SoundIdentityAnalyzer` metadata verdicts as default category truth. Use them as optional weak positive evidence, never as negative examples.
- Compare a tiny supervised classifier **on cached 512D CLAP** against the experimental CLAP text-prototype approach (same embedded checkpoint only). No extra inference model in release.
- Eight independent source heads: Keys, Synth, Bass, Vocal, Strings, Winds, Guitar, FX; a sample can have >1 positive head. Unknown if none cleared calibrated per-class evidence.
- Classifier learns from verified human labels and carefully vetted additional examples; use grouped, held-out sample packs to prevent nearly-duplicate leakage. Never tune and report on the same small review set.
- Show class-wise precision, recall, abstention (unknown), multilabel coverage and erroneous inclusions, separately for loops and one-shots.

## B. Percussion (does not depend on loop classification)

- Detect **percussion-dominant** material, particularly whole drum/perc loops, not merely whether one transient exists.
- Same cached CLAP vector -> small regularized percussion rejector. Train with drum-loop positives and hard negatives: rhythmic guitar/synth/bass, mallets, vocal chops, FX, noisy melodic sequences.
- Keep conservative positive/negative/uncertain states. Exact explicit drum metadata can be separate high-precision exclusion. User override wins.
- Avoid current max-18-reference/max-cosine cutoffs as production verdict. Do not add more filename substring hacks.
- Measure recall of labelled drum loops **and** the false removal rate among manually confirmed non-drum audio. Report both, not one headline accuracy.

## C. One-shots vs loops

- Decouple `isLoop` classification from percussion prediction. Preserve `loopUserOverride` and saved tab state.
- Use independently measured onset sequence, envelope repetition, changes in pitch, and length-to-tempo-grid as evidence; tagged file/folder names are supporting evidence only.
- Do not infer a loop from duration, BPM, or a handful of transient peaks alone. A long sustained note/reverb tail remains a one-shot.
- Allow `Uncertain` and a visible user correction. Do not write a confident binary answer until held-out listening audit validates both directions.

## D. Map X/Y — match musical intuition

- X = perceived **tonal register** low-to-high, not spectral brightness. Implement a robust multi-window low-frequency harmonic/f0 estimator (bass notes may change; global pitch **stability** must not be required to find a bass register). Use meaningful harmonic-salience / low-frequency-energy fallback; never let noisy upper harmonics alone push an obviously low-register bass to the far right.
- Independently keep `brightness`/flatness as Character scores, not synonymous with map X.
- Y keeps the currently established attack-time interpretation unless listening validation suggests a better dimension.
- CLAP defines similarity neighborhoods/island membership inside those axes, not instrument-family locations. Keep coordinates reasonably stable on refresh.
- Evaluate individual named bass and high-register examples plus within-neighborhood listening, not just scatter appearance.

## E. Colours: undo the visual regression

- Default to **flat, equally legible colour per coherent region/island**, not changing dot luminance by attack. A faint dot must never be mistaken for a filtered-out or lower-confidence sample.
- Keep the original high-contrast monochromatic/island look as the immediate safe baseline. Only trial **hue changes** at roughly constant perceptual lightness/saturation if they represent one independently validated timbre dimension.
- Do not redundantly encode Y attack as lightness. Filtered-out samples disappear entirely, not dim.
- A single consistent colour legend; no accidental unrelated palette when selecting a filter.

## Acceptance gates — before asking user to install another alpha

1. Fixed annotated 84-WAV benchmark plus a separate grouped test split; report full confusion matrices, coverage and error files. If a class lacks enough human truth, state that and defer its accuracy claim.
2. Before changing model thresholds, numerical native CLAP vs official Hugging Face audio parity must be measured on the same WAV inputs. If the frontend is wrong, fix it first.
3. Percussion: clear improvement on **held-out** long drum loops, with explicit count of wrongfully hidden melodic loops. No silent removal of original WAVs.
4. One-shot/loop: independently audited short list of known positive and negative examples including ambiguous sustained and long FX; provide false-loop and false-one-shot counts.
5. X-axis: low-register samples from the manually reviewed examples have low X and show stable left-to-right ordering even when heavily saturated. Report counterexamples.
6. Colours: all points equally visible; no attack-dependent light/dark alternation. Only accept hue experiments if actually distinguishable and meaningfully tied to sound.
7. Project regression: native AU/VST3 builds; reopen saved projects/pads; Refresh stable; no UI/performance regression; old cache compatible.

## Delivery policy

Do not ask the user to repeatedly test cosmetic or micro-threshold commits. Work on one unified experimental branch with offline benchmarks; deliver **one** properly tested alpha after the core stages are integrated, then collect targeted listening corrections. All claims should say which source/version produced the metric.
