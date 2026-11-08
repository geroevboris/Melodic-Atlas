#pragma once
#include "AtlasTypes.h"
#include <cmath>

namespace Atlas
{
    // Shared scan/map exclusion policy. Never delete a user's source file.
    // Strong metadata exclusions outrank a pitched drum's stable fundamental.
    class PercussionGate
    {
    public:
        // Deliberately narrow, word-boundary metadata gate. It must not
        // reject melodic sounds containing these syllables as substrings.
        // A pitched drum is still a drum for a melodic-only browser.
        static bool isExplicitDrumFile(const juce::File& file)
        {
            // Use filename AND explicitly named drum-category directories.
            // Keep arbitrary sample-pack names out of this decision.
            const auto hasDrumTokens = [](const juce::String& value)
            {
                juce::StringArray words;
                words.addTokens(value.toLowerCase(),
                                " _-.()[]{}0123456789", "");
                static constexpr const char* drumWords[] = {
                    "kick", "kicks", "bd", "snare", "snares", "snr", "sd",
                    "clap", "claps", "cp", "hihat", "hihats", "hh",
                    "closedhat", "openhat", "hat", "hats", "cymbal",
                    "cymbals", "crash", "ride", "rimshot", "rim",
                    "tambourine", "shaker", "shakers", "cowbell",
                    "tom", "toms", "drum", "drums", "percussion",
                    "perc", "conga", "bongo", "clave", "claves",
                    "woodblock", "tamb", "timbale", "timbales",
                    "cabasa", "guiro", "agogo", "triangle", "castanet",
                    "castanets", "finger", "snap", "snaps", "stick",
                    "sticks", "brush", "brushes", "click", "clicks"
                };
                for (const char* token : drumWords)
                    if (words.contains(token))
                        return true;
                return false;
            };
            const auto name = file.getFileNameWithoutExtension().toLowerCase();
            // Compact labels and common drum-loop naming conventions are
            // frequent in commercial sample packs (e.g. DrumLoop_128).
            static constexpr const char* explicitPatterns[] = {
                "drumloop", "drum_loop", "drum-loop", "drum loop",
                "drumbreak", "drum_break", "drum break",
                "breakbeat", "break_beat", "break beat",
                "drumfill", "drum_fill", "drum fill",
                "perc_loop", "perc-loop", "percussion_loop",
                "shakerloop", "claploop", "snareloop",
                "hihatloop", "hatloop", "kickloop"
            };
            for (const auto* token : explicitPatterns)
                if (name.contains(token))
                    return true;
            if (hasDrumTokens(name))
                return true;

            // Some libraries use anonymous numbered files stored beneath
            // Drums/Kicks/Snares/Hats. Match category folders, not whole packs.
            for (int level = 0; level < 2; ++level)
            {
                const auto dir = level == 0 ? file.getParentDirectory()
                    : file.getParentDirectory().getParentDirectory();
                const juce::String folder = dir.getFileName().toLowerCase();
                if (folder == "drums" || folder == "drum" ||
                    folder == "percussion" || folder == "perc" ||
                    folder == "kicks" || folder == "kick" ||
                    folder == "snares" || folder == "snare" ||
                    folder == "claps" || folder == "clap" ||
                    folder == "hats" || folder == "hihat" ||
                    folder == "hi hats" || folder == "cymbals" ||
                    folder == "shakers" || folder == "toms" ||
                    folder == "drum loops" || folder == "drumloops" ||
                    folder == "drum breaks" || folder == "breakbeats" ||
                    folder == "breaks" || folder == "percussion loops" ||
                    folder == "perc loops" || folder == "drum fills")
                    return true;
            }
            return false;
        }

        static bool shouldExcludeFromAtlas(const SampleRecord& rec)
        {
            return isExplicitDrumFile(rec.file)
                || rec.percussionState == PercussionState::Percussive;
        }

        static void evaluate(SampleRecord& rec)
        {
            rec.percussionState = PercussionState::NotEvaluated;
            rec.percussionConfidence = 0.0f;

            const auto& a = rec.acoustic;
            const auto& t = rec.tonal;
            if (!a.valid || !t.valid)
                return;

            const auto valid01 = [](float x) {
                return std::isfinite(x) && x >= 0.0f && x <= 1.0f;
            };
            if (!valid01(a.spectralFlatness) || !valid01(a.onsetActivity)
                || !valid01(a.sustainRatio) || !valid01(t.periodicity)
                || !valid01(t.pitchStability))
            {
                rec.percussionState = PercussionState::Uncertain;
                return;
            }

            // A strong pitch track, or one very clear short-lived periodic
            // window, is evidence for melodic material even when transient.
            // Never reject plucks just because their attack is strong.
            const bool stablePitch = t.validWindowCount >= 2
                                  && t.periodicity >= 0.68f
                                  && t.pitchStability >= 0.65f;
            // A single apparent pitch estimate is not enough to save a
            // very noisy, short and dry percussive hit.
            const bool exceptionallyDryHit =
                t.validWindowCount <= 1
                && a.spectralFlatness >= 0.48f
                && a.onsetActivity >= 0.11f
                && a.sustainRatio <= 0.17f;
            const bool clearPitchedTransient = t.validWindowCount >= 1
                                            && t.periodicity >= 0.84f
                                            && !exceptionallyDryHit;
            if (stablePitch || clearPitchedTransient)
            {
                rec.percussionState = PercussionState::Melodic;
                rec.percussionConfidence = juce::jlimit(
                    0.0f, 1.0f, t.periodicity * (stablePitch ? 0.95f : 0.86f));
                return;
            }

            // Strong independent evidence of a short, noisy transient.
            // Neither filename words nor short duration alone can reject
            // samples. Tonal/percussive mixtures remain uncertain.
            const bool noisy = a.spectralFlatness >= 0.48f;
            const bool impulsive = a.onsetActivity >= 0.13f;
            const bool decaying = a.sustainRatio <= 0.28f;
            const bool weakPitch = t.validWindowCount == 0
                                || t.periodicity < 0.35f;
            // Second, more sensitive gate for sharp, dry, moderately noisy
            // percussion. A strong tonal track above already protects
            // mallets, piano attacks and pitched synth plucks.
            const bool drySharpTransient =
                a.spectralFlatness >= 0.23f
                && a.onsetActivity >= 0.085f
                && a.sustainRatio <= 0.19f
                && (t.validWindowCount == 0 || t.periodicity < 0.43f);

            if ((noisy && impulsive && decaying && weakPitch)
                || drySharpTransient || exceptionallyDryHit)
            {
                rec.percussionState = PercussionState::Percussive;
                // This is rule evidence strength, not calibrated probability.
                rec.percussionConfidence = juce::jlimit(0.0f, 1.0f,
                    drySharpTransient ? 0.58f
                        : 0.55f + 0.20f * (a.spectralFlatness - 0.48f)
                          + 0.20f * (a.onsetActivity - 0.13f)
                          + 0.10f * (0.28f - a.sustainRatio));
                return;
            }

            rec.percussionState = PercussionState::Uncertain;
            rec.percussionConfidence = 0.0f;
        }
    };
}
