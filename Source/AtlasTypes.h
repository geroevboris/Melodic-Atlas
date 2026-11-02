#pragma once
#include <JuceHeader.h>
#include <array>
#include <vector>
#include <cmath>

namespace Atlas
{
    // Режимы работы авто-питча
    enum class PitchMode : int
    {
        ScaleSnap = 0, // Подтяжка к ближайшей ступени выбранного лада (±1..2 пт)
        RootForce = 1, // Принудительная подтяжка всех звуков в тонику проекта
        Bypass    = 2  // Оригинальная высота тона
    };

    // Режимы работы 12-слотового банка
    enum class BankMode : int
    {
        PadTrigger = 0, // Клавиши C1..B1 запускают слоты 1..12
        Chromatic  = 1  // Клавиатура играет полифонически активными (●) слотами
    };

    // Режимы фильтрации на панели
    enum class FilterCategory : int
    {
        Timbre    = 0,
        Character = 1
    };

    // Теги инструментов (Timbre)
    enum TimbreFlags : uint32_t
    {
        Timbre_None    = 0,
        Timbre_Keys    = 1 << 0,
        Timbre_Guitar  = 1 << 1,
        Timbre_Vox     = 1 << 2,
        Timbre_Synth   = 1 << 3,
        Timbre_Strings = 1 << 4
    };

    // Теги характера (Character — определяются спектрально и по огибающей)
    enum CharacterFlags : uint32_t
    {
        Char_None    = 0,
        Char_Warm    = 1 << 0, // Низкий спектральный центроид
        Char_Deep    = 1 << 1, // Выраженный низкочастотный фундамент
        Char_Ethy    = 1 << 2, // Воздушный верх + плавная огибающая
        Char_Clean   = 1 << 3, // Высокая гармоническая чистота (низкий уровень шума)
        Char_Dirty   = 1 << 4, // Насыщенный гармониками / сатурированный спектр
        Char_Pluck   = 1 << 5, // Быстрая атака и выраженный спад
        Char_Sustain = 1 << 6  // Длительное ровное удержание энергии
    };

    // Паспорт отсканированного сэмпла
    struct SampleRecord
    {
        juce::File file;
        juce::String name;
        bool isLoop = false;
        bool isPolyChord = false;      // Определен ли внутри сэмпла готовый аккорд

        double durationSec = 0.0;
        double detectedBpm = 120.0;
        int rootMidiNote = 60;         // Базовая нота (например, 60 = C3, 69 = A3)
        int pitchClass = 0;            // 0 = C, 1 = C#, ..., 9 = A, ..., 11 = B
        float pitchConfidence = 0.0f;

        float normGainLinear = 1.0f;   // Коэффициент для нормализации под -6.0 dB True Peak

        // Акустические дескрипторы (0.0 .. 1.0)
        float spectralCentroidNorm = 0.5f; // Ось Y: Тембр (Темный -> Яркий)
        float pitchMobilityNorm    = 0.5f; // Ось X: Движение (Статичная нота -> Подвижная фраза)
        float spectralFlatness     = 0.1f;
        float attackRatio          = 0.5f;

        // Итоговые координаты на 2D-карте (0.0 .. 1.0)
        float mapX = 0.5f;
        float mapY = 0.5f;

        uint32_t timbreMask    = Timbre_None;
        uint32_t characterMask = Char_None;
    };

    // Состояние одного из 12 пэдов
    struct PadSlotState
    {
        int sampleIndex = -1;          // Индекс в базе (-1 = пустой слот)
        bool chromaticActive = true;   // ● (true) или ○ (false) в режиме CHROMATIC
        float volumeDb = 0.0f;         // Индивидуальная громкость (-24 .. +6 dB)
        float pan = 0.0f;              // Панорама (-1.0 .. +1.0)
        int semitoneOffset = 0;        // Ручной сдвиг (-24 .. +24 пт)
        bool reverse = false;          // Реверс сэмпла
    };

    // Утилиты музыкальной теории и цвета
    struct MusicMath
    {
        static juce::String getNoteName(int pitchClass)
        {
            static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
            return names[((pitchClass % 12) + 12) % 12];
        }

        // Цвет точки по квинтовому кругу + яркость от спектрального центроида
        static juce::Colour getKeyColour(int pitchClass, float brightnessNorm)
        {
            // Позиция в квинтовом круге: (pitchClass * 7) % 12
            int circleIndex = ((pitchClass % 12) * 7) % 12;
            float hue = (float)circleIndex / 12.0f;
            float saturation = 0.68f;
            float value = juce::jlimit(0.55f, 0.98f, 0.55f + brightnessNorm * 0.43f);
            return juce::Colour::fromHSV(hue, saturation, value, 1.0f);
        }

        // Расчет сдвига в полутонах для Scale Snap и Root Force
        static int calculatePitchShift(int samplePitchClass,
                                       int projectKeyRoot,
                                       bool isMinor,
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

            // Режим ScaleSnap: ищем ближайшую ступень гаммы (0, ±1, ±2 полутона)
            static const int majorScale[7] = { 0, 2, 4, 5, 7, 9, 11 };
            static const int minorScale[7] = { 0, 2, 3, 5, 7, 8, 10 };
            const int* scale = isMinor ? minorScale : majorScale;

            int bestDiff = 99;
            for (int i = 0; i < 7; ++i)
            {
                int scaleDegreePitch = (root + scale[i]) % 12;
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
