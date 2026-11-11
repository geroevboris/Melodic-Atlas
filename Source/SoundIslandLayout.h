#pragma once
#include "AtlasTypes.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace Atlas
{
    // Four register zones on X; Y follows original CLAP/UMAP neighbourhood,
    // never attack. CLAP similarity also defines island membership.
    struct SoundIslandLayout
    {
        static constexpr int similarityIslands = 18;
        static constexpr int maxIslands = similarityIslands * 4;
        static constexpr int geometryVersion = 9;

        static float frequencyPosition(const SampleRecord& rec)
        {
            // Primary axis = perceived musical register, NOT spectral
            // brightness. Distorted bass retains a low fundamental even
            // when its upper harmonics dominate the spectral centroid.
            const bool verifiedBass = rec.identityState == IdentityState::Identified
                && rec.identityCluster == ClusterGroup::Bass;

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
            std::array<Island, maxIslands> islands {};
            size_t eligible = 0;
            for (size_t i = 0; i < records.size(); ++i)
            {
                auto& rec = records[i];
                if (rec.isLoop != loopMode
                    || rec.islandGroup < 0
                    || rec.islandGroup >= similarityIslands
                    || !rec.clapEmbeddingValid || !rec.neuralMapValid)
                    continue;
                // Split a broad CLAP island by musical register for stable
                // low-left to high-right navigation.
                const int region = spectralRegion(frequencyPosition(rec));
                rec.visualIslandGroup = rec.islandGroup * 4 + region;
                // First upgrade from old caches has only rendered positions.
                // Subsequent reflows always use the saved source projection.
                if (rec.projectionX < 0.0f || rec.projectionY < 0.0f)
                {
                    rec.projectionX = rec.neuralMapX;
                    rec.projectionY = rec.neuralMapY;
                }
                islands[(size_t)rec.visualIslandGroup].members.push_back(i);
                ++eligible;
            }
            if (eligible == 0)
                return false;

            int active = 0;
            for (auto& island : islands)
                if (!island.members.empty())
                    ++active;

            for (auto& island : islands)
            {
                if (island.members.empty())
                    continue;
                std::vector<float> x, y;
                x.reserve(island.members.size());
                y.reserve(island.members.size());
                island.sourceX = 0.0f;
                island.sourceY = 0.0f;
                for (size_t index : island.members)
                {
                    const auto& rec = records[index];
                    x.push_back(frequencyPosition(rec));
                    y.push_back(juce::jlimit(0.0f, 1.0f,
                        rec.projectionY));
                    island.sourceX += rec.projectionX;
                    island.sourceY += rec.projectionY;
                }
                const float count = (float)island.members.size();
                island.sourceX /= count;
                island.sourceY /= count;
                island.anchorX = median(x);
                island.anchorY = median(y);
                island.centreX = island.anchorX;
                island.centreY = island.anchorY;
                island.region = spectralRegion(island.anchorX);
                // No attack shading: all points of one register hue have
                // the same luminance and saturation.
                island.attackShadeLevel = 1;

                const float expected = (float)eligible / (float)active;
                const float relative = count / std::max(1.0f, expected);
                island.radius = juce::jlimit(0.022f, 0.075f,
                    0.043f * std::sqrt(std::max(0.08f, relative)));
                for (size_t index : island.members)
                {
                    const auto& rec = records[index];
                    const float dx = rec.projectionX - island.sourceX;
                    const float dy = rec.projectionY - island.sourceY;
                    island.maxDistance = std::max(island.maxDistance,
                        std::sqrt(dx * dx + dy * dy));
                }
            }

            // Compact groups while preserving register X and CLAP-neighbour
            // Y. Nearby islands in the same register and UMAP neighbourhood
            // softly attract one another. Remote groups
            // remain separate; collision forces prevent samples merging.
            // Attraction only affects island centres, never CLAP membership.
            for (int iteration = 0; iteration < 120 && active > 1; ++iteration)
            {
                std::array<juce::Point<float>, maxIslands> force {};
                for (int a = 0; a < maxIslands; ++a)
                {
                    if (islands[(size_t)a].members.empty())
                        continue;
                    for (int b = a + 1; b < maxIslands; ++b)
                    {
                        if (islands[(size_t)b].members.empty())
                            continue;
                        const auto& left = islands[(size_t)a];
                        const auto& right = islands[(size_t)b];
                        const float dx = right.centreX - left.centreX;
                        const float dy = right.centreY - left.centreY;
                        const float dist = std::sqrt(dx * dx + dy * dy);
                        const float required = left.radius + right.radius
                                               + 0.006f;
                        const float angle = (float)(a * 17 + b * 29)
                            * 1.173f;
                        const float nx = dist > 0.0001f
                            ? dx / dist : std::cos(angle);
                        const float ny = dist > 0.0001f
                            ? dy / dist : std::sin(angle);
                        if (dist < required)
                        {
                            const float push = (required - dist) * 0.41f;
                            force[(size_t)a].x -= nx * push;
                            force[(size_t)a].y -= ny * push;
                            force[(size_t)b].x += nx * push;
                            force[(size_t)b].y += ny * push;
                        }
                        else if (left.region == right.region
                                 && dist < 0.24f
                                 && std::abs(left.anchorX-right.anchorX) < 0.12f
                                 && std::abs(left.anchorY-right.anchorY) < 0.17f)
                        {
                            // Gentle pull without distorting CLAP neighbours.
                            const float pull = std::min(0.0024f,
                                (dist-required) * 0.023f);
                            force[(size_t)a].x += nx * pull;
                            force[(size_t)a].y += ny * pull;
                            force[(size_t)b].x -= nx * pull;
                            force[(size_t)b].y -= ny * pull;
                        }
                    }
                }
                for (int i = 0; i < maxIslands; ++i)
                {
                    auto& island = islands[(size_t)i];
                    if (island.members.empty())
                        continue;
                    const float x = island.centreX + force[(size_t)i].x
                        + 0.055f * (island.anchorX - island.centreX);
                    const float y = island.centreY + force[(size_t)i].y
                        + 0.055f * (island.anchorY - island.centreY);
                    const float bandStart = (float)island.region * 0.25f;
                    const float bandEnd = bandStart + 0.25f;
                    // Register-region boundaries remain stable.
                    island.centreX = juce::jlimit(
                        juce::jmax(bandStart + 0.024f,
                                   island.anchorX - 0.035f),
                        juce::jmin(bandEnd - 0.024f,
                                   island.anchorX + 0.035f), x);
                    island.centreY = juce::jlimit(
                        juce::jmax(0.025f, island.anchorY - 0.055f),
                        juce::jmin(0.975f, island.anchorY + 0.055f), y);
                }
            }

            for (const auto& island : islands)
            {
                if (island.members.empty())
                    continue;
                const float scale = island.maxDistance > 0.00001f
                    ? island.radius * 0.72f / island.maxDistance : 0.0f;
                for (size_t order = 0; order < island.members.size(); ++order)
                {
                    auto& rec = records[island.members[order]];
                    float dx, dy;
                    if (scale > 0.0f)
                    {
                        dx = (rec.projectionX - island.sourceX) * scale;
                        dy = (rec.projectionY - island.sourceY) * scale;
                    }
                    else
                    {
                        const float angle = 2.399963f * (float)order;
                        const float r = island.radius * 0.28f *
                            std::sqrt((float)(order + 1)
                                / (float)island.members.size());
                        dx = std::cos(angle) * r;
                        dy = std::sin(angle) * r;
                    }
                    // Intra-island X respects musical register, while Y
                    // stays determined solely by CLAP/UMAP neighbours.
                    dx += juce::jlimit(-0.045f, 0.045f,
                        0.45f * (frequencyPosition(rec) - island.anchorX));

                    const float len = std::sqrt(dx * dx + dy * dy);
                    const float maxR = island.radius * 0.93f;
                    if (len > maxR)
                    {
                        dx *= maxR / len;
                        dy *= maxR / len;
                    }
                    const float bandStart = (float)island.region * 0.25f;
                    rec.neuralMapX = juce::jlimit(
                        bandStart + 0.006f, bandStart + 0.244f,
                        island.centreX + dx);
                    rec.neuralMapY = juce::jlimit(0.0f, 1.0f,
                                                 island.centreY + dy);
                    rec.colourGroup = island.region;
                    rec.attackShadeLevel = 1;
                }
            }
            return true;
        }
    };
}
