#pragma once
// View-only organic, collision-free point packing at the existing 18x zoom.
// Source CLAP embeddings, classification and persisted coordinates are untouched.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
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

    static uint32_t mix(uint64_t key)
    {
        uint32_t x=(uint32_t)(key ^ (key >> 32));
        x^=x>>16; x*=0x7feb352du;
        x^=x>>15; x*=0x846ca68bu;
        return x^(x>>16);
    }

    static std::vector<Point> pack(const std::vector<Point>& input,
                                   float viewportWidth, float viewportHeight)
    {
        if (viewportWidth<=58.0f || viewportHeight<=54.0f)
            return input;
        std::vector<Point> positions;
        positions.reserve(input.size());
        const float sw=viewportWidth-56.0f;
        const float sh=viewportHeight-50.0f;
        constexpr float sep=packingSeparationPx;
        constexpr float sepSq=sep*sep;
        constexpr float goldenAngle=2.399963229728653f;
        constexpr float tau=6.283185307179586f;

        // Continuous coordinates instead of rounded square-grid centres.
        // A spatial hash enforces Euclidean minimum separation, while the
        // seeded golden-angle spiral preserves local source neighbourhoods.
        struct Pixel { float x,y; };
        std::vector<Pixel> placed;
        placed.reserve(input.size());
        std::unordered_map<uint64_t,std::vector<uint32_t>> cells;
        cells.reserve(input.size()*2u+1u);
        std::unordered_map<uint64_t,uint32_t> nextCandidate;
        nextCandidate.reserve(input.size()/2u+1u);

        for (const auto& source:input)
        {
            const float mx=std::isfinite(source.x)?source.x:0.5f;
            const float my=std::isfinite(source.y)?source.y:0.5f;
            const float ox=referenceZoom*(28.0f+mx*sw);
            const float oy=referenceZoom*(34.0f+my*sh);
            const int baseX=(int)std::floor(ox/sep);
            const int baseY=(int)std::floor(oy/sep);
            const uint64_t baseKey=cellKey(baseX,baseY);
            uint32_t& cursor=nextCandidate[baseKey];
            const float phase=tau*(float)(mix(baseKey)&0x00ffffffu)/16777216.0f;
            float px=ox,py=oy;
            int cx=baseX,cy=baseY;
            for (;;)
            {
                const uint32_t k=cursor++;
                if (k!=0u)
                {
                    const float angle=phase+(float)k*goldenAngle;
                    const float radius=sep*0.79f*std::sqrt((float)k);
                    px=ox+radius*std::cos(angle);
                    py=oy+radius*std::sin(angle);
                }
                cx=(int)std::floor(px/sep);
                cy=(int)std::floor(py/sep);
                bool overlap=false;
                for(int dy=-1;dy<=1 && !overlap;++dy)
                    for(int dx=-1;dx<=1 && !overlap;++dx)
                    {
                        const auto it=cells.find(cellKey(cx+dx,cy+dy));
                        if(it==cells.end())continue;
                        for(uint32_t otherIdx:it->second)
                        {
                            const auto& p=placed[(size_t)otherIdx];
                            const float ddx=px-p.x,ddy=py-p.y;
                            if(ddx*ddx+ddy*ddy<sepSq-0.0001f)
                            { overlap=true; break; }
                        }
                    }
                if(!overlap)break;
            }
            cells[cellKey(cx,cy)].push_back((uint32_t)placed.size());
            placed.push_back({px,py});
            positions.push_back({(px/referenceZoom-28.0f)/sw,
                                 (py/referenceZoom-34.0f)/sh});
        }
        return positions;
    }
};
}
