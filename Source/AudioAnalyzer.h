#pragma once
#include "AtlasTypes.h"
#include <juce_dsp/juce_dsp.h>

namespace Atlas
{
    class AudioAnalyzer
    {
    public:
        static constexpr float kTargetMinus6dBLinear = 0.50118723f;
        static constexpr int kFftOrder = 11;
        static constexpr int kFftSize  = 1 << kFftOrder;

        static bool analyzeFile(const juce::File& file,
                                juce::AudioFormatManager& formatManager,
                                SampleRecord& outRecord)
        {
            std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
            if (reader == nullptr || reader->sampleRate <= 0 || reader->lengthInSamples < 2048)
                return false;

            const double duration = (double)reader->lengthInSamples / reader->sampleRate;
            if (duration < 0.08 || duration > 36.0)
                return false;

            juce::String lowerName = file.getFileNameWithoutExtension().toLowerCase();
            juce::String lowerPath = file.getFullPathName().toLowerCase();

            // Отсекаем явные ударные по названию файла/папки
            if (lowerName.contains("kick") || lowerName.contains("snare") ||
                lowerName.contains("hihat") || lowerName.contains("hi-hat") ||
                lowerName.contains("clap") || lowerName.contains("cymbal") ||
                lowerName.contains("hat_") || lowerName.contains("_hat") ||
                lowerName.contains("rimshot") || lowerName.contains("tambourine") ||
                lowerName.contains("shaker"))
            {
                return false;
            }

            const int maxSamplesToRead = (int)std::min<juce::int64>(reader->lengthInSamples,
                                                                    (juce::int64)(reader->sampleRate * 12.0));
            juce::AudioBuffer<float> buffer((int)juce::jmin<unsigned int>(2, reader->numChannels), maxSamplesToRead);
            reader->read(&buffer, 0, maxSamplesToRead, 0, true, true);

            std::vector<float> mono((size_t)maxSamplesToRead, 0.0f);
            float maxPeak = 0.0f;
            const int numCh = buffer.getNumChannels();

            for (int ch = 0; ch < numCh; ++ch)
            {
                const float* chData = buffer.getReadPointer(ch);
                for (int i = 0; i < maxSamplesToRead; ++i)
                {
                    float absVal = std::abs(chData[i]);
                    if (absVal > maxPeak) maxPeak = absVal;
                    mono[(size_t)i] += chData[i] / (float)numCh;
                }
            }

            if (maxPeak < 0.002f)
                return false;

            // 1. Сегментный анализ огибающей (16 сегментов) для точного отделения One-Shot от Loop
            const int numSegs = 16;
            const int segLen = std::max(1, maxSamplesToRead / numSegs);
            std::array<float, numSegs> segRms { 0.0f };
            float maxSegRms = 0.0001f;

            for (int s = 0; s < numSegs; ++s)
            {
                int start = s * segLen;
                int end = std::min(maxSamplesToRead, start + segLen);
                double sumSq = 0.0;
                for (int i = start; i < end; ++i)
                    sumSq += (double)mono[(size_t)i] * (double)mono[(size_t)i];
                segRms[(size_t)s] = std::sqrt((float)(sumSq / (double)std::max(1, end - start)));
                if (segRms[(size_t)s] > maxSegRms)
                    maxSegRms = segRms[(size_t)s];
            }

            // Отношение громкости хвоста (последние 3 сегмента) к пиковому сегменту
            float tailRms = (segRms[13] + segRms[14] + segRms[15]) / 3.0f;
            float tailDecayRatio = tailRms / maxSegRms;

            // Считаем количество повторных ритмических всплесков (онсетов) внутри файла
            int rhythmicOnsets = 0;
            for (int s = 2; s < numSegs; ++s)
            {
                if (segRms[(size_t)s] > segRms[(size_t)(s - 1)] * 1.42f && segRms[(size_t)s] > maxSegRms * 0.28f)
                    rhythmicOnsets++;
            }

            // Энергия первых 90 мс и расчёт ZCR / Crest Factor
            const int earlySamples = std::min(maxSamplesToRead, (int)(reader->sampleRate * 0.090));
            double earlyEnergy = 0.0;
            double totalEnergy = 0.0;
            int clippedSamples = 0;

            for (int i = 0; i < maxSamplesToRead; ++i)
            {
                float v = mono[(size_t)i];
                double sq = (double)v * (double)v;
                totalEnergy += sq;
                if (i < earlySamples) earlyEnergy += sq;
                if (std::abs(v) > maxPeak * 0.92f) clippedSamples++;
            }

            float earlyEnergyRatio = (totalEnergy > 1e-9) ? (float)(earlyEnergy / totalEnergy) : 1.0f;
            float globalRms = std::sqrt((float)(totalEnergy / (double)maxSamplesToRead));
            float crestFactor = maxPeak / (globalRms + 1e-6f);
            float clipDensity = (float)clippedSamples / (float)maxSamplesToRead;

            // 2. Спектральный анализ по окнам
            juce::dsp::FFT fft(kFftOrder);
            juce::dsp::WindowingFunction<float> window(kFftSize, juce::dsp::WindowingFunction<float>::hann);

            std::vector<float> fftData(kFftSize * 2, 0.0f);
            int numFrames = std::min(20, std::max(1, (maxSamplesToRead - kFftSize) / (kFftSize / 2)));

            float sumFlatness = 0.0f;
            float sumCentroidHz = 0.0f;
            float sumLowBand = 0.0f;
            float sumWarmBand = 0.0f;
            float sumHighBand = 0.0f;
            float sumRoughness = 0.0f;

            std::vector<int> framePitches;
            float sumPitchConfidence = 0.0f;
            int validFrames = 0;

            for (int f = 0; f < numFrames; ++f)
            {
                int startSample = f * (kFftSize / 2);
                if (startSample + kFftSize > maxSamplesToRead)
                    break;

                float frameRms = 0.0f;
                for (int i = 0; i < kFftSize; ++i)
                    frameRms += mono[(size_t)(startSample + i)] * mono[(size_t)(startSample + i)];
                frameRms = std::sqrt(frameRms / (float)kFftSize);
                if (frameRms < maxPeak * 0.05f)
                    continue;

                std::fill(fftData.begin(), fftData.end(), 0.0f);
                std::copy(mono.begin() + startSample, mono.begin() + startSample + kFftSize, fftData.begin());
                window.multiplyWithWindowingTable(fftData.data(), kFftSize);
                fft.performFrequencyOnlyForwardTransform(fftData.data());

                const int numBins = kFftSize / 2;
                double logSum = 0.0, arithSum = 0.0, weightedFreqSum = 0.0, magSum = 0.0;
                double lowE = 0.0, warmE = 0.0, highE = 0.0, hfJaggedness = 0.0;

                for (int b = 1; b < numBins; ++b)
                {
                    double mag = (double)fftData[(size_t)b] + 1e-7;
                    double power = mag * mag;
                    logSum += std::log(power);
                    arithSum += power;

                    double freqHz = (double)b * reader->sampleRate / (double)kFftSize;
                    weightedFreqSum += freqHz * mag;
                    magSum += mag;

                    if (freqHz <= 240.0) lowE += power;
                    else if (freqHz <= 950.0) warmE += power;
                    else if (freqHz >= 2400.0)
                    {
                        highE += power;
                        if (b > 1)
                            hfJaggedness += std::abs(fftData[(size_t)b] - fftData[(size_t)(b - 1)]);
                    }
                }

                double geomMean = std::exp(logSum / (double)(numBins - 1));
                double arithMean = arithSum / (double)(numBins - 1);
                float flatness = (float)(geomMean / (arithMean + 1e-12));
                float centroidHz = (float)(weightedFreqSum / (magSum + 1e-12));

                sumFlatness += flatness;
                sumCentroidHz += centroidHz;
                sumLowBand += (float)(lowE / (arithSum + 1e-12));
                sumWarmBand += (float)(warmE / (arithSum + 1e-12));
                sumHighBand += (float)(highE / (arithSum + 1e-12));
                sumRoughness += (float)(hfJaggedness / (magSum + 1e-12));

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
            float avgLowBand    = sumLowBand / (float)validFrames;
            float avgWarmBand   = sumWarmBand / (float)validFrames;
            float avgHighBand   = sumHighBand / (float)validFrames;
            float avgRoughness  = sumRoughness / (float)validFrames;
            float avgConfidence = framePitches.empty() ? 0.0f : (sumPitchConfidence / (float)framePitches.size());

            // Отсев перкуссии
            if (avgFlatness > 0.40f && avgConfidence < 0.62f)
                return false;
            if (duration < 0.75 && earlyEnergyRatio > 0.80f && avgConfidence < 0.74f)
                return false;
            if (framePitches.empty() || avgConfidence < 0.52f)
                return false;

            // Основная нота
            std::array<int, 12> pitchClassHist { 0 };
            for (int n : framePitches)
                pitchClassHist[(size_t)(((n % 12) + 12) % 12)]++;

            int dominantPitchClass = 0, maxCount = -1;
            for (int pc = 0; pc < 12; ++pc)
            {
                if (pitchClassHist[(size_t)pc] > maxCount)
                {
                    maxCount = pitchClassHist[(size_t)pc];
                    dominantPitchClass = pc;
                }
            }

            std::sort(framePitches.begin(), framePitches.end());
            int medianMidi = framePitches[framePitches.size() / 2];

            int uniquePitchClasses = 0;
            for (int pc = 0; pc < 12; ++pc)
                if (pitchClassHist[(size_t)pc] > 0) uniquePitchClasses++;

            float pitchMobility = juce::jlimit(0.0f, 1.0f, (float)(uniquePitchClasses - 1) / 5.0f);

            // =========================================================================
            // ТОЧНОЕ РАЗДЕЛЕНИЕ ONE-SHOTS vs LOOPS
            // =========================================================================
            bool explicitOneShot = lowerPath.contains("oneshot") || lowerPath.contains("one-shot") ||
                                   lowerPath.contains("one_shot") || lowerPath.contains("1shot") ||
                                   lowerName.contains("shot") || lowerName.contains("stab") ||
                                   lowerName.contains("hit") || lowerName.contains("pluck");

            bool explicitLoop = lowerPath.contains("loop") || lowerName.contains("_lp") ||
                                lowerName.contains("arp") || lowerName.contains("phrase") ||
                                lowerName.contains("motif") || lowerName.contains("riff") ||
                                lowerName.contains("prog") || lowerName.contains("seq") ||
                                hasExplicitBpmTag(lowerName);

            bool isDetectedLoop = false;
            if (duration < 1.40 || (explicitOneShot && !explicitLoop))
            {
                isDetectedLoop = false; // Короткие звуки или явные ваншоты
            }
            else if (explicitLoop && duration >= 1.45)
            {
                isDetectedLoop = true;  // Явные лупы по метаданным
            }
            else if (tailDecayRatio < 0.34f && rhythmicOnsets <= 1)
            {
                // Длинный звук, который плавно затухает к концу без ритмических повторов — это сустейновый One-Shot (пэд/аккорд/нота)
                isDetectedLoop = false;
            }
            else if (duration >= 1.85 && tailDecayRatio >= 0.42f && (rhythmicOnsets >= 2 || uniquePitchClasses >= 3))
            {
                // Длинный звук с неослабевающим концом и ритмическим/мелодическим развитием — это Loop
                isDetectedLoop = true;
            }

            outRecord.file = file;
            outRecord.name = file.getFileNameWithoutExtension();
            outRecord.durationSec = duration;
            outRecord.rootMidiNote = juce::jlimit(24, 96, medianMidi);
            outRecord.pitchClass = dominantPitchClass;
            outRecord.pitchConfidence = avgConfidence;
            outRecord.normGainLinear = juce::jlimit(0.05f, 6.0f, kTargetMinus6dBLinear / maxPeak);

            outRecord.isLoop = isDetectedLoop;
            outRecord.isPolyChord = lowerName.contains("chord") || (avgFlatness > 0.16f && uniquePitchClasses >= 3 && !outRecord.isLoop);
            outRecord.detectedBpm = extractBpmFromName(outRecord.name, duration);

            float logCentroid = std::log2(juce::jlimit(120.0f, 6500.0f, avgCentroidHz) / 120.0f) / std::log2(6500.0f / 120.0f);
            outRecord.spectralCentroidNorm = juce::jlimit(0.05f, 0.95f, logCentroid);
            outRecord.pitchMobilityNorm    = juce::jlimit(0.05f, 0.95f, 0.60f * pitchMobility + 0.40f * (1.0f - earlyEnergyRatio));
            outRecord.spectralFlatness     = avgFlatness;
            outRecord.attackRatio          = earlyEnergyRatio;
            outRecord.tailDecayRatio       = tailDecayRatio;
            outRecord.lowBandRatio         = avgLowBand;
            outRecord.highBandRatio        = avgHighBand;

            // Комплексный индекс сатурации/грязи (Roughness + Clipping + сжатый динамический диапазон)
            float compressionScore = juce::jlimit(0.0f, 1.0f, (4.5f - crestFactor) / 3.5f);
            outRecord.harmonicRoughness = juce::jlimit(0.0f, 1.0f, avgRoughness * 1.4f + clipDensity * 4.5f + compressionScore * 0.25f + avgWarmBand * 0.0f);

            assignAccurateTagsAndCluster(lowerPath, lowerName, outRecord);
            return true;
        }

        // Кластерная раскладка в стиле XLN XO: разделяет звуки на аккуратные острова-кластеры
        static void relaxMapCoordinates(std::vector<SampleRecord>& samples)
        {
            if (samples.empty()) return;

            // 1. Калибруем порог Dirty / Clean относительно распределения реальной папки
            std::vector<float> roughnessVals;
            roughnessVals.reserve(samples.size());
            for (const auto& s : samples)
                roughnessVals.push_back(s.harmonicRoughness);
            std::sort(roughnessVals.begin(), roughnessVals.end());
            float dirtyPercentileThreshold = roughnessVals[roughnessVals.size() * 76 / 100]; // Топ 24% самых насыщенных/грязных

            for (auto& s : samples)
            {
                juce::String ln = s.name.toLowerCase();
                bool hasDirtyWord = ln.contains("dirty") || ln.contains("dist") || ln.contains("sat") ||
                                    ln.contains("fuzz") || ln.contains("grit") || ln.contains("lofi") ||
                                    ln.contains("lo-fi") || ln.contains("bit") || ln.contains("crush") ||
                                    ln.contains("drive") || ln.contains("overdrive") || ln.contains("acid");

                s.characterMask &= ~(Char_Dirty | Char_Clean);
                if (hasDirtyWord || (s.harmonicRoughness >= std::max(0.32f, dirtyPercentileThreshold) && s.pitchConfidence < 0.88f))
                    s.characterMask |= Char_Dirty;
                else if (s.pitchConfidence >= 0.72f && s.harmonicRoughness < dirtyPercentileThreshold * 0.92f)
                    s.characterMask |= Char_Clean;
            }

            // 2. Центры 6 смысловых кластеров на 2D-карте (с чистыми промежутками между островами)
            struct ClusterCenter { float cx; float cy; float rx; float ry; };
            static const ClusterCenter centers[6] = {
                { 0.18f, 0.25f, 0.12f, 0.15f }, // 0: Deep / Bass (Индиго)
                { 0.50f, 0.26f, 0.13f, 0.15f }, // 1: Keys / Rhodes (Янтарь)
                { 0.82f, 0.26f, 0.12f, 0.15f }, // 2: Guitar / Pluck (Шалфей)
                { 0.20f, 0.73f, 0.13f, 0.16f }, // 3: Synth / Lead (Циан)
                { 0.52f, 0.74f, 0.13f, 0.15f }, // 4: Vocal / Choir (Коралл)
                { 0.82f, 0.73f, 0.12f, 0.16f }  // 5: Strings / Pad (Лаванда)
            };

            // Раскладываем One-Shots и Loops по своим кластерам методом золотого спирального распределения (Фибоначчи)
            // с сортировкой по высоте и яркости — это даёт идеальные созвездия без налипания на края!
            for (int loopPass = 0; loopPass < 2; ++loopPass)
            {
                bool targetLoop = (loopPass == 1);
                for (int c = 0; c < 6; ++c)
                {
                    std::vector<size_t> indices;
                    for (size_t i = 0; i < samples.size(); ++i)
                    {
                        if (samples[i].isLoop == targetLoop && (int)samples[i].cluster == c)
                            indices.push_back(i);
                    }

                    if (indices.empty()) continue;

                    // Сортируем внутри кластера по спектральной яркости и тональности для плавного градиента соседей
                    std::sort(indices.begin(), indices.end(), [&](size_t a, size_t b)
                    {
                        return (samples[a].spectralCentroidNorm * 2.0f + (float)samples[a].pitchClass * 0.05f)
                             < (samples[b].spectralCentroidNorm * 2.0f + (float)samples[b].pitchClass * 0.05f);
                    });

                    const auto& cc = centers[c];
                    const size_t count = indices.size();
                    const float goldenAngle = 2.39996323f;

                    for (size_t k = 0; k < count; ++k)
                    {
                        auto& rec = samples[indices[k]];
                        float normIdx = ((float)k + 0.5f) / (float)count;
                        float r = std::sqrt(normIdx);
                        float theta = (float)k * goldenAngle + (rec.pitchMobilityNorm - 0.5f) * 0.8f;

                        // Комбинируем спираль Фибоначчи с акустическими координатами сэмпла
                        float localX = 0.72f * (r * std::cos(theta)) + 0.28f * (rec.pitchMobilityNorm * 2.0f - 1.0f);
                        float localY = 0.72f * (r * std::sin(theta)) + 0.28f * (rec.spectralCentroidNorm * 2.0f - 1.0f);

                        rec.mapX = juce::jlimit(0.05f, 0.95f, cc.cx + localX * cc.rx);
                        rec.mapY = juce::jlimit(0.06f, 0.94f, cc.cy + localY * cc.ry);
                    }
                }
            }
        }

    private:
        static bool hasExplicitBpmTag(const juce::String& lowerName)
        {
            if (lowerName.contains("bpm")) return true;
            for (int bpm = 70; bpm <= 175; ++bpm)
            {
                juce::String bStr(bpm);
                if (lowerName.contains("_" + bStr + "_") || lowerName.endsWith("_" + bStr))
                    return true;
            }
            return false;
        }

        static float detectPitchYIN(const float* buf, int size, float sampleRate, float& outConfidence)
        {
            const int minLag = std::max(4, (int)(sampleRate / 1800.0f));
            const int maxLag = std::min(size / 2, (int)(sampleRate / 45.0f));
            if (maxLag <= minLag) { outConfidence = 0.0f; return 0.0f; }

            std::vector<float> diff((size_t)(maxLag + 1), 0.0f);
            std::vector<float> cmndf((size_t)(maxLag + 1), 1.0f);

            const int windowSize = size / 2;
            for (int tau = 1; tau <= maxLag; ++tau)
            {
                float sum = 0.0f;
                for (int i = 0; i < windowSize; ++i)
                {
                    float d = buf[i] - buf[i + tau];
                    sum += d * d;
                }
                diff[(size_t)tau] = sum;
            }

            float runningSum = 0.0f;
            for (int tau = 1; tau <= maxLag; ++tau)
            {
                runningSum += diff[(size_t)tau];
                cmndf[(size_t)tau] = (runningSum > 1e-9f) ? (diff[(size_t)tau] * (float)tau / runningSum) : 1.0f;
            }

            int bestTau = -1;
            float minVal = 1.0f;
            for (int tau = minLag; tau <= maxLag; ++tau)
            {
                if (cmndf[(size_t)tau] < minVal)
                {
                    minVal = cmndf[(size_t)tau];
                    bestTau = tau;
                }
                if (cmndf[(size_t)tau] < 0.18f && tau + 1 <= maxLag && cmndf[(size_t)tau] < cmndf[(size_t)(tau + 1)])
                {
                    bestTau = tau;
                    minVal = cmndf[(size_t)tau];
                    break;
                }
            }

            if (bestTau <= 0) { outConfidence = 0.0f; return 0.0f; }
            outConfidence = juce::jlimit(0.0f, 1.0f, 1.0f - minVal);
            return sampleRate / (float)bestTau;
        }

        static double extractBpmFromName(const juce::String& name, double durationSec)
        {
            for (int bpm = 70; bpm <= 180; ++bpm)
            {
                if (name.containsWholeWordIgnoreCase(juce::String(bpm)) ||
                    name.containsIgnoreCase(juce::String(bpm) + "bpm") ||
                    name.containsIgnoreCase("_" + juce::String(bpm)))
                {
                    return (double)bpm;
                }
            }
            if (durationSec > 1.0)
            {
                double guessed = (8.0 * 60.0) / durationSec;
                while (guessed < 80.0)  guessed *= 2.0;
                while (guessed > 165.0) guessed *= 0.5;
                return std::round(guessed);
            }
            return 120.0;
        }

        static void assignAccurateTagsAndCluster(const juce::String& lowerPath,
                                                 const juce::String& lowerName,
                                                 SampleRecord& rec)
        {
            // 1. Определение TIMBRE
            if (lowerPath.contains("key") || lowerPath.contains("piano") || lowerPath.contains("rhodes") ||
                lowerPath.contains("wurli") || lowerPath.contains("organ") || lowerPath.contains("epiano"))
                rec.timbreMask |= Timbre_Keys;

            if (lowerPath.contains("guitar") || lowerPath.contains("gtr") || lowerPath.contains("acoustic") ||
                lowerPath.contains("ukulele") || lowerPath.contains("harp") || lowerPath.contains("kalimba"))
                rec.timbreMask |= Timbre_Guitar;

            if (lowerPath.contains("vox") || lowerPath.contains("vocal") || lowerPath.contains("voice") ||
                lowerPath.contains("chant") || lowerPath.contains("choir"))
                rec.timbreMask |= Timbre_Vox;

            if (lowerPath.contains("string") || lowerPath.contains("violin") || lowerPath.contains("cello") ||
                lowerPath.contains("viola") || lowerPath.contains("orchestra"))
                rec.timbreMask |= Timbre_Strings;

            if (lowerPath.contains("synth") || lowerPath.contains("lead") || lowerPath.contains("pad") ||
                lowerPath.contains("arp") || lowerPath.contains("bass") || rec.timbreMask == Timbre_None)
                rec.timbreMask |= Timbre_Synth;

            // 2. Определение CHARACTER
            bool isBassOrDeep = (rec.rootMidiNote <= 50) || (rec.lowBandRatio >= 0.42f) ||
                                lowerName.contains("bass") || lowerName.contains("sub") || lowerName.contains("deep");
            if (isBassOrDeep)
                rec.characterMask |= Char_Deep;

            if (rec.spectralCentroidNorm < 0.46f && rec.highBandRatio < 0.18f)
                rec.characterMask |= Char_Warm;

            if ((rec.highBandRatio >= 0.22f && rec.attackRatio < 0.35f) ||
                lowerName.contains("air") || lowerName.contains("eth") || lowerName.contains("pad") || lowerName.contains("shimmer"))
                rec.characterMask |= Char_Ethy;

            // Взаимоисключающие Pluck vs Sustain
            bool isPlucky = (rec.attackRatio >= 0.38f && rec.tailDecayRatio < 0.40f) ||
                            lowerName.contains("pluck") || lowerName.contains("stab") || lowerName.contains("pizz");
            if (isPlucky)
                rec.characterMask |= Char_Pluck;
            else
                rec.characterMask |= Char_Sustain;

            // 3. Назначение смыслового кластера для 2D-карты и цвета
            if (rec.timbreMask & Timbre_Vox)
                rec.cluster = ClusterGroup::Vocal;
            else if (rec.timbreMask & Timbre_Keys)
                rec.cluster = ClusterGroup::Keys;
            else if ((rec.timbreMask & Timbre_Guitar) || (rec.characterMask & Char_Pluck))
                rec.cluster = ClusterGroup::GuitarPluck;
            else if (isBassOrDeep)
                rec.cluster = ClusterGroup::DeepBass;
            else if ((rec.timbreMask & Timbre_Strings) || (rec.characterMask & Char_Ethy))
                rec.cluster = ClusterGroup::StringsPad;
            else
                rec.cluster = ClusterGroup::SynthLead;
        }
    };
}
