#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

// Cheap, independent structure analysis using amplitude events only.
// An active melodic arpeggio is a sequence too. NEVER use this alone
// to decide if a sound is percussion or to hide it from the atlas.
// Intended only on scan/Refresh worker threads, not audio callback.
namespace Atlas
{
    struct RhythmSequenceEvidence9F
    {
        struct Result
        {
            int onsetCount = 0;
            float onsetSpanSeconds = 0.0f;
            bool isSequence = false;
        };

        static Result analyse(const float* mono, int n, double sampleRate)
        {
            Result result;
            if (mono == nullptr || n < 4096 || !(sampleRate >= 8000.0)
                || !std::isfinite(sampleRate))
                return result;
            const int hop = std::max(64, (int)std::lround(sampleRate * 0.010));
            const int frames = n / hop;
            if (frames < 120)
                return result;
            std::vector<float> envelope((size_t)frames, 0.0f);
            float maximum = 0.0f;
            for (int f = 0; f < frames; ++f)
            {
                double energy = 0.0;
                const int start = f * hop;
                for (int j = 0; j < hop; ++j)
                {
                    const float v = mono[start + j];
                    if (std::isfinite(v))
                        energy += (double)v * v;
                }
                const float rms = (float)std::sqrt(energy / hop + 1.0e-11);
                envelope[(size_t)f] = rms;
                maximum = std::max(maximum, rms);
            }
            if (maximum < 1.0e-5f)
                return result;

            const int localWindow = 11; // +/- 110 ms, adaptive peak contrast
            const int minSeparation = 9; // events >= 90 ms apart
            const float prominence = maximum * 0.060f;
            const float minimumPeak = maximum * 0.12f;
            struct Event { int index; float peak; };
            std::vector<Event> events;
            events.reserve(64);

            for (int i = 2; i < frames - 2; ++i)
            {
                const float current = envelope[(size_t)i];
                if (!(current >= envelope[(size_t)i - 1]
                    && current > envelope[(size_t)i + 1]
                    && current > minimumPeak))
                    continue;
                float leftMin = current, rightMin = current;
                for (int k = std::max(0, i - localWindow); k < i; ++k)
                    leftMin = std::min(leftMin, envelope[(size_t)k]);
                for (int k = i + 1; k < std::min(frames, i + localWindow + 1); ++k)
                    rightMin = std::min(rightMin, envelope[(size_t)k]);
                if (current - std::max(leftMin, rightMin) < prominence)
                    continue;
                if (!events.empty() && i - events.back().index < minSeparation)
                {
                    if (current > events.back().peak)
                        events.back() = { i, current };
                }
                else
                    events.push_back({ i, current });
            }
            result.onsetCount = (int)events.size();
            if (events.size() > 1)
                result.onsetSpanSeconds = (float)(
                    (events.back().index - events.front().index)
                    * (double)hop / sampleRate);
            const float analysedSeconds = (float)(
                frames * (double)hop / sampleRate);
            result.isSequence = result.onsetCount >= 6
                && result.onsetSpanSeconds >= 1.3f
                && result.onsetSpanSeconds >= 0.35f * analysedSeconds;
            return result;
        }
    };
}
