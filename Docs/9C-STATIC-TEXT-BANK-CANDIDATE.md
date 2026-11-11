# 9C — verified static CLAP text-prototype candidate (not deployed)

## Implementation delivered

- `Source/ClapTextBank9CData.h`: **52** prompt vectors × **512** components, quantized to signed Q15 with a scale per prompt. Actual payload **53,248 bytes** (71,000 Base64 characters), generated directly from the text bank supplied by the user on 11 Nov 2026. The original unquantized SHA-256 is `b0c37d1246dfab6974e14d49324b216d6b360a5ace45e884b518c2e77a5ab6ce`.
- Nine independent candidate classes in bank order: `Keys, Synth, Bass, Vocal, Strings, Winds, Guitar, FX, Drums`. The `Brass` and `Woodwind` prompts are grouped under **Winds**; bowed/orchestral strings remain **Strings**.
- `Source/ClapTextPrototypeCandidate9C.h`: decode static data once, normalize each vector, compare one 512D audio CLAP vector against all 52 text vectors, expose **nine family cosine scores**, strongest prompt per family, top family, second family and score difference. This candidate does **not** set identity, hide drums, affect map coordinates, or alter saved user data.
- `Source/PluginProcessor.cpp`: compile-check include only. No call to candidate scorer, no filter switch.
- `Tools/test_clap_bank_9c.py`: no-dependency validation of 52×512 payload, 9 family indices, reconstruction norms, known prototype components, and separate Winds. Added to GitHub Actions CI. Build workflow updated to bundle native CLAP for branch 9C, exactly like 8M/8P.

## Offline numerical verification

Using the same user-provided Mac bank and 1,373 original cached 512D audio embeddings:

- Every text vector reconstructed from Q15 maintains cosine >= **0.9999999** with its uncompressed original.
- Maximum absolute difference in audio-to-prompt cosine scores: **5.37×10⁻⁶**.
- **0 / 1,373** changed top prompt IDs due to quantization.
- **0 / 1,373** changed top subtype predictions due to quantization.
- These checks establish format fidelity, **not classifier accuracy**.

## Important safety and validity caveats

- Production instrument assignment remains `ClapLibraryClassifier` / existing metadata logic. Existing drum exclusion, 2-second minimum, loop classifier, and colours are unchanged.
- The user-supplied bank and scores have been numerically verified; only **31/39** familiar known-source clips and **7/9** known percussion examples are correctly top-1, with **1/41** false drum positives. This is NOT independent held-out accuracy.
- A top-1 match is always assigned even if every cosine is weak; **never convert these candidate rankings into hard filter verdicts without a source-specific confidence/abstention design and held-out false-positive evaluation**.
- No second neural model, PyTorch, Python, or Hugging Face runtime is required in the plugin itself; the text vectors are static.
- AU/VST3 compile/run success is **not yet established for this branch** merely because the files have been committed. CI needs to run and pass.

## What comes next — 9D

1. Use the static bank scorer in a versioned **shadow-evaluation** path (scanner thread only) so the old category/filter decisions remain intact.
2. Freeze a decision policy separating (A) source-family scores, (B) drum suspicion vs confirmed exclusion, (C) Unknown/Uncertain; preserve manual overrides. Do not fold Winds into Strings.
3. Hold out sample packs by provenance and add meaningful hard negatives: layered musical loops, Rhodes/guitar confusion, bass mistaken as DrumLoop, percussion FX, noisy synth, rhythmically played guitar and ordinary vocal clips. Report per-family precision/recall/abstention and erroneous drum hides separately.
4. Integrate into production filters only once clear improvement is demonstrated on independent data. Keep a rollback mode and cache migration.
5. Treat One-shot/Loop, register X and dot colours as separate, already documented 9A tasks; do not claim they're fixed by this candidate.

**Do not ask the user to install this branch just to see identical filtering.**
