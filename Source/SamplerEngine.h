#pragma once
#include "AtlasTypes.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <memory>

namespace Atlas
{
    // Загруженный в память аудио-буфер с превью волны для UI
    struct LoadedSampleAudio
    {
        juce::AudioBuffer<float> buffer;
        double sampleRate = 44100.0;
        std::array<float, 96> waveformPeaks { 0.0f };

        void buildWaveformPeaks()
        {
            waveformPeaks.fill(0.0f);
            const int total = buffer.getNumSamples();
            if (total <= 0) return;

            const int numCh = buffer.getNumChannels();
            const int samplesPerBin = std::max(1, total / (int)waveformPeaks.size());

            for (size_t b = 0; b < waveformPeaks.size(); ++b)
            {
                int start = (int)b * samplesPerBin;
                int end = std::min(total, start + samplesPerBin);
                float maxVal = 0.0f;
                for (int ch = 0; ch < numCh; ++ch)
                {
                    const float* ptr = buffer.getReadPointer(ch);
                    for (int i = start; i < end; ++i)
                        maxVal = std::max(maxVal, std::abs(ptr[i]));
                }
                waveformPeaks[b] = juce::jlimit(0.0f, 1.0f, maxVal);
            }
        }
    };

    // Двухголовый гранулярный стретчер для независимого питча и BPM-синхронизации лупов
    struct DualGrainLoopStretcher
    {
        double timelinePos = 0.0;
        double grainPosA = 0.0;
        double grainPosB = 0.0;
        double grainPhase = 0.0;
        static constexpr double kGrainLengthSamples = 2048.0;

        void reset(double startPos = 0.0)
        {
            timelinePos = startPos;
            grainPosA = startPos;
            grainPosB = startPos;
            grainPhase = 0.0;
        }

        void renderSample(const juce::AudioBuffer<float>& buf,
                          double timeRatio,
                          double pitchRatio,
                          bool loopWrap,
                          float& outL,
                          float& outR)
        {
            const int totalSamples = buf.getNumSamples();
            if (totalSamples < 4) { outL = 0.0f; outR = 0.0f; return; }

            const int numCh = buf.getNumChannels();
            const float* ch0 = buf.getReadPointer(0);
            const float* ch1 = (numCh > 1) ? buf.getReadPointer(1) : ch0;

            auto readInterp = [&](double pos, float& l, float& r)
            {
                if (loopWrap)
                {
                    while (pos >= (double)totalSamples) pos -= (double)totalSamples;
                    while (pos < 0.0) pos += (double)totalSamples;
                }
                else
                {
                    pos = juce::jlimit(0.0, (double)(totalSamples - 2), pos);
                }

                int idx0 = (int)pos;
                int idx1 = loopWrap ? ((idx0 + 1) % totalSamples) : std::min(totalSamples - 1, idx0 + 1);
                float frac = (float)(pos - (double)idx0);

                l = ch0[idx0] + frac * (ch0[idx1] - ch0[idx0]);
                r = ch1[idx0] + frac * (ch1[idx1] - ch1[idx0]);
            };

            // Окно Хэнна для двух зерен со сдвигом фазы на 0.5
            double phaseA = grainPhase;
            double phaseB = grainPhase + 0.5;
            if (phaseB >= 1.0) phaseB -= 1.0;

            float winA = 0.5f * (1.0f - std::cos(juce::MathConstants<float>::twoPi * (float)phaseA));
            float winB = 0.5f * (1.0f - std::cos(juce::MathConstants<float>::twoPi * (float)phaseB));

            float lA = 0.0f, rA = 0.0f, lB = 0.0f, rB = 0.0f;
            readInterp(grainPosA, lA, rA);
            readInterp(grainPosB, lB, rB);

            outL = lA * winA + lB * winB;
            outR = rA * winA + rB * winB;

            // Продвигаем позиции
            timelinePos += timeRatio;
            if (loopWrap)
            {
                while (timelinePos >= (double)totalSamples) timelinePos -= (double)totalSamples;
                while (timelinePos < 0.0) timelinePos += (double)totalSamples;
            }

            grainPosA += pitchRatio;
            grainPosB += pitchRatio;

            double prevPhaseA = phaseA;
            double prevPhaseB = phaseB;
            grainPhase += 1.0 / kGrainLengthSamples;

            if (grainPhase >= 1.0)
            {
                grainPhase -= 1.0;
                grainPosA = timelinePos; // Перезапуск зерна А в точке таймлайна
            }
            if (prevPhaseB > (grainPhase + 0.5 - (grainPhase + 0.5 >= 1.0 ? 1.0 : 0.0)) ||
                (prevPhaseA < 0.5 && grainPhase >= 0.5))
            {
                grainPosB = timelinePos; // Перезапуск зерна B на противофазе
            }
        }
    };

    class SamplerEngine
    {
    public:
        SamplerEngine()
        {
            formatManager.registerBasicFormats();
            for (int i = 0; i < 12; ++i)
                padSlots[i].sampleIndex = -1;
        }

        void prepare(double newSampleRate, int samplesPerBlock)
        {
            const juce::ScopedLock sl(engineLock);
            sampleRate = (newSampleRate > 0.0) ? newSampleRate : 44100.0;

            juce::dsp::ProcessSpec spec;
            spec.sampleRate = sampleRate;
            spec.maximumBlockSize = (juce::uint32)std::max(64, samplesPerBlock);
            spec.numChannels = 2;

            reverb.prepare(spec);
            updateReverbParams();

            for (auto& v : voices)
            {
                v.adsr.setSampleRate(sampleRate);
                v.active = false;
            }
        }

        // Обновление глобальных параметров из процессора
        void setMusicalContext(int keyRoot, bool minorScale, PitchMode pMode, int octShift,
                               double dawBpm, bool syncToHost, bool hostIsPlaying, double hostPpqPos)
        {
            projectKeyRoot = keyRoot;
            isMinor = minorScale;
            pitchMode = pMode;
            globalOctaveShift = octShift;
            currentBpm = (dawBpm > 20.0) ? dawBpm : 120.0;
            syncEnabled = syncToHost;
            dawIsPlaying = hostIsPlaying;
            currentPpq = hostPpqPos;
        }

        void setEngineControls(BankMode mode, float attSec, float decSec, float susLvl, float relSec,
                               float glideSec, float reverbAmt)
        {
            bankMode = mode;
            adsrParams.attack  = juce::jlimit(0.002f, 5.0f, attSec);
            adsrParams.decay   = juce::jlimit(0.01f, 5.0f, decSec);
            adsrParams.sustain = juce::jlimit(0.0f, 1.0f, susLvl);
            adsrParams.release = juce::jlimit(0.01f, 8.0f, relSec);
            glideTimeSec       = juce::jlimit(0.0f, 1.0f, glideSec);
            reverbWet          = juce::jlimit(0.0f, 1.0f, reverbAmt);
            updateReverbParams();
        }

        // Предпрослушка сэмпла с 2D-карты (с 15 мс кроссфейдом)
        void auditionSample(int sampleIdx, const SampleRecord& rec)
        {
            auto loaded = loadAudioFromFile(rec.file);
            if (loaded == nullptr) return;

            const juce::ScopedLock sl(engineLock);

            // Переносим текущий играющий голос в уходящий (для 15 мс кроссфейда без щелчков)
            if (auditionCurrent.playing)
            {
                auditionFadingOut = auditionCurrent;
                auditionFadingOut.fadeGain = 1.0f;
            }

            auditionCurrent.audio = loaded;
            auditionCurrent.record = rec;
            auditionCurrent.sampleIndex = sampleIdx;
            auditionCurrent.readPos = 0.0;
            auditionCurrent.stretcher.reset(0.0);
            auditionCurrent.fadeGain = 0.0f;
            auditionCurrent.playing = true;

            // Если это луп и включен SYNC во время проигрывания DAW — ждем начала следующей доли/такта
            if (rec.isLoop && syncEnabled && dawIsPlaying)
                auditionCurrent.waitingForBeatSync = true;
            else
                auditionCurrent.waitingForBeatSync = false;

            lastAuditionedIndex.store(sampleIdx);
        }

        void stopAudition()
        {
            const juce::ScopedLock sl(engineLock);
            if (auditionCurrent.playing)
            {
                auditionFadingOut = auditionCurrent;
                auditionFadingOut.fadeGain = 1.0f;
                auditionCurrent.playing = false;
            }
        }

        // Загрузка сэмпла в один из 12 пэдов
        void assignSampleToPad(int padIdx, int sampleIdx, const SampleRecord& rec)
        {
            if (padIdx < 0 || padIdx >= 12) return;
            auto loaded = loadAudioFromFile(rec.file);
            if (loaded == nullptr) return;

            const juce::ScopedLock sl(engineLock);
            padSlots[padIdx].sampleIndex = sampleIdx;
            // По умолчанию: монофонические ваншоты активны (●), а лупы и аккорды выключены (○) в Chromatic
            padSlots[padIdx].chromaticActive = !(rec.isLoop || rec.isPolyChord);
            padSlots[padIdx].volumeDb = -3.0f;
            padSlots[padIdx].pan = 0.0f;
            padSlots[padIdx].semitoneOffset = 0;
            padSlots[padIdx].reverse = false;

            padAudio[padIdx] = loaded;
            padRecords[padIdx] = rec;
        }

        void clearAllPads()
        {
            const juce::ScopedLock sl(engineLock);
            for (int i = 0; i < 12; ++i)
            {
                padSlots[i].sampleIndex = -1;
                padAudio[i].reset();
            }
            for (auto& v : voices)
                v.active = false;
        }

        PadSlotState& getPadState(int padIdx) { return padSlots[juce::jlimit(0, 11, padIdx)]; }
        const PadSlotState& getPadState(int padIdx) const { return padSlots[juce::jlimit(0, 11, padIdx)]; }
        const SampleRecord* getPadRecord(int padIdx) const
        {
            if (padIdx < 0 || padIdx >= 12 || padSlots[padIdx].sampleIndex < 0) return nullptr;
            return &padRecords[padIdx];
        }
        std::shared_ptr<LoadedSampleAudio> getPadAudio(int padIdx) const
        {
            if (padIdx < 0 || padIdx >= 12) return nullptr;
            return padAudio[padIdx];
        }

        std::shared_ptr<LoadedSampleAudio> getAuditionAudio() const
        {
            const juce::ScopedLock sl(engineLock);
            return auditionCurrent.audio;
        }

        int getLastAuditionedIndex() const { return lastAuditionedIndex.load(); }

        // Прямой триггер пэда мышкой из UI
        void triggerPadFromUI(int padIdx, float velocity = 0.9f)
        {
            const juce::ScopedLock sl(engineLock);
            if (padIdx < 0 || padIdx >= 12 || padSlots[padIdx].sampleIndex < 0) return;
            startVoiceForSlot(padIdx, 36 + padIdx, velocity, false);
        }

        // Главный рендер аудио и обработка MIDI
        void renderNextBlock(juce::AudioBuffer<float>& outputAudio, const juce::MidiBuffer& midiData)
        {
            const juce::ScopedLock sl(engineLock);
            const int numSamples = outputAudio.getNumSamples();
            if (numSamples <= 0) return;

            for (const auto metadata : midiData)
            {
                const auto msg = metadata.getMessage();
                if (msg.isNoteOn())
                    handleNoteOn(msg.getNoteNumber(), msg.getFloatVelocity());
                else if (msg.isNoteOff())
                    handleNoteOff(msg.getNoteNumber());
                else if (msg.isAllNotesOff() || msg.isAllSoundOff())
                    allNotesOff();
            }

            juce::AudioBuffer<float> dryBuf(2, numSamples);
            dryBuf.clear();

            float* outL = dryBuf.getWritePointer(0);
            float* outR = dryBuf.getWritePointer(1);

            const float fadeStep = 1.0f / std::max(1.0f, (float)(sampleRate * 0.015)); // 15 мс кроссфейд

            // 1. Рендерим предпрослушку карты (Audition)
            if (auditionFadingOut.playing && auditionFadingOut.audio != nullptr)
            {
                for (int i = 0; i < numSamples; ++i)
                {
                    float l = 0.0f, r = 0.0f;
                    if (stepAuditionStream(auditionFadingOut, l, r))
                    {
                        outL[i] += l * auditionFadingOut.fadeGain;
                        outR[i] += r * auditionFadingOut.fadeGain;
                        auditionFadingOut.fadeGain -= fadeStep;
                        if (auditionFadingOut.fadeGain <= 0.0f)
                        {
                            auditionFadingOut.playing = false;
                            break;
                        }
                    }
                    else
                    {
                        auditionFadingOut.playing = false;
                        break;
                    }
                }
            }

            if (auditionCurrent.playing && auditionCurrent.audio != nullptr)
            {
                // Проверяем квантованный старт лупа по сетке такта
                if (auditionCurrent.waitingForBeatSync)
                {
                    double fracBeat = currentPpq - std::floor(currentPpq);
                    if (!dawIsPlaying || fracBeat < 0.08 || fracBeat > 0.92)
                        auditionCurrent.waitingForBeatSync = false;
                }

                if (!auditionCurrent.waitingForBeatSync)
                {
                    for (int i = 0; i < numSamples; ++i)
                    {
                        float l = 0.0f, r = 0.0f;
                        if (stepAuditionStream(auditionCurrent, l, r))
                        {
                            auditionCurrent.fadeGain = std::min(1.0f, auditionCurrent.fadeGain + fadeStep);
                            outL[i] += l * auditionCurrent.fadeGain;
                            outR[i] += r * auditionCurrent.fadeGain;
                        }
                        else
                        {
                            auditionCurrent.playing = false;
                            break;
                        }
                    }
                }
            }

            // 2. Рендерим голоса 12-слотового банка
            for (auto& v : voices)
            {
                if (!v.active || v.audio == nullptr) continue;

                v.adsr.setParameters(adsrParams);

                for (int i = 0; i < numSamples; ++i)
                {
                    float env = v.adsr.getNextSample();
                    if (!v.adsr.isActive())
                    {
                        v.active = false;
                        break;
                    }

                    // Плавный Glide к целевой высоте тона
                    if (glideTimeSec > 0.005f)
                    {
                        double glideCoeff = 1.0 - std::exp(-1.0 / (sampleRate * (double)glideTimeSec * 0.25));
                        v.currentPitchRatio += (v.targetPitchRatio - v.currentPitchRatio) * glideCoeff;
                    }
                    else
                    {
                        v.currentPitchRatio = v.targetPitchRatio;
                    }

                    float l = 0.0f, r = 0.0f;
                    bool alive = stepVoiceStream(v, l, r);

                    float gain = env * v.velocityGain * v.slotLinearGain * v.normGain;
                    outL[i] += l * gain * v.panL;
                    outR[i] += r * gain * v.panR;

                    if (!alive)
                    {
                        v.active = false;
                        v.adsr.reset();
                        break;
                    }
                }
            }

            // 3. Применяем мастер-эффект REVERB
            if (reverbWet > 0.005f)
            {
                juce::dsp::AudioBlock<float> block(dryBuf);
                juce::dsp::ProcessContextReplacing<float> ctx(block);
                reverb.process(ctx);
            }

            // Копируем в выходной буфер DAW
            for (int ch = 0; ch < outputAudio.getNumChannels(); ++ch)
            {
                const float* src = dryBuf.getReadPointer( std::min(ch, 1) );
                outputAudio.addFrom(ch, 0, src, numSamples);
            }
        }

    private:
        struct AuditionStream
        {
            std::shared_ptr<LoadedSampleAudio> audio;
            SampleRecord record;
            int sampleIndex = -1;
            double readPos = 0.0;
            DualGrainLoopStretcher stretcher;
            float fadeGain = 0.0f;
            bool playing = false;
            bool waitingForBeatSync = false;
        };

        struct PolyVoice
        {
            bool active = false;
            int slotIndex = -1;
            int midiNote = -1;
            bool isBassAnchorVoice = false;

            std::shared_ptr<LoadedSampleAudio> audio;
            SampleRecord record;
            bool reverse = false;

            double readPos = 0.0;
            double currentPitchRatio = 1.0;
            double targetPitchRatio = 1.0;

            float velocityGain = 1.0f;
            float slotLinearGain = 1.0f;
            float normGain = 1.0f;
            float panL = 1.0f;
            float panR = 1.0f;

            DualGrainLoopStretcher stretcher;
            juce::ADSR adsr;
        };

        double calculateAuditionPitchRatio(const SampleRecord& rec) const
        {
            int semiShift = MusicMath::calculatePitchShift(rec.pitchClass, projectKeyRoot, isMinor,
                                                           pitchMode, globalOctaveShift);
            double pitchRatio = std::pow(2.0, (double)semiShift / 12.0);
            double srRatio = (auditionCurrent.audio != nullptr)
                                ? (auditionCurrent.audio->sampleRate / sampleRate) : 1.0;
            return pitchRatio * srRatio;
        }

        bool stepAuditionStream(AuditionStream& stream, float& outL, float& outR)
        {
            const auto& buf = stream.audio->buffer;
            const int totalSamples = buf.getNumSamples();
            if (totalSamples < 4) return false;

            double pitchRatio = calculateAuditionPitchRatio(stream.record);

            if (stream.record.isLoop)
            {
                double bpmRatio = syncEnabled ? (currentBpm / std::max(40.0, stream.record.detectedBpm)) : 1.0;
                double srRatio = stream.audio->sampleRate / sampleRate;
                stream.stretcher.renderSample(buf, bpmRatio * srRatio, pitchRatio, true, outL, outR);
                outL *= stream.record.normGainLinear;
                outR *= stream.record.normGainLinear;
                return true;
            }

            // Ваншот: чистый ресэмплинг с линейной/кубической интерполяцией
            if (stream.readPos >= (double)(totalSamples - 2))
                return false;

            int idx0 = (int)stream.readPos;
            int idx1 = std::min(totalSamples - 1, idx0 + 1);
            float frac = (float)(stream.readPos - (double)idx0);

            const float* ch0 = buf.getReadPointer(0);
            const float* ch1 = (buf.getNumChannels() > 1) ? buf.getReadPointer(1) : ch0;

            outL = (ch0[idx0] + frac * (ch0[idx1] - ch0[idx0])) * stream.record.normGainLinear;
            outR = (ch1[idx0] + frac * (ch1[idx1] - ch1[idx0])) * stream.record.normGainLinear;

            stream.readPos += pitchRatio;
            return true;
        }

        bool stepVoiceStream(PolyVoice& v, float& outL, float& outR)
        {
            const auto& buf = v.audio->buffer;
            const int totalSamples = buf.getNumSamples();
            if (totalSamples < 4) return false;

            if (v.record.isLoop)
            {
                double bpmRatio = syncEnabled ? (currentBpm / std::max(40.0, v.record.detectedBpm)) : 1.0;
                double srRatio = v.audio->sampleRate / sampleRate;
                v.stretcher.renderSample(buf, bpmRatio * srRatio, v.currentPitchRatio, true, outL, outR);
                return true;
            }

            if (v.readPos < 0.0 || v.readPos >= (double)(totalSamples - 2))
                return false;

            int idx0 = (int)v.readPos;
            int idx1 = std::min(totalSamples - 1, idx0 + 1);
            float frac = (float)(v.readPos - (double)idx0);

            if (v.reverse)
            {
                idx0 = totalSamples - 1 - idx0;
                idx1 = std::max(0, idx0 - 1);
            }

            const float* ch0 = buf.getReadPointer(0);
            const float* ch1 = (buf.getNumChannels() > 1) ? buf.getReadPointer(1) : ch0;

            outL = ch0[idx0] + frac * (ch0[idx1] - ch0[idx0]);
            outR = ch1[idx0] + frac * (ch1[idx1] - ch1[idx0]);

            v.readPos += v.currentPitchRatio;
            return true;
        }

        void handleNoteOn(int midiNote, float velocity)
        {
            heldNotes.insert(midiNote);

            if (bankMode == BankMode::PadTrigger)
            {
                // Ноты C1..B1 (36..47) или любая октава по модулю 12 запускают слот 0..11
                int slotIdx = (midiNote >= 36 && midiNote < 48) ? (midiNote - 36) : (midiNote % 12);
                if (padSlots[slotIdx].sampleIndex >= 0 && padAudio[slotIdx] != nullptr)
                    startVoiceForSlot(slotIdx, midiNote, velocity, false);
                return;
            }

            // Режим CHROMATIC:
            // 1. Все активные (●) монофонические ваншоты играют полифонически
            // 2. Активные (●) лупы и аккорды работают в режиме Bass Follower (следуют за самой нижней нотой)
            int lowestNote = *heldNotes.begin();

            for (int slotIdx = 0; slotIdx < 12; ++slotIdx)
            {
                if (padSlots[slotIdx].sampleIndex < 0 || !padSlots[slotIdx].chromaticActive || padAudio[slotIdx] == nullptr)
                    continue;

                const auto& rec = padRecords[slotIdx];
                bool isBassFollowerType = (rec.isLoop || rec.isPolyChord);

                if (isBassFollowerType)
                {
                    // Перенаправляем существующий басовый голос этого слота на нижнюю ноту аккорда
                    updateOrStartBassAnchorVoice(slotIdx, lowestNote, velocity);
                }
                else
                {
                    startVoiceForSlot(slotIdx, midiNote, velocity, true);
                }
            }
        }

        void handleNoteOff(int midiNote)
        {
            heldNotes.erase(midiNote);

            if (bankMode == BankMode::PadTrigger)
            {
                for (auto& v : voices)
                {
                    if (v.active && v.midiNote == midiNote)
                        v.adsr.noteOff();
                }
                return;
            }

            // В режиме CHROMATIC:
            int lowestNote = heldNotes.empty() ? -1 : *heldNotes.begin();

            for (auto& v : voices)
            {
                if (!v.active) continue;

                if (v.isBassAnchorVoice)
                {
                    if (heldNotes.empty())
                        v.adsr.noteOff();
                    else
                        retuneVoiceToMidiNote(v, lowestNote);
                }
                else if (v.midiNote == midiNote)
                {
                    v.adsr.noteOff();
                }
            }
        }

        void allNotesOff()
        {
            heldNotes.clear();
            for (auto& v : voices)
                if (v.active) v.adsr.noteOff();
        }

        void updateOrStartBassAnchorVoice(int slotIdx, int bassMidiNote, float velocity)
        {
            for (auto& v : voices)
            {
                if (v.active && v.slotIndex == slotIdx && v.isBassAnchorVoice)
                {
                    retuneVoiceToMidiNote(v, bassMidiNote);
                    return;
                }
            }
            startVoiceForSlot(slotIdx, bassMidiNote, velocity, true, true);
        }

        void retuneVoiceToMidiNote(PolyVoice& v, int targetMidiNote)
        {
            v.midiNote = targetMidiNote;
            int semiDiff = (targetMidiNote - v.record.rootMidiNote)
                         + padSlots[v.slotIndex].semitoneOffset
                         + globalOctaveShift * 12;
            double srRatio = v.audio->sampleRate / sampleRate;
            v.targetPitchRatio = std::pow(2.0, (double)semiDiff / 12.0) * srRatio;
        }

        void startVoiceForSlot(int slotIdx, int midiNote, float velocity, bool chromaticPitch, bool isBassAnchor = false)
        {
            const auto& slot = padSlots[slotIdx];
            const auto& rec  = padRecords[slotIdx];
            auto audio       = padAudio[slotIdx];
            if (audio == nullptr) return;

            // Ищем свободный голос
            PolyVoice* targetVoice = nullptr;
            for (auto& v : voices)
            {
                if (!v.active) { targetVoice = &v; break; }
            }
            if (targetVoice == nullptr)
                targetVoice = &voices[0];

            double prevPitch = targetVoice->active ? targetVoice->currentPitchRatio : 0.0;

            targetVoice->active = true;
            targetVoice->slotIndex = slotIdx;
            targetVoice->midiNote = midiNote;
            targetVoice->isBassAnchorVoice = isBassAnchor;
            targetVoice->audio = audio;
            targetVoice->record = rec;
            targetVoice->reverse = slot.reverse;
            targetVoice->readPos = 0.0;
            targetVoice->stretcher.reset(0.0);

            int semiShift = 0;
            if (chromaticPitch)
            {
                semiShift = (midiNote - rec.rootMidiNote) + slot.semitoneOffset + globalOctaveShift * 12;
            }
            else
            {
                semiShift = MusicMath::calculatePitchShift(rec.pitchClass, projectKeyRoot, isMinor,
                                                           pitchMode, globalOctaveShift)
                          + slot.semitoneOffset;
            }

            double srRatio = audio->sampleRate / sampleRate;
            targetVoice->targetPitchRatio = std::pow(2.0, (double)semiShift / 12.0) * srRatio;
            targetVoice->currentPitchRatio = (glideTimeSec > 0.005f && prevPitch > 0.0)
                                                ? prevPitch : targetVoice->targetPitchRatio;

            targetVoice->velocityGain = juce::jlimit(0.1f, 1.0f, velocity);
            targetVoice->slotLinearGain = juce::Decibels::decibelsToGain(slot.volumeDb);
            targetVoice->normGain = rec.normGainLinear;

            float panAngle = (slot.pan + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
            targetVoice->panL = std::cos(panAngle);
            targetVoice->panR = std::sin(panAngle);

            targetVoice->adsr.setSampleRate(sampleRate);
            targetVoice->adsr.setParameters(adsrParams);
            targetVoice->adsr.noteOn();
        }

        std::shared_ptr<LoadedSampleAudio> loadAudioFromFile(const juce::File& file)
        {
            if (!file.existsAsFile()) return nullptr;
            std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
            if (reader == nullptr || reader->lengthInSamples <= 0) return nullptr;

            const int numSamples = (int)std::min<juce::int64>(reader->lengthInSamples,
                                                              (juce::int64)(reader->sampleRate * 32.0));
            auto loaded = std::make_shared<LoadedSampleAudio>();
            loaded->sampleRate = reader->sampleRate;
            loaded->buffer.setSize((int)juce::jmin<unsigned int>(2, reader->numChannels), numSamples);
            reader->read(&loaded->buffer, 0, numSamples, 0, true, true);
            loaded->buildWaveformPeaks();
            return loaded;
        }

        void updateReverbParams()
        {
            juce::dsp::Reverb::Parameters rp;
            rp.roomSize   = 0.78f;
            rp.damping    = 0.42f;
            rp.width      = 1.0f;
            rp.wetLevel   = reverbWet * 0.55f;
            rp.dryLevel   = 1.0f - (reverbWet * 0.22f);
            rp.freezeMode = 0.0f;
            reverb.setParameters(rp);
        }

        juce::CriticalSection engineLock;
        juce::AudioFormatManager formatManager;
        double sampleRate = 44100.0;

        int projectKeyRoot = 9; // По умолчанию A
        bool isMinor = true;
        PitchMode pitchMode = PitchMode::ScaleSnap;
        int globalOctaveShift = 0;

        double currentBpm = 128.0;
        bool syncEnabled = true;
        bool dawIsPlaying = false;
        double currentPpq = 0.0;

        BankMode bankMode = BankMode::Chromatic;
        juce::ADSR::Parameters adsrParams { 0.012f, 1.2f, 0.80f, 0.35f };
        float glideTimeSec = 0.0f;
        float reverbWet = 0.35f;

        juce::dsp::Reverb reverb;

        AuditionStream auditionCurrent;
        AuditionStream auditionFadingOut;
        std::atomic<int> lastAuditionedIndex { -1 };

        std::array<PadSlotState, 12> padSlots;
        std::array<SampleRecord, 12> padRecords;
        std::array<std::shared_ptr<LoadedSampleAudio>, 12> padAudio;

        std::array<PolyVoice, 24> voices;
        std::set<int> heldNotes;
    };
}
