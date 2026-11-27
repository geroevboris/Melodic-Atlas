#pragma once
// 12: Move whole colour regions closer together on the *view* map.
// Existing organic island shapes, CLAP vectors, per-sound labels, filters,
// and cached neural coordinates are never modified. Within each colour
// region every point receives the SAME X translation.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace Atlas {
struct ColourRegionBlend12
{
    struct Point { float x = 0.5f, y = 0.5f; };

    static std::vector<Point> arrange(const std::vector<Point>& input,
                                      const std::vector<int>& colourRegions)
    {
        if (input.size() != colourRegions.size())
            return input;

        // Four spectral colours, independently for One-shots and Loops.
        // Values -1/invalid are not changed.
        constexpr int regions = 8;
        constexpr float maxShift = 0.105f;
        constexpr float boundaryOverlap = 0.009f;
        struct Region
        {
            std::vector<float> xs;
            float p05 = 0.0f, p95 = 0.0f;
            float minX = 1.0f, maxX = 0.0f, centre = 0.0f;
            float shift = 0.0f;
            float lo = 0.0f, hi = 0.0f;
            bool valid = false;
        };
        std::array<Region, regions> stats;
        for (size_t i = 0; i < input.size(); ++i)
        {
            const int group = colourRegions[i];
            if (group >= 0 && group < regions && std::isfinite(input[i].x))
                stats[(size_t)group].xs.push_back(input[i].x);
        }

        for (auto& group : stats)
        {
            if (group.xs.size() < 12)
                continue;
            std::sort(group.xs.begin(), group.xs.end());
            const size_t last = group.xs.size() - 1;
            group.minX = group.xs.front();
            group.maxX = group.xs.back();
            group.p05 = group.xs[(size_t)(0.05 * (double)last)];
            group.p95 = group.xs[(size_t)(0.95 * (double)last)];
            group.centre = group.xs[last / 2];
            // Never push points off the atlas. Translate each whole region,
            // not its individual islands or their internal point positions.
            group.lo = std::max(-maxShift, 0.002f - group.minX);
            group.hi = std::min(maxShift, 0.998f - group.maxX);
            group.valid = group.lo <= group.hi;
        }

        for (int tab = 0; tab < 2; ++tab)
        {
            // Only neighbouring spectral colours interact. Do not collapse
            // far-away colours or change musical low-to-high ordering.
            for (int iteration = 0; iteration < 18; ++iteration)
            {
                for (int colour = 0; colour < 3; ++colour)
                {
                    auto& left = stats[(size_t)(tab * 4 + colour)];
                    auto& right = stats[(size_t)(tab * 4 + colour + 1)];
                    if (!left.valid || !right.valid ||
                        right.centre <= left.centre)
                        continue;

                    // Percentile envelopes discount isolated outlier samples.
                    // A small negative gap gives modest natural colour mixing
                    // where the existing clusters already come close.
                    const float gap = (right.p05 + right.shift)
                                    - (left.p95 + left.shift);
                    if (gap <= -boundaryOverlap)
                        continue;

                    const float move = std::min(0.012f,
                        0.31f * (gap + boundaryOverlap));
                    const float moveLeft = std::max(0.0f,
                        std::min(move, left.hi - left.shift));
                    const float moveRight = std::max(0.0f,
                        std::min(move, right.shift - right.lo));
                    left.shift += moveLeft;
                    right.shift -= moveRight;
                }
            }
        }

        auto output = input;
        for (size_t i = 0; i < input.size(); ++i)
        {
            const int group = colourRegions[i];
            if (group >= 0 && group < regions && stats[(size_t)group].valid)
                output[i].x += stats[(size_t)group].shift;
        }
        return output;
    }
};
}
