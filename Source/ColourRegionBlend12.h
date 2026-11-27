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
            // Work on each contiguous run of real colour regions. A missing
            // spectral colour must not cause non-neighbouring colours to jump
            // toward one another. Each group moves rigidly in X.
            for (int start = tab * 4; start < tab * 4 + 4;)
            {
                while (start < tab * 4 + 4 &&
                       !stats[(size_t)start].valid)
                    ++start;
                if (start >= tab * 4 + 4)
                    break;
                int end = start + 1;
                while (end < tab * 4 + 4 &&
                       stats[(size_t)end].valid &&
                       stats[(size_t)end].centre >
                           stats[(size_t)(end - 1)].centre)
                    ++end;
                if (end - start < 2)
                {
                    start = end;
                    continue;
                }

                std::array<float, 4> shifts {};
                // Reduce the *gaps between whole colour bands*, not the
                // dimensions, shapes or internal layout of any CLAP island.
                for (int i = start + 1; i < end; ++i)
                {
                    const auto& left = stats[(size_t)(i - 1)];
                    const auto& right = stats[(size_t)i];
                    const float emptyGap = right.p05 - left.p95;
                    const float centreGap = right.centre - left.centre;
                    const float closing = std::min(
                        std::min(0.11f, std::max(0.0f,
                            emptyGap + boundaryOverlap)),
                        std::max(0.0f, centreGap - 0.025f));
                    shifts[(size_t)(i - start)] =
                        shifts[(size_t)(i - start - 1)] - closing;
                }

                // Centre the contraction so the entire map does not drift
                // left or right. Bound all shifts together to retain the
                // intended relative distances and original spectral order.
                float mean = 0.0f;
                for (int i = start; i < end; ++i)
                    mean += shifts[(size_t)(i - start)];
                mean /= (float)(end - start);
                float scale = 1.0f;
                for (int i = start; i < end; ++i)
                {
                    const auto& r = stats[(size_t)i];
                    const float desired = shifts[(size_t)(i - start)] - mean;
                    if (desired > 0.0f)
                        scale = std::min(scale, r.hi / desired);
                    else if (desired < 0.0f)
                        scale = std::min(scale, r.lo / desired);
                }
                scale = std::max(0.0f, std::min(1.0f, scale));
                for (int i = start; i < end; ++i)
                    stats[(size_t)i].shift =
                        (shifts[(size_t)(i - start)] - mean) * scale;
                start = end;
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
