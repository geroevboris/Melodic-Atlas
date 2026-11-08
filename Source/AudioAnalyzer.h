#pragma once
#include "AtlasTypes.h"
#include "AcousticFeatureExtractor.h"
#include "TonalFeatureExtractor.h"
#include "CharacterAnalyzer.h"
#include "PercussionGate.h"
#include "SoundIdentityAnalyzer.h"
#include "PitchRhythmEvidence.h"
#include "LoopStructureAnalyzer.h"
#include "InstrumentSubgroupTags.h"

namespace Atlas
{
    class AudioAnalyzer
    {
    public:
        static bool analyzeFile(const juce::File& file,
                                juce::AudioFormatManager& formatManager,
                                SampleRecord& outRecord)
        {
            std::unique_ptr<juce::AudioFormatReader> reader(
                formatManager.createReaderFor(file));
            if (reader == nullptr || reader->lengthInSamples <= 0 || reader->sampleRate <= 0.0)
                return false;

            const double sr = reader->sampleRate;
            const double duration = (double)reader->lengthInSamples / sr;

            if (duration < 0.06 || duration > 60.0)
                return false;

            const int maxSamplesToRead = (int)std::min<juce::int64>(
                reader->lengthInSamples, (juce::int64)(sr * 6.0));

            // Retain legacy left-channel data for today's classifier and map,
            // but measure new acoustic features from a proper mono downmix.
            const bool stereo = reader->numChannels > 1;
            juce::AudioBuffer<float> buffer(stereo ? 2 : 1, maxSamplesToRead);
            buffer.clear();
            if (!reader->read(&buffer, 0, maxSamplesToRead, 0, true, stereo))
                return false;

            const float* data = buffer.getReadPointer(0);
            std::vector<float> downmix((size_t)maxSamplesToRead);
            const float* right = stereo ? buffer.getReadPointer(1) : nullptr;
            for (int i = 0; i < maxSamplesToRead; ++i)
            {
                const float a = std::isfinite(data[i]) ? data[i] : 0.0f;
                const float b = right && std::isfinite(right[i]) ? right[i] : a;
                downmix[(size_t)i] = 0.5f * (a + b);
            }

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

                if (i > 0 && ((data[i - 1] >= 0.0f && s < 0.0f) ||
                              (data[i - 1] < 0.0f && s >= 0.0f)))
                {
                    ++zeroCrossings;
                }

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

            const double activeSeconds = estimateActiveSeconds(
                downmix.data(), maxSamplesToRead, sr, duration);

            outRecord.file = file;
            outRecord.name = file.getFileNameWithoutExtension();
            outRecord.durationSec = duration;
            outRecord.effectiveDurationSec = activeSeconds;
            outRecord.durationAnalysisVersion = 1;
            outRecord.isLoop = LoopStructureAnalyzer::isLoop(
                file, downmix.data(), maxSamplesToRead, sr, duration);
            outRecord.loopAnalysisVersion = 3;
            outRecord.rootMidiNote = detectedMidi;
            outRecord.pitchClass = ((detectedMidi % 12) + 12) % 12;
            outRecord.pitchConfidence = confidence;
            outRecord.normGainLinear = juce::jlimit(
                0.15f, 3.0f, 0.22f / std::max(0.02f, rms));

            outRecord.spectralCentroidNorm = brightnessNorm;
            outRecord.attackRatio = attackRatio;
            outRecord.tailDecayRatio = tailDecayRatio;
            outRecord.pitchMobilityNorm = juce::jlimit(
                0.06f, 0.94f,
                0.5f * (1.0f - confidence) + 0.5f * std::abs(attackRatio - 0.5f) * 1.6f);
            outRecord.harmonicRoughness = juce::jlimit(0.0f, 1.0f, 1.0f - confidence);
            outRecord.acoustic = AcousticFeatureExtractor::analyze(
                downmix.data(), maxSamplesToRead, sr);
            outRecord.attackAnalysisVersion = 1;
            outRecord.tonal = TonalFeatureExtractor::analyze(
                downmix.data(), maxSamplesToRead, sr);

            classifySample(file, outRecord);
            CharacterAnalyzer::evaluate(outRecord);
            PercussionGate::evaluate(outRecord);
            SoundIdentityAnalyzer::evaluate(file, outRecord);
            PitchRhythmEvidence::evaluate(outRecord);
            // Never present a guess from the legacy heuristic as a confident
            // instrument family. Ambiguous sounds remain browsable under ALL
            // but do not leak into an instrument filter.
            if (outRecord.identityState == IdentityState::Identified)
                outRecord.cluster = outRecord.identityCluster;
            else
                outRecord.cluster = ClusterGroup::Other;
            InstrumentSubgroupTags::evaluate(file, outRecord);
            return true;
        }

        // Refresh only the attack envelope of unchanged cached audio. The
        // original CLAP embeddings and spectral analysis stay untouched.
        static bool refreshInitialAttack(const juce::File& file,
                                         juce::AudioFormatManager& formats,
                                         SampleRecord& record)
        {
            std::unique_ptr<juce::AudioFormatReader> reader(
                formats.createReaderFor(file));
            if (reader == nullptr || reader->lengthInSamples <= 0
                || reader->sampleRate < 8000.0)
                return false;
            const double rate = reader->sampleRate;
            const int count = (int)std::min<juce::int64>(
                reader->lengthInSamples, (juce::int64)(rate * 4.0));
            const bool stereo = reader->numChannels > 1;
            juce::AudioBuffer<float> data(stereo ? 2 : 1, count);
            data.clear();
            if (!reader->read(&data, 0, count, 0, true, stereo))
                return false;
            std::vector<float> mono((size_t)count);
            const float* left = data.getReadPointer(0);
            const float* right = stereo ? data.getReadPointer(1) : nullptr;
            for (int i = 0; i < count; ++i)
            {
                const float l = std::isfinite(left[i]) ? left[i] : 0.0f;
                const float r = right && std::isfinite(right[i]) ? right[i] : l;
                mono[(size_t)i] = 0.5f * (l + r);
            }
            float seconds = 0.0f;
            const bool measured = AcousticFeatureExtractor::measureInitialAttack(
                mono.data(), count, rate, seconds);
            record.acoustic.attackMeasured = measured;
            record.acoustic.attackTimeSec = measured ? seconds : 0.0f;
            // A silent/invalid envelope still receives a cached 'unknown'
            // result, preventing repeated re-decoding on every Refresh.
            record.attackAnalysisVersion = 1;
            return true;
        }

        // Re-evaluate loop structure on cached audio without touching the
        // previously computed CLAP embedding or spatial descriptors.
        static bool refreshLoopStructure(const juce::File& file,
                                         juce::AudioFormatManager& formats,
                                         SampleRecord& record)
        {
            std::unique_ptr<juce::AudioFormatReader> reader(
                formats.createReaderFor(file));
            if (reader == nullptr || reader->lengthInSamples <= 0
                || reader->sampleRate <= 0.0)
                return false;
            const double sr = reader->sampleRate;
            const double seconds = (double)reader->lengthInSamples / sr;
            const int frames = (int)std::min<juce::int64>(
                reader->lengthInSamples, (juce::int64)(sr * 6.0));
            const bool stereo = reader->numChannels > 1;
            juce::AudioBuffer<float> buffer(stereo ? 2 : 1, frames);
            buffer.clear();
            if (!reader->read(&buffer, 0, frames, 0, true, stereo))
                return false;
            std::vector<float> mono((size_t)frames);
            const float* l = buffer.getReadPointer(0);
            const float* r = stereo ? buffer.getReadPointer(1) : nullptr;
            for (int i = 0; i < frames; ++i)
            {
                const float left = std::isfinite(l[i]) ? l[i] : 0.0f;
                const float right = r && std::isfinite(r[i]) ? r[i] : left;
                mono[(size_t)i] = (left + right) * 0.5f;
            }
            record.isLoop = LoopStructureAnalyzer::isLoop(
                file, mono.data(), frames, sr, seconds);
            record.loopAnalysisVersion = 3;
            if (record.loopUserOverride >= 0)
                record.isLoop = record.loopUserOverride == 1;
            return true;
        }

        // Refresh old caches without re-running expensive CLAP inference.
        // Used on scanner thread only and only once per unchanged file.
        static bool refreshEffectiveDuration(const juce::File& file,
                                             juce::AudioFormatManager& formats,
                                             SampleRecord& record)
        {
            std::unique_ptr<juce::AudioFormatReader> reader(
                formats.createReaderFor(file));
            if (reader == nullptr || reader->lengthInSamples <= 0
                || reader->sampleRate <= 0.0)
                return false;
            const double sampleRate = reader->sampleRate;
            const double totalSeconds = (double)reader->lengthInSamples / sampleRate;
            const int count = (int)std::min<juce::int64>(
                reader->lengthInSamples, (juce::int64)(sampleRate * 6.0));
            const bool stereo = reader->numChannels > 1;
            juce::AudioBuffer<float> data(stereo ? 2 : 1, count);
            data.clear();
            if (!reader->read(&data, 0, count, 0, true, stereo))
                return false;
            std::vector<float> mono((size_t)count);
            const float* l = data.getReadPointer(0);
            const float* r = stereo ? data.getReadPointer(1) : nullptr;
            for (int i = 0; i < count; ++i)
            {
                const float vl = std::isfinite(l[i]) ? l[i] : 0.0f;
                const float vr = r && std::isfinite(r[i]) ? r[i] : vl;
                mono[(size_t)i] = (vl + vr) * 0.5f;
            }
            record.effectiveDurationSec = estimateActiveSeconds(
                mono.data(), count, sampleRate, totalSeconds);
            record.durationAnalysisVersion = 1;
            return true;
        }

        static void relaxMapCoordinates(std::vector<SampleRecord>& records)
        {
            const size_t n = records.size();
            if (n == 0)
                return;

            // Перепроверяем классификацию с учётом путей папок (исправляет духовые в кэше)
            for (size_t i = 0; i < n; ++i)
            {
                if (records[i].file.getFullPathName().isNotEmpty())
                    classifySample(records[i].file, records[i]);
            }

            // Центры созвездий, формирующие диагональную туманность
            static const float clusterAnchors[8][3] = {
                { 0.22f, 0.36f,  0.45f }, // Keys
                { 0.38f, 0.62f, -0.35f }, // Synth
                { 0.28f, 0.72f,  0.25f }, // Bass
                { 0.50f, 0.45f, -0.50f }, // Vocal
                { 0.63f, 0.34f,  0.40f }, // Strings & Winds
                { 0.74f, 0.48f, -0.20f }, // Guitar
                { 0.68f, 0.68f,  0.30f }, // FX
                { 0.52f, 0.58f,  0.00f }  // Other
            };

            juce::Random rng(1337);

            for (size_t i = 0; i < n; ++i)
            {
                auto& r = records[i];
                int cIdx = juce::jlimit(0, 7, (int)r.cluster);
                float ax = clusterAnchors[cIdx][0];
                float ay = clusterAnchors[cIdx][1];
                float armAngle = clusterAnchors[cIdx][2];

                float u1 = juce::jlimit(0.002f, 0.998f, rng.nextFloat());
                float u2 = rng.nextFloat();
                float gaussR = std::sqrt(-2.0f * std::log(u1));
                float softR = 0.17f * std::tanh(gaussR * 0.55f);
                float theta = 2.0f * juce::MathConstants<float>::pi * u2;

                float localX = softR * std::cos(theta) * 1.25f;
                float localY = softR * std::sin(theta) * 0.72f;

                float cosA = std::cos(armAngle);
                float sinA = std::sin(armAngle);
                float rotX = localX * cosA - localY * sinA;
                float rotY = localX * sinA + localY * cosA;

                float featX = (r.spectralCentroidNorm - 0.5f) * 0.14f;
                float featY = (r.pitchMobilityNorm - 0.5f) * 0.14f;

                float rawX = ax + rotX + featX;
                float rawY = ay + rotY + featY;

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

        static void classifySample(const juce::File& file, SampleRecord& r)
        {
            juce::String lowerName = file.getFileNameWithoutExtension().toLowerCase();
            juce::File p1 = file.getParentDirectory();
            juce::File p2 = p1.getParentDirectory();
            juce::File p3 = p2.getParentDirectory();

            // Объединяем имя файла и 3 папки выше для точного определения по директориям
            juce::String searchCtx = lowerName + " " +
                                     p1.getFileName().toLowerCase() + " " +
                                     p2.getFileName().toLowerCase() + " " +
                                     p3.getFileName().toLowerCase();

            auto hasAny = [&](std::initializer_list<const char*> words) -> bool
            {
                for (const char* w : words)
                {
                    if (searchCtx.contains(w))
                        return true;
                }
                return false;
            };

            // 1. Сначала проверяем явные эффекты в имени самого файла
            bool explicitFxInName = lowerName.contains("riser") ||
                                    lowerName.contains("downlifter") ||
                                    lowerName.contains("uplifter") ||
                                    lowerName.contains("sweep") ||
                                    lowerName.contains("whoosh") ||
                                    lowerName.contains("impact") ||
                                    lowerName.contains("foley") ||
                                    lowerName.contains("sfx") ||
                                    lowerName.contains("laser") ||
                                    lowerName.contains("zap");

            if (explicitFxInName)
            {
                r.cluster = ClusterGroup::FX;
            }
            else if (hasAny({ "bass", "sub", "808", "reese", "donk" }))
            {
                r.cluster = ClusterGroup::Bass;
            }
            else if (hasAny({ "vox", "vocal", "choir", "voice", "chant",
                              "adlib", "speech", "acappella", "singer" }))
            {
                r.cluster = ClusterGroup::Vocal;
            }
            else if (hasAny({ "string", "cello", "violin", "viola", "contrabass",
                              "orchestra", "orch", "brass", "horn", "sax",
                              "trumpet", "trombone", "tuba", "flute", "piccolo",
                              "oboe", "clarinet", "bassoon", "woodwind", "winds",
                              "wind", "harmonica", "accordion", "duduk", "erhu",
                              "ensemble", "symphon" }))
            {
                // Все струнные, медные и деревянные духовые направляем в оркестровый кластер
                r.cluster = ClusterGroup::Strings;
            }
            else if (hasAny({ "guitar", "gtr", "strum", "ukulele", "banjo",
                              "mandolin", "harpe", "harp", "lute", "sitar", "koto" }))
            {
                r.cluster = ClusterGroup::Guitar;
            }
            else if (hasAny({ "key", "rhodes", "piano", "wurli", "organ", "clav",
                              "harpsichord", "marimba", "kalimba", "xylophone",
                              "vibraphone", "vibes", "celesta", "glock", "mallet",
                              "epiano", "grand", "upright", "toy_piano" }))
            {
                r.cluster = ClusterGroup::Keys;
            }
            else if (hasAny({ "synth", "pad", "lead", "arp", "acid", "saw",
                              "square", "pluck", "chord", "stab", "poly",
                              "analog", "modular", "juno", "prophet", "serum",
                              "diva", "sylenth", "massive", "oberheim", "moog" }))
            {
                r.cluster = ClusterGroup::Synth;
            }
            else if (hasAny({ "fx", "hit", "noise", "glitch", "siren",
                              "alarm", "atmos", "ambience", "drone", "texture" }))
            {
                r.cluster = ClusterGroup::FX;
            }
            else
            {
                // Если в имени и папках нет подсказок, решаем по акустике (не сваливая всё в FX)
                if (r.pitchConfidence < 0.30f && r.harmonicRoughness > 0.68f)
                    r.cluster = ClusterGroup::FX;
                else if (r.spectralCentroidNorm < 0.26f)
                    r.cluster = ClusterGroup::Bass;
                else if (r.attackRatio > 0.65f)
                    r.cluster = ClusterGroup::Keys;
                else
                    r.cluster = ClusterGroup::Synth;
            }

            uint32_t mask = Sub_None;

            if (r.harmonicRoughness < 0.25f)
                mask |= Sub_Clean;
            else
                mask |= Sub_Noisy;

            if (r.spectralCentroidNorm < 0.48f ||
                hasAny({ "brass", "horn", "sax", "warm", "analog", "trombone" }))
            {
                mask |= Sub_Warm;
            }

            if (r.attackRatio > 0.62f ||
                hasAny({ "pluck", "pizz", "chop", "stacc", "spicc", "short" }))
            {
                mask |= Sub_Pluck;
            }
            else
            {
                mask |= Sub_Sustain;
            }

            if (lowerName.contains("pad") ||
                (r.cluster == ClusterGroup::Synth && (mask & Sub_Sustain) &&
                 !lowerName.contains("lead")))
            {
                mask |= Sub_Pad;
            }

            if (lowerName.contains("lead") || lowerName.contains("acid") ||
                (r.cluster == ClusterGroup::Synth && r.spectralCentroidNorm > 0.6f))
            {
                mask |= Sub_Lead;
            }

            if (lowerName.contains("riser") || lowerName.contains("sweep") ||
                lowerName.contains("uplifter"))
            {
                mask |= Sub_RiserSweep;
            }

            if (lowerName.contains("impact") || lowerName.contains("hit") ||
                (r.cluster == ClusterGroup::FX && (mask & Sub_Pluck)))
            {
                mask |= Sub_ImpactHit;
            }

            if (lowerName.contains("sub") ||
                (r.cluster == ClusterGroup::Bass && r.spectralCentroidNorm < 0.28f))
            {
                mask |= Sub_SubBass;
            }

            if (lowerName.contains("bell") || lowerName.contains("metal") ||
                (r.cluster == ClusterGroup::FX && (mask & Sub_Pluck)))
            {
                mask |= Sub_BellMetallic;
            }

            if (lowerName.contains("texture") || lowerName.contains("gran") ||
                lowerName.contains("air") || r.pitchMobilityNorm > 0.58f)
            {
                mask |= Sub_Textural;
            }

            r.subMask = mask;
            // relaxMapCoordinates re-runs this function for positioning.
            // Preserve the conservative identity decision after every pass,
            // including when the input came from a legacy cache.
            if (r.identityState == IdentityState::Identified)
                r.cluster = r.identityCluster;
            else if (r.identityState != IdentityState::NotEvaluated)
                r.cluster = ClusterGroup::Other;
            InstrumentSubgroupTags::evaluate(file, r);
        }

    private:
        static double estimateActiveSeconds(const float* signal, int samples,
                                            double sampleRate, double duration)
        {
            if (samples <= 0 || sampleRate <= 0.0)
                return duration;
            const int window = std::max(1, (int)(sampleRate * 0.025));
            float highest = 0.0f;
            std::vector<float> energy;
            energy.reserve((size_t)(samples / window + 1));
            for (int start = 0; start < samples; start += window)
            {
                double power = 0.0;
                const int count = std::min(window, samples - start);
                for (int j = 0; j < count; ++j)
                {
                    const float value = signal[start + j];
                    power += (double)value * value;
                }
                const float rms = (float)std::sqrt(power / count);
                energy.push_back(rms);
                highest = std::max(highest, rms);
            }
            const float threshold = std::max(0.0005f, highest * 0.032f);
            int lastActive = -1;
            for (int i = 0; i < (int)energy.size(); ++i)
                if (energy[(size_t)i] >= threshold)
                    lastActive = i;
            double active = lastActive < 0 ? duration
                : std::min(duration,
                    (double)(lastActive + 1) * (double)window / sampleRate);
            // A six-second excerpt can't know when a later sustained loop
            // ends; conservatively keep its full duration.
            if (duration > (double)samples / sampleRate + 0.02
                && lastActive >= (int)energy.size() - 2)
                active = duration;
            return std::max(0.06, active);
        }

        static void detectPitchAutocorr(const float* data, int numSamples, double sr,
                                         int& outMidi, float& outConf)
        {
            const int window = std::min(numSamples, 2048);
            const int startOffset = std::min(
                std::max(0, numSamples / 6), numSamples - window);
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
            int midi = (int)std::round(
                69.0f + 12.0f * std::log2(std::max(20.0f, freq) / 440.0f));
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
    };
}
