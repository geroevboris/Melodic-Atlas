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

            // Отсекаем явные одиночные барабаны, но сохраняем FX, текстуры и мелодику
            bool isExplicitDrum =
                lowerName.contains("kick") || lowerName.contains("snare") ||
                lowerName.contains("hihat") || lowerName.contains("hi-hat") ||
                lowerName.contains("clap") || lowerName.contains("cymbal") ||
                lowerName.contains("hat_") || lowerName.contains("_hat") ||
                lowerName.contains("rimshot") || lowerName.contains("tambourine") ||
                lowerName.contains("shaker");

            bool isExplicitFx =
                lowerPath.contains("fx") || lowerPath.contains("riser") ||
                lowerPath.contains("sweep") || lowerPath.contains("uplift") ||
                lowerPath.contains("downlift") || lowerPath.contains("faller") ||
                lowerPath.contains("whoosh") || lowerPath.contains("impact") ||
                lowerPath.contains("transition") || lowerPath.contains("noise") ||
                lowerPath.contains("texture") || lowerPath.contains("atmos") ||
                lowerPath.contains("foley") || lowerPath.contains("swell");

            if (isExplicitDrum && !isExplicitFx)
                return false;

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

            // 1. Сегментный анализ огибающей (16 сегментов)
            const int numSegs = 16;
            const int segLen = std::max(1, maxSamplesToRead / numSegs);
            std::array<float, numSegs> segRms { 0.0f };
            float maxSegRms = 0.0001f;
            int peakSegmentIndex = 0;

            for (int s = 0; s < numSegs; ++s)
            {
                int start = s * segLen;
                int end = std::min(maxSamplesToRead, start + segLen);
                double sumSq = 0.0;
                for (int i = start; i < end; ++i)
                    sumSq += (double)mono[(size_t)i] * (double)mono[(size_t)i];
                segRms[(size_t)s] = std::sqrt((float)(sumSq / (double)std::max(1, end - start)));
                if (segRms[(size_t)s] > maxSegRms)
                {
                    maxSegRms = segRms[(size_t)s];
                    peakSegmentIndex = s;
                }
            }

            float tailRms = (segRms[13] + segRms[14] + segRms[15]) / 3.0f;
            float tailDecayRatio = tailRms / maxSegRms;

            // Нарастает ли громкость к середине/концу (признак Riser / Reverse / Swell)
            float startRms = (segRms[0] + segRms[1]) * 0.5f;
            bool isRisingEnvelope = (peakSegmentIndex >= 5 && startRms < maxSegRms * 0.45f);

            int rhythmicOnsets = 0;
            for (int s = 2; s < numSegs; ++s)
            {
                if (segRms[(size_t)s] > segRms[(size_t)(s - 1)] * 1.42f && segRms[(size_t)s] > maxSegRms * 0.28f)
                    rhythmicOnsets++;
            }

            const int earlySamples = std::min(maxSamplesToRead, (int)(reader->sampleRate * 0.085));
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

            // 2. Спектральный анализ
            juce::dsp::FFT fft(kFftOrder);
            juce::dsp::WindowingFunction<float> window(kFftSize, juce::dsp::WindowingFunction<float>::hann);

            std::vector<float> fftData(kFftSize * 2, 0.0f);
            int numFrames = std::min(20, std::max(1, (maxSamplesToRead - kFftSize) / (kFftSize / 2)));

            float sumFlatness = 0.0f;
            float sumCentroidHz = 0.0f;
            float firstHalfCentroid = 0.0f;
            float secondHalfCentroid = 0.0f;
            int firstHalfCount = 0, secondHalfCount = 0;

            float sumLowBand = 0.0f;
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
                if (frameRms < maxPeak * 0.04f)
                    continue;

                std::fill(fftData.begin(), fftData.end(), 0.0f);
                std::copy(mono.begin() + startSample, mono.begin() + startSample + kFftSize, fftData.begin());
                window.multiplyWithWindowingTable(fftData.data(), kFftSize);
                fft.performFrequencyOnlyForwardTransform(fftData.data());

                const int numBins = kFftSize / 2;
                double logSum = 0.0, arithSum = 0.0, weightedFreqSum = 0.0, magSum = 0.0;
                double lowE = 0.0, highE = 0.0, hfJaggedness = 0.0;

                for (int b = 1; b < numBins; ++b)
                {
                    double mag = (double)fftData[(size_t)b] + 1e-7;
                    double power = mag * mag;
                    logSum += std::log(power);
                    arithSum += power;

                    double freqHz = (double)b * reader->sampleRate / (double)kFftSize;
                    weightedFreqSum += freqHz * mag;
                    magSum += mag;

                    if (freqHz <= 250.0) lowE += power;
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
                if (f < numFrames / 2) { firstHalfCentroid += centroidHz; firstHalfCount++; }
                else                   { secondHalfCentroid += centroidHz; secondHalfCount++; }

                sumLowBand += (float)(lowE / (arithSum + 1e-12));
                sumHighBand += (float)(highE / (arithSum + 1e-12));
                sumRoughness += (float)(hfJaggedness / (magSum + 1e-12));

                float conf = 0.0f;
                float f0 = detectPitchYIN(mono.data() + startSample, kFftSize, (float)reader->sampleRate, conf);
                if (f0 > 32.0f && conf > 0.38f)
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
            float avgHighBand   = sumHighBand / (float)validFrames;
            float avgRoughness  = sumRoughness / (float)validFrames;
            float avgConfidence = framePitches.empty() ? 0.25f : (sumPitchConfidence / (float)framePitches.size());

            // Проверяем, есть ли сильный спектральный сдвиг (Sweep / Riser)
            float c1 = (firstHalfCount > 0) ? (firstHalfCentroid / (float)firstHalfCount) : avgCentroidHz;
            float c2 = (secondHalfCount > 0) ? (secondHalfCentroid / (float)secondHalfCount) : avgCentroidHz;
            float centroidSweepRatio = std::abs(c2 - c1) / std::max(200.0f, avgCentroidHz);
            bool hasSweepSignature = (duration >= 0.9 && (centroidSweepRatio > 0.42f || isRisingEnvelope));

            // Отсекаем только короткие немелодические щелчки/перкуссию, если это не FX
            if (!isExplicitFx && !hasSweepSignature)
            {
                if (duration < 0.65 && earlyEnergyRatio > 0.82f && avgConfidence < 0.68f)
                    return false;
                if (duration < 0.45 && avgFlatness > 0.38f)
                    return false;
            }

            int dominantPitchClass = 0;
            int medianMidi = 60;
            int uniquePitchClasses = 1;

            if (!framePitches.empty())
            {
                std::array<int, 12> pitchClassHist { 0 };
                for (int n : framePitches)
                    pitchClassHist[(size_t)(((n % 12) + 12) % 12)]++;

                int maxCount = -1;
                for (int pc = 0; pc < 12; ++pc)
                {
                    if (pitchClassHist[(size_t)pc] > maxCount)
                    {
                        maxCount = pitchClassHist[(size_t)pc];
                        dominantPitchClass = pc;
                    }
                }

                std::sort(framePitches.begin(), framePitches.end());
                medianMidi = framePitches[framePitches.size() / 2];

                uniquePitchClasses = 0;
                for (int pc = 0; pc < 12; ++pc)
                    if (pitchClassHist[(size_t)pc] > 0) uniquePitchClasses++;
            }

            float pitchMobility = juce::jlimit(0.0f, 1.0f, (float)(uniquePitchClasses - 1) / 5.0f);

            // Определение One-Shot vs Loop
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
                isDetectedLoop = false;
            else if (explicitLoop && duration >= 1.45)
                isDetectedLoop = true;
            else if (tailDecayRatio < 0.34f && rhythmicOnsets <= 1)
                isDetectedLoop = false;
            else if (duration >= 1.85 && tailDecayRatio >= 0.42f && (rhythmicOnsets >= 2 || uniquePitchClasses >= 3))
                isDetectedLoop = true;

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

            float compressionScore = juce::jlimit(0.0f, 1.0f, (4.5f - crestFactor) / 3.5f);
            outRecord.harmonicRoughness = juce::jlimit(0.0f, 1.0f, avgRoughness * 1.4f + clipDensity * 4.5f + compressionScore * 0.25f);

            classifyInstrumentFxOrOther(lowerPath, lowerName, isExplicitFx, hasSweepSignature, outRecord);
            return true;
        }

        // Плотная укладка кластеров в единое полотно в стиле XLN XO + расчёт 3 оттенков высоты тона
        static void relaxMapCoordinates(std::vector<SampleRecord>& samples)
        {
            if (samples.empty()) return;

            // 1. Калибруем фильтр Noisy / Clean относительно реального распределения папки
            std::vector<float> noiseScores;
            noiseScores.reserve(samples.size());
            for (const auto& s : samples)
                noiseScores.push_back(s.harmonicRoughness * 0.65f + s.spectralFlatness * 0.85f);
            std::sort(noiseScores.begin(), noiseScores.end());
            float noisyThreshold = noiseScores[noiseScores.size() * 72 / 100];

            for (auto& s : samples)
            {
                juce::String ln = s.name.toLowerCase();
                bool explicitNoisy = ln.contains("dirty") || ln.contains("dist") || ln.contains("noise") ||
                                     ln.contains("lofi") || ln.contains("lo-fi") || ln.contains("grit") ||
                                     ln.contains("fuzz") || ln.contains("crush") || ln.contains("sat");

                float score = s.harmonicRoughness * 0.65f + s.spectralFlatness * 0.85f;
                s.subMask &= ~(Sub_Clean | Sub_Noisy);
                if (explicitNoisy || score >= std::max(0.24f, noisyThreshold))
                    s.subMask |= Sub_Noisy;
                else
                    s.subMask |= Sub_Clean;
            }

            // 2. Координаты центров 8 кластеров в едином плотном континенте (как в XLN XO)
            // Кластеры смыкаются друг с другом в центральной области без огромных дыр
            struct ClusterSector { float cx; float cy; float rx; float ry; };
            static const ClusterSector sectors[8] = {
                { 0.34f, 0.35f, 0.14f, 0.16f }, // 0: KEYS (Янтарь)
                { 0.52f, 0.33f, 0.15f, 0.16f }, // 1: SYNTH (Циан)
                { 0.25f, 0.56f, 0.14f, 0.16f }, // 2: BASS (Индиго)
                { 0.69f, 0.37f, 0.14f, 0.16f }, // 3: VOX (Коралл)
                { 0.66f, 0.63f, 0.14f, 0.16f }, // 4: STRINGS (Лаванда)
                { 0.45f, 0.64f, 0.14f, 0.16f }, // 5: GUITAR (Шалфей)
                { 0.77f, 0.58f, 0.13f, 0.15f }, // 6: FX (Пурпурный)
                { 0.32f, 0.72f, 0.15f, 0.15f }  // 7: OTHER (Сланец)
            };

            for (int loopPass = 0; loopPass < 2; ++loopPass)
            {
                bool targetLoop = (loopPass == 1);
                for (int c = 0; c < 8; ++c)
                {
                    std::vector<size_t> indices;
                    for (size_t i = 0; i < samples.size(); ++i)
                    {
                        if (samples[i].isLoop == targetLoop && (int)samples[i].cluster == c)
                            indices.push_back(i);
                    }

                    if (indices.empty()) continue;

                    // Сортируем по высоте тона и спектральной яркости
                    std::sort(indices.begin(), indices.end(), [&](size_t a, size_t b)
                    {
                        float toneA = (float)samples[a].rootMidiNote + samples[a].spectralCentroidNorm * 24.0f;
                        float toneB = (float)samples[b].rootMidiNote + samples[b].spectralCentroidNorm * 24.0f;
                        return toneA < toneB;
                    });

                    const auto& sec = sectors[c];
                    const size_t count = indices.size();
                    const float goldenAngle = 2.39996323f;

                    // Радиус сектора плавно адаптируется под количество звуков для высокой плотности
                    float densityScale = juce::jlimit(0.55f, 1.18f, std::sqrt((float)count / 45.0f));

                    for (size_t k = 0; k < count; ++k)
                    {
                        auto& rec = samples[indices[k]];
                        float rankNorm = ((float)k + 0.5f) / (float)count;

                        // Назначаем ровно 3 оттенка тона (0 = низкий/тёмный, 1 = средний, 2 = высокий/светлый)
                        if (rankNorm < 0.33f)      rec.toneShadeBucket = 0;
                        else if (rankNorm < 0.67f) rec.toneShadeBucket = 1;
                        else                       rec.toneShadeBucket = 2;

                        float r = std::sqrt(rankNorm) * densityScale;
                        float theta = (float)k * goldenAngle;

                        // Укладываем точки плотным органическим облаком с плавным градиентом высоты тона снизу-вверх
                        float localX = 0.78f * (r * std::cos(theta)) + 0.22f * (rec.pitchMobilityNorm * 2.0f - 1.0f);
                        float localY = 0.72f * (r * std::sin(theta)) + 0.28f * (rankNorm * 2.0f - 1.0f);

                        rec.mapX = juce::jlimit(0.08f, 0.92f, sec.cx + localX * sec.rx);
                        rec.mapY = juce::jlimit(0.08f, 0.92f, sec.cy + localY * sec.ry);
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

        static void classifyInstrumentFxOrOther(const juce::String& lowerPath,
                                                const juce::String& lowerName,
                                                bool isExplicitFx,
                                                bool hasSweepSignature,
                                                SampleRecord& rec)
        {
            rec.subMask = Sub_None;

            // 1. Определяем подгруппы характера (для фильтрации и для группы Other / FX)
            if (rec.spectralCentroidNorm < 0.45f && rec.highBandRatio < 0.18f)
                rec.subMask |= Sub_Warm;

            bool isPlucky = (rec.attackRatio >= 0.36f && rec.tailDecayRatio < 0.42f) ||
                            lowerName.contains("pluck") || lowerName.contains("stab") || lowerName.contains("pizz");
            if (isPlucky)
                rec.subMask |= Sub_Pluck;
            else
                rec.subMask |= Sub_Sustain;

            if (isExplicitFx || hasSweepSignature || lowerName.contains("riser") || lowerName.contains("sweep") || lowerName.contains("swell"))
                rec.subMask |= Sub_RiserSweep;

            if (rec.spectralFlatness > 0.14f || lowerPath.contains("texture") || lowerPath.contains("atmos") || lowerPath.contains("ambien"))
                rec.subMask |= Sub_Textural;

            // 2. Явное определение группы инструмента, FX или отправка в Other
            bool isKeys = lowerPath.contains("key") || lowerPath.contains("piano") || lowerPath.contains("rhodes") ||
                          lowerPath.contains("wurli") || lowerPath.contains("organ") || lowerPath.contains("epiano") ||
                          lowerPath.contains("clav");

            bool isGuitar = lowerPath.contains("guitar") || lowerPath.contains("gtr") || lowerPath.contains("acoustic") ||
                            lowerPath.contains("ukulele") || lowerPath.contains("harp") || lowerPath.contains("kalimba") ||
                            lowerPath.contains("nylon");

            bool isVox = lowerPath.contains("vox") || lowerPath.contains("vocal") || lowerPath.contains("voice") ||
                         lowerPath.contains("chant") || lowerPath.contains("choir") || lowerPath.contains("sing");

            bool isStrings = lowerPath.contains("string") || lowerPath.contains("violin") || lowerPath.contains("cello") ||
                             lowerPath.contains("viola") || lowerPath.contains("orchestra");

            bool isBass = lowerPath.contains("bass") || lowerPath.contains("sub") || lowerPath.contains("808") ||
                          lowerPath.contains("reese") || (rec.rootMidiNote <= 45 && rec.lowBandRatio >= 0.50f && rec.pitchConfidence >= 0.72f);

            bool isSynth = lowerPath.contains("synth") || lowerPath.contains("lead") || lowerPath.contains("pad") ||
                           lowerPath.contains("arp") || lowerPath.contains("saw") || lowerPath.contains("analog");

            if (isExplicitFx || (hasSweepSignature && !isKeys && !isGuitar && !isVox && !isStrings && !isBass))
                rec.cluster = ClusterGroup::FX;
            else if (isVox)
                rec.cluster = ClusterGroup::Vocal;
            else if (isKeys)
                rec.cluster = ClusterGroup::Keys;
            else if (isGuitar)
                rec.cluster = ClusterGroup::Guitar;
            else if (isStrings)
                rec.cluster = ClusterGroup::Strings;
            else if (isBass)
                rec.cluster = ClusterGroup::Bass;
            else if (isSynth)
                rec.cluster = ClusterGroup::Synth;
            else
                rec.cluster = ClusterGroup::Other; // Все неочевидные звуки попадают в Other с фильтрацией по характеру
        }
    };
}
