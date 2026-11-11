#pragma once
#include "AtlasTypes.h"
#include "ClapHierarchy05.h"
#include <JuceHeader.h>

// Conservative content-driven subcategory selection. The high-level source
// is immutable; uncertain child labels abstain. No audio thread processing.
namespace Atlas {
struct DynamicSubgroupTags9F {
    static void apply(SampleRecord& rec) {
        if (!rec.clapEmbeddingValid
            || rec.identityState != IdentityState::Identified
            || rec.identityCluster == ClusterGroup::Other
            || rec.percussionState == PercussionState::Percussive)
            return;

        const auto decision=ClapHierarchy05::evaluate(rec);
        if (!decision.valid) return; // preserve old tags if bank unavailable
        const auto options=MusicMath::getSubgroupsForCluster(rec.identityCluster);
        uint32_t subtypeMask=0;
        for(const auto& option:options)
            subtypeMask|=option.flagBit;
        const uint32_t previous=rec.subMask & subtypeMask;

        // An explicitly named cinematic Impact is an FX subtype even
        // when its low-frequency transient resembles a kick in CLAP.
        // Preserve this verified file-level evidence; it never sets
        // PercussionState and cannot affect unrelated instruments.
        if(rec.identityCluster==ClusterGroup::FX
            && (previous & Sub_ImpactHit)!=0) {
            rec.subMask &= ~subtypeMask;
            rec.subMask |= Sub_ImpactHit;
            return;
        }

        // Do not keep forced filename-only narrow tags under disagreement.
        // The candidate is broad. Existing verified Rhodes/processed guitar
        // hints may remain as optional *narrow* tags alongside the new parent.
        rec.subMask &= ~subtypeMask;
        if(decision.parentBit==0) return;
        rec.subMask |= decision.parentBit;
        if(decision.parentId==1 && (previous & Sub_ElectricPiano))
            rec.subMask |= Sub_ElectricPiano;
        if(decision.parentId==19 && (previous & Sub_ProcessedGuitar))
            rec.subMask |= Sub_ProcessedGuitar;
    }
};
}
