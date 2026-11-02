#pragma once

#include <set>
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

    // Управление сканированием папок
    void scanFolderAsync(const juce::File& folder);
    void generateDemoAtlas();

    bool isCurrentlyScanning() const { return scanning.load(); }
    float getScanProgress() const { return scanProgress.load(); }
    int getScannedCount() const { return scannedCount.load(); }

    const std::vector<Atlas::SampleRecord>& getSampleDatabase() const { return sampleDatabase; }
    Atlas::SamplerEngine& getSamplerEngine() { return samplerEngine; }

    // Глобальные параметры интерфейса и движка
    std::atomic<bool> showLoopsTab { false };
    std::atomic<int>  projectKeyRoot { 9 }; // 9 = A
    std::atomic<bool> projectIsMinor { true };
    std::atomic<int>  pitchModeIndex { (int)Atlas::PitchMode::ScaleSnap };
    std::atomic<bool> keyLocked { true };

    std::atomic<double> currentBpm { 128.0 };
    std::atomic<bool>   syncToHostBpm { true };
    std::atomic<int>    octaveShift { 0 };

    std::atomic<int>   bankModeIndex { (int)Atlas::BankMode::Chromatic };
    std::atomic<float> reverbAmount { 0.35f };
    std::atomic<float> adsrAttack { 0.015f };
    std::atomic<float> adsrDecay { 1.20f };
    std::atomic<float> adsrSustain { 0.80f };
    std::atomic<float> adsrRelease { 0.45f };
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
