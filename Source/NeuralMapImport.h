#pragma once

#include "AtlasTypes.h"
#include <cmath>
#include <unordered_map>
#include <vector>

namespace Atlas
{
    // Import previously computed Pure CLAP + UMAP Balanced coordinates.
    // No ML code, file writes or process launches happen on the audio thread.
    // The manifest is treated as untrusted external data: all records and
    // file metadata must match before any coordinate is committed.
    struct NeuralMapImport
    {
        static bool apply(const juce::File& scannedFolder,
                          std::vector<SampleRecord>& records)
        {
            if (!scannedFolder.isDirectory() || records.empty())
                return false;

            const auto manifest = scannedFolder.getChildFile(".atlas_neural_map_v1.json");
            if (!manifest.existsAsFile())
                return false;

            // Reject unexpectedly large files (e.g. a corrupted manifest).
            if (manifest.getSize() > 100 * 1024 * 1024)
                return false;

            const auto parsed = juce::JSON::parse(manifest.loadFileAsString());
            auto* obj = parsed.getDynamicObject();
            if (obj == nullptr || (int)obj->getProperty("schema") != 1
                || obj->getProperty("method").toString() != "clap-umap-balanced"
                || obj->getProperty("source").toString()
                    != scannedFolder.getFullPathName()
                || !obj->hasProperty("samples"))
                return false;

            auto* entries = obj->getProperty("samples").getArray();
            if (entries == nullptr || entries->size() != (int)records.size())
                return false;

            struct Entry
            {
                float x = 0.5f;
                float y = 0.5f;
                int64_t bytes = -1;
                int64_t modifiedMs = -1;
            };
            std::unordered_map<std::string, Entry> byRelativePath;
            byRelativePath.reserve((size_t)entries->size());

            for (const auto& item : *entries)
            {
                auto* entry = item.getDynamicObject();
                if (entry == nullptr)
                    return false;
                const auto relative = entry->getProperty("file").toString()
                    .replaceCharacter('\\', '/');
                if (relative.isEmpty() || relative.startsWithChar('/')
                    || relative.startsWith("../")
                    || relative.contains("/../"))
                    return false;

                const double x = (double)entry->getProperty("x");
                const double y = (double)entry->getProperty("y");
                if (!std::isfinite(x) || !std::isfinite(y)
                    || x < 0.0 || x > 1.0 || y < 0.0 || y > 1.0)
                    return false;

                Entry value;
                value.x = (float)x;
                value.y = (float)y;
                value.bytes = (int64_t)entry->getProperty("bytes");
                value.modifiedMs = (int64_t)entry->getProperty("modifiedMs");
                if (value.bytes < 0 || value.modifiedMs < 0)
                    return false;

                if (!byRelativePath.emplace(relative.toStdString(), value).second)
                    return false;
            }

            // Validate everything first. If one file changed or a file was
            // skipped during inference, the plugin keeps the whole DSP map.
            std::vector<Entry> ordered;
            ordered.reserve(records.size());
            for (const auto& rec : records)
            {
                const auto relative = rec.file.getRelativePathFrom(scannedFolder)
                    .replaceCharacter('\\', '/');
                const auto it = byRelativePath.find(relative.toStdString());
                if (it == byRelativePath.end()
                    || rec.file.getSize() != it->second.bytes
                    || std::abs(rec.file.getLastModificationTime().toMilliseconds()
                                - it->second.modifiedMs) > 2000)
                    return false;
                ordered.push_back(it->second);
            }

            for (size_t i = 0; i < records.size(); ++i)
            {
                records[i].neuralMapX = ordered[i].x;
                records[i].neuralMapY = ordered[i].y;
                records[i].neuralMapValid = true;
            }
            return true;
        }
    };
}
