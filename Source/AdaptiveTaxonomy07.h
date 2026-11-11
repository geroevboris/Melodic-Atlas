#pragma once
#include "AtlasTypes.h"
#include "ClapHierarchy05.h"
#include "DynamicTaxonomy07Policy.h"
#include <JuceHeader.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <string>
#include <vector>

// Dynamic taxonomy 07: no "Unknown" display bucket for eligible samples.
// A main source is always assigned; subcategory suggestions are created
// from the actual audio contents of THIS library, not static UI options.
// Existing drum exclusion, map coordinates and user WAVs stay untouched.
namespace Atlas
{
struct AdaptiveTaxonomy07
{
    static constexpr int familyCount = 7;
    static constexpr int typeCount = 65;
    static constexpr int taxonomyVersion = 7;

    static const char* categoryName(int family)
    {
        static const char* names[] =
            {"Keys", "Synth", "Bass", "Vocal", "Strings & Winds", "Guitar", "FX"};
        return family>=0 && family<familyCount ? names[family] : "Synth";
    }

    static const char* fallbackName(int family)
    {
        static const char* names[] = {
            "Keyboard Sounds", "Synth Textures", "Bass Sounds",
            "Vocal Sounds", "Strings / Winds", "Guitar Sounds", "Designed FX"
        };
        return names[(size_t)juce::jlimit(0,6,family)];
    }

    static const char* broadName(int parent, int family)
    {
        static const char* names[] = {
            "", "Piano / E.Piano", "Organs", "Bells / Mallets",
            "Synth Pads / Drones", "Synth Leads", "Synth Plucks",
            "Synth Arpeggios", "Synth Textures",
            "Synth Bass", "Sub Bass", "Electric Bass", "Acoustic Bass",
            "Voice / Singing", "Vocal Chops", "Choir",
            "Strings", "Brass", "Woodwinds",
            "Electric / Processed Guitar", "Acoustic Guitar",
            "Atmospheres", "Risers / Sweeps", "Impacts & Hits",
            "Metallic / Foley"
        };
        return parent>=1 && parent<=24
            ? names[parent] : fallbackName(family);
    }

    static const char* fineName(int type)
    {
        // 65 learned text subtypes. Order is stable across all 145
        // reference prompts. Only types actually found appear in menus.
        static const char* names[typeCount] = {
            "Piano", "Rhodes", "Organ", "Mallets",
            "Synth Leads", "Synth Pads", "Synth Plucks", "Synth Arpeggios",
            "Sub Bass", "Synth Bass", "Electric Bass", "Acoustic Bass",
            "Singing", "Rap", "Choir", "Vocal Chops",
            "Violin", "Viola", "Cello", "String Ensembles",
            "Brass", "Woodwinds", "Electric Guitar", "Acoustic Guitar",
            "Processed Guitar", "Atmospheres", "Risers", "Impacts", "Foley",
            "Kick", "Snare", "Cymbals", "Percussion", "Drum Loops",
            "Glockenspiel", "Celesta", "Kalimba", "Marimba",
            "Vibraphone", "Music Box", "Synth Drones", "Synth Textures",
            "Synth Sequences", "Dark Pads", "Bright Pads",
            "Organic Synth Plucks", "Reese Bass", "Neurobass",
            "Sine Sub Bass", "808 Bass", "Wobble Bass",
            "Guitar Chords", "Resampled Guitar", "Guitar Plucks",
            "Metallic Textures", "Metallic Hits", "Wind Chimes",
            "Vinyl Scratch", "Chain / Metal Foley",
            "Pizzicato Strings", "Harp", "Saxophone", "Flute",
            "Trumpet", "Textural Vocal Chops"
        };
        return type>=0 && type<typeCount ? names[type] : "";
    }

    static int familyOfType(int subtype)
    {
        if (subtype < 0 || subtype >= typeCount) return -1;
        for (const auto& prompt: ClapHierarchy05Data::prompts)
            if (prompt.subtype == subtype)
                return prompt.source < familyCount ? prompt.source : -1;
        return -1;
    }

    static int parentOfType(int subtype)
    {
        if(subtype<0 || subtype>=typeCount)return 0;
        for(const auto& p:ClapHierarchy05Data::prompts)
            if(p.subtype==subtype)return p.parent;
        return 0;
    }

    static bool filenameImpact(const juce::File& f)
    {
        juce::StringArray parts;
        parts.addTokens(f.getFileNameWithoutExtension().toLowerCase(),
                        " _-.()[]{}0123456789", "");
        return parts.contains("impact") || parts.contains("impacts");
    }

    static bool nameContains(const juce::File& f, const char* term)
    {
        juce::StringArray parts;
        parts.addTokens(f.getFileNameWithoutExtension().toLowerCase(),
                        " _-.()[]{}0123456789", "");
        return parts.contains(juce::String(term));
    }

    static void publishManual(SampleRecord& rec)
    {
        int family = juce::jlimit(0,6,rec.automaticCategory);
        rec.identityCluster = (ClusterGroup)family;
        rec.cluster = (ClusterGroup)family;
        rec.dynamicSubgroup = rec.automaticSubgroup.isNotEmpty()
            ? rec.automaticSubgroup
            : juce::String(fallbackName(family));
        if (rec.manualCategory>=0 && rec.manualCategory<familyCount)
        {
            family = rec.manualCategory;
            rec.identityCluster = (ClusterGroup)family;
            rec.cluster = rec.identityCluster;
            rec.dynamicSubgroup = rec.manualSubgroup.isNotEmpty()
                ? rec.manualSubgroup
                : juce::String(fallbackName(family));
        }
        rec.identityState = IdentityState::Identified;
    }

    struct NamedSeed { int family; const SampleRecord* rec; float norm; };
    static int classify(std::vector<SampleRecord>& records)
    {
        std::array<int, familyCount> seedCounts {};
        std::vector<NamedSeed> seeds;
        seeds.reserve(448);
        for(const auto& rec:records)
        {
            if(!rec.clapEmbeddingValid || rec.manualCategory>=0
                || rec.identityState!=IdentityState::Identified
                || rec.identityOrigin!=0
                || rec.percussionState==PercussionState::Percussive)
                continue;
            const int category=(int)rec.identityCluster;
            if(category<0 || category>=familyCount
                || seedCounts[(size_t)category]>=64)continue;
            double norm=0;
            for(float v:rec.clapEmbedding)
                if(std::isfinite(v))norm+=(double)v*v;
            if(!(norm>1e-14))continue;
            seeds.push_back({category,&rec,(float)std::sqrt(norm)});
            ++seedCounts[(size_t)category];
        }

        struct TypeChoice { int subtype=-1; int parent=0; float score=-2; float margin=0; };
        std::vector<TypeChoice> proposals(records.size());
        std::map<std::pair<int,int>,int> typeCounts;
        int assigned=0;

        for(size_t ri=0;ri<records.size();++ri)
        {
            auto& rec=records[ri];
            if(rec.percussionState==PercussionState::Percussive)continue;
            const int oldCategory=(int)rec.identityCluster;
            int family=-1;
            // Preserve explicit trusted source labels, not weak inferences.
            if(rec.identityState==IdentityState::Identified
               && rec.identityOrigin==0
               && oldCategory>=0 && oldCategory<familyCount)
                family=oldCategory;

            std::array<float,8> textScores {};
            bool hasText=rec.clapEmbeddingValid
                && ClapHierarchy05::sourceScores(rec.clapEmbedding,textScores);
            if(family<0 && hasText)
            {
                std::array<double,familyCount> rank {};
                for(int g=0;g<familyCount;++g)
                    rank[(size_t)g]=textScores[(size_t)g];
                // Content-neighbour evidence influences but never replaces
                // the neural acoustic score entirely.
                double norm=0;
                for(float v:rec.clapEmbedding)
                    if(std::isfinite(v)) norm+=(double)v*v;
                if(norm>1e-15)
                {
                    std::array<int,familyCount> votes {};
                    std::array<float,familyCount> strongest {};
                    const float inv=1.0f/(float)std::sqrt(norm);
                    for(const auto& seed:seeds)
                    {
                        double dot=0;
                        for(size_t i=0;i<rec.clapEmbedding.size();++i)
                            dot+=(double)rec.clapEmbedding[i]
                                * seed.rec->clapEmbedding[i];
                        const float similarity=(float)dot*inv/seed.norm;
                        if(similarity>=0.55f)
                        {
                            ++votes[(size_t)seed.family];
                            strongest[(size_t)seed.family]=std::max(
                                strongest[(size_t)seed.family],similarity);
                        }
                    }
                    for(int g=0;g<familyCount;++g)
                        rank[(size_t)g]+=std::min(0.11,
                            0.016*(double)votes[(size_t)g])
                            +0.07*std::max(0.0f,
                                strongest[(size_t)g]-0.55f);
                }
                // A previous low-confidence metadata label is evidence,
                // but cannot override a decisive audio source.
                if(oldCategory>=0 && oldCategory<familyCount
                   && rec.identityState==IdentityState::Ambiguous)
                    rank[(size_t)oldCategory]+=0.055;
                family=DynamicTaxonomy07Policy::chooseFamily(rank,1);
            }
            if(family<0)
            {
                if(oldCategory>=0 && oldCategory<familyCount
                   && rec.identityState==IdentityState::Ambiguous)
                    family=oldCategory;
                else if(rec.acoustic.valid
                    && rec.acoustic.lowBandRatio>=0.55f)
                    family=(int)ClusterGroup::Bass;
                else if(rec.acoustic.valid
                    && rec.acoustic.spectralFlatness>=0.27f)
                    family=(int)ClusterGroup::FX;
                else
                    family=(int)ClusterGroup::Synth;
            }
            if(filenameImpact(rec.file))
                family=(int)ClusterGroup::FX;

            rec.automaticCategory=family;
            rec.identityState=IdentityState::Identified;
            rec.identityCluster=(ClusterGroup)family;
            rec.cluster=rec.identityCluster;
            if(rec.identityOrigin!=0)rec.identityOrigin=7;
            rec.identityConfidence=juce::jmax(0.20f,rec.identityConfidence);
            ++assigned;

            TypeChoice guess;
            std::array<float,ClapHierarchy05Data::subtypeCount> subScores{};
            if(rec.clapEmbeddingValid
               && ClapHierarchy05::subtypeScores(rec.clapEmbedding,subScores))
            {
                float runner=-2.0f;
                for(int t=0;t<typeCount;++t)
                {
                    if(familyOfType(t)!=family)continue;
                    const float value=subScores[(size_t)t];
                    if(value>guess.score)
                    {
                        runner=guess.score;guess.score=value;guess.subtype=t;
                    }
                    else if(value>runner) runner=value;
                }
                guess.margin=guess.score-runner;
                guess.parent=parentOfType(guess.subtype);
            }
            // Explicit identity-specific context outranks acoustically
            // similar but distinct musical sources, such as piano/Celesta
            // and a cinematic impact/Kick.
            if(family==0 && (nameContains(rec.file,"piano")
                || nameContains(rec.file,"pianochords")
                || nameContains(rec.file,"pn")))
                guess={0,1,1.0f,1.0f};
            else if(family==6 && filenameImpact(rec.file))
                guess={27,23,1.0f,1.0f};
            else if(family==1 && (nameContains(rec.file,"arp")
                || nameContains(rec.file,"arpeggio")))
                guess={7,7,1.0f,1.0f};

            proposals[ri]=guess;
            if(guess.subtype>=0)
                ++typeCounts[{family,guess.subtype}];
        }

        for(size_t ri=0;ri<records.size();++ri)
        {
            auto& rec=records[ri];
            if(rec.percussionState==PercussionState::Percussive)continue;
            const auto& g=proposals[ri];
            const int family=juce::jlimit(0,6,rec.automaticCategory);
            // Display a detailed type only when this library actually
            // contains enough such sounds, or filename gives direct
            // unambiguous instrument evidence.
            const int population=g.subtype>=0
                ?typeCounts[{family,g.subtype}]:0;
            const bool explicitName=g.score>=0.99f;
            if(g.subtype>=0
                && DynamicTaxonomy07Policy::useFineSubtype(
                    g.score,g.margin,population,explicitName))
                rec.automaticSubgroup=fineName(g.subtype);
            else if(g.parent>0)
                rec.automaticSubgroup=broadName(g.parent,family);
            else
                rec.automaticSubgroup=fallbackName(family);
            publishManual(rec);
        }
        return assigned;
    }
};
}
