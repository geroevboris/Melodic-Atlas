#pragma once

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

    const juce::String getName() const override { return "Melodic-Atlas"; }

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

    // --- Управление базой сэмплов и сканированием ---
    void scanFolderAsync(const juce::File& folder);
    bool isCurrentlyScanning() const { return scanning.load(); }
    float getScanProgress() const    { return scanProgress.load(); }
    int getScannedCount() const      { return scannedCount.load(); }

    const std::vector<Atlas::SampleRecord>& getSampleDatabase() const { return sampleDatabase; }
    Atlas::SamplerEngine& getSamplerEngine() { return samplerEngine; }

    // --- Работа с пэдами и назначением звуков ---
    void assignSampleToPadWithPreviewSettings(int padIdx, int sampleIdx, bool recordGlobalUndo = true);
    void stepSimilarSampleInPad(int padIdx, int direction);
    void clearAllPadsWithUndo();
    void setGlobalExportMode(bool processed);

    // --- Глобальная история проекта (Undo / Redo) ---
    void pushGlobalUndoState();
    bool canGlobalUndo() const { return !undoStack.empty(); }
    bool canGlobalRedo() const { return !redoStack.empty(); }
    void performGlobalUndo();
    void performGlobalRedo();

    // --- Постоянный дисковый кэш ---
    void saveDatabaseCacheToDisk();
    bool loadDatabaseCacheFromDisk();

    // --- Атомарные параметры движка и интерфейса ---
    std::atomic<bool> showLoopsTab { false };
    std::atomic<int> projectKeyRoot { 9 }; // A
    std::atomic<int> projectScaleType { (int)Atlas::ScaleType::NaturalMinor };
    std::atomic<int> pitchModeIndex { (int)Atlas::PitchMode::ScaleSnap };
    std::atomic<int> transposeQualityIndex { (int)Atlas::TransposeQuality::CleanRepitch };
    std::atomic<int> octaveShift { 0 };

    std::atomic<bool> bpmLockedToHost { true };
    std::atomic<double> currentBpm { 120.0 };

    std::atomic<int> bankModeIndex { (int)Atlas::BankMode::Chromatic };
    std::atomic<bool> padSettingsLocked { false };
    std::atomic<bool> globalExportProcessed { true }; // true = [P], false = [D]

    std::atomic<float> glideTime { 0.0f };
    std::atomic<float> reverbAmount { 0.25f };       // MAP REV
    std::atomic<float> bankReverbAmount { 0.30f };   // BANK REV
    std::atomic<float> bankPitchSemitones { 0.0f };  // TUNE предпрослушивания
    std::atomic<float> masterVolumeDb { 0.0f };      // MAIN громкость

    std::atomic<uint64_t> databaseVersion { 1 };

    // Состояние UI, сохраняемое при закрытии/открытии окна плагина
    int savedEditorWidth = 1080;
    int savedEditorHeight = 640;
    int savedClusterFilter = -1;
    uint32_t savedSubFilterMask = 0;
    int primarySelectedPad = 0;

private:
    void run() override;
    void generateDemoAtlas();
    juce::File getDatabaseCacheFile() const;

    juce::CriticalSection dbLock;
    juce::AudioFormatManager formatManager;
    std::vector<Atlas::SampleRecord> sampleDatabase;
    Atlas::SamplerEngine samplerEngine;

    std::vector<Atlas::ProjectSnapshot> undoStack;
    std::vector<Atlas::ProjectSnapshot> redoStack;

    juce::File folderToScan;
    std::atomic<bool> scanning { false };
    std::atomic<float> scanProgress { 0.0f };
    std::atomic<int> scannedCount { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MelodicAtlasAudioProcessor)
};
