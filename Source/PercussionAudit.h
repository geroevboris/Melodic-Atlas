#pragma once
#include "AtlasTypes.h"
#include "PercussionGate.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace Atlas
{
    // Scanner-thread-only diagnostics. Does not affect classification or the map.
    // The CSV is regenerated on Refresh, and only relative sample paths are stored.
    struct PercussionAudit
    {
        using Embedding = std::array<float, 512>;

        static void write(const std::vector<SampleRecord>& records,
                          const std::vector<Embedding>& drumExamples,
                          const juce::File& sourceFolder)
        {
            const juce::File report =
                juce::File::getSpecialLocation(
                    juce::File::userApplicationDataDirectory)
                .getChildFile("MelodicAtlas")
                .getChildFile("PercussionAudit_8O.csv");
            if (report.getParentDirectory().createDirectory().failed())
                return;
            auto output = report.createOutputStream();
            if (!output)
                return;

            const auto csv = [](juce::String s)
            {
                return "\"" + s.replace("\"", "\"\"") + "\"";
            };
            const auto writeLine = [&output](const juce::String& line)
            {
                output->writeText(line + "\n", false, false, "\n");
            };

            writeLine("relative_path,duration_seconds,active_seconds,is_loop,"
                      "percussion_state,percussion_confidence,"
                      "acoustic_flatness,onset_activity,sustain_ratio,"
                      "pitch_periodicity,pitch_stability,tonal_windows,"
                      "clap_valid,drum_reference_cosine,"
                      "instrument_identity,source_origin,excluded_by_gate");

            for (const auto& rec : records)
            {
                float nearestDrum = -1.0f;
                if (rec.clapEmbeddingValid)
                {
                    for (const auto& example : drumExamples)
                    {
                        double dot = 0.0;
                        for (size_t j = 0; j < example.size(); ++j)
                            dot += (double)rec.clapEmbedding[j] * example[j];
                        if (std::isfinite(dot))
                            nearestDrum = std::max(nearestDrum, (float)dot);
                    }
                }

                const auto relativePath = rec.file.getRelativePathFrom(sourceFolder);
                writeLine(csv(relativePath) + ","
                    + juce::String(rec.durationSec, 5) + ","
                    + juce::String(rec.effectiveDurationSec, 5) + ","
                    + juce::String(rec.isLoop ? 1 : 0) + ","
                    + juce::String((int)rec.percussionState) + ","
                    + juce::String(rec.percussionConfidence, 5) + ","
                    + juce::String(rec.acoustic.spectralFlatness, 5) + ","
                    + juce::String(rec.acoustic.onsetActivity, 5) + ","
                    + juce::String(rec.acoustic.sustainRatio, 5) + ","
                    + juce::String(rec.tonal.periodicity, 5) + ","
                    + juce::String(rec.tonal.pitchStability, 5) + ","
                    + juce::String(rec.tonal.validWindowCount) + ","
                    + juce::String(rec.clapEmbeddingValid ? 1 : 0) + ","
                    + juce::String(nearestDrum, 5) + ","
                    + juce::String((int)rec.identityCluster) + ","
                    + juce::String(rec.identityOrigin) + ","
                    + juce::String(PercussionGate::shouldExcludeFromAtlas(rec) ? 1 : 0));
            }
            output->flush();
        }
    };
}
