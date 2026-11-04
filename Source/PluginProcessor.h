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

    void scanFolderAsync(const juce::File& folder);
    bool isCurrentlyScanning() const { return scanning.load(); }
    float getScanProgress() const    { return scanProgress.load(); }
    int getScannedCount() const      { return scannedCount.load(); }

    const std::vector<Atlas::SampleRecord>& getSampleDatabase() const
    {
        return sampleDatabase;
    }

    Atlas::SamplerEngine& getSamplerEngine()
    {
        return samplerEngine;
    }

    void assignSampleToPadWithPreviewSettings(int padIdx,
                                              int sampleIdx,
                                              bool recordGlobalUndo = true);
    void stepSimilarSampleInPad(int padIdx, int direction);
    void recalculatePadPitch(int padIdx);
    void clearAllPadsWithUndo();

    void pushGlobalUndoState();
    bool canGlobalUndo() const { return !undoStack.empty(); }
    bool canGlobalRedo() const { return !redoStack.empty(); }
    void performGlobalUndo();
    void performGlobalRedo();

    void saveDatabaseCacheToDisk();
    bool loadDatabaseCacheFromDisk();

    std::atomic<bool> showLoopsTab { false };
    std::atomic<int> projectKeyRoot { 9 };
    std::atomic<int> projectScaleType { (int)Atlas::ScaleType::NaturalMinor };
    std::atomic<int> pitchModeIndex { (int)Atlas::PitchMode::ScaleSnap };
    std::atomic<int> transposeQualityIndex { (int)Atlas::TransposeQuality::CleanRepitch };
    std::atomic<int> octaveShift { 0 };

    std::atomic<bool> bpmLockedToHost { true };
    std::atomic<double> currentBpm { 120.0 };
    std::atomic<float> globalBpmMultiplier { 1.0f };

    std::atomic<int> bankModeIndex { (int)Atlas::BankMode::Chromatic };
    std::atomic<bool> padSettingsLocked { false };

    std::atomic<float> glideTime { 0.0f };
    std::atomic<float> reverbAmount { 0.25f };
    std::atomic<float> bankReverbAmount { 0.30f };
    std::atomic<float> bankPitchSemitones { 0.0f };
    std::atomic<float> masterVolumeDb { 0.0f };

    std::atomic<uint64_t> databaseVersion { 1 };

    int savedEditorWidth = 1080;
    int savedEditorHeight = 640;
    uint32_t savedClustersMask = 0;
    std::array<uint32_t, 7> savedClusterSubs = { 0, 0, 0, 0, 0, 0, 0 };
    uint32_t savedMoodMask = 0;
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
