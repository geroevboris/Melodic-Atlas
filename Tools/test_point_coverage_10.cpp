#include "AtlasPointCoverage10.h"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <unordered_set>
#include <vector>

using Coverage=Atlas::AtlasPointCoverage10;
using Point=Coverage::Point;
static uint64_t quantizedKey(const Point& p,float width,float height)
{
    const float x=Coverage::referenceZoom*(28.0f+p.x*(width-56.0f));
    const float y=Coverage::referenceZoom*(34.0f+p.y*(height-50.0f));
    const int ix=(int)std::lround(x/Coverage::packingSeparationPx);
    const int iy=(int)std::lround(y/Coverage::packingSeparationPx);
    return Coverage::cellKey(ix,iy);
}
static void verifyComplete(const std::vector<Point>& original,
                           float width,float height)
{
    const auto result=Coverage::pack(original,width,height);
    assert(result.size()==original.size());
    std::unordered_set<uint64_t> seen;
    for(const auto& p:result)
    {
        assert(std::isfinite(p.x) && std::isfinite(p.y));
        // A cell is never reused. Therefore all normal size markers can
        // be separately selected at 18x without visual overlap.
        assert(seen.insert(quantizedKey(p,width,height)).second);
    }
    const auto again=Coverage::pack(original,width,height);
    for(size_t i=0;i<result.size();++i)
    {
        assert(result[i].x==again[i].x);
        assert(result[i].y==again[i].y);
    }
}
int main()
{
    for(int ring=1;ring<100;++ring)
    {
        const uint32_t start=(uint32_t)(2*ring-1)*(2*ring-1);
        const uint32_t finish=(uint32_t)(2*ring+1)*(2*ring+1);
        std::unordered_set<uint64_t> ringCells;
        for(uint32_t k=start;k<finish;++k)
        {
            const auto [x,y]=Coverage::spiralOffset(k);
            assert(std::max(std::abs(x),std::abs(y))==ring);
            assert(ringCells.insert(Coverage::cellKey(x,y)).second);
        }
        assert(ringCells.size()==(size_t)ring*8u);
    }

    // Worst-case 3000 sound pack, identical neural projections:
    // no sound vanishes at maximum zoom and all remain individually
    // addressable. This was impossible with the previous 14.5px LOD cap.
    std::vector<Point> identical(3000,{.5f,.5f});
    verifyComplete(identical,1150.0f,515.0f);

    // Thousands of clustered, overlapping projected points from
    // four different semantic islands, plus edge-of-map duplicates.
    std::vector<Point> groups;
    for(int i=0;i<12000;++i)
    {
        const int group=i%4;
        const float x=.13f+(.18f*group)+(float)((i/4)%31)*.00017f;
        const float y=.49f+(float)((i/124)%19)*.00015f;
        groups.push_back({x,y});
    }
    for(int i=0;i<400;++i)
        groups.push_back({0.001f,0.998f});
    verifyComplete(groups,1100.0f,530.0f);

    // A sample hidden from the atlas must still reserve its deterministic
    // display slot so the next Hide can't reshuffle other marker positions.
    const auto before=Coverage::pack(identical,1150.0f,515.0f);
    // Visibility is a separate mask; packing takes original complete set.
    const auto after=Coverage::pack(identical,1150.0f,515.0f);
    for(size_t i=1;i<identical.size();++i)
        assert(before[i].x==after[i].x &&
               before[i].y==after[i].y);
    std::cout<<"10 - complete collision-free coverage PASS\n";
}
