# Instrument classification experiment 01 — one CLAP + DSP

**Experimental only.** Base: `dev/8k-compact-islands-attack-palette`. No production C++ source, cache, WAV files, UI or bundled model changed.

## Question
Could a small classifier (kNN, logistic regression, linear SVM, RBF SVM) improve the existing instrument filters using the **single already-shipped LAION CLAP audio encoder** plus inexpensive cached DSP descriptors? No additional neural models.

## Diagnostic setup
- 1,373 valid cached CLAP vectors.
- 40 previous human annotations; 35 found in cache. Four `Unidentified` examples have explicit user comments indicating wind instruments and are assessed as `Winds` (not inferred from filenames).
- All 35 items excluded from 459 weakly supervised training examples extracted from other filenames; ambiguous naming discarded; `Future Bass` is not a bass instrument label.
- Classes: Keys, Synth, Bass, Vocal, Strings, **Winds**, Guitar, FX, Drums.
- This **is not an independent blind test**: 35 annotations have been inspected in prior experiments, and weak name-labels have pack/genre biases.

| Method | diagnostic matching labels |
|---|---:|
| Existing 8K confirmed-label filters | 5/35 (9/35 assigned a category; the others are Unknown) |
| CLAP nearest weakly-labeled examples | 21/35 |
| CLAP logistic regression | 21/35 |
| CLAP linear SVM | 20/35 |
| CLAP RBF SVM | 21/35 |
| CLAP 7-NN | 21/35 |
| **CLAP + DSP logistic regression** | **23/35** |
| CLAP + DSP linear SVM | 21/35 |
| CLAP + DSP RBF SVM | 22/35 |
| CLAP + DSP 7-NN | 20/35 |

For CLAP + DSP logistic regression: FX 9/10, Synth 6/10, Keys 4/5, Winds 1/4, Drums 1/4, Vocal 1/1, Guitar 1/1. **There are zero human-confirmed Bass or Strings examples in this diagnostic set.** Hence this experiment cannot validate those filters.

## Implications
- Changing the estimator alone has **not** produced reliable Winds/Drums classification. Extra heavyweight models are unjustified at present.
- Keep instrument source labels distinct from acoustic traits and from the CLAP map. Avoid forcing one instrument label for a polyphonic loop or unrecognized source.
- Determine drum status independently of musical instrument source; 808-type sounds can have both roles. Existing cache excludes many drums before embedding, so an additional audit of source WAVs is required.
- Hard-negative training (e.g., Rhodes vs Bass) requires verified negative labels. Do not generate them by guessing from names.
- Do not deploy derived or calibrated confidence thresholds without a fresh, separately held-out human-labeled test set.

## Next acceptance gate
A browser-only review manifest (36 new candidates, optionally 36 more, plus up to 12 files missing from the cache) asks a human to **listen without seeing model predictions** and assign multiple source classes and an independent percussion status. Review CSV stays local until user explicitly sends it. Then use a group-disjoint split to compare per-class precision/recall, abstention, percussion false inclusion, and Bas/Keys and Strings/Winds hard negatives. Production filters remain unchanged until this gate passes.
