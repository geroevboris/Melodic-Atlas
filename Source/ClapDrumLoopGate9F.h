#pragma once
#include "AtlasTypes.h"
#include "ClapTextPrototypeCandidate9C.h"
#include "ClapDrumLoopGate9E.h"
#include "DrumLoop9FPolicy.h"
#include <algorithm>
#include <cstring>

// Background-only production gate for *repeated* drum/percussion sequences.
// It does NOT infer loops from drum timbre alone. It does NOT reject pitched
// musical rhythm; all 3 independent sources must agree.
namespace Atlas
{
    struct ClapDrumLoopGate9F
    {
        static bool apply(SampleRecord& rec)
        {
            if (!rec.clapEmbeddingValid || !rec.acoustic.valid
                || rec.percussionState == PercussionState::Percussive
                || !rec.rhythmicSequence || rec.rhythmicEventCount < 6)
                return false;
            const auto scores =
                ClapTextPrototypeCandidate9C::evaluate(rec.clapEmbedding);
            if (!scores.valid || scores.topPrompt < 0)
                return false;
            const char* subtype =
                ClapTextBank9C::prompts[(size_t)scores.topPrompt].subtype;
            const bool isDrumSequence =
                std::strcmp(subtype, "DrumLoop") == 0
                || std::strcmp(subtype, "Snare") == 0;

            float bestMelodic = -1.0f;
            for (int i = 0; i < ClapTextBank9C::familyCount - 1; ++i)
                bestMelodic = std::max(
                    bestMelodic, scores.familyCosines[(size_t)i]);
            const bool trusted = rec.identityState == IdentityState::Identified
                && rec.identityOrigin == 0
                && rec.identityCluster != ClusterGroup::Other;
            const DrumLoop9FPolicy::Evidence evidence {
                scores.topFamily == ClapTextBank9C::familyCount - 1,
                isDrumSequence,
                ClapDrumLoopGate9E::hasExplicitMelodicSourceName(rec.file),
                trusted,
                rec.rhythmicSequence,
                rec.rhythmicEventCount,
                rec.rhythmicSpanSeconds,
                rec.durationSec,
                scores.familyCosines[(size_t)(ClapTextBank9C::familyCount - 1)],
                bestMelodic,
                rec.acoustic.spectralFlatness,
                rec.acoustic.onsetActivity,
                rec.tonal.validWindowCount
            };
            if (!DrumLoop9FPolicy::shouldExclude(evidence))
                return false;
            rec.percussionState = PercussionState::Percussive;
            rec.percussionConfidence =
                std::max(rec.percussionConfidence, 0.71f);
            return true;
        }
    };
}
