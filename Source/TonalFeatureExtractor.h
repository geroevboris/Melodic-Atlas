#pragma once
#include "AtlasTypes.h"
#include <array>
#include <cmath>
#include <algorithm>
#include <vector>

namespace Atlas
{
    // Scanner-only pitch evidence. Uses a low-rate YIN-style difference
    // function rather than raw autocorrelation; no decision reaches the audio
    // engine or triggers automatic pitch correction.
    class TonalFeatureExtractor
    {
    public:
        static TonalFeatures analyze(const float* mono, int count, double sampleRate)
        {
            TonalFeatures out;
            if (mono == nullptr || count < 1024 || sampleRate < 8000.0)
                return out;

            // Decimation gives useful low-frequency periods at a bounded CPU
            // cost. A simple low-pass box average limits high-frequency aliases.
            const int stride = std::max(1, (int)std::round(sampleRate / 12000.0));
            const double reducedRate = sampleRate / stride;
            const int reducedCount = count / stride;
            constexpr int window = 2048;
            constexpr int minWindow = 768;
            if (reducedCount < minWindow)
                return out;
            const int n = std::min(window, reducedCount);
            const int segment = n / 2;

            std::vector<float> reduced((size_t)reducedCount);
            for (int i = 0; i < reducedCount; ++i)
            {
                double sum = 0.0;
                for (int j = 0; j < stride; ++j)
                {
                    const float sample = mono[i * stride + j];
                    sum += std::isfinite(sample) ? sample : 0.0;
                }
                reduced[(size_t)i] = (float)(sum / stride);
            }

            // Choose three non-overlapping-ish positions when possible, but
            // still admit short one-shots. Avoid the very first transient.
            const int available = reducedCount - n;
            const std::array<int, 3> starts = {
                available / 10, available / 2, available * 9 / 10
            };
            std::array<float, 3> pitches {};
            std::array<float, 3> strengths {};
            int valid = 0;

            for (int start : starts)
            {
                auto estimate = measureWindow(reduced.data() + start, n,
                                              segment, reducedRate);
                if (estimate.strength >= 0.52f
                    && estimate.midi >= 24.0f && estimate.midi <= 96.0f)
                {
                    pitches[(size_t)valid] = estimate.midi;
                    strengths[(size_t)valid] = estimate.strength;
                    ++valid;
                }
            }

            out.valid = true;
            out.validWindowCount = valid;
            if (valid == 0)
                return out;

            float totalStrength = 0.0f;
            for (int i = 0; i < valid; ++i)
                totalStrength += strengths[(size_t)i];
            out.periodicity = juce::jlimit(0.0f, 1.0f, totalStrength / valid);
            std::sort(pitches.begin(), pitches.begin() + valid);
            out.estimatedMidi = pitches[(size_t)(valid / 2)];

            // Compare pitch estimates in semitones: an octave error cannot
            // masquerade as pitch stability. One window stays inconclusive.
            if (valid >= 2)
            {
                const float median = out.estimatedMidi;
                float variance = 0.0f;
                for (int i = 0; i < valid; ++i)
                {
                    const float delta = pitches[(size_t)i] - median;
                    variance += delta * delta;
                }
                out.pitchDeviationSemitones = std::sqrt(variance / valid);
                out.pitchStability = juce::jlimit(
                    0.0f, 1.0f, 1.0f - out.pitchDeviationSemitones / 1.25f);
            }
            return out;
        }

    private:
        struct Estimate
        {
            float midi = 0.0f;
            float strength = 0.0f;
        };

        static Estimate measureWindow(const float* x, int n, int segment,
                                      double sampleRate)
        {
            const int minLag = std::max(7, (int)(sampleRate / 1100.0));
            const int maxLag = std::min(n - segment - 1,
                                        (int)(sampleRate / 55.0));
            if (maxLag <= minLag)
                return {};

            double energy = 0.0;
            for (int i = 0; i < segment; ++i)
                energy += (double)x[i] * x[i];
            if (energy < segment * 1.0e-8)
                return {};

            std::vector<double> diff((size_t)maxLag + 1, 0.0);
            for (int tau = 1; tau <= maxLag; ++tau)
            {
                double value = 0.0;
                for (int i = 0; i < segment; ++i)
                {
                    const double delta = (double)x[i] - x[i + tau];
                    value += delta * delta;
                }
                diff[(size_t)tau] = value;
            }

            // YIN cumulative mean normalized difference.
            double cumulative = 0.0;
            std::vector<double> cmnd((size_t)maxLag + 1, 1.0);
            for (int tau = 1; tau <= maxLag; ++tau)
            {
                cumulative += diff[(size_t)tau];
                cmnd[(size_t)tau] = cumulative > 1.0e-12
                    ? diff[(size_t)tau] * tau / cumulative : 1.0;
            }

            // Select the first credible local minimum (avoids the common
            // octave-down error from taking the global correlation maximum).
            int lag = 0;
            for (int tau = minLag; tau < maxLag; ++tau)
            {
                if (cmnd[(size_t)tau] < 0.16)
                {
                    while (tau + 1 <= maxLag &&
                           cmnd[(size_t)tau + 1] < cmnd[(size_t)tau])
                        ++tau;
                    lag = tau;
                    break;
                }
            }
            if (lag == 0)
            {
                // No clear periodicity: don't manufacture a note from noise.
                return {};
            }

            double refined = (double)lag;
            if (lag > 1 && lag < maxLag)
            {
                const double left = cmnd[(size_t)lag - 1];
                const double center = cmnd[(size_t)lag];
                const double right = cmnd[(size_t)lag + 1];
                const double denom = left - 2.0 * center + right;
                if (std::abs(denom) > 1.0e-12)
                    refined += juce::jlimit(-0.5, 0.5,
                        0.5 * (left - right) / denom);
            }
            if (refined <= 0.0)
                return {};
            const double hz = sampleRate / refined;
            const double midi = 69.0 + 12.0 * std::log2(hz / 440.0);
            const float strength = (float)juce::jlimit(
                0.0, 1.0, 1.0 - cmnd[(size_t)lag]);
            if (!std::isfinite(midi))
                return {};
            return { (float)midi, strength };
        }
    };
}
