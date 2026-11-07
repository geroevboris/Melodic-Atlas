#pragma once
#include <JuceHeader.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <limits>
#include <vector>

namespace Atlas
{
    // CPU-side feature extractor for the fixed-shape HTSAT unfused audio
    // encoder. Always run from a worker, never from processBlock().
    //
    // Reference shape: [1, 1, 1001, 64] float log-mel dB, 48 kHz,
    // 1024-sample Hann FFT, 480-sample hop, 64 Slaney mel bands, 0–14 kHz.
    //
    // IMPORTANT: This is a first native implementation. Before enabling
    // production inference, compare these features numerically against
    // Hugging Face ClapFeatureExtractor with reference WAV fixtures. The
    // approximate linear resampler is not guaranteed equivalent to librosa.
    struct ClapAudioFeatures
    {
        static constexpr int sampleRate = 48000;
        static constexpr int fftSize = 1024;
        static constexpr int hopSize = 480;
        static constexpr int bands = 64;
        static constexpr int frames = 1001;
        static constexpr int samples = sampleRate * 10;

        static bool fromFile(const juce::File& file,
                             juce::AudioFormatManager& formats,
                             std::vector<float>& output)
        {
            std::unique_ptr<juce::AudioFormatReader> reader(
                formats.createReaderFor(file));
            if (!reader || reader->sampleRate < 8000.0 ||
                reader->numChannels < 1 || reader->lengthInSamples < 1)
                return false;

            const int64_t maxSource = (int64_t)std::ceil(reader->sampleRate * 10.0);
            const auto toRead64 = std::min(reader->lengthInSamples, maxSource);
            if (toRead64 < 1 || toRead64 > (int64_t)std::numeric_limits<int>::max())
                return false;
            const int toRead = (int)toRead64;
            const int channels = juce::jlimit(1, 2, (int)reader->numChannels);
            juce::AudioBuffer<float> decoded(channels, toRead);
            if (!reader->read(&decoded, 0, toRead, 0, true, channels > 1))
                return false;

            std::vector<float> source((size_t)toRead);
            for (int i = 0; i < toRead; ++i)
            {
                float value = decoded.getSample(0, i);
                if (channels > 1)
                    value = 0.5f * (value + decoded.getSample(1, i));
                source[(size_t)i] = std::isfinite(value) ? value : 0.0f;
            }

            // Repeat complete recordings, then zero-pad (HF repeatpad).
            // Linear resampling is deterministic; verify against HF before
            // selecting this backend for production CLAP inference.
            const int resampledCount = juce::jlimit(1, samples,
                (int)std::llround((double)toRead * (double)sampleRate
                                  / reader->sampleRate));
            std::vector<float> waveform((size_t)samples, 0.0f);
            for (int i = 0; i < samples; ++i)
            {
                // repeatpad repeats only complete copies.
                const int repeats = samples / resampledCount;
                if (i >= repeats * resampledCount)
                    break;
                const int sourceFrame = i % resampledCount;
                const double position = (double)sourceFrame
                    * reader->sampleRate / (double)sampleRate;
                const int idx = juce::jlimit(0, toRead - 1, (int)position);
                const int next = std::min(idx + 1, toRead - 1);
                const float frac = (float)(position - idx);
                waveform[(size_t)i] =
                    source[(size_t)idx] * (1.0f - frac)
                    + source[(size_t)next] * frac;
            }

            // These filter coefficients and window values depend only on
            // fixed CLAP configuration, not on the audio file.
            static const auto melFilters = makeMelFilters();
            static const auto hann = [] {
                std::array<float, fftSize> result {};
                for (int n = 0; n < fftSize; ++n)
                    result[(size_t)n] = 0.5f -
                        0.5f * std::cos(juce::MathConstants<float>::twoPi
                                        * (float)n / (float)fftSize);
                return result;
            }();
            // Iterate only bins with non-zero mel weights. The non-zero
            // products remain in the original ascending bin order.
            static const auto melBinRanges = [] {
                std::array<std::pair<int, int>, bands> result {};
                for (int m = 0; m < bands; ++m)
                {
                    int begin = 0;
                    while (begin <= fftSize / 2 &&
                           melFilters[(size_t)m][(size_t)begin] == 0.0f)
                        ++begin;
                    int end = fftSize / 2 + 1;
                    while (end > begin &&
                           melFilters[(size_t)m][(size_t)(end - 1)] == 0.0f)
                        --end;
                    result[(size_t)m] = { begin, end };
                }
                return result;
            }();

            juce::dsp::FFT fft(10);
            std::array<float, fftSize * 2> fftBuffer {};
            std::array<float, fftSize / 2 + 1> power {};
            output.assign((size_t)frames * bands, 0.0f);

            for (int t = 0; t < frames; ++t)
            {
                fftBuffer.fill(0.0f);
                for (int n = 0; n < fftSize; ++n)
                {
                    int index = t * hopSize + n - fftSize / 2;
                    if (index < 0)
                        index = -index;
                    if (index >= samples)
                        index = 2 * samples - 2 - index;
                    index = juce::jlimit(0, samples - 1, index);
                    fftBuffer[(size_t)n] =
                        waveform[(size_t)index] * hann[(size_t)n];
                }
                fft.performRealOnlyForwardTransform(fftBuffer.data());
                for (int k = 0; k <= fftSize / 2; ++k)
                {
                    const auto real = fftBuffer[(size_t)2 * k];
                    const auto imag = fftBuffer[(size_t)2 * k + 1];
                    power[(size_t)k] = real * real + imag * imag;
                }
                for (int m = 0; m < bands; ++m)
                {
                    double energy = 0.0;
                    const auto range = melBinRanges[(size_t)m];
                    for (int k = range.first; k < range.second; ++k)
                        energy += (double)power[(size_t)k] *
                                  melFilters[(size_t)m][(size_t)k];
                    output[(size_t)t * bands + (size_t)m] =
                        (float)(10.0 * std::log10(std::max(1e-10, energy)));
                }
            }
            return true;
        }

    private:
        // Slaney mel scale and area-normalized triangular filters, same
        // conceptual filter family as HF's non-fusion ClapFeatureExtractor.
        static double hzToMel(double hz)
        {
            if (hz < 1000.0)
                return hz * 3.0 / 200.0;
            return 15.0 + std::log(hz / 1000.0) /
                (std::log(6.4) / 27.0);
        }

        static double melToHz(double mel)
        {
            if (mel < 15.0)
                return mel * 200.0 / 3.0;
            return 1000.0 *
                std::exp((mel - 15.0) * std::log(6.4) / 27.0);
        }

        using Bank = std::array<std::array<float, fftSize / 2 + 1>, bands>;

        static Bank makeMelFilters()
        {
            Bank bank {};
            std::array<double, bands + 2> boundaries {};
            const double maxMel = hzToMel(14000.0);
            for (int i = 0; i < bands + 2; ++i)
                boundaries[(size_t)i] =
                    melToHz(maxMel * i / (bands + 1));

            for (int m = 0; m < bands; ++m)
            {
                const double lo = boundaries[(size_t)m];
                const double mid = boundaries[(size_t)m + 1];
                const double hi = boundaries[(size_t)m + 2];
                const double normalization = 2.0 / (hi - lo);
                for (int k = 0; k <= fftSize / 2; ++k)
                {
                    const double hz = (double)k * sampleRate / fftSize;
                    const double rising = (hz - lo) / (mid - lo);
                    const double falling = (hi - hz) / (hi - mid);
                    bank[(size_t)m][(size_t)k] =
                        (float)(normalization * std::max(
                            0.0, std::min(rising, falling)));
                }
            }
            return bank;
        }
    };
}
