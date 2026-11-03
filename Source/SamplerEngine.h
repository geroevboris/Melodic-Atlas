#pragma once
#include "AtlasTypes.h"
#include <unordered_map>

namespace Atlas
{
    struct CachedAudioData
    {
        juce::AudioBuffer<float> buffer;
        double sampleRate = 44100.0;
        std::vector<float> waveformPeaks;
    };

    class SamplerEngine
    {
    public:
        SamplerEngine()
        {
            formatManager.registerBasicFormats();
            for (int i = 0; i < 12; ++i)
            {
                padStates[(size_t)i].sampleIndex = -1;
                padStates[(size_t)i].chromaticActive = (i == 0); // По умолчанию жёлтая точка на 1-м пэде
                padStates[(size_t)i].exportProcessed = true;     // По умолчанию режим [P] (Processed)
            }
        }

        void prepare(double sampleRate, int samplesPerBlock)
        {
            const juce::ScopedLock sl(engineLock);
            currentSampleRate = (sampleRate > 0.0) ? sampleRate : 44100.0;

            juce::dsp::ProcessSpec spec;
            spec.sampleRate = currentSampleRate;
            spec.maximumBlockSize = (juce::uint32)std::max(64, samplesPerBlock);
            spec.numChannels = 2;

            mapReverb.prepare(spec);
            bankReverb.prepare(spec);
            updateReverbParameters();

            mapRevBuffer.setSize(2, std::max(512, samplesPerBlock * 2));
            bankRevBuffer.setSize(2, std::max(512, samplesPerBlock * 2));
        }

        void setSampleDatabase(const std::vector<SampleRecord>* dbPtr)
        {
            const juce::ScopedLock sl(engineLock);
            database = dbPtr;
        }

        void setMusicalContext(int keyRoot, ScaleType scale, PitchMode pMode,
                               int octShift, double bpm, bool bpmLocked,
                               bool hostPlaying, double hostPpq)
        {
            projectKeyRoot = keyRoot;
            projectScale = scale;
            pitchMode = pMode;
            octaveShift = octShift;
            currentBpm = std::max(40.0, bpm);
            syncBpm = bpmLocked;
            isHostPlaying = hostPlaying;
            currentPpq = hostPpq;
        }

        void setEngineControls(BankMode bMode, float glideSec, float mapRev,
                               float bankRev, float previewTuneSemis,
                               TransposeQuality tQuality, float masterDb)
        {
            bankMode = bMode;
            glideTimeSec = juce::jlimit(0.0f, 1.0f, glideSec);
            mapReverbAmt = juce::jlimit(0.0f, 1.0f, mapRev);
            bankReverbAmt = juce::jlimit(0.0f, 1.0f, bankRev);
            previewPitchSemitones = previewTuneSemis;
            transposeQuality = tQuality;
            masterGainLinear = juce::Decibels::decibelsToGain(masterDb, -60.0f);
            updateReverbParameters();
        }

        // --- Предпрослушивание на карте (двухголосный анти-клик кроссфейдер) ---
        void startPreview(int sampleIdx, const SampleRecord& rec)
        {
            auto audio = loadOrGetAudio(rec.file);
            if (audio == nullptr || audio->buffer.getNumSamples() <= 4)
                return;

            const juce::ScopedLock sl(engineLock);

            // Переключаем активный слот превью, а предыдущий плавно уводим в фейд-аут за 15 мс (без кликов)
            int prevSlot = activePreviewSlot;
            if (previewVoices[prevSlot].active)
            {
                previewVoices[prevSlot].fadingOut = true;
                previewVoices[prevSlot].holding = false;
                float fadeSamples = (float)(currentSampleRate * 0.015);
                previewVoices[prevSlot].fadeOutStep = previewVoices[prevSlot].envGain / std::max(1.0f, fadeSamples);
            }

            activePreviewSlot = 1 - prevSlot;
            auto& v = previewVoices[activePreviewSlot];

            v.audio = audio;
            v.record = rec;
            v.sampleIndex = sampleIdx;
            v.position = 0.0; // Строго с начала дорожки при каждом клике!
            v.elapsedSamples = 0;
            v.active = true;
            v.holding = true;
            v.fadingOut = false;
            v.envGain = 0.0f;

            // Мягкий анти-клик старт (4 мс)
            float fadeInSamples = (float)(currentSampleRate * 0.004);
            v.fadeInStep = 1.0f / std::max(1.0f, fadeInSamples);

            // Минимальное время звучания при коротком клике (чтобы звук не глох мгновенно)
            double minPlaySec = rec.isLoop ? 2.2 : std::min(3.5, rec.durationSec);
            v.minHoldSamples = (int64_t)(currentSampleRate * minPlaySec);

            previewPlayheadNorm.store(0.0f);
            previewActiveFlag.store(true);
        }

        void stopPreview()
        {
            const juce::ScopedLock sl(engineLock);
            auto& v = previewVoices[activePreviewSlot];
            if (v.active)
            {
                // Снимаем флаг удержания: голос доиграет свой минимальный музыкальный отрезок и мягко затухнет
                v.holding = false;
            }
        }

        float getPreviewPlayheadNorm() const { return previewPlayheadNorm.load(); }
        bool isPreviewActive() const         { return previewActiveFlag.load(); }

        // --- Работа с банком из 12 пэдов ---
        void assignSampleToPad(int padIdx, int sampleIdx, const SampleRecord& rec, bool savePadHistory = true)
        {
            if (padIdx < 0 || padIdx >= 12) return;
            auto audio = loadOrGetAudio(rec.file);

            const juce::ScopedLock sl(engineLock);
            auto& st = padStates[(size_t)padIdx];

            if (savePadHistory && st.sampleIndex >= 0 && padRecords[(size_t)padIdx].file.existsAsFile())
            {
                PadHistoryEntry entry;
                entry.sampleIndex = st.sampleIndex;
                entry.record = padRecords[(size_t)padIdx];
                entry.semitoneOffset = st.semitoneOffset;
                entry.centOffset = st.centOffset;
                entry.volumeDb = st.volumeDb;
                entry.pan = st.pan;
                entry.regionStart = st.regionStart;
                entry.regionEnd = st.regionEnd;
                entry.chromaticActive = st.chromaticActive;
                entry.exportProcessed = st.exportProcessed;

                st.history.push_back(entry);
                if (st.history.size() > 24)
                    st.history.erase(st.history.begin());
            }

            // Если это первый назначенный пэд и ни один пэд ещё не помечен жёлтой точкой Chromatic — включаем её
            bool anyChromatic = false;
            for (int i = 0; i < 12; ++i)
                if (padStates[(size_t)i].sampleIndex >= 0 && padStates[(size_t)i].chromaticActive)
                    anyChromatic = true;

            st.sampleIndex = sampleIdx;
            if (!anyChromatic)
                st.chromaticActive = true;

            padRecords[(size_t)padIdx] = rec;
            padAudio[(size_t)padIdx] = audio;
        }

        void undoPadSample(int padIdx)
        {
            const juce::ScopedLock sl(engineLock);
            if (padIdx < 0 || padIdx >= 12) return;
            auto& st = padStates[(size_t)padIdx];
            if (st.history.empty()) return;

            auto last = st.history.back();
            st.history.pop_back();

            st.sampleIndex = last.sampleIndex;
            st.semitoneOffset = last.semitoneOffset;
            st.centOffset = last.centOffset;
            st.volumeDb = last.volumeDb;
            st.pan = last.pan;
            st.regionStart = last.regionStart;
            st.regionEnd = last.regionEnd;
            st.chromaticActive = last.chromaticActive;
            st.exportProcessed = last.exportProcessed;

            padRecords[(size_t)padIdx] = last.record;
            padAudio[(size_t)padIdx] = loadOrGetAudio(last.record.file);
        }

        void clearAllPads()
        {
            const juce::ScopedLock sl(engineLock);
            for (int i = 0; i < 12; ++i)
            {
                padStates[(size_t)i].sampleIndex = -1;
                padAudio[(size_t)i].reset();
            }
            for (auto& v : voices)
                v.active = false;
        }

        // --- Глобальная история проекта (Snapshots для Undo / Redo) ---
        ProjectSnapshot createSnapshot(int primaryPad, bool globalExpProc) const
        {
            const juce::ScopedLock sl(engineLock);
            ProjectSnapshot snap;
            snap.pads = padStates;
            snap.primarySelectedPad = primaryPad;
            snap.globalExportProcessed = globalExpProc;
            return snap;
        }

        void restoreSnapshot(const ProjectSnapshot& snap)
        {
            const juce::ScopedLock sl(engineLock);
            padStates = snap.pads;
            for (int i = 0; i < 12; ++i)
            {
                int sIdx = padStates[(size_t)i].sampleIndex;
                if (database != nullptr && sIdx >= 0 && sIdx < (int)database->size())
                {
                    padRecords[(size_t)i] = (*database)[(size_t)sIdx];
                    padAudio[(size_t)i] = loadOrGetAudio(padRecords[(size_t)i].file);
                }
                else
                {
                    padStates[(size_t)i].sampleIndex = -1;
                    padAudio[(size_t)i].reset();
                }
            }
        }

        PadSlotState& getPadState(int idx)             { return padStates[(size_t)juce::jlimit(0, 11, idx)]; }
        const PadSlotState& getPadState(int idx) const { return padStates[(size_t)juce::jlimit(0, 11, idx)]; }

        const SampleRecord* getPadRecord(int idx) const
        {
            if (idx < 0 || idx >= 12 || padStates[(size_t)idx].sampleIndex < 0)
                return nullptr;
            return &padRecords[(size_t)idx];
        }

        std::shared_ptr<CachedAudioData> getPadAudio(int idx) const
        {
            if (idx < 0 || idx >= 12 || padStates[(size_t)idx].sampleIndex < 0)
                return nullptr;
            return padAudio[(size_t)idx];
        }

        // Клик мышкой по пэду в UI — проигрывает сам пэд с его настройками
        void triggerPadFromUI(int padIdx)
        {
            const juce::ScopedLock sl(engineLock);
            if (padIdx < 0 || padIdx >= 12) return;
            triggerPadVoice(padIdx, 60, 0.9f, 0.0f);
        }

        uint32_t getActivePlayingPadsMask() const { return activePadsBitmask.load(); }

        // --- Рендер сэмпла пэда в WAV-файл с учётом всех преднастроек (для перетягивания в DAW) ---
        juce::File renderPadForExport(int padIdx)
        {
            const juce::ScopedLock sl(engineLock);
            if (padIdx < 0 || padIdx >= 12) return {};

            const auto& st = padStates[(size_t)padIdx];
            if (st.sampleIndex < 0) return {};

            const auto& rec = padRecords[(size_t)padIdx];
            auto audio = padAudio[(size_t)padIdx];
            if (audio == nullptr || audio->buffer.getNumSamples() <= 4)
                return rec.file;

            // Если на пэде выбран режим [D] (Dry) — отдаём чистый исходный файл без обработки
            if (!st.exportProcessed)
                return rec.file;

            // Режим [P] (Processed): рендерим с учётом питча, центов, региона (Start/End), громкости, панорамы и ADSR
            const int srcTotalSamples = audio->buffer.getNumSamples();
            const double sr = (audio->sampleRate > 0.0) ? audio->sampleRate : currentSampleRate;

            int startSample = juce::jlimit(0, srcTotalSamples - 2, (int)std::floor(st.regionStart * (float)srcTotalSamples));
            int endSample   = juce::jlimit(startSample + 2, srcTotalSamples, (int)std::ceil(st.regionEnd * (float)srcTotalSamples));
            int regionLen   = endSample - startSample;

            double totalSemis = (double)st.semitoneOffset + (double)st.centOffset * 0.01;
            if (rec.isLoop && syncBpm && rec.detectedBpm > 40.0)
            {
                double bpmRatio = currentBpm / rec.detectedBpm;
                totalSemis += 12.0 * std::log2(std::max(0.25, std::min(4.0, bpmRatio)));
            }

            double speedRatio = std::pow(2.0, totalSemis / 12.0);
            speedRatio = juce::jlimit(0.125, 8.0, speedRatio);

            int outSamples = std::max(64, (int)std::ceil((double)regionLen / speedRatio));
            juce::AudioBuffer<float> outBuf(2, outSamples);
            outBuf.clear();

            float gainLin = juce::Decibels::decibelsToGain(st.volumeDb, -60.0f) * rec.normGainLinear;
            float panAngle = (st.pan + 1.0f) * 0.5f * juce::MathConstants<float>::halfPi;
            float gainL = std::cos(panAngle) * gainLin;
            float gainR = std::sin(panAngle) * gainLin;

            const int numSrcCh = audio->buffer.getNumChannels();
            const float* srcL = audio->buffer.getReadPointer(0);
            const float* srcR = (numSrcCh > 1) ? audio->buffer.getReadPointer(1) : srcL;

            float* dstL = outBuf.getWritePointer(0);
            float* dstR = outBuf.getWritePointer(1);

            int fadeInLen  = std::min(outSamples / 4, std::max(32, (int)(st.attackSec * sr)));
            int fadeOutLen = std::min(outSamples / 3, std::max(64, (int)(0.015 * sr)));

            double readPos = (double)startSample;
            for (int i = 0; i < outSamples; ++i)
            {
                int idx0 = (int)readPos;
                int idx1 = std::min(endSample - 1, idx0 + 1);
                float frac = (float)(readPos - (double)idx0);

                idx0 = juce::jlimit(startSample, endSample - 1, idx0);
                float sL = srcL[idx0] + frac * (srcL[idx1] - srcL[idx0]);
                float sR = srcR[idx0] + frac * (srcR[idx1] - srcR[idx0]);

                float env = 1.0f;
                if (i < fadeInLen)
                    env = (float)i / (float)std::max(1, fadeInLen);
                else if (i >= outSamples - fadeOutLen)
                    env = (float)(outSamples - 1 - i) / (float)std::max(1, fadeOutLen);

                dstL[i] = sL * gainL * env;
                dstR[i] = sR * gainR * env;

                readPos += speedRatio;
                if (readPos >= (double)endSample)
                    break;
            }

            juce::File exportDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                       .getChildFile("MelodicAtlas_ExportedAudio");
            exportDir.createDirectory();

            juce::String noteTag = MusicMath::formatPadNoteAndCents(rec.pitchClass, st.semitoneOffset, st.centOffset)
                                       .replaceCharacter(' ', '_')
                                       .replaceCharacter('#', 's');
            juce::String outName = rec.name + "_" + noteTag + "_Proc.wav";
            juce::File outFile = exportDir.getChildFile(juce::File::createLegalFileName(outName));
            if (outFile.existsAsFile())
                outFile.deleteFile();

            juce::WavAudioFormat wavFormat;
            if (auto stream = std::unique_ptr<juce::FileOutputStream>(outFile.createOutputStream()))
            {
                if (auto writer = std::unique_ptr<juce::AudioFormatWriter>(
                        wavFormat.createWriterFor(stream.get(), sr, 2, 24, {}, 0)))
                {
                    stream.release();
                    writer->writeFromAudioSampleBuffer(outBuf, 0, outSamples);
                    return outFile;
                }
            }

            return rec.file;
        }

        // --- Основной аудио-блок ---
        void renderNextBlock(juce::AudioBuffer<float>& outputBuffer, const juce::MidiBuffer& midiMessages)
        {
            const juce::ScopedLock sl(engineLock);

            for (const auto metadata : midiMessages)
            {
                const auto msg = metadata.getMessage();
                if (msg.isNoteOn())
                    handleMidiNoteOn(msg.getNoteNumber(), msg.getFloatVelocity());
                else if (msg.isNoteOff())
                    handleMidiNoteOff(msg.getNoteNumber());
                else if (msg.isAllNotesOff() || msg.isAllSoundOff())
                    for (auto& v : voices) v.releasing = true;
            }

            const int numSamples = outputBuffer.getNumSamples();
            if (numSamples <= 0) return;

            mapRevBuffer.clear();
            bankRevBuffer.clear();

            float* outL = outputBuffer.getWritePointer(0);
            float* outR = outputBuffer.getNumChannels() > 1 ? outputBuffer.getWritePointer(1) : outL;

            float* mapRevL = mapRevBuffer.getWritePointer(0);
            float* mapRevR = mapRevBuffer.getWritePointer(1);

            float* bankRevL = bankRevBuffer.getWritePointer(0);
            float* bankRevR = bankRevBuffer.getWritePointer(1);

            // 1. Рендер двухголосного превью карты (с анти-клик кроссфейдом)
            bool anyPreviewStillActive = false;
            for (int slot = 0; slot < 2; ++slot)
            {
                auto& pv = previewVoices[slot];
                if (!pv.active || pv.audio == nullptr)
                    continue;

                const auto& buf = pv.audio->buffer;
                const int totalFrames = buf.getNumSamples();
                if (totalFrames <= 4)
                {
                    pv.active = false;
                    continue;
                }

                const float* srcL = buf.getReadPointer(0);
                const float* srcR = buf.getNumChannels() > 1 ? buf.getReadPointer(1) : srcL;

                int semiShift = MusicMath::calculatePitchShift(pv.record.pitchClass, projectKeyRoot, projectScale, pitchMode, octaveShift);
                double totalSemis = (double)semiShift + (double)previewPitchSemitones;

                double pitchRatio = std::pow(2.0, totalSemis / 12.0);
                double srRatio = pv.audio->sampleRate / currentSampleRate;
                double step = pitchRatio * srRatio;

                if (pv.record.isLoop && syncBpm && pv.record.detectedBpm > 40.0)
                {
                    double bpmRatio = currentBpm / pv.record.detectedBpm;
                    step *= juce::jlimit(0.5, 2.0, bpmRatio);
                }

                // Мягкое затухание после отпускания мыши (только когда минимальная длина проиграна)
                float releaseFadeStep = (float)(1.0 / std::max(1.0, currentSampleRate * 0.18));
                int endFadeFrames = std::min(totalFrames / 5, (int)(pv.audio->sampleRate * 0.04));

                for (int i = 0; i < numSamples; ++i)
                {
                    if (!pv.fadingOut && pv.envGain < 1.0f)
                        pv.envGain = std::min(1.0f, pv.envGain + pv.fadeInStep);

                    if (!pv.holding && pv.elapsedSamples >= pv.minHoldSamples && !pv.fadingOut)
                    {
                        pv.fadingOut = true;
                        pv.fadeOutStep = releaseFadeStep;
                    }

                    if (pv.fadingOut)
                    {
                        pv.envGain -= pv.fadeOutStep;
                        if (pv.envGain <= 0.0001f)
                        {
                            pv.envGain = 0.0f;
                            pv.active = false;
                            break;
                        }
                    }

                    int idx0 = (int)pv.position;
                    if (idx0 >= totalFrames - 1)
                    {
                        if (pv.record.isLoop && pv.holding && !pv.fadingOut)
                        {
                            pv.position = std::fmod(pv.position, (double)(totalFrames - 1));
                            idx0 = (int)pv.position;
                        }
                        else
                        {
                            pv.active = false;
                            break;
                        }
                    }

                    int idx1 = std::min(totalFrames - 1, idx0 + 1);
                    float frac = (float)(pv.position - (double)idx0);

                    float sL = srcL[idx0] + frac * (srcL[idx1] - srcL[idx0]);
                    float sR = srcR[idx0] + frac * (srcR[idx1] - srcR[idx0]);

                    // Защита от клика в самом конце файла или на границе лупа
                    float boundaryEnv = 1.0f;
                    if (!pv.record.isLoop && idx0 > totalFrames - endFadeFrames)
                        boundaryEnv = (float)(totalFrames - 1 - idx0) / (float)std::max(1, endFadeFrames);
                    else if (pv.record.isLoop && idx0 > totalFrames - 128)
                        boundaryEnv = (float)(totalFrames - 1 - idx0) / 128.0f;

                    float gain = pv.envGain * boundaryEnv * pv.record.normGainLinear * 0.75f;
                    outL[i]    += sL * gain;
                    outR[i]    += sR * gain;
                    mapRevL[i] += sL * gain;
                    mapRevR[i] += sR * gain;

                    pv.position += step;
                    pv.elapsedSamples++;
                }

                if (slot == activePreviewSlot && pv.active)
                {
                    float normPos = juce::jlimit(0.0f, 1.0f, (float)(pv.position / (double)std::max(1, totalFrames)));
                    previewPlayheadNorm.store(normPos);
                    anyPreviewStillActive = true;
                }
            }
            previewActiveFlag.store(anyPreviewStillActive);

            // 2. Рендер голосов банка (12 пэдов: PadTrigger и Chromatic)
            uint32_t playingMask = 0;
            for (auto& v : voices)
            {
                if (!v.active || v.audio == nullptr || v.padIndex < 0 || v.padIndex >= 12)
                    continue;

                const auto& st = padStates[(size_t)v.padIndex];
                const auto& rec = padRecords[(size_t)v.padIndex];
                const auto& buf = v.audio->buffer;
                const int totalFrames = buf.getNumSamples();

                int startFrame = juce::jlimit(0, totalFrames - 2, (int)(st.regionStart * (float)totalFrames));
                int endFrame   = juce::jlimit(startFrame + 2, totalFrames, (int)(st.regionEnd * (float)totalFrames));

                const float* srcL = buf.getReadPointer(0);
                const float* srcR = buf.getNumChannels() > 1 ? buf.getReadPointer(1) : srcL;

                float padGain = juce::Decibels::decibelsToGain(st.volumeDb, -60.0f) * rec.normGainLinear * v.velocity;
                float panAngle = (st.pan + 1.0f) * 0.5f * juce::MathConstants<float>::halfPi;
                float gainL = std::cos(panAngle) * padGain;
                float gainR = std::sin(panAngle) * padGain;

                float attStep = (float)(1.0 / std::max(1.0, currentSampleRate * std::max(0.002f, st.attackSec)));
                float decStep = (float)((1.0 - st.sustainLvl) / std::max(1.0, currentSampleRate * std::max(0.01f, st.decaySec)));
                float relStep = (float)(1.0 / std::max(1.0, currentSampleRate * std::max(0.015f, st.releaseSec)));

                double glideCoeff = (glideTimeSec > 0.002f)
                    ? (1.0 - std::exp(-1.0 / (currentSampleRate * glideTimeSec)))
                    : 1.0;

                double srRatio = v.audio->sampleRate / currentSampleRate;

                for (int i = 0; i < numSamples; ++i)
                {
                    v.currentNoteShift += (v.targetNoteShift - v.currentNoteShift) * glideCoeff;

                    // Полная высота тона = настройка пэда (TUNE + CENT) + хроматический сдвиг от клавиши MIDI
                    double totalSemis = (double)st.semitoneOffset + (double)st.centOffset * 0.01 + v.currentNoteShift;
                    double step = std::pow(2.0, totalSemis / 12.0) * srRatio;

                    if (v.releasing)
                    {
                        v.envLevel -= relStep;
                        if (v.envLevel <= 0.0001f)
                        {
                            v.envLevel = 0.0f;
                            v.active = false;
                            break;
                        }
                    }
                    else if (v.inAttack)
                    {
                        v.envLevel += attStep;
                        if (v.envLevel >= 1.0f)
                        {
                            v.envLevel = 1.0f;
                            v.inAttack = false;
                        }
                    }
                    else
                    {
                        v.envLevel = std::max(st.sustainLvl, v.envLevel - decStep);
                    }

                    int idx0 = (int)v.position;
                    if (idx0 >= endFrame - 1)
                    {
                        if (rec.isLoop && !v.releasing)
                        {
                            v.position = (double)startFrame;
                            idx0 = startFrame;
                        }
                        else
                        {
                            v.active = false;
                            break;
                        }
                    }

                    int idx1 = std::min(endFrame - 1, idx0 + 1);
                    float frac = (float)(v.position - (double)idx0);

                    float sL = srcL[idx0] + frac * (srcL[idx1] - srcL[idx0]);
                    float sR = srcR[idx0] + frac * (srcR[idx1] - srcR[idx0]);

                    // Мягкий микро-фейд на краю региона против щелчков
                    float edgeFade = 1.0f;
                    int remain = endFrame - 1 - idx0;
                    if (remain < 96)
                        edgeFade = (float)remain / 96.0f;

                    float l = sL * gainL * v.envLevel * edgeFade;
                    float r = sR * gainR * v.envLevel * edgeFade;

                    outL[i]     += l;
                    outR[i]     += r;
                    bankRevL[i] += l;
                    bankRevR[i] += r;

                    v.position += step;
                }

                if (v.active)
                    playingMask |= (1u << v.padIndex);
            }
            activePadsBitmask.store(playingMask);

            // 3. Применение раздельных шин реверберации (Map Reverb и Bank Reverb)
            if (mapReverbAmt > 0.005f)
            {
                juce::dsp::AudioBlock<float> block(mapRevBuffer.getArrayOfWritePointers(), 2, (size_t)numSamples);
                juce::dsp::ProcessContextReplacing<float> ctx(block);
                mapReverb.process(ctx);

                for (int i = 0; i < numSamples; ++i)
                {
                    outL[i] += mapRevL[i];
                    outR[i] += mapRevR[i];
                }
            }

            if (bankReverbAmt > 0.005f)
            {
                juce::dsp::AudioBlock<float> block(bankRevBuffer.getArrayOfWritePointers(), 2, (size_t)numSamples);
                juce::dsp::ProcessContextReplacing<float> ctx(block);
                bankReverb.process(ctx);

                for (int i = 0; i < numSamples; ++i)
                {
                    outL[i] += bankRevL[i];
                    outR[i] += bankRevR[i];
                }
            }

            // 4. Мастер-громкость (MAIN) и мягкий лимитер
            for (int i = 0; i < numSamples; ++i)
            {
                outL[i] = std::tanh(outL[i] * masterGainLinear);
                outR[i] = std::tanh(outR[i] * masterGainLinear);
            }
        }

    private:
        struct AuditionVoice
        {
            std::shared_ptr<CachedAudioData> audio;
            SampleRecord record;
            int sampleIndex = -1;
            double position = 0.0;
            int64_t elapsedSamples = 0;
            int64_t minHoldSamples = 44100;
            float envGain = 0.0f;
            float fadeInStep = 0.01f;
            float fadeOutStep = 0.001f;
            bool active = false;
            bool holding = false;
            bool fadingOut = false;
        };

        struct SynthVoice
        {
            bool active = false;
            bool inAttack = true;
            bool releasing = false;
            int padIndex = -1;
            int midiNote = 60;
            float velocity = 1.0f;
            float envLevel = 0.0f;
            double position = 0.0;
            double currentNoteShift = 0.0;
            double targetNoteShift = 0.0;
            std::shared_ptr<CachedAudioData> audio;
        };

        void updateReverbParameters()
        {
            juce::dsp::Reverb::Parameters mp;
            mp.roomSize = 0.68f;
            mp.damping = 0.42f;
            mp.wetLevel = mapReverbAmt * 0.55f;
            mp.dryLevel = 0.0f;
            mp.width = 0.95f;
            mapReverb.setParameters(mp);

            juce::dsp::Reverb::Parameters bp;
            bp.roomSize = 0.72f;
            bp.damping = 0.38f;
            bp.wetLevel = bankReverbAmt * 0.58f;
            bp.dryLevel = 0.0f;
            bp.width = 1.0f;
            bankReverb.setParameters(bp);
        }

        void handleMidiNoteOn(int midiNote, float velocity)
        {
            if (bankMode == BankMode::PadTrigger)
            {
                // Поддержка любой октавы (C1..B1, C3..B3 и т.д.) для 12 пэдов
                int padIdx = ((midiNote % 12) + 12) % 12;
                triggerPadVoice(padIdx, midiNote, velocity, 0.0f);
            }
            else
            {
                // Режим CHROMATIC: определяем базовую ноту (36 для C1 или 60 для C3), чтобы всегда звучало в рабочей октаве
                int baseNote = (midiNote < 48) ? 36 : 60;
                float chromaticShiftSemis = (float)(midiNote - baseNote);

                bool anyTriggered = false;
                for (int i = 0; i < 12; ++i)
                {
                    if (padStates[(size_t)i].sampleIndex >= 0 && padStates[(size_t)i].chromaticActive)
                    {
                        triggerPadVoice(i, midiNote, velocity, chromaticShiftSemis);
                        anyTriggered = true;
                    }
                }

                // Если ни на одном пэде не горит жёлтая точка — играем первым заполненным пэдом
                if (!anyTriggered)
                {
                    for (int i = 0; i < 12; ++i)
                    {
                        if (padStates[(size_t)i].sampleIndex >= 0)
                        {
                            triggerPadVoice(i, midiNote, velocity, chromaticShiftSemis);
                            break;
                        }
                    }
                }
            }
            lastPlayedChromaticShift = (float)(midiNote - ((midiNote < 48) ? 36 : 60));
            hasLastChromaticNote = true;
        }

        void handleMidiNoteOff(int midiNote)
        {
            for (auto& v : voices)
            {
                if (v.active && !v.releasing && v.midiNote == midiNote)
                    v.releasing = true;
            }
        }

        void triggerPadVoice(int padIdx, int midiNote, float velocity, float chromaticSemitoneShift)
        {
            if (padIdx < 0 || padIdx >= 12) return;
            const auto& st = padStates[(size_t)padIdx];
            if (st.sampleIndex < 0) return;

            auto audio = padAudio[(size_t)padIdx];
            if (audio == nullptr || audio->buffer.getNumSamples() <= 4) return;

            SynthVoice* chosen = nullptr;
            for (auto& v : voices)
            {
                if (!v.active)
                {
                    chosen = &v;
                    break;
                }
            }
            if (chosen == nullptr)
                chosen = &voices[0];

            int totalFrames = audio->buffer.getNumSamples();
            int startFrame = juce::jlimit(0, totalFrames - 2, (int)(st.regionStart * (float)totalFrames));

            chosen->active = true;
            chosen->inAttack = true;
            chosen->releasing = false;
            chosen->padIndex = padIdx;
            chosen->midiNote = midiNote;
            chosen->velocity = juce::jlimit(0.1f, 1.0f, velocity);
            chosen->envLevel = 0.0f;
            chosen->position = (double)startFrame;
            chosen->targetNoteShift = (double)chromaticSemitoneShift;
            chosen->currentNoteShift = (hasLastChromaticNote && glideTimeSec > 0.005f)
                                       ? (double)lastPlayedChromaticShift
                                       : (double)chromaticSemitoneShift;
            chosen->audio = audio;
        }

        std::shared_ptr<CachedAudioData> loadOrGetAudio(const juce::File& file)
        {
            if (!file.existsAsFile()) return nullptr;
            juce::String path = file.getFullPathName();

            {
                const juce::ScopedLock sl(cacheLock);
                auto it = audioCache.find(path);
                if (it != audioCache.end())
                    return it->second;
            }

            std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
            if (reader == nullptr || reader->lengthInSamples <= 0)
                return nullptr;

            auto data = std::make_shared<CachedAudioData>();
            data->sampleRate = reader->sampleRate;
            int numSamples = (int)std::min<juce::int64>(reader->lengthInSamples, (juce::int64)(reader->sampleRate * 30.0));
            data->buffer.setSize(2, numSamples);
            reader->read(&data->buffer, 0, numSamples, 0, true, true);

            const int numPeaks = 64;
            data->waveformPeaks.resize((size_t)numPeaks, 0.0f);
            int samplesPerPeak = std::max(1, numSamples / numPeaks);
            const float* ch0 = data->buffer.getReadPointer(0);

            for (int p = 0; p < numPeaks; ++p)
            {
                float maxVal = 0.0f;
                int start = p * samplesPerPeak;
                int end = std::min(numSamples, start + samplesPerPeak);
                for (int i = start; i < end; ++i)
                    maxVal = std::max(maxVal, std::abs(ch0[i]));
                data->waveformPeaks[(size_t)p] = juce::jlimit(0.05f, 1.0f, maxVal);
            }

            {
                const juce::ScopedLock sl(cacheLock);
                if (audioCache.size() > 256)
                    audioCache.clear();
                audioCache[path] = data;
            }
            return data;
        }

        juce::CriticalSection engineLock;
        juce::CriticalSection cacheLock;
        juce::AudioFormatManager formatManager;
        std::unordered_map<juce::String, std::shared_ptr<CachedAudioData>> audioCache;

        const std::vector<SampleRecord>* database = nullptr;
        double currentSampleRate = 44100.0;

        int projectKeyRoot = 9;
        ScaleType projectScale = ScaleType::NaturalMinor;
        PitchMode pitchMode = PitchMode::ScaleSnap;
        int octaveShift = 0;
        double currentBpm = 120.0;
        bool syncBpm = true;
        bool isHostPlaying = false;
        double currentPpq = 0.0;

        BankMode bankMode = BankMode::Chromatic;
        float glideTimeSec = 0.0f;
        float mapReverbAmt = 0.25f;
        float bankReverbAmt = 0.30f;
        float previewPitchSemitones = 0.0f;
        TransposeQuality transposeQuality = TransposeQuality::CleanRepitch;
        float masterGainLinear = 1.0f;

        float lastPlayedChromaticShift = 0.0f;
        bool hasLastChromaticNote = false;

        AuditionVoice previewVoices[2];
        int activePreviewSlot = 0;
        std::atomic<float> previewPlayheadNorm { 0.0f };
        std::atomic<bool> previewActiveFlag { false };

        std::array<PadSlotState, 12> padStates;
        std::array<SampleRecord, 12> padRecords;
        std::array<std::shared_ptr<CachedAudioData>, 12> padAudio;
        std::array<SynthVoice, 24> voices;
        std::atomic<uint32_t> activePadsBitmask { 0 };

        juce::dsp::Reverb mapReverb;
        juce::dsp::Reverb bankReverb;
        juce::AudioBuffer<float> mapRevBuffer;
        juce::AudioBuffer<float> bankRevBuffer;
    };
}
