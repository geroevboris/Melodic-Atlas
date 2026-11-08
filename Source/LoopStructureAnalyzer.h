#pragma once
#include <JuceHeader.h>
#include <algorithm>
#include <cmath>
#include <vector>

namespace Atlas
{
    // Loop/one-shot is an independent structural property. A long sustained
    // note is not automatically a loop, and the word "arp" is not proof.
    struct LoopStructureAnalyzer
    {
        static bool isLoop(const juce::File& file, const float* mono,
                           int samples, double sampleRate, double duration)
        {
            const auto name = file.getFileNameWithoutExtension().toLowerCase();
            const auto parent = file.getParentDirectory().getFileName().toLowerCase();
            const auto grandparent =
                file.getParentDirectory().getParentDirectory().getFileName().toLowerCase();

            auto word = [](const juce::String& input, const juce::String& target)
            {
                juce::StringArray tokens;
                tokens.addTokens(input, " _-.()[]{}0123456789", "");
                return tokens.contains(target);
            };
            const bool taggedOneShot =
                word(name, "oneshot") || word(name, "oneshots")
                || (word(name, "one") && word(name, "shot"))
                || word(parent, "oneshots") || word(parent, "oneshot")
                || word(parent, "one shots") || word(grandparent, "oneshots");
            if (taggedOneShot)
                return false;

            const bool taggedLoop =
                word(name, "loop") || word(name, "loops")
                || word(name, "looped") || word(name, "drumloop")
                || word(name, "beatloop") || word(name, "breakbeat")
                || word(name, "groove") || word(name, "pattern")
                || word(name, "sequence") || word(name, "phrase")
                || word(name, "riff") || word(name, "arpeggio")
                || word(parent, "loops") || word(parent, "loop")
                || word(parent, "drumloops") || word(parent, "drum loops")
                || word(parent, "melodic loops") || word(parent, "phrases")
                || word(grandparent, "loops");
            if (taggedLoop)
                return true;

            // Without structural repetition, length and a BPM label alone
            // must never convert a one-shot pad/chord into a loop.
            if (!mono || sampleRate < 8000.0 || duration < 1.0
                || samples < 4096)
                return false;

            const int hop = std::max(64, (int)std::lround(sampleRate * 0.02));
            const int count = samples / hop;
            if (count < 65)
                return false;
            std::vector<float> envelope((size_t)count);
            float maximum = 0.0f;
            for (int i = 0; i < count; ++i)
            {
                double energy = 0.0;
                for (int j = 0; j < hop; ++j)
                {
                    const float v = mono[i * hop + j];
                    if (std::isfinite(v))
                        energy += (double)v * v;
                }
                envelope[(size_t)i] = (float)std::sqrt(energy / hop);
                maximum = std::max(maximum, envelope[(size_t)i]);
            }
            if (maximum < 1.0e-5f)
                return false;

            // Detect distinct, repeated energy attacks; a constant sustained
            // tone or a decaying one-shot does not pass this test.
            std::vector<int> onsets;
            const float threshold = maximum * 0.16f;
            const int minSeparation = std::max(3, (int)(0.12 * sampleRate / hop));
            for (int i = 2; i < count - 1; ++i)
            {
                const float rise = envelope[(size_t)i]
                    - envelope[(size_t)(i - 2)];
                if (rise > threshold
                    && envelope[(size_t)i] > maximum * 0.20f
                    && (onsets.empty() || i - onsets.back() >= minSeparation))
                    onsets.push_back(i);
            }
            if (onsets.size() < 3)
                return false;

            // Regularly spaced events are a conservative rhythm cue, not
            // a BPM estimate. Require several intervals to match one period.
            int bestMatches = 0;
            for (size_t a = 0; a + 1 < onsets.size(); ++a)
            {
                const int period = onsets[a + 1] - onsets[a];
                const double seconds = period * hop / sampleRate;
                if (seconds < 0.14 || seconds > 1.5)
                    continue;
                int matches = 0;
                for (size_t b = 1; b < onsets.size(); ++b)
                {
                    const int delta = onsets[b] - onsets[b - 1];
                    if (std::abs(delta - period) <= std::max(2, period / 7))
                        ++matches;
                }
                bestMatches = std::max(bestMatches, matches);
            }
            // Two repeated intervals imply a phrase; avoid promoting a
            // decaying one-shot with a few incidental transient tails.
            const bool strongSequence = bestMatches >= 2
                && bestMatches >= (int)(onsets.size() - 1) * 2 / 3;
            const bool enoughDuration = duration >= 1.4;
            return strongSequence && enoughDuration;
        }
    };
}
