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

    enum class FilterCategory : int
    {
        Timbre    = 0,
        Character = 1
    };

    enum class ColorMode : int
    {
        ByCluster    = 0,
        ByMusicalKey = 1
    };

    // 6 смысловых кластеров с собственной взрослой палитрой
    enum class ClusterGroup : int
    {
        DeepBass    = 0,
        Keys        = 1,
        GuitarPluck = 2,
        SynthLead   = 3,
        Vocal       = 4,
        StringsPad  = 5
    };

    enum TimbreFlags : uint32_t
    {
        Timbre_None    = 0,
        Timbre_Keys    = 1 << 0,
        Timbre_Guitar  = 1 << 1,
        Timbre_Vox     = 1 << 2,
        Timbre_Synth   = 1 << 3,
        Timbre_Strings = 1 << 4
    };

    enum CharacterFlags : uint32_t
    {
        Char_None    = 0,
        Char_Warm    = 1 << 0,
        Char_Deep    = 1 << 1,
        Char_Ethy    = 1 << 2,
        Char_Clean   = 1 << 3,
        Char_Dirty   = 1 << 4,
        Char_Pluck   = 1 << 5,
        Char_Sustain = 1 << 6
    };

    struct SampleRecord
    {
        juce::File file;
        juce::String name;
        bool isLoop = false;
        bool isPolyChord = false;

        double durationSec = 0.0;
        double detectedBpm = 120.0;
        int rootMidiNote = 60;
        int pitchClass = 0;
        float pitchConfidence = 0.0f;

        float normGainLinear = 1.0f;

        // Акустические метрики
        float spectralCentroidNorm = 0.5f;
        float pitchMobilityNorm    = 0.5f;
        float spectralFlatness     = 0.05f;
        float attackRatio          = 0.5f;
        float tailDecayRatio       = 0.2f;
        float harmonicRoughness    = 0.1f;
        float lowBandRatio         = 0.3f;
        float highBandRatio        = 0.2f;

        // Координаты на 2D-карте (0.0 .. 1.0)
        float mapX = 0.5f;
        float mapY = 0.5f;

        // Дополнительные координаты проекций (для совместимости)
        float timbreX = 0.5f;
        float timbreY = 0.5f;
        float charX   = 0.5f;
        float charY   = 0.5f;

        ClusterGroup cluster   = ClusterGroup::SynthLead;
        int timbreCluster      = 3;
        int charCluster        = 0;

        uint32_t timbreMask    = Timbre_None;
        uint32_t characterMask = Char_None;
    };

    struct PadSlotState
    {
        int sampleIndex = -1;
        bool chromaticActive = true;
        float volumeDb = -3.0f;
        float pan = 0.0f;
        int semitoneOffset = 0;
        bool reverse = false;

        float attackSec  = 0.015f;
        float decaySec   = 1.20f;
        float sustainLvl = 0.80f;
        float releaseSec = 0.45f;
    };

    struct MusicMath
    {
        static juce::String getNoteName(int pitchClass)
        {
            static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
            return names[((pitchClass % 12) + 12) % 12];
        }

        static juce::String getScaleShortName(ScaleType st)
        {
            switch (st)
            {
                case ScaleType::NaturalMinor:    return "[NAT MIN]";
                case ScaleType::Major:           return "[MAJOR]";
                case ScaleType::HarmonicMinor:   return "[HARM MIN]";
                case ScaleType::MelodicMinor:    return "[MEL MIN]";
                case ScaleType::Dorian:          return "[DORIAN]";
                case ScaleType::Phrygian:        return "[PHRYG]";
                case ScaleType::Lydian:          return "[LYDIAN]";
                case ScaleType::Mixolydian:      return "[MIXOLY]";
                case ScaleType::MinorPentatonic: return "[MIN PENT]";
                case ScaleType::MajorPentatonic: return "[MAJ PENT]";
                default:                         return "[NAT MIN]";
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

        static juce::Colour getClusterColour(ClusterGroup cluster, float brightnessNorm)
        {
            juce::Colour base;
            switch (cluster)
            {
                case ClusterGroup::DeepBass:    base = juce::Colour(0xff4f7cac); break;
                case ClusterGroup::Keys:        base = juce::Colour(0xffd99b52); break;
                case ClusterGroup::GuitarPluck: base = juce::Colour(0xff5da886); break;
                case ClusterGroup::SynthLead:   base = juce::Colour(0xff52a8bd); break;
                case ClusterGroup::Vocal:       base = juce::Colour(0xffc96d7e); break;
                case ClusterGroup::StringsPad:  base = juce::Colour(0xff967bb6); break;
                default:                        base = juce::Colour(0xff6b90b2); break;
            }
            float bShift = (brightnessNorm - 0.5f) * 0.22f;
            return (bShift >= 0.0f) ? base.brighter(bShift) : base.darker(-bShift);
        }

        static juce::Colour getKeyColour(int pitchClass, float brightnessNorm)
        {
            static const uint32_t keyPalette[12] = {
                0xff4f7cac, 0xff52a8bd, 0xff5da886, 0xff7fb069,
                0xffd99b52, 0xffd07a56, 0xffc96d7e, 0xffb56598,
                0xff967bb6, 0xff7475b8, 0xff5b85b8, 0xff4e97a8
            };
            juce::Colour base(keyPalette[((pitchClass % 12) + 12) % 12]);
            float bShift = (brightnessNorm - 0.5f) * 0.20f;
            return (bShift >= 0.0f) ? base.brighter(bShift) : base.darker(-bShift);
        }

        static juce::Colour getSampleColour(const SampleRecord& rec, FilterCategory, ColorMode colMode)
        {
            if (colMode == ColorMode::ByMusicalKey)
                return getKeyColour(rec.pitchClass, rec.spectralCentroidNorm);
            return getClusterColour(rec.cluster, rec.spectralCentroidNorm);
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

            int src  = ((samplePitchClass % 12) + 12) % 12;
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

            int sIdx = juce::jlimit(0, 9, (int)scaleType);
            const auto& scale = scales[sIdx];

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
