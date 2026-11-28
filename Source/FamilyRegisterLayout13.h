#pragma once
// 13: A compact CLAP family is the atomic map island. Its *whole-family*
// spectral profile decides where it lives on LOW -> HIGH X. Individual
// register measurements add only a small bounded within-island nudge.
// This is geometry only: classification, CLAP embeddings and persisted
// source UMAP projections are never modified.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace Atlas
{
    struct FamilyRegisterLayout13
    {
        static constexpr int maxFamilies = 18;
        struct Member
        {
            int family = -1;
            float sourceX = 0.5f;
            float sourceY = 0.5f;
            float registerX = 0.5f;
        };
        struct Position
        {
            float x = 0.5f, y = 0.5f;
            int zone = -1;
        };

        static float clamp(float value, float lo, float hi)
        {
            return std::max(lo, std::min(hi, value));
        }

        static int zoneForX(float x)
        {
            return std::max(0, std::min(3,
                (int)std::floor(clamp(x, 0.0f, 0.99999f) * 4.0f)));
        }

        static std::vector<Position> arrange(const std::vector<Member>& input)
        {
            std::vector<Position> output;
            output.reserve(input.size());
            for (const auto& m : input)
                output.push_back({m.sourceX, m.sourceY,
                    std::isfinite(m.registerX) ? zoneForX(m.registerX) : -1});

            struct Family
            {
                std::vector<size_t> members;
                float sumSourceX = 0.0f, sumSourceY = 0.0f;
                float sumRegister = 0.0f;
                std::array<int, 4> counts {};
                std::array<float, 4> sums {};
                float sourceX = 0.5f, sourceY = 0.5f;
                float anchorX = 0.5f, anchorY = 0.5f;
                float centreX = 0.5f, centreY = 0.5f;
                float radius = 0.035f;
                float scale = 0.0f;
            };
            std::array<Family, maxFamilies> families {};
            size_t assigned = 0;
            for (size_t i = 0; i < input.size(); ++i)
            {
                const auto& m = input[i];
                if (m.family < 0 || m.family >= maxFamilies
                    || !std::isfinite(m.sourceX)
                    || !std::isfinite(m.sourceY)
                    || !std::isfinite(m.registerX))
                    continue;
                auto& group = families[(size_t)m.family];
                group.members.push_back(i);
                group.sumSourceX += m.sourceX;
                group.sumSourceY += m.sourceY;
                const float frequency = clamp(m.registerX, 0.02f, 0.98f);
                group.sumRegister += frequency;
                const int zone = zoneForX(frequency);
                ++group.counts[(size_t)zone];
                group.sums[(size_t)zone] += frequency;
                ++assigned;
            }

            int active = 0;
            for (const auto& group : families)
                if (!group.members.empty())
                    ++active;
            if (active == 0)
                return output;

            for (auto& group : families)
            {
                const float count = (float)group.members.size();
                if (group.members.empty())
                    continue;
                group.sourceX = group.sumSourceX / count;
                group.sourceY = group.sumSourceY / count;

                // A clear >=70% majority keeps its family in that register.
                // Mixed neighbouring registers (e.g. 60/40) naturally locate
                // the family near their common boundary. Neither case splits
                // a CLAP family into four distant islands.
                float centre = group.sumRegister / count;
                const int majority = (int)std::distance(group.counts.begin(),
                    std::max_element(group.counts.begin(), group.counts.end()));
                if (group.counts[(size_t)majority] >= 0.70f * count)
                {
                    const float majorityMean = group.sums[(size_t)majority]
                        / (float)group.counts[(size_t)majority];
                    centre = 0.74f * majorityMean + 0.26f * centre;
                    const float start = (float)majority * 0.25f;
                    centre = clamp(centre, start + 0.022f, start + 0.228f);
                }

                const float expected = (float)assigned / (float)active;
                const float relative = count / std::max(1.0f, expected);
                group.radius = clamp(
                    0.044f * std::sqrt(std::max(0.18f, relative)),
                    0.025f, 0.069f);
                const float margin = group.radius + 0.013f;
                group.anchorX = clamp(centre, margin, 1.0f - margin);
                group.anchorY = clamp(group.sourceY, margin, 1.0f - margin);
                group.centreX = group.anchorX;
                group.centreY = group.anchorY;

                // Normalise the original CLAP/UMAP neighbourhood into a
                // bounded cloud: no far-away outlier can stretch the family
                // across the map. Y remains determined by CLAP neighbours.
                std::vector<float> distances;
                distances.reserve(group.members.size());
                for (const size_t i : group.members)
                {
                    const float dx = input[i].sourceX - group.sourceX;
                    const float dy = input[i].sourceY - group.sourceY;
                    distances.push_back(std::sqrt(dx * dx + dy * dy));
                }
                const size_t percentile = (size_t)(0.90 *
                    (double)(distances.size() - 1));
                std::nth_element(distances.begin(),
                    distances.begin() + (std::ptrdiff_t)percentile,
                    distances.end());
                group.scale = group.radius * 0.76f /
                    std::max(0.0001f, distances[percentile]);
            }

            // Allow adjacent acoustically-related clouds to touch, but avoid
            // turning the entire map into an undifferentiated pile. Family
            // centres may move only slightly around their frequency anchor.
            for (int iteration = 0; iteration < 90 && active > 1; ++iteration)
            {
                std::array<float, maxFamilies> fx {}, fy {};
                for (int a = 0; a < maxFamilies; ++a)
                {
                    if (families[(size_t)a].members.empty())
                        continue;
                    for (int b = a + 1; b < maxFamilies; ++b)
                    {
                        const auto& left = families[(size_t)a];
                        const auto& right = families[(size_t)b];
                        if (right.members.empty())
                            continue;
                        const float dx = right.centreX - left.centreX;
                        const float dy = right.centreY - left.centreY;
                        const float dist = std::sqrt(dx * dx + dy * dy);
                        const float threshold = (left.radius + right.radius)
                            * 0.89f;
                        if (dist >= threshold)
                            continue;
                        const float angle = (float)(a * 17 + b * 29)
                            * 1.173f;
                        const float nx = dist > 0.0001f
                            ? dx / dist : std::cos(angle);
                        const float ny = dist > 0.0001f
                            ? dy / dist : std::sin(angle);
                        const float push = (threshold - dist) * 0.24f;
                        fx[(size_t)a] -= nx * push;
                        fy[(size_t)a] -= ny * push;
                        fx[(size_t)b] += nx * push;
                        fy[(size_t)b] += ny * push;
                    }
                }
                for (int i = 0; i < maxFamilies; ++i)
                {
                    auto& group = families[(size_t)i];
                    if (group.members.empty())
                        continue;
                    const float margin = group.radius + 0.013f;
                    group.centreX = clamp(
                        group.centreX + fx[(size_t)i]
                            + 0.08f * (group.anchorX - group.centreX),
                        std::max(margin, group.anchorX - 0.047f),
                        std::min(1.0f - margin, group.anchorX + 0.047f));
                    group.centreY = clamp(
                        group.centreY + fy[(size_t)i]
                            + 0.08f * (group.anchorY - group.centreY),
                        std::max(margin, group.anchorY - 0.15f),
                        std::min(1.0f - margin, group.anchorY + 0.15f));
                }
            }

            for (const auto& group : families)
            {
                for (size_t index : group.members)
                {
                    const auto& m = input[index];
                    float dx = (m.sourceX - group.sourceX) * group.scale;
                    float dy = (m.sourceY - group.sourceY) * group.scale;
                    // A bright member can be on the right edge of its
                    // low-mid family, but cannot fly into distant HIGH.
                    dx += clamp((m.registerX - group.anchorX) * 0.18f,
                        -group.radius * 0.18f, group.radius * 0.18f);
                    const float dist = std::sqrt(dx * dx + dy * dy);
                    const float maxDistance = group.radius * 0.92f;
                    if (dist > maxDistance)
                    {
                        dx *= maxDistance / dist;
                        dy *= maxDistance / dist;
                    }
                    const float x = clamp(group.centreX + dx, 0.006f, 0.994f);
                    const float y = clamp(group.centreY + dy, 0.006f, 0.994f);
                    // Colour ALWAYS follows horizontal register position;
                    // there are no arbitrary per-family colour mixes.
                    output[index] = {x, y, zoneForX(x)};
                }
            }
            return output;
        }
    };
}
