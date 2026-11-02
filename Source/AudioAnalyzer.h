#pragma once
#include "AtlasTypes.h"
#include <juce_dsp/juce_dsp.h>

namespace Atlas
{
    class AudioAnalyzer
    {
    public:
        static constexpr float kTargetMinus6dBLinear = 0.50118723f; // 10^(-6/20)
        static constexpr int kFftOrder = 11;                        // 2048 точек
        static constexpr int kFftSize  = 1 << kFftOrder;

        // Возвращает true, если сэмпл прошел фильтр (мелодический/текстурный), и заполняет outRecord
        static bool analyzeFile(const juce::File& file,
                                juce::AudioFormatManager& formatManager,
                                SampleRecord& outRecord)
        {
            std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
            if (reader == nullptr || reader->sampleRate <= 0 || reader->lengthInSamples < 2048)
                return false;

            const double duration = (double)reader->lengthInSamples / reader->sampleRate;
            // Отсекаем микро-клики (< 0.08 сек) и длинные треки (> 32 сек)
            if (duration < 0.08 || duration > 32.0)
                return false;

            // Читаем до первых 10 секунд в моно-буфер для быстрого DSP-анализа
            const int maxSamplesToRead = (int)std::min<juce::int64>(reader->lengthInSamples,
                                                                    (juce::int64)(reader->sampleRate * 10.0));
            juce::AudioBuffer<float> buffer((int)juce::jmin<unsigned int>(2, reader->numChannels), maxSamplesToRead);
            reader->read(&buffer, 0, maxSamplesToRead, 0, true, true);

            std::vector<float> mono(maxSamplesToRead, 0.0f);
            float maxPeak = 0.0f;
            const int numCh = buffer.getNumChannels();

            for (int ch = 0; ch < numCh; ++ch)
            {
                const float* chData = buffer.getReadPointer(ch);
                for (int i = 0; i < maxSamplesToRead; ++i)
                {
                    float absVal = std::abs(chData[i]);
                    if (absVal > maxPeak) maxPeak = absVal;
                    mono[i] += chData[i] / (float)numCh;
                }
            }

            // Отсекаем пустые/тихие файлы
            if (maxPeak < 0.002f)
                return false;

            // 1. Анализ огибающей (Transient Decay Test против ударных ваншотов)
            const int earlySamples = std::min(maxSamplesToRead, (int)(reader->sampleRate * 0.085)); // первые 85 мс
            double earlyEnergy = 0.0;
            double totalEnergy = 0.0;

            for (int i = 0; i < maxSamplesToRead; ++i)
            {
                double sq = (double)mono[i] * (double)mono[i];
                totalEnergy += sq;
                if (i < earlySamples)
                    earlyEnergy += sq;
            }

            float earlyEnergyRatio = (totalEnergy > 1e-9) ? (float)(earlyEnergy / totalEnergy) : 1.0f;

            // 2. Спектральный анализ по окнам (Spectral Flatness + Spectral Centroid)
            juce::dsp::FFT fft(kFftOrder);
            juce::dsp::WindowingFunction<float> window(kFftSize, juce::dsp::WindowingFunction<float>::hann);

            std::vector<float> fftData(kFftSize * 2, 0.0f);
            int numFrames = std::max(1, (maxSamplesToRead - kFftSize) / (kFftSize / 2));
            numFrames = std::min(numFrames, 24); // До 24 окон для реактивной скорости сканирования

            float sumFlatness = 0.0f;
            float sumCentroidHz = 0.0f;
            std::vector<int> framePitches;
            float sumPitchConfidence = 0.0f;
            int validFrames = 0;

            for (int f = 0; f < numFrames; ++f)
            {
                int startSample = f * (kFftSize / 2);
                if (startSample + kFftSize > maxSamplesToRead)
                    break;

                // Проверяем энергию окна (пропускаем тишину в хвосте)
                float frameRms = 0.0f;
                for (int i = 0; i < kFftSize; ++i)
                    frameRms += mono[startSample + i] * mono[startSample + i];
                frameRms = std::sqrt(frameRms / (float)kFftSize);
                if (frameRms < maxPeak * 0.05f)
                    continue;

                std::fill(fftData.begin(), fftData.end(), 0.0f);
                std::copy(mono.begin() + startSample, mono.begin() + startSample + kFftSize, fftData.begin());
                window.multiplyWithWindowingTable(fftData.data(), kFftSize);
                fft.performFrequencyOnlyForwardTransform(fftData.data());

                const int numBins = kFftSize / 2;
                double logSum = 0.0;
                double arithSum = 0.0;
                double weightedFreqSum = 0.0;
                double magSum = 0.0;

                for (int b = 1; b < numBins; ++b)
                {
                    double mag = (double)fftData[b] + 1e-7;
                    double power = mag * mag;
                    logSum += std::log(power);
                    arithSum += power;

                    double freqHz = (double)b * reader->sampleRate / (double)kFftSize;
                    weightedFreqSum += freqHz * mag;
                    magSum += mag;
                }

                double geomMean = std::exp(logSum / (double)(numBins - 1));
                double arithMean = arithSum / (double)(numBins - 1);
                float flatness = (float)(geomMean / (arithMean + 1e-12));
                float centroidHz = (float)(weightedFreqSum / (magSum + 1e-12));

                sumFlatness += flatness;
                sumCentroidHz += centroidHz;

                // Определение высоты тона в окне методом YIN-автокорреляции
                float conf = 0.0f;
                float f0 = detectPitchYIN(mono.data() + startSample, kFftSize, (float)reader->sampleRate, conf);
                if (f0 > 32.0f && conf > 0.45f)
                {
                    int midiNote = (int)std::round(69.0f + 12.0f * std::log2(f0 / 440.0f));
                    framePitches.push_back(midiNote);
                    sumPitchConfidence += conf;
                }

                validFrames++;
            }

            if (validFrames == 0)
                return false;

            float avgFlatness   = sumFlatness / (float)validFrames;
            float avgCentroidHz = sumCentroidHz / (float)validFrames;
            float avgConfidence = framePitches.empty() ? 0.0f : (sumPitchConfidence / (float)framePitches.size());

            // =========================================================================
            // DRUM REJECTION FILTER (Отсев бочек, снейров, хэтов и перкуссии)
            // =========================================================================
            juce::String lowerPath = file.getFullPathName().toLowerCase();
            if (lowerPath.contains("kick") || lowerPath.contains("snare") ||
                lowerPath.contains("hihat") || lowerPath.contains("hi-hat") ||
                lowerPath.contains("clap") || lowerPath.contains("cymbal"))
            {
                return false;
            }

            // 1. Отсев шумовой перкуссии (хэты, клэпы, шейкеры)
            if (avgFlatness > 0.42f && avgConfidence < 0.60f)
                return false;

            // 2. Отсев коротких транзиентных ударов без сустейна (бочки, томы, римшоты)
            if (duration < 0.75 && earlyEnergyRatio > 0.82f && avgConfidence < 0.72f)
                return false;

            // 3. Отсев атональных звуков без выраженного музыкального тона
            if (framePitches.empty() || avgConfidence < 0.52f)
                return false;

            // =========================================================================
            // ВЫЧИСЛЕНИЕ ОСНОВНОЙ НОТЫ И ДВИЖЕНИЯ МЕЛОДИИ
            // =========================================================================
            // Находим медианную ноту и разброс нот во времени (Pitch Mobility)
            std::array<int, 12> pitchClassHist { 0 };
            for (int n : framePitches)
                pitchClassHist[((n % 12) + 12) % 12]++;

            int dominantPitchClass = 0;
            int maxCount = -1;
            for (int pc = 0; pc < 12; ++pc)
            {
                if (pitchClassHist[pc] > maxCount)
                {
                    maxCount = pitchClassHist[pc];
                    dominantPitchClass = pc;
                }
            }

            std::sort(framePitches.begin(), framePitches.end());
            int medianMidi = framePitches[framePitches.size() / 2];

            // Подсчитываем, сколько разных нот звучало (оценка мелодического движения для оси X)
            int uniquePitchClasses = 0;
            for (int pc = 0; pc < 12; ++pc)
                if (pitchClassHist[pc] > 0) uniquePitchClasses++;

            float pitchMobility = juce::jlimit(0.0f, 1.0f, (float)(uniquePitchClasses - 1) / 6.0f);

            // Заполняем запись сэмпла
            outRecord.file = file;
            outRecord.name = file.getFileNameWithoutExtension();
            outRecord.durationSec = duration;
            outRecord.rootMidiNote = juce::jlimit(24, 96, medianMidi);
            outRecord.pitchClass = dominantPitchClass;
            outRecord.pitchConfidence = avgConfidence;
            outRecord.normGainLinear = juce::jlimit(0.05f, 8.0f, kTargetMinus6dBLinear / maxPeak);

            // Определяем Loop vs One-Shot
            bool nameSaysLoop = lowerPath.contains("loop") || lowerPath.contains("bpm") || lowerPath.contains("arp");
            outRecord.isLoop = (duration >= 2.2 && earlyEnergyRatio < 0.35f) || (nameSaysLoop && duration >= 1.5);
            outRecord.isPolyChord = lowerPath.contains("chord") || (avgFlatness > 0.18f && uniquePitchClasses >= 3 && !outRecord.isLoop);
            outRecord.detectedBpm = extractBpmFromName(outRecord.name, duration);

            // Нормализуем координаты для 2D-карты
            // Ось Y: логарифмическая яркость спектра (150 Гц .. 6500 Гц)
            float logCentroid = std::log2(juce::jlimit(150.0f, 6500.0f, avgCentroidHz) / 150.0f) / std::log2(6500.0f / 150.0f);
            outRecord.spectralCentroidNorm = juce::jlimit(0.05f, 0.95f, logCentroid);

            // Ось X: комбинация тональной подвижности и формы огибающей
            outRecord.pitchMobilityNorm = juce::jlimit(0.05f, 0.95f, 0.65f * pitchMobility + 0.35f * (1.0f - earlyEnergyRatio));
            outRecord.spectralFlatness = avgFlatness;
            outRecord.attackRatio = earlyEnergyRatio;

            outRecord.mapX = outRecord.pitchMobilityNorm;
            outRecord.mapY = outRecord.spectralCentroidNorm;

            // Заполняем теги Timbre и Character
            assignTags(lowerPath, outRecord);

            return true;
        }

        // Равномерное распределение точек на 2D-карте (Force-Directed Relaxation), чтобы точки не слипались
        static void relaxMapCoordinates(std::vector<SampleRecord>& samples)
        {
            const size_t N = samples.size();
            if (N < 2) return;

            const float minDist = 0.032f;
            for (int iter = 0; iter < 15; ++iter)
            {
                for (size_t i = 0; i < N; ++i)
                {
                    for (size_t j = i + 1; j < N; ++j)
                    {
                        if (samples[i].isLoop != samples[j].isLoop)
                            continue;

                        float dx = samples[j].mapX - samples[i].mapX;
                        float dy = samples[j].mapY - samples[i].mapY;
                        float distSq = dx * dx + dy * dy;

                        if (distSq < minDist * minDist && distSq > 1e-7f)
                        {
                            float dist = std::sqrt(distSq);
                            float push = 0.5f * (minDist - dist) / dist;
                            samples[i].mapX = juce::jlimit(0.04f, 0.96f, samples[i].mapX - dx * push);
                            samples[i].mapY = juce::jlimit(0.04f, 0.96f, samples[i].mapY - dy * push);
                            samples[j].mapX = juce::jlimit(0.04f, 0.96f, samples[j].mapX + dx * push);
                            samples[j].mapY = juce::jlimit(0.04f, 0.96f, samples[j].mapY + dy * push);
                        }
                    }
                }
            }
        }

    private:
        // Быстрый YIN-детектор основного тона
        static float detectPitchYIN(const float* buf, int size, float sampleRate, float& outConfidence)
        {
            const int minLag = std::max(4, (int)(sampleRate / 1800.0f));
            const int maxLag = std::min(size / 2, (int)(sampleRate / 45.0f));
            if (maxLag <= minLag) { outConfidence = 0.0f; return 0.0f; }

            std::vector<float> diff(maxLag + 1, 0.0f);
            std::vector<float> cmndf(maxLag + 1, 1.0f);

            const int windowSize = size / 2;
            for (int tau = 1; tau <= maxLag; ++tau)
            {
                float sum = 0.0f;
                for (int i = 0; i < windowSize; ++i)
                {
                    float d = buf[i] - buf[i + tau];
                    sum += d * d;
                }
                diff[tau] = sum;
            }

            float runningSum = 0.0f;
            for (int tau = 1; tau <= maxLag; ++tau)
            {
                runningSum += diff[tau];
                cmndf[tau] = (runningSum > 1e-9f) ? (diff[tau] * (float)tau / runningSum) : 1.0f;
            }

            int bestTau = -1;
            float minVal = 1.0f;
            for (int tau = minLag; tau <= maxLag; ++tau)
            {
                if (cmndf[tau] < minVal)
                {
                    minVal = cmndf[tau];
                    bestTau = tau;
                }
                if (cmndf[tau] < 0.18f && tau + 1 <= maxLag && cmndf[tau] < cmndf[tau + 1])
                {
                    bestTau = tau;
                    minVal = cmndf[tau];
                    break;
                }
            }

            if (bestTau <= 0) { outConfidence = 0.0f; return 0.0f; }
            outConfidence = juce::jlimit(0.0f, 1.0f, 1.0f - minVal);
            return sampleRate / (float)bestTau;
        }

        static double extractBpmFromName(const juce::String& name, double durationSec)
        {
            // Ищем число от 70 до 180 в имени файла
            for (int bpm = 70; bpm <= 180; ++bpm)
            {
                if (name.containsWholeWordIgnoreCase(juce::String(bpm)) ||
                    name.containsIgnoreCase(juce::String(bpm) + "bpm") ||
                    name.containsIgnoreCase("_" + juce::String(bpm)))
                {
                    return (double)bpm;
                }
            }
            // Если в названии нет BPM, оцениваем по длительности (предполагая 2 или 4 такта в 4/4)
            if (durationSec > 1.0)
            {
                double guessed = (8.0 * 60.0) / durationSec; // 2 такта = 8 долей
                while (guessed < 80.0)  guessed *= 2.0;
                while (guessed > 165.0) guessed *= 0.5;
                return std::round(guessed);
            }
            return 120.0;
        }

        static void assignTags(const juce::String& lowerPath, SampleRecord& rec)
        {
            // 1. Теги инструментов (TIMBRE) — парсинг пути + спектральные эвристики
            if (lowerPath.contains("key") || lowerPath.contains("piano") || lowerPath.contains("rhodes") || lowerPath.contains("organ"))
                rec.timbreMask |= Timbre_Keys;
            if (lowerPath.contains("guitar") || lowerPath.contains("gtr") || lowerPath.contains("acoustic"))
                rec.timbreMask |= Timbre_Guitar;
            if (lowerPath.contains("vox") || lowerPath.contains("vocal") || lowerPath.contains("voice") || lowerPath.contains("chant") || lowerPath.contains("choir"))
                rec.timbreMask |= Timbre_Vox;
            if (lowerPath.contains("string") || lowerPath.contains("violin") || lowerPath.contains("cello") || lowerPath.contains("orchestra"))
                rec.timbreMask |= Timbre_Strings;
            if (lowerPath.contains("synth") || lowerPath.contains("lead") || lowerPath.contains("pad") || lowerPath.contains("arp") || rec.timbreMask == Timbre_None)
                rec.timbreMask |= Timbre_Synth;

            // 2. Теги характера (CHARACTER) — чистая физика звука
            if (rec.spectralCentroidNorm < 0.42f)
                rec.characterMask |= Char_Warm;
            if (rec.rootMidiNote < 52 || rec.spectralCentroidNorm < 0.28f)
                rec.characterMask |= Char_Deep;
            if (rec.spectralCentroidNorm > 0.58f && rec.attackRatio < 0.30f)
                rec.characterMask |= Char_Ethy;
            if (rec.spectralFlatness < 0.08f && rec.pitchConfidence > 0.78f)
                rec.characterMask |= Char_Clean;
            if (rec.spectralFlatness >= 0.14f)
                rec.characterMask |= Char_Dirty;
            if (rec.attackRatio >= 0.42f)
                rec.characterMask |= Char_Pluck;
            else
                rec.characterMask |= Char_Sustain;
        }
    };
}
