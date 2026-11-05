#pragma once
#include "AtlasTypes.h"
#include <array>
#include <cmath>
#include <algorithm>

namespace Atlas
{
    // Offline exploratory tonal descriptors. These are measurements, not
    // evidence that a sample belongs to a particular instrument or category.
    class TonalFeatureExtractor
    {
    public:
        static TonalFeatures analyze(const float* mono, int count, double sampleRate)
        {
            TonalFeatures features;
            constexpr int window = 2048;
            if (mono == nullptr || count < window || sampleRate < 8000.0)
                return features;

            // Three windows spread over the available excerpt. No FFT or
            // unbounded allocation; this runs only on the scanner thread.
            const std::array<int, 3> starts = {
                0, (count - window) / 2, count - window
            };
            std::array<float, 3> pitches {};
            std::array<float, 3> strengths {};
            int valid = 0;

            for (int start : starts)
            {
                const auto measurement = measureWindow(mono + start, sampleRate);
                if (measurement.strength >= 0.30f &&
                    measurement.midi >= 24.0f &&
                    measurement.midi <= 96.0f)
                {
                    pitches[(size_t)valid] = measurement.midi;
                    strengths[(size_t)valid] = measurement.strength;
                    ++valid;
                }
            }

            features.valid = true;
            features.validWindowCount = valid;

            if (valid == 0)
                return features;

            float sum = 0.0f;
            for (int i = 0; i < valid; ++i)
                sum += strengths[(size_t)i];
            features.periodicity = juce::jlimit(0.0f, 1.0f, sum / valid);

            // A single usable window is not evidence of stable tuning.
            if (valid >= 2)
            {
                float mean = 0.0f;
                for (int i = 0; i < valid; ++i)
                    mean += pitches[(size_t)i];
                mean /= valid;

                float variance = 0.0f;
                for (int i = 0; i < valid; ++i)
                {
                    const float delta = pitches[(size_t)i] - mean;
                    variance += delta * delta;
                }
                const float deviation = std::sqrt(variance / valid);
                features.pitchDeviationSemitones = deviation;
                features.pitchStability = juce::jlimit(
                    0.0f, 1.0f, 1.0f - deviation / 2.0f);
            }

            // Median helps avoid one erratic estimate; it is not a
            // validated pitch transcription and must not retune pads.
            std::sort(pitches.begin(), pitches.begin() + valid);
            features.estimatedMidi = pitches[(size_t)(valid / 2)];
            return features;
        }

    private:
        struct WindowEstimate
        {
            float midi = 0.0f;
            float strength = 0.0f;
        };

        static WindowEstimate measureWindow(const float* x, double sampleRate)
        {
            constexpr int n = 2048;
            const int minLag = std::max(12, (int)(sampleRate / 1100.0));
            const int maxLag = std::min(n / 2 - 1, (int)(sampleRate / 55.0));
            if (minLag >= maxLag)
                return {};

            // Evaluate a normalized correlation on a fixed segment so that
            // successive lag values do not change the signal window length.
            constexpr int segment = 1024;
            double baseEnergy = 0.0;
            for (int i = 0; i < segment; ++i)
            {
                const double a = std::isfinite(x[i]) ? x[i] : 0.0;
                baseEnergy += a * a;
            }
            if (baseEnergy < 1.0e-7)
                return {};

            float bestScore = 0.0f;
            int bestLag = 0;
            for (int lag = minLag; lag <= maxLag; ++lag)
            {
                double cross = 0.0, shiftedEnergy = 0.0;
                for (int i = 0; i < segment; ++i)
                {
                    const double a = std::isfinite(x[i]) ? x[i] : 0.0;
                    const double b = std::isfinite(x[i + lag]) ? x[i + lag] : 0.0;
                    cross += a * b;
                    shiftedEnergy += b * b;
                }
                if (shiftedEnergy < 1.0e-9)
                    continue;
                const float score = (float)juce::jlimit(
                    -1.0, 1.0,
                    cross / std::sqrt(baseEnergy * shiftedEnergy));
                if (score > bestScore)
                {
                    bestScore = score;
                    bestLag = lag;
                }
            }

            if (bestLag == 0)
                return {};

            const double hz = sampleRate / bestLag;
            const float midi = (float)(69.0 + 12.0 * std::log2(hz / 440.0));
            return { midi, juce::jlimit(0.0f, 1.0f, bestScore) };
        }
    };
}
