#pragma once
#include "AtlasTypes.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace Atlas
{
    // Scanner-only layout pass. Uses genuine UMAP neighbour geometry as the
    // initial arrangement, then separates fine CLAP clusters into islands.
    // Does not change instrument classification, audio data or playback.
    struct SoundIslandLayout
    {
        static constexpr int maxIslands = 18;
        static constexpr int geometryVersion = 3;

        struct Island
        {
            std::vector<size_t> members;
            float originalX = 0.5f, originalY = 0.5f;
            float x = 0.5f, y = 0.5f;
            float radius = 0.06f;
            float maximumDeviation = 0.0f;
        };

        static bool apply(std::vector<SampleRecord>& records)
        {
            std::array<Island, maxIslands> islands {};
            size_t eligible = 0;
            for (size_t i = 0; i < records.size(); ++i)
            {
                const auto& rec = records[i];
                if (rec.islandGroup < 0 || rec.islandGroup >= maxIslands
                    || !rec.neuralMapValid || !rec.clapEmbeddingValid)
                    continue;
                auto& island = islands[(size_t)rec.islandGroup];
                island.members.push_back(i);
                island.originalX += rec.neuralMapX;
                island.originalY += rec.neuralMapY;
                ++eligible;
            }
            int active = 0;
            for (auto& island : islands)
            {
                if (island.members.empty())
                    continue;
                ++active;
                // Initial offsets are removed from the sums.
                const float count = (float)island.members.size();
                island.originalX =
                    (island.originalX - 0.5f) / count;
                island.originalY =
                    (island.originalY - 0.5f) / count;
                island.x = juce::jlimit(0.10f, 0.90f, island.originalX);
                island.y = juce::jlimit(0.10f, 0.90f, island.originalY);
                const float relativeSize = (float)island.members.size()
                    * (float)active / (float)std::max<size_t>(1, eligible);
                // Calibrated after active count is known below.
                island.radius = relativeSize;
                for (size_t index : island.members)
                {
                    const auto& rec = records[index];
                    const float dx = rec.neuralMapX - island.originalX;
                    const float dy = rec.neuralMapY - island.originalY;
                    island.maximumDeviation = std::max(
                        island.maximumDeviation,
                        std::sqrt(dx * dx + dy * dy));
                }
            }
            if (active < 2)
                return false;

            for (auto& island : islands)
                if (!island.members.empty())
                {
                    const float normalizedSize = (float)island.members.size()
                        * (float)active / (float)std::max<size_t>(1, eligible);
                    island.radius = juce::jlimit(0.04f, 0.10f,
                        0.062f * std::sqrt(std::max(0.02f, normalizedSize)));
                }

            // Separation in the 2-D display only: the original CLAP/UMAP
            // neighbourhoods define the initial relative island positions.
            // Collision resolution only acts between islands, never randomly
            // shuffles individual sounds.
            for (int iteration = 0; iteration < 150; ++iteration)
            {
                std::array<juce::Point<float>, maxIslands> forces {};
                for (int a = 0; a < maxIslands; ++a)
                {
                    if (islands[(size_t)a].members.empty())
                        continue;
                    for (int b = a + 1; b < maxIslands; ++b)
                    {
                        if (islands[(size_t)b].members.empty())
                            continue;
                        const float dx = islands[(size_t)b].x
                                       - islands[(size_t)a].x;
                        const float dy = islands[(size_t)b].y
                                       - islands[(size_t)a].y;
                        const float minDist = islands[(size_t)a].radius
                            + islands[(size_t)b].radius + 0.025f;
                        const float dist = std::sqrt(dx * dx + dy * dy);
                        if (dist >= minDist)
                            continue;
                        const float angle = (float)(a * 17 + b * 29) * 1.173f;
                        const float nx = dist > 1.e-5f
                            ? dx / dist : std::cos(angle);
                        const float ny = dist > 1.e-5f
                            ? dy / dist : std::sin(angle);
                        const float penetration = (minDist - dist) * 0.43f;
                        forces[(size_t)a].x -= nx * penetration;
                        forces[(size_t)a].y -= ny * penetration;
                        forces[(size_t)b].x += nx * penetration;
                        forces[(size_t)b].y += ny * penetration;
                    }
                }
                for (int a = 0; a < maxIslands; ++a)
                {
                    auto& island = islands[(size_t)a];
                    if (island.members.empty())
                        continue;
                    // Weak spring preserves the overall CLAP/UMAP structure.
                    const float spring = 0.007f;
                    island.x += forces[(size_t)a].x
                        + spring * (island.originalX - island.x);
                    island.y += forces[(size_t)a].y
                        + spring * (island.originalY - island.y);
                    const float margin = island.radius + 0.025f;
                    island.x = juce::jlimit(margin, 1.0f - margin, island.x);
                    island.y = juce::jlimit(margin, 1.0f - margin, island.y);
                }
            }

            for (const auto& island : islands)
            {
                if (island.members.empty())
                    continue;
                const float scale = island.maximumDeviation > 0.00001f
                    ? island.radius * 0.86f / island.maximumDeviation
                    : 0.0f;
                for (size_t order = 0; order < island.members.size(); ++order)
                {
                    auto& rec = records[island.members[order]];
                    float dx = rec.neuralMapX - island.originalX;
                    float dy = rec.neuralMapY - island.originalY;
                    if (scale == 0.0f)
                    {
                        // Deterministic, minuscule spread for coincident data.
                        const float angle = (float)order * 2.399963f;
                        const float r = island.radius * 0.32f
                            * std::sqrt((float)(order + 1)
                            / (float)island.members.size());
                        dx = std::cos(angle) * r;
                        dy = std::sin(angle) * r;
                    }
                    else
                    {
                        dx *= scale;
                        dy *= scale;
                    }
                    rec.neuralMapX = juce::jlimit(0.0f, 1.0f, island.x + dx);
                    rec.neuralMapY = juce::jlimit(0.0f, 1.0f, island.y + dy);
                }
            }
            return true;
        }
    };
}
