#pragma once
#include "AtlasTypes.h"
#include "ClapTextPrototypeCandidate9C.h"
#include "FamilyRegisterLayout13.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace Atlas
{
    // Whole CLAP families follow their shared LOW-to-HIGH spectral profile.
    // Individual register offsets are bounded; source UMAP stays cached.
    struct SoundIslandLayout
    {
        static constexpr int similarityIslands = 18;
        static constexpr int maxIslands = similarityIslands * 4;
        static constexpr int geometryVersion = 13;

        static float frequencyPosition(const SampleRecord& rec)
        {
            // Primary axis = perceived musical register, NOT spectral
            // brightness. Distorted bass retains a low fundamental even
            // when its upper harmonics dominate the spectral centroid.
            const bool verifiedBass = rec.musicalBassRegisterHint
                || (rec.identityState == IdentityState::Identified
                    && rec.identityCluster == ClusterGroup::Bass);

            float spectralX = juce::jlimit(0.025f, 0.975f,
                                          rec.spectralCentroidNorm);
            if (rec.acoustic.valid
                && std::isfinite(rec.acoustic.spectralCentroidHz)
                && rec.acoustic.spectralCentroidHz > 0.0f)
            {
                const float hz = juce::jlimit(
                    90.0f, 9000.0f, rec.acoustic.spectralCentroidHz);
                spectralX = juce::jlimit(0.04f, 0.96f,
                    std::log(hz / 90.0f)
                    / std::log(9000.0f / 90.0f));
            }

            // The YIN analyzer already saves the median pitch across
            // several windows. A moving bass phrase need not have a
            // near-constant note to contribute meaningful register.
            const bool hasF0 = rec.tonal.valid
                && rec.tonal.validWindowCount >= 1
                && rec.tonal.periodicity >= 0.58f
                && std::isfinite(rec.tonal.estimatedMidi)
                && rec.tonal.estimatedMidi >= 24.0f
                && rec.tonal.estimatedMidi <= 96.0f;
            if (hasF0)
            {
                const float hz = 440.0f * std::pow(
                    2.0f, (rec.tonal.estimatedMidi - 69.0f) / 12.0f);
                const float registerX = juce::jlimit(
                    0.04f, 0.96f, std::log(std::max(45.0f, hz) / 45.0f)
                    / std::log(2600.0f / 45.0f));
                // Prefer the musical fundamental over timbral brightness.
                spectralX = 0.82f * registerX + 0.18f * spectralX;
            }
            else if (rec.acoustic.valid)
            {
                // When f0 is absent, do not confuse harmonically-rich bass
                // with treble purely because of a high centroid.
                if (rec.acoustic.lowBandRatio >= 0.40f)
                    spectralX = std::min(spectralX, 0.30f);
                else if (rec.acoustic.lowBandRatio >= 0.28f
                         && rec.acoustic.lowBandRatio
                            > rec.acoustic.highBandRatio)
                    spectralX = std::min(spectralX, 0.44f);
            }

            if (verifiedBass)
            {
                // Source identity outranks spectral centroid. Keep ALL
                // confirmed basses in the left register, including Reese,
                // Neurobass, and dynamically changing basslines.
                spectralX = std::min(spectralX,
                    hasF0 ? 0.235f : 0.20f);
            }
            return juce::jlimit(0.02f, 0.98f, spectralX);
        }

        static float attackPosition(const SampleRecord& rec)
        {
            if (!rec.acoustic.attackMeasured
                || !std::isfinite(rec.acoustic.attackTimeSec))
                return 0.5f; // no reliable attack evidence
            // 5 ms -> bottom, 2 seconds -> top; log scale gives useful
            // separation among short attacks without crushing slow pads.
            const float seconds = juce::jlimit(
                0.005f, 2.0f, rec.acoustic.attackTimeSec);
            const float progress = std::log(seconds / 0.005f)
                                 / std::log(2.0f / 0.005f);
            return juce::jlimit(0.05f, 0.95f, 0.95f - 0.90f * progress);
        }

        static int spectralRegion(float x)
        {
            return juce::jlimit(0, 3, (int)std::floor(
                juce::jlimit(0.0f, 0.99999f, x) * 4.0f));
        }

        struct Island
        {
            std::vector<size_t> members;
            float anchorX = 0.5f, anchorY = 0.5f;
            float centreX = 0.5f, centreY = 0.5f;
            float sourceX = 0.5f, sourceY = 0.5f;
            float maxDistance = 0.0f;
            float radius = 0.045f;
            int region = 0;
            int attackShadeLevel = 1; // sharp / medium / soft
        };

        static float median(std::vector<float>& v)
        {
            if (v.empty())
                return 0.5f;
            auto mid = v.begin() + (std::ptrdiff_t)(v.size() / 2);
            std::nth_element(v.begin(), mid, v.end());
            return *mid;
        }

        static bool apply(std::vector<SampleRecord>& records)
        {
            // One-shots and loops have separate visible atlases; their
            // positions and cluster medians must be fitted independently.
            const bool oneShots = applySubset(records, false);
            const bool loops = applySubset(records, true);
            return oneShots || loops;
        }

    private:
        static bool applySubset(std::vector<SampleRecord>& records, bool loopMode)
        {
            // Audio similarity membership (0..17) is fixed by CLAP.
            // No spectral sub-islands: each family moves as ONE cloud.
            // Loops and One-shots are handled by separate calls.
            std::vector<size_t> indices;
            std::vector<FamilyRegisterLayout13::Member> members;
            indices.reserve(records.size());
            members.reserve(records.size());

            for (size_t i = 0; i < records.size(); ++i)
            {
                auto& rec = records[i];
                if (rec.isLoop != loopMode
                    || rec.islandGroup < 0
                    || rec.islandGroup >= similarityIslands
                    || !rec.clapEmbeddingValid || !rec.neuralMapValid)
                    continue;

                // Keep the existing, conservatively corroborated bass
                // hints. They influence musical register, never labels.
                const auto name =
                    rec.file.getFileNameWithoutExtension().toLowerCase();
                rec.musicalBassRegisterHint =
                    name.contains("neurobass") || name.contains("reese")
                    || name.contains("subbass") || name.contains("sub bass")
                    || name.contains("bassloop") || name.contains("bass_loop")
                    || name.contains("bass loop") || name.contains("_bass_");
                if (!rec.musicalBassRegisterHint && rec.clapEmbeddingValid)
                {
                    const auto scores =
                        ClapTextPrototypeCandidate9C::evaluate(rec.clapEmbedding);
                    rec.musicalBassRegisterHint = scores.valid
                        && scores.topFamily == 2
                        && scores.topCosine >= 0.42f
                        && scores.margin >= 0.065f;
                }

                // An old cache keeps the ORIGINAL source UMAP projection.
                // Updating the visual layout never iterates on coordinates
                // previously warped by a different geometry version.
                if (!std::isfinite(rec.projectionX)
                    || !std::isfinite(rec.projectionY)
                    || rec.projectionX < 0.0f || rec.projectionY < 0.0f)
                {
                    rec.projectionX = rec.neuralMapX;
                    rec.projectionY = rec.neuralMapY;
                }

                indices.push_back(i);
                members.push_back({
                    rec.islandGroup, rec.projectionX, rec.projectionY,
                    frequencyPosition(rec)
                });
            }

            if (members.empty())
                return false;

            const auto positions = FamilyRegisterLayout13::arrange(members);
            if (positions.size() != members.size())
                return false;

            for (size_t i = 0; i < members.size(); ++i)
            {
                auto& rec = records[indices[i]];
                const auto& pt = positions[i];
                rec.neuralMapX = pt.x;
                rec.neuralMapY = pt.y;
                // The overlay now identifies a real CLAP family, not an
                // artificial four-way register split (old 18 x 4 map).
                rec.visualIslandGroup = rec.islandGroup;
                // Red -> blue -> green -> gold always reflects X.
                rec.colourGroup = pt.zone;
                rec.attackShadeLevel = 1;
            }
            return true;
        }
    };
}
