#include "ColourRegionBlend12.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

using Blend=Atlas::ColourRegionBlend12;
int main()
{
    std::vector<Blend::Point> input;
    std::vector<int> region;
    // Deliberately separated whole spectral colours. Each already contains
    // its own finished organic cloud, which must translate rigidly.
    for (int group=0;group<4;++group)
        for (int i=0;i<96;++i)
        {
            const float x=0.15f+0.225f*(float)group
                +0.025f*((float)(i%19)-9.0f)/9.0f;
            const float y=0.21f+0.005f*(float)(i%23);
            input.push_back({x,y});
            region.push_back(group);
        }
    // A second, independent atlas with two colours and one unassigned point.
    for (int group=4;group<6;++group)
        for (int i=0;i<60;++i)
        {
            input.push_back({0.20f+0.35f*(float)(group-4)
                +0.01f*(float)(i%3),0.60f+0.003f*(float)i});
            region.push_back(group);
        }
    input.push_back({0.93f,0.91f});region.push_back(-1);
    const auto output=Blend::arrange(input,region);
    const auto repeated=Blend::arrange(input,region);
    assert(output.size()==input.size());
    for(size_t i=0;i<input.size();++i)
    {
        assert(output[i].x==repeated[i].x);
        assert(output[i].y==input[i].y);
        assert(output[i].x>=0.0f && output[i].x<=1.0f);
        if(region[i]<0)
        {
            assert(output[i].x==input[i].x);
            continue;
        }
        const int first=region[i]<4 ? region[i]*96 : 384+(region[i]-4)*60;
        const float shift=output[i].x-input[i].x;
        const float reference=output[(size_t)first].x-input[(size_t)first].x;
        assert(std::abs(shift-reference)<0.000002f);
    }
    // All neighbouring macro-regions come closer; their original organic
    // internal shapes and source ordering remain invariant.
    for(int group=0;group<3;++group)
    {
        const float before=input[(size_t)(group+1)*96].x-
                           input[(size_t)group*96].x;
        const float after=output[(size_t)(group+1)*96].x-
                          output[(size_t)group*96].x;
        assert(after<before-0.03f);
        assert(after>0.025f-0.00001f);
    }
    // Second atlas is unaffected by the four-colour region layout.
    assert(output[384].x>input[384].x);
    assert(output[444].x<input[444].x);
    assert(output.back().x==input.back().x);
    std::cout<<"12 - whole-colour region convergence, identity and spacing PASS\n";
}
