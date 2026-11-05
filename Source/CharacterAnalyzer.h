#pragma once
#include "AtlasTypes.h"
#include <algorithm>
#include <cmath>

namespace Atlas
{
    // Independently measured character descriptors. Scores are deliberately
    // conservative proxies, not ground-truth emotion or instrument labels.
    // Never derive identity/category from these values.
    class CharacterAnalyzer
    {
    public:
        enum ScoreIndex : size_t
        {
            Dark = 0, Bright, Warm, Clean, Noisy,
            Plucky, Sustained, Textural, Rhythmic
        };

        static void evaluate(SampleRecord& record)
        {
            record.characterMask = 0;
            record.characterScores.fill(0.0f);
            if (!record.acoustic.valid)
                return;

            const auto& a = record.acoustic;
            auto clamp = [](float x) { return juce::jlimit(0.0f, 1.0f, x); };

            // Centroid in Hz is more interpretable across sample rates than
            // dividing by Nyquist. A slow logarithmic scale avoids treating
            // every ordinary synth as uniformly dark.
            const float brightness = clamp(
                (std::log2(std::max(100.0f, a.spectralCentroidHz) / 300.0f)) / 4.0f);
            const float flatness = clamp(a.spectralFlatness);
            const float lowerEnergy = clamp(a.lowBandRatio + 0.45f * a.midBandRatio);
            const float transient = clamp(a.onsetActivity * 7.0f);
            const float sustain = clamp(a.sustainRatio);

            record.characterScores[Dark] = 1.0f - brightness;
            record.characterScores[Bright] = brightness;
            record.characterScores[Warm] = clamp(
                lowerEnergy * (1.0f - 0.35f * flatness));
            record.characterScores[Clean] = clamp(
                (1.0f - flatness) * (0.5f + 0.5f * record.tonal.periodicity));
            record.characterScores[Noisy] = flatness;
            record.characterScores[Plucky] = clamp(
                0.65f * transient + 0.35f * (1.0f - sustain));
            record.characterScores[Sustained] = clamp(
                0.75f * sustain + 0.25f * (1.0f - transient));
            record.characterScores[Textural] = clamp(
                0.6f * flatness + 0.4f * a.highBandRatio);
            // Repeated onsets require a dedicated onset-sequence estimator.
            // Do not claim rhythmic character from aggregate activity alone.
            record.characterScores[Rhythmic] = 0.0f;

            constexpr float threshold = 0.64f;
            const uint32_t flags[] = {
                Character_Dark, Character_Bright, Character_Warm,
                Character_Clean, Character_Noisy, Character_Pluck,
                Character_Sustain, Character_Textural, Character_Rhythmic
            };
            for (size_t i = 0; i < record.characterScores.size(); ++i)
                if (record.characterScores[i] >= threshold)
                    record.characterMask |= flags[i];
        }
    };
}
