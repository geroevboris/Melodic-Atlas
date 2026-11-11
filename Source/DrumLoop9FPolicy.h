#pragma once
#include <cmath>

// 9F instrument-independent *drum source* decision, distinct from
// rhythmic structure. All thresholds are screening evidence only,
// NOT calibrated probabilities or a universal percussion detector.
namespace Atlas
{
    struct DrumLoop9FPolicy
    {
        struct Evidence
        {
            bool clapDrumsTop = false;
            bool clapSubtypeIsDrumSequence = false;
            bool melodicFilenameVeto = false;
            bool trustedInstrumentVeto = false;
            bool rhythmicSequence = false;
            int rhythmicEventCount = 0;
            float rhythmicSpanSeconds = 0.0f;
            double durationSeconds = 0.0;
            float drumCosine = -1.0f;
            float bestMelodicCosine = -1.0f;
            float spectralFlatness = 0.0f;
            float onsetActivity = 0.0f;
            int tonalWindows = 0;
        };
        static bool shouldExclude(const Evidence& e)
        {
            return e.clapDrumsTop
                && e.clapSubtypeIsDrumSequence
                && !e.melodicFilenameVeto
                && !e.trustedInstrumentVeto
                && e.rhythmicSequence
                && e.rhythmicEventCount >= 6
                && e.rhythmicSpanSeconds >= 1.3f
                && std::isfinite(e.durationSeconds)
                && e.durationSeconds >= 2.0
                && std::isfinite(e.drumCosine)
                && std::isfinite(e.bestMelodicCosine)
                && e.drumCosine - e.bestMelodicCosine >= 0.08f
                && std::isfinite(e.spectralFlatness)
                && e.spectralFlatness >= 0.09f
                && std::isfinite(e.onsetActivity)
                && e.onsetActivity >= 0.09f
                && e.tonalWindows >= 0 && e.tonalWindows <= 1;
        }
    };
}
