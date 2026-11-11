# 06 — Recover MAIN instrument source families from Unknown (not a subtype patch)

Updated priority: main families before refining dynamic subgroups. Existing source
filters hide every `IdentityState != Identified` record, regardless of
whether its subtype is recognized. 05 only upgraded already-identified
sounds; it could not fix the majority of the user's library.

## Implemented in the SAME WORK BRANCH

- `Source/SoundIdentityAnalyzer.h`: explicit word-boundary tokens
  `Impact` / `Impacts` → FX. This rescues named cinematic effects even
  when a transient-only text-CLAP prediction says Kick. Existing drum filename
  exclusions are left untouched, and real impact WAV files are never deleted.
- `Source/InstrumentSubgroupTags.h` and `DynamicSubgroupTags9F.h`: keep
  explicit `Impact/Impacts` in the FX → `Impacts & Hits` subfilter even if
  CLAP cannot give it a convincing audio subtype.
- `Source/ClapHierarchy05.h`: expose uncalibrated **main-family scores**
  from existing 145 original text prompts; separate Strings and Winds text
  descriptions are folded into existing Strings/Winds family 4.
- `Source/PrimaryFamilyPolicy06.h`: text-family top score >= 0.30,
  separation >=0.05, reject a strongly dominant Drums score, and demand
  at least three agreeing named CLAP neighbours among the top eight
  above 0.43 cosine; agreement must be the majority, and the strongest
  opponent may not outscore the strongest supporter.
- `Source/ClapPrimaryRecovery06.h`: implement recovery on scanner /
  **Refresh worker** only. Seed only native audio CLAP examples with
  `identityOrigin==0` and already `Identified`, cap 128/family to
  bound costs. Never use newly inferred sounds as seeds in the same pass.
  Only recover previously unknown/ambiguous sounds. No file
  move, percussion-gate change, re-embedding or second neural network.
  Mark recovered source with origin=2 so it survives cache persistence
  and is not mistaken for user/named ground truth.
- `Source/PluginProcessor.cpp`: apply recovery **after** the old CLAP
  exemplar classifier and **before** subcategory calculation and final
  percussion gate. Do not run an expensive kNN search synchronously
  in the plugin constructor. Old cache loads safely; user performs normal
  **Refresh once** to rescue initially Unknown files. Saved metadata
  survives subsequent opens and Refreshes.
- `Tools/test_primary06_policy.cpp` included in GitHub CI; failure to
  build/test means no installation request.

## Reproducible OFFLINE measurement on the old user's v4 cache

Data: 1,373 cached native audio embeddings, 1,050 eligible
>=2s, 359 named-labelled CLAP seed examples, and user's previous
84 + 20 annotations (overlap/different status). Not a new scan.

- Old confirmed main labels: 426 / 1,050, Unknown 624.
- Combined new 06 candidate assignments: **153 / 624**, including
  **139 CLAP + same-family neighbour agreements** and
  **14 explicitly named FX** (impacts etc), leaving **471 Unknown**.
- Four newest user-verified controls:
  `PN_SUSTAINED`→Keys,
  `GeeChords_107_G`→Guitar,
  `009 Impacts - DESFX Zenhiser`→FX,
  `011 Impacts - DESFX Zenhiser`→FX.
- Among the 24 cached previous manually labelled Unknown sounds,
  **12** receive 06 family assignments, all 12 match a manual source
  tag. This is an in-sample sanity check, not 100% real-world precision.
- For the 359 named-seed examples, an exploratory leave-one-out
  consistency check predicts **241** and agrees with their existing
  named source on **232**. It is optimistic because near-duplicates and
  packs are not fully isolated; user has already flagged mistakes in
  some metadata names.
- **No new audio classification for 471 still-Unknown samples.**
  Forced coverage is not a goal if it creates mislabeled instrument filters.

The old native cache is a snapshot from earlier version 9A; a current
9F library may have changed and needs in-plugin verification after green CI.
In particular, Impact effects with a low kick-like sound are treated
as **FX** by their unambiguous effect name, not as newly found drums.

## Required validation and next steps

1. Pass Python/C++ tests and full macOS AU/VST3 GitHub CI.
2. Run the latest build and normal **Refresh**. Do not delete CLAP
   model, old cache, original WAVs or user pad state.
3. Confirm `PN_SUSTAINED` under Keys, `GeeChords_107_G` under Guitar,
   `009...` and `011...` under FX and FX → Impacts & Hits. Confirm
   obvious named melodic samples have NOT disappeared.
4. Measure new categories by actual listening (false inclusion and false
   omission), not just reduction in Unknown count.
5. Only then tune additional recovery and dynamic groups; no new
   branches, no cosmetic changes, no changes to loop/percussion detection.
