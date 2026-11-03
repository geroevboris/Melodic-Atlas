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

    // Режим транспонирования: чистый ресэмплинг без искажений или сохранение длительности
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

    // 8 основных кластеров на единой карте (6 явных инструментов + FX + Other)
    enum class ClusterGroup : int
    {
        Keys    = 0,
        Synth   = 1,
        Bass    = 2,
        Vocal   = 3,
        Strings = 4,
        Guitar  = 5,
        FX      = 6,
        Other   = 7
    };

    // Подгруппы для уточняющей фильтрации и категории внутри Other / FX
    enum SubFilterFlags : uint32_t
    {
        Sub_None       = 0,
        Sub_Clean      = 1 << 0,
        Sub_Noisy      = 1 << 1,
        Sub_Warm       = 1 << 2,
        Sub_Pluck      = 1 << 3,
        Sub_Sustain    = 1 << 4,
        Sub_RiserSweep = 1 << 5,
        Sub_Textural   = 1 << 6
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

        // Координаты на плотной 2D-карте (0.0 .. 1.0)
        float mapX = 0.5f;
        float mapY = 0.5f;

        ClusterGroup cluster = ClusterGroup::Synth;
        uint32_t subMask     = Sub_None;

        // 3 дискретные ступени оттенка по высоте тона: 0 = низкий, 1 = средний, 2 = высокий
        int toneShadeBucket  = 1;
    };

    struct PadHistoryEntry
    {
        int sampleIndex = -1;
        SampleRecord record;
        int semitoneOffset = 0;
        int centOffset = 0;
        float regionStart = 0.0f;
        float regionEnd = 1.0f;
    };

    struct PadSlotState
    {
        int sampleIndex = -1;
        bool chromaticActive = true;
        bool settingsViewOpen = false; // Открыты ли настройки под шестерёнкой ⚙ внутри пэда

        float volumeDb = -3.0f;        // -24 .. +6 dB
        float pan = 0.0f;              // -1.0 (L) .. +1.0 (R)
        int semitoneOffset = 0;        // -24 .. +24 полутонов
        int centOffset = 0;            // -100 .. +100 центов

        float regionStart = 0.0f;      // Начало выбранного участка (0.0 .. 0.95)
        float regionEnd   = 1.0f;      // Конец выбранного участка (0.05 .. 1.0)
        bool reverse = false;

        // Огибающая ADSR пэда
        float attackSec  = 0.008f;
        float decaySec   = 1.20f;
        float sustainLvl = 0.85f;
        float releaseSec = 0.35f;

        // Стек истории замен сэмплов для кнопки Undo (↶)
        std::vector<PadHistoryEntry> history;
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
                case ClusterGroup::Other:   return "OTHER";
                default:                    return "OTHER";
            }
        }

        // Спокойная взрослая палитра кластеров + ровно 3 градации оттенка по высоте тона (0: темнее, 1: база, 2: светлее)
        static juce::Colour getClusterColour(ClusterGroup cluster, int toneShadeBucket)
        {
            juce::Colour base;
            switch (cluster)
            {
                case ClusterGroup::Keys:    base = juce::Colour(0xffd99b52); break; // Тёплый янтарь
                case ClusterGroup::Synth:   base = juce::Colour(0xff4fa8be); break; // Стальной циан
                case ClusterGroup::Bass:    base = juce::Colour(0xff4a72a8); break; // Глубокий индиго
                case ClusterGroup::Vocal:   base = juce::Colour(0xffcf6d7e); break; // Приглушённый коралл
                case ClusterGroup::Strings: base = juce::Colour(0xff967bb6); break; // Мягкая лаванда
                case ClusterGroup::Guitar:  base = juce::Colour(0xff5da886); break; // Шалфейно-изумрудный
                case ClusterGroup::FX:      base = juce::Colour(0xffc47db8); break; // Аметистовый FX
                case ClusterGroup::Other:   base = juce::Colour(0xff8ca0b5); break; // Нейтральный серо-голубой сланец
                default:                    base = juce::Colour(0xff8ca0b5); break;
            }

            int bucket = juce::jlimit(0, 2, toneShadeBucket);
            if (bucket == 0)
                return base.darker(0.24f);   // Низкий тон
            if (bucket == 2)
                return base.brighter(0.26f); // Высокий тон
            return base;                     // Средний тон
        }

        // Расчёт транспонирования для предпрослушивания (реагирует на смену тоники и лада)
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

            // В режиме RootForce всегда приводим ноту сэмпла к выбранной тонике проекта
            if (mode == PitchMode::RootForce)
            {
                int diff = root - src;
                while (diff > 6)  diff -= 12;
                while (diff < -6) diff += 12;
                return diff + octShiftSemitones;
            }

            // В режиме ScaleSnap: транспонируем относительно базовой тоники A (9) + квантуем в выбранный лад,
            // чтобы переключение ноты KEY всегда реально транспонировало проигрываемый сэмпл/луп
            static const std::vector<int> scales[10] = {
                { 0, 2, 3, 5, 7, 8, 10 }, // Natural Minor
                { 0, 2, 4, 5, 7, 9, 11 }, // Major
                { 0, 2, 3, 5, 7, 8, 11 }, // Harmonic Minor
                { 0, 2, 3, 5, 7, 9, 11 }, // Melodic Minor
                { 0, 2, 3, 5, 7, 9, 10 }, // Dorian
                { 0, 1, 3, 5, 7, 8, 10 }, // Phrygian
                { 0, 2, 4, 6, 7, 9, 11 }, // Lydian
                { 0, 2, 4, 5, 7, 9, 10 }, // Mixolydian
                { 0, 3, 5, 7, 10 },       // Minor Pentatonic
                { 0, 2, 4, 7, 9 }         // Major Pentatonic
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
