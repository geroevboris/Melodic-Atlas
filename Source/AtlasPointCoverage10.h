#pragma once
// 10: View-only, collision-free point packing at the existing maximum zoom.
// JUCE-free so the contract can be tested independently in CI.
// Source coordinates/CLAP embeddings are never modified.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Atlas {
struct AtlasPointCoverage10
{
    struct Point { float x=0.5f, y=0.5f; };
    static constexpr float referenceZoom = 18.0f;
    static constexpr float packingSeparationPx = 6.6f;

    static uint64_t cellKey(int x, int y)
    {
        return ((uint64_t)(uint32_t)x << 32) | (uint32_t)y;
    }

    // Outward square spiral in O(1) per step. Reused origin cells start
    // where the previous sound finished: identical coordinates don't
    // lead to quadratic rescanning on a large drum/FX sample pack.
    static std::pair<int,int> spiralOffset(uint32_t index)
    {
        if(index==0u)return {0,0};
        const int ring=(int)std::ceil(
            (std::sqrt((double)index+1.0)-1.0)*0.5);
        const int side=2*ring;
        const uint32_t first=(uint32_t)(2*ring-1)*(uint32_t)(2*ring-1);
        const int t=(int)(index-first);
        if(t<side)return {ring,-ring+t};
        if(t<2*side)return {ring-(t-side),ring};
        if(t<3*side)return {-ring,ring-(t-2*side)};
        return {-ring+(t-3*side),-ring};
    }

    static std::vector<Point> pack(const std::vector<Point>& input,
                                   float viewportWidth, float viewportHeight)
    {
        std::vector<Point> positions;
        positions.reserve(input.size());
        if(viewportWidth<=58.0f || viewportHeight<=54.0f)
            return input;

        const float sw=viewportWidth-56.0f;
        const float sh=viewportHeight-50.0f;
        constexpr float sep=packingSeparationPx;
        std::unordered_set<uint64_t> occupied;
        std::unordered_map<uint64_t,uint32_t> nextCandidate;
        occupied.reserve(input.size()*2u+1u);
        nextCandidate.reserve(input.size()/2u+1u);

        for(const auto& source:input)
        {
            const float mx=std::isfinite(source.x)?source.x:0.5f;
            const float my=std::isfinite(source.y)?source.y:0.5f;
            const int bx=(int)std::lround(
                referenceZoom*(28.0f+mx*sw)/sep);
            const int by=(int)std::lround(
                referenceZoom*(34.0f+my*sh)/sep);
            const uint64_t baseKey=cellKey(bx,by);
            auto& candidate=nextCandidate[baseKey];
            int cx=bx,cy=by;
            for(;;)
            {
                const auto [ox,oy]=spiralOffset(candidate++);
                cx=bx+ox;
                cy=by+oy;
                if(occupied.insert(cellKey(cx,cy)).second)
                    break;
            }
            positions.push_back({
                (cx*sep/referenceZoom-28.0f)/sw,
                (cy*sep/referenceZoom-34.0f)/sh
            });
        }
        return positions;
    }
};
}
