# 10 — Every eligible sound is individually reachable at maximum zoom

**Branch:** same existing `dev/9f-grounded-loop-percussion`. Do not edit `main`.
No CLAP model changes, no rescanning, no changes to sound classification,
user overrides, saved pads, history, MIDI, Random pool or audio output.

## User-visible issue

Previous `ensureVisibleSamples()` limited sample dots by a fixed distance
that actually **grew** from 10.5 px at overview to 14.5 px at 18x. It
never rendered all samples for a dense island. When the user hid one drum
loop, the next semantic LOD pass filled the vacated pixel area with
other drum and synth loops. A drawn island was still computed from ALL
samples, so it looked much denser than its playable point markers.

The old LOD code wasn't missing any WAVs from analysis: it was
permanently withholding access to parts of the library.

## Implementation

- New JUCE-independent `AtlasPointCoverage10.h` deterministically packs
  existing musical-map coordinates into an adjacent, collision-free lattice
  **at the pre-existing maximum zoom 18x**, using a 6.6-pixel cell interval
  and O(1)-per-step square spiral. Stable sample ordering within a
  library ensures no two eligible samples share one final cell, even
  when thousands have exactly the same 512D CLAP / map projection.
  Packing is a view-only derived layout; original CLAP embeddings,
  map position, island/source IDs and on-disk caches remain unchanged.
- `MapCanvasGL.h`: use packed locations consistently in marker rendering,
  hit testing, island hulls, and intermediate-zoom LOD. Recompute only on
  a new sample database or resized map, not on every drag/zoom/paint.
- At 18x (maximum zoom) show **every currently eligible sound in the
  selected One-shots/Loops tab and instrumental filters**, without a
  density-pruning cap. Pan around the enlarged map to inspect sounds
  beyond the current window. No sound is intrinsically unclickable due to
  an invisible overlapping point.
- Overview/partial zoom min spacing decreases from 8.0 px at 1x to
  5.7 px at 18x rather than increasing to 14.5 px. More real sample
  markers are displayed immediately, and progressive zoom reveals the
  remainder until **visible/total reaches 100%**.
- The top map bar has `POINTS N / TOTAL` to make coverage explicit.
  Note that N counts LOD-selected points across the virtual atlas, not
  only the current screen viewport when panned/zoomed.
- Island colour-fill alpha tracks the number of **all** matching samples
  in its hull with a less quickly saturated range (0.055 to 0.38).
  Thus more populated islands remain visually denser than sparse ones.
- On manual Hide-only edits, preserve existing packed positions, the
  current visible subset and candidate priority. Remove only newly
  hidden points without immediately filling the holes. Next normal
  zoom/filter selection can reveal more samples progressively.
- Cached mapping, user hidden samples and Random's intentionally bounded
  200-sample game pool retain their existing rules: the 18x full-coverage
  guarantee applies to the **normal atlas**, not Random mode.

## CI and acceptance

- `test_point_coverage_10.cpp` packs 3,000 identical locations, 12,400
  clustered/edge locations, checks uniqueness/determinism, and confirms
  hidden sounds keep reserved marker cells.
- `test_map_coverage_10.py` checks normal UI integration, no LOD cap
  at maximum zoom, density indicator, and no replenishment on Hide.
- CI must pass C++ tests and full AU/VST3 macOS and Windows compilation.
- Ableton check: zoom into an island until 18x; expect
  `POINTS N / N`. Every sound can be clicked (pan to navigate).
  Hiding a dot on the *same zoom* reduces visible count by one and does
  not cause previously invisible dots to appear.
- The 18x view may be very large: the user needs to pan around to see
  its whole virtual area. All sounds remain indexed, not all at once
  inside the viewport. That's intentional to avoid overlap.
