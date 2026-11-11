#pragma once
#include <array>
#include <cmath>
#include <algorithm>

// Plain C++ policy used by the full JUCE classifier and standalone tests.
// No unknown/unassigned family is emitted for a playable atlas sample.
namespace Atlas {
struct DynamicTaxonomy07Policy {
    static int chooseFamily(const std::array<double,7>& rank, int fallback) {
        int best=-1;
        for(int i=0;i<7;++i)
            if(std::isfinite(rank[(size_t)i]) &&
               (best<0 || rank[(size_t)i]>rank[(size_t)best]))
                best=i;
        if(best>=0)return best;
        return fallback>=0 && fallback<7 ? fallback : 1; // Synth
    }
    static bool showMainFamily(int count) { return count>0; }
    static bool useFineSubtype(float cosine, float margin, int localCount,
                               bool explicitlyNamed) {
        return explicitlyNamed ||
            (std::isfinite(cosine) && std::isfinite(margin)
             && cosine>=0.38f && margin>=0.04f && localCount>=2);
    }
};
}
