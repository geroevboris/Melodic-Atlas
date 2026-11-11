#include "Hierarchy05Policy.h"
#include <cassert>
#include <iostream>
int main() {
    using P=Atlas::Hierarchy05Policy;
    P::Evidence e; e.source=0;e.globalSource=0;e.audioTopParent=3;
    e.audioTopScore=.63f;e.audioMargin=.07f;e.namedPiano=true;
    assert(P::choose(e).parent==1);
    e={};e.source=1;e.globalSource=1;e.namedArpeggio=true;
    e.audioTopParent=6;e.audioTopScore=.50f; assert(P::choose(e).parent==7);
    e={};e.source=1;e.globalSource=3;e.audioTopParent=6;
    e.audioTopScore=.52f;e.audioMargin=.10f;e.paraphraseAgreement=.20f;
    assert(P::choose(e).reason==P::SourceConflict);
    e={};e.source=2;e.globalSource=2;e.audioTopParent=9;
    e.audioTopScore=.533f;e.audioMargin=.07f;e.paraphraseAgreement=.30f;
    assert(P::choose(e).parent==9);
    e.audioTopScore=.405f;e.audioMargin=.048f;
    assert(P::choose(e).parent==0);
    e.audioTopScore=.40f;e.audioMargin=.05f;e.paraphraseAgreement=.9f;
    assert(P::choose(e).parent==9);
    e.audioTopScore=.582f;e.audioMargin=.02f;
    assert(P::choose(e).parent==0);
    std::cout<<"Hierarchy05 policy PASS\n";
}
