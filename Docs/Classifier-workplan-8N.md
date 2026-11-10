# Melodic Atlas 8N — minimum duration and classifier validation plan

Base: `dev/8m-clap-instrument-validation` (itself based on 8K). Branch `dev/8l-filter-explain-manual-tags-island-focus` has separate UI changes and is **not** merged here.

## Agreed product decisions
- Minimum whole-file audio duration: **2.0 seconds inclusive**. Exclude files shorter than 2.0 s from both One-shots and Loops, before expensive CLAP analysis.
- Do **not** delete WAV/audio files or alter samples assigned to pads. An existing cache can contain old short entries; the map view must hide them before Refresh.
- Keep the independent percussion gate for long drum loops and other percussive sounds.
- One local CLAP model only; no new paid or separate heavy model.
- Keep CLAP/UMAP + DSP spatial geometry independent from instrument names and predictions.

## 8N code changes
- `Source/AtlasDurationPolicy.h`: centralized minimum duration predicate.
- `Source/PluginProcessor.cpp`: lightweight duration check by JUCE reader metadata before CLAP; short files skipped from scanning and drum reference CLAP embeddings.
- `Source/MapCanvasGL.h`: the same predicate hides old cached entries and excluded sounds on the map.
- `Tools/test_duration_policy.cpp`: 1.99999 s rejected; 2.0 s accepted; invalid durations rejected.
- `.github/workflows/build.yml`: 8N uses native CLAP packaging like 8M; C++ boundary test added to smoke job.

## Before acceptance
1. Confirm macOS AU/VST3 build and loading, including native CLAP, on this branch. CI status is **not** proof of plugin runtime function.
2. Test existing XML cache: all WAVs shorter than 2.0 s disappear from map on load. After Refresh, they are not analyzed by CLAP.
3. Verify existing Ableton projects/pads can still play previously assigned short samples. Test with a saved project, before/after scan.
4. With manually reviewed WAVs, run duration audit and count removed `percussion=yes` and `percussion=no`. Rule is fixed by the user; this audit quantifies consequences, not a threshold optimization.
5. Run `Tools/Run-CLAP-8M.command` on the actual Mac and inspect `native_vs_official.json`. Correct ONNX/HF audio frontend mismatch before tuning classifiers.
6. Develop a held-out-by-pack CLAP-based multi-label source classifier, independent percussion detector with `Yes/No/Uncertain`, and `Unknown` fallback. Preserve manually assigned tags and do not force one source label.
7. Later reconcile the 8L UI/manual-tag branch carefully rather than overwriting or merging without tests. Character tags and sound islands are separate validation stages.

Do not report whole-library accuracy from the hand-picked 84 review records.
