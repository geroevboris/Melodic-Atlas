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

            auto words = [](const juce::String& input)
            {
                juce::StringArray result;
                result.addTokens(input, " _-.()[]{}\\/+", "");
                result.removeEmptyStrings();
                return result;
            };
            const auto filenameTokens = words(name);
            const auto parentTokens = words(parent);
            const auto grandparentTokens = words(grandparent);
            const auto contains = [](const juce::StringArray& tokens,
                                     const juce::String& needle)
            {
                return tokens.contains(needle);
            };
            const auto folderHas = [&](const juce::StringArray& tokens,
                                       const char* first, const char* second)
            {
                return contains(tokens, first) || contains(tokens, second);
            };

            // Filename-level evidence ALWAYS outranks the parent folder:
            // "One Shots/123bpm_MelodicLoop.wav" is still a loop.
            const bool explicitLoopFile =
                name.contains("loop") || name.contains("breakbeat")
                || contains(filenameTokens, "groove")
                || contains(filenameTokens, "pattern")
                || contains(filenameTokens, "sequence")
                || contains(filenameTokens, "phrase")
                || contains(filenameTokens, "riff");
            if (explicitLoopFile)
                return true;

            const bool explicitOneShotFile =
                name.contains("oneshot") || name.contains("one-shot")
                || name.contains("one_shot")
                || (contains(filenameTokens, "one")
                    && contains(filenameTokens, "shot"))
                || contains(filenameTokens, "hit")
                || contains(filenameTokens, "stab");
            if (explicitOneShotFile)
                return false;

            const bool taggedOneShotFolder =
                folderHas(parentTokens, "oneshot", "oneshots")
                || folderHas(grandparentTokens, "oneshot", "oneshots")
                || (contains(parentTokens, "one")
                    && contains(parentTokens, "shots"))
                || (contains(grandparentTokens, "one")
                    && contains(grandparentTokens, "shots"));
            const bool taggedLoopFolder =
                folderHas(parentTokens, "loop", "loops")
                || folderHas(grandparentTokens, "loop", "loops")
                || parent.contains("loops") || parent.contains("looped");
            if (taggedLoopFolder)
                return true;

            // BPM in a name is evidence, but not by itself proof: audio with
            // a stable tone can be a long one-shot regardless of its BPM tag.
            // Support '120BPM', 'BPM120' and '120_BPM' without stripping
            // the digits from the filename.
            int labelledBpm = 0;
            for (int i = 0; i < filenameTokens.size(); ++i)
            {
                const auto token = filenameTokens[i];
                juce::String number;
                if (token == "bpm")
                {
                    if (i > 0)
                        number = filenameTokens[i - 1];
                    if (number.isEmpty() && i + 1 < filenameTokens.size())
                        number = filenameTokens[i + 1];
                }
                else if (token.endsWith("bpm"))
                    number = token.dropLastCharacters(3);
                else if (token.startsWith("bpm"))
                    number = token.substring(3);
                if (number.isNotEmpty()
                    && number.containsOnly("0123456789"))
                {
                    const int candidate = number.getIntValue();
                    if (candidate >= 50 && candidate <= 240)
                    {
                        labelledBpm = candidate;
                        break;
                    }
                }
            }
            const bool durationOnGrid = [&]()
            {
                if (labelledBpm == 0 || duration < 1.25)
                    return false;
                const double beat = 60.0 / (double)labelledBpm;
                for (int beats : {2, 4, 8, 16, 32})
                {
                    const double bar = beat * (double)beats;
                    if (std::abs(duration - bar) / bar <= 0.065)
                        return true;
                }
                return false;
            }();

            // An explicit BPM filename plus a musical beat-grid length is
            // stronger evidence than a generic 'One Shots' parent folder.
            // Otherwise the folder tag protects sustained individual notes.
            if (taggedOneShotFolder && !durationOnGrid)
                return false;

            // Without structural repetition, a time label alone must not
            // convert an arbitrary long note into a loop.
            if (!mono || sampleRate < 8000.0 || duration < 1.0
                || samples < 4096)
                return durationOnGrid;

            const int hop = std::max(64, (int)std::lround(sampleRate * 0.02));
            const int count = samples / hop;
            if (count < 65)
                return durationOnGrid;
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
                return durationOnGrid;

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
            const bool strongSequence = onsets.size() >= 4
                && bestMatches >= 3
                && bestMatches >= (int)(onsets.size() - 1) * 2 / 3;
            const bool enoughDuration = duration >= 1.4;
            return durationOnGrid || (strongSequence && enoughDuration);
        }
    };
}
