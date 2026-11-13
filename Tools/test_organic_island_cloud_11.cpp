#include "OrganicIslandCloud11.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>
using Cloud=Atlas::OrganicIslandCloud11;
using Point=Cloud::Point;
int main()
{
    std::vector<Point> positions;
    std::vector<int> groups;
    std::vector<uint32_t> seeds;
    for (int i=0;i<800;++i)
    {
        positions.push_back({0.30f+(float)i*0.00040f,
                             0.50f+(float)(i%7)*0.00010f});
        groups.push_back(3);
        seeds.push_back((uint32_t)i*103u+1u);
    }
    // Already tall islands should not be unnecessarily circularised.
    for (int i=0;i<64;++i)
    {
        positions.push_back({0.8f+0.001f*(float)(i%3),
                             0.1f+0.008f*(float)i});
        groups.push_back(4);
        seeds.push_back((uint32_t)i*29u+11u);
    }
    // Sources with no validated CLAP island remain exactly unchanged.
    positions.push_back({0.95f,0.87f});
    groups.push_back(-1);
    seeds.push_back(123456u);

    const auto moved=Cloud::arrange(positions,groups,seeds,1500.0f,520.0f);
    const auto repeated=Cloud::arrange(positions,groups,seeds,1500.0f,520.0f);
    assert(moved.size()==positions.size());
    double oldX=0,oldY=0,newX=0,newY=0;
    for (size_t i=0;i<positions.size();++i)
    {
        assert(moved[i].x==repeated[i].x && moved[i].y==repeated[i].y);
        assert(std::isfinite(moved[i].x) && std::isfinite(moved[i].y));
        if(i<800u)
        {
            oldX+=positions[i].x;oldY+=positions[i].y;
            newX+=moved[i].x;newY+=moved[i].y;
        }
        else
        {
            assert(moved[i].x==positions[i].x);
            assert(moved[i].y==positions[i].y);
        }
    }
    oldX/=800.0;oldY/=800.0;newX/=800.0;newY/=800.0;
    assert(std::abs(newX-oldX)<0.002);
    assert(std::abs(newY-oldY)<0.003);
    double oldVX=0,oldVY=0,newVX=0,newVY=0;
    for(int i=0;i<800;++i)
    {
        oldVX+=std::pow((positions[(size_t)i].x-oldX)*1444.0,2);
        oldVY+=std::pow((positions[(size_t)i].y-oldY)*470.0,2);
        newVX+=std::pow((moved[(size_t)i].x-newX)*1444.0,2);
        newVY+=std::pow((moved[(size_t)i].y-newY)*470.0,2);
    }
    const double ratioBefore=std::sqrt(oldVX/800.0)/std::max(1.0,std::sqrt(oldVY/800.0));
    const double ratioAfter=std::sqrt(newVX/800.0)/std::max(1.0,std::sqrt(newVY/800.0));
    assert(ratioAfter<ratioBefore*0.45);
    assert(ratioAfter<3.5);
    std::cout << "11 - deterministic CLAP-preserving organic clouds PASS\n";
}
