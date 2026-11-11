#pragma once
#include <array>
#include <algorithm>
#include <cmath>

// Deliberately conservative recovery from Unknown into a MAIN instrument
// family. Input similarities are uncalibrated cosines, not probabilities.
// An uncertain source remains Unknown. Never alter a confirmed label.
namespace Atlas {
struct PrimaryFamilyPolicy06 {
    static constexpr int familyCount=8; // 0..6 melodic, 7 drums
    struct Neighbor { int family=-1; float cosine=-1.0f; };
    struct Evidence {
        std::array<float,familyCount> familyCosines {};
        std::array<Neighbor,8> closestNamed {};
    };
    struct Decision { int family=-1; float textMargin=0.0f; int agreeingNeighbors=0; };

    static Decision evaluate(const Evidence& e) {
        int best=0,second=1;
        if(e.familyCosines[second]>e.familyCosines[best])
            std::swap(best,second);
        for(int family=2;family<7;++family) {
            if(e.familyCosines[(size_t)family]>e.familyCosines[(size_t)best]) {
                second=best;
                best=family;
            } else if(e.familyCosines[(size_t)family]>e.familyCosines[(size_t)second])
                second=family;
        }
        const float score=e.familyCosines[(size_t)best];
        const float runner=e.familyCosines[(size_t)second];
        if(!std::isfinite(score)||!std::isfinite(runner)
            ||!std::isfinite(e.familyCosines[7])
            ||score<0.30f || score-runner<0.050f
            ||e.familyCosines[7]>score+0.09f)
            return {};
        int support=0,opposed=0;
        float strongestSupport=-1.0f,strongestOpposed=-1.0f;
        for(const auto& n:e.closestNamed) {
            if(!std::isfinite(n.cosine)||n.cosine<0.43f) continue;
            if(n.family==best) {
                ++support;strongestSupport=std::max(strongestSupport,n.cosine);
            } else if(n.family>=0&&n.family<7) {
                ++opposed;strongestOpposed=std::max(strongestOpposed,n.cosine);
            }
        }
        if(support<3||support<=opposed
            ||strongestOpposed>strongestSupport)
            return {};
        return {best,score-runner,support};
    }
};
}
