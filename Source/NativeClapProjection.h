#pragma once
#include "AtlasTypes.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#if defined(ATLAS_ENABLE_NATIVE_CLAP) && ATLAS_ENABLE_NATIVE_CLAP
#include <umappp/umappp.hpp>

namespace Atlas
{
    // Native CPU UMAP for initial layout, stable nearest-neighbor transform
    // for incremental Refresh. Expensive layout fitting happens only on the
    // scanner thread. Existing coordinates remain immutable on Refresh.
    struct NativeClapProjection
    {
        using OldRecords = std::unordered_map<std::string, SampleRecord>;
        static constexpr int dimensions = 512;

        static bool apply(std::vector<SampleRecord>& records,
                          const OldRecords& old)
        {
            if (records.size() < 4)
                return false;
            for (const auto& r : records)
                if (!r.clapEmbeddingValid)
                    return false;

            std::vector<int> anchors, newcomers;
            anchors.reserve(records.size());
            newcomers.reserve(records.size());
            for (int i = 0; i < (int)records.size(); ++i)
            {
                auto& rec = records[(size_t)i];
                const auto it = old.find(rec.file.getFullPathName().toStdString());
                if (it != old.end() && it->second.clapEmbeddingValid
                    && it->second.neuralMapValid
                    && it->second.sourceBytes == rec.sourceBytes
                    && it->second.sourceModifiedMs == rec.sourceModifiedMs)
                {
                    rec.neuralMapX = it->second.neuralMapX;
                    rec.neuralMapY = it->second.neuralMapY;
                    rec.neuralMapValid = true;
                    anchors.push_back(i);
                }
                else
                    newcomers.push_back(i);
            }

            if (anchors.size() >= 4)
            {
                // Use established CLAP geometry; never refit and reshuffle
                // the library merely because a few files were added.
                for (int index : newcomers)
                {
                    auto& target = records[(size_t)index];
                    struct Neighbor { float similarity; int index; };
                    std::vector<Neighbor> nearest;
                    nearest.reserve(8);
                    for (int anchor : anchors)
                    {
                        const auto& base = records[(size_t)anchor];
                        double similarity = 0.0;
                        for (int d = 0; d < dimensions; ++d)
                            similarity += (double)target.clapEmbedding[(size_t)d]
                                          * base.clapEmbedding[(size_t)d];
                        const float sim = (float)similarity;
                        const auto where = std::lower_bound(
                            nearest.begin(), nearest.end(), sim,
                            [](const Neighbor& candidate, float value)
                            { return candidate.similarity > value; });
                        nearest.insert(where, { sim, anchor });
                        if (nearest.size() > 8)
                            nearest.pop_back();
                    }

                    double xx = 0.0, yy = 0.0, weightSum = 0.0;
                    for (const auto& neighbor : nearest)
                    {
                        const auto& base = records[(size_t)neighbor.index];
                        // Sharpen cosine similarities so a dissimilar anchor
                        // does not pull a newcomer toward an unrelated region.
                        const double weight = std::pow(
                            std::max(0.0f, neighbor.similarity), 8.0);
                        weightSum += weight;
                        xx += weight * base.neuralMapX;
                        yy += weight * base.neuralMapY;
                    }
                    if (weightSum < 1e-8)
                        return false;
                    target.neuralMapX = juce::jlimit(0.0f, 1.0f,
                        (float)(xx / weightSum));
                    target.neuralMapY = juce::jlimit(0.0f, 1.0f,
                        (float)(yy / weightSum));
                    target.neuralMapValid = true;
                }
                return true;
            }

            // First scan, or a new library: fit UMAP directly on CLAP data.
            const int n = (int)records.size();
            std::vector<double> vectors((size_t)n * dimensions);
            for (int i = 0; i < n; ++i)
                for (int d = 0; d < dimensions; ++d)
                    vectors[(size_t)i * dimensions + (size_t)d] =
                        records[(size_t)i].clapEmbedding[(size_t)d];

            knncolle::VptreeBuilder<int, double, double> neighbors(
                std::make_shared<knncolle::EuclideanDistance<double, double>>());
            umappp::Options options;
            options.num_neighbors = std::min(12, n - 1);
            options.num_epochs = 250;
            options.min_dist = 0.055;
            std::vector<double> coords((size_t)n * 2);
            auto status = umappp::initialize(
                dimensions, n, vectors.data(), neighbors,
                2, coords.data(), options);
            status.run(coords.data());

            double minX = std::numeric_limits<double>::infinity();
            double minY = minX;
            double maxX = -minX, maxY = -minX;
            for (int i = 0; i < n; ++i)
            {
                const double x = coords[(size_t)i * 2];
                const double y = coords[(size_t)i * 2 + 1];
                if (!std::isfinite(x) || !std::isfinite(y))
                    return false;
                minX = std::min(minX, x);
                minY = std::min(minY, y);
                maxX = std::max(maxX, x);
                maxY = std::max(maxY, y);
            }
            for (int i = 0; i < n; ++i)
            {
                auto& r = records[(size_t)i];
                r.neuralMapX = (float)(0.04 + 0.92 *
                    (coords[(size_t)i * 2] - minX) /
                    std::max(1e-8, maxX - minX));
                r.neuralMapY = (float)(0.04 + 0.92 *
                    (coords[(size_t)i * 2 + 1] - minY) /
                    std::max(1e-8, maxY - minY));
                r.neuralMapValid = true;
            }
            return true;
        }
    };
}
#endif
