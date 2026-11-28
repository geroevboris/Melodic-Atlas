#include "FamilyRegisterLayout13.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

using Layout = Atlas::FamilyRegisterLayout13;

int main()
{
    std::vector<Layout::Member> members;
    // A 72/28 family must stay in LOW-MID as ONE compact cluster.
    for (int i = 0; i < 100; ++i)
        members.push_back({0, 0.32f + (float)(i % 13) * 0.001f,
            0.26f + (float)(i % 17) * 0.001f,
            i < 72 ? 0.35f : 0.60f});
    // A mixed 60/40 family should lie NEAR the LOW-MID/HIGH-MID
    // boundary, not be cut into widely separated spectral islands.
    for (int i = 0; i < 100; ++i)
        members.push_back({1, 0.67f + (float)(i % 11) * 0.001f,
            0.70f + (float)(i % 19) * 0.001f,
            i < 60 ? 0.39f : 0.63f});
    // Deep bass and bright highs are still left and right, respectively.
    for (int i = 0; i < 50; ++i)
        members.push_back({2, 0.48f, 0.45f + (float)i * 0.0001f, 0.09f});
    for (int i = 0; i < 50; ++i)
        members.push_back({3, 0.48f, 0.45f + (float)i * 0.0001f, 0.89f});
    // Unclassified, non-CLAP files remain exactly where they came from.
    members.push_back({-1, 0.23f, 0.87f, 0.42f});

    auto output = Layout::arrange(members);
    auto repeated = Layout::arrange(members);
    assert(output.size() == members.size());
    for (size_t i = 0; i < output.size(); ++i)
    {
        assert(std::isfinite(output[i].x) && std::isfinite(output[i].y));
        assert(output[i].x == repeated[i].x
            && output[i].y == repeated[i].y);
        assert(output[i].zone == repeated[i].zone);
        assert(output[i].x >= 0.0f && output[i].x <= 1.0f);
        assert(output[i].y >= 0.0f && output[i].y <= 1.0f);
        if (i + 1 != output.size())
            assert(output[i].zone == Layout::zoneForX(output[i].x));
    }
    assert(output.back().x == members.back().sourceX);
    assert(output.back().y == members.back().sourceY);

    auto centroid = [&](int a, int b) {
        float sum = 0.0f;
        for (int i = a; i < b; ++i)
            sum += output[(size_t)i].x;
        return sum / (float)(b - a);
    };
    const float majority = centroid(0, 100);
    const float mixed = centroid(100, 200);
    const float bass = centroid(200, 250);
    const float high = centroid(250, 300);
    assert(majority >= 0.25f && majority < 0.50f);
    assert(mixed > 0.43f && mixed < 0.54f);
    assert(bass < majority);
    assert(high > mixed);

    for (int start : {0, 100, 200, 250})
    {
        const int end = start == 0 || start == 100 ? start + 100 : start + 50;
        float minX = 1.0f, maxX = 0.0f;
        float minY = 1.0f, maxY = 0.0f;
        for (int i = start; i < end; ++i)
        {
            const auto& pt = output[(size_t)i];
            minX = std::min(minX, pt.x);
            maxX = std::max(maxX, pt.x);
            minY = std::min(minY, pt.y);
            maxY = std::max(maxY, pt.y);
        }
        // No full-map stretches, even with opposing spectral evidence.
        assert(maxX - minX <= 0.140f);
        assert(maxY - minY <= 0.140f);
    }
    // Keeping original source UMAP shape must not be replaced by spectral
    // islands; small per-record spectral nudges stay within the same cloud.
    assert(output[10].x < output[90].x + 0.13f);
    std::cout << "13 - CLAP family register placement and bounded integrity PASS\n";
}
