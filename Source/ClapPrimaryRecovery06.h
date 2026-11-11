#pragma once
#include "AtlasTypes.h"
#include "ClapHierarchy05.h"
#include "PrimaryFamilyPolicy06.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

// Scanner-worker-only: recover clear Unknown instruments from existing
// native audio CLAP + official text prototypes + named library neighbours.
// Never change existing Identified family, manual hidden/loop settings,
// percussion exclusions, source WAVs, or audio callback.
namespace Atlas {
struct ClapPrimaryRecovery06 {
    static constexpr size_t maxNamedSeedsPerFamily=128;
    using Vector = std::array<float,512>;

    struct Seed {
        int family=-1;
        Vector unit{};
    };

    static int apply(std::vector<SampleRecord>& records) {
        std::array<size_t,7> perFamily{};
        std::vector<Seed> seeds;
        seeds.reserve(7*maxNamedSeedsPerFamily);
        for(const auto& rec:records) {
            if(!rec.clapEmbeddingValid
               ||rec.identityState!=IdentityState::Identified
               ||rec.identityOrigin!=0
               ||rec.percussionState==PercussionState::Percussive)
                continue;
            const int group=(int)rec.identityCluster;
            if(group<0||group>=7
               ||perFamily[(size_t)group]>=maxNamedSeedsPerFamily)
                continue;
            double normSq=0;
            for(float v:rec.clapEmbedding) {
                if(!std::isfinite(v)){normSq=0;break;}
                normSq+=(double)v*v;
            }
            if(normSq<1.0e-16)continue;
            Seed seed;seed.family=group;
            const float inv=(float)(1.0/std::sqrt(normSq));
            for(size_t i=0;i<seed.unit.size();++i)
                seed.unit[i]=rec.clapEmbedding[i]*inv;
            seeds.push_back(std::move(seed));
            ++perFamily[(size_t)group];
        }
        if(seeds.size()<3)
            return 0;

        int recovered=0;
        for(auto& rec:records) {
            if(!rec.clapEmbeddingValid
                ||rec.identityState==IdentityState::Identified
                ||rec.percussionState==PercussionState::Percussive
                ||rec.durationSec<2.0)
                continue;

            PrimaryFamilyPolicy06::Evidence evidence;
            if(!ClapHierarchy05::sourceScores(rec.clapEmbedding,
                                               evidence.familyCosines))
                continue;
            double normSq=0;
            for(float v:rec.clapEmbedding){
                if(!std::isfinite(v)){normSq=0;break;}
                normSq+=(double)v*v;
            }
            if(normSq<1.0e-16)continue;
            const float inv=(float)(1.0/std::sqrt(normSq));
            // The top eight NAMED neighbours, not inferred ones, vote.
            // Keep a bounded 128 seeds/family for large libraries.
            for(const auto& seed:seeds) {
                double dot=0;
                for(size_t i=0;i<rec.clapEmbedding.size();++i)
                    dot+=(double)rec.clapEmbedding[i]*seed.unit[i];
                const float cosine=(float)dot*inv;
                for(size_t k=0;k<evidence.closestNamed.size();++k)
                    if(cosine>evidence.closestNamed[k].cosine) {
                        for(size_t m=evidence.closestNamed.size()-1;m>k;--m)
                            evidence.closestNamed[m]=evidence.closestNamed[m-1];
                        evidence.closestNamed[k]={seed.family,cosine};
                        break;
                    }
            }

            const auto decision=PrimaryFamilyPolicy06::evaluate(evidence);
            if(decision.family<0)continue;
            rec.identityState=IdentityState::Identified;
            rec.identityCluster=(ClusterGroup)decision.family;
            rec.cluster=rec.identityCluster;
            rec.identityOrigin=2; // 06 audio/text+named-neighbour recovery
            // A heuristic confidence flag, NOT probability of being correct.
            rec.identityConfidence=0.66f;
            ++recovered;
        }
        return recovered;
    }
};
}
