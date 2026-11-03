#pragma once

#include <set>
#include <map>
#include <deque>
#include <vector>
#include <array>
#include <memory>
#include <cmath>
#include <algorithm>
#include <atomic>

#include <JuceHeader.h>
#include "AtlasTypes.h"
#include "AudioAnalyzer.h"
#include "SamplerEngine.h"

class MelodicAtlasAudioProcessor : public juce::AudioProcessor,
                                   private juce::Thread
{
public:
    MelodicAtlasAudioProcessor();
    ~MelodicAtlasAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.5; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    void scanFolderAsync(const juce::File& folder);
    void generateDemoAtlas();

    // Назначение сэмпла в пэд с учётом замка настроек (Lock) и переносом октавы/тона предпрослушивания
    void assignSampleToPadWithPreviewSettings(int padIdx, int sampleIdx);

    // Переключение звука в пэде на ближайший похожий из того же кластера (стрелочки ◄ / ►)
    void stepSimilarSampleInPad(int padIdx, int direction);

    bool isCurrentlyScanning() const { return scanning.load(); }
    float getScanProgress() const { return scanProgress.load(); }
    int getScannedCount() const { return scannedCount.load(); }

    const std::vector<Atlas::SampleRecord>& getSampleDatabase() const { return sampleDatabase; }
    Atlas::SamplerEngine& getSamplerEngine() { return samplerEngine; }

    // Настройки предпрослушивания на карте (не ломают уже добавленные в банк пэды)
    std::atomic<bool> showLoopsTab { false };
    std::atomic<int>  projectKeyRoot { 9 }; // A
    std::atomic<int>  projectScaleType { (int)Atlas::ScaleType::NaturalMinor };
    std::atomic<int>  pitchModeIndex { (int)Atlas::PitchMode::ScaleSnap };
    std::atomic<int>  octaveShift { 0 };
    std::atomic<int>  transposeQualityIndex { (int)Atlas::TransposeQuality::CleanRepitch };

    // Кнопка BPM: true = кнопка горит (синхронизация с DAW активна), false = не горит (ручной BPM)
    std::atomic<double> currentBpm { 120.0 };
    std::atomic<bool>   bpmLockedToHost { true };

    // Настройки банка пэдов и мастер-секции
    std::atomic<int>   bankModeIndex { (int)Atlas::BankMode::Chromatic };
    std::atomic<bool>  padSettingsLocked { false }; // Замок настроек пэдов при замене звука
    std::atomic<float> reverbAmount { 0.25f };      // Реверб предпрослушивания
    std::atomic<float> bankReverbAmount { 0.30f };  // Отдельная ручка реверба для банка пэдов
    std::atomic<float> bankPitchSemitones { 0.0f }; // Общая крутилка высоты тона
    std::atomic<float> masterVolumeDb { 0.0f };
    std::atomic<float> glideTime { 0.0f };

    std::atomic<uint64_t> databaseVersion { 1 };

private:
    void run() override;

    Atlas::SamplerEngine samplerEngine;
    juce::AudioFormatManager formatManager;

    juce::CriticalSection dbLock;
    std::vector<Atlas::SampleRecord> sampleDatabase;
    juce::File folderToScan;

    std::atomic<bool>  scanning { false };
    std::atomic<float> scanProgress { 0.0f };
    std::atomic<int>   scannedCount { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MelodicAtlasAudioProcessor)
};
