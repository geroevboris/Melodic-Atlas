#pragma once
#include "AtlasTypes.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <memory>
#include <set>

namespace Atlas
{
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

        bool renderSample(const juce::AudioBuffer<float>& buf,
                          double timeRatio,
                          double pitchRatio,
                          bool loopWrap,
                          float& outL,
                          float& outR)
        {
            const int totalSamples = buf.getNumSamples();
            if (totalSamples < 4) { outL = 0.0f; outR = 0.0f; return false; }

            if (!loopWrap && timelinePos >= (double)(totalSamples - 4))
            {
                outL = 0.0f; outR = 0.0f;
                return false;
            }

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

            timelinePos += timeRatio;
            if (loopWrap)
            {
                while (timelinePos >= (double)totalSamples) timelinePos -= (double)totalSamples;
                while (timelinePos < 0.0) timelinePos += (double)totalSamples;
            }

            grainPosA += pitchRatio;
            grainPosB += pitchRatio;

            double prevPhaseA = phaseA;
            grainPhase += 1.0 / kGrainLengthSamples;

            if (grainPhase >= 1.0)
            {
                grainPhase -= 1.0;
                grainPosA = timelinePos;
            }
            if (prevPhaseA < 0.5 && grainPhase >= 0.5)
            {
                grainPosB = timelinePos;
            }
            return true;
        }
    };

    class SamplerEngine
    {
    public:
        SamplerEngine()
        {
            formatManager.registerBasicFormats();
            for (size_t i = 0; i < 12; ++i)
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

        void setMusicalContext(int keyRoot, ScaleType sType, PitchMode pMode, int octShift,
                               double dawBpm, bool syncToHost, bool hostIsPlaying, double hostPpqPos)
        {
            projectKeyRoot = keyRoot;
            scaleType = sType;
            pitchMode = pMode;
            globalOctaveShift = octShift;
            currentBpm = (dawBpm > 20.0) ? dawBpm : 120.0;
            syncEnabled = syncToHost;
            dawIsPlaying = hostIsPlaying;
            currentPpq = hostPpqPos;
        }

        void setEngineControls(BankMode mode, float glideSec, float reverbAmt, float masterDb)
        {
            bankMode = mode;
            glideTimeSec = juce::jlimit(0.0f, 1.0f, glideSec);
            reverbWet    = juce::jlimit(0.0f, 1.0f, reverbAmt);
            masterGainLinear = juce::Decibels::decibelsToGain(juce::jlimit(-36.0f, 6.0f, masterDb));
            updateReverbParams();
        }

        void auditionSample(int sampleIdx, const SampleRecord& rec)
        {
            auto loaded = loadAudioFromFile(rec.file);
            if (loaded == nullptr) return;

            const juce::ScopedLock sl(engineLock);

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
            auditionCurrent.waitingForBeatSync = (rec.isLoop && syncEnabled && dawIsPlaying);

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

        bool isAuditionPlaying() const { return auditionCurrent.playing; }

        void assignSampleToPad(int padIdx, int sampleIdx, const SampleRecord& rec)
        {
            if (padIdx < 0 || padIdx >= 12) return;
            auto loaded = loadAudioFromFile(rec.file);
            if (loaded == nullptr) return;

            const juce::ScopedLock sl(engineLock);
            auto& slot = padSlots[(size_t)padIdx];
            slot.sampleIndex = sampleIdx;
            slot.chromaticActive = !(rec.isLoop || rec.isPolyChord);
            slot.volumeDb = -3.0f;
            slot.pan = 0.0f;
            slot.semitoneOffset = 0;
            slot.reverse = false;
            slot.attackSec  = 0.015f;
            slot.decaySec   = 1.20f;
            slot.sustainLvl = 0.80f;
            slot.releaseSec = 0.45f;

            padAudio[(size_t)padIdx] = loaded;
            padRecords[(size_t)padIdx] = rec;
        }

        void clearAllPads()
        {
            const juce::ScopedLock sl(engineLock);
            for (size_t i = 0; i < 12; ++i)
            {
                padSlots[i].sampleIndex = -1;
                padAudio[i].reset();
            }
            for (auto& v : voices)
                v.active = false;
            activePlayingPadsMask.store(0);
        }

        PadSlotState& getPadState(int padIdx) { return padSlots[(size_t)juce::jlimit(0, 11, padIdx)]; }
        const PadSlotState& getPadState(int padIdx) const { return padSlots[(size_t)juce::jlimit(0, 11, padIdx)]; }

        const SampleRecord* getPadRecord(int padIdx) const
        {
            if (padIdx < 0 || padIdx >= 12 || padSlots[(size_t)padIdx].sampleIndex < 0) return nullptr;
            return &padRecords[(size_t)padIdx];
        }

        std::shared_ptr<LoadedSampleAudio> getPadAudio(int padIdx) const
        {
            if (padIdx < 0 || padIdx >= 12) return nullptr;
            return padAudio[(size_t)padIdx];
        }

        std::shared_ptr<LoadedSampleAudio> getAuditionAudio() const
        {
            const juce::ScopedLock sl(engineLock);
            return auditionCurrent.audio;
        }

        int getLastAuditionedIndex() const { return lastAuditionedIndex.load(); }
        uint32_t getActivePlayingPadsMask() const { return activePlayingPadsMask.load(); }

        void triggerPadFromUI(int padIdx, float velocity = 0.9f)
        {
            const juce::ScopedLock sl(engineLock);
            if (padIdx < 0 || padIdx >= 12 || padSlots[(size_t)padIdx].sampleIndex < 0) return;

            int previewGateSamples = (int)(sampleRate * 1.15);

            if (bankMode == BankMode::Chromatic && padSlots[(size_t)padIdx].chromaticActive)
            {
                // В режиме Chromatic нота при клике берется от выбранной тоники проекта
                int targetMidi = 48 + ((projectKeyRoot % 12) + 12) % 12;
                for (int s = 0; s < 12; ++s)
                {
                    if (padSlots[(size_t)s].sampleIndex >= 0 && padSlots[(size_t)s].chromaticActive && padAudio[(size_t)s] != nullptr)
                    {
                        bool isBassType = (padRecords[(size_t)s].isLoop || padRecords[(size_t)s].isPolyChord);
                        startVoiceForSlot(s, targetMidi, velocity, true, isBassType, previewGateSamples);
                    }
                }
            }
            else
            {
                startVoiceForSlot(padIdx, 36 + padIdx, velocity, false, false, previewGateSamples);
            }
        }

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

            const float fadeStep = 1.0f / std::max(1.0f, (float)(sampleRate * 0.015));

            // 1. Предпрослушка карты (Audition)
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

            // 2. Голоса 12 пэдов
            uint32_t currentPlayingMask = 0;

            for (auto& v : voices)
            {
                if (!v.active || v.audio == nullptr) continue;

                const auto& slot = padSlots[(size_t)juce::jlimit(0, 11, v.slotIndex)];
                juce::ADSR::Parameters padAdsr {
                    juce::jlimit(0.002f, 5.0f, slot.attackSec),
                    juce::jlimit(0.01f,  5.0f, slot.decaySec),
                    juce::jlimit(0.0f,   1.0f, slot.sustainLvl),
                    juce::jlimit(0.01f,  8.0f, slot.releaseSec)
                };
                v.adsr.setParameters(padAdsr);

                float slotLinearGain = juce::Decibels::decibelsToGain(slot.volumeDb);
                float panAngle = (slot.pan + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
                float panL = std::cos(panAngle);
                float panR = std::sin(panAngle);

                for (int i = 0; i < numSamples; ++i)
                {
                    if (v.autoGateSamplesRemaining > 0)
                    {
                        v.autoGateSamplesRemaining--;
                        if (v.autoGateSamplesRemaining == 0)
                            v.adsr.noteOff();
                    }

                    float env = v.adsr.getNextSample();
                    if (!v.adsr.isActive())
                    {
                        v.active = false;
                        break;
                    }

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

                    float gain = env * v.velocityGain * slotLinearGain * v.normGain;
                    outL[i] += l * gain * panL;
                    outR[i] += r * gain * panR;

                    if (!alive)
                    {
                        v.active = false;
                        v.adsr.reset();
                        break;
                    }
                }

                if (v.active && v.slotIndex >= 0 && v.slotIndex < 12)
                    currentPlayingMask |= (1u << v.slotIndex);
            }

            activePlayingPadsMask.store(currentPlayingMask);

            // 3. ЧИСТЫЙ CONSTANT-POWER DRY/WET КРОССФЕЙД РЕВЕРБЕРАЦИИ (БЕЗ ЛИМИТЕРА!)
            if (reverbWet > 0.005f)
            {
                juce::AudioBuffer<float> wetBuf(2, numSamples);
                wetBuf.makeCopyOf(dryBuf);

                juce::dsp::AudioBlock<float> block(wetBuf);
                juce::dsp::ProcessContextReplacing<float> ctx(block);
                reverb.process(ctx);

                const float dryGain = std::cos(reverbWet * juce::MathConstants<float>::halfPi);
                const float wetGain = std::sin(reverbWet * juce::MathConstants<float>::halfPi) * 0.62f;

                const float* wL = wetBuf.getReadPointer(0);
                const float* wR = wetBuf.getReadPointer(1);

                for (int i = 0; i < numSamples; ++i)
                {
                    outL[i] = outL[i] * dryGain + wL[i] * wetGain;
                    outR[i] = outR[i] * dryGain + wR[i] * wetGain;
                }
            }

            // 4. Применяем только линейную ручку MASTER VOLUME (без нелинейных искажений и лимитеров)
            if (std::abs(masterGainLinear - 1.0f) > 0.001f)
            {
                dryBuf.applyGain(masterGainLinear);
            }

            for (int ch = 0; ch < outputAudio.getNumChannels(); ++ch)
            {
                const float* src = dryBuf.getReadPointer(std::min(ch, 1));
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
            int autoGateSamplesRemaining = -1;

            std::shared_ptr<LoadedSampleAudio> audio;
            SampleRecord record;
            bool reverse = false;

            double readPos = 0.0;
            double currentPitchRatio = 1.0;
            double targetPitchRatio = 1.0;

            float velocityGain = 1.0f;
            float normGain = 1.0f;

            DualGrainLoopStretcher stretcher;
            juce::ADSR adsr;
        };

        double calculateAuditionPitchRatio(const SampleRecord& rec) const
        {
            int semiShift = MusicMath::calculatePitchShift(rec.pitchClass, projectKeyRoot, scaleType,
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
                bool alive = stream.stretcher.renderSample(buf, bpmRatio * srRatio, pitchRatio, false, outL, outR);
                outL *= stream.record.normGainLinear;
                outR *= stream.record.normGainLinear;
                return alive;
            }

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

            // В режиме PadTrigger пересчитываем высоту на лету при смене KEY/SCALE/OCTAVE
            if (bankMode == BankMode::PadTrigger)
            {
                const auto& slot = padSlots[(size_t)juce::jlimit(0, 11, v.slotIndex)];
                int semiShift = MusicMath::calculatePitchShift(v.record.pitchClass, projectKeyRoot, scaleType,
                                                               pitchMode, globalOctaveShift)
                              + slot.semitoneOffset;
                double srRatio = v.audio->sampleRate / sampleRate;
                v.targetPitchRatio = std::pow(2.0, (double)semiShift / 12.0) * srRatio;
            }

            if (v.record.isLoop)
            {
                double bpmRatio = syncEnabled ? (currentBpm / std::max(40.0, v.record.detectedBpm)) : 1.0;
                double srRatio = v.audio->sampleRate / sampleRate;
                bool wrapLoop = (v.autoGateSamplesRemaining < 0);
                return v.stretcher.renderSample(buf, bpmRatio * srRatio, v.currentPitchRatio, wrapLoop, outL, outR);
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
                int slotIdx = (midiNote >= 36 && midiNote < 48) ? (midiNote - 36) : (((midiNote % 12) + 12) % 12);
                if (padSlots[(size_t)slotIdx].sampleIndex >= 0 && padAudio[(size_t)slotIdx] != nullptr)
                    startVoiceForSlot(slotIdx, midiNote, velocity, false, false, -1);
                return;
            }

            int lowestNote = *heldNotes.begin();

            for (int slotIdx = 0; slotIdx < 12; ++slotIdx)
            {
                if (padSlots[(size_t)slotIdx].sampleIndex < 0 || !padSlots[(size_t)slotIdx].chromaticActive || padAudio[(size_t)slotIdx] == nullptr)
                    continue;

                const auto& rec = padRecords[(size_t)slotIdx];
                bool isBassFollowerType = (rec.isLoop || rec.isPolyChord);

                if (isBassFollowerType)
                    updateOrStartBassAnchorVoice(slotIdx, lowestNote, velocity);
                else
                    startVoiceForSlot(slotIdx, midiNote, velocity, true, false, -1);
            }
        }

        void handleNoteOff(int midiNote)
        {
            heldNotes.erase(midiNote);

            if (bankMode == BankMode::PadTrigger)
            {
                for (auto& v : voices)
                    if (v.active && v.midiNote == midiNote)
                        v.adsr.noteOff();
                return;
            }

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
            startVoiceForSlot(slotIdx, bassMidiNote, velocity, true, true, -1);
        }

        void retuneVoiceToMidiNote(PolyVoice& v, int targetMidiNote)
        {
            v.midiNote = targetMidiNote;
            int semiDiff = (targetMidiNote - v.record.rootMidiNote)
                         + padSlots[(size_t)v.slotIndex].semitoneOffset
                         + globalOctaveShift * 12;
            double srRatio = v.audio->sampleRate / sampleRate;
            v.targetPitchRatio = std::pow(2.0, (double)semiDiff / 12.0) * srRatio;
        }

        void startVoiceForSlot(int slotIdx, int midiNote, float velocity, bool chromaticPitch,
                               bool isBassAnchor, int autoGateSamples)
        {
            const auto& slot = padSlots[(size_t)slotIdx];
            const auto& rec  = padRecords[(size_t)slotIdx];
            auto audio       = padAudio[(size_t)slotIdx];
            if (audio == nullptr) return;

            PolyVoice* targetVoice = nullptr;
            if (!chromaticPitch)
            {
                for (auto& v : voices)
                {
                    if (v.active && v.slotIndex == slotIdx)
                    {
                        targetVoice = &v;
                        break;
                    }
                }
            }

            if (targetVoice == nullptr)
            {
                for (auto& v : voices)
                {
                    if (!v.active) { targetVoice = &v; break; }
                }
            }
            if (targetVoice == nullptr)
                targetVoice = &voices[0];

            double prevPitch = targetVoice->active ? targetVoice->currentPitchRatio : 0.0;

            targetVoice->active = true;
            targetVoice->slotIndex = slotIdx;
            targetVoice->midiNote = midiNote;
            targetVoice->isBassAnchorVoice = isBassAnchor;
            targetVoice->autoGateSamplesRemaining = autoGateSamples;
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
                semiShift = MusicMath::calculatePitchShift(rec.pitchClass, projectKeyRoot, scaleType,
                                                           pitchMode, globalOctaveShift)
                          + slot.semitoneOffset;
            }

            double srRatio = audio->sampleRate / sampleRate;
            targetVoice->targetPitchRatio = std::pow(2.0, (double)semiShift / 12.0) * srRatio;
            targetVoice->currentPitchRatio = (glideTimeSec > 0.005f && prevPitch > 0.0)
                                                ? prevPitch : targetVoice->targetPitchRatio;

            targetVoice->velocityGain = juce::jlimit(0.15f, 1.0f, velocity);
            targetVoice->normGain = rec.normGainLinear;

            juce::ADSR::Parameters padAdsr {
                juce::jlimit(0.002f, 5.0f, slot.attackSec),
                juce::jlimit(0.01f,  5.0f, slot.decaySec),
                juce::jlimit(0.0f,   1.0f, slot.sustainLvl),
                juce::jlimit(0.01f,  8.0f, slot.releaseSec)
            };
            targetVoice->adsr.setSampleRate(sampleRate);
            targetVoice->adsr.setParameters(padAdsr);
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
            rp.roomSize   = 0.80f;
            rp.damping    = 0.45f;
            rp.width      = 1.0f;
            rp.wetLevel   = 1.0f;
            rp.dryLevel   = 0.0f;
            rp.freezeMode = 0.0f;
            reverb.setParameters(rp);
        }

        juce::CriticalSection engineLock;
        juce::AudioFormatManager formatManager;
        double sampleRate = 44100.0;

        int projectKeyRoot = 9;
        ScaleType scaleType = ScaleType::NaturalMinor;
        PitchMode pitchMode = PitchMode::ScaleSnap;
        int globalOctaveShift = 0;

        double currentBpm = 128.0;
        bool syncEnabled = true;
        bool dawIsPlaying = false;
        double currentPpq = 0.0;

        BankMode bankMode = BankMode::Chromatic;
        float glideTimeSec = 0.0f;
        float reverbWet = 0.35f;
        float masterGainLinear = 1.0f;

        juce::dsp::Reverb reverb;

        AuditionStream auditionCurrent;
        AuditionStream auditionFadingOut;
        std::atomic<int> lastAuditionedIndex { -1 };
        std::atomic<uint32_t> activePlayingPadsMask { 0 };

        std::array<PadSlotState, 12> padSlots;
        std::array<SampleRecord, 12> padRecords;
        std::array<std::shared_ptr<LoadedSampleAudio>, 12> padAudio;

        std::array<PolyVoice, 48> voices;
        std::set<int> heldNotes;
    };
}
