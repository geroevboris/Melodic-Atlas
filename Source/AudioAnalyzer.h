#pragma once
#include "AtlasTypes.h"

namespace Atlas
{
    class AudioAnalyzer
    {
    public:
        static bool analyzeFile(const juce::File& file,
                                juce::AudioFormatManager& formatManager,
                                SampleRecord& outRecord)
        {
            std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
            if (reader == nullptr || reader->lengthInSamples <= 0 || reader->sampleRate <= 0.0)
                return false;

            const double sr = reader->sampleRate;
            const double duration = (double)reader->lengthInSamples / sr;

            if (duration < 0.06 || duration > 60.0)
                return false;

            const int maxSamplesToRead = (int)std::min<juce::int64>(
                reader->lengthInSamples, (juce::int64)(sr * 6.0));

            juce::AudioBuffer<float> buffer(1, maxSamplesToRead);
            buffer.clear();
            reader->read(&buffer, 0, maxSamplesToRead, 0, true, false);

            const float* data = buffer.getReadPointer(0);

            float sumSq = 0.0f;
            float peak = 0.0f;
            int zeroCrossings = 0;

            float envFirstQuarter = 0.0f;
            float envLastQuarter  = 0.0f;
            const int quarterLen = std::max(1, maxSamplesToRead / 4);

            for (int i = 0; i < maxSamplesToRead; ++i)
            {
                float s = data[i];
                if (!std::isfinite(s))
                    s = 0.0f;

                float a = std::abs(s);
                sumSq += s * s;
                if (a > peak)
                    peak = a;

                if (i > 0 && ((data[i - 1] >= 0.0f && s < 0.0f) || (data[i - 1] < 0.0f && s >= 0.0f)))
                    ++zeroCrossings;

                if (i < quarterLen)
                    envFirstQuarter += a;
                else if (i >= maxSamplesToRead - quarterLen)
                    envLastQuarter += a;
            }

            float rms = std::sqrt(sumSq / (float)maxSamplesToRead);
            if (rms < 0.0004f || peak < 0.002f)
                return false;

            envFirstQuarter /= (float)quarterLen;
            envLastQuarter  /= (float)quarterLen;

            float zcrHz = ((float)zeroCrossings * 0.5f) / (float)(maxSamplesToRead / sr);
            float brightnessNorm = juce::jlimit(
                0.04f, 0.96f,
                (float)(std::log10(std::max(60.0f, zcrHz) / 60.0f) / 2.2f));

            float attackRatio = juce::jlimit(
                0.0f, 1.0f,
                envFirstQuarter / std::max(0.0001f, envFirstQuarter + envLastQuarter));

            float tailDecayRatio = juce::jlimit(
                0.0f, 1.0f,
                envLastQuarter / std::max(0.0001f, envFirstQuarter));

            int detectedMidi = 60;
            float confidence = 0.5f;
            detectPitchAutocorr(data, maxSamplesToRead, sr, detectedMidi, confidence);

            juce::String lowerName = file.getFileNameWithoutExtension().toLowerCase();
            parsePitchAndBpmFromFilename(lowerName, detectedMidi, outRecord.detectedBpm);

            outRecord.file = file;
            outRecord.name = file.getFileNameWithoutExtension();
            outRecord.durationSec = duration;
            outRecord.isLoop = (duration > 2.4)
                            || lowerName.contains("loop")
                            || lowerName.contains("bpm")
                            || lowerName.contains("arp");
            outRecord.rootMidiNote = detectedMidi;
            outRecord.pitchClass = ((detectedMidi % 12) + 12) % 12;
            outRecord.pitchConfidence = confidence;
            outRecord.normGainLinear = juce::jlimit(0.15f, 3.0f, 0.22f / std::max(0.02f, rms));

            outRecord.spectralCentroidNorm = brightnessNorm;
            outRecord.attackRatio = attackRatio;
            outRecord.tailDecayRatio = tailDecayRatio;
            outRecord.pitchMobilityNorm = juce::jlimit(
                0.06f, 0.94f,
                0.5f * (1.0f - confidence) + 0.5f * std::abs(attackRatio - 0.5f) * 1.6f);
            outRecord.harmonicRoughness = juce::jlimit(0.0f, 1.0f, 1.0f - confidence);

            classifySample(lowerName, outRecord);
            return true;
        }

        static void relaxMapCoordinates(std::vector<SampleRecord>& records)
        {
            const size_t n = records.size();
            if (n == 0)
                return;

            // Центры созвездий, формирующие диагональную галактику (как в XO)
            static const float clusterAnchors[8][3] = {
                { 0.22f, 0.36f, 0.45f }, // Keys (x, y, угол наклона рукава)
                { 0.38f, 0.62f, -0.35f }, // Synth
                { 0.28f, 0.72f, 0.25f }, // Bass
                { 0.50f, 0.45f, -0.50f }, // Vocal
                { 0.63f, 0.34f, 0.40f }, // Strings
                { 0.74f, 0.48f, -0.20f }, // Guitar
                { 0.68f, 0.68f, 0.30f }, // FX
                { 0.52f, 0.58f, 0.00f }  // Other
            };

            juce::Random rng(1337);

            for (size_t i = 0; i < n; ++i)
            {
                auto& r = records[i];
                int cIdx = juce::jlimit(0, 7, (int)r.cluster);
                float ax = clusterAnchors[cIdx][0];
                float ay = clusterAnchors[cIdx][1];
                float armAngle = clusterAnchors[cIdx][2];

                // Мягкое гауссово облако без вылетов в жесткую рамку
                float u1 = juce::jlimit(0.002f, 0.998f, rng.nextFloat());
                float u2 = rng.nextFloat();
                float gaussR = std::sqrt(-2.0f * std::log(u1));
                // Ограничиваем радиус плавным гиперболическим тангенсом вместо жесткого обрезания
                float softR = 0.17f * std::tanh(gaussR * 0.55f);
                float theta = 2.0f * juce::MathConstants<float>::pi * u2;

                // Эллиптический разброс вдоль рукава созвездия
                float localX = softR * std::cos(theta) * 1.25f;
                float localY = softR * std::sin(theta) * 0.72f;

                float cosA = std::cos(armAngle);
                float sinA = std::sin(armAngle);
                float rotX = localX * cosA - localY * sinA;
                float rotY = localX * sinA + localY * cosA;

                // Смещение внутри созвездия по акустическим признакам
                float featX = (r.spectralCentroidNorm - 0.5f) * 0.14f;
                float featY = (r.pitchMobilityNorm - 0.5f) * 0.14f;

                float rawX = ax + rotX + featX;
                float rawY = ay + rotY + featY;

                // Радиальное мягкое сжатие к центру экрана (убирает квадратные края!)
                float dx = rawX - 0.5f;
                float dy = rawY - 0.5f;
                float distFromCenter = std::sqrt(dx * dx + dy * dy);
                if (distFromCenter > 0.001f)
                {
                    float maxAngleNoise = 0.42f + 0.05f * std::sin(std::atan2(dy, dx) * 5.0f);
                    float compressedDist = maxAngleNoise * std::tanh(distFromCenter / maxAngleNoise);
                    float scale = compressedDist / distFromCenter;
                    rawX = 0.5f + dx * scale;
                    rawY = 0.5f + dy * scale;
                }

                r.mapX = rawX;
                r.mapY = rawY;

                if (r.spectralCentroidNorm < 0.38f)
                    r.toneShadeBucket = 0;
                else if (r.spectralCentroidNorm > 0.64f)
                    r.toneShadeBucket = 2;
                else
                    r.toneShadeBucket = 1;
            }
        }

    private:
        static void detectPitchAutocorr(const float* data, int numSamples, double sr,
                                         int& outMidi, float& outConf)
        {
            const int window = std::min(numSamples, 2048);
            const int startOffset = std::min(std::max(0, numSamples / 6), numSamples - window);
            const float* seg = data + startOffset;

            int minLag = std::max(8, (int)(sr / 1000.0));
            int maxLag = std::min(window / 2, (int)(sr / 45.0));

            float bestCorr = 0.0f;
            int bestLag = minLag;
            float energy = 0.0001f;

            for (int i = 0; i < window / 2; ++i)
                energy += seg[i] * seg[i];

            for (int lag = minLag; lag < maxLag; ++lag)
            {
                float sum = 0.0f;
                for (int i = 0; i < window / 2; ++i)
                    sum += seg[i] * seg[i + lag];

                float norm = sum / energy;
                if (norm > bestCorr)
                {
                    bestCorr = norm;
                    bestLag = lag;
                }
            }

            float freq = (float)(sr / (double)std::max(1, bestLag));
            int midi = (int)std::round(69.0f + 12.0f * std::log2(std::max(20.0f, freq) / 440.0f));
            outMidi = juce::jlimit(24, 96, midi);
            outConf = juce::jlimit(0.1f, 0.98f, bestCorr);
        }

        static void parsePitchAndBpmFromFilename(const juce::String& lowerName,
                                                 int& ioMidi, double& outBpm)
        {
            outBpm = 120.0;
            for (int bpm = 70; bpm <= 180; ++bpm)
            {
                if (lowerName.contains(juce::String(bpm) + "bpm") ||
                    lowerName.contains("_" + juce::String(bpm) + "_"))
                {
                    outBpm = (double)bpm;
                    break;
                }
            }

            struct NoteToken { const char* token; int pc; };
            static const NoteToken tokens[] = {
                { "_c#", 1 }, { "_d#", 3 }, { "_f#", 6 }, { "_g#", 8 }, { "_a#", 10 },
                { "_db", 1 }, { "_eb", 3 }, { "_gb", 6 }, { "_ab", 8 }, { "_bb", 10 },
                { "_c",  0 }, { "_d",  2 }, { "_e",  4 }, { "_f",  5 }, { "_g",  7 }, { "_a", 9 }, { "_b", 11 }
            };

            for (const auto& t : tokens)
            {
                if (lowerName.contains(t.token))
                {
                    int oct = (ioMidi / 12);
                    ioMidi = oct * 12 + t.pc;
                    break;
                }
            }
        }

        static void classifySample(const juce::String& lowerName, SampleRecord& r)
        {
            if (lowerName.contains("key") || lowerName.contains("rhodes") ||
                lowerName.contains("piano") || lowerName.contains("wurli"))
                r.cluster = ClusterGroup::Keys;
            else if (lowerName.contains("bass") || lowerName.contains("sub") ||
                     lowerName.contains("808") || lowerName.contains("reese"))
                r.cluster = ClusterGroup::Bass;
            else if (lowerName.contains("vox") || lowerName.contains("vocal") ||
                     lowerName.contains("choir") || lowerName.contains("voice"))
                r.cluster = ClusterGroup::Vocal;
            else if (lowerName.contains("string") || lowerName.contains("cello") ||
                     lowerName.contains("violin") || lowerName.contains("viola"))
                r.cluster = ClusterGroup::Strings;
            else if (lowerName.contains("guitar") || lowerName.contains("gtr") ||
                     lowerName.contains("strum"))
                r.cluster = ClusterGroup::Guitar;
            else if (lowerName.contains("synth") || lowerName.contains("pad") ||
                     lowerName.contains("lead") || lowerName.contains("arp") ||
                     lowerName.contains("acid"))
                r.cluster = ClusterGroup::Synth;
            else
                r.cluster = ClusterGroup::FX;

            uint32_t mask = Sub_None;

            if (r.harmonicRoughness < 0.25f)
                mask |= Sub_Clean;
            else
                mask |= Sub_Noisy;

            if (r.spectralCentroidNorm < 0.48f)
                mask |= Sub_Warm;

            if (r.attackRatio > 0.62f || lowerName.contains("pluck") ||
                lowerName.contains("pizz") || lowerName.contains("chop"))
                mask |= Sub_Pluck;
            else
                mask |= Sub_Sustain;

            if (lowerName.contains("pad") ||
                (r.cluster == ClusterGroup::Synth && (mask & Sub_Sustain) && !lowerName.contains("lead")))
                mask |= Sub_Pad;

            if (lowerName.contains("lead") || lowerName.contains("acid") ||
                (r.cluster == ClusterGroup::Synth && r.spectralCentroidNorm > 0.6f))
                mask |= Sub_Lead;

            if (lowerName.contains("riser") || lowerName.contains("sweep"))
                mask |= Sub_RiserSweep;

            if (lowerName.contains("impact") || lowerName.contains("hit") ||
                (r.cluster == ClusterGroup::FX && (mask & Sub_Pluck)))
                mask |= Sub_ImpactHit;

            if (lowerName.contains("sub") ||
                (r.cluster == ClusterGroup::Bass && r.spectralCentroidNorm < 0.28f))
                mask |= Sub_SubBass;

            if (lowerName.contains("bell") || lowerName.contains("metal") ||
                (r.cluster == ClusterGroup::FX && (mask & Sub_Pluck)))
                mask |= Sub_BellMetallic;

            if (lowerName.contains("texture") || lowerName.contains("gran") ||
                lowerName.contains("air") || r.pitchMobilityNorm > 0.58f)
                mask |= Sub_Textural;

            r.subMask = mask;
        }
    };
}
