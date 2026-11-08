#pragma once
#include "AtlasTypes.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace Atlas
{
    // Self-supervised library classifier. CLAP has no instrument/drum tags:
    // labelled sample names act as trusted examples in the audio embedding
    // space. Only strongly matching UNKNOWN sounds get propagated labels.
    // A small number of explicitly labelled drums form rejection examples.
    struct ClapLibraryClassifier
    {
        using Centre = std::array<float, 512>;
        static constexpr int instruments = 7;
        static constexpr int maxExamplesPerFamily = 32;

        static float similarity(const Centre& a, const Centre& b)
        {
            double dot = 0.0;
            for (size_t i = 0; i < a.size(); ++i)
                dot += (double)a[i] * b[i];
            return (float)dot;
        }

        static void apply(std::vector<SampleRecord>& records,
                          const std::vector<Centre>& drumExamples = {})
        {
            std::array<std::vector<const Centre*>, instruments> labelled {};
            for (const auto& rec : records)
            {
                const int group = (int)rec.identityCluster;
                if (rec.clapEmbeddingValid
                    && rec.identityState == IdentityState::Identified
                    && rec.identityOrigin == 0
                    && group >= 0 && group < instruments)
                {
                    auto& examples = labelled[(size_t)group];
                    if ((int)examples.size() < maxExamplesPerFamily)
                        examples.push_back(&rec.clapEmbedding);
                }
            }

            for (auto& rec : records)
            {
                if (!rec.clapEmbeddingValid)
                    continue;
                std::array<float, instruments> scores {};
                scores.fill(-1.0f);
                float closestMelodic = -1.0f;

                for (int group = 0; group < instruments; ++group)
                {
                    const auto& examples = labelled[(size_t)group];
                    if (examples.empty())
                        continue;
                    float first = -1.0f, second = -1.0f;
                    for (const auto* example : examples)
                    {
                        const float score = similarity(rec.clapEmbedding, *example);
                        if (score > first)
                        {
                            second = first;
                            first = score;
                        }
                        else if (score > second)
                            second = score;
                    }
                    closestMelodic = std::max(closestMelodic, first);
                    if (examples.size() >= 2)
                        scores[(size_t)group] = 0.72f * first + 0.28f * second;
                }

                // No reference audio: retain the independent acoustic gate.
                if (!drumExamples.empty()
                    && rec.percussionState != PercussionState::Percussive)
                {
                    float closestDrum = -1.0f;
                    for (const auto& example : drumExamples)
                        closestDrum = std::max(closestDrum,
                            similarity(rec.clapEmbedding, example));

                    const bool weakTone = !rec.tonal.valid
                        || rec.tonal.validWindowCount == 0
                        || rec.tonal.periodicity < 0.56f;
                    const bool hitLike = rec.acoustic.valid
                        && (rec.acoustic.spectralFlatness > 0.19f
                            || rec.acoustic.sustainRatio < 0.26f
                            || rec.acoustic.onsetActivity > 0.10f);
                    // Strict relative advantage prevents a plucky synth
                    // matching both drum and synth references from vanishing.
                    if (closestDrum > 0.79f && hitLike && weakTone
                        && closestDrum > closestMelodic + 0.055f)
                    {
                        rec.percussionState = PercussionState::Percussive;
                        rec.percussionConfidence = std::max(
                            rec.percussionConfidence, 0.70f);
                        continue;
                    }
                }

                if (rec.identityState == IdentityState::Identified)
                    continue;
                int best = -1;
                float first = -1.0f, second = -1.0f;
                for (int group = 0; group < instruments; ++group)
                {
                    const float score = scores[(size_t)group];
                    if (score > first)
                    {
                        second = first;
                        first = score;
                        best = group;
                    }
                    else if (score > second)
                        second = score;
                }
                if (best >= 0 && first > 0.80f && first > second + 0.065f)
                {
                    rec.identityState = IdentityState::Identified;
                    rec.identityCluster = (ClusterGroup)best;
                    rec.cluster = (ClusterGroup)best;
                    rec.identityOrigin = 1; // inferred, not a named sample
                    rec.identityConfidence = juce::jlimit(
                        0.0f, 0.85f, 0.70f + (first - 0.80f));
                }
            }
        }
    };
}
