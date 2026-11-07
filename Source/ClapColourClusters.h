#pragma once
#include "AtlasTypes.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
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

        // Two levels: find up to 18 fine neighbourhoods, then merge
        // the closest ones into six broad, colour-coded families.
        // Both levels use cosine similarity in original CLAP space.
        static constexpr int fineGroups = 18;
        static constexpr int algorithmVersion = 2;

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

            // Preserve colour families from THIS algorithm when refreshing.
            // Old six-way k-means assignments do not qualify.
            std::array<Centre, groups> existing {};
            std::array<int, groups> existingCount {};
            int established = 0;
            for (size_t i : valid)
            {
                const auto& rec = records[i];
                if (rec.colourAlgorithmVersion != algorithmVersion
                    || rec.cachedColourGroup < 0
                    || rec.cachedColourGroup >= groups)
                    continue;
                const int g = rec.cachedColourGroup;
                ++existingCount[(size_t)g];
                for (int d = 0; d < dimensions; ++d)
                    existing[(size_t)g][(size_t)d] +=
                        rec.clapEmbedding[(size_t)d];
            }
            for (int g = 0; g < groups; ++g)
                if (existingCount[(size_t)g] != 0)
                {
                    normalize(existing[(size_t)g]);
                    ++established;
                }
            if (established == groups)
            {
                for (size_t i : valid)
                {
                    auto& rec = records[i];
                    // Keep previously assigned samples stable; use nearest
                    // established family for genuinely new embeddings.
                    rec.colourGroup = rec.colourAlgorithmVersion == algorithmVersion
                        && rec.cachedColourGroup >= 0
                        && rec.cachedColourGroup < groups
                            ? rec.cachedColourGroup
                            : nearest(rec.clapEmbedding, existing, groups);
                    rec.colourAlgorithmVersion = algorithmVersion;
                }
                return;
            }

            std::sort(valid.begin(), valid.end(), [&](size_t a, size_t b) {
                return records[a].file.getFullPathName().compare(
                    records[b].file.getFullPathName()) < 0;
            });
            const int k = std::min((int)valid.size(), fineGroups);
            std::vector<Centre> fine((size_t)k);
            fine[0] = records[valid[0]].clapEmbedding;
            for (int c = 1; c < k; ++c)
            {
                float farthestScore = std::numeric_limits<float>::infinity();
                size_t farthest = valid[0];
                for (size_t index : valid)
                {
                    float best = -std::numeric_limits<float>::infinity();
                    for (int j = 0; j < c; ++j)
                        best = std::max(best, dot(records[index].clapEmbedding,
                                                  fine[(size_t)j]));
                    if (best < farthestScore)
                    {
                        farthestScore = best;
                        farthest = index;
                    }
                }
                fine[(size_t)c] = records[farthest].clapEmbedding;
            }

            std::vector<int> assignment(records.size(), -1);
            std::vector<int> sizes((size_t)k);
            for (int iter = 0; iter < 16; ++iter)
            {
                std::vector<Centre> sums((size_t)k);
                std::fill(sizes.begin(), sizes.end(), 0);
                bool changed = false;
                for (size_t index : valid)
                {
                    const int c = nearest(records[index].clapEmbedding, fine, k);
                    if (assignment[index] != c)
                        changed = true;
                    assignment[index] = c;
                    ++sizes[(size_t)c];
                    for (int d = 0; d < dimensions; ++d)
                        sums[(size_t)c][(size_t)d] +=
                            records[index].clapEmbedding[(size_t)d];
                }
                for (int c = 0; c < k; ++c)
                    if (sizes[(size_t)c] != 0)
                    {
                        normalize(sums[(size_t)c]);
                        fine[(size_t)c] = sums[(size_t)c];
                    }
                if (!changed)
                    break;
            }

            struct Family
            {
                Centre sum {};
                int size = 0;
                std::vector<int> members;
            };
            std::vector<Family> families;
            for (int c = 0; c < k; ++c)
                if (sizes[(size_t)c] > 0)
                {
                    Family f;
                    f.size = sizes[(size_t)c];
                    f.members.push_back(c);
                    for (int d = 0; d < dimensions; ++d)
                        f.sum[(size_t)d] =
                            fine[(size_t)c][(size_t)d] * (float)f.size;
                    families.push_back(std::move(f));
                }

            // Agglomerative merging by highest cosine similarity of family
            // centroids; no arbitrary 2-D spatial proximity is involved.
            while (families.size() > (size_t)groups)
            {
                float best = -std::numeric_limits<float>::infinity();
                size_t bestA = 0, bestB = 1;
                for (size_t a = 0; a < families.size(); ++a)
                    for (size_t b = a + 1; b < families.size(); ++b)
                    {
                        Centre ca = families[a].sum;
                        Centre cb = families[b].sum;
                        normalize(ca);
                        normalize(cb);
                        const float similarity = dot(ca, cb);
                        if (similarity > best)
                        {
                            best = similarity;
                            bestA = a;
                            bestB = b;
                        }
                    }
                for (int d = 0; d < dimensions; ++d)
                    families[bestA].sum[(size_t)d] +=
                        families[bestB].sum[(size_t)d];
                families[bestA].size += families[bestB].size;
                families[bestA].members.insert(families[bestA].members.end(),
                    families[bestB].members.begin(), families[bestB].members.end());
                families.erase(families.begin() + (std::ptrdiff_t)bestB);
            }

            std::vector<int> fineToBroad((size_t)k, 0);
            for (size_t group = 0; group < families.size(); ++group)
                for (int fineIndex : families[group].members)
                    fineToBroad[(size_t)fineIndex] = (int)group;

            for (size_t index : valid)
            {
                auto& rec = records[index];
                rec.colourGroup = fineToBroad[(size_t)assignment[index]];
                rec.colourAlgorithmVersion = algorithmVersion;
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

        template<typename Container>
        static int nearest(const Centre& vector,
                           const Container& centres,
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
