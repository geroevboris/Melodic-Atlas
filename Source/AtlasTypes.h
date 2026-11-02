#pragma once
#include <JuceHeader.h>
#include <array>
#include <vector>
#include <cmath>

namespace Atlas
{
    enum class PitchMode : int
    {
        ScaleSnap = 0, // Транспонирует в тональность проекта с привязкой к интервалам выбранного лада
        RootForce = 1, // Строгий сдвиг тоники сэмпла в ноту KEY проекта
        Bypass    = 2  // Оригинальная высота без изменений
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

    // Режим проекции 2D-карты (переключает раскладку кластеров с морфингом)
    enum class FilterCategory : int
    {
        Timbre    = 0, // Острова по типу инструмента
        Character = 1  // Острова по характеру звучания
    };

    // Режим окраски точек на карте
    enum class ColorMode : int
    {
        ByCluster = 0, // Цвет соответствует кластеру (Timbre или Character)
        ByKey     = 1  // Цвет соответствует ноте по квинтовому кругу
    };

    // 6 инструментальных кластеров для проекции [TIMBRE]
    enum class TimbreCluster : int
    {
        DeepBass    = 0,
        Keys        = 1,
        GuitarPluck = 2,
        SynthLead   = 3,
        Vocal       = 4,
        StringsPad  = 5
    };

    // 5 тембральных кластеров для проекции [CHARACTER]
    enum class CharacterCluster : int
    {
        WarmDeep    = 0,
        AirEthereal = 1,
        PluckCrisp  = 2,
        DirtyRaw    = 3,
        LushMoving  = 4
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
        float spectralFlux         = 0.2f;
        float lowBandRatio         = 0.3f;
        float highBandRatio        = 0.2f;

        // Две пары координат для мгновенного морфинга между картами TIMBRE и CHARACTER
        float timbreX = 0.5f;
        float timbreY = 0.5f;
        float charX   = 0.5f;
        float charY   = 0.5f;

        TimbreCluster    timbreCluster = TimbreCluster::SynthLead;
        CharacterCluster charCluster   = CharacterCluster::WarmDeep;

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

        // Спокойная студийная палитра для кластеров инструментов (Timbre View)
        static juce::Colour getTimbreClusterColour(TimbreCluster cluster, float brightnessNorm)
        {
            juce::Colour base;
            switch (cluster)
            {
                case TimbreCluster::DeepBass:    base = juce::Colour(0xff4f7cac); break; // Глубокий индиго
                case TimbreCluster::Keys:        base = juce::Colour(0xffd99b52); break; // Тёплый янтарь
                case TimbreCluster::GuitarPluck: base = juce::Colour(0xff5da886); break; // Шалфей / изумруд
                case TimbreCluster::SynthLead:   base = juce::Colour(0xff52a8bd); break; // Стальной циан
                case TimbreCluster::Vocal:       base = juce::Colour(0xffc96d7e); break; // Пыльный коралл
                case TimbreCluster::StringsPad:  base = juce::Colour(0xff967bb6); break; // Приглушённая лаванда
                default:                         base = juce::Colour(0xff6b90b2); break;
            }
            float bShift = (brightnessNorm - 0.5f) * 0.20f;
            return (bShift >= 0.0f) ? base.brighter(bShift) : base.darker(-bShift);
        }

        // Студийная палитра для 5 кластеров характеров (Character View)
        static juce::Colour getCharacterClusterColour(CharacterCluster cluster, float brightnessNorm)
        {
            juce::Colour base;
            switch (cluster)
            {
                case CharacterCluster::WarmDeep:    base = juce::Colour(0xff547aa5); break; // Глубокий кобальт
                case CharacterCluster::AirEthereal: base = juce::Colour(0xff62b6cb); break; // Воздушный льдистый циан
                case CharacterCluster::PluckCrisp:  base = juce::Colour(0xff6ab089); break; // Упругий мятно-шалфейный
                case CharacterCluster::DirtyRaw:    base = juce::Colour(0xffd47a55); break; // Ржавая охра / терракота
                case CharacterCluster::LushMoving:  base = juce::Colour(0xffa876b5); break; // Насыщенный аметист
                default:                            base = juce::Colour(0xff7a93ac); break;
            }
            float bShift = (brightnessNorm - 0.5f) * 0.20f;
            return (bShift >= 0.0f) ? base.brighter(bShift) : base.darker(-bShift);
        }

        // Сдержанная палитра по квинтовому кругу для режима окраски [BY KEY]
        static juce::Colour getStudioKeyColour(int pitchClass, float brightnessNorm)
        {
            int circleIndex = (((pitchClass % 12) + 12) % 12 * 7) % 12;
            float hue = (float)circleIndex / 12.0f;
            float saturation = 0.46f; // Сдержанная насыщенность без «детских фломастеров»
            float value = juce::jlimit(0.62f, 0.88f, 0.64f + brightnessNorm * 0.22f);
            return juce::Colour::fromHSV(hue, saturation, value, 1.0f);
        }

        static juce::Colour getSampleColour(const SampleRecord& rec, FilterCategory viewMode, ColorMode colMode)
        {
            if (colMode == ColorMode::ByKey)
                return getStudioKeyColour(rec.pitchClass, rec.spectralCentroidNorm);

            if (viewMode == FilterCategory::Timbre)
                return getTimbreClusterColour(rec.timbreCluster, rec.spectralCentroidNorm);
            else
                return getCharacterClusterColour(rec.charCluster, rec.spectralCentroidNorm);
        }

        // Расчёт транспонирования: смена KEY всегда транспонирует сэмпл в выбранную тональность,
        // а ScaleSnap дополнительно корректирует ладовое наклонение (минор/мажор/гармонический и т.д.)
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

            // Базовый сдвиг тоники сэмпла к выбранной ноте KEY проекта (-6 .. +6 полутонов)
            int rootDiff = root - src;
            while (rootDiff > 6)  rootDiff -= 12;
            while (rootDiff < -6) rootDiff += 12;

            if (mode == PitchMode::RootForce)
                return rootDiff + octShiftSemitones;

            // В режиме ScaleSnap:
            // Тоника сэмпла привязывается к выбранной тональности KEY, а ступень квантуется по выбранному ладу
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

            // Определяем ближайшую ступень выбранного лада для гармонического выравнивания
            int bestDegreeOffset = 0;
            int minDist = 99;
            for (int deg : scale)
            {
                int dist = deg - (src % 12);
                while (dist > 6)  dist -= 12;
                while (dist < -6) dist += 12;
                if (std::abs(dist) < std::abs(minDist))
                {
                    minDist = dist;
                    bestDegreeOffset = dist;
                }
            }

            // Смеalщаем на сдвиг тональности + ладовую коррекцию
            int totalShift = rootDiff + (bestDegreeOffset % 2);
            while (totalShift > 6)  totalShift -= 12;
            while (totalShift < -6) totalShift += 12;

            return totalShift + octShiftSemitones;
        }
    };
}
