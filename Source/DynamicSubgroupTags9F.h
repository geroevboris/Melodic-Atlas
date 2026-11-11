#pragma once
#include "AtlasTypes.h"
#include "ClapTextPrototypeCandidate9C.h"
#include <algorithm>
#include <array>
#include <cmath>

// Subcategory suggestions from the CONTENT of a user's local library.
// Primary categories remain stable and manually/named subtypes outrank CLAP.
// Unknown/ambiguous sound sources do not acquire false definite labels.
namespace Atlas
{
    struct DynamicSubgroupTags9F
    {
        static uint32_t subtypeFlag(const juce::String& name,
                                     ClusterGroup group)
        {
            switch (group)
            {
                case ClusterGroup::Keys:
                    if (name == "Piano") return Sub_Piano;
                    if (name == "Rhodes") return Sub_ElectricPiano;
                    if (name == "Organ") return Sub_Organ;
                    if (name == "Mallet") return Sub_Mallet;
                    break;
                case ClusterGroup::Synth:
                    if (name == "SynthPad") return Sub_Pad;
                    if (name == "SynthLead") return Sub_Lead;
                    if (name == "SynthPluck") return Sub_Pluck;
                    if (name == "SynthArp") return Sub_Arp;
                    break;
                case ClusterGroup::Bass:
                    if (name == "SubBass") return Sub_SubBass;
                    if (name == "SynthBass") return Sub_SynthBass;
                    if (name == "ElectricBass") return Sub_ElectricBass;
                    if (name == "AcousticBass") return Sub_AcousticBass;
                    break;
                case ClusterGroup::Vocal:
                    if (name == "Singing" || name == "Rap")
                        return Sub_VocalSinging;
                    if (name == "Choir") return Sub_Choir;
                    if (name == "VocalChops") return Sub_VocalChops;
                    break;
                case ClusterGroup::Strings:
                    if (name == "Brass") return Sub_Brass;
                    if (name == "Woodwind") return Sub_Woodwind;
                    if (name == "Violin" || name == "Viola"
                        || name == "Cello" || name == "StringsSection")
                        return Sub_StringsSection;
                    break;
                case ClusterGroup::Guitar:
                    if (name == "ElectricGuitar") return Sub_ElectricGuitar;
                    if (name == "AcousticGuitar") return Sub_AcousticGuitar;
                    if (name == "ProcessedGuitar") return Sub_ProcessedGuitar;
                    break;
                case ClusterGroup::FX:
                    if (name == "Atmosphere") return Sub_Atmos;
                    if (name == "Riser") return Sub_RiserSweep;
                    if (name == "Impact") return Sub_ImpactHit;
                    break;
                default: break;
            }
            return Sub_None;
        }

        static void apply(SampleRecord& rec)
        {
            if (!rec.clapEmbeddingValid
                || rec.identityState != IdentityState::Identified
                || rec.identityCluster == ClusterGroup::Other
                || rec.percussionState == PercussionState::Percussive)
                return;

            // Do not overwrite an explicit instrument subtype found in
            // the WAV name or instrument folder. Only fill blanks.
            const auto group = rec.identityCluster;
            const auto options = MusicMath::getSubgroupsForCluster(group);
            uint32_t supportedMask = 0;
            for (const auto& option : options)
                supportedMask |= option.flagBit;
            if ((rec.subMask & supportedMask) != 0)
                return;

            const auto scores =
                ClapTextPrototypeCandidate9C::evaluate(rec.clapEmbedding);
            if (!scores.valid)
                return;

            std::array<float, 32> bestByBit {};
            bestByBit.fill(-2.0f);
            for (int p = 0; p < ClapTextBank9C::promptCount; ++p)
            {
                const auto& info = ClapTextBank9C::prompts[(size_t)p];
                const uint32_t bit =
                    subtypeFlag(juce::String(info.subtype), group);
                if (bit == 0 || (supportedMask & bit) == 0)
                    continue;
                for (int k = 0; k < 32; ++k)
                    if ((bit & (1u << k)) != 0)
                    {
                        bestByBit[(size_t)k] = std::max(
                            bestByBit[(size_t)k],
                            scores.promptCosines[(size_t)p]);
                        break;
                    }
            }

            float first = -2.0f, second = -2.0f;
            uint32_t bestBit = 0;
            for (int k = 0; k < 32; ++k)
            {
                const float value = bestByBit[(size_t)k];
                if (value > first)
                {
                    second = first;
                    first = value;
                    bestBit = 1u << k;
                }
                else if (value > second)
                    second = value;
            }

            // Cosine is not a probability. Avoid pushing an unknown timbre
            // into a narrow menu merely because something must be top-1.
            // Require positive similarity, intra-family separation and
            // no strong contrary family evidence. Counts make subfilters
            // appear dynamically only for groups present in the library.
            if (bestBit != 0 && first >= 0.34f
                && first - second >= 0.035f
                && first + 0.09f >= scores.topCosine)
                rec.subMask |= bestBit;
        }
    };
}
