#pragma once
// 11: View-only bounded organic deformation inside existing CLAP islands.
// Never mutates saved projections, CLAP vectors, groups, or acoustic labels.
// All coordinates are normalised; amplitudes are measured in unzoomed pixels.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace Atlas {
struct OrganicIslandCloud11
{
    struct Point { float x = 0.5f, y = 0.5f; };

    static uint32_t scramble(uint32_t k)
    {
        k ^= k >> 16; k *= 0x7feb352du;
        k ^= k >> 15; k *= 0x846ca68bu;
        return k ^ (k >> 16);
    }

    static float unit(uint32_t k)
    {
        return (float)(scramble(k) & 0x00ffffffu) / 16777216.0f;
    }

    // id < 0 leaves unrelated/legacy samples exactly where they were.
    // Stable per-file seeds make the deformation independent of scan order.
    static std::vector<Point> arrange(const std::vector<Point>& source,
                                       const std::vector<int>& islandIds,
                                       const std::vector<uint32_t>& seeds,
                                       float width, float height)
    {
        if (source.size() != islandIds.size() ||
            source.size() != seeds.size() || width <= 58.0f || height <= 54.0f)
            return source;

        std::vector<Point> result = source;
        struct Group { std::vector<size_t> members; double sumX=0, sumY=0; };
        std::unordered_map<int,Group> groups;
        groups.reserve(80);
        for (size_t i=0; i<source.size(); ++i)
        {
            if (islandIds[i] < 0 || !std::isfinite(source[i].x) ||
                !std::isfinite(source[i].y))
                continue;
            auto& g=groups[islandIds[i]];
            g.members.push_back(i);
            g.sumX += source[i].x;
            g.sumY += source[i].y;
        }

        const float scaleX=width - 56.0f;
        const float scaleY=height - 50.0f;
        for (const auto& entry:groups)
        {
            const auto& member=entry.second.members;
            if (member.size()<12u)continue;
            const float centreX=(float)(entry.second.sumX/(double)member.size());
            const float centreY=(float)(entry.second.sumY/(double)member.size());
            double sumDx2=0.0, sumDy2=0.0;
            for (const auto idx:member)
            {
                // Clip extremes while measuring scale so a lone outlier
                // cannot distort hundreds of nearby sounds.
                const float dx=std::max(-180.0f,std::min(180.0f,
                    (source[idx].x-centreX)*scaleX));
                const float dy=std::max(-140.0f,std::min(140.0f,
                    (source[idx].y-centreY)*scaleY));
                sumDx2+=(double)dx*dx;
                sumDy2+=(double)dy*dy;
            }
            const float spreadX=std::sqrt((float)(sumDx2/(double)member.size()));
            const float spreadY=std::sqrt((float)(sumDy2/(double)member.size()));
            // Only strongly horizontal islands need help; naturally tall
            // or approximately isotropic ones retain their CLAP shape.
            const float ratio=spreadX/std::max(4.0f,spreadY);
            const float strength=std::max(0.0f,std::min(1.0f,
                (ratio-1.45f)/2.5f));
            if(strength<0.02f)continue;

            const float xGain=1.0f-0.30f*strength;
            const float yGain=1.0f+1.7f*strength;
            const float bloom=std::min(32.0f,std::max(9.0f,0.50f*spreadX))*strength;
            const float phase=6.28318530718f*unit((uint32_t)entry.first*53u+943u);
            double newSumX=0.0, newSumY=0.0;
            for(const auto idx:member)
            {
                const float dx=(source[idx].x-centreX)*scaleX;
                const float dy=(source[idx].y-centreY)*scaleY;
                const uint32_t seed=seeds[idx] ^ ((uint32_t)entry.first*0x9e3779b9u);
                const float a=6.28318530718f*unit(seed+11u);
                const float r=std::sqrt(unit(seed+37u));
                // Continuous warping keeps neighbouring CLAP coordinates
                // close; small seeded radial scatter breaks artificial rows.
                const float u=dx/std::max(10.0f,spreadX);
                const float coherent=0.58f*std::sin(u*1.65f+phase)
                    +0.31f*std::sin(u*3.05f+phase*0.63f);
                const float bx=centreX+(xGain*dx+0.23f*bloom*r*std::cos(a))/scaleX;
                const float by=centreY+(yGain*dy+bloom*(0.52f*coherent
                    +0.88f*r*std::sin(a)))/scaleY;
                result[idx] = {bx,by};
                newSumX += bx;
                newSumY += by;
            }
            // No group-level drift: only the cloud's internal arrangement
            // changes. This preserves the global low-to-high composition.
            const float driftX=(float)(newSumX/(double)member.size())-centreX;
            const float driftY=(float)(newSumY/(double)member.size())-centreY;
            for (const auto idx:member)
            {
                result[idx].x=std::max(0.002f,std::min(0.998f,result[idx].x-driftX));
                result[idx].y=std::max(0.002f,std::min(0.998f,result[idx].y-driftY));
            }
        }
        return result;
    }
};
}
