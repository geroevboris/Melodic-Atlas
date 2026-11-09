# Sound-first map: selected CLAP + DSP prototype

Status: **experimental specification only**. No production C++ or filters changed.
Base: `dev/8k-compact-islands-attack-palette`.
Chosen after a user's comparison of three browser prototypes (8K, DSP-only, CLAP+DSP) on the same **1,373 cached samples**.

## Existing browser experiment — exact behavior, not a C++ implementation

- **X:** `clip((ln(max(acousticCentroidHz,70))-ln(70))/(ln(11000)-ln(70)),.01,.99)`. Spectral centroid is frequency-energy focus, NOT fundamental frequency; never override X with pitch or an instrument guess.
- **DSP description:** 12 recorded descriptors: log spectral centroid, sqrt(low/mid/high band ratios), sqrt(flatness), log1p(8*onset), sqrt(sustain), log10(max(.005,attack)), tonal periodicity, pitch stability, tonal window count/3, log1p(active duration). Standardize each feature over the user's library, clip z to [-3,3], multiply by `[.75,1.25,1,1.25,1.15,.90,.95,.95,1.05,.55,.40,.25]`.
- **CLAP:** normalize cached 512D audio vectors; PCA to 48D; scale all PCA dimensions together by their summed variance. Scale all DSP dimensions together by their summed variance. Form `H = concat(0.86*CLAP48, 0.36*DSP12)`.
- **Y:** regress `H` on a cubic spline of X (7 knots) with Ridge(alpha=9), subtract predicted spectral trend, run **scikit-learn 1D t-SNE** (perplexity 35, 850 iterations, seed 29), robustly map 1%/99% percentiles into [0.04,0.96].
- **Color:** 7-means on 2D map coordinates weighted (X, 1.15*Y), reorder clusters from left to right and top to bottom. Colors describe spatial groups, **not** declared musical instrument categories.
- One-shot and loop tabs remain separate in the UI. The proof-of-concept fit Y and colors over all 1,373 cached records.
- Original experiment did NOT solve percussion filtering. Percussion must be audited as a separate pre-map problem. Never interpret the new map as an improved percussion classifier.

## Validation

Technical metrics on 1,373 cached vectors (k=12): new CLAP+DSP layout CLAP neighbor overlap 0.330, DSP neighbor overlap 0.181, hybrid neighbor overlap 0.315, spectral-X Spearman 1.000. These measure geometry against mathematical reference distances, **not perceived timbre similarity**. Need listening evaluation and acceptance criteria before replacing production geometry.

## Engineering constraints and rollout

1. Preserve current 8K branch and cache. Do not modify working 8K or merge 8M classifier experiments into this branch.
2. Browser prototype uses **scikit-learn t-SNE**, but production C++ already embeds `umappp`, **not** t-SNE. Do not falsely claim replacing 1D t-SNE with 1D UMAP reproduces the approved layout. First compare a native 1D alternative on the same saved cache and audition neighbors.
3. One existing audio model only: LAION CLAP, optionally retain DSP-only as a fallback. No Essentia, PANNs, Microsoft CLAP, Python, scikit-learn, or model download in shipping plugin.
4. Replace `ClapColourClusters` + `SoundIslandLayout` with a **single** map geometry/colour step behind an experimental flag only after native-quality validation. Do not remove functional features (preview, pads, DnD, history).
5. Keep UI source labels (Piano, Guitar...) off the map's core semantics. Metadata hints may remain as optional searches; never use uncertain source labels to hide otherwise playable sounds.
6. Preserve stable positions during incremental scans by reusing existing anchors, but measure layout stability on added samples; preserve cache migration and don't silently reinterpret `mapGeometryVersion=7`.
7. For excluded percussion, retain diagnostic status (skip display, never delete audio files); distinguish confidently percussive from uncertain, and test both false positives and false negatives.
8. Success requires: spectral X order, short-range timbral neighbor listening quality, clean refresh stability, safe thread behaviour, predictable filter behavior, and no larger bundled model.

## Decision gate

User judged the browser **CLAP + DSP** arrangement the most logical. This is approval of **that visualization as a direction**, not evidence that a different native projection will sound the same or permission to replace the working plugin blindly.
