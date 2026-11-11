#pragma once
#include <cmath>

// Standalone, auditable 9E evidence policy. This is NOT a probability.
// A DrumLoop text-CLAP win alone is insufficient: demand independent
// rhythmic attack evidence, positive absolute CLAP support, separation
// from every melodic family and no strong contradictory metadata.
// This is a deliberately conservative *first* production change.
namespace Atlas
{
    struct DrumLoop9EPolicy
    {
        struct Evidence
        {
            bool clapValid = false;
            bool strongestPromptIsDrumLoop = false;
            bool strongestFamilyIsDrums = false;
            bool explicitMelodicFileLabel = false;
            bool trustedOldInstrumentLabel = false;
            double durationSeconds = 0.0;
            float drumCosine = -1.0f;
            float bestMelodicCosine = -1.0f;
            float onsetActivity = 0.0f;
            int tonalWindows = 0;
        };

        static bool shouldExclude(const Evidence& e)
        {
            return e.clapValid
                && e.strongestPromptIsDrumLoop
                && e.strongestFamilyIsDrums
                && !e.explicitMelodicFileLabel
                && !e.trustedOldInstrumentLabel
                && std::isfinite(e.durationSeconds)
                && e.durationSeconds >= 2.0
                && std::isfinite(e.drumCosine)
                && std::isfinite(e.bestMelodicCosine)
                && std::isfinite(e.onsetActivity)
                && e.drumCosine >= 0.35f
                && e.drumCosine - e.bestMelodicCosine >= 0.12f
                && e.onsetActivity >= 0.09f
                && e.tonalWindows >= 0
                && e.tonalWindows <= 1;
        }
    };
}
