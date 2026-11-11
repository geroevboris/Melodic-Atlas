#pragma once
#include "ClapTextBank9CData.h"
#include <JuceHeader.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

// Candidate-only zero-shot scorer. No production gate, no hard tag, no side
// effect on sampler, cache, filter or map. Use on a scanner/background thread.
namespace Atlas
{
    struct ClapTextScores9C
    {
        std::array<float, ClapTextBank9C::familyCount> familyCosines {};
        // Scores of all 52 prompts, needed for content-driven subgroups.
        std::array<float, ClapTextBank9C::promptCount> promptCosines {};
        std::array<int, ClapTextBank9C::familyCount> familyBestPrompt {};
        int topFamily = -1, secondFamily = -1, topPrompt = -1;
        float topCosine = -1.0f, secondCosine = -1.0f, margin = 0.0f;
        bool valid = false;

        ClapTextScores9C()
        {
            familyCosines.fill(-1.0f);
            promptCosines.fill(-1.0f);
            familyBestPrompt.fill(-1);
        }
    };

    struct ClapTextPrototypeCandidate9C
    {
        static ClapTextScores9C evaluate(
            const std::array<float, ClapTextBank9C::dimensions>& audio)
        {
            ClapTextScores9C out;
            const auto& vectors = getVectors();
            if (!vectors.valid)
                return out;

            double squared = 0.0;
            for (float x : audio)
            {
                if (!std::isfinite(x))
                    return out;
                squared += (double)x * x;
            }
            if (!(squared > 1.0e-16))
                return out;
            const float inverseNorm = (float)(1.0 / std::sqrt(squared));

            for (int p = 0; p < ClapTextBank9C::promptCount; ++p)
            {
                const auto& proto = vectors.unit[(size_t)p];
                const int family = ClapTextBank9C::prompts[(size_t)p].familyIndex;
                if (family < 0 || family >= ClapTextBank9C::familyCount)
                    return ClapTextScores9C {};

                double dot = 0.0;
                for (int d = 0; d < ClapTextBank9C::dimensions; ++d)
                    dot += (double)audio[(size_t)d] * proto[(size_t)d];

                const float cosine = (float)dot * inverseNorm;
                out.promptCosines[(size_t)p] = cosine;
                if (cosine > out.familyCosines[(size_t)family])
                {
                    out.familyCosines[(size_t)family] = cosine;
                    out.familyBestPrompt[(size_t)family] = p;
                }
            }

            for (int family = 0; family < ClapTextBank9C::familyCount; ++family)
            {
                const float v = out.familyCosines[(size_t)family];
                if (v > out.topCosine)
                {
                    out.secondCosine = out.topCosine;
                    out.secondFamily = out.topFamily;
                    out.topCosine = v;
                    out.topFamily = family;
                }
                else if (v > out.secondCosine)
                {
                    out.secondCosine = v;
                    out.secondFamily = family;
                }
            }
            if (out.topFamily >= 0)
            {
                out.topPrompt = out.familyBestPrompt[(size_t)out.topFamily];
                out.margin = out.topCosine - out.secondCosine;
                out.valid = true;
            }
            return out;
        }

    private:
        struct Vectors
        {
            bool valid = false;
            std::array<std::array<float, ClapTextBank9C::dimensions>,
                       ClapTextBank9C::promptCount> unit {};
        };

        static Vectors decodeVectors()
        {
            Vectors result;
            juce::MemoryOutputStream decoded;
            if (!juce::Base64::convertFromBase64(
                    decoded, juce::StringRef(ClapTextBank9C::encodedQ15)))
                return result;

            const size_t count = (size_t)ClapTextBank9C::promptCount
                               * (size_t)ClapTextBank9C::dimensions;
            if (decoded.getDataSize() != count * 2)
                return result;
            const auto* raw = static_cast<const uint8_t*>(decoded.getData());

            for (int p = 0; p < ClapTextBank9C::promptCount; ++p)
            {
                const float scale = ClapTextBank9C::prompts[(size_t)p].scale;
                if (!(std::isfinite(scale) && scale > 0.0f))
                    return Vectors {};
                double normSquared = 0.0;
                for (int d = 0; d < ClapTextBank9C::dimensions; ++d)
                {
                    const size_t i = ((size_t)p
                        * (size_t)ClapTextBank9C::dimensions + (size_t)d) * 2;
                    const unsigned value = (unsigned)raw[i]
                                         | ((unsigned)raw[i + 1] << 8);
                    const int signedValue = value < 32768u
                        ? (int)value : (int)value - 65536;
                    const float component = (float)signedValue * scale;
                    result.unit[(size_t)p][(size_t)d] = component;
                    normSquared += (double)component * component;
                }
                if (!(std::isfinite(normSquared) && normSquared > 1.0e-16))
                    return Vectors {};
                const float inv = (float)(1.0 / std::sqrt(normSquared));
                for (float& x : result.unit[(size_t)p])
                    x *= inv;
            }
            result.valid = true;
            return result;
        }

        static const Vectors& getVectors()
        {
            static const Vectors bank = decodeVectors();
            return bank;
        }
    };
}
