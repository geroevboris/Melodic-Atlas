// Standalone regression tests for the *production* 9E policy. No JUCE.
#include "DrumLoop9EPolicy.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>

using Atlas::DrumLoop9EPolicy;
int main()
{
    auto e = DrumLoop9EPolicy::Evidence {};
    e.clapValid = true;
    e.strongestPromptIsDrumLoop = true;
    e.strongestFamilyIsDrums = true;
    e.durationSeconds = 5.33;
    e.drumCosine = 0.43f;
    e.bestMelodicCosine = 0.29f;
    e.onsetActivity = 0.125f;
    e.tonalWindows = 1;
    assert(DrumLoop9EPolicy::shouldExclude(e)); // rhythmic beatbox loop

    // CLAP-only decisions are never enough to hide musical material.
    e.bestMelodicCosine = 0.35f;
    assert(!DrumLoop9EPolicy::shouldExclude(e));
    e.bestMelodicCosine = 0.29f;

    // Known strongly drum-like bass from hand-reviewed corpus: 0.490 / 0.392.
    e.drumCosine = 0.490f;
    e.bestMelodicCosine = 0.392f;
    e.tonalWindows = 0;
    assert(!DrumLoop9EPolicy::shouldExclude(e));
    e.drumCosine = 0.43f;
    e.bestMelodicCosine = 0.29f;

    e.explicitMelodicFileLabel = true;
    assert(!DrumLoop9EPolicy::shouldExclude(e));
    e.explicitMelodicFileLabel = false;
    e.trustedOldInstrumentLabel = true;
    assert(!DrumLoop9EPolicy::shouldExclude(e));
    e.trustedOldInstrumentLabel = false;

    e.onsetActivity = 0.03f;
    assert(!DrumLoop9EPolicy::shouldExclude(e));
    e.onsetActivity = 0.125f;
    e.tonalWindows = 2;
    assert(!DrumLoop9EPolicy::shouldExclude(e));
    e.tonalWindows = 1;

    e.strongestFamilyIsDrums = false;
    assert(!DrumLoop9EPolicy::shouldExclude(e));
    e.strongestFamilyIsDrums = true;
    e.strongestPromptIsDrumLoop = false;
    assert(!DrumLoop9EPolicy::shouldExclude(e));
    e.strongestPromptIsDrumLoop = true;

    e.durationSeconds = 1.99;
    assert(!DrumLoop9EPolicy::shouldExclude(e));
    e.durationSeconds = 5.33;
    e.drumCosine = std::numeric_limits<float>::quiet_NaN();
    assert(!DrumLoop9EPolicy::shouldExclude(e));

    std::cout << "9E production drum-loop guard tests passed\n";
}
