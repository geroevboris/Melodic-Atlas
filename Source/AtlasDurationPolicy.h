#pragma once
#include <cmath>

namespace Atlas
{
    // Global Atlas visibility / scan admission rule. Source audio on disk and
    // samples already assigned to pads are never deleted or rewritten.
    struct AtlasDurationPolicy
    {
        static constexpr double minimumSeconds = 2.0;

        static bool isEligible(double durationSeconds) noexcept
        {
            return std::isfinite(durationSeconds)
                && durationSeconds >= minimumSeconds;
        }
    };
}
