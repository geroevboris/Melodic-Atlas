#pragma once
#include "AtlasTypes.h"
#include <juce_dsp/juce_dsp.h>

namespace Atlas
{
    class AudioAnalyzer
    {
    public:
        static constexpr float kTargetMinus6dBLinear = 0.50118723f; // -6.0 dBFS True Peak
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

            // 1. Анализ огибающей по 16 сегментам для точного распознавания Loop vs One-Shot
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

            float tailRms = (segRms[13] + segRms[14] + segRms[15]) / 3.0f;
            float tailDecayRatio = tailRms / maxSegRms;

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

            // 2. Спектральный анализ + вычисление Spectral Flux (движение спектра во времени)
            juce::dsp::FFT fft(kFftOrder);
            juce::dsp::WindowingFunction<float> window(kFftSize, juce::dsp::WindowingFunction<float>::hann);

            std::vector<float> fftData(kFftSize * 2, 0.0f);
            std::vector<float> prevMagnitudes(kFftSize / 2, 0.0f);
            int numFrames = std::min(20, std::max(1, (maxSamplesToRead - kFftSize) / (kFftSize / 2)));

            float sumFlatness = 0.0f;
            float sumCentroidHz = 0.0f;
            float sumLowBand = 0.0f;
            float sumHighBand = 0.0f;
            float sumRoughness = 0.0f;
            float sumSpectralFlux = 0.0f;

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
                double lowE = 0.0, highE = 0.0, hfJaggedness = 0.0, frameFlux = 0.0;

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

                    if (validFrames > 0)
                    {
                        double diff = mag - (double)prevMagnitudes[(size_t)b];
                        if (diff > 0.0) frameFlux += diff;
                    }
                    prevMagnitudes[(size_t)b] = (float)mag;
                }

                double geomMean = std::exp(logSum / (double)(numBins - 1));
                double arithMean = arithSum / (double)(numBins - 1);
                float flatness = (float)(geomMean / (arithMean + 1e-12));
                float centroidHz = (float)(weightedFreqSum / (magSum + 1e-12));

                sumFlatness += flatness;
                sumCentroidHz += centroidHz;
                sumLowBand += (float)(lowE / (arithSum + 1e-12));
                sumHighBand += (float)(highE / (arithSum + 1e-12));
                sumRoughness += (float)(hfJaggedness / (magSum + 1e-12));
                if (validFrames > 0)
                    sumSpectralFlux += (float)(frameFlux / (magSum + 1e-12));

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
            float avgHighBand   = sumHighBand / (float)validFrames;
            float avgRoughness  = sumRoughness / (float)validFrames;
            float avgFlux       = (validFrames > 1) ? (sumSpectralFlux / (float)(validFrames - 1)) : 0.1f;
            float avgConfidence = framePitches.empty() ? 0.0f : (sumPitchConfidence / (float)framePitches.size());

            if (avgFlatness > 0.40f && avgConfidence < 0.62f)
                return false;
            if (duration < 0.75 && earlyEnergyRatio > 0.80f && avgConfidence < 0.74f)
                return false;
            if (framePitches.empty() || avgConfidence < 0.52f)
                return false;

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

            // Точное разделение One-Shots vs Loops
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
            outRecord.spectralFlux         = juce::jlimit(0.0f, 1.0f, avgFlux * 1.8f + pitchMobility * 0.4f);

            float compressionScore = juce::jlimit(0.0f, 1.0f, (4.5f - crestFactor) / 3.5f);
            outRecord.harmonicRoughness = juce::jlimit(0.0f, 1.0f, avgRoughness * 1.4f + clipDensity * 4.5f + compressionScore * 0.25f);

            assignAccurateTagsAndClusters(lowerPath, lowerName, outRecord);
            return true;
        }

        // Формирует сразу две кластерные проекции:
        // 1) Timbre View (6 инструментальных островов)
        // 2) Character View (5 островов по характеру звука)
        static void relaxMapCoordinates(std::vector<SampleRecord>& samples)
        {
            if (samples.empty()) return;

            // Калибруем порог Dirty / Clean относительно распределения реальной папки
            std::vector<float> roughnessVals;
            roughnessVals.reserve(samples.size());
            for (const auto& s : samples)
                roughnessVals.push_back(s.harmonicRoughness);
            std::sort(roughnessVals.begin(), roughnessVals.end());
            float dirtyPercentileThreshold = roughnessVals[roughnessVals.size() * 76 / 100];

            for (auto& s : samples)
            {
                juce::String ln = s.name.toLowerCase();
                bool hasDirtyWord = ln.contains("dirty") || ln.contains("dist") || ln.contains("sat") ||
                                    ln.contains("fuzz") || ln.contains("grit") || ln.contains("lofi") ||
                                    ln.contains("lo-fi") || ln.contains("bit") || ln.contains("crush") ||
                                    ln.contains("drive") || ln.contains("overdrive") || ln.contains("acid");

                s.characterMask &= ~(Char_Dirty | Char_Clean);
                if (hasDirtyWord || (s.harmonicRoughness >= std::max(0.32f, dirtyPercentileThreshold) && s.pitchConfidence < 0.88f))
                {
                    s.characterMask |= Char_Dirty;
                    s.charCluster = CharacterCluster::DirtyRaw;
                }
                else
                {
                    if (s.pitchConfidence >= 0.72f && s.harmonicRoughness < dirtyPercentileThreshold * 0.92f)
                        s.characterMask |= Char_Clean;

                    // Распределяем по остальным 4 кластерам характеров
                    if (s.characterMask & Char_Pluck)
                        s.charCluster = CharacterCluster::PluckCrisp;
                    else if (s.characterMask & Char_Ethy)
                        s.charCluster = CharacterCluster::AirEthereal;
                    else if (s.spectralFlux >= 0.42f || s.pitchMobilityNorm >= 0.55f)
                        s.charCluster = CharacterCluster::LushMoving;
                    else
                        s.charCluster = CharacterCluster::WarmDeep;
                }
            }

            struct ClusterCenter { float cx; float cy; float rx; float ry; };

            // Центры 6 островов для [TIMBRE VIEW]
            static const ClusterCenter timbreCenters[6] = {
                { 0.18f, 0.26f, 0.12f, 0.15f }, // 0: Deep / Bass
                { 0.50f, 0.26f, 0.13f, 0.15f }, // 1: Keys
                { 0.82f, 0.26f, 0.12f, 0.15f }, // 2: Guitar / Pluck
                { 0.20f, 0.73f, 0.13f, 0.16f }, // 3: Synth / Lead
                { 0.52f, 0.74f, 0.13f, 0.15f }, // 4: Vocal
                { 0.82f, 0.73f, 0.12f, 0.16f }  // 5: Strings / Pad
            };

            // Центры 5 островов для [CHARACTER VIEW]
            static const ClusterCenter charCenters[5] = {
                { 0.20f, 0.27f, 0.13f, 0.16f }, // 0: Warm & Deep
                { 0.52f, 0.25f, 0.13f, 0.15f }, // 1: Air & Ethereal
                { 0.82f, 0.28f, 0.12f, 0.15f }, // 2: Pluck & Crisp
                { 0.32f, 0.74f, 0.14f, 0.16f }, // 3: Dirty & Raw
                { 0.70f, 0.74f, 0.14f, 0.16f }  // 4: Lush & Moving
            };

            const float goldenAngle = 2.39996323f;

            for (int loopPass = 0; loopPass < 2; ++loopPass)
            {
                bool targetLoop = (loopPass == 1);

                // 1. Раскладка для [TIMBRE VIEW]
                for (int c = 0; c < 6; ++c)
                {
                    std::vector<size_t> indices;
                    for (size_t i = 0; i < samples.size(); ++i)
                        if (samples[i].isLoop == targetLoop && (int)samples[i].timbreCluster == c)
                            indices.push_back(i);

                    if (indices.empty()) continue;

                    std::sort(indices.begin(), indices.end(), [&](size_t a, size_t b) {
                        return (samples[a].spectralCentroidNorm * 2.0f + (float)samples[a].pitchClass * 0.05f)
                             < (samples[b].spectralCentroidNorm * 2.0f + (float)samples[b].pitchClass * 0.05f);
                    });

                    const auto& cc = timbreCenters[c];
                    const size_t count = indices.size();
                    for (size_t k = 0; k < count; ++k)
                    {
                        auto& rec = samples[indices[k]];
                        float r = std::sqrt(((float)k + 0.5f) / (float)count);
                        float theta = (float)k * goldenAngle + (rec.pitchMobilityNorm - 0.5f) * 0.8f;
                        float lx = 0.72f * (r * std::cos(theta)) + 0.28f * (rec.pitchMobilityNorm * 2.0f - 1.0f);
                        float ly = 0.72f * (r * std::sin(theta)) + 0.28f * (rec.spectralCentroidNorm * 2.0f - 1.0f);
                        rec.timbreX = juce::jlimit(0.05f, 0.95f, cc.cx + lx * cc.rx);
                        rec.timbreY = juce::jlimit(0.06f, 0.94f, cc.cy + ly * cc.ry);
                    }
                }

                // 2. Раскладка для [CHARACTER VIEW]
                for (int c = 0; c < 5; ++c)
                {
                    std::vector<size_t> indices;
                    for (size_t i = 0; i < samples.size(); ++i)
                        if (samples[i].isLoop == targetLoop && (int)samples[i].charCluster == c)
                            indices.push_back(i);

                    if (indices.empty()) continue;

                    std::sort(indices.begin(), indices.end(), [&](size_t a, size_t b) {
                        return (samples[a].harmonicRoughness + samples[a].spectralCentroidNorm)
                             < (samples[b].harmonicRoughness + samples[b].spectralCentroidNorm);
                    });

                    const auto& cc = charCenters[c];
                    const size_t count = indices.size();
                    for (size_t k = 0; k < count; ++k)
                    {
                        auto& rec = samples[indices[k]];
                        float r = std::sqrt(((float)k + 0.5f) / (float)count);
                        float theta = (float)k * goldenAngle + (rec.harmonicRoughness - 0.3f) * 0.9f;
                        float lx = 0.72f * (r * std::cos(theta)) + 0.28f * (rec.harmonicRoughness * 2.0f - 1.0f);
                        float ly = 0.72f * (r * std::sin(theta)) + 0.28f * (rec.spectralCentroidNorm * 2.0f - 1.0f);
                        rec.charX = juce::jlimit(0.05f, 0.95f, cc.cx + lx * cc.rx);
                        rec.charY = juce::jlimit(0.06f, 0.94f, cc.cy + ly * cc.ry);
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

        static void assignAccurateTagsAndClusters(const juce::String& lowerPath,
                                                  const juce::String& lowerName,
                                                  SampleRecord& rec)
        {
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

            bool isBassOrDeep = (rec.rootMidiNote <= 50) || (rec.lowBandRatio >= 0.42f) ||
                                lowerName.contains("bass") || lowerName.contains("sub") || lowerName.contains("deep");
            if (isBassOrDeep)
                rec.characterMask |= Char_Deep;

            if (rec.spectralCentroidNorm < 0.46f && rec.highBandRatio < 0.18f)
                rec.characterMask |= Char_Warm;

            if ((rec.highBandRatio >= 0.22f && rec.attackRatio < 0.35f) ||
                lowerName.contains("air") || lowerName.contains("eth") || lowerName.contains("pad") || lowerName.contains("shimmer"))
                rec.characterMask |= Char_Ethy;

            bool isPlucky = (rec.attackRatio >= 0.38f && rec.tailDecayRatio < 0.40f) ||
                            lowerName.contains("pluck") || lowerName.contains("stab") || lowerName.contains("pizz");
            if (isPlucky)
                rec.characterMask |= Char_Pluck;
            else
                rec.characterMask |= Char_Sustain;

            if (rec.timbreMask & Timbre_Vox)
                rec.timbreCluster = TimbreCluster::Vocal;
            else if (rec.timbreMask & Timbre_Keys)
                rec.timbreCluster = TimbreCluster::Keys;
            else if ((rec.timbreMask & Timbre_Guitar) || (rec.characterMask & Char_Pluck))
                rec.timbreCluster = TimbreCluster::GuitarPluck;
            else if (isBassOrDeep)
                rec.timbreCluster = TimbreCluster::DeepBass;
            else if ((rec.timbreMask & Timbre_Strings) || (rec.characterMask & Char_Ethy))
                rec.timbreCluster = TimbreCluster::StringsPad;
            else
                rec.timbreCluster = TimbreCluster::SynthLead;
        }
    };
}
