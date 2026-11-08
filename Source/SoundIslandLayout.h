#pragma once
#include "AtlasTypes.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace Atlas
{
    // Four fixed spectral zones on X; logarithmic effective duration on Y
    // (long sounds at the top). CLAP defines island membership and relative
    // neighbour geometry, NOT the global axis positions.
    struct SoundIslandLayout
    {
        static constexpr int similarityIslands = 18;
        static constexpr int maxIslands = similarityIslands * 4;
        static constexpr int geometryVersion = 5;

        static float frequencyPosition(const SampleRecord& rec)
        {
            if (rec.acoustic.valid
                && std::isfinite(rec.acoustic.spectralCentroidHz)
                && rec.acoustic.spectralCentroidHz > 0.0f)
            {
                // Log frequency and energy-weighting, not MIDI pitch:
                // a harmonic bass can therefore be perceptually brighter
                // than its fundamental note would suggest.
                const float hz = juce::jlimit(90.0f, 9000.0f,
                                               rec.acoustic.spectralCentroidHz);
                const float logFreq = std::log(hz / 90.0f)
                                    / std::log(9000.0f / 90.0f);
                const float energyCorrection = 0.09f *
                    (rec.acoustic.highBandRatio - rec.acoustic.lowBandRatio);
                float acousticX = juce::jlimit(0.04f, 0.96f,
                                              logFreq + energyCorrection);
                // Spectral centroid alone pushes harmonically rich basses
                // into HIGH-MID. Incorporate the measured fundamental only
                // when multiple independent tonal windows agree.
                if (rec.tonal.valid
                    && rec.tonal.validWindowCount >= 2
                    && rec.tonal.periodicity >= 0.65f
                    && rec.tonal.pitchStability >= 0.58f
                    && rec.tonal.estimatedMidi >= 24.0f
                    && rec.tonal.estimatedMidi <= 96.0f)
                {
                    const float fundamental = 440.0f *
                        std::pow(2.0f,
                            (rec.tonal.estimatedMidi - 69.0f) / 12.0f);
                    const float f0x = juce::jlimit(0.04f, 0.96f,
                        std::log(std::max(45.0f, fundamental) / 90.0f)
                            / std::log(9000.0f / 90.0f));
                    // A bass with rich harmonics still has a low
                    // fundamental; above middle C the spectral character
                    // becomes progressively more important.
                    const float fundamentalWeight =
                        fundamental < 220.0f ? 0.77f
                        : fundamental < 650.0f ? 0.58f : 0.32f;
                    acousticX = (1.0f - fundamentalWeight) * acousticX
                        + fundamentalWeight * f0x;
                    if (fundamental < 165.0f)
                        acousticX = std::min(acousticX, 0.232f);
                    else if (fundamental < 420.0f)
                        acousticX = std::min(acousticX, 0.470f);
                }
                // Strong low-band dominance is independent evidence that a
                // sound belongs toward the left, even if its noisy/harmonic
                // spectrum raises the centroid.
                if (rec.acoustic.lowBandRatio >= 0.43f
                    && rec.acoustic.highBandRatio < 0.27f)
                    acousticX = std::min(acousticX, 0.232f);
                else if (rec.acoustic.lowBandRatio >= 0.31f
                         && rec.acoustic.highBandRatio < 0.24f)
                    acousticX = std::min(acousticX, 0.475f);
                return juce::jlimit(0.015f, 0.985f, acousticX);
            }
            return juce::jlimit(0.04f, 0.96f, rec.spectralCentroidNorm);
        }

        static float durationPosition(const SampleRecord& rec)
        {
            const double measured = rec.effectiveDurationSec > 0.0
                ? rec.effectiveDurationSec : rec.durationSec;
            const float secs = juce::jlimit(
                0.08f, 60.0f, (float)(std::isfinite(measured) ? measured : 1.0));
            const float progress = std::log(secs / 0.08f)
                                  / std::log(60.0f / 0.08f);
            // Screen Y is downward: high Y values mean SHORT duration.
            return juce::jlimit(0.05f, 0.95f, 1.0f - progress);
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
                // A broad CLAP island may span an entire keyboard or many
                // registers. Split its VISUAL territory by spectral region
                // while retaining the original CLAP similarity assignment.
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
                    y.push_back(durationPosition(rec));
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

            // Layout repulsion is deliberately subordinate to the axes:
            // X may move at most ~3.5% and Y at most ~4% from an island's
            // measured median. Groups with identical measurements can overlap
            // slightly rather than falsely move into distant spectral bands.
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
                        const float required =
                            left.radius + right.radius + 0.014f;
                        if (dist >= required)
                            continue;
                        const float angle = (float)(a * 17 + b * 29) * 1.173f;
                        const float nx = dist > 0.0001f
                            ? dx / dist : std::cos(angle);
                        const float ny = dist > 0.0001f
                            ? dy / dist : std::sin(angle);
                        const float push = (required - dist) * 0.36f;
                        force[(size_t)a].x -= nx * push;
                        force[(size_t)a].y -= ny * push;
                        force[(size_t)b].x += nx * push;
                        force[(size_t)b].y += ny * push;
                    }
                }
                for (int i = 0; i < maxIslands; ++i)
                {
                    auto& island = islands[(size_t)i];
                    if (island.members.empty())
                        continue;
                    const float x = island.centreX + force[(size_t)i].x
                        + 0.06f * (island.anchorX - island.centreX);
                    const float y = island.centreY + force[(size_t)i].y
                        + 0.06f * (island.anchorY - island.centreY);
                    const float bandStart = (float)island.region * 0.25f;
                    const float bandEnd = bandStart + 0.25f;
                    island.centreX = juce::jlimit(
                        juce::jmax(bandStart + 0.024f,
                                   island.anchorX - 0.025f),
                        juce::jmin(bandEnd - 0.024f,
                                   island.anchorX + 0.025f), x);
                    island.centreY = juce::jlimit(
                        juce::jmax(0.025f, island.anchorY - 0.04f),
                        juce::jmin(0.975f, island.anchorY + 0.04f), y);
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
                    // Soft intra-island acoustic ordering: similar sounds
                    // stay close in CLAP, bright ones lean slightly right and
                    // long ones slightly upward, without splitting the island.
                    dx += juce::jlimit(-0.045f, 0.045f,
                        0.45f * (frequencyPosition(rec) - island.anchorX));
                    dy += juce::jlimit(-0.015f, 0.015f,
                        0.15f * (durationPosition(rec) - island.anchorY));
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
                    rec.colourGroup = island.region; // fixed colour for island
                }
            }
            return true;
        }
    };
}
