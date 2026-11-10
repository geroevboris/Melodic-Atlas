# Melodic Atlas 8P — precise drum-loop names, preliminary 8O diagnosis

## Findings from user-supplied 8O scan

- 1,052 eligible audio candidates after the fixed >=2 s duration gate; all had native CLAP vectors.
- Only **5/1,052** had `Percussive` and `excluded_by_gate=1`; 538 were `Melodic`, 509 `Uncertain`.
- Among **50** matched previously hand-labelled percussion yes/no files, **9** were confirmed percussion and **41** were confirmed non-percussion.
- Existing 8O gate rejected **0/9** confirmed percussion, and falsely rejected **0/41** confirmed non-percussion.
- **5/9** long confirmed percussion were marked `is_loop=0` despite user identifying them as rhythm/percussion, so fixing loop detection alone cannot safely solve the problem.
- 11 previous labelled samples were not present in this scan, plausibly due to the new >=2 s rule and/or prior filtering; no assumption is made about exact removal reason without their original WAV durations.
- Drum reference cosine is NOT a reliable global exclusion threshold. Among confirmed non-percussion are guitar loops with values 0.80–0.90. Some true percussion has values as low as 0.24. Do not lower the existing CLAP cutoff arbitrarily.
- Previous group-CV CLAP percussion model yielded **6/9 detected and 2/41 false positives** on these 50 matched samples. These are retrospective results, NOT production confidence or independently held-out evidence.

## Changes in 8P

Only high-evidence naming cases are added to `PercussionGate::isExplicitDrumFile`: `percloop`, `percs`, `beats`, `drums`, `drumroll`, `cng loop`, `hhsd loop`, `beat fill` and closely equivalent forms. Generic `rhythm`, `groove`, `beat` (singular), `pattern` and `loop` alone are deliberately **not** exclusion triggers.

Offline pattern check on the 8O audit: 16 names matched the proposed patterns, of which two were already excluded. Thus 14 previously admitted long sounds become excluded by name. Among the 50 matched manual labels, three confirmed percussion matched the new patterns and zero confirmed non-percussion matched them. This is a limited test; unlabelled matches must be auditioned for production QA.

This patch does NOT change the ML thresholds, audio model, map geometry, recent per-point attack shading, or >=2 s duration policy.

## Required follow-up

- Build / test AU and VST3; verify no Melodic-Atlas instantiation regression.
- Compare the 14 newly excluded audio files by ear if needed, especially full `beats` files that might include melodic material.
- Replace the tiny maximum-18-drum-example cosine gate with a **validated lightweight CLAP 512D rejection classifier** trained/tested across held-out sample packs, targeting whole drum loops. Do not integrate unvalidated weights; don't add a second large model.
- Preserve `Unknown/Uncertain` as distinct from `Melodic`. Do not interpret `percussionConfidence` or CLAP cosine as a calibrated probability.
- Validate source classifier separately; do not let source tags move the CLAP islands.

No original WAVs are removed. Original cache and user instrument overrides remain unchanged.
