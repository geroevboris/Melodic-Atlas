#include "PluginProcessor.h"
#include "PluginEditor.h"

MelodicAtlasAudioProcessor::MelodicAtlasAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      juce::Thread("MelodicAtlasScannerThread")
{
    formatManager.registerBasicFormats();
    generateDemoAtlas();
}

MelodicAtlasAudioProcessor::~MelodicAtlasAudioProcessor()
{
    stopThread(4000);
}

void MelodicAtlasAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    samplerEngine.prepare(sampleRate, samplesPerBlock);
}

void MelodicAtlasAudioProcessor::releaseResources()
{
}

bool MelodicAtlasAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void MelodicAtlasAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    bool hostPlaying = false;
    double hostPpq = 0.0;
    double hostBpm = currentBpm.load();

    if (auto* playHead = getPlayHead())
    {
        if (auto posInfo = playHead->getPosition())
        {
            if (posInfo->getBpm().hasValue() && bpmLockedToHost.load())
            {
                hostBpm = *posInfo->getBpm();
                currentBpm.store(hostBpm);
            }
            hostPlaying = posInfo->getIsPlaying();
            if (posInfo->getPpqPosition().hasValue())
                hostPpq = *posInfo->getPpqPosition();
        }
    }

    samplerEngine.setMusicalContext(
        projectKeyRoot.load(),
        static_cast<Atlas::ScaleType>(projectScaleType.load()),
        static_cast<Atlas::PitchMode>(pitchModeIndex.load()),
        octaveShift.load(),
        hostBpm,
        bpmLockedToHost.load(),
        hostPlaying,
        hostPpq
    );

    samplerEngine.setEngineControls(
        static_cast<Atlas::BankMode>(bankModeIndex.load()),
        glideTime.load(),
        reverbAmount.load(),
        masterVolumeDb.load()
    );

    samplerEngine.renderNextBlock(buffer, midiMessages);
}

void MelodicAtlasAudioProcessor::scanFolderAsync(const juce::File& folder)
{
    if (!folder.isDirectory())
        return;

    stopThread(2000);
    folderToScan = folder;
    scanning.store(true);
    scanProgress.store(0.0f);
    scannedCount.store(0);
    startThread(juce::Thread::Priority::low);
}

void MelodicAtlasAudioProcessor::run()
{
    juce::Array<juce::File> audioFiles;
    folderToScan.findChildFiles(audioFiles,
                                juce::File::findFiles,
                                true,
                                "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");

    std::vector<Atlas::SampleRecord> newRecords;
    const int total = audioFiles.size();
    newRecords.reserve((size_t)std::min(total, 4000));

    for (int i = 0; i < total; ++i)
    {
        if (threadShouldExit())
            break;

        scanProgress.store((float)(i + 1) / (float)std::max(1, total));

        Atlas::SampleRecord rec;
        if (Atlas::AudioAnalyzer::analyzeFile(audioFiles.getReference(i), formatManager, rec))
        {
            newRecords.push_back(std::move(rec));
            scannedCount.store((int)newRecords.size());
        }
    }

    if (!threadShouldExit() && !newRecords.empty())
    {
        Atlas::AudioAnalyzer::relaxMapCoordinates(newRecords);

        const juce::ScopedLock sl(dbLock);
        sampleDatabase = std::move(newRecords);
        databaseVersion.fetch_add(1);
    }

    scanning.store(false);
}

void MelodicAtlasAudioProcessor::generateDemoAtlas()
{
    juce::File tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                             .getChildFile("MelodicAtlas_DemoSamplesV2");
    tempDir.createDirectory();

    struct PresetDef
    {
        const char* name;
        int midiNote;
        bool isLoop;
        float brightness;
        float mobility;
        float roughness;
        Atlas::ClusterGroup cluster;
        uint32_t timbre;
        uint32_t character;
    };

    static const PresetDef defs[] = {
        { "Rhodes_Warm_Am",     57, false, 0.32f, 0.18f, 0.08f, Atlas::ClusterGroup::Keys,        Atlas::Timbre_Keys,    Atlas::Char_Warm | Atlas::Char_Clean | Atlas::Char_Sustain },
        { "Tape_Pad_C",         60, false, 0.40f, 0.25f, 0.15f, Atlas::ClusterGroup::StringsPad,  Atlas::Timbre_Synth,   Atlas::Char_Warm | Atlas::Char_Ethy  | Atlas::Char_Sustain },
        { "Sub_Bass_G",         43, false, 0.16f, 0.12f, 0.05f, Atlas::ClusterGroup::DeepBass,    Atlas::Timbre_Synth,   Atlas::Char_Deep | Atlas::Char_Warm  | Atlas::Char_Clean   },
        { "Vocal_Air_A",        69, false, 0.72f, 0.48f, 0.10f, Atlas::ClusterGroup::Vocal,       Atlas::Timbre_Vox,     Atlas::Char_Ethy | Atlas::Char_Clean | Atlas::Char_Sustain },
        { "Dust_Pluck_Em",      64, false, 0.58f, 0.65f, 0.12f, Atlas::ClusterGroup::GuitarPluck, Atlas::Timbre_Guitar,  Atlas::Char_Pluck| Atlas::Char_Warm  | Atlas::Char_Clean   },
        { "Cello_Swell_D",      50, false, 0.28f, 0.30f, 0.09f, Atlas::ClusterGroup::StringsPad,  Atlas::Timbre_Strings, Atlas::Char_Deep | Atlas::Char_Warm  | Atlas::Char_Sustain },
        { "Shimmer_Keys_F#",    78, false, 0.82f, 0.42f, 0.07f, Atlas::ClusterGroup::Keys,        Atlas::Timbre_Keys,    Atlas::Char_Ethy | Atlas::Char_Clean | Atlas::Char_Pluck   },
        { "Dirty_Acid_Lead_C#", 61, false, 0.68f, 0.55f, 0.75f, Atlas::ClusterGroup::SynthLead,   Atlas::Timbre_Synth,   Atlas::Char_Dirty| Atlas::Char_Sustain },
        { "Velvet_Choir_F",     65, false, 0.52f, 0.22f, 0.11f, Atlas::ClusterGroup::Vocal,       Atlas::Timbre_Vox,     Atlas::Char_Warm | Atlas::Char_Ethy  | Atlas::Char_Sustain },
        { "Nylon_Pluck_B",      71, false, 0.75f, 0.72f, 0.06f, Atlas::ClusterGroup::GuitarPluck, Atlas::Timbre_Guitar,  Atlas::Char_Clean| Atlas::Char_Pluck },
        { "Guitar_Loop_120_Am", 57, true,  0.48f, 0.78f, 0.12f, Atlas::ClusterGroup::GuitarPluck, Atlas::Timbre_Guitar,  Atlas::Char_Warm | Atlas::Char_Pluck | Atlas::Char_Clean   },
        { "Synth_Arp_128_Cm",   60, true,  0.68f, 0.85f, 0.10f, Atlas::ClusterGroup::SynthLead,   Atlas::Timbre_Synth,   Atlas::Char_Clean| Atlas::Char_Pluck },
        { "Ethereal_Vox_124_F", 65, true,  0.74f, 0.62f, 0.14f, Atlas::ClusterGroup::Vocal,       Atlas::Timbre_Vox,     Atlas::Char_Ethy | Atlas::Char_Sustain },
        { "Strings_Loop_120_D", 62, true,  0.44f, 0.70f, 0.09f, Atlas::ClusterGroup::StringsPad,  Atlas::Timbre_Strings, Atlas::Char_Warm | Atlas::Char_Sustain }
    };

    std::vector<Atlas::SampleRecord> demoRecords;
    const double sr = 44100.0;
    juce::Random rng(42);

    for (int idx = 0; idx < 60; ++idx)
    {
        const auto& base = defs[idx % 14];
        int pitchVar = (idx / 14) * 2;
        int midi = base.midiNote + pitchVar;
        float freq = 440.0f * std::pow(2.0f, (float)(midi - 69) / 12.0f);

        juce::String fileName = juce::String(base.name) + "_" + juce::String(idx + 1) + ".wav";
        juce::File outFile = tempDir.getChildFile(fileName);

        double dur = base.isLoop ? 3.2 : 1.6;
        int numSamples = (int)(sr * dur);

        if (!outFile.existsAsFile())
        {
            juce::AudioBuffer<float> buf(2, numSamples);
            for (int i = 0; i < numSamples; ++i)
            {
                double t = (double)i / sr;
                float env = base.isLoop
                    ? (float)(std::min(1.0, t / 0.02) * std::min(1.0, (dur - t) / 0.05))
                    : (float)(std::min(1.0, t / 0.015) * std::exp(-t * ((base.character & Atlas::Char_Pluck) ? 3.5 : 1.3)));

                float mod = base.isLoop ? (float)(1.0 + 0.15 * std::sin(t * 4.0 * juce::MathConstants<double>::twoPi)) : 1.0f;
                float raw = 0.35f * std::sin((float)(juce::MathConstants<double>::twoPi * freq * t))
                          + 0.16f * std::sin((float)(juce::MathConstants<double>::twoPi * freq * 2.0 * t * mod))
                          + 0.08f * std::sin((float)(juce::MathConstants<double>::twoPi * freq * 3.002 * t));

                if (base.character & Atlas::Char_Dirty)
                    raw = std::tanh(raw * 3.8f) * 0.45f;

                buf.setSample(0, i, raw * env);
                buf.setSample(1, i, raw * env * 0.98f);
            }

            juce::WavAudioFormat wavFormat;
            if (auto stream = std::unique_ptr<juce::FileOutputStream>(outFile.createOutputStream()))
            {
                if (auto writer = std::unique_ptr<juce::AudioFormatWriter>(
                        wavFormat.createWriterFor(stream.get(), sr, 2, 16, {}, 0)))
                {
                    stream.release();
                    writer->writeFromAudioSampleBuffer(buf, 0, numSamples);
                }
            }
        }

        Atlas::SampleRecord rec;
        rec.file = outFile;
        rec.name = outFile.getFileNameWithoutExtension();
        rec.isLoop = base.isLoop;
        rec.isPolyChord = false;
        rec.durationSec = dur;
        rec.detectedBpm = 120.0;
        rec.rootMidiNote = midi;
        rec.pitchClass = ((midi % 12) + 12) % 12;
        rec.pitchConfidence = 0.92f;
        rec.normGainLinear = 1.0f;

        rec.spectralCentroidNorm = juce::jlimit(0.06f, 0.94f, base.brightness + (rng.nextFloat() - 0.5f) * 0.18f);
        rec.pitchMobilityNorm    = juce::jlimit(0.06f, 0.94f, base.mobility   + (rng.nextFloat() - 0.5f) * 0.18f);
        rec.harmonicRoughness    = base.roughness;
        rec.cluster = base.cluster;
        rec.timbreMask = base.timbre;
        rec.characterMask = base.character;

        demoRecords.push_back(rec);
    }

    Atlas::AudioAnalyzer::relaxMapCoordinates(demoRecords);

    if (demoRecords.size() >= 4)
    {
        samplerEngine.assignSampleToPad(0, 0, demoRecords[0]);
        samplerEngine.assignSampleToPad(2, 2, demoRecords[2]);
        samplerEngine.assignSampleToPad(3, 3, demoRecords[3]);
    }

    const juce::ScopedLock sl(dbLock);
    sampleDatabase = std::move(demoRecords);
    databaseVersion.fetch_add(1);
}

void MelodicAtlasAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::ValueTree state("MelodicAtlasState");
    state.setProperty("keyRoot", projectKeyRoot.load(), nullptr);
    state.setProperty("scaleType", projectScaleType.load(), nullptr);
    state.setProperty("pitchMode", pitchModeIndex.load(), nullptr);
    state.setProperty("bpmLocked", bpmLockedToHost.load(), nullptr);
    state.setProperty("manualBpm", currentBpm.load(), nullptr);
    state.setProperty("reverb", reverbAmount.load(), nullptr);
    state.setProperty("masterDb", masterVolumeDb.load(), nullptr);
    state.setProperty("bankMode", bankModeIndex.load(), nullptr);
    state.setProperty("folder", folderToScan.getFullPathName(), nullptr);

    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void MelodicAtlasAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState != nullptr)
    {
        auto state = juce::ValueTree::fromXml(*xmlState);
        if (state.isValid())
        {
            projectKeyRoot.store(state.getProperty("keyRoot", 9));
            projectScaleType.store(state.getProperty("scaleType", 0));
            pitchModeIndex.store(state.getProperty("pitchMode", 0));
            bpmLockedToHost.store(state.getProperty("bpmLocked", true));
            currentBpm.store(state.getProperty("manualBpm", 128.0));
            reverbAmount.store(state.getProperty("reverb", 0.35f));
            masterVolumeDb.store(state.getProperty("masterDb", 0.0f));
            bankModeIndex.store(state.getProperty("bankMode", 1));

            juce::String savedFolder = state.getProperty("folder", "").toString();
            if (savedFolder.isNotEmpty())
            {
                juce::File f(savedFolder);
                if (f.isDirectory())
                    scanFolderAsync(f);
            }
        }
    }
}

juce::AudioProcessorEditor* MelodicAtlasAudioProcessor::createEditor()
{
    return new MelodicAtlasAudioProcessorEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MelodicAtlasAudioProcessor();
}
