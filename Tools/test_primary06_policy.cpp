#include "PrimaryFamilyPolicy06.h"
#include <cassert>
#include <iostream>
using P = Atlas::PrimaryFamilyPolicy06;
static P::Evidence pn() {
    P::Evidence e;
    e.familyCosines = { .301f, .127f, .050f, .217f, .249f,
                        .209f, .215f, .178f };
    // The four guitar/keys user WAVs are held outside seed examples.
    e.closestNamed = {{
        {0,.591f},{0,.578f},{1,.556f},{1,.528f},
        {0,.522f},{0,.494f},{0,.445f},{1,.434f}
    }};
    return e;
}
int main() {
    assert(P::evaluate(pn()).family==0); // PN_SUSTAINED -> Keys
    auto guitar=pn();
    guitar.familyCosines={.089f,.122f,.076f,.018f,.042f,
                          .407f,.005f,.040f};
    guitar.closestNamed = {{
        {5,.631f},{5,.610f},{5,.582f},{5,.512f},
        {5,.454f},{5,.422f},{5,.412f},{5,.411f}
    }};
    assert(P::evaluate(guitar).family==5); // GeeChords -> Guitar
    auto uncertain=pn();
    uncertain.familyCosines[0]=.295f;
    assert(P::evaluate(uncertain).family==-1);
    auto drum=pn();
    drum.familyCosines[7]=.490f;
    assert(P::evaluate(drum).family==-1);
    auto split=pn();
    split.closestNamed={{{1,.601f},{0,.591f},{0,.578f},{1,.556f},
                         {1,.550f},{0,.540f},{1,.530f},{1,.520f}}};
    assert(P::evaluate(split).family==-1);
    auto weak=pn();
    weak.closestNamed={{{0,.42f},{0,.41f},{0,.40f},{0,.39f},
                        {0,.38f},{0,.37f},{0,.36f},{0,.35f}}};
    assert(P::evaluate(weak).family==-1);
    // A strong text-only vote without trusted named neighbours
    // must NOT force a primary instrument family.
    weak.familyCosines={.9f,.01f,.01f,.01f,.01f,.01f,.01f,.01f};
    assert(P::evaluate(weak).family==-1);
    std::cout<<"Primary source recovery 06 policy PASS\n";
}
