#include "DynamicTaxonomy07Policy.h"
#include <array>
#include <cassert>
#include <iostream>
#include <limits>
using P=Atlas::DynamicTaxonomy07Policy;
int main() {
    std::array<double,7> scores{.301,.127,.05,.217,.249,.209,.215};
    assert(P::chooseFamily(scores,1)==0); // PN_SUSTAINED -> Keys
    scores={.089,.122,.076,.018,.042,.407,.005};
    assert(P::chooseFamily(scores,1)==5); // GeeChords -> Guitar
    scores={.2,.3,.29,.1,.11,.15,.499};
    assert(P::chooseFamily(scores,1)==6); // FX impact selected by metadata
    scores.fill(std::numeric_limits<double>::quiet_NaN());
    assert(P::chooseFamily(scores,6)==6);
    assert(P::chooseFamily(scores,-1)==1);
    assert(!P::showMainFamily(0));
    assert(P::showMainFamily(1));
    assert(!P::useFineSubtype(.57f,.02f,30,false));
    assert(!P::useFineSubtype(.57f,.10f,1,false));
    assert(P::useFineSubtype(.57f,.10f,30,false));
    assert(P::useFineSubtype(.20f,.00f,1,true));
    std::cout<<"Taxonomy07 main/subgroup policy PASS\n";
}
