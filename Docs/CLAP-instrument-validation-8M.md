# 8M — CLAP instrument-validation track (experimental)

Base: \`dev/8k-compact-islands-attack-palette\`. The 8L filter/UI prototype is deliberately **not** merged.

## What was verified

We recovered the previous 1D experiment's 40 blinded predictions and the user's 40 completed listening labels. Comparison by **both ID and filename**:

- Strict single-label category agreement: **16/40 (40.0%)**.
- Confidence cutoffs did not rescue precision: cosine >= 0.30 retained 31/40, 15 matched (48.4%). At >= 0.50: 6/40 retained, 3 matched (50.0%).
- Per predicted category: Bass 0/5, Drums 4/11, FX 3/4, Guitar 1/4, Keys 4/4, Strings 0/4, Synth 3/4, Vocal 1/4.
- The earlier "7/7" examples were involved in prompt tuning and are not independent validation.
- The 40 files were selected *stratified by model prediction*, not uniformly sampled from the full library. No claim of full-library accuracy is justified.
- Four labels were "Unidentified", including **brass/woodwinds** cases because the old listening UI offered no dedicated source category. This also contaminates strict accuracy.

The v8K code currently uses a native audio-only ONNX CLAP encoder and a separate nearest-labelled-audio exemplar classifier. It does **not** use CLAP's text encoder or precomputed instrument prompt vectors. Its audio log-mel input implementation explicitly states it has not been numerically checked against the original Hugging Face feature extractor. This is a deployment risk, not yet a proven mismatch.

## What is implemented here

- \`Tools/evaluate_clap_blind.py\`: reproducible 40-file baseline; fails closed on mismatched IDs/filenames. Dependency-free.
- \`Tools/clap_zero_shot_experiment.py\`: retrieves **already computed 512D embeddings** from the user's existing \`ScannedDatabaseCache_v4.xml\`; computes text embeddings with the **same** \`laion/clap-htsat-unfused\` checkpoint; reports raw per-subtype similarities and ranks; optionally compares cached native vectors against original Hugging Face audio inference for a small number of files.
- \`Tools/test_clap_validation.py\`: standard-library tests for XML/base64 and evaluation correctness.

No new model is required. No source audio is altered. No sample is automatically assigned to an instrument or removed from a filter in this experimental stage.

## Why this phase must happen before changing the filters

The current audio encoder consumes hand-written 48 kHz log-mel features. If these differ significantly from the reference extractor, multiplying the cached embeddings by correct text embeddings cannot reliably recover instrument labels. The optional \`--verify-native\` measurement tests that prerequisite.

Even with high encoder agreement, raw CLAP cosine is NOT a calibrated probability; comparisons between subtypes are prompt-sensitive, and polyphonic loops can contain more than one instrument. A single forced label is not the production target. Final filters should take per-category calibrated evidence and allow "unknown" without losing discoverability.

## Reproduction on the Mac with the existing plugin scan cache

\`\`\`bash
python3 Tools/clap_zero_shot_experiment.py \
  --output "$HOME/Desktop/Atlas-CLAP-8M" \
  --verify-native 8
\`\`\`

Uses the default macOS cache at \`~/Library/Application Support/MelodicAtlas/ScannedDatabaseCache_v4.xml\`, not the WAV folder. Verification reopens only the first eight cache-linked files; without \`--verify-native\` it does not read any WAV files.

Dependencies for the experimental command: \`numpy\`, \`torch\`, \`transformers\`; optional native reference verification also requires \`soundfile\` and \`scipy\`. The model may need an initial Hugging Face download. CLAP already exists as a licensed offline component; **no new commercial model or API** is introduced.

Outputs: \`clap_subtype_scores.csv\`, \`clap_text_bank.json\`, \`native_vs_official.json\` (optional), \`summary.json\`. The bank is an experimental artifact, **not automatically deployed as production truth**.

## Production integration gates

1. Confirm cached native audio vectors align numerically with official HF audio embeddings on the *same audio*. Investigate discrepant frontend parameters before interpreting prototype scores.
2. Verify on a held-out human-labelled set and measure per-family precision and recall, separately for loops and one-shots, along with fraction abstained; keep percussion false inclusion rate as a first-class test.
3. Define family and subtype thresholds using validation data (not arbitrary cosine cutoffs); avoid evaluating on the same samples used to tune prompts.
4. Support multiple independent instrument sources for polyphonic loops rather than a single forced winner. Keep instrument evidence separate from acoustic map and character tags.
5. Only then gate/replace the current label-exemplar classifier. Preserve CLAP islands, coordinates, previews, pads, right click and existing user data.

**Status:** Baseline evaluation performed. New zero-shot prototype code added and format unit tests provided. Audio-text inference on the user's 1500 actual WAV files and native-vs-HF parity have not been executed from this remote environment. The path \`/Users/Boris/Desktop/Test sounds\` is on the user's Mac, not mounted here.
