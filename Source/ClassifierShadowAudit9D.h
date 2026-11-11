#pragma once
#include "AtlasTypes.h"
#include "PercussionGate.h"
#include "ClapTextPrototypeCandidate9C.h"
#include <JuceHeader.h>
#include <algorithm>
#include <cmath>
#include <vector>

// 9D scanner-only shadow report. Does not change ANY production verdict.
// Text-to-audio CLAP cosines are not calibrated probabilities. In particular,
// a top-1 'Drums' match does NOT cause a sample to be hidden.
namespace Atlas
{
    struct ClassifierShadowAudit9D
    {
        static juce::File reportFile()
        {
            return juce::File::getSpecialLocation(
                juce::File::userApplicationDataDirectory)
                .getChildFile("MelodicAtlas")
                .getChildFile("ClassifierShadow_9D.csv");
        }

        static void write(const std::vector<SampleRecord>& classified,
                          const std::vector<SampleRecord>& earlyExcluded,
                          const std::vector<juce::File>& metadataExcluded,
                          const juce::File& sourceFolder)
        {
            const auto target = reportFile();
            if (target.getParentDirectory().createDirectory().failed())
                return;
            juce::TemporaryFile temp(target);
            auto out = temp.getFile().createOutputStream();
            if (out == nullptr)
                return;

            const auto quote = [](juce::String s)
            {
                return "\"" + s.replace("\"", "\"\"") + "\"";
            };
            const auto strFloat = [](float value) -> juce::String
            {
                return std::isfinite(value) ? juce::String(value, 7) : juce::String();
            };
            juce::StringArray header { "relative_path", "scan_stage",
                "duration_s", "active_duration_s",
                "legacy_is_loop", "loop_override", "loop_version",
                "tempo_evidence", "tempo_evidence_strength",
                "pitch_structure", "onset_activity", "sustain_ratio",
                "spectral_flatness", "tonal_windows", "tonal_periodicity",
                "tonal_stability", "rhythmic_character",
                "old_identity_state", "old_instrument_family",
                "old_identity_origin", "old_percussion_state",
                "old_percussion_confidence", "old_excluded_from_map",
                "clap_valid", "shadow_top_family", "shadow_top_subtype",
                "shadow_top_cosine", "shadow_second_family",
                "shadow_second_cosine", "shadow_margin",
                "shadow_best_melodic_cosine", "shadow_drums_cosine",
                "shadow_drums_minus_melodic", "shadow_drums_top1",
                "old_vs_shadow_family_disagree",
                "old_vs_shadow_drums_disagree" };
            for (int i = 0; i < ClapTextBank9C::familyCount; ++i)
                header.add("cosine_" + juce::String(
                    ClapTextBank9C::familyNames[i]));
            const auto write = [&out](const juce::StringArray& row)
            {
                out->writeText(row.joinIntoString(",") + "\n", false, false, "\n");
            };
            write(header);

            const auto familyName = [](int index) -> juce::String
            {
                if (index < 0 || index >= ClapTextBank9C::familyCount)
                    return {};
                return ClapTextBank9C::familyNames[index];
            };
            const auto oldFamily = [](const SampleRecord& rec) -> juce::String
            {
                if (rec.identityState != IdentityState::Identified)
                    return {};
                switch (rec.identityCluster)
                {
                    case ClusterGroup::Keys: return "Keys";
                    case ClusterGroup::Synth: return "Synth";
                    case ClusterGroup::Bass: return "Bass";
                    case ClusterGroup::Vocal: return "Vocal";
                    case ClusterGroup::Strings: return "StringsOrWinds";
                    case ClusterGroup::Guitar: return "Guitar";
                    case ClusterGroup::FX: return "FX";
                    default: return {};
                }
            };

            const auto writeSample = [&](const SampleRecord& rec,
                                         const juce::String& stage)
            {
                const auto result = rec.clapEmbeddingValid
                    ? ClapTextPrototypeCandidate9C::evaluate(rec.clapEmbedding)
                    : ClapTextScores9C {};
                const bool oldDrum = PercussionGate::shouldExcludeFromAtlas(rec);
                const bool shadowDrum = result.valid
                    && result.topFamily == ClapTextBank9C::familyCount - 1;
                float bestMelodic = -1.0f;
                if (result.valid)
                    for (int i = 0; i < ClapTextBank9C::familyCount - 1; ++i)
                        bestMelodic = std::max(bestMelodic,
                            result.familyCosines[(size_t)i]);
                const float drum = result.valid
                    ? result.familyCosines[(size_t)(ClapTextBank9C::familyCount - 1)]
                    : -1.0f;
                const auto source = oldFamily(rec);
                // In the legacy taxonomy Strings also contains Winds.
                const bool familyDiff = result.valid && source.isNotEmpty()
                    && !(source == familyName(result.topFamily)
                        || (source == "StringsOrWinds"
                            && (result.topFamily == 4 || result.topFamily == 5)));

                juce::StringArray row;
                row.add(quote(rec.file.getRelativePathFrom(sourceFolder)));
                row.add(stage);
                row.add(juce::String(rec.durationSec, 5));
                row.add(juce::String(rec.effectiveDurationSec, 5));
                row.add(juce::String(rec.isLoop ? 1 : 0));
                row.add(juce::String(rec.loopUserOverride));
                row.add(juce::String(rec.loopAnalysisVersion));
                row.add(juce::String((int)rec.tempoEvidence));
                row.add(strFloat(rec.tempoEvidenceStrength));
                row.add(juce::String((int)rec.pitchStructure));
                row.add(strFloat(rec.acoustic.onsetActivity));
                row.add(strFloat(rec.acoustic.sustainRatio));
                row.add(strFloat(rec.acoustic.spectralFlatness));
                row.add(juce::String(rec.tonal.validWindowCount));
                row.add(strFloat(rec.tonal.periodicity));
                row.add(strFloat(rec.tonal.pitchStability));
                row.add(juce::String(
                    (rec.characterMask & Character_Rhythmic) != 0 ? 1 : 0));
                row.add(juce::String((int)rec.identityState));
                row.add(source);
                row.add(juce::String(rec.identityOrigin));
                row.add(juce::String((int)rec.percussionState));
                row.add(strFloat(rec.percussionConfidence));
                row.add(juce::String(oldDrum ? 1 : 0));
                row.add(juce::String(result.valid ? 1 : 0));
                row.add(result.valid ? familyName(result.topFamily) : juce::String());
                row.add(result.valid && result.topPrompt >= 0
                    ? juce::String(ClapTextBank9C::prompts[(size_t)result.topPrompt].subtype)
                    : juce::String());
                row.add(result.valid ? strFloat(result.topCosine) : "");
                row.add(result.valid ? familyName(result.secondFamily) : juce::String());
                row.add(result.valid ? strFloat(result.secondCosine) : "");
                row.add(result.valid ? strFloat(result.margin) : "");
                row.add(result.valid ? strFloat(bestMelodic) : "");
                row.add(result.valid ? strFloat(drum) : "");
                row.add(result.valid ? strFloat(drum - bestMelodic) : "");
                row.add(result.valid ? juce::String(shadowDrum ? 1 : 0) : "");
                row.add(result.valid && source.isNotEmpty()
                    ? juce::String(familyDiff ? 1 : 0) : "");
                row.add(result.valid ? juce::String(oldDrum != shadowDrum ? 1 : 0) : "");
                for (int i = 0; i < ClapTextBank9C::familyCount; ++i)
                    row.add(result.valid
                        ? strFloat(result.familyCosines[(size_t)i]) : "");
                jassert(row.size() == header.size());
                write(row);
            };

            for (const auto& rec : classified)
                writeSample(rec, "classified_before_final_gate");
            for (const auto& rec : earlyExcluded)
                writeSample(rec, "early_dsp_gate_excluded");
            // These files never reached audio analysis under the existing
            // production policy. Record their names, but do not pretend
            // that CLAP, loop structure or acoustic evidence was measured.
            for (const auto& file : metadataExcluded)
            {
                juce::StringArray row;
                row.add(quote(file.getRelativePathFrom(sourceFolder)));
                row.add("explicit_filename_gate_excluded");
                for (int i = 2; i < header.size(); ++i)
                    row.add(juce::String());
                jassert(row.size() == header.size());
                write(row);
            }
            out->flush();
            out.reset();
            if (!temp.overwriteTargetFileWithTemporary())
                juce::Logger::writeToLog(
                    "Melodic Atlas: could not save ClassifierShadow_9D.csv");
        }
    };
}
