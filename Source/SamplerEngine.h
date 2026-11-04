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
                padStates[(size_t)i].chromaticActive = (i == 0);
                padStates[(size_t)i].exportProcessed = true;
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
                               int octShift, double bpm, bool bpmLocked)
        {
            projectKeyRoot = keyRoot;
            projectScale = scale;
            pitchMode = pMode;
            octaveShift = octShift;
            currentBpm = std::max(40.0, bpm);
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
            previewPitchSemitones = previewTuneSemis;
            transposeQuality = tQuality;
            masterGainLinear = juce::Decibels::decibelsToGain(masterDb, -60.0f);
            updateReverbParameters();
        }

        // --- ВОСПРОИЗВЕДЕНИЕ С КАРТЫ (ТОЛЬКО ПОКА НАЖАТА ЛКМ) ---
        void startPreview(int sampleIdx, const SampleRecord& rec)
        {
            auto audio = loadOrGetAudio(rec.file);
            if (audio == nullptr || audio->buffer.getNumSamples() <= 4)
                return;

            const juce::ScopedLock sl(engineLock);

            int prevSlot = activePreviewSlot;
            
            // Если предыдущий звук играл, уводим его в мягкую тишину (анти-клик 5мс)
            if (previewVoices[prevSlot].active && !previewVoices[prevSlot].fadingOut)
            {
                previewVoices[prevSlot].fadingOut = true;
                float fadeSamples = (float)std::max(10.0, currentSampleRate * 0.005);
                previewVoices[prevSlot].fadeOutStep = previewVoices[prevSlot].envGain / fadeSamples;
            }

            activePreviewSlot = 1 - prevSlot;
            auto& v = previewVoices[activePreviewSlot];

            v.audio = audio;
            v.record = rec;
            v.sampleIndex = sampleIdx;
            v.position = 0.0;
            v.active = true;
            v.fadingOut = false;
            v.envGain = 0.0f;
            
            // Быстрая атака (3мс)
            float fadeInSamples = (float)std::max(10.0, currentSampleRate * 0.003);
            v.fadeInStep = 1.0f / fadeInSamples;
        }

        // Остановка звука (запускается, когда пользователь отпускает ЛКМ)
        void stopPreview()
        {
            const juce::ScopedLock sl(engineLock);
            auto& v = previewVoices[activePreviewSlot];
            if (v.active && !v.fadingOut)
            {
                v.fadingOut = true;
                float fadeSamples = (float)std::max(10.0, currentSampleRate * 0.02); // Мягкий релиз 20мс
                v.fadeOutStep = v.envGain / fadeSamples;
            }
        }

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
                if (padStates[(size_t)i].sampleIndex >= 0 && padStates[(size_t)i].chromaticActive)
                    anyChromatic = true;
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

        PadSlotState& getPadState(int idx)             { return padStates[(size_t)juce::jlimit(0, 11, idx)]; }
        const PadSlotState& getPadState(int idx) const { return padStates[(size_t)juce::jlimit(0, 11, idx)]; }

        const SampleRecord* getPadRecord(int idx) const
        {
            if (idx < 0 || idx >= 12 || padStates[(size_t)idx].sampleIndex < 0) return nullptr;
            return &padRecords[(size_t)idx];
        }

        std::shared_ptr<CachedAudioData> getPadAudio(int idx) const
        {
            if (idx < 0 || idx >= 12 || padStates[(size_t)idx].sampleIndex < 0) return nullptr;
            return padAudio[(size_t)idx];
        }

        // --- ВОСПРОИЗВЕДЕНИЕ С ПЭДОВ ---
        void triggerPadFromUI(int padIdx)
        {
            const juce::ScopedLock sl(engineLock);
            if (padIdx < 0 || padIdx >= 12) return;
            
            // Жестко глушим предыдущий голос этого пэда (Anti-click)
            for (auto& v : voices)
            {
                if (v.active && v.padIndex == padIdx && !v.releasing)
                {
                    v.releasing = true;
                }
            }
            triggerPadVoice(padIdx, 60, 0.9f, 0.0f);
        }

        // Запускает релиз, когда пользователь отжимает мышь от пэда
        void releasePadFromUI(int padIdx)
        {
            const juce::ScopedLock sl(engineLock);
            for (auto& v : voices)
            {
                if (v.active && !v.releasing && v.padIndex == padIdx)
                {
                    v.releasing = true;
                }
            }
        }

        uint32_t getActivePlayingPadsMask() const { return activePadsBitmask.load(); }

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

            if (!st.exportProcessed)
                return rec.file;

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

            float gainLin = juce::jlimit(0.0f, 10.0f, juce::Decibels::decibelsToGain(st.volumeDb, -60.0f) * rec.normGainLinear);
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
                if (readPos >= (double)endSample) break;
            }

            juce::File exportDir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("MelodicAtlas_Export");
            exportDir.createDirectory();

            juce::String noteTag = MusicMath::formatPadNoteAndCents(rec.pitchClass, st.semitoneOffset, st.centOffset).replaceCharacter(' ', '_').replaceCharacter('#', 's');
            juce::String outName = rec.name + "_" + noteTag + "_Proc.wav";
            juce::File outFile = exportDir.getChildFile(juce::File::createLegalFileName(outName));
            if (outFile.existsAsFile()) outFile.deleteFile();

            juce::WavAudioFormat wavFormat;
            if (auto stream = std::unique_ptr<juce::FileOutputStream>(outFile.createOutputStream()))
            {
                if (auto writer = std::unique_ptr<juce::AudioFormatWriter>(wavFormat.createWriterFor(stream.get(), sr, 2, 24, {}, 0)))
                {
                    stream.release();
                    writer->writeFromAudioSampleBuffer(outBuf, 0, outSamples);
                    return outFile;
                }
            }
            return rec.file;
        }

        void renderNextBlock(juce::AudioBuffer<float>& outputBuffer, const juce::MidiBuffer& midiMessages)
        {
            const juce::ScopedLock sl(engineLock);

            for (const auto metadata : midiMessages)
            {
                const auto msg = metadata.getMessage();
                if (msg.isNoteOn())
                {
                    handleMidiNoteOn(msg.getNoteNumber(), msg.getFloatVelocity());
                }
                else if (msg.isNoteOff())
                {
                    handleMidiNoteOff(msg.getNoteNumber());
                }
                else if (msg.isAllNotesOff() || msg.isAllSoundOff())
                {
                    for (auto& v : voices) v.releasing = true;
                }
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

            float safeMasterGain = juce::jlimit(0.0f, 4.0f, masterGainLinear);

            // 1. ПРЕДПРОСЛУШКА НА КАРТЕ
            for (int slot = 0; slot < 2; ++slot)
            {
                auto& pv = previewVoices[slot];
                if (!pv.active || pv.audio == nullptr) continue;

                const auto& buf = pv.audio->buffer;
                const int totalFrames = buf.getNumSamples();
                const float* srcL = buf.getReadPointer(0);
                const float* srcR = buf.getNumChannels() > 1 ? buf.getReadPointer(1) : srcL;

                int semiShift = MusicMath::calculatePitchShift(pv.record.pitchClass, projectKeyRoot, projectScale, pitchMode, octaveShift);
                double step = std::pow(2.0, ((double)semiShift + (double)previewPitchSemitones) / 12.0) * (pv.audio->sampleRate / std::max(1.0, currentSampleRate));
                
                // ЗАЩИТА: Ограничение питча
                step = juce::jlimit(0.05, 20.0, step); 

                if (pv.record.isLoop && syncBpm && pv.record.detectedBpm > 40.0)
                    step *= juce::jlimit(0.25, 4.0, currentBpm / pv.record.detectedBpm);

                for (int i = 0; i < numSamples; ++i)
                {
                    if (!pv.fadingOut && pv.envGain < 1.0f)
                    {
                        pv.envGain = std::min(1.0f, pv.envGain + pv.fadeInStep);
                    }

                    if (pv.fadingOut)
                    {
                        pv.envGain -= pv.fadeOutStep;
                        if (pv.envGain <= 0.0001f || std::isnan(pv.envGain)) 
                        { 
                            pv.envGain = 0.0f; 
                            pv.active = false; 
                            break; 
                        }
                    }

                    int idx0 = (int)pv.position;
                    // Если дошли до конца файла, а мышка еще зажата (fadingOut = false) -> крутим луп.
                    if (idx0 >= totalFrames - 1)
                    {
                        if (pv.record.isLoop && !pv.fadingOut) 
                        { 
                            pv.position = std::fmod(pv.position, (double)(totalFrames - 1)); 
                            idx0 = (int)pv.position; 
                        }
                        else 
                        { 
                            pv.fadingOut = true; 
                            pv.fadeOutStep = pv.envGain / std::max(1.0f, (float)(currentSampleRate * 0.01)); 
                        }
                    }

                    int idx1 = std::min(totalFrames - 1, idx0 + 1);
                    float frac = (float)(pv.position - (double)idx0);
                    
                    float sL = srcL[idx0] + frac * (srcL[idx1] - srcL[idx0]);
                    float sR = srcR[idx0] + frac * (srcR[idx1] - srcR[idx0]);
                    
                    // ЗАЩИТА: Проверка на NaN
                    if (std::isnan(sL) || std::isnan(sR)) { sL = 0.0f; sR = 0.0f; } 

                    float gain = pv.envGain * pv.record.normGainLinear * 0.75f;
                    outL[i] += sL * gain; outR[i] += sR * gain;
                    mapRevL[i] += sL * gain; mapRevR[i] += sR * gain;

                    pv.position += step;
                }
            }

            // 2. ГОЛОСА ПЭДОВ
            uint32_t playingMask = 0;
            for (auto& v : voices)
            {
                if (!v.active || v.audio == nullptr || v.padIndex < 0 || v.padIndex >= 12) continue;

                const auto& st = padStates[(size_t)v.padIndex];
                const auto& rec = padRecords[(size_t)v.padIndex];
                const auto& buf = v.audio->buffer;
                const int totalFrames = buf.getNumSamples();

                int startFrame = juce::jlimit(0, totalFrames - 2, (int)(st.regionStart * (float)totalFrames));
                int endFrame   = juce::jlimit(startFrame + 2, totalFrames, (int)(st.regionEnd * (float)totalFrames));

                const float* srcL = buf.getReadPointer(0);
                const float* srcR = buf.getNumChannels() > 1 ? buf.getReadPointer(1) : srcL;

                float padGain = juce::Decibels::decibelsToGain(st.volumeDb, -60.0f) * rec.normGainLinear * v.velocity;
                padGain = juce::jlimit(0.0f, 4.0f, padGain); 
                
                float panAngle = (st.pan + 1.0f) * 0.5f * juce::MathConstants<float>::halfPi;
                float gainL = std::cos(panAngle) * padGain;
                float gainR = std::sin(panAngle) * padGain;

                // БЕЗОПАСНЫЙ РАСЧЕТ ШАГОВ ADSR (предотвращает деление на 0)
                float safeAtt = std::max(0.001f, st.attackSec);
                float safeDec = std::max(0.001f, st.decaySec);
                float safeRel = std::max(0.005f, st.releaseSec); // Минимум 5мс релиз

                float attStep = (float)(1.0 / (currentSampleRate * safeAtt));
                float decStep = (float)((1.0 - st.sustainLvl) / (currentSampleRate * safeDec));
                float relStep = (float)(1.0 / (currentSampleRate * safeRel));

                double glideCoeff = (glideTimeSec > 0.002f) ? (1.0 - std::exp(-1.0 / (currentSampleRate * glideTimeSec))) : 1.0;
                double srRatio = v.audio->sampleRate / std::max(1.0, currentSampleRate);

                for (int i = 0; i < numSamples; ++i)
                {
                    v.currentNoteShift += (v.targetNoteShift - v.currentNoteShift) * glideCoeff;
                    double totalSemis = (double)st.semitoneOffset + (double)st.centOffset * 0.01 + v.currentNoteShift;
                    double step = std::pow(2.0, totalSemis / 12.0) * srRatio;
                    
                    step = juce::jlimit(0.01, 20.0, step); // ЗАЩИТА ПИТЧА

                    if (v.releasing)
                    {
                        v.envLevel -= relStep;
                        if (v.envLevel <= 0.0001f || std::isnan(v.envLevel)) 
                        { 
                            v.envLevel = 0.0f; 
                            v.active = false; 
                            break; 
                        }
                    }
                    else if (v.inAttack)
                    {
                        v.envLevel += attStep;
                        if (v.envLevel >= 1.0f || std::isnan(v.envLevel)) 
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
                        // Луп крутится, пока клавиша зажата (!v.releasing)
                        if (rec.isLoop && !v.releasing) 
                        { 
                            v.position = (double)startFrame; 
                            idx0 = startFrame; 
                        }
                        else 
                        { 
                            v.releasing = true; 
                        } 
                    }

                    int idx1 = std::min(endFrame - 1, idx0 + 1);
                    float frac = (float)(v.position - (double)idx0);

                    float sL = srcL[idx0] + frac * (srcL[idx1] - srcL[idx0]);
                    float sR = srcR[idx0] + frac * (srcR[idx1] - srcR[idx0]);
                    if (std::isnan(sL) || std::isnan(sR)) { sL = 0.0f; sR = 0.0f; }

                    float l = sL * gainL * v.envLevel;
                    float r = sR * gainR * v.envLevel;

                    outL[i] += l; outR[i] += r;
                    bankRevL[i] += l; bankRevR[i] += r;

                    v.position += step;
                }
                if (v.active && !v.releasing) playingMask |= (1u << v.padIndex);
            }
            activePadsBitmask.store(playingMask);

            // Реверберация
            if (mapReverbAmt > 0.005f)
            {
                juce::dsp::AudioBlock<float> block(mapRevBuffer.getArrayOfWritePointers(), 2, (size_t)numSamples);
                mapReverb.process(juce::dsp::ProcessContextReplacing<float>(block));
                for (int i = 0; i < numSamples; ++i) { outL[i] += mapRevL[i]; outR[i] += mapRevR[i]; }
            }

            if (bankReverbAmt > 0.005f)
            {
                juce::dsp::AudioBlock<float> block(bankRevBuffer.getArrayOfWritePointers(), 2, (size_t)numSamples);
                bankReverb.process(juce::dsp::ProcessContextReplacing<float>(block));
                for (int i = 0; i < numSamples; ++i) { outL[i] += bankRevL[i]; outR[i] += bankRevR[i]; }
            }

            // ФИНАЛЬНЫЙ ЛИМИТЕР: Мгновенное гашение NaN и софт-клип
            for (int i = 0; i < numSamples; ++i)
            {
                float safeL = outL[i] * safeMasterGain;
                float safeR = outR[i] * safeMasterGain;
                
                if (std::isnan(safeL) || std::isinf(safeL)) safeL = 0.0f;
                if (std::isnan(safeR) || std::isinf(safeR)) safeR = 0.0f;
                
                outL[i] = std::tanh(safeL);
                outR[i] = std::tanh(safeR);
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

        void updateReverbParameters()
        {
            juce::dsp::Reverb::Parameters mp;
            mp.roomSize = 0.68f; mp.damping = 0.42f; mp.wetLevel = mapReverbAmt * 0.55f; mp.dryLevel = 0.0f; mp.width = 0.95f;
            mapReverb.setParameters(mp);

            juce::dsp::Reverb::Parameters bp;
            bp.roomSize = 0.72f; bp.damping = 0.38f; bp.wetLevel = bankReverbAmt * 0.58f; bp.dryLevel = 0.0f; bp.width = 1.0f;
            bankReverb.setParameters(bp);
        }

        void handleMidiNoteOn(int midiNote, float velocity)
        {
            if (bankMode == BankMode::PadTrigger)
            {
                triggerPadVoice(((midiNote % 12) + 12) % 12, midiNote, velocity, 0.0f);
            }
            else
            {
                int baseNote = (midiNote < 48) ? 36 : 60;
                float chromShift = (float)(midiNote - baseNote);
                bool triggered = false;
                for (int i = 0; i < 12; ++i)
                {
                    if (padStates[(size_t)i].sampleIndex >= 0 && padStates[(size_t)i].chromaticActive)
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

        void triggerPadVoice(int padIdx, int midiNote, float velocity, float chromaticSemitoneShift)
        {
            if (padIdx < 0 || padIdx >= 12) return;
            const auto& st = padStates[(size_t)padIdx];
            if (st.sampleIndex < 0) return;

            auto audio = padAudio[(size_t)padIdx];
            if (audio == nullptr || audio->buffer.getNumSamples() <= 4) return;

            // Жестко глушим предыдущие голоса этого же пэда, чтобы предотвратить наслоение громкости
            for (auto& v : voices)
            {
                if (v.active && v.padIndex == padIdx && !v.releasing)
                {
                    v.releasing = true;
                }
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
            if (chosen == nullptr) chosen = &voices[0]; 

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
            chosen->currentNoteShift = (hasLastChromaticNote && glideTimeSec > 0.005f) ? (double)lastPlayedChromaticShift : (double)chromaticSemitoneShift;
            chosen->audio = audio;
        }

        std::shared_ptr<CachedAudioData> loadOrGetAudio(const juce::File& file)
        {
            if (!file.existsAsFile()) return nullptr;
            juce::String path = file.getFullPathName();

            {
                const juce::ScopedLock sl(cacheLock);
                auto it = audioCache.find(path);
                if (it != audioCache.end()) return it->second;
            }

            std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
            if (reader == nullptr || reader->lengthInSamples <= 0) return nullptr;

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
                for (int i = start; i < end; ++i) maxVal = std::max(maxVal, std::abs(ch0[i]));
                data->waveformPeaks[(size_t)p] = juce::jlimit(0.05f, 1.0f, maxVal);
            }

            {
                const juce::ScopedLock sl(cacheLock);
                if (audioCache.size() > 256) audioCache.clear();
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
