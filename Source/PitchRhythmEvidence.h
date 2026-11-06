#pragma once
#include "AtlasTypes.h"
#include <cmath>

namespace Atlas
{
    // First-pass evidence labels only. These labels are intentionally NOT
    // used for automatic pitch correction or BPM synchronization.
    // Mono autocorrelation cannot reliably distinguish a chord from a tone.
    struct PitchRhythmEvidence
    {
        static void evaluate(SampleRecord& rec)
        {
            rec.pitchStructure = PitchStructure::NotEvaluated;
            rec.tempoEvidence = TempoEvidence::NotEvaluated;
            rec.pitchEvidenceStrength = 0.0f;
            rec.tempoEvidenceStrength = 0.0f;

            if (rec.tonal.valid)
            {
                const int windows = juce::jlimit(0, 3, rec.tonal.validWindowCount);
                const float periodicity = std::isfinite(rec.tonal.periodicity)
                    ? juce::jlimit(0.0f, 1.0f, rec.tonal.periodicity) : 0.0f;
                const float stability = std::isfinite(rec.tonal.pitchStability)
                    ? juce::jlimit(0.0f, 1.0f, rec.tonal.pitchStability) : 0.0f;
                rec.pitchEvidenceStrength =
                    periodicity * stability * ((float)windows / 3.0f);

                if (windows == 3 && periodicity >= 0.68f &&
                    stability >= 0.85f)
                    rec.pitchStructure = PitchStructure::StableToneCandidate;
                else if (windows < 2 || periodicity < 0.38f)
                    rec.pitchStructure = PitchStructure::NoReliableTone;
                else
                    rec.pitchStructure = PitchStructure::ComplexOrMoving;
            }

            // BPM metadata is a hint, not proof of audible rhythmic structure.
            // The existing 120 BPM default must never count as a detection.
            const auto name = rec.name.toLowerCase();
            const bool explicitlyTaggedBpm = name.contains("bpm");
            if (rec.isLoop && explicitlyTaggedBpm)
            {
                rec.tempoEvidence = TempoEvidence::TempoTaggedCandidate;
                rec.tempoEvidenceStrength = 0.5f; // unvalidated metadata
            }
            else
                rec.tempoEvidence = TempoEvidence::NoSyncEvidence;
        }
    };
}
