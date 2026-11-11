# 07 — Adaptive library taxonomy + manual category correction

## Product contract

This is one integrated change in the existing single development branch,
`dev/9f-grounded-loop-percussion`; `main` is unchanged. No extra neural
model, source WAV relocation, map geometry change or pad gesture change.

1. Every eligible, non-hidden, non-percussion sound published to the atlas
   is assigned a real **main instrument family** (Keys, Synth, Bass,
   Vocal, Strings/Winds, Guitar, FX). There is no unfilterable Unknown.
2. A sound is given a content-based subcategory from the actual library
   using its native 512D audio CLAP vector and 145 static text references
   (65 concrete subtypes and 24 broad parent types). Subtype labels and
   counts in the filter menu come **only from observed files**, not a fixed
   list of UI buttons. Main categories with zero assigned sounds are hidden.
3. If CLAP confidently discovers many different Synth timbres, the
   library exposes more Synth subcategories. If it discovers only a few,
   the menu is shorter. Very weak narrow candidates are folded into a
   broader named parent category. No sound disappears from its main
   category due to a failed subtype match.
4. **Shift+right-click on a map point → Move to… → category →
   subcategory**, or choose All [category] for a manual main-category-only
   correction. Reset to automatic category reverses the override.
   Ordinary right-click still assigns to selected pad. Mouse drag remains
   map navigation, no draggable sample dots.
5. Manual fixes persist in the existing AtlasMapOverrides.xml (path keyed)
   and survive Refresh, plugin reopening and DAW project restore. The
   automatic source/subtype is preserved independently, so manual
   corrections cannot corrupt automatic seed examples. The source WAV
   file is never moved or modified.
6. Cache schema version 07 stores automatic category and subgroup.
   Old caches are upgraded from existing CLAP vectors on first load and
   normally persisted after Refresh. No user-requested re-embedding or
   reinstall of PyTorch.
7. All saved project pad values, audio playback, One-shot/Loop logic,
   percussion gates and island/map positions remain unchanged.

## Classification approach

- Keep explicit, trusted source names as initial references.
- Combine official CLAP main-family text scores with same-library
  neighbours. Ambiguous clips still receive their best supported
  category; missing CLAP falls back to current metadata and broad
  acoustic evidence.
- Recognize explicitly named cinematic Impact sounds as FX.
- Generate names from the 65 subtype concepts; fallback to a concrete
  broad family group when narrow identification is unreliable.
- No hard disappearance due to category confidence. The only atlas
  exclusions remain the prior duration gate, percussion gate and
  explicit user hide override.

## Offline sanity check (old 9A v4 cache, NOT the current installed library)

- Eligible WAV: **1,050**; all receive one of seven main families.
- Snapshot family distribution for this prototype:
  Keys 85, Synth 209, Bass 172, Vocal 201,
  Strings/Winds 170, Guitar 79, FX 134.
- Four current user controls restored to main categories:
  `PN_SUSTAINED` → Keys;
  `GeeChords_107_G` → Guitar;
  `009 Impacts - DESFX Zenhiser` and `011 Impacts - DESFX Zenhiser`
  → FX and Impacts subtype.
- **Accuracy caveat:** forced 100% coverage is not 100% correct
  classification. The prior small human-labelled sample showed
  many genuinely ambiguous audio sources and incorrect CLAP guesses.
  Real-world recognition quality still requires direct listening tests
  against independent sample packs. User correction is therefore a
  core feature, not an optional afterthought.

## Validation gates

- Standalone C++ algorithm-policy tests and Python static-contract tests
  are in CI.
- Full macOS AU/VST3 and Windows builds must PASS before offering install.
- On a test library: verify no eligible sample is missing from all
  categories; main-category chips disappear at zero count, submenu is
  populated by actual content, and Shift+right-click assignment persists
  after Refresh and reopening. Check old pads, one-shot/loop and preview.
- Do not describe this as unlimited natural-language invention of
  instrument names. The *population and available subcategories* are
  dynamic, while names are drawn from the 65 CLAP semantic types and
  broader instrumental groups. Fully open-ended naming would require
  an additional semantic mechanism not present here.
