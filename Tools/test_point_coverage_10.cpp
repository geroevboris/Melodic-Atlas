#include "AtlasPointCoverage10.h"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using Coverage=Atlas::AtlasPointCoverage10;
using Point=Coverage::Point;

static void verifyComplete(const std::vector<Point>& original,
                           float width,float height,
                           bool checkOrganic = false)
{
    const auto result=Coverage::pack(original,width,height);
    assert(result.size()==original.size());
    using CellKey=uint64_t;
    struct Pixel { float x, y; };
    std::unordered_map<CellKey,std::vector<Pixel>> occupied;
    occupied.reserve(result.size());
    std::unordered_set<int> uniqueY;
    constexpr float sep=Coverage::packingSeparationPx;
    for(const auto& p:result)
    {
        assert(std::isfinite(p.x) && std::isfinite(p.y));
        const float x=Coverage::referenceZoom*
            (28.0f+p.x*(width-56.0f));
        const float y=Coverage::referenceZoom*
            (34.0f+p.y*(height-50.0f));
        uniqueY.insert((int)std::lround(y*10.0f));
        const int cx=(int)std::floor(x/sep);
        const int cy=(int)std::floor(y/sep);
        // Collision-free means actual circular marker separation, not
        // merely different square-grid addresses.
        for(int dy=-1;dy<=1;++dy)
            for(int dx=-1;dx<=1;++dx)
            {
                const auto it=occupied.find(Coverage::cellKey(cx+dx,cy+dy));
                if(it==occupied.end())continue;
                for(const auto& q:it->second)
                {
                    const float xx=x-q.x,yy=y-q.y;
                    assert(xx*xx+yy*yy>=sep*sep-0.03f);
                }
            }
        occupied[Coverage::cellKey(cx,cy)].push_back({x,y});
    }
    if(checkOrganic)
        assert(uniqueY.size()>250u); // No visible aligned square rows.
    const auto again=Coverage::pack(original,width,height);
    for(size_t i=0;i<result.size();++i)
    {
        assert(result[i].x==again[i].x);
        assert(result[i].y==again[i].y);
    }
}
int main()
{
    // Worst-case 3000 identical source projections: all remain individually
    // selectable, with visually organic subpixel positions and no collisions.
    std::vector<Point> identical(3000,{.5f,.5f});
    verifyComplete(identical,1150.0f,515.0f,true);

    // Thousands of very tightly spaced CLAP projections from several
    // islands, plus edge duplicates.
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

    // Hiding is a visibility flag, so packing the unchanged source
    // sequence must preserve every previously assigned point.
    const auto before=Coverage::pack(identical,1150.0f,515.0f);
    const auto after=Coverage::pack(identical,1150.0f,515.0f);
    for(size_t i=0;i<identical.size();++i)
        assert(before[i].x==after[i].x && before[i].y==after[i].y);
    std::cout<<"10 - complete organic collision-free coverage PASS\n";
}
