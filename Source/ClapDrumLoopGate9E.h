#pragma once
#include "AtlasTypes.h"
#include "ClapTextPrototypeCandidate9C.h"
#include "DrumLoop9EPolicy.h"
#include <JuceHeader.h>
#include <algorithm>
#include <cstring>

namespace Atlas
{
    // This actually changes production exclusion by setting the existing
    // PercussionState. No new model, external runtime, disk movements,
    // changes to manual hidden/loop overrides or audio thread operations.
    struct ClapDrumLoopGate9E
    {
        static bool hasExplicitMelodicSourceName(const juce::File& file)
        {
            juce::StringArray words;
            words.addTokens(file.getFileNameWithoutExtension().toLowerCase(),
                            " _-.()[]{}0123456789", "");
            // Protective veto only: an explicitly labelled musical part
            // cannot be silently hidden by one CLAP text prompt.
            static constexpr const char* musical[] = {
                "melody", "melodic", "piano", "keys", "key", "rhodes",
                "organ", "guitar", "bass", "synth", "synthesizer",
                "strings", "string", "violin", "cello", "brass",
                "woodwind", "trumpet", "sax", "saxophone", "flute",
                "vocal", "vocals", "voice", "choir", "arp", "arpeggio",
                "orchestral", "pad", "lead"
            };
            for (const char* token : musical)
                if (words.contains(juce::String(token)))
                    return true;
            return false;
        }

        static bool apply(SampleRecord& rec)
        {
            if (!rec.clapEmbeddingValid || !rec.acoustic.valid
                || rec.percussionState == PercussionState::Percussive)
                return false;
            // Already explicitly named instruments have stronger source
            // evidence than one zero-shot text prompt. This guard must
            // not prevent clearly unlabelled drum loops being recognized.
            const bool trustedInstrument =
                rec.identityState == IdentityState::Identified
                && rec.identityOrigin == 0
                && rec.identityCluster != ClusterGroup::Other;

            const auto scores =
                ClapTextPrototypeCandidate9C::evaluate(rec.clapEmbedding);
            if (!scores.valid || scores.topPrompt < 0)
                return false;

            float strongestMelodic = -1.0f;
            for (int i = 0; i < ClapTextBank9C::familyCount - 1; ++i)
                strongestMelodic = std::max(
                    strongestMelodic, scores.familyCosines[(size_t)i]);

            const DrumLoop9EPolicy::Evidence evidence {
                true,
                std::strcmp(
                    ClapTextBank9C::prompts[(size_t)scores.topPrompt].subtype,
                    "DrumLoop") == 0,
                scores.topFamily == ClapTextBank9C::familyCount - 1,
                hasExplicitMelodicSourceName(rec.file),
                trustedInstrument,
                rec.durationSec,
                scores.familyCosines[
                    (size_t)(ClapTextBank9C::familyCount - 1)],
                strongestMelodic,
                rec.acoustic.onsetActivity,
                rec.tonal.validWindowCount
            };
            if (!DrumLoop9EPolicy::shouldExclude(evidence))
                return false;

            rec.percussionState = PercussionState::Percussive;
            // Strength of rule evidence, never a calibrated probability.
            rec.percussionConfidence =
                std::max(rec.percussionConfidence, 0.70f);
            return true;
        }
    };
}
