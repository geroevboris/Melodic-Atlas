#pragma once
#include "AtlasTypes.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

namespace Atlas
{
    // Colour is determined by semantic/audio neighbourhood in the ORIGINAL
    // 512-D CLAP embedding, not by instrument metadata, arbitrary timbre
    // thresholds, or distance in the distorted 2-D UMAP view.
    struct ClapColourClusters
    {
        static constexpr int groups = 6;
        static constexpr int dimensions = 512;
        using Centre = std::array<float, dimensions>;

        static void apply(std::vector<SampleRecord>& records)
        {
            std::vector<size_t> valid;
            valid.reserve(records.size());
            for (size_t i = 0; i < records.size(); ++i)
            {
                records[i].colourGroup = -1;
                if (records[i].clapEmbeddingValid)
                    valid.push_back(i);
            }
            if (valid.empty())
                return;

            const int k = std::min(groups, (int)valid.size());
            // Preserve known colour families on an incremental refresh:
            // recompute old centres, then allocate new sounds to them.
            // Avoid changing every colour merely because a new sample arrived.
            std::array<Centre, groups> centres {};
            std::array<int, groups> counts {};
            int established = 0;
            for (size_t i : valid)
            {
                const auto& rec = records[i];
                if (rec.cachedColourGroup < 0 || rec.cachedColourGroup >= groups)
                    continue;
                const int group = rec.cachedColourGroup;
                ++counts[(size_t)group];
                for (int d = 0; d < dimensions; ++d)
                    centres[(size_t)group][(size_t)d] += rec.clapEmbedding[(size_t)d];
            }
            for (int c = 0; c < groups; ++c)
                if (counts[(size_t)c] > 0)
                {
                    normalize(centres[(size_t)c]);
                    ++established;
                }

            if (established == groups)
            {
                for (size_t i : valid)
                    records[i].colourGroup = nearest(
                        records[i].clapEmbedding, centres, groups);
                return;
            }

            // Deterministic spherical k-means. Farthest-point seeds give
            // broad, well-spaced groups without random initialisation.
            // File order is explicitly stabilized for repeatable scans.
            std::sort(valid.begin(), valid.end(), [&](size_t a, size_t b) {
                return records[a].file.getFullPathName()
                     < records[b].file.getFullPathName();
            });
            centres[0] = records[valid[0]].clapEmbedding;
            for (int c = 1; c < k; ++c)
            {
                size_t farthest = valid[0];
                float smallestBest = std::numeric_limits<float>::infinity();
                for (size_t i : valid)
                {
                    float best = -std::numeric_limits<float>::infinity();
                    for (int j = 0; j < c; ++j)
                        best = std::max(best,
                            dot(records[i].clapEmbedding, centres[(size_t)j]));
                    if (best < smallestBest)
                    {
                        smallestBest = best;
                        farthest = i;
                    }
                }
                centres[(size_t)c] = records[farthest].clapEmbedding;
            }

            for (int iteration = 0; iteration < 16; ++iteration)
            {
                std::array<Centre, groups> sums {};
                std::array<int, groups> sizes {};
                bool changed = false;
                for (size_t i : valid)
                {
                    auto& rec = records[i];
                    const int group = nearest(rec.clapEmbedding, centres, k);
                    if (rec.colourGroup != group)
                        changed = true;
                    rec.colourGroup = group;
                    ++sizes[(size_t)group];
                    for (int d = 0; d < dimensions; ++d)
                        sums[(size_t)group][(size_t)d] +=
                            rec.clapEmbedding[(size_t)d];
                }
                for (int c = 0; c < k; ++c)
                    if (sizes[(size_t)c] > 0)
                    {
                        normalize(sums[(size_t)c]);
                        centres[(size_t)c] = sums[(size_t)c];
                    }
                if (!changed)
                    break;
            }
        }

    private:
        static float dot(const Centre& a, const Centre& b)
        {
            double result = 0.0;
            for (int d = 0; d < dimensions; ++d)
                result += (double)a[(size_t)d] * b[(size_t)d];
            return (float)result;
        }

        static void normalize(Centre& a)
        {
            double lengthSq = 0.0;
            for (float v : a)
                lengthSq += (double)v * v;
            const float inv = (float)(1.0 / std::sqrt(std::max(1.0e-12, lengthSq)));
            for (float& v : a)
                v *= inv;
        }

        static int nearest(const Centre& vector,
                           const std::array<Centre, groups>& centres,
                           int count)
        {
            int result = 0;
            float best = -std::numeric_limits<float>::infinity();
            for (int c = 0; c < count; ++c)
            {
                const float score = dot(vector, centres[(size_t)c]);
                if (score > best)
                {
                    best = score;
                    result = c;
                }
            }
            return result;
        }
    };
}
