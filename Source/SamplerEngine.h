#pragma once
#include <JuceHeader.h>
#include "AtlasTypes.h"
#include <vector>
#include <array>
#include <atomic>

namespace Atlas
{
    struct AuditionVoice
    {
        bool active = false;
        const juce::AudioBuffer<float>* sourceBuffer = nullptr;
        double sampleRate = 44100.0;
        double position = 0.0;
        double playbackRate = 1.0;
        float gain = 0.8f;
        bool isLoop = false;
        double loopStartSample = 0.0;
        double loopEndSample = 0.0;
        bool isReleased = false;
        float releaseEnvelope = 1.0f;
    };

    struct SamplerVoice
    {
        bool active = false;
        int padIndex = -1;
        int midiNote = -1;
        const juce::AudioBuffer<float>* sourceBuffer = nullptr;
        double sampleRate = 44100.0;
        double position = 0.0;
        double playbackRate = 1.0;

        float volumeLinear = 1.0f;
        float panL = 0.707f;
        float panR = 0.707f;

        // Регион сэмпла
        double regionStartSample = 0.0;
        double regionEndSample = 0.0;

        // ADSR огибающая
        enum EnvelopeState { StateAttack, StateDecay, StateSustain, StateRelease, StateIdle };
        EnvelopeState envState = StateIdle;
        float envLevel = 0.0f;
        float attackRate = 0.01f;
        float decayRate = 0.001f;
        float sustainLevel = 0.8f;
        float releaseRate = 0.001f;

        void startNote(int pad, int note, const juce::AudioBuffer<float>* buf, double sr,
                       const PadSlotState& st, double hostBpm, TransposeQuality tq)
        {
            padIndex = pad;
            midiNote = note;
            sourceBuffer = buf;
            sampleRate = sr;
            active = true;

            if (buf == nullptr || buf->getNumSamples() < 16)
            {
                active = false;
                return;
            }

            double totalLen = (double)buf->getNumSamples();
            regionStartSample = juce::jlimit(0.0, totalLen - 10.0, st.regionStart * totalLen);
            regionEndSample   = juce::jlimit(regionStartSample + 10.0, totalLen, st.regionEnd * totalLen);
            position = regionStartSample;

            volumeDbToLinear(st.volumeDb, st.pan);

            double semitones = (double)(st.semitoneOffset + st.centOffset * 0.01);
            playbackRate = std::pow(2.0, semitones / 12.0);

            envState = StateAttack;
            envLevel = 0.0f;
            attackRate  = 1.0f / std::max(1.0, (double)st.attackSec  * sr);
            decayRate   = 1.0f / std::max(1.0, (double)st.decaySec   * sr);
            sustainLevel = juce::jlimit(0.0f, 1.0f, st.sustainLvl);
            releaseRate = 1.0f / std::max(1.0, (double)st.releaseSec * sr);
        }

        void stopNote()
        {
            if (envState != StateIdle && envState != StateRelease)
            {
                envState = StateRelease;
            }
        }

        void volumeDbToLinear(float db, float pan)
        {
            float rawLin = juce::Decibels::decibelsToGain(db);
            volumeLinear = juce::jlimit(0.0f, 2.0f, rawLin);

            float clampedPan = juce::jlimit(-1.0f, 1.0f, pan);
            float angle = (clampedPan + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
            panL = std::cos(angle);
            panR = std::sin(angle);
        }
    };

    struct CachedAudioData
    {
        juce::AudioBuffer<float> buffer;
        std::vector<float> waveformPeaks;
        double originalBpm = 120.0;
    };

    class SamplerEngine
    {
    public:
        SamplerEngine()
        {
            for (auto& st : padStates)
                st.sampleIndex = -1;
        }

        void prepare(double sampleRate, int samplesPerBlock)
        {
            currentSampleRate = sampleRate;
            juce::dsp::ProcessSpec spec { sampleRate, (uint32_t)samplesPerBlock, 2 };
            reverb.prepare(spec);
            updateReverbParameters();
        }

        void setMusicalContext(int keyRoot, ScaleType scaleType, PitchMode pitchMode, int octaveShift,
                               double bpm, bool hostSynced, bool playing, double ppq)
        {
            projectKeyRoot = keyRoot;
            projectScaleType = scaleType;
            projectPitchMode = pitchMode;
            globalOctaveShift = octaveShift;
            currentBpm = bpm;
            isHostBpmSynced = hostSynced;
        }

        void setEngineControls(BankMode bm, float glide, float mapRev, float bankRev, float globalTuning, TransposeQuality tq, float masterDb)
        {
            bankMode = bm;
            globalReverbAmount = mapRev;
            bankReverbAmount = bankRev;
            globalPitchOffset = globalTuning;
            transposeQuality = tq;
            masterVolumeDb = masterDb;
            updateReverbParameters();
        }

        PadSlotState& getPadState(int padIdx)
        {
            return padStates[(size_t)juce::jlimit(0, 11, padIdx)];
        }

        const SampleRecord* getPadRecord(int padIdx) const
        {
            int idx = padStates[(size_t)juce::jlimit(0, 11, padIdx)].sampleIndex;
            if (database != nullptr && idx >= 0 && idx < (int)database->size())
                return &(*database)[(size_t)idx];
            return nullptr;
        }

        std::shared_ptr<CachedAudioData> getPadAudio(int padIdx) const
        {
            int idx = padStates[(size_t)juce::jlimit(0, 11, padIdx)].sampleIndex;
            if (database != nullptr && idx >= 0 && idx < (int)database->size())
            {
                const auto& rec = (*database)[(size_t)idx];
                return getOrLoadAudio(rec.file);
            }
            return nullptr;
        }

        void setSampleDatabase(const std::vector<SampleRecord>* db)
        {
            database = db;
        }

        void assignSampleToPad(int padIdx, int sampleIdx, const SampleRecord& rec)
        {
            if (padIdx < 0 || padIdx > 11) return;
            auto& st = padStates[(size_t)padIdx];

            // Сохраняем текущее состояние в историю Undo перед заменой
            if (st.sampleIndex >= 0)
            {
                PadHistoryEntry entry;
                entry.sampleIndex = st.sampleIndex;
                if (database != nullptr && st.sampleIndex < (int)database->size())
                    entry.record = (*database)[(size_t)st.sampleIndex];
                entry.semitoneOffset = st.semitoneOffset;
                entry.centOffset = st.centOffset;
                entry.regionStart = st.regionStart;
                entry.regionEnd = st.regionEnd;
                st.history.push_back(entry);
                if (st.history.size() > 10)
                    st.history.erase(st.history.begin());
            }

            st.sampleIndex = sampleIdx;
            getOrLoadAudio(rec.file);
        }

        bool undoPadSample(int padIdx)
        {
            if (padIdx < 0 || padIdx > 11) return false;
            auto& st = padStates[(size_t)padIdx];
            if (st.history.empty()) return false;

            auto prev = st.history.back();
            st.history.pop_back();
            st.sampleIndex = prev.sampleIndex;
            st.semitoneOffset = prev.semitoneOffset;
            st.centOffset = prev.centOffset;
            st.regionStart = prev.regionStart;
            st.regionEnd = prev.regionEnd;
            return true;
        }

        void clearAllPads()
        {
            for (auto& st : padStates)
            {
                st.sampleIndex = -1;
                st.history.clear();
            }
        }

        // Предпрослушивание сэмпла на карте с поддержкой удержания (Gate) и полосы прогресса
        void auditionSample(int sampleIdx, const SampleRecord& rec, bool isButtonDown, float& outProgress)
        {
            auditionButtonDown = isButtonDown;
            if (!isButtonDown)
            {
                auditionVoice.isReleased = true;
                outProgress = 0.0f;
                return;
            }

            auto audio = getOrLoadAudio(rec.file);
            if (audio == nullptr || audio->buffer.getNumSamples() < 16)
            {
                outProgress = 0.0f;
                return;
            }

            if (auditionVoice.sourceBuffer != &audio->buffer)
            {
                auditionVoice.sourceBuffer = &audio->buffer;
                auditionVoice.sampleRate = currentSampleRate;
                auditionVoice.position = 0.0;
                auditionVoice.isLoop = rec.isLoop;
                auditionVoice.loopStartSample = 0.0;
                auditionVoice.loopEndSample = (double)audio->buffer.getNumSamples();
                auditionVoice.isReleased = false;
                auditionVoice.releaseEnvelope = 1.0f;

                double targetSemitones = globalPitchOffset + (double)MusicMath::calculatePitchShift(
                    rec.pitchClass, projectKeyRoot, projectScaleType, projectPitchMode, globalOctaveShift);
                auditionVoice.playbackRate = std::pow(2.0, targetSemitones / 12.0);
            }

            auditionVoice.active = true;
            auditionVoice.isReleased = false;

            double totalLen = (double)audio->buffer.getNumSamples();
            outProgress = (totalLen > 1.0) ? (float)(auditionVoice.position / totalLen) : 0.0f;
        }

        void stopAudition()
        {
            auditionVoice.active = false;
            auditionVoice.isReleased = true;
            auditionButtonDown = false;
        }

        std::shared_ptr<CachedAudioData> getAuditionAudio() const
        {
            if (auditionVoice.sourceBuffer != nullptr)
            {
                // Находим закешированный объект по буферу
                for (auto& pair : audioCache)
                {
                    if (&pair.second->buffer == auditionVoice.sourceBuffer)
                        return pair.second;
                }
            }
            return nullptr;
        }

        void triggerPadFromUI(int padIdx)
        {
            if (padIdx < 0 || padIdx > 11) return;
            const auto* rec = getPadRecord(padIdx);
            auto audio = getPadAudio(padIdx);
            if (rec == nullptr || audio == nullptr) return;

            int midi = 36 + padIdx; // C1 .. B1
            for (auto& v : polyVoices)
            {
                if (v.active && v.padIndex == padIdx)
                    v.stopNote();
            }

            for (auto& v : polyVoices)
            {
                if (!v.active)
                {
                    v.startNote(padIdx, midi, &audio->buffer, currentSampleRate,
                                padStates[(size_t)padIdx], currentBpm, transposeQuality);
                    break;
                }
            }
        }

        void handleMidiMessage(const juce::MidiMessage& msg)
        {
            if (msg.isNoteOn())
            {
                int note = msg.getNoteNumber();
                int padIdx = -1;

                if (bankMode == BankMode::PadTrigger)
                {
                    if (note >= 36 && note <= 47) padIdx = note - 36;
                }
                else
                {
                    // Chromatic режим
                    for (int i = 0; i < 12; ++i)
                    {
                        if (padStates[(size_t)i].sampleIndex >= 0 && padStates[(size_t)i].chromaticActive)
                        {
                            padIdx = i;
                            break;
                        }
                    }
                    if (padIdx < 0) padIdx = (note % 12);
                }

                if (padIdx >= 0 && padIdx < 12)
                {
                    const auto* rec = getPadRecord(padIdx);
                    auto audio = getPadAudio(padIdx);
                    if (rec != nullptr && audio != nullptr)
                    {
                        for (auto& v : polyVoices)
                        {
                            if (!v.active)
                            {
                                v.startNote(padIdx, note, &audio->buffer, currentSampleRate,
                                            padStates[(size_t)padIdx], currentBpm, transposeQuality);
                                break;
                            }
                        }
                    }
                }
            }
            else if (msg.isNoteOff())
            {
                int note = msg.getNoteNumber();
                for (auto& v : polyVoices)
                {
                    if (v.active && v.midiNote == note)
                        v.stopNote();
                }
            }
            else if (msg.isAllNotesOff())
            {
                for (auto& v : polyVoices)
                    v.stopNote();
            }
        }

        uint32_t getActivePlayingPadsMask() const
        {
            uint32_t mask = 0;
            for (const auto& v : polyVoices)
            {
                if (v.active && v.padIndex >= 0 && v.padIndex < 12)
                    mask |= (1u << v.padIndex);
            }
            if (auditionVoice.active)
            {
                // Если превью на карте активно, подсветим условный индикатор
            }
            return mask;
        }

        void renderNextBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
        {
            buffer.clear();
            const int numSamples = buffer.getNumSamples();

            juce::MidiBuffer::Iterator iterator(midiMessages);
            juce::MidiMessage msg;
            int samplePos = 0;
            while (iterator.getNextEvent(msg, samplePos))
            {
                handleMidiMessage(msg);
            }

            juce::AudioBuffer<float> dryBankBuffer(buffer.getNumChannels(), numSamples);
            dryBankBuffer.clear();

            // 1. Рендерим голоса банка пэдов
            for (auto& v : polyVoices)
            {
                if (!v.active || v.sourceBuffer == nullptr) continue;

                const auto& src = *v.sourceBuffer;
                int totalLen = src.getNumSamples();

                for (int i = 0; i < numSamples; ++i)
                {
                    if (!v.active) break;

                    // ADSR огибающая
                    if (v.envState == SamplerVoice::StateAttack)
                    {
                        v.envLevel += v.attackRate;
                        if (v.envLevel >= 1.0f) { v.envLevel = 1.0f; v.envState = SamplerVoice::StateDecay; }
                    }
                    else if (v.envState == SamplerVoice::StateDecay)
                    {
                        v.envLevel -= v.decayRate;
                        if (v.envLevel <= v.sustainLevel) { v.envLevel = v.sustainLevel; v.envState = SamplerVoice::StateSustain; }
                    }
                    else if (v.envState == SamplerVoice::StateRelease)
                    {
                        v.envLevel -= v.releaseRate;
                        if (v.envLevel <= 0.0f)
                        {
                            v.envLevel = 0.0f;
                            v.envState = SamplerVoice::StateIdle;
                            v.active = false;
                            break;
                        }
                    }

                    int idx0 = (int)v.position;
                    int idx1 = idx0 + 1;
                    if (idx1 >= totalLen || v.position >= v.regionEndSample)
                    {
                        v.active = false;
                        break;
                    }

                    float frac = (float)(v.position - (double)idx0);
                    float s0L = src.getSample(0, idx0);
                    float s1L = src.getSample(0, idx1);
                    float sampleL = s0L + frac * (s1L - s0L);

                    float sampleR = sampleL;
                    if (src.getNumChannels() > 1)
                    {
                        float s0R = src.getSample(1, idx0);
                        float s1R = src.getSample(1, idx1);
                        sampleR = s0R + frac * (s1R - s0R);
                    }

                    // Микрофейд против щелчков на старте/конце
                    float antiClick = 1.0f;
                    double elapsedFromStart = v.position - v.regionStartSample;
                    double remainingFromEnd = v.regionEndSample - v.position;
                    if (elapsedFromStart < 192.0) antiClick = (float)(elapsedFromStart / 192.0);
                    if (remainingFromEnd < 192.0) antiClick = std::min(antiClick, (float)(remainingFromEnd / 192.0));
                    antiClick = juce::jlimit(0.0f, 1.0f, antiClick);

                    float finalEnv = v.envLevel * antiClick;
                    float outL = sampleL * v.volumeLinear * v.panL * finalEnv;
                    float outR = sampleR * v.volumeLinear * v.panR * finalEnv;

                    dryBankBuffer.addSample(0, i, outL);
                    if (dryBankBuffer.getNumChannels() > 1)
                        dryBankBuffer.addSample(1, i, outR);

                    v.position += v.playbackRate;
                }
            }

            // 2. Рендерим предпрослушку на карте (Audition Voice)
            juce::AudioBuffer<float> auditionBuffer(buffer.getNumChannels(), numSamples);
            auditionBuffer.clear();

            if (auditionVoice.active && auditionVoice.sourceBuffer != nullptr)
            {
                const auto& src = *auditionVoice.sourceBuffer;
                int totalLen = src.getNumSamples();

                for (int i = 0; i < numSamples; ++i)
                {
                    if (!auditionVoice.active) break;

                    if (!auditionButtonDown && !auditionVoice.isReleased)
                    {
                        auditionVoice.isReleased = true;
                    }

                    if (auditionVoice.isReleased)
                    {
                        auditionVoice.releaseEnvelope -= 0.015f; // Быстрый мягкий фэйд при отпускании
                        if (auditionVoice.releaseEnvelope <= 0.0f)
                        {
                            auditionVoice.active = false;
                            break;
                        }
                    }

                    int idx0 = (int)auditionVoice.position;
                    int idx1 = idx0 + 1;
                    if (idx1 >= totalLen || auditionVoice.position >= auditionVoice.loopEndSample)
                    {
                        if (auditionVoice.isLoop && !auditionVoice.isReleased)
                        {
                            auditionVoice.position = auditionVoice.loopStartSample;
                            continue;
                        }
                        else
                        {
                            auditionVoice.active = false;
                            break;
                        }
                    }

                    float frac = (float)(auditionVoice.position - (double)idx0);
                    float s0L = src.getSample(0, idx0);
                    float s1L = src.getSample(0, idx1);
                    float sampleL = s0L + frac * (s1L - s0L);
                    float sampleR = (src.getNumChannels() > 1) ? (src.getSample(1, idx0) + frac * (src.getSample(1, idx1) - src.getSample(0, idx0))) : sampleL;

                    float antiClick = 1.0f;
                    if (auditionVoice.position < 192.0) antiClick = (float)(auditionVoice.position / 192.0);
                    antiClick = juce::jlimit(0.0f, 1.0f, antiClick);

                    float finalEnv = auditionVoice.releaseEnvelope * antiClick;
                    float outL = sampleL * auditionVoice.gain * finalEnv;
                    float outR = sampleR * auditionVoice.gain * finalEnv;

                    auditionBuffer.addSample(0, i, outL);
                    if (auditionBuffer.getNumChannels() > 1)
                        auditionBuffer.addSample(1, i, outR);

                    auditionVoice.position += auditionVoice.playbackRate;
                }
            }

            // 3. Отдельная шина реверберации для банка пэдов
            juce::AudioBuffer<float> revBankBuf(dryBankBuffer.getNumChannels(), numSamples);
            revBankBuf.copyFrom(0, 0, dryBankBuffer, 0, 0, numSamples);
            if (revBankBuf.getNumChannels() > 1)
                revBankBuf.copyFrom(1, 0, dryBankBuffer, 1, 0, numSamples);

            if (bankReverbAmount > 0.001f)
            {
                juce::dsp::AudioBlock<float> block(revBankBuf);
                reverb.process(juce::dsp::ProcessContextReplacing<float>(block));
            }

            // 4. Суммируем банк с ревербом и превью с карты, применяя master volume
            float masterGain = juce::Decibels::decibelsToGain(masterVolumeDb);
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            {
                float* dest = buffer.getWritePointer(ch);
                const float* dry = dryBankBuffer.getReadPointer(ch);
                const float* rev = revBankBuf.getReadPointer(ch);
                const float* aud = auditionBuffer.getReadPointer(ch);

                for (int i = 0; i < numSamples; ++i)
                {
                    float bankMixed = dry[i] * (1.0f - bankReverbAmount) + rev[i] * bankReverbAmount;
                    float total = bankMixed + aud[i];
                    dest[i] = total * masterGain;
                }
            }
        }

    private:
        std::shared_ptr<CachedAudioData> getOrLoadAudio(const juce::File& file) const
        {
            if (!file.existsAsFile()) return nullptr;

            auto it = audioCache.find(file.getFullPathName());
            if (it != audioCache.end())
                return it->second;

            ju2FormatManager.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> reader(ju2FormatManager.createReaderFor(file));
            if (reader == nullptr) return nullptr;

            auto cached = std::make_shared<CachedAudioData>();
            int numSamples = (int)std::min<juce::int64>(reader->lengthInSamples, reader->sampleRate * 15.0);
            cached->buffer.setSize((int)reader->numChannels, numSamples);
            reader->read(&cached->buffer, 0, numSamples, 0, true, true);

            // Генерация пиков для мини-волны в стиле XO
            const int numPeaks = 64;
            cached->waveformPeaks.resize((size_t)numPeaks, 0.0f);
            int blockSize = std::max(1, numSamples / numPeaks);

            const float* chData = cached->buffer.getReadPointer(0);
            for (int p = 0; p < numPeaks; ++p)
            {
                float peak = 0.0f;
                int start = p * blockSize;
                int end = std::min(numSamples, start + blockSize);
                for (int i = start; i < end; ++i)
                {
                    float absVal = std::abs(chData[i]);
                    if (absVal > peak) peak = absVal;
                }
                cached->waveformPeaks[(size_t)p] = peak;
            }

            audioCache[file.getFullPathName()] = cached;
            return cached;
        }

        void updateReverbParameters()
        {
            juce::dsp::Reverb::Parameters p;
            p.roomSize = 0.65f;
            p.damping  = 0.45f;
            p.wetLevel = 0.5f;
            p.dryLevel = 0.5f;
            p.width    = 1.0f;
            p.freezeMode = 0.0f;
            reverb.setParameters(p);
        }

        double currentSampleRate = 44100.0;
        const std::vector<SampleRecord>* database = nullptr;
        std::array<PadSlotState, 12> padStates;

        mutable juce::AudioFormatManager ju2FormatManager;
        mutable std::map<String, std::shared_ptr<CachedAudioData>> audioCache;

        std::array<SamplerVoice, 16> polyVoices;
        AuditionVoice auditionVoice;
        bool auditionButtonDown = false;

        int projectKeyRoot = 9; // A
        ScaleType projectScaleType = ScaleType::NaturalMinor;
        PitchMode projectPitchMode = PitchMode::ScaleSnap;
        int globalOctaveShift = 0;
        double currentBpm = 120.0;
        bool isHostBpmSynced = true;

        BankMode bankMode = BankMode::Chromatic;
        float globalReverbAmount = 0.3f;
        float bankReverbAmount = 0.3f;
        float globalPitchOffset = 0.0f;
        TransposeQuality transposeQuality = TransposeQuality::CleanRepitch;
        float masterVolumeDb = 0.0f;

        juce::dsp::Reverb reverb;
    };
}
