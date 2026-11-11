# 9A: Offline baseline from 84 manually reviewed WAVs

Status: **report only — no plugin classifier code or model weights changed**.
Cache: prior `ScannedDatabaseCache_v4.xml` (1,373 saved samples). Review: 84 annotated WAVs.

- 12/84 have no matching cached native embedding.
- 13/84 cached clips fall below the new hard minimum of 2.0 seconds.
- Remaining: **59** labelled clips with cached CLAP; **50** binary percussion annotations (9 yes / 41 no); **39** definite non-percussion audio files with instrument labels.
- Old cached percussion gate: **0/9** percussion caught and **0/41** legitimate clips wrongly rejected.
- One exploratory logistic model on the existing CLAP 512-D vectors, 3-fold approximate filename-family group split: **6/9** percussion caught, but **4/41** legitimate clips wrongly rejected. Its four false positives include two trumpet loops, one bass and one instrumental loop. Unsafe for unconditional hiding.
- Multi-label class heads over 39 known non-percussion examples: Keys 3/5, Synth 7/11, Bass 4/5, Vocal 0/4, Strings 4/4, Guitar 0/6, FX 1/5. **Winds unmeasurable** on the positive examples: all share one approximate origin group, and training on other groups contains no positive Winds labels.
- The train/test grouping was **inferred heuristically from filenames**, not actual confirmed independent sample packs. All accuracy is exploratory; these metrics MUST NOT be claimed as robust cross-pack generalization or used to ship a model.
- No low-level CLAP vs Hugging Face numerical parity was checked during this baseline, so the first technical gate remains open.

Conclusion: simple logistic heads and the current 84 hand-picked annotations **cannot** by themselves deliver the promised category reliability. Current code should stay unchanged; prioritize numerical frontend parity, more independent/hard-negative labels (especially drum loops, trumpet, vocal, guitar, reeds), then repeat frozen held-out metrics and check calibration. Separately test One-shot/Loop recognition and register-axis accuracy. Do not make confidence categories from high-frequency brightness.

The reproducible read-only standalone benchmark and full per-file reports were delivered as `Atlas_9A_Classifier_Offline_Results.zip` in the conversation; their file bytes are not part of this GitHub commit.
