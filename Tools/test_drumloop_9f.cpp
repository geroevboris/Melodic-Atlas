#include "DrumLoop9FPolicy.h"
#include <cassert>
#include <iostream>

using Atlas::DrumLoop9FPolicy;
int main()
{
    // Original waveform files are NOT committed to this repository.
    // These values are from measured 9F local waveform evidence and the
    // existing native CLAP/audio-feature cache, with user-confirmed labels.
    DrumLoop9FPolicy::Evidence eq;
    eq.clapDrumsTop = eq.clapSubtypeIsDrumSequence = true;
    eq.rhythmicSequence = true;
    eq.rhythmicEventCount = 11;
    eq.rhythmicSpanSeconds = 4.33f;
    eq.durationSeconds = 5.05263;
    eq.drumCosine = 0.415274f;
    eq.bestMelodicCosine = 0.244933f;
    eq.spectralFlatness = 0.282134f;
    eq.onsetActivity = 0.130385f;
    eq.tonalWindows = 0;
    assert(DrumLoop9FPolicy::shouldExclude(eq));

    auto anonymous = eq;
    anonymous.rhythmicEventCount = 15;
    anonymous.rhythmicSpanSeconds = 4.67f;
    anonymous.durationSeconds = 5.16567;
    anonymous.drumCosine = 0.324555f;
    anonymous.bestMelodicCosine = 0.235654f;
    anonymous.spectralFlatness = 0.116331f;
    anonymous.onsetActivity = 0.138962f;
    assert(DrumLoop9FPolicy::shouldExclude(anonymous));

    auto crunchy = eq;
    crunchy.rhythmicEventCount = 22; // first 6 seconds of 9.6s file
    crunchy.rhythmicSpanSeconds = 5.87f;
    crunchy.durationSeconds = 9.6;
    crunchy.drumCosine = 0.234748f;
    crunchy.bestMelodicCosine = 0.066413f;
    crunchy.spectralFlatness = 0.148770f;
    crunchy.onsetActivity = 0.204339f;
    assert(DrumLoop9FPolicy::shouldExclude(crunchy));

    // Actual hand-confirmed rhythmic bass in the previous audit:
    // CLAP calls it DrumLoop, but spectral flatness is too low
    // to conclude percussion dominance.
    auto bass = eq;
    bass.rhythmicEventCount = 12;
    bass.rhythmicSpanSeconds = 4.0f;
    bass.durationSeconds = 6.857;
    bass.drumCosine = 0.490372f;
    bass.bestMelodicCosine = 0.392323f;
    bass.spectralFlatness = 0.066709f;
    bass.onsetActivity = 0.215834f;
    assert(!DrumLoop9FPolicy::shouldExclude(bass));

    auto melodic = eq;
    melodic.melodicFilenameVeto = true;
    assert(!DrumLoop9FPolicy::shouldExclude(melodic));
    melodic.melodicFilenameVeto = false;
    melodic.trustedInstrumentVeto = true;
    assert(!DrumLoop9FPolicy::shouldExclude(melodic));

    auto oneShot = eq;
    oneShot.rhythmicSequence = false;
    oneShot.rhythmicEventCount = 2;
    assert(!DrumLoop9FPolicy::shouldExclude(oneShot));

    auto pitched = eq;
    pitched.tonalWindows = 3;
    assert(!DrumLoop9FPolicy::shouldExclude(pitched));

    std::cout << "9F user-confirmed drum loops + bass preservation: PASS\n";
}
