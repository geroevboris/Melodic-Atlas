#pragma once
#include "AtlasTypes.h"
#include <cmath>

namespace Atlas
{
    // Scanner-only, high-precision provisional percussion gate.
    // Intentionally prioritizes retaining pitched plucks, bells and stabs
    // over achieving high percussion recall. No file is deleted or hidden.
    class PercussionGate
    {
    public:
        static void evaluate(SampleRecord& rec)
        {
            rec.percussionState = PercussionState::NotEvaluated;
            rec.percussionConfidence = 0.0f;

            const auto& a = rec.acoustic;
            const auto& t = rec.tonal;
            if (!a.valid || !t.valid)
                return;

            const auto valid01 = [](float x) {
                return std::isfinite(x) && x >= 0.0f && x <= 1.0f;
            };
            if (!valid01(a.spectralFlatness) || !valid01(a.onsetActivity)
                || !valid01(a.sustainRatio) || !valid01(t.periodicity)
                || !valid01(t.pitchStability))
            {
                rec.percussionState = PercussionState::Uncertain;
                return;
            }

            // A strong pitch track, or one very clear short-lived periodic
            // window, is evidence for melodic material even when transient.
            // Never reject plucks just because their attack is strong.
            const bool stablePitch = t.validWindowCount >= 2
                                  && t.periodicity >= 0.68f
                                  && t.pitchStability >= 0.65f;
            const bool clearPitchedTransient = t.validWindowCount >= 1
                                            && t.periodicity >= 0.84f;
            if (stablePitch || clearPitchedTransient)
            {
                rec.percussionState = PercussionState::Melodic;
                rec.percussionConfidence = juce::jlimit(
                    0.0f, 1.0f, t.periodicity * (stablePitch ? 0.95f : 0.86f));
                return;
            }

            // Strong independent evidence of a short, noisy transient.
            // Neither filename words nor short duration alone can reject
            // samples. Tonal/percussive mixtures remain uncertain.
            const bool noisy = a.spectralFlatness >= 0.48f;
            const bool impulsive = a.onsetActivity >= 0.13f;
            const bool decaying = a.sustainRatio <= 0.28f;
            const bool weakPitch = t.validWindowCount == 0
                                || t.periodicity < 0.35f;

            if (noisy && impulsive && decaying && weakPitch)
            {
                rec.percussionState = PercussionState::Percussive;
                // This is rule evidence strength, not calibrated probability.
                rec.percussionConfidence = juce::jlimit(0.0f, 1.0f,
                    0.55f + 0.20f * (a.spectralFlatness - 0.48f)
                          + 0.20f * (a.onsetActivity - 0.13f)
                          + 0.10f * (0.28f - a.sustainRatio));
                return;
            }

            rec.percussionState = PercussionState::Uncertain;
            rec.percussionConfidence = 0.0f;
        }
    };
}
