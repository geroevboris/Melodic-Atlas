#pragma once
#include "AtlasTypes.h"
#include "ClapHierarchy05Bank.h"
#include "Hierarchy05Policy.h"
#include <JuceHeader.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <initializer_list>

// A conservative, hierarchical subtype scorer using the existing *audio*
// CLAP vector and an offline-generated static *text* bank. No new neural
// runtime. Called only when loading/scanning the library, never in audio.
namespace Atlas {
struct ClapHierarchy05 {
    struct Decision {
        bool valid = false;
        uint32_t parentBit = 0;
        int parentId = 0;
        int globalFamily = -1;
        float topScore = -2.0f;
        float margin = 0.0f;
        float paraphraseAgreement = 0.0f;
        Hierarchy05Policy::Reason reason = Hierarchy05Policy::Abstain;
    };

    // Primary-family text scores are independent of any existing
    // instrument identity. Only used by the recovery classifier on the
    // scanner worker; never force a new label from top-1 alone.
    static bool sourceScores(
        const std::array<float,ClapHierarchy05Data::dimensions>& audio,
        std::array<float,8>& scores)
    {
        const auto& bank=getBank();
        if(!bank.valid)return false;
        double normSquared=0;
        for(float v:audio) {
            if(!std::isfinite(v))return false;
            normSquared+=(double)v*v;
        }
        if(!(normSquared>1.0e-16))return false;
        const float inv=(float)(1.0/std::sqrt(normSquared));
        scores.fill(-2.0f);
        for(int p=0;p<ClapHierarchy05Data::promptCount;++p) {
            const int group=ClapHierarchy05Data::prompts[(size_t)p].source;
            if(group<0||group>=8)continue;
            double dot=0;
            const auto& proto=bank.unit[(size_t)p];
            for(int d=0;d<ClapHierarchy05Data::dimensions;++d)
                dot+=(double)audio[(size_t)d]*proto[(size_t)d];
            scores[(size_t)group]=std::max(scores[(size_t)group],
                (float)dot*inv);
        }
        return true;
    }

    static uint32_t flagForParent(int parent) {
        switch(parent) {
            case 1: return Sub_Piano;
            case 2: return Sub_Organ;
            case 3: return Sub_Mallet;
            case 4: return Sub_Pad;
            case 5: return Sub_Lead;
            case 6: return Sub_Pluck;
            case 7: return Sub_Arp;
            case 8: return Sub_Textural;
            case 9: return Sub_SynthBass;
            case 10: return Sub_SubBass;
            case 11: return Sub_ElectricBass;
            case 12: return Sub_AcousticBass;
            case 13: return Sub_VocalSinging;
            case 14: return Sub_VocalChops;
            case 15: return Sub_Choir;
            case 16: return Sub_StringsSection;
            case 17: return Sub_Brass;
            case 18: return Sub_Woodwind;
            case 19: return Sub_ElectricGuitar;
            case 20: return Sub_AcousticGuitar;
            case 21: return Sub_Atmos;
            case 22: return Sub_RiserSweep;
            case 23: return Sub_ImpactHit;
            case 24: return Sub_BellMetallic;
            default: return 0;
        }
    }

    static Decision evaluate(const SampleRecord& rec) {
        Decision out;
        if (!rec.clapEmbeddingValid || rec.identityState != IdentityState::Identified
            || (int)rec.identityCluster < 0 || (int)rec.identityCluster > 6)
            return out;
        const auto& bank = getBank();
        if (!bank.valid) return out;

        double audioSquared = 0;
        for(float v: rec.clapEmbedding) {
            if(!std::isfinite(v)) return out;
            audioSquared += (double)v*v;
        }
        if(audioSquared < 1e-15) return out;
        const float audioInvNorm = (float)(1.0/std::sqrt(audioSquared));
        std::array<float,ClapHierarchy05Data::promptCount> similarities{};
        std::array<float,8> familyMax{};
        std::array<float,ClapHierarchy05Data::parentCount> parentMax{};
        familyMax.fill(-2.0f);
        parentMax.fill(-2.0f);
        const int currentFamily = (int)rec.identityCluster;
        for(int p=0;p<ClapHierarchy05Data::promptCount;++p) {
            const auto& proto=bank.unit[(size_t)p];
            double dot=0;
            for(int d=0;d<ClapHierarchy05Data::dimensions;++d)
                dot += (double)rec.clapEmbedding[(size_t)d]*proto[(size_t)d];
            const float sim=(float)dot*audioInvNorm;
            similarities[(size_t)p]=sim;
            const auto& info=ClapHierarchy05Data::prompts[(size_t)p];
            if(info.source < 8)
                familyMax[(size_t)info.source]=std::max(familyMax[(size_t)info.source],sim);
            if(info.source==currentFamily && info.parent>0)
                parentMax[(size_t)info.parent]=std::max(parentMax[(size_t)info.parent],sim);
        }
        int globalFamily=0;
        for(int f=1;f<8;++f)
            if(familyMax[(size_t)f]>familyMax[(size_t)globalFamily])
                globalFamily=f;
        out.globalFamily=globalFamily;

        int first=0;
        float best=-2.0f,second=-2.0f;
        for(int parent=1;parent<ClapHierarchy05Data::parentCount;++parent) {
            if(ClapHierarchy05Data::parentSource[(size_t)parent]!=currentFamily)
                continue;
            const float s=parentMax[(size_t)parent];
            if(s>best) {second=best;best=s;first=parent;}
            else if(s>second) second=s;
        }
        if(first==0) return out;
        out.topScore=best;
        out.margin=best-second;
        out.valid=true;

        // Paraphrase robustness: choose one wording per audio sub-type,
        // deterministically over 64 precomputed trials. This prevents a
        // long list of textual synonyms winning solely by maximum search.
        int agreement=0;
        for(int trial=0;trial<64;++trial) {
            std::array<float,ClapHierarchy05Data::parentCount> sampled{};
            sampled.fill(-2.0f);
            for(int sub=0;sub<ClapHierarchy05Data::subtypeCount;++sub) {
                const int p=bank.sampled[(size_t)trial][(size_t)sub];
                if(p<0)continue;
                const auto& info=ClapHierarchy05Data::prompts[(size_t)p];
                if(info.source==currentFamily && info.parent>0)
                    sampled[(size_t)info.parent]=std::max(
                        sampled[(size_t)info.parent],similarities[(size_t)p]);
            }
            float maxValue=-2.0f; int maxParent=0;
            for(int parent=1;parent<ClapHierarchy05Data::parentCount;++parent)
                if(ClapHierarchy05Data::parentSource[(size_t)parent]==currentFamily
                    && sampled[(size_t)parent]>maxValue) {
                    maxValue=sampled[(size_t)parent];maxParent=parent;
                }
            agreement += maxParent==first ? 1:0;
        }
        out.paraphraseAgreement=agreement/64.0f;
        juce::StringArray tokens;
        tokens.addTokens(rec.file.getFileNameWithoutExtension().toLowerCase(),
                         " _-.()[]{}0123456789", "");
        const auto hasToken = [&tokens](std::initializer_list<const char*> words) {
            for(const char* word:words)
                if(tokens.contains(juce::String(word)))return true;
            return false;
        };
        const bool pianoCue=hasToken({"piano","pianochords","rhodes","wurli"});
        const bool arpCue=hasToken({"arp","arps","arpeggio","arpeggiator"});
        const auto policy=Hierarchy05Policy::choose({
            currentFamily,globalFamily,first,best,out.margin,
            out.paraphraseAgreement,pianoCue,arpCue});
        out.parentId=policy.parent;
        out.parentBit=flagForParent(policy.parent);
        out.reason=policy.reason;
        return out;
    }

private:
    struct Vectors {
        bool valid = false;
        std::array<std::array<float,ClapHierarchy05Data::dimensions>,
                   ClapHierarchy05Data::promptCount> unit{};
        std::array<std::array<int16_t,ClapHierarchy05Data::subtypeCount>,64> sampled{};
    };
    static uint32_t hash32(uint32_t x) {
        x=(x^(x>>16))*0x7feb352du;
        x=(x^(x>>15))*0x846ca68bu;
        return x^(x>>16);
    }
    static Vectors loadBank() {
        Vectors out;
        juce::MemoryOutputStream stream;
        if(!juce::Base64::convertFromBase64(
             stream,juce::StringRef(ClapHierarchy05Data::encodedQ15)))
            return out;
        if(stream.getDataSize() != (size_t)ClapHierarchy05Data::promptCount
                  * (size_t)ClapHierarchy05Data::dimensions * 2)
            return out;
        const uint8_t* bytes=(const uint8_t*)stream.getData();
        for(int p=0;p<ClapHierarchy05Data::promptCount;++p) {
            const float scale=ClapHierarchy05Data::prompts[(size_t)p].scale;
            if(!std::isfinite(scale) || scale<=0) return Vectors{};
            double normSq=0;
            for(int d=0;d<ClapHierarchy05Data::dimensions;++d) {
                size_t i=((size_t)p*ClapHierarchy05Data::dimensions+(size_t)d)*2;
                unsigned v=(unsigned)bytes[i]|((unsigned)bytes[i+1]<<8);
                float x=(float)(v>=32768u?(int)v-65536:(int)v)*scale;
                out.unit[(size_t)p][(size_t)d]=x;
                normSq+=(double)x*x;
            }
            if(normSq<1e-16 || !std::isfinite(normSq))return Vectors{};
            float inv=(float)(1.0/std::sqrt(normSq));
            for(float& x:out.unit[(size_t)p]) x*=inv;
        }
        // Precompute 64 paraphrase selections ONCE; deterministic and
        // insensitive to files scanned or iteration order.
        std::array<int,ClapHierarchy05Data::subtypeCount> counts{};
        for(const auto& info:ClapHierarchy05Data::prompts)
            if(info.subtype<ClapHierarchy05Data::subtypeCount)
                ++counts[(size_t)info.subtype];
            else return Vectors{};
        for(int t=0;t<64;++t)
            for(int s=0;s<ClapHierarchy05Data::subtypeCount;++s) {
                if(counts[(size_t)s]<1)return Vectors{};
                int nth=(int)(hash32((uint32_t)t*0x9e3779b9u
                    +(uint32_t)s*0x85ebca6bu)%(uint32_t)counts[(size_t)s]);
                int selected=-1;
                for(int p=0;p<ClapHierarchy05Data::promptCount;++p)
                    if(ClapHierarchy05Data::prompts[(size_t)p].subtype==s
                        && nth--==0){selected=p;break;}
                if(selected<0)return Vectors{};
                out.sampled[(size_t)t][(size_t)s]=(int16_t)selected;
            }
        out.valid=true;
        return out;
    }
    static const Vectors& getBank() {
        static const Vectors bank=loadBank();
        return bank;
    }
};
}
