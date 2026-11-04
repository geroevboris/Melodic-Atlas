#pragma once
#include "AtlasTypes.h"
#include <unordered_map>

namespace Atlas
{
    struct CachedAudioData
    {
        juce::AudioBuffer<float> buffer;
        double sampleRate = 48000.0;
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
                padStates[(size_t)i].chromaticActive = (i == 0);
                padStates[(size_t)i].exportProcessed = true;
            }
        }

        void prepare(double sampleRate, int samplesPerBlock)
        {
            const juce::ScopedLock sl(engineLock);
            currentSampleRate = (sampleRate > 0.0) ? sampleRate : 48000.0;

            juce::dsp::ProcessSpec spec;
            spec.sampleRate = currentSampleRate;
            spec.maximumBlockSize = (juce::uint32)std::max(64, samplesPerBlock);
            spec.numChannels = 2;

            mapReverb.prepare(spec);
            mapReverb.reset();
            bankReverb.prepare(spec);
            bankReverb.reset();
            updateReverbParameters();

            mapRevBuffer.setSize(2, std::max(512, samplesPerBlock * 2));
            bankRevBuffer.setSize(2, std::max(512, samplesPerBlock * 2));
            mapRevBuffer.clear();
            bankRevBuffer.clear();
        }

        void setSampleDatabase(const std::vector<SampleRecord>* dbPtr)
        {
            const juce::ScopedLock sl(engineLock);
            database = dbPtr;
        }

        void setMusicalContext(int keyRoot, ScaleType scale, PitchMode pMode,
                               int octShift, double bpm, bool bpmLocked)
        {
            projectKeyRoot = keyRoot;
            projectScale = scale;
            pitchMode = pMode;
            octaveShift = octShift;
            currentBpm = juce::jlimit(40.0, 300.0, bpm);
            syncBpm = bpmLocked;
        }

        void setEngineControls(BankMode bMode, float glideSec, float mapRev,
                               float bankRev, float previewTuneSemis,
                               TransposeQuality tQuality, float masterDb)
        {
            bankMode = bMode;
            glideTimeSec = juce::jlimit(0.0f, 1.0f, glideSec);
            mapReverbAmt = juce::jlimit(0.0f, 1.0f, mapRev);
            bankReverbAmt = juce::jlimit(0.0f, 1.0f, bankRev);
            previewPitchSemitones = juce::jlimit(-24.0f, 24.0f, previewTuneSemis);
            transposeQuality = tQuality;
            masterGainLinear = juce::Decibels::decibelsToGain(
                juce::jlimit(-48.0f, 12.0f, masterDb), -60.0f);
            updateReverbParameters();
        }

        void startPreview(int sampleIdx, const SampleRecord& rec)
        {
            auto audio = loadOrGetAudio(rec.file);
            if (audio == nullptr || audio->buffer.getNumSamples() <= 8)
                return;

            const juce::ScopedLock sl(engineLock);

            // Уводим все предыдущие голоса предпрослушки в мягкий фейд-аут (12 мс)
            float fadeOutSamples = (float)std::max(32.0, currentSampleRate * 0.012);
            for (int s = 0; s < 4; ++s)
            {
                if (previewVoices[s].active && !previewVoices[s].fadingOut)
                {
                    previewVoices[s].fadingOut = true;
                    previewVoices[s].fadeOutStep =
                        std::max(0.0005f, previewVoices[s].envGain / fadeOutSamples);
                }
            }

            activePreviewSlot = (activePreviewSlot + 1) % 4;
            auto& v = previewVoices[activePreviewSlot];

            v.audio = audio;
            v.record = rec;
            v.sampleIndex = sampleIdx;
            v.position = 0.0;
            v.active = true;
            v.fadingOut = false;
            v.envGain = 0.0f;

            float fadeInSamples = (float)std::max(24.0, currentSampleRate * 0.004);
            v.fadeInStep = 1.0f / fadeInSamples;
        }

        void stopPreview()
        {
            const juce::ScopedLock sl(engineLock);
            float fadeSamples = (float)std::max(48.0, currentSampleRate * 0.018);
            for (int s = 0; s < 4; ++s)
            {
                auto& v = previewVoices[s];
                if (v.active && !v.fadingOut)
                {
                    v.fadingOut = true;
                    v.fadeOutStep = std::max(0.0002f, v.envGain / fadeSamples);
                }
            }
        }

        void assignSampleToPad(int padIdx, int sampleIdx,
                               const SampleRecord& rec, bool savePadHistory = true)
        {
            if (padIdx < 0 || padIdx >= 12)
                return;

            auto audio = loadOrGetAudio(rec.file);

            const juce::ScopedLock sl(engineLock);
            auto& st = padStates[(size_t)padIdx];

            if (savePadHistory && st.sampleIndex >= 0 &&
                padRecords[(size_t)padIdx].file.existsAsFile())
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
                entry.attackSec = st.attackSec;
                entry.decaySec = st.decaySec;
                entry.sustainLvl = st.sustainLvl;
                entry.releaseSec = st.releaseSec;

                st.history.push_back(entry);
                if (st.history.size() > 24)
                    st.history.erase(st.history.begin());
            }

            bool anyChromatic = false;
            for (int i = 0; i < 12; ++i)
            {
                if (padStates[(size_t)i].sampleIndex >= 0 &&
                    padStates[(size_t)i].chromaticActive)
                {
                    anyChromatic = true;
                }
            }

            st.sampleIndex = sampleIdx;
            if (!anyChromatic)
                st.chromaticActive = true;

            padRecords[(size_t)padIdx] = rec;
            padAudio[(size_t)padIdx] = audio;
        }

        void undoPadSample(int padIdx)
        {
            const juce::ScopedLock sl(engineLock);
            if (padIdx < 0 || padIdx >= 12)
                return;

            auto& st = padStates[(size_t)padIdx];
            if (st.history.empty())
                return;

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
            st.attackSec = last.attackSec;
            st.decaySec = last.decaySec;
            st.sustainLvl = last.sustainLvl;
            st.releaseSec = last.releaseSec;

            padRecords[(size_t)padIdx] = last.record;
            padAudio[(size_t)padIdx] = loadOrGetAudio(last.record.file);
        }

        void clearAllPads()
        {
            const juce::ScopedLock sl(engineLock);
            for (int i = 0; i < 12; ++i)
            {
                padStates[(size_t)i].sampleIndex = -1;
                padStates[(size_t)i].settingsViewOpen = false;
                padAudio[(size_t)i].reset();
            }
            for (auto& v : voices)
                v.active = false;
        }

        void clearPad(int padIdx)
        {
            const juce::ScopedLock sl(engineLock);
            if (padIdx >= 0 && padIdx < 12)
            {
                padStates[(size_t)padIdx].sampleIndex = -1;
                padStates[(size_t)padIdx].settingsViewOpen = false;
                padAudio[(size_t)padIdx].reset();
                for (auto& v : voices)
                {
                    if (v.active && v.padIndex == padIdx)
                        v.releasing = true;
                }
            }
        }

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
                    padStates[(size_t)i].settingsViewOpen = false;
                    padAudio[(size_t)i].reset();
                }
            }
        }

        PadSlotState& getPadState(int idx)
        {
            return padStates[(size_t)juce::jlimit(0, 11, idx)];
        }

        const PadSlotState& getPadState(int idx) const
        {
            return padStates[(size_t)juce::jlimit(0, 11, idx)];
        }

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

        void triggerPadFromUI(int padIdx)
        {
            const juce::ScopedLock sl(engineLock);
            if (padIdx < 0 || padIdx >= 12)
                return;

            for (auto& v : voices)
            {
                if (v.active && v.padIndex == padIdx && !v.releasing)
                    v.releasing = true;
            }
            triggerPadVoice(padIdx, 60, 0.9f, 0.0f);
        }

        void releasePadFromUI(int padIdx)
        {
            const juce::ScopedLock sl(engineLock);
            for (auto& v : voices)
            {
                if (v.active && !v.releasing && (padIdx < 0 || v.padIndex == padIdx))
                    v.releasing = true;
            }
        }

        uint32_t getActivePlayingPadsMask() const
        {
            return activePadsBitmask.load();
        }

        juce::File renderPadForExport(int padIdx)
        {
            const juce::ScopedLock sl(engineLock);
            if (padIdx < 0 || padIdx >= 12)
                return {};

            const auto& st = padStates[(size_t)padIdx];
            if (st.sampleIndex < 0)
                return {};

            const auto& rec = padRecords[(size_t)padIdx];
            auto audio = padAudio[(size_t)padIdx];
            if (audio == nullptr || audio->buffer.getNumSamples() <= 8)
                return rec.file;

            if (!st.exportProcessed)
                return rec.file;

            const int srcTotalSamples = audio->buffer.getNumSamples();
            const double sr = (audio->sampleRate > 0.0) ? audio->sampleRate : currentSampleRate;

            int startSample = juce::jlimit(
                0, srcTotalSamples - 2,
                (int)std::floor(st.regionStart * (float)srcTotalSamples));
            int endSample = juce::jlimit(
                startSample + 2, srcTotalSamples,
                (int)std::ceil(st.regionEnd * (float)srcTotalSamples));
            int regionLen = endSample - startSample;

            double totalSemis = (double)st.semitoneOffset + (double)st.centOffset * 0.01;
            if (rec.isLoop && syncBpm && rec.detectedBpm > 40.0)
            {
                double bpmRatio = currentBpm / rec.detectedBpm;
                totalSemis += 12.0 * std::log2(juce::jlimit(0.25, 4.0, bpmRatio));
            }

            double speedRatio = juce::jlimit(0.125, 8.0, std::pow(2.0, totalSemis / 12.0));
            int outSamples = std::max(64, (int)std::ceil((double)regionLen / speedRatio));

            juce::AudioBuffer<float> outBuf(2, outSamples);
            outBuf.clear();

            float gainLin = juce::jlimit(
                0.0f, 8.0f,
                juce::Decibels::decibelsToGain(st.volumeDb, -60.0f) * rec.normGainLinear);
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
                int idx0 = juce::jlimit(startSample, endSample - 1, (int)readPos);
                int idx1 = juce::jlimit(startSample, endSample - 1, idx0 + 1);
                float frac = juce::jlimit(0.0f, 1.0f, (float)(readPos - (double)idx0));

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
                if (readPos >= (double)(endSample - 1))
                    break;
            }

            juce::File exportDir = juce::File::getSpecialLocation(
                juce::File::tempDirectory).getChildFile("MelodicAtlas_Export");
            exportDir.createDirectory();

            juce::String noteTag = MusicMath::formatPadNoteAndCents(
                rec.pitchClass, st.semitoneOffset, st.centOffset)
                .replaceCharacter(' ', '_').replaceCharacter('#', 's');
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

        void renderNextBlock(juce::AudioBuffer<float>& outputBuffer,
                             const juce::MidiBuffer& midiMessages)
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
                {
                    for (auto& v : voices)
                        v.releasing = true;
                }
            }

            const int numSamples = outputBuffer.getNumSamples();
            if (numSamples <= 0)
                return;

            if (mapRevBuffer.getNumSamples() < numSamples)
                mapRevBuffer.setSize(2, numSamples, false, false, true);
            if (bankRevBuffer.getNumSamples() < numSamples)
                bankRevBuffer.setSize(2, numSamples, false, false, true);

            mapRevBuffer.clear(0, numSamples);
            bankRevBuffer.clear(0, numSamples);

            float* outL = outputBuffer.getWritePointer(0);
            float* outR = outputBuffer.getNumChannels() > 1
                        ? outputBuffer.getWritePointer(1) : outL;
            float* mapRevL = mapRevBuffer.getWritePointer(0);
            float* mapRevR = mapRevBuffer.getWritePointer(1);
            float* bankRevL = bankRevBuffer.getWritePointer(0);
            float* bankRevR = bankRevBuffer.getWritePointer(1);

            float safeMasterGain = juce::jlimit(0.0f, 4.0f, masterGainLinear);
            if (!std::isfinite(safeMasterGain))
                safeMasterGain = 1.0f;

            // 1. ПРЕДПРОСЛУШКА НА КАРТЕ (4-голосный пул без выхода за пределы буфера)
            for (int slot = 0; slot < 4; ++slot)
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

                int semiShift = MusicMath::calculatePitchShift(
                    pv.record.pitchClass, projectKeyRoot, projectScale, pitchMode, octaveShift);
                double srRatio = pv.audio->sampleRate / std::max(8000.0, currentSampleRate);
                double step = std::pow(2.0, ((double)semiShift + (double)previewPitchSemitones) / 12.0) * srRatio;

                if (pv.record.isLoop && syncBpm && pv.record.detectedBpm > 40.0)
                    step *= juce::jlimit(0.5, 2.0, currentBpm / pv.record.detectedBpm);

                step = juce::jlimit(0.05, 8.0, step);
                const int tailStartFrame = std::max(0, totalFrames - 256);

                for (int i = 0; i < numSamples; ++i)
                {
                    if (!pv.fadingOut && pv.envGain < 1.0f)
                        pv.envGain = std::min(1.0f, pv.envGain + pv.fadeInStep);

                    if (pv.fadingOut)
                    {
                        pv.envGain -= pv.fadeOutStep;
                        if (pv.envGain <= 0.0001f || !std::isfinite(pv.envGain))
                        {
                            pv.envGain = 0.0f;
                            pv.active = false;
                            break;
                        }
                    }

                    int idx0 = (int)pv.position;
                    if (idx0 >= totalFrames - 2)
                    {
                        if (pv.record.isLoop && !pv.fadingOut)
                        {
                            pv.position = std::fmod(pv.position, (double)(totalFrames - 2));
                            if (pv.position < 0.0 || !std::isfinite(pv.position))
                                pv.position = 0.0;
                            idx0 = (int)pv.position;
                        }
                        else
                        {
                            // Конец файла: немедленно выключаем голос, не читая за границей!
                            pv.envGain = 0.0f;
                            pv.active = false;
                            break;
                        }
                    }

                    // Плавное затухание в самом конце ван-шота против щелчка
                    float endWindow = 1.0f;
                    if (!pv.record.isLoop && idx0 > tailStartFrame)
                    {
                        endWindow = (float)(totalFrames - 2 - idx0) /
                                    (float)std::max(1, totalFrames - 2 - tailStartFrame);
                        endWindow = juce::jlimit(0.0f, 1.0f, endWindow);
                    }

                    idx0 = juce::jlimit(0, totalFrames - 2, idx0);
                    int idx1 = idx0 + 1;
                    float frac = juce::jlimit(0.0f, 1.0f, (float)(pv.position - (double)idx0));

                    float sL = srcL[idx0] + frac * (srcL[idx1] - srcL[idx0]);
                    float sR = srcR[idx0] + frac * (srcR[idx1] - srcR[idx0]);
                    if (!std::isfinite(sL) || !std::isfinite(sR))
                    {
                        sL = 0.0f;
                        sR = 0.0f;
                    }

                    float gain = pv.envGain * endWindow *
                                 juce::jlimit(0.1f, 3.0f, pv.record.normGainLinear) * 0.70f;
                    outL[i] += sL * gain;
                    outR[i] += sR * gain;
                    mapRevL[i] += sL * gain;
                    mapRevR[i] += sR * gain;

                    pv.position += step;
                }
            }

            // 2. ГОЛОСА ПЭДОВ (Строгая защита границ региона и ADSR)
            uint32_t playingMask = 0;
            for (auto& v : voices)
            {
                if (!v.active || v.audio == nullptr || v.padIndex < 0 || v.padIndex >= 12)
                    continue;

                const auto& st = padStates[(size_t)v.padIndex];
                const auto& rec = padRecords[(size_t)v.padIndex];
                const auto& buf = v.audio->buffer;
                const int totalFrames = buf.getNumSamples();
                if (totalFrames <= 4)
                {
                    v.active = false;
                    continue;
                }

                int startFrame = juce::jlimit(
                    0, totalFrames - 2, (int)(st.regionStart * (float)totalFrames));
                int endFrame = juce::jlimit(
                    startFrame + 2, totalFrames, (int)(st.regionEnd * (float)totalFrames));

                const float* srcL = buf.getReadPointer(0);
                const float* srcR = buf.getNumChannels() > 1 ? buf.getReadPointer(1) : srcL;

                float padGain = juce::Decibels::decibelsToGain(st.volumeDb, -60.0f) *
                                juce::jlimit(0.1f, 3.0f, rec.normGainLinear) * v.velocity;
                padGain = juce::jlimit(0.0f, 3.5f, padGain);

                float panAngle = (juce::jlimit(-1.0f, 1.0f, st.pan) + 1.0f) *
                                 0.5f * juce::MathConstants<float>::halfPi;
                float gainL = std::cos(panAngle) * padGain;
                float gainR = std::sin(panAngle) * padGain;

                float safeAtt = juce::jlimit(0.002f, 5.0f, st.attackSec);
                float safeDec = juce::jlimit(0.005f, 10.0f, st.decaySec);
                float safeSus = juce::jlimit(0.0f, 1.0f, st.sustainLvl);
                float safeRel = juce::jlimit(0.008f, 10.0f, st.releaseSec);

                float attStep = (float)(1.0 / (currentSampleRate * safeAtt));
                float decStep = (float)((1.0 - safeSus) / (currentSampleRate * safeDec));
                float relStep = (float)(1.0 / (currentSampleRate * safeRel));

                double glideCoeff = (glideTimeSec > 0.002f)
                    ? (1.0 - std::exp(-1.0 / (currentSampleRate * glideTimeSec))) : 1.0;
                double srRatio = v.audio->sampleRate / std::max(8000.0, currentSampleRate);

                int tailGuardStart = std::max(startFrame, endFrame - 256);

                for (int i = 0; i < numSamples; ++i)
                {
                    v.currentNoteShift += (v.targetNoteShift - v.currentNoteShift) * glideCoeff;
                    double totalSemis = (double)st.semitoneOffset +
                                        (double)st.centOffset * 0.01 + v.currentNoteShift;

                    double step = std::pow(2.0, juce::jlimit(-48.0, 48.0, totalSemis) / 12.0) * srRatio;
                    if (rec.isLoop && syncBpm && rec.detectedBpm > 40.0)
                        step *= juce::jlimit(0.5, 2.0, currentBpm / rec.detectedBpm);

                    step = juce::jlimit(0.05, 8.0, step);

                    if (v.releasing)
                    {
                        v.envLevel -= relStep;
                        if (v.envLevel <= 0.0001f || !std::isfinite(v.envLevel))
                        {
                            v.envLevel = 0.0f;
                            v.active = false;
                            break;
                        }
                    }
                    else if (v.inAttack)
                    {
                        v.envLevel += attStep;
                        if (v.envLevel >= 1.0f || !std::isfinite(v.envLevel))
                        {
                            v.envLevel = 1.0f;
                            v.inAttack = false;
                        }
                    }
                    else
                    {
                        v.envLevel = std::max(safeSus, v.envLevel - decStep);
                    }

                    int idx0 = (int)v.position;
                    if (idx0 >= endFrame - 2)
                    {
                        if (rec.isLoop && !v.releasing)
                        {
                            v.position = (double)startFrame;
                            idx0 = startFrame;
                        }
                        else
                        {
                            // Дошли до конца региона: выключаем голос без чтения за границей!
                            v.envLevel = 0.0f;
                            v.active = false;
                            break;
                        }
                    }

                    float endWindow = 1.0f;
                    if ((!rec.isLoop || v.releasing) && idx0 > tailGuardStart)
                    {
                        endWindow = (float)(endFrame - 2 - idx0) /
                                    (float)std::max(1, endFrame - 2 - tailGuardStart);
                        endWindow = juce::jlimit(0.0f, 1.0f, endWindow);
                    }

                    idx0 = juce::jlimit(startFrame, endFrame - 2, idx0);
                    int idx1 = idx0 + 1;
                    float frac = juce::jlimit(0.0f, 1.0f, (float)(v.position - (double)idx0));

                    float sL = srcL[idx0] + frac * (srcL[idx1] - srcL[idx0]);
                    float sR = srcR[idx0] + frac * (srcR[idx1] - srcR[idx0]);
                    if (!std::isfinite(sL) || !std::isfinite(sR))
                    {
                        sL = 0.0f;
                        sR = 0.0f;
                    }

                    float env = v.envLevel * endWindow;
                    float l = sL * gainL * env;
                    float r = sR * gainR * env;

                    outL[i] += l;
                    outR[i] += r;
                    bankRevL[i] += l;
                    bankRevR[i] += r;

                    v.position += step;
                }

                if (v.active && !v.releasing)
                    playingMask |= (1u << v.padIndex);
            }
            activePadsBitmask.store(playingMask);

            // 3. РЕВЕРБЕРАЦИЯ С ЗАЩИТОЙ ОТ NaN
            if (mapReverbAmt > 0.005f)
            {
                sanitizeBuffer(mapRevBuffer, numSamples);
                juce::dsp::AudioBlock<float> block(
                    mapRevBuffer.getArrayOfWritePointers(), 2, (size_t)numSamples);
                mapReverb.process(juce::dsp::ProcessContextReplacing<float>(block));
                for (int i = 0; i < numSamples; ++i)
                {
                    if (std::isfinite(mapRevL[i]) && std::isfinite(mapRevR[i]))
                    {
                        outL[i] += mapRevL[i];
                        outR[i] += mapRevR[i];
                    }
                    else
                    {
                        mapReverb.reset();
                        break;
                    }
                }
            }

            if (bankReverbAmt > 0.005f)
            {
                sanitizeBuffer(bankRevBuffer, numSamples);
                juce::dsp::AudioBlock<float> block(
                    bankRevBuffer.getArrayOfWritePointers(), 2, (size_t)numSamples);
                bankReverb.process(juce::dsp::ProcessContextReplacing<float>(block));
                for (int i = 0; i < numSamples; ++i)
                {
                    if (std::isfinite(bankRevL[i]) && std::isfinite(bankRevR[i]))
                    {
                        outL[i] += bankRevL[i];
                        outR[i] += bankRevR[i];
                    }
                    else
                    {
                        bankReverb.reset();
                        break;
                    }
                }
            }

            // 4. ФИНАЛЬНЫЙ МАСТЕР-ВЫХОД
            for (int i = 0; i < numSamples; ++i)
            {
                float safeL = outL[i] * safeMasterGain;
                float safeR = outR[i] * safeMasterGain;

                if (!std::isfinite(safeL))
                    safeL = 0.0f;
                if (!std::isfinite(safeR))
                    safeR = 0.0f;

                outL[i] = std::tanh(juce::jlimit(-8.0f, 8.0f, safeL));
                outR[i] = std::tanh(juce::jlimit(-8.0f, 8.0f, safeR));
            }
        }

    private:
        struct AuditionVoice
        {
            std::shared_ptr<CachedAudioData> audio;
            SampleRecord record;
            int sampleIndex = -1;
            double position = 0.0;
            float envGain = 0.0f;
            float fadeInStep = 0.01f;
            float fadeOutStep = 0.001f;
            bool active = false;
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

        static void sanitizeBuffer(juce::AudioBuffer<float>& buf, int numSamples)
        {
            for (int ch = 0; ch < buf.getNumChannels(); ++ch)
            {
                float* d = buf.getWritePointer(ch);
                for (int i = 0; i < numSamples; ++i)
                {
                    if (!std::isfinite(d[i]) || std::abs(d[i]) > 16.0f)
                        d[i] = 0.0f;
                }
            }
        }

        void updateReverbParameters()
        {
            juce::dsp::Reverb::Parameters mp;
            mp.roomSize = 0.65f;
            mp.damping = 0.45f;
            mp.wetLevel = mapReverbAmt * 0.45f;
            mp.dryLevel = 0.0f;
            mp.width = 0.95f;
            mapReverb.setParameters(mp);

            juce::dsp::Reverb::Parameters bp;
            bp.roomSize = 0.68f;
            bp.damping = 0.42f;
            bp.wetLevel = bankReverbAmt * 0.48f;
            bp.dryLevel = 0.0f;
            bp.width = 1.0f;
            bankReverb.setParameters(bp);
        }

        void handleMidiNoteOn(int midiNote, float velocity)
        {
            if (bankMode == BankMode::PadTrigger)
            {
                int padIdx = ((midiNote % 12) + 12) % 12;
                triggerPadVoice(padIdx, midiNote, velocity, 0.0f);
            }
            else
            {
                int baseNote = (midiNote < 48) ? 36 : 60;
                float chromShift = (float)(midiNote - baseNote);
                bool triggered = false;

                for (int i = 0; i < 12; ++i)
                {
                    if (padStates[(size_t)i].sampleIndex >= 0 &&
                        padStates[(size_t)i].chromaticActive)
                    {
                        triggerPadVoice(i, midiNote, velocity, chromShift);
                        triggered = true;
                    }
                }

                if (!triggered)
                {
                    for (int i = 0; i < 12; ++i)
                    {
                        if (padStates[(size_t)i].sampleIndex >= 0)
                        {
                            triggerPadVoice(i, midiNote, velocity, chromShift);
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

        void triggerPadVoice(int padIdx, int midiNote,
                             float velocity, float chromaticSemitoneShift)
        {
            if (padIdx < 0 || padIdx >= 12)
                return;

            const auto& st = padStates[(size_t)padIdx];
            if (st.sampleIndex < 0)
                return;

            auto audio = padAudio[(size_t)padIdx];
            if (audio == nullptr || audio->buffer.getNumSamples() <= 8)
                return;

            for (auto& v : voices)
            {
                if (v.active && v.padIndex == padIdx && v.midiNote == midiNote && !v.releasing)
                    v.releasing = true;
            }

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
            {
                for (auto& v : voices)
                {
                    if (v.releasing)
                    {
                        chosen = &v;
                        break;
                    }
                }
            }

            if (chosen == nullptr)
                chosen = &voices[0];

            int totalFrames = audio->buffer.getNumSamples();
            int startFrame = juce::jlimit(
                0, totalFrames - 2, (int)(st.regionStart * (float)totalFrames));

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
                ? (double)lastPlayedChromaticShift : (double)chromaticSemitoneShift;
            chosen->audio = audio;
        }

        std::shared_ptr<CachedAudioData> loadOrGetAudio(const juce::File& file)
        {
            if (!file.existsAsFile())
                return nullptr;

            juce::String path = file.getFullPathName();
            {
                const juce::ScopedLock sl(cacheLock);
                auto it = audioCache.find(path);
                if (it != audioCache.end())
                    return it->second;
            }

            std::unique_ptr<juce::AudioFormatReader> reader(
                formatManager.createReaderFor(file));
            if (reader == nullptr || reader->lengthInSamples <= 8 || reader->sampleRate <= 0.0)
                return nullptr;

            auto data = std::make_shared<CachedAudioData>();
            data->sampleRate = reader->sampleRate;
            int numSamples = (int)std::min<juce::int64>(
                reader->lengthInSamples, (juce::int64)(reader->sampleRate * 30.0));

            data->buffer.setSize(2, numSamples);
            data->buffer.clear();
            reader->read(&data->buffer, 0, numSamples, 0, true, true);
            sanitizeBuffer(data->buffer, numSamples);

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
        double currentSampleRate = 48000.0;

        int projectKeyRoot = 9;
        ScaleType projectScale = ScaleType::NaturalMinor;
        PitchMode pitchMode = PitchMode::ScaleSnap;
        int octaveShift = 0;
        double currentBpm = 120.0;
        bool syncBpm = true;

        BankMode bankMode = BankMode::Chromatic;
        float glideTimeSec = 0.0f;
        float mapReverbAmt = 0.25f;
        float bankReverbAmt = 0.30f;
        float previewPitchSemitones = 0.0f;
        TransposeQuality transposeQuality = TransposeQuality::CleanRepitch;
        float masterGainLinear = 1.0f;

        float lastPlayedChromaticShift = 0.0f;
        bool hasLastChromaticNote = false;

        AuditionVoice previewVoices[4];
        int activePreviewSlot = 0;

        std::array<PadSlotState, 12> padStates;
        std::array<SampleRecord, 12> padRecords;
        std::array<std::shared_ptr<CachedAudioData>, 12> padAudio;
        std::array<SynthVoice, 32> voices;
        std::atomic<uint32_t> activePadsBitmask { 0 };

        juce::dsp::Reverb mapReverb;
        juce::dsp::Reverb bankReverb;
        juce::AudioBuffer<float> mapRevBuffer;
        juce::AudioBuffer<float> bankRevBuffer;
    };
}
