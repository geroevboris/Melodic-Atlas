#pragma once
#include "AtlasTypes.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace Atlas
{
    // Offline features only: never call this from the real-time audio callback.
    // No classification thresholds are inferred from these measurements.
    class AcousticFeatureExtractor
    {
    public:
        // Measure the very first audible onset, not the number or strength
        // of attacks throughout a loop. Scanner-only, bounded to four seconds.
        // 5 ms RMS frames with 15 ms smoothing reject isolated sample spikes.
        static bool measureInitialAttack(const float* mono, int count,
                                         double sampleRate, float& attackSec)
        {
            if (mono == nullptr || count < 128 || sampleRate < 8000.0)
                return false;
            const int limit = std::min(count, (int)(sampleRate * 4.0));
            const int hop = std::max(32, (int)(sampleRate * 0.005));
            const int n = limit / hop;
            if (n < 3)
                return false;
            std::vector<float> envelope((size_t)n);
            for (int i = 0; i < n; ++i)
            {
                double power = 0.0;
                const int offset = i * hop;
                for (int j = 0; j < hop; ++j)
                {
                    const float value = mono[offset + j];
                    if (std::isfinite(value))
                        power += (double)value * value;
                }
                envelope[(size_t)i] = (float)std::sqrt(power / hop);
            }
            std::vector<float> smooth((size_t)n);
            float peak = 0.0f;
            for (int i = 0; i < n; ++i)
            {
                const float before = envelope[(size_t)std::max(0, i - 1)];
                const float after = envelope[(size_t)std::min(n - 1, i + 1)];
                smooth[(size_t)i] = (before + envelope[(size_t)i] + after)
                    / 3.0f;
                peak = std::max(peak, smooth[(size_t)i]);
            }
            if (peak < 0.0005f)
                return false;

            // Start at 10% of a file's meaningful loudness, avoiding silent
            // pre-roll. Reach 80% of the same peak for the attack endpoint.
            int start = -1, reached = -1;
            for (int i = 0; i < n; ++i)
            {
                if (start < 0 && smooth[(size_t)i] >= peak * 0.10f)
                    start = i;
                if (start >= 0 && smooth[(size_t)i] >= peak * 0.80f)
                {
                    reached = i;
                    break;
                }
            }
            if (start < 0 || reached < 0)
                return false;
            attackSec = juce::jlimit(0.005f, 3.0f,
                (float)((reached - start) * (double)hop / sampleRate));
            return true;
        }

        static AcousticFeatures analyze(const float* mono, int count, double sampleRate)
        {
            AcousticFeatures result;
            if (mono == nullptr || count < 256 || sampleRate <= 0.0)
                return result;

            constexpr int fftOrder = 11;
            constexpr int fftSize = 1 << fftOrder;
            constexpr int bins = fftSize / 2;
            if (count < fftSize)
                return result;

            juce::dsp::FFT fft(fftOrder);
            std::vector<float> spectrum((size_t)fftSize * 2, 0.0f);
            constexpr int maxFrames = 24;
            const int available = 1 + (count - fftSize) / 1024;
            const int frames = std::min(available, maxFrames);

            double sumCentroid = 0.0, sumFlatness = 0.0;
            double sumLow = 0.0, sumMid = 0.0, sumHigh = 0.0;
            int goodFrames = 0;
            const double nyquist = sampleRate * 0.5;

            for (int frame = 0; frame < frames; ++frame)
            {
                // Cover the analysed duration rather than just its opening frames.
                const int start = (frames == 1) ? 0 :
                    (int)((int64_t)frame * (count - fftSize) / (frames - 1));
                std::fill(spectrum.begin(), spectrum.end(), 0.0f);

                double energy = 0.0;
                for (int i = 0; i < fftSize; ++i)
                {
                    const float x = std::isfinite(mono[start + i]) ?
                                    mono[start + i] : 0.0f;
                    const float w = 0.5f - 0.5f * std::cos(
                        juce::MathConstants<float>::twoPi * i / (fftSize - 1));
                    spectrum[(size_t)i] = x * w;
                    energy += (double)x * x;
                }
                if (energy < 1.0e-9)
                    continue;

                fft.performFrequencyOnlyForwardTransform(spectrum.data());

                double sumMag = 0.0, centroid = 0.0, logMag = 0.0;
                double low = 0.0, mid = 0.0, high = 0.0;
                constexpr double epsilon = 1.0e-12;
                for (int k = 1; k < bins; ++k)
                {
                    const double magnitude = std::max(0.0f, spectrum[(size_t)k]);
                    const double hz = (double)k * sampleRate / fftSize;
                    sumMag += magnitude;
                    centroid += magnitude * hz;
                    logMag += std::log(magnitude + epsilon);
                    if (hz < 250.0) low += magnitude;
                    else if (hz < 4000.0) mid += magnitude;
                    else high += magnitude;
                }
                if (sumMag < epsilon)
                    continue;

                const double meanMag = sumMag / (bins - 1);
                const double geometric = std::exp(logMag / (bins - 1));
                sumCentroid += centroid / sumMag;
                sumFlatness += std::min(1.0, geometric / (meanMag + epsilon));
                sumLow += low / sumMag;
                sumMid += mid / sumMag;
                sumHigh += high / sumMag;
                ++goodFrames;
            }

            if (goodFrames == 0)
                return result;

            result.spectralCentroidHz = (float)(sumCentroid / goodFrames);
            result.spectralCentroidNorm = (float)juce::jlimit(
                0.0, 1.0, (sumCentroid / goodFrames) / std::max(1.0, nyquist));
            result.spectralFlatness = (float)(sumFlatness / goodFrames);
            result.lowBandRatio = (float)(sumLow / goodFrames);
            result.midBandRatio = (float)(sumMid / goodFrames);
            result.highBandRatio = (float)(sumHigh / goodFrames);

            // Non-negative envelope differences provide a provisional
            // onset activity measurement. Not a percussion classifier.
            constexpr int envelopeHop = 256;
            const int blocks = count / envelopeHop;
            double previous = 0.0, positiveChanges = 0.0, envelopeSum = 0.0;
            double first = 0.0, last = 0.0;
            int firstCount = 0, lastCount = 0;
            for (int b = 0; b < blocks; ++b)
            {
                double squareSum = 0.0;
                for (int i = 0; i < envelopeHop; ++i)
                {
                    const float x = mono[b * envelopeHop + i];
                    if (std::isfinite(x))
                        squareSum += (double)x * x;
                }
                const double rms = std::sqrt(squareSum / envelopeHop);
                if (b > 0)
                    positiveChanges += std::max(0.0, rms - previous);
                previous = rms;
                envelopeSum += rms;
                if (b < std::max(1, blocks / 5)) { first += rms; ++firstCount; }
                if (b >= blocks - std::max(1, blocks / 5)) { last += rms; ++lastCount; }
            }
            result.onsetActivity = (float)juce::jlimit(
                0.0, 1.0, positiveChanges / std::max(1.0e-9, envelopeSum));
            result.sustainRatio = (float)juce::jlimit(
                0.0, 1.0, (last / std::max(1, lastCount)) /
                std::max(1.0e-9, first / std::max(1, firstCount)));
            result.attackMeasured = measureInitialAttack(
                mono, count, sampleRate, result.attackTimeSec);
            result.valid = true;
            return result;
        }
    };
}
