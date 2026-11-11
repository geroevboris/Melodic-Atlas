#pragma once
#include <cmath>
#include <cstdint>
namespace Atlas {
struct Hierarchy05Policy {
    enum Reason : uint8_t { Abstain, ExplicitPiano, NamedSequence, StrongAudio, StableAudio, SourceConflict };
    struct Evidence {
        int source = -1;
        int globalSource = -1; // Drums = 7
        int audioTopParent = 0;
        float audioTopScore = -2.0f;
        float audioMargin = 0;
        float paraphraseAgreement = 0;
        bool namedPiano = false;
        bool namedArpeggio = false;
    };
    struct Decision { int parent = 0; Reason reason = Abstain; };
    static Decision choose(const Evidence& e) {
        if (e.source < 0 || e.source > 6 || !std::isfinite(e.audioTopScore)
            || !std::isfinite(e.audioMargin) || !std::isfinite(e.paraphraseAgreement))
            return {};
        if (e.source == 0 && e.namedPiano)
            return {1, ExplicitPiano};
        if (e.source == 1 && e.namedArpeggio)
            return {7, NamedSequence};
        if (e.globalSource != e.source && e.paraphraseAgreement < .75f)
            return {0, SourceConflict};
        if (e.audioTopParent == 0)
            return {};
        if (e.audioTopScore >= .50f && e.audioMargin >= .065f)
            return {e.audioTopParent, StrongAudio};
        if (e.audioTopScore >= .36f && e.audioMargin >= .045f
            && e.paraphraseAgreement >= .75f)
            return {e.audioTopParent, StableAudio};
        return {};
    }
};
}
