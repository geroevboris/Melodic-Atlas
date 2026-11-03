#include "PluginProcessor.h"
#include "PluginEditor.h"

MelodicAtlasAudioProcessor::MelodicAtlasAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      juce::Thread("MelodicAtlasScannerThread")
{
    formatManager.registerBasicFormats();
    samplerEngine.setSampleDatabase(&sampleDatabase);

    // Сначала пытаемся мгновенно загрузить ранее отсканированную пользователем базу с диска
    if (!loadDatabaseCacheFromDisk())
    {
        generateDemoAtlas();
    }
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
        bankReverbAmount.load(),
        bankPitchSemitones.load(),
        static_cast<Atlas::TransposeQuality>(transposeQualityIndex.load()),
        masterVolumeDb.load()
    );

    samplerEngine.renderNextBlock(buffer, midiMessages);
}

void MelodicAtlasAudioProcessor::pushGlobalUndoState()
{
    const juce::ScopedLock sl(dbLock);
    undoStack.push_back(samplerEngine.createSnapshot(primarySelectedPad, globalExportProcessed.load()));
    if (undoStack.size() > 32)
        undoStack.erase(undoStack.begin());
    redoStack.clear();
}

void MelodicAtlasAudioProcessor::performGlobalUndo()
{
    const juce::ScopedLock sl(dbLock);
    if (undoStack.empty())
        return;

    redoStack.push_back(samplerEngine.createSnapshot(primarySelectedPad, globalExportProcessed.load()));
    auto snap = undoStack.back();
    undoStack.pop_back();

    samplerEngine.restoreSnapshot(snap);
    primarySelectedPad = snap.primarySelectedPad;
    globalExportProcessed.store(snap.globalExportProcessed);
}

void MelodicAtlasAudioProcessor::performGlobalRedo()
{
    const juce::ScopedLock sl(dbLock);
    if (redoStack.empty())
        return;

    undoStack.push_back(samplerEngine.createSnapshot(primarySelectedPad, globalExportProcessed.load()));
    auto snap = redoStack.back();
    redoStack.pop_back();

    samplerEngine.restoreSnapshot(snap);
    primarySelectedPad = snap.primarySelectedPad;
    globalExportProcessed.store(snap.globalExportProcessed);
}

void MelodicAtlasAudioProcessor::clearAllPadsWithUndo()
{
    pushGlobalUndoState();
    samplerEngine.clearAllPads();
}

void MelodicAtlasAudioProcessor::setGlobalExportMode(bool processed)
{
    pushGlobalUndoState();
    globalExportProcessed.store(processed);
    for (int i = 0; i < 12; ++i)
        samplerEngine.getPadState(i).exportProcessed = processed;
}

void MelodicAtlasAudioProcessor::assignSampleToPadWithPreviewSettings(int padIdx, int sampleIdx, bool recordGlobalUndo)
{
    const juce::ScopedLock sl(dbLock);
    if (padIdx < 0 || padIdx >= 12 || sampleIdx < 0 || sampleIdx >= (int)sampleDatabase.size())
        return;

    if (recordGlobalUndo)
        pushGlobalUndoState();

    const auto& rec = sampleDatabase[(size_t)sampleIdx];
    bool lockOn = padSettingsLocked.load();

    // Если замочек закрыт (lockOn == true), сохраняем историю внутри пэда и удерживаем настройки пэда
    samplerEngine.assignSampleToPad(padIdx, sampleIdx, rec, true);

    if (!lockOn)
    {
        auto& st = samplerEngine.getPadState(padIdx);
        int previewPitch = Atlas::MusicMath::calculatePitchShift(
            rec.pitchClass,
            projectKeyRoot.load(),
            static_cast<Atlas::ScaleType>(projectScaleType.load()),
            static_cast<Atlas::PitchMode>(pitchModeIndex.load()),
            octaveShift.load()
        ) + (int)std::round(bankPitchSemitones.load());

        st.semitoneOffset = juce::jlimit(-24, 24, previewPitch);
        st.centOffset = 0;
        st.regionStart = 0.0f;
        st.regionEnd = 1.0f;
        st.exportProcessed = globalExportProcessed.load();
    }
}

void MelodicAtlasAudioProcessor::stepSimilarSampleInPad(int padIdx, int direction)
{
    const juce::ScopedLock sl(dbLock);
    if (padIdx < 0 || padIdx >= 12 || sampleDatabase.empty())
        return;

    auto& st = samplerEngine.getPadState(padIdx);
    int curIdx = st.sampleIndex;

    if (curIdx < 0 || curIdx >= (int)sampleDatabase.size())
    {
        bool wantLoops = showLoopsTab.load();
        for (int i = 0; i < (int)sampleDatabase.size(); ++i)
        {
            if (sampleDatabase[(size_t)i].isLoop == wantLoops)
            {
                assignSampleToPadWithPreviewSettings(padIdx, i, true);
                samplerEngine.triggerPadFromUI(padIdx);
                return;
            }
        }
        return;
    }

    const auto& curRec = sampleDatabase[(size_t)curIdx];
    std::vector<int> clusterCandidates;

    for (int i = 0; i < (int)sampleDatabase.size(); ++i)
    {
        if (sampleDatabase[(size_t)i].isLoop == curRec.isLoop &&
            sampleDatabase[(size_t)i].cluster == curRec.cluster)
        {
            clusterCandidates.push_back(i);
        }
    }

    if (clusterCandidates.size() <= 1)
    {
        clusterCandidates.clear();
        for (int i = 0; i < (int)sampleDatabase.size(); ++i)
        {
            if (sampleDatabase[(size_t)i].isLoop == curRec.isLoop)
                clusterCandidates.push_back(i);
        }
    }

    if (clusterCandidates.empty())
        return;

    std::sort(clusterCandidates.begin(), clusterCandidates.end(), [&](int a, int b)
    {
        const auto& ra = sampleDatabase[(size_t)a];
        const auto& rb = sampleDatabase[(size_t)b];
        float scoreA = (float)ra.toneShadeBucket * 10.0f + ra.spectralCentroidNorm * 3.0f + ra.mapX;
        float scoreB = (float)rb.toneShadeBucket * 10.0f + rb.spectralCentroidNorm * 3.0f + rb.mapX;
        if (std::abs(scoreA - scoreB) < 0.0001f)
            return a < b;
        return scoreA < scoreB;
    });

    auto it = std::find(clusterCandidates.begin(), clusterCandidates.end(), curIdx);
    int pos = (it != clusterCandidates.end()) ? (int)std::distance(clusterCandidates.begin(), it) : 0;
    int count = (int)clusterCandidates.size();
    int nextPos = ((pos + (direction >= 0 ? 1 : -1)) % count + count) % count;
    int nextSampleIdx = clusterCandidates[(size_t)nextPos];

    pushGlobalUndoState();
    bool wasLocked = padSettingsLocked.load();
    padSettingsLocked.store(true);
    assignSampleToPadWithPreviewSettings(padIdx, nextSampleIdx, false);
    padSettingsLocked.store(wasLocked);

    samplerEngine.triggerPadFromUI(padIdx);
}

juce::File MelodicAtlasAudioProcessor::getDatabaseCacheFile() const
{
    auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                   .getChildFile("MelodicAtlas");
    dir.createDirectory();
    return dir.getChildFile("ScannedDatabaseCache_v2.xml");
}

void MelodicAtlasAudioProcessor::saveDatabaseCacheToDisk()
{
    const juce::ScopedLock sl(dbLock);
    if (sampleDatabase.empty())
        return;

    juce::ValueTree root("MelodicAtlasDatabaseCache");
    root.setProperty("scannedFolder", folderToScan.getFullPathName(), nullptr);

    for (const auto& rec : sampleDatabase)
    {
        juce::ValueTree item("Sample");
        item.setProperty("path", rec.file.getFullPathName(), nullptr);
        item.setProperty("name", rec.name, nullptr);
        item.setProperty("isLoop", rec.isLoop, nullptr);
        item.setProperty("dur", rec.durationSec, nullptr);
        item.setProperty("bpm", rec.detectedBpm, nullptr);
        item.setProperty("midi", rec.rootMidiNote, nullptr);
        item.setProperty("pc", rec.pitchClass, nullptr);
        item.setProperty("conf", rec.pitchConfidence, nullptr);
        item.setProperty("normGain", rec.normGainLinear, nullptr);
        item.setProperty("centroid", rec.spectralCentroidNorm, nullptr);
        item.setProperty("mobility", rec.pitchMobilityNorm, nullptr);
        item.setProperty("rough", rec.harmonicRoughness, nullptr);
        item.setProperty("mx", rec.mapX, nullptr);
        item.setProperty("my", rec.mapY, nullptr);
        item.setProperty("cluster", (int)rec.cluster, nullptr);
        item.setProperty("subMask", (int)rec.subMask, nullptr);
        item.setProperty("shade", rec.toneShadeBucket, nullptr);
        root.appendChild(item, nullptr);
    }

    if (auto xml = root.createXml())
        xml->writeTo(getDatabaseCacheFile());
}

bool MelodicAtlasAudioProcessor::loadDatabaseCacheFromDisk()
{
    juce::File cacheFile = getDatabaseCacheFile();
    if (!cacheFile.existsAsFile())
        return false;

    auto xml = juce::XmlDocument::parse(cacheFile);
    if (xml == nullptr)
        return false;

    auto root = juce::ValueTree::fromXml(*xml);
    if (!root.isValid() || root.getNumChildren() == 0)
        return false;

    juce::String savedFolder = root.getProperty("scannedFolder", "").toString();
    if (savedFolder.isNotEmpty())
        folderToScan = juce::File(savedFolder);

    std::vector<Atlas::SampleRecord> loaded;
    loaded.reserve((size_t)root.getNumChildren());

    for (int i = 0; i < root.getNumChildren(); ++i)
    {
        auto item = root.getChild(i);
        juce::File f(item.getProperty("path", "").toString());
        if (!f.existsAsFile())
            continue;

        Atlas::SampleRecord rec;
        rec.file = f;
        rec.name = item.getProperty("name", f.getFileNameWithoutExtension()).toString();
        rec.isLoop = (bool)item.getProperty("isLoop", false);
        rec.durationSec = (double)item.getProperty("dur", 1.0);
        rec.detectedBpm = (double)item.getProperty("bpm", 120.0);
        rec.rootMidiNote = (int)item.getProperty("midi", 60);
        rec.pitchClass = (int)item.getProperty("pc", 0);
        rec.pitchConfidence = (float)item.getProperty("conf", 0.8f);
        rec.normGainLinear = (float)item.getProperty("normGain", 1.0f);
        rec.spectralCentroidNorm = (float)item.getProperty("centroid", 0.5f);
        rec.pitchMobilityNorm = (float)item.getProperty("mobility", 0.5f);
        rec.harmonicRoughness = (float)item.getProperty("rough", 0.1f);
        rec.mapX = (float)item.getProperty("mx", 0.5f);
        rec.mapY = (float)item.getProperty("my", 0.5f);
        rec.cluster = static_cast<Atlas::ClusterGroup>((int)item.getProperty("cluster", 1));
        rec.subMask = (uint32_t)(int)item.getProperty("subMask", 0);
        rec.toneShadeBucket = (int)item.getProperty("shade", 1);

        loaded.push_back(std::move(rec));
    }

    if (loaded.empty())
        return false;

    const juce::ScopedLock sl(dbLock);
    sampleDatabase = std::move(loaded);
    samplerEngine.setSampleDatabase(&sampleDatabase);
    databaseVersion.fetch_add(1);
    return true;
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
    newRecords.reserve((size_t)std::min(total, 5000));

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

        {
            const juce::ScopedLock sl(dbLock);
            sampleDatabase = std::move(newRecords);
            samplerEngine.setSampleDatabase(&sampleDatabase);
            databaseVersion.fetch_add(1);
        }

        // Сразу сохраняем отсканированную базу в постоянный кэш на диск
        saveDatabaseCacheToDisk();
    }

    scanning.store(false);
}

void MelodicAtlasAudioProcessor::generateDemoAtlas()
{
    juce::File tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                             .getChildFile("MelodicAtlas_DemoSamplesV4");
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
        uint32_t subMask;
    };

    static const PresetDef defs[] = {
        { "Rhodes_Warm_Am",     57, false, 0.32f, 0.18f, 0.08f, Atlas::ClusterGroup::Keys,    Atlas::Sub_Warm  | Atlas::Sub_Clean   | Atlas::Sub_Sustain },
        { "Analog_Pad_C",       60, false, 0.42f, 0.25f, 0.12f, Atlas::ClusterGroup::Synth,   Atlas::Sub_Pad   | Atlas::Sub_Warm    | Atlas::Sub_Sustain },
        { "Deep_Sub_Bass_G",    43, false, 0.16f, 0.12f, 0.05f, Atlas::ClusterGroup::Bass,    Atlas::Sub_SubBass | Atlas::Sub_Warm  | Atlas::Sub_Sustain },
        { "Vocal_Air_A",        69, false, 0.72f, 0.48f, 0.10f, Atlas::ClusterGroup::Vocal,   Atlas::Sub_Clean | Atlas::Sub_Sustain | Atlas::Sub_Textural },
        { "Acoustic_Pluck_Em",  64, false, 0.58f, 0.65f, 0.10f, Atlas::ClusterGroup::Guitar,  Atlas::Sub_Pluck | Atlas::Sub_Warm    | Atlas::Sub_Clean   },
        { "Cello_Swell_D",      50, false, 0.28f, 0.30f, 0.09f, Atlas::ClusterGroup::Strings, Atlas::Sub_Warm  | Atlas::Sub_Clean   | Atlas::Sub_Sustain },
        { "Shimmer_Keys_F#",    78, false, 0.82f, 0.42f, 0.07f, Atlas::ClusterGroup::Keys,    Atlas::Sub_Clean | Atlas::Sub_Pluck   },
        { "Acid_Synth_Lead_C#", 61, false, 0.68f, 0.55f, 0.72f, Atlas::ClusterGroup::Synth,   Atlas::Sub_Lead  | Atlas::Sub_Noisy   | Atlas::Sub_Sustain },
        { "Noise_Riser_FX_A",   72, false, 0.78f, 0.88f, 0.65f, Atlas::ClusterGroup::FX,      Atlas::Sub_Noisy | Atlas::Sub_RiserSweep | Atlas::Sub_Textural },
        { "Impact_Hit_FX_C",    48, false, 0.50f, 0.75f, 0.55f, Atlas::ClusterGroup::FX,      Atlas::Sub_ImpactHit | Atlas::Sub_Pluck },
        { "Granular_Texture_E", 64, false, 0.54f, 0.70f, 0.48f, Atlas::ClusterGroup::Other,   Atlas::Sub_Textural | Atlas::Sub_Noisy | Atlas::Sub_Sustain },
        { "Clean_Bell_Other_B", 71, false, 0.75f, 0.62f, 0.06f, Atlas::ClusterGroup::Other,   Atlas::Sub_BellMetallic | Atlas::Sub_Clean | Atlas::Sub_Pluck },
        { "Guitar_Loop_120_Am", 57, true,  0.48f, 0.78f, 0.12f, Atlas::ClusterGroup::Guitar,  Atlas::Sub_Warm  | Atlas::Sub_Pluck   | Atlas::Sub_Clean   },
        { "Synth_Arp_120_Cm",   60, true,  0.68f, 0.85f, 0.10f, Atlas::ClusterGroup::Synth,   Atlas::Sub_Clean | Atlas::Sub_Pluck   },
        { "Vox_Choir_120_F",    65, true,  0.74f, 0.62f, 0.14f, Atlas::ClusterGroup::Vocal,   Atlas::Sub_Clean | Atlas::Sub_Sustain },
        { "Sweep_Loop_FX_120",  67, true,  0.80f, 0.90f, 0.55f, Atlas::ClusterGroup::FX,      Atlas::Sub_Noisy | Atlas::Sub_RiserSweep }
    };

    std::vector<Atlas::SampleRecord> demoRecords;
    const double sr = 44100.0;
    juce::Random rng(42);

    for (int idx = 0; idx < 72; ++idx)
    {
        const auto& base = defs[idx % 16];
        int pitchVar = (idx / 16) * 2;
        int midi = base.midiNote + pitchVar;
        float freq = 440.0f * std::pow(2.0f, (float)(midi - 69) / 12.0f);

        juce::String fileName = juce::String(base.name) + "_" + juce::String(idx + 1) + ".wav";
        juce::File outFile = tempDir.getChildFile(fileName);

        double dur = base.isLoop ? 2.0 : 1.5;
        int numSamples = (int)(sr * dur);

        if (!outFile.existsAsFile())
        {
            juce::AudioBuffer<float> buf(2, numSamples);
            for (int i = 0; i < numSamples; ++i)
            {
                double t = (double)i / sr;
                float env = base.isLoop
                    ? (float)(std::min(1.0, t / 0.02) * std::min(1.0, (dur - t) / 0.04))
                    : (float)(std::min(1.0, t / 0.012) * std::exp(-t * ((base.subMask & Atlas::Sub_Pluck) ? 3.5 : 1.3)));

                if (base.subMask & Atlas::Sub_RiserSweep)
                    env = (float)std::pow(t / dur, 1.5) * (float)std::min(1.0, (dur - t) / 0.04);

                float sweepMult = (base.subMask & Atlas::Sub_RiserSweep) ? (1.0f + 0.5f * (float)(t / dur)) : 1.0f;
                float fNow = freq * sweepMult;

                float raw = 0.35f * std::sin((float)(juce::MathConstants<double>::twoPi * fNow * t))
                          + 0.16f * std::sin((float)(juce::MathConstants<double>::twoPi * fNow * 2.0 * t))
                          + 0.08f * std::sin((float)(juce::MathConstants<double>::twoPi * fNow * 3.002 * t));

                if (base.subMask & Atlas::Sub_Noisy)
                    raw = std::tanh(raw * 3.2f) * 0.45f;

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
        rec.subMask = base.subMask;

        demoRecords.push_back(rec);
    }

    Atlas::AudioAnalyzer::relaxMapCoordinates(demoRecords);

    const juce::ScopedLock sl(dbLock);
    sampleDatabase = std::move(demoRecords);
    samplerEngine.setSampleDatabase(&sampleDatabase);

    if (sampleDatabase.size() >= 4)
    {
        samplerEngine.assignSampleToPad(0, 0, sampleDatabase[0], false);
        samplerEngine.assignSampleToPad(1, 1, sampleDatabase[1], false);
        samplerEngine.assignSampleToPad(2, 2, sampleDatabase[2], false);
        samplerEngine.assignSampleToPad(3, 3, sampleDatabase[3], false);
    }

    databaseVersion.fetch_add(1);
}

void MelodicAtlasAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::ValueTree state("MelodicAtlasStateV2");
    state.setProperty("keyRoot", projectKeyRoot.load(), nullptr);
    state.setProperty("scaleType", projectScaleType.load(), nullptr);
    state.setProperty("pitchMode", pitchModeIndex.load(), nullptr);
    state.setProperty("transposeQual", transposeQualityIndex.load(), nullptr);
    state.setProperty("octShift", octaveShift.load(), nullptr);
    state.setProperty("bpmLocked", bpmLockedToHost.load(), nullptr);
    state.setProperty("manualBpm", currentBpm.load(), nullptr);
    state.setProperty("reverb", reverbAmount.load(), nullptr);
    state.setProperty("bankReverb", bankReverbAmount.load(), nullptr);
    state.setProperty("bankPitch", bankPitchSemitones.load(), nullptr);
    state.setProperty("padLocked", padSettingsLocked.load(), nullptr);
    state.setProperty("globalExpProc", globalExportProcessed.load(), nullptr);
    state.setProperty("masterDb", masterVolumeDb.load(), nullptr);
    state.setProperty("bankMode", bankModeIndex.load(), nullptr);
    state.setProperty("showLoops", showLoopsTab.load(), nullptr);
    state.setProperty("folder", folderToScan.getFullPathName(), nullptr);
    state.setProperty("uiW", savedEditorWidth, nullptr);
    state.setProperty("uiH", savedEditorHeight, nullptr);
    state.setProperty("clusterFilter", savedClusterFilter, nullptr);
    state.setProperty("subFilter", (int)savedSubFilterMask, nullptr);
    state.setProperty("primaryPad", primarySelectedPad, nullptr);

    // Сохраняем состояние каждого из 12 пэдов
    juce::ValueTree padsTree("Pads");
    for (int i = 0; i < 12; ++i)
    {
        const auto& st = samplerEngine.getPadState(i);
        const auto* rec = samplerEngine.getPadRecord(i);

        juce::ValueTree padNode("Pad");
        padNode.setProperty("idx", i, nullptr);
        padNode.setProperty("filePath", (rec != nullptr) ? rec->file.getFullPathName() : "", nullptr);
        padNode.setProperty("vol", st.volumeDb, nullptr);
        padNode.setProperty("pan", st.pan, nullptr);
        padNode.setProperty("semi", st.semitoneOffset, nullptr);
        padNode.setProperty("cent", st.centOffset, nullptr);
        padNode.setProperty("regStart", st.regionStart, nullptr);
        padNode.setProperty("regEnd", st.regionEnd, nullptr);
        padNode.setProperty("chrom", st.chromaticActive, nullptr);
        padNode.setProperty("expProc", st.exportProcessed, nullptr);
        padsTree.appendChild(padNode, nullptr);
    }
    state.appendChild(padsTree, nullptr);

    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void MelodicAtlasAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState == nullptr)
        return;

    auto state = juce::ValueTree::fromXml(*xmlState);
    if (!state.isValid())
        return;

    projectKeyRoot.store(state.getProperty("keyRoot", 9));
    projectScaleType.store(state.getProperty("scaleType", 0));
    pitchModeIndex.store(state.getProperty("pitchMode", 0));
    transposeQualityIndex.store(state.getProperty("transposeQual", 0));
    octaveShift.store(state.getProperty("octShift", 0));
    bpmLockedToHost.store(state.getProperty("bpmLocked", true));
    currentBpm.store(state.getProperty("manualBpm", 120.0));
    reverbAmount.store(state.getProperty("reverb", 0.25f));
    bankReverbAmount.store(state.getProperty("bankReverb", 0.30f));
    bankPitchSemitones.store(state.getProperty("bankPitch", 0.0f));
    padSettingsLocked.store(state.getProperty("padLocked", false));
    globalExportProcessed.store(state.getProperty("globalExpProc", true));
    masterVolumeDb.store(state.getProperty("masterDb", 0.0f));
    bankModeIndex.store(state.getProperty("bankMode", 1));
    showLoopsTab.store(state.getProperty("showLoops", false));

    savedEditorWidth = juce::jlimit(920, 1920, (int)state.getProperty("uiW", 1080));
    savedEditorHeight = juce::jlimit(560, 1200, (int)state.getProperty("uiH", 640));
    savedClusterFilter = (int)state.getProperty("clusterFilter", -1);
    savedSubFilterMask = (uint32_t)(int)state.getProperty("subFilter", 0);
    primarySelectedPad = juce::jlimit(0, 11, (int)state.getProperty("primaryPad", 0));

    // Загружаем кэш базы, если она ещё не была подгружена
    loadDatabaseCacheFromDisk();

    juce::String savedFolder = state.getProperty("folder", "").toString();
    if (savedFolder.isNotEmpty())
    {
        juce::File f(savedFolder);
        if (f.isDirectory() && sampleDatabase.empty())
            scanFolderAsync(f);
    }

    // Восстанавливаем все 12 пэдов из сохранённого состояния проекта
    auto padsTree = state.getChildWithName("Pads");
    if (padsTree.isValid())
    {
        const juce::ScopedLock sl(dbLock);
        for (int i = 0; i < padsTree.getNumChildren(); ++i)
        {
            auto padNode = padsTree.getChild(i);
            int padIdx = (int)padNode.getProperty("idx", -1);
            if (padIdx < 0 || padIdx >= 12)
                continue;

            juce::String path = padNode.getProperty("filePath", "").toString();
            auto& st = samplerEngine.getPadState(padIdx);

            if (path.isNotEmpty())
            {
                int foundDbIdx = -1;
                for (int k = 0; k < (int)sampleDatabase.size(); ++k)
                {
                    if (sampleDatabase[(size_t)k].file.getFullPathName() == path)
                    {
                        foundDbIdx = k;
                        break;
                    }
                }

                if (foundDbIdx >= 0)
                {
                    samplerEngine.assignSampleToPad(padIdx, foundDbIdx, sampleDatabase[(size_t)foundDbIdx], false);
                }
                else
                {
                    juce::File f(path);
                    if (f.existsAsFile())
                    {
                        Atlas::SampleRecord rec;
                        if (Atlas::AudioAnalyzer::analyzeFile(f, formatManager, rec))
                        {
                            sampleDatabase.push_back(rec);
                            int newIdx = (int)sampleDatabase.size() - 1;
                            samplerEngine.assignSampleToPad(padIdx, newIdx, sampleDatabase[(size_t)newIdx], false);
                        }
                    }
                }
            }
            else
            {
                st.sampleIndex = -1;
            }

            st.volumeDb = (float)padNode.getProperty("vol", -3.0f);
            st.pan = (float)padNode.getProperty("pan", 0.0f);
            st.semitoneOffset = (int)padNode.getProperty("semi", 0);
            st.centOffset = (int)padNode.getProperty("cent", 0);
            st.regionStart = (float)padNode.getProperty("regStart", 0.0f);
            st.regionEnd = (float)padNode.getProperty("regEnd", 1.0f);
            st.chromaticActive = (bool)padNode.getProperty("chrom", padIdx == 0);
            st.exportProcessed = (bool)padNode.getProperty("expProc", true);
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
