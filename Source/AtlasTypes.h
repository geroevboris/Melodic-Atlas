#pragma once
#include <JuceHeader.h>
#include <array>
#include <vector>
#include <cmath>

namespace Atlas
{
    enum class PitchMode : int
    {
        ScaleSnap = 0,
        RootForce = 1,
        Bypass    = 2
    };

    enum class TransposeQuality : int
    {
        CleanRepitch = 0,
        TimeStretch  = 1
    };

    enum class ScaleType : int
    {
        NaturalMinor    = 0,
        Major           = 1,
        HarmonicMinor   = 2,
        MelodicMinor    = 3,
        Dorian          = 4,
        Phrygian        = 5,
        Lydian          = 6,
        Mixolydian      = 7,
        MinorPentatonic = 8,
        MajorPentatonic = 9
    };

    enum class BankMode : int
    {
        PadTrigger = 0,
        Chromatic  = 1
    };

    enum class PadPlaybackMode : int
    {
        OneShot = 0,
        Loop    = 1,
        Reverse = 2
    };

    enum class ClusterGroup : int
    {
        Keys    = 0,
        Synth   = 1,
        Bass    = 2,
        Vocal   = 3,
        Strings = 4,
        Guitar  = 5,
        FX      = 6,
        Other   = 7 // В UI отображается как фильтр MOODS
    };

    enum SubFilterFlags : uint32_t
    {
        Sub_None         = 0,
        Sub_Clean        = 1 << 0,
        Sub_Noisy        = 1 << 1,
        Sub_Warm         = 1 << 2,
        Sub_Pluck        = 1 << 3,
        Sub_Sustain      = 1 << 4,
        Sub_RiserSweep   = 1 << 5,
        Sub_Textural     = 1 << 6,
        Sub_Pad          = 1 << 7,
        Sub_Lead         = 1 << 8,
        Sub_ImpactHit    = 1 << 9,
        Sub_SubBass      = 1 << 10,
        Sub_BellMetallic = 1 << 11
    };

    // Character flags form an independent axis from instrument subtypes.
    // Low six bits intentionally match legacy MOODS selections so saved
    // project filters keep their meaning during migration.
    enum CharacterFlags : uint32_t
    {
        Character_Clean     = 1u << 0,
        Character_Noisy     = 1u << 1,
        Character_Warm      = 1u << 2,
        Character_Pluck     = 1u << 3,
        Character_Sustain   = 1u << 4,
        Character_Textural  = 1u << 6,
        Character_Dark      = 1u << 12,
        Character_Bright    = 1u << 13,
        Character_Rhythmic  = 1u << 14
    };

    struct SubgroupOption
    {
        juce::String fullName;
        juce::String shortName;
        uint32_t flagBit = Sub_None;
    };

    // Identity and percussion decisions are independent. Legacy records
    // start as NotEvaluated rather than inheriting guesses as trusted labels.
    enum class IdentityState : int
    {
        NotEvaluated = 0,
        Identified   = 1,
        Ambiguous    = 2,
        Unidentified = 3
    };

    enum class PercussionState : int
    {
        NotEvaluated = 0,
        Melodic      = 1,
        Percussive   = 2,
        Uncertain    = 3
    };

    // New measured acoustic descriptors; kept separate from legacy map
    // features until classification and map changes can be calibrated.
    struct AcousticFeatures
    {
        bool valid = false;
        float spectralCentroidHz = 0.0f;
        float spectralCentroidNorm = 0.0f;
        float spectralFlatness = 0.0f;
        float lowBandRatio = 0.0f;
        float midBandRatio = 0.0f;
        float highBandRatio = 0.0f;
        float onsetActivity = 0.0f;
        float sustainRatio = 0.0f;
    };

    struct TonalFeatures
    {
        bool valid = false;
        int validWindowCount = 0;
        float periodicity = 0.0f;
        float pitchStability = 0.0f;
        float pitchDeviationSemitones = 0.0f;
        float estimatedMidi = 0.0f;
    };

    // Passive evidence only: these classifications must NOT silently change
    // playback until an independently validated detector is implemented.
    enum class PitchStructure : int
    {
        NotEvaluated = 0,
        StableToneCandidate = 1,
        ComplexOrMoving = 2,
        NoReliableTone = 3
    };

    enum class TempoEvidence : int
    {
        NotEvaluated = 0,
        NoSyncEvidence = 1,
        TempoTaggedCandidate = 2
    };

    struct SampleRecord
    {
        juce::File file;
        juce::String name;
        // File identity for incremental Refresh; missing legacy values require
        // one fresh analysis rather than trusting potentially stale features.
        int64_t sourceBytes = -1;
        int64_t sourceModifiedMs = -1;
        bool isLoop = false;
        bool isPolyChord = false;
        double durationSec = 0.0;
        double detectedBpm = 120.0;
        int rootMidiNote = 60;
        int pitchClass = 0;
        float pitchConfidence = 0.0f;
        float normGainLinear = 1.0f;
        float spectralCentroidNorm = 0.5f;
        float pitchMobilityNorm = 0.5f;
        float spectralFlatness = 0.05f;
        float attackRatio = 0.5f;
        float tailDecayRatio = 0.2f;
        float harmonicRoughness = 0.1f;
        float lowBandRatio = 0.3f;
        float highBandRatio = 0.2f;
        float mapX = 0.5f;
        float mapY = 0.5f;
        // Optional normalized Pure CLAP / UMAP Balanced coordinates.
        // Never overwrite the legacy map; an incomplete ML projection is
        // not mixed with DSP geometry.
        // CLAP features stored locally for incremental native UMAP refresh.
        // Never read or write these arrays from the audio callback.
        bool clapEmbeddingValid = false;
        std::array<float, 512> clapEmbedding {};
        bool neuralMapValid = false;
        float neuralMapX = 0.5f;
        float neuralMapY = 0.5f;
        ClusterGroup cluster = ClusterGroup::Synth;
        // Legacy display/category fields remain intact until the new
        // classifier is validated; these fields do not change current UI.
        IdentityState identityState = IdentityState::NotEvaluated;
        ClusterGroup identityCluster = ClusterGroup::Other;
        PercussionState percussionState = PercussionState::NotEvaluated;
        float percussionConfidence = 0.0f;
        float identityConfidence = 0.0f;
        AcousticFeatures acoustic;
        TonalFeatures tonal;
        PitchStructure pitchStructure = PitchStructure::NotEvaluated;
        TempoEvidence tempoEvidence = TempoEvidence::NotEvaluated;
        // Evidence confidence, not a calibrated probability of correctness.
        float pitchEvidenceStrength = 0.0f;
        // A future beat tracker will supply a separately validated BPM.
        float tempoEvidenceStrength = 0.0f;
        uint32_t characterMask = 0;
        std::array<float, 9> characterScores {};
        uint32_t subMask = Sub_None;
        int toneShadeBucket = 1;
    };

    struct PadHistoryEntry
    {
        int sampleIndex = -1;
        SampleRecord record;
        int semitoneOffset = 0;
        int centOffset = 0;
        float volumeDb = -3.0f;
        float pan = 0.0f;
        float regionStart = 0.0f;
        float regionEnd = 1.0f;
        bool chromaticActive = true;
        PadPlaybackMode playbackMode = PadPlaybackMode::OneShot;
        bool padSyncBpm = true;
        float padSpeedMult = 1.0f;
        PitchMode padPitchMode = PitchMode::ScaleSnap;
        float attackSec = 0.005f;
        float decaySec = 1.2f;
        float sustainLvl = 1.0f;
        float releaseSec = 0.05f;
    };

    struct PadSlotState
    {
        int sampleIndex = -1;
        bool fileMissing = false;
        bool chromaticActive = true;
        bool settingsViewOpen = false;
        bool syncViewOpen = false;

        PadPlaybackMode playbackMode = PadPlaybackMode::OneShot;
        bool padSyncBpm = true;
        float padSpeedMult = 1.0f;
        PitchMode padPitchMode = PitchMode::ScaleSnap;

        float volumeDb = -3.0f;
        float pan = 0.0f;
        int semitoneOffset = 0;
        int centOffset = 0;
        float regionStart = 0.0f;
        float regionEnd = 1.0f;

        float attackSec = 0.005f;
        float decaySec = 1.20f;
        float sustainLvl = 1.0f;
        float releaseSec = 0.05f;

        std::vector<PadHistoryEntry> history;
    };

    struct ProjectSnapshot
    {
        std::array<PadSlotState, 12> pads;
        std::array<SampleRecord, 12> records;
        int primarySelectedPad = 0;
    };

    struct MusicMath
    {
        static juce::String getNoteName(int pitchClass)
        {
            static const char* names[12] = {
                "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
            };
            return names[((pitchClass % 12) + 12) % 12];
        }

        static juce::String formatPadNoteAndCents(int basePitchClass,
                                                  int semitoneOffset,
                                                  int centOffset)
        {
            int totalSemis = basePitchClass + semitoneOffset;
            int extraSemisFromCents = 0;
            int remCents = centOffset;

            if (remCents > 50)
            {
                extraSemisFromCents = 1;
                remCents -= 100;
            }
            else if (remCents < -50)
            {
                extraSemisFromCents = -1;
                remCents += 100;
            }

            juce::String noteStr = getNoteName(totalSemis + extraSemisFromCents);
            if (remCents == 0)
                return noteStr;

            return noteStr + (remCents > 0 ? " +" : " ") + juce::String(remCents);
        }

        static juce::String getScaleShortName(ScaleType st)
        {
            switch (st)
            {
                case ScaleType::NaturalMinor:    return "NAT MIN";
                case ScaleType::Major:           return "MAJOR";
                case ScaleType::HarmonicMinor:   return "HARM MIN";
                case ScaleType::MelodicMinor:    return "MEL MIN";
                case ScaleType::Dorian:          return "DORIAN";
                case ScaleType::Phrygian:        return "PHRYG";
                case ScaleType::Lydian:          return "LYDIAN";
                case ScaleType::Mixolydian:      return "MIXOLY";
                case ScaleType::MinorPentatonic: return "MIN PENT";
                case ScaleType::MajorPentatonic: return "MAJ PENT";
                default:                         return "NAT MIN";
            }
        }

        static juce::String getScaleFullName(ScaleType st)
        {
            switch (st)
            {
                case ScaleType::NaturalMinor:    return "Natural Minor (Aeolian)";
                case ScaleType::Major:           return "Major (Ionian)";
                case ScaleType::HarmonicMinor:   return "Harmonic Minor";
                case ScaleType::MelodicMinor:    return "Melodic Minor";
                case ScaleType::Dorian:          return "Dorian";
                case ScaleType::Phrygian:        return "Phrygian";
                case ScaleType::Lydian:          return "Lydian";
                case ScaleType::Mixolydian:      return "Mixolydian";
                case ScaleType::MinorPentatonic: return "Minor Pentatonic";
                case ScaleType::MajorPentatonic: return "Major Pentatonic";
                default:                         return "Natural Minor";
            }
        }

        static juce::String getClusterName(ClusterGroup cluster)
        {
            switch (cluster)
            {
                case ClusterGroup::Keys:    return "KEYS";
                case ClusterGroup::Synth:   return "SYNTH";
                case ClusterGroup::Bass:    return "BASS";
                case ClusterGroup::Vocal:   return "VOX";
                case ClusterGroup::Strings: return "STRINGS";
                case ClusterGroup::Guitar:  return "GUITAR";
                case ClusterGroup::FX:      return "FX";
                case ClusterGroup::Other:   return "MOODS";
                default:                    return "FX";
            }
        }

        static std::vector<SubgroupOption> getSubgroupsForCluster(ClusterGroup cluster)
        {
            switch (cluster)
            {
                case ClusterGroup::Keys:
                    return {
                        { "All Keys", "", Sub_None },
                        { "Warm / Electric", "WARM", Sub_Warm },
                        { "Clean / Acoustic", "CLEAN", Sub_Clean },
                        { "Pluck / Mallet", "PLUCK", Sub_Pluck },
                        { "Sustained", "SUST", Sub_Sustain }
                    };
                case ClusterGroup::Synth:
                    return {
                        { "All Synths", "", Sub_None },
                        { "Pads", "PAD", Sub_Pad },
                        { "Leads", "LEAD", Sub_Lead },
                        { "Plucks & Arps", "PLUCK", Sub_Pluck },
                        { "Warm Analog", "WARM", Sub_Warm },
                        { "Noisy / Textured", "NOISY", Sub_Noisy }
                    };
                case ClusterGroup::Bass:
                    return {
                        { "All Bass", "", Sub_None },
                        { "Deep Sub", "SUB", Sub_SubBass },
                        { "Warm / Round", "WARM", Sub_Warm },
                        { "Pluck / Short", "PLUCK", Sub_Pluck },
                        { "Reese / Sustain", "SUST", Sub_Sustain },
                        { "Distorted", "GRIT", Sub_Noisy }
                    };
                case ClusterGroup::Vocal:
                    return {
                        { "All Vocals", "", Sub_None },
                        { "Airy / Clean", "CLEAN", Sub_Clean },
                        { "Choir / Sustain", "CHOIR", Sub_Sustain },
                        { "Chops / Short", "CHOP", Sub_Pluck },
                        { "Textural", "TEXT", Sub_Textural }
                    };
                case ClusterGroup::Strings:
                    return {
                        { "All Strings & Winds", "", Sub_None },
                        { "Ensemble Sustain", "SUST", Sub_Sustain },
                        { "Pizzicato / Short", "PIZZ", Sub_Pluck },
                        { "Warm / Brass & Solo", "WARM", Sub_Warm },
                        { "Textural", "TEXT", Sub_Textural }
                    };
                case ClusterGroup::Guitar:
                    return {
                        { "All Guitars", "", Sub_None },
                        { "Acoustic / Pluck", "PLUCK", Sub_Pluck },
                        { "Warm / Clean", "WARM", Sub_Warm },
                        { "Swells", "SWELL", Sub_Sustain },
                        { "Textural", "TEXT", Sub_Textural }
                    };
                case ClusterGroup::FX:
                    return {
                        { "All FX", "", Sub_None },
                        { "Risers & Sweeps", "RISER", Sub_RiserSweep },
                        { "Impacts & Hits", "IMPACT", Sub_ImpactHit },
                        { "Bells & Metallic", "BELL", Sub_BellMetallic },
                        { "Noisy", "NOISE", Sub_Noisy }
                    };
                case ClusterGroup::Other:
                    return {
                        { "Clear Character", "", Sub_None },
                        { "Dark", "DARK", Character_Dark },
                        { "Bright", "BRIGHT", Character_Bright },
                        { "Warm", "WARM", Character_Warm },
                        { "Clean", "CLEAN", Character_Clean },
                        { "Noisy", "NOISY", Character_Noisy },
                        { "Plucky", "PLUCK", Character_Pluck },
                        { "Sustained", "SUST", Character_Sustain },
                        { "Textural", "TEXT", Character_Textural }
                    };
                default:
                    return {};
            }
        }

        static juce::String getSubgroupShortLabel(ClusterGroup cluster, uint32_t flagBit)
        {
            if (flagBit == Sub_None)
                return {};
            auto opts = getSubgroupsForCluster(cluster);
            for (const auto& o : opts)
            {
                if (o.flagBit == flagBit)
                    return o.shortName;
            }
            return "SUB";
        }

        static juce::Colour getClusterColour(ClusterGroup cluster, int toneShadeBucket)
        {
            juce::Colour base;
            switch (cluster)
            {
                case ClusterGroup::Keys:    base = juce::Colour(0xffd99b52); break;
                case ClusterGroup::Synth:   base = juce::Colour(0xff4fa8be); break;
                case ClusterGroup::Bass:    base = juce::Colour(0xff4a72a8); break;
                case ClusterGroup::Vocal:   base = juce::Colour(0xffcf6d7e); break;
                case ClusterGroup::Strings: base = juce::Colour(0xff967bb6); break;
                case ClusterGroup::Guitar:  base = juce::Colour(0xff5da886); break;
                case ClusterGroup::FX:      base = juce::Colour(0xffc47db8); break;
                case ClusterGroup::Other:   base = juce::Colour(0xff8ca0b5); break;
                default:                    base = juce::Colour(0xff8ca0b5); break;
            }
            int bucket = juce::jlimit(0, 2, toneShadeBucket);
            if (bucket == 0) return base.darker(0.24f);
            if (bucket == 2) return base.brighter(0.26f);
            return base;
        }

        static int calculatePitchShift(int samplePitchClass,
                                       int projectKeyRoot,
                                       ScaleType scaleType,
                                       PitchMode mode,
                                       int globalOctaveShift)
        {
            int octShiftSemitones = globalOctaveShift * 12;
            if (mode == PitchMode::Bypass)
                return octShiftSemitones;

            int src = ((samplePitchClass % 12) + 12) % 12;
            int root = ((projectKeyRoot % 12) + 12) % 12;

            if (mode == PitchMode::RootForce)
            {
                int diff = root - src;
                while (diff > 6)  diff -= 12;
                while (diff < -6) diff += 12;
                return diff + octShiftSemitones;
            }

            static const std::vector<int> scales[10] = {
                { 0, 2, 3, 5, 7, 8, 10 },
                { 0, 2, 4, 5, 7, 9, 11 },
                { 0, 2, 3, 5, 7, 8, 11 },
                { 0, 2, 3, 5, 7, 9, 11 },
                { 0, 2, 3, 5, 7, 9, 10 },
                { 0, 1, 3, 5, 7, 8, 10 },
                { 0, 2, 4, 6, 7, 9, 11 },
                { 0, 2, 4, 5, 7, 9, 10 },
                { 0, 3, 5, 7, 10 },
                { 0, 2, 4, 7, 9 }
            };

            const auto& scale = scales[juce::jlimit(0, 9, (int)scaleType)];
            int bestDiff = 99;
            for (int deg : scale)
            {
                int scaleDegreePitch = (root + deg) % 12;
                int diff = scaleDegreePitch - src;
                while (diff > 6)  diff -= 12;
                while (diff < -6) diff += 12;
                if (std::abs(diff) < std::abs(bestDiff))
                    bestDiff = diff;
            }
            return bestDiff + octShiftSemitones;
        }
    };
}
