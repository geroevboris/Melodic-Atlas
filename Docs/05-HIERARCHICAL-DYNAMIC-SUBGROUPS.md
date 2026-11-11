# 05 — Hierarchical, conservative dynamic subcategories (11 Nov 2026)

## Scope and evidence

Single existing work branch `dev/9f-grounded-loop-percussion`. Do not make another branch or change `main`.

Reproducible offline script: `refine_hierarchy_05.py` (delivered as an analysis artifact).
Inputs: old `ScannedDatabaseCache_v4.xml`, verified 145-vector CLAP text bank,
03 paraphrase analysis and user's **20 hand-listened** corrections. This cache is NOT the
latest installed 9F cache. The 20 examples have influenced design, so they are
**not an independent accuracy test**.

- 1,050 sounds with CLAP and duration >=2s; 426 existing confirmed instrument
  families and 624 Unknown.
- 03 assigned a broad subtype to 360/426; revised 05 assigns **292/426**,
  abstains for **134/426**.
- On 20 targeted reviewed files: 03 correctly assigned 11; 05 correctly
  assigned 13, made 0 **wrong specific** subtype assignments, and abstained on 7.
  One of the 7 is user-intended broad `Bass` with no subtype. This is on the
  training-informed control list: do not report 13/13 as generalization accuracy.
- 95/426 known-family clips have global extended text-CLAP source labels
  different from their current family. Source disagreements trigger
  **abstention**, never an automatic category move.
- Strong explicit `Piano` cue protects 15 files in the 05 old cache, while
  explicit `Arp` cue describes 19 named sequences. Names are evidence and
  these cases are not credited as sound-only CLAP successes.

## New production implementation (first conservative broad layer)

- `ClapHierarchy05Bank.h`: complete verified `laion/clap-htsat-unfused`
  extended text bank, 145 × 512, quantized Q15 signed LE (~149KB decoded);
  **no second audio model**. Text model runs only for experimental prompt
  generation, not inside installed plugin.
- `ClapHierarchy05.h`: compare original normalized 512D audio CLAP vectors to
  packed 145 reference prompts; first choose broad parent *within a confirmed
  source family*, evaluate absolute cosine, runner-up margin and deterministic
  64-paraphrase consistency. Compare global families only to abstain from
  contradictory assignments, NEVER to move a sample to another source.
- `Hierarchy05Policy.h`: explicit decision reasons (Abstain, StrongAudio,
  StableAudio, SourceConflict, ExplicitPiano, NamedSequence).
- `DynamicSubgroupTags9F.h`: apply a confident broad tag to existing
  filters; when the model abstains, **do not force a narrow tag**. Preserve
  explicitly named Rhodes/processed guitar as secondary tags when their broad
  family is confirmed. Existing pads, original WAV, map, 2s gate, manual
  loop/hidden overrides remain untouched.
- `AtlasTypes.h`: label broad Piano/E.Piano and Foley/Metallic categories
  accurately, add Synth Textures option. Main groups remain stable.
- Pure C++ decision-policy tests and pure Python Q15 bank tests are included
  in GitHub Actions. This document **does not claim CI success until observed**.

## Limitations and follow-up

- This is **content-driven assignment of an adaptive set of available
  pre-defined broad parent groups**. It is NOT fully open-ended automatic
  discovery of any arbitrary new instrument name. The separate CLAP cluster
  discovery experiment is still needed for genuinely novel groups.
- `Synth/Arp` still needs independent audio pattern recognition when
  filename does not say Arp. E.g. manually confirmed
  `130_Bm_Pad_SP_02.wav` is conservatively left as Synth without a
  subtype rather than incorrectly named Pads or Plucks.
- Two `LOFI - Lead` loops are reportedly Piano/E.Piano, but their current
  source family is Synth. We do **not** auto-move their family on one
  expanded CLAP text prediction.
- `flute.wav` was manually identified as Synth Lead, contrary to the
  Strings/Winds source/filename. Again no unsafe automatic source switch.
- No claim of precision on an independent library or source-pack holdout.
  Next verify the CI build, then evaluate actual saved plugin cache and
  listening results from novel source packs before finalizing broad
  subcategories.
- Keep working in the same branch; `main` is not modified.
