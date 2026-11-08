#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "NeuralMapImport.h"
#include "ClapAudioFeatures.h"
#include "ClapColourClusters.h"
#include "SoundIslandLayout.h"
#if defined(ATLAS_ENABLE_NATIVE_CLAP) && ATLAS_ENABLE_NATIVE_CLAP
#include "ClapOnnxEncoder.h"
#include "NativeClapProjection.h"
#include "ClapPluginResources.h"
#endif
static_assert(Atlas::ClapAudioFeatures::frames == 1001 &&
              Atlas::ClapAudioFeatures::bands == 64,
              "Unexpected CLAP HTSAT feature shape");
#include <unordered_map>

MelodicAtlasAudioProcessor::MelodicAtlasAudioProcessor()
    : AudioProcessor(
          BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      juce::Thread("MelodicAtlasScannerThread")
{
    formatManager.registerBasicFormats();
    publishedDatabase = std::make_shared<const std::vector<Atlas::SampleRecord>>();
    samplerEngine.setSampleDatabase(publishedDatabase);

    if (!loadDatabaseCacheFromDisk())
    {
        generateDemoAtlas();
    }
}

MelodicAtlasAudioProcessor::~MelodicAtlasAudioProcessor()
{
    // Request cooperative cancellation and wait for the worker to exit.
    // Do not forcibly terminate a thread that may be accessing members.
    // A blocked disk read may delay destruction until it returns.
    signalThreadShouldExit();
    waitForThreadToExit(-1);
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

void MelodicAtlasAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                              juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    double hostBpm = currentBpm.load();

    if (auto* playHead = getPlayHead())
    {
        if (auto posInfo = playHead->getPosition())
        {
            if (posInfo->getBpm().hasValue())
            {
                double detectedHostBpm = *posInfo->getBpm();
                if (detectedHostBpm > 20.0 && detectedHostBpm < 400.0)
                {
                    hostBpm = detectedHostBpm;
                    currentBpm.store(hostBpm);
                }
            }
        }
    }

    samplerEngine.setMusicalContext(
        projectKeyRoot.load(),
        static_cast<Atlas::ScaleType>(projectScaleType.load()),
        static_cast<Atlas::PitchMode>(pitchModeIndex.load()),
        octaveShift.load(),
        hostBpm,
        bpmLockedToHost.load(),
        globalBpmMultiplier.load()
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
    undoStack.push_back(samplerEngine.createSnapshot(primarySelectedPad));
    if (undoStack.size() > 32)
        undoStack.erase(undoStack.begin());
    redoStack.clear();
}

void MelodicAtlasAudioProcessor::performGlobalUndo()
{
    const juce::ScopedLock sl(dbLock);
    if (undoStack.empty())
        return;

    redoStack.push_back(samplerEngine.createSnapshot(primarySelectedPad));
    auto snap = undoStack.back();
    undoStack.pop_back();

    samplerEngine.restoreSnapshot(snap);
    primarySelectedPad = snap.primarySelectedPad;
}

void MelodicAtlasAudioProcessor::performGlobalRedo()
{
    const juce::ScopedLock sl(dbLock);
    if (redoStack.empty())
        return;

    undoStack.push_back(samplerEngine.createSnapshot(primarySelectedPad));
    auto snap = redoStack.back();
    redoStack.pop_back();

    samplerEngine.restoreSnapshot(snap);
    primarySelectedPad = snap.primarySelectedPad;
}

void MelodicAtlasAudioProcessor::clearAllPadsWithUndo()
{
    pushGlobalUndoState();
    samplerEngine.clearAllPads();
}

void MelodicAtlasAudioProcessor::recalculatePadPitch(int padIdx)
{
    const juce::ScopedLock sl(dbLock);
    if (padIdx < 0 || padIdx >= 12)
        return;

    const auto* rec = samplerEngine.getPadRecord(padIdx);
    if (rec == nullptr)
        return;

    auto& st = samplerEngine.getPadState(padIdx);
    // Retained for old call sites: manual Tune is never auto-overwritten.
    juce::ignoreUnused(rec, st);
}

void MelodicAtlasAudioProcessor::assignSampleToPadWithPreviewSettings(
    int padIdx, int sampleIdx, bool recordGlobalUndo)
{
    const juce::ScopedLock sl(dbLock);
    if (padIdx < 0 || padIdx >= 12 ||
        sampleIdx < 0 || sampleIdx >= (int)sampleDatabase.size())
    {
        return;
    }

    if (recordGlobalUndo)
        pushGlobalUndoState();

    const auto& rec = sampleDatabase[(size_t)sampleIdx];
    bool lockOn = padSettingsLocked.load();

    // Сохраняет предыдущий сэмпл вместе с его старым регионом и настройками в историю Undo пэда
    samplerEngine.assignSampleToPad(padIdx, sampleIdx, rec, true);

    auto& st = samplerEngine.getPadState(padIdx);

    // Регион воспроизведения всегда сбрасывается на полный сэмпл (даже при включенном Lock)
    st.regionStart = 0.0f;
    st.regionEnd = 1.0f;

    if (!lockOn)
    {
        st.padPitchMode = Atlas::PitchMode::Bypass;
        // New pads keep preview varispeed unchanged; user opts into BPM.
        st.padSyncBpm = false;
        st.padSpeedMult = 1.0f;
        st.playbackMode = rec.isLoop
            ? Atlas::PadPlaybackMode::Loop
            : Atlas::PadPlaybackMode::OneShot;

        st.semitoneOffset = juce::jlimit(-24, 24,
            (int)std::round(bankPitchSemitones.load()));
        st.centOffset = 0;

        st.attackSec = 0.005f;
        st.decaySec = 1.2f;
        st.sustainLvl = 1.0f;
        st.releaseSec = 0.05f;
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
        float scoreA = (float)ra.toneShadeBucket * 10.0f +
                       ra.spectralCentroidNorm * 3.0f + ra.mapX;
        float scoreB = (float)rb.toneShadeBucket * 10.0f +
                       rb.spectralCentroidNorm * 3.0f + rb.mapX;

        if (std::abs(scoreA - scoreB) < 0.0001f)
            return a < b;
        return scoreA < scoreB;
    });

    auto it = std::find(clusterCandidates.begin(), clusterCandidates.end(), curIdx);
    int pos = (it != clusterCandidates.end())
            ? (int)std::distance(clusterCandidates.begin(), it) : 0;
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
    auto dir = juce::File::getSpecialLocation(
        juce::File::userApplicationDataDirectory).getChildFile("MelodicAtlas");
    dir.createDirectory();
    return dir.getChildFile("ScannedDatabaseCache_v4.xml");
}

void MelodicAtlasAudioProcessor::saveDatabaseCacheToDisk()
{
    const juce::ScopedLock sl(dbLock);
    if (sampleDatabase.empty())
        return;

    juce::ValueTree root("MelodicAtlasDatabaseCache");
    root.setProperty("scannedFolder", folderToScan.getFullPathName(), nullptr);
    root.setProperty("mapGeometryVersion",
                     geometryVersionCurrent.load()
                         ? Atlas::SoundIslandLayout::geometryVersion : 0,
                     nullptr);

    for (const auto& rec : sampleDatabase)
    {
        juce::ValueTree item("Sample");
        item.setProperty("path", rec.file.getFullPathName(), nullptr);
        item.setProperty("name", rec.name, nullptr);
        item.setProperty("sourceBytes", (juce::int64)rec.sourceBytes, nullptr);
        item.setProperty("sourceModifiedMs", (juce::int64)rec.sourceModifiedMs, nullptr);
        item.setProperty("isLoop", rec.isLoop, nullptr);
        item.setProperty("loopAnalysisVersion", rec.loopAnalysisVersion, nullptr);
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
        item.setProperty("clapEmbeddingValid", rec.clapEmbeddingValid, nullptr);
        item.setProperty("colourGroup", rec.colourGroup, nullptr);
        item.setProperty("islandGroup", rec.islandGroup, nullptr);
        item.setProperty("colourAlgorithmVersion", rec.colourAlgorithmVersion, nullptr);
        if (rec.clapEmbeddingValid)
        {
            juce::MemoryBlock bytes(rec.clapEmbedding.data(),
                rec.clapEmbedding.size() * sizeof(float));
            item.setProperty("clapEmbedding", bytes.toBase64Encoding(), nullptr);
        }
        item.setProperty("neuralMapValid", rec.neuralMapValid, nullptr);
        if (rec.neuralMapValid)
        {
            item.setProperty("neuralMapX", rec.neuralMapX, nullptr);
            item.setProperty("neuralMapY", rec.neuralMapY, nullptr);
        }
        item.setProperty("cluster", (int)rec.cluster, nullptr);
        item.setProperty("subMask", (int)rec.subMask, nullptr);
        item.setProperty("shade", rec.toneShadeBucket, nullptr);
        item.setProperty("identityState", (int)rec.identityState, nullptr);
        item.setProperty("identityCluster", (int)rec.identityCluster, nullptr);
        item.setProperty("percussionState", (int)rec.percussionState, nullptr);
        item.setProperty("percussionConfidence", rec.percussionConfidence, nullptr);
        item.setProperty("identityConfidence", rec.identityConfidence, nullptr);
        item.setProperty("acousticValid", rec.acoustic.valid, nullptr);
        item.setProperty("acousticCentroidHz", rec.acoustic.spectralCentroidHz, nullptr);
        item.setProperty("acousticCentroidNorm", rec.acoustic.spectralCentroidNorm, nullptr);
        item.setProperty("acousticFlatness", rec.acoustic.spectralFlatness, nullptr);
        item.setProperty("acousticLow", rec.acoustic.lowBandRatio, nullptr);
        item.setProperty("acousticMid", rec.acoustic.midBandRatio, nullptr);
        item.setProperty("acousticHigh", rec.acoustic.highBandRatio, nullptr);
        item.setProperty("acousticOnset", rec.acoustic.onsetActivity, nullptr);
        item.setProperty("acousticSustain", rec.acoustic.sustainRatio, nullptr);
        item.setProperty("tonalValid", rec.tonal.valid, nullptr);
        item.setProperty("tonalWindows", rec.tonal.validWindowCount, nullptr);
        item.setProperty("tonalPeriodicity", rec.tonal.periodicity, nullptr);
        item.setProperty("tonalStability", rec.tonal.pitchStability, nullptr);
        item.setProperty("tonalDeviation", rec.tonal.pitchDeviationSemitones, nullptr);
        item.setProperty("tonalMidi", rec.tonal.estimatedMidi, nullptr);
        item.setProperty("pitchStructure", (int)rec.pitchStructure, nullptr);
        item.setProperty("tempoEvidence", (int)rec.tempoEvidence, nullptr);
        item.setProperty("pitchEvidenceStrength", rec.pitchEvidenceStrength, nullptr);
        item.setProperty("tempoEvidenceStrength", rec.tempoEvidenceStrength, nullptr);
        item.setProperty("characterMask", (int)rec.characterMask, nullptr);
        for (size_t feature = 0; feature < rec.characterScores.size(); ++feature)
            item.setProperty("char" + juce::String((int)feature),
                             rec.characterScores[feature], nullptr);
        root.appendChild(item, nullptr);
    }

    if (auto xml = root.createXml())
    {
        // Write a complete cache before replacing the previous valid version.
        // A failed/interrupted write must not truncate the existing database.
        juce::TemporaryFile temporaryCache(getDatabaseCacheFile());
        if (xml->writeTo(temporaryCache.getFile()))
            temporaryCache.overwriteTargetFileWithTemporary();
    }
}

bool MelodicAtlasAudioProcessor::loadDatabaseCacheFromDisk()
{
    juce::File cacheFile = getDatabaseCacheFile();
    // Keep existing libraries usable without forcing a rescan. The legacy
    // classifier output remains available for display, but is not treated
    // as a confident acoustic identity.
    if (!cacheFile.existsAsFile())
        cacheFile = cacheFile.getSiblingFile("ScannedDatabaseCache_v3.xml");
    if (!cacheFile.existsAsFile())
        return false;

    auto xml = juce::XmlDocument::parse(cacheFile);
    if (xml == nullptr)
        return false;

    auto root = juce::ValueTree::fromXml(*xml);
    if (!root.isValid() || root.getNumChildren() == 0)
        return false;

    const int savedMapVersion = (int)root.getProperty("mapGeometryVersion", 0);

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
        rec.sourceBytes = (int64_t)(juce::int64)item.getProperty("sourceBytes", (juce::int64)-1);
        rec.sourceModifiedMs = (int64_t)(juce::int64)item.getProperty("sourceModifiedMs", (juce::int64)-1);
        rec.loopAnalysisVersion =
            (int)item.getProperty("loopAnalysisVersion", 0);
        // Old caches used a duration threshold; never present that guess
        // as a verified loop before the new scan runs.
        rec.isLoop = rec.loopAnalysisVersion >= 1
            && (bool)item.getProperty("isLoop", false);
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
        // This is the scanner's native CLAP vector cache, independent of
        // the old offline Python UMAP manifest.
        rec.colourGroup = (int)item.getProperty("colourGroup", -1);
        rec.islandGroup = (int)item.getProperty("islandGroup", -1);
        rec.cachedColourGroup = rec.colourGroup;
        rec.colourAlgorithmVersion = (int)item.getProperty("colourAlgorithmVersion", 0);
        rec.clapEmbeddingValid = false;
        if ((bool)item.getProperty("clapEmbeddingValid", false))
        {
            juce::MemoryBlock bytes;
            if (bytes.fromBase64Encoding(
                    item.getProperty("clapEmbedding", "").toString())
                && bytes.getSize() == rec.clapEmbedding.size() * sizeof(float))
            {
                std::memcpy(rec.clapEmbedding.data(), bytes.getData(), bytes.getSize());
                rec.clapEmbeddingValid = std::all_of(
                    rec.clapEmbedding.begin(), rec.clapEmbedding.end(),
                    [](float value) { return std::isfinite(value); });
            }
        }
        // Missing fields in legacy caches naturally fall back to DSP.
        const float neuralX = (float)item.getProperty("neuralMapX", 0.5f);
        const float neuralY = (float)item.getProperty("neuralMapY", 0.5f);
        rec.neuralMapValid = (bool)item.getProperty("neuralMapValid", false)
            && std::isfinite(neuralX) && std::isfinite(neuralY)
            && neuralX >= 0.0f && neuralX <= 1.0f
            && neuralY >= 0.0f && neuralY <= 1.0f;
        if (rec.neuralMapValid)
        {
            rec.neuralMapX = neuralX;
            rec.neuralMapY = neuralY;
        }
        int rawCluster = juce::jlimit(0, 6, (int)item.getProperty("cluster", 1));
        rec.cluster = static_cast<Atlas::ClusterGroup>(rawCluster);
        rec.subMask = (uint32_t)(int)item.getProperty("subMask", 0);
        rec.toneShadeBucket = (int)item.getProperty("shade", 1);
        rec.identityState = static_cast<Atlas::IdentityState>(
            juce::jlimit(0, 3, (int)item.getProperty("identityState", 0)));
        rec.identityCluster = static_cast<Atlas::ClusterGroup>(
            juce::jlimit(0, 7, (int)item.getProperty("identityCluster", 7)));
        rec.percussionState = static_cast<Atlas::PercussionState>(
            juce::jlimit(0, 3, (int)item.getProperty("percussionState", 0)));
        rec.percussionConfidence = juce::jlimit(
            0.0f, 1.0f, (float)item.getProperty("percussionConfidence", 0.0f));
        rec.identityConfidence = juce::jlimit(
            0.0f, 1.0f, (float)item.getProperty("identityConfidence", 0.0f));
        rec.acoustic.valid = (bool)item.getProperty("acousticValid", false);
        rec.acoustic.spectralCentroidHz =
            (float)item.getProperty("acousticCentroidHz", 0.0f);
        rec.acoustic.spectralCentroidNorm =
            (float)item.getProperty("acousticCentroidNorm", 0.0f);
        rec.acoustic.spectralFlatness =
            (float)item.getProperty("acousticFlatness", 0.0f);
        rec.acoustic.lowBandRatio =
            (float)item.getProperty("acousticLow", 0.0f);
        rec.acoustic.midBandRatio =
            (float)item.getProperty("acousticMid", 0.0f);
        rec.acoustic.highBandRatio =
            (float)item.getProperty("acousticHigh", 0.0f);
        rec.acoustic.onsetActivity =
            (float)item.getProperty("acousticOnset", 0.0f);
        rec.acoustic.sustainRatio =
            (float)item.getProperty("acousticSustain", 0.0f);
        rec.tonal.valid = (bool)item.getProperty("tonalValid", false);
        rec.tonal.validWindowCount = juce::jlimit(
            0, 3, (int)item.getProperty("tonalWindows", 0));
        rec.tonal.periodicity = juce::jlimit(
            0.0f, 1.0f, (float)item.getProperty("tonalPeriodicity", 0.0f));
        rec.tonal.pitchStability = juce::jlimit(
            0.0f, 1.0f, (float)item.getProperty("tonalStability", 0.0f));
        rec.tonal.pitchDeviationSemitones =
            (float)item.getProperty("tonalDeviation", 0.0f);
        rec.tonal.estimatedMidi =
            (float)item.getProperty("tonalMidi", 0.0f);
        // Legacy cache entries remain explicitly NotEvaluated. Never infer a
        // trusted pitch class or tempo from old placeholder/default values.
        rec.pitchStructure = static_cast<Atlas::PitchStructure>(
            juce::jlimit(0, 3, (int)item.getProperty("pitchStructure", 0)));
        rec.tempoEvidence = static_cast<Atlas::TempoEvidence>(
            juce::jlimit(0, 2, (int)item.getProperty("tempoEvidence", 0)));
        const float savedPitchStrength =
            (float)item.getProperty("pitchEvidenceStrength", 0.0f);
        const float savedTempoStrength =
            (float)item.getProperty("tempoEvidenceStrength", 0.0f);
        rec.pitchEvidenceStrength = std::isfinite(savedPitchStrength)
            ? juce::jlimit(0.0f, 1.0f, savedPitchStrength) : 0.0f;
        rec.tempoEvidenceStrength = std::isfinite(savedTempoStrength)
            ? juce::jlimit(0.0f, 1.0f, savedTempoStrength) : 0.0f;
        // Legacy cache records have no independently evaluated identity.
        // Reuse existing measurements without rescanning the audio.
        if (rec.identityState == Atlas::IdentityState::NotEvaluated &&
            (rec.acoustic.valid || rec.tonal.valid))
            Atlas::SoundIdentityAnalyzer::evaluate(rec.file, rec);
        // When upgrading from a previous v4 cache, compute the gate from
        // already-measured descriptors rather than re-reading the audio.
        // Re-evaluate already measured records under the updated gate,
        // including those previously labelled Melodic or Uncertain.
        if (rec.acoustic.valid && rec.tonal.valid)
            Atlas::PercussionGate::evaluate(rec);
        rec.characterMask = (uint32_t)(int)item.getProperty("characterMask", 0);
        for (size_t feature = 0; feature < rec.characterScores.size(); ++feature)
            rec.characterScores[feature] = juce::jlimit(
                0.0f, 1.0f,
                (float)item.getProperty("char" + juce::String((int)feature), 0.0f));

        loaded.push_back(std::move(rec));
    }

    if (loaded.empty())
        return false;

    Atlas::AudioAnalyzer::relaxMapCoordinates(loaded);
    // Recolour from saved embeddings without requiring an audio rescan.
    Atlas::ClapColourClusters::apply(loaded);
    for (auto& record : loaded)
        record.cachedColourGroup = record.colourGroup;
    // The projected map may be published independently of the legacy cache.
    // This allows updating an existing library without re-decoding its audio.
    // A missing/stale/incomplete manifest leaves the legacy map intact.
    // Native CLAP coordinates already cached must not be silently replaced
    // by a separate legacy Python projection manifest on next DAW launch.
    const bool completeNativeMap = std::all_of(
        loaded.begin(), loaded.end(),
        [](const Atlas::SampleRecord& record)
        { return record.clapEmbeddingValid && record.neuralMapValid; });
    bool migratedGeometry = false;
#if defined(ATLAS_ENABLE_NATIVE_CLAP) && ATLAS_ENABLE_NATIVE_CLAP
    const bool allNativeEmbeddings =
        loaded.size() >= 4 && std::all_of(loaded.begin(), loaded.end(),
            [](const Atlas::SampleRecord& rec) {
                return rec.clapEmbeddingValid;
            });
    if (savedMapVersion < Atlas::SoundIslandLayout::geometryVersion
        && allNativeEmbeddings)
    {
        try
        {
            // One-time full fit; empty old-anchor set deliberately avoids
            // retaining the previous, scattered UMAP coordinates.
            migratedGeometry = Atlas::NativeClapProjection::apply(loaded, {});
            if (migratedGeometry)
                Atlas::SoundIslandLayout::apply(loaded);
        }
        catch (const std::exception& error)
        {
            juce::Logger::writeToLog(
                "CLAP map migration failed: " + juce::String(error.what()));
        }
    }
#endif
    if (!completeNativeMap && !migratedGeometry)
        Atlas::NeuralMapImport::apply(folderToScan, loaded);

    geometryVersionCurrent.store(
        savedMapVersion >= Atlas::SoundIslandLayout::geometryVersion
        || migratedGeometry);
    {
        const juce::ScopedLock sl(dbLock);
        sampleDatabase = std::move(loaded);
        publishedDatabase =
            std::make_shared<const std::vector<Atlas::SampleRecord>>(sampleDatabase);
        samplerEngine.setSampleDatabase(publishedDatabase);
        databaseVersion.fetch_add(1);
    }
    if (migratedGeometry)
        saveDatabaseCacheToDisk();
    return true;
}

void MelodicAtlasAudioProcessor::scanFolderAsync(const juce::File& folder)
{
    if (!folder.isDirectory())
        return;

    // Request cancellation without forcibly killing the scanner.
    // If it is blocked on disk I/O, leave the current scan alone and let
    // the user try again after the worker has exited.
    signalThreadShouldExit();
    if (!waitForThreadToExit(2000))
        return;

    // A second scan of the same library must reuse already analysed
    // records. A new folder still gets a complete fresh scan.
    bool sameLibrary = false;
    {
        const juce::ScopedLock sl(dbLock);
        sameLibrary = folderToScan == folder && !sampleDatabase.empty();
    }
    folderToScan = folder;
    incrementalRefresh.store(sameLibrary);
    scanning.store(true);
    scanProgress.store(0.0f);
    scannedCount.store(0);
    startThread(juce::Thread::Priority::low);
}

void MelodicAtlasAudioProcessor::refreshLibraryAsync()
{
    if (scanning.load() || !folderToScan.isDirectory())
        return;

    signalThreadShouldExit();
    if (!waitForThreadToExit(2000))
        return;

    incrementalRefresh.store(true);
    scanning.store(true);
    scanProgress.store(0.0f);
    scannedCount.store(0);
    startThread(juce::Thread::Priority::low);
}

void MelodicAtlasAudioProcessor::run()
{
    juce::Array<juce::File> audioFiles;
    folderToScan.findChildFiles(
        audioFiles, juce::File::findFiles, true, "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");

    std::vector<Atlas::SampleRecord> newRecords;
    const int total = audioFiles.size();
    newRecords.reserve((size_t)std::min(total, 8000));

    // Snapshot old descriptors before scanning; no lock is held during I/O.
    // Preserve unchanged data, avoiding repeated audio decoding on Refresh.
    std::unordered_map<std::string, Atlas::SampleRecord> unchangedCandidates;
    if (incrementalRefresh.load())
    {
        const juce::ScopedLock sl(dbLock);
        unchangedCandidates.reserve(sampleDatabase.size());
        for (const auto& record : sampleDatabase)
            unchangedCandidates.emplace(record.file.getFullPathName().toStdString(), record);
    }
    bool hasChangedRecords = !incrementalRefresh.load();
#if defined(ATLAS_ENABLE_NATIVE_CLAP) && ATLAS_ENABLE_NATIVE_CLAP
    std::unique_ptr<Atlas::ClapOnnxEncoder> nativeClap;
    bool nativeInferenceFailed = false;
    auto ensureNativeClap = [&]() -> bool
    {
        if (nativeInferenceFailed)
            return false;
        if (nativeClap)
            return true;
        try
        {
            nativeClap = std::make_unique<Atlas::ClapOnnxEncoder>(
                Atlas::nativeClapModelFile());
            return true;
        }
        catch (const std::exception& error)
        {
            nativeInferenceFailed = true;
            juce::Logger::writeToLog(
                "Native CLAP initialization failed: " + juce::String(error.what()));
            return false;
        }
    };
#endif

#if defined(ATLAS_ENABLE_NATIVE_CLAP) && ATLAS_ENABLE_NATIVE_CLAP
    // Only scanner-thread memory is modified. On a fixed-batch ONNX export
    // the encoder safely falls back to single-item inference.
    std::vector<size_t> pendingIndices;
    std::vector<juce::File> pendingFiles;
    pendingIndices.reserve(16);
    pendingFiles.reserve(16);
    auto flushClapBatch = [&]()
    {
        if (pendingFiles.empty())
            return;
        if (ensureNativeClap())
        {
            std::vector<std::array<float, Atlas::ClapOnnxEncoder::embeddingSize>> vectors;
            std::vector<bool> valid;
            nativeClap->embedBatch(pendingFiles, formatManager, vectors, valid);
            for (size_t j = 0; j < pendingIndices.size(); ++j)
            {
                auto& record = newRecords[pendingIndices[j]];
                record.clapEmbeddingValid = j < valid.size() && valid[j];
                if (record.clapEmbeddingValid)
                    record.clapEmbedding = vectors[j];
            }
            // A failure on a single corrupt file must not disable CLAP
            // for the remainder of the library.
        }
        pendingFiles.clear();
        pendingIndices.clear();
    };
#endif

    for (int i = 0; i < total; ++i)
    {
        if (threadShouldExit())
            break;

        scanProgress.store((float)(i + 1) / (float)std::max(1, total));

        const auto& file = audioFiles.getReference(i);
        // Skip unambiguous drum-labeled files before expensive audio decode.
        if (Atlas::PercussionGate::isExplicitDrumFile(file))
            continue;

        const int64_t fileBytes = (int64_t)file.getSize();
        const int64_t modifiedMs = (int64_t)file.getLastModificationTime().toMilliseconds();
        auto old = unchangedCandidates.find(file.getFullPathName().toStdString());
        if (old != unchangedCandidates.end()
            && old->second.sourceBytes == fileBytes
            && old->second.sourceModifiedMs == modifiedMs
            && old->second.loopAnalysisVersion >= 1
#if defined(ATLAS_ENABLE_NATIVE_CLAP) && ATLAS_ENABLE_NATIVE_CLAP
            && old->second.clapEmbeddingValid
#endif
            )
        {
            if (!Atlas::PercussionGate::shouldExcludeFromAtlas(old->second))
                newRecords.push_back(old->second);
            scannedCount.store((int)newRecords.size());
            continue;
        }

        hasChangedRecords = true;
        Atlas::SampleRecord rec;
        if (Atlas::AudioAnalyzer::analyzeFile(file, formatManager, rec)
            && !Atlas::PercussionGate::shouldExcludeFromAtlas(rec))
        {
            rec.sourceBytes = fileBytes;
            rec.sourceModifiedMs = modifiedMs;
            newRecords.push_back(std::move(rec));
#if defined(ATLAS_ENABLE_NATIVE_CLAP) && ATLAS_ENABLE_NATIVE_CLAP
            pendingIndices.push_back(newRecords.size() - 1);
            pendingFiles.push_back(file);
            if (pendingFiles.size() == 16)
                flushClapBatch();
#endif
            scannedCount.store((int)newRecords.size());
        }
    }

#if defined(ATLAS_ENABLE_NATIVE_CLAP) && ATLAS_ENABLE_NATIVE_CLAP
    if (!threadShouldExit())
        flushClapBatch();
#endif

    if (!threadShouldExit() && !newRecords.empty())
    {
        // Cached DSP positions stay fixed if nothing changed. Neural
        // coordinates are refreshed from the manifest when available.
        if (hasChangedRecords || newRecords.size() != unchangedCandidates.size())
        {
            Atlas::AudioAnalyzer::relaxMapCoordinates(newRecords);
            // A refresh must not make existing dots jump. Newly analyzed
            // samples use the fresh geometry; unchanged ones keep positions.
            if (incrementalRefresh.load())
                for (auto& record : newRecords)
                {
                    const auto it = unchangedCandidates.find(
                        record.file.getFullPathName().toStdString());
                    if (it != unchangedCandidates.end()
                        && it->second.sourceBytes == record.sourceBytes
                        && it->second.sourceModifiedMs == record.sourceModifiedMs)
                    {
                        record.mapX = it->second.mapX;
                        record.mapY = it->second.mapY;
                    }
                }
        }
        // Worker thread only; does not touch the audio callback.
        // Native CLAP + UMAP takes priority over legacy external manifests.
        bool projectedByNativeClap = false;
#if defined(ATLAS_ENABLE_NATIVE_CLAP) && ATLAS_ENABLE_NATIVE_CLAP
        if (!nativeInferenceFailed)
        {
            try
            {
                projectedByNativeClap =
                    Atlas::NativeClapProjection::apply(
                        newRecords, unchangedCandidates);
            }
            catch (const std::exception& error)
            {
                juce::Logger::writeToLog(
                    "Native UMAP failed: " + juce::String(error.what()));
            }
        }
#endif
        if (!projectedByNativeClap)
            Atlas::NeuralMapImport::apply(folderToScan, newRecords);
        // The fine CLAP families give each island a stable identity.
        Atlas::ClapColourClusters::apply(newRecords);
        bool islandsReady = projectedByNativeClap;
        if (projectedByNativeClap &&
            (!incrementalRefresh.load() || !geometryVersionCurrent.load()))
            islandsReady = Atlas::SoundIslandLayout::apply(newRecords);
        // An incremental refresh reuses old island coordinates and places
        // newcomers near their existing CLAP neighbours without reshuffling.
        geometryVersionCurrent.store(islandsReady);
        for (auto& record : newRecords)
            record.cachedColourGroup = record.colourGroup;
        {
            const juce::ScopedLock sl(dbLock);
            sampleDatabase = std::move(newRecords);
            publishedDatabase = std::make_shared<const std::vector<Atlas::SampleRecord>>(sampleDatabase);
            samplerEngine.setSampleDatabase(publishedDatabase);
            databaseVersion.fetch_add(1);
        }
        saveDatabaseCacheToDisk();
    }
    scanning.store(false);
}

void MelodicAtlasAudioProcessor::generateDemoAtlas()
{
}

void MelodicAtlasAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::ValueTree state("MelodicAtlasStateV5");
    state.setProperty("keyRoot", projectKeyRoot.load(), nullptr);
    state.setProperty("scaleType", projectScaleType.load(), nullptr);
    state.setProperty("pitchMode", pitchModeIndex.load(), nullptr);
    state.setProperty("transposeQual", transposeQualityIndex.load(), nullptr);
    state.setProperty("octShift", octaveShift.load(), nullptr);
    state.setProperty("bpmLocked", bpmLockedToHost.load(), nullptr);
    state.setProperty("manualBpm", currentBpm.load(), nullptr);
    state.setProperty("bpmMult", globalBpmMultiplier.load(), nullptr);
    state.setProperty("reverb", reverbAmount.load(), nullptr);
    state.setProperty("bankReverb", bankReverbAmount.load(), nullptr);
    state.setProperty("bankPitch", bankPitchSemitones.load(), nullptr);
    state.setProperty("padLocked", padSettingsLocked.load(), nullptr);
    state.setProperty("masterDb", masterVolumeDb.load(), nullptr);
    state.setProperty("bankMode", bankModeIndex.load(), nullptr);
    state.setProperty("showLoops", showLoopsTab.load(), nullptr);
    state.setProperty("folder", folderToScan.getFullPathName(), nullptr);
    state.setProperty("uiW", savedEditorWidth, nullptr);
    state.setProperty("uiH", savedEditorHeight, nullptr);
    state.setProperty("clustersMask", (int)savedClustersMask, nullptr);
    state.setProperty("moodMask", (int)savedMoodMask, nullptr);
    state.setProperty("primaryPad", primarySelectedPad, nullptr);

    for (int c = 0; c < 7; ++c)
    {
        state.setProperty("cSub" + juce::String(c),
                          (int)savedClusterSubs[(size_t)c], nullptr);
    }

    juce::ValueTree padsTree("Pads");
    for (int i = 0; i < 12; ++i)
    {
        const auto& st = samplerEngine.getPadState(i);
        const auto* rec = samplerEngine.getPadRecord(i);

        juce::ValueTree padNode("Pad");
        padNode.setProperty("idx", i, nullptr);
        padNode.setProperty("filePath",
                            (rec != nullptr) ? rec->file.getFullPathName() : "", nullptr);
        padNode.setProperty("sampleName",
                            (rec != nullptr) ? rec->name : "", nullptr);
        padNode.setProperty("pc", (rec != nullptr) ? rec->pitchClass : 0, nullptr);
        padNode.setProperty("bpm", (rec != nullptr) ? rec->detectedBpm : 120.0, nullptr);
        padNode.setProperty("isLoop", (rec != nullptr) ? rec->isLoop : false, nullptr);
        padNode.setProperty("cluster", (rec != nullptr) ? (int)rec->cluster : 1, nullptr);
        padNode.setProperty("normGain", (rec != nullptr) ? rec->normGainLinear : 1.0f, nullptr);

        padNode.setProperty("vol", st.volumeDb, nullptr);
        padNode.setProperty("pan", st.pan, nullptr);
        padNode.setProperty("semi", st.semitoneOffset, nullptr);
        padNode.setProperty("cent", st.centOffset, nullptr);
        padNode.setProperty("regStart", st.regionStart, nullptr);
        padNode.setProperty("regEnd", st.regionEnd, nullptr);
        padNode.setProperty("chrom", st.chromaticActive, nullptr);
        padNode.setProperty("playMode", (int)st.playbackMode, nullptr);
        padNode.setProperty("padSync", st.padSyncBpm, nullptr);
        padNode.setProperty("padMult", st.padSpeedMult, nullptr);
        padNode.setProperty("padPitchMode", (int)st.padPitchMode, nullptr);
        padNode.setProperty("att", st.attackSec, nullptr);
        padNode.setProperty("dec", st.decaySec, nullptr);
        padNode.setProperty("sus", st.sustainLvl, nullptr);
        padNode.setProperty("rel", st.releaseSec, nullptr);

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
    octaveShift.store(0);
    bpmLockedToHost.store(state.getProperty("bpmLocked", true));
    currentBpm.store(state.getProperty("manualBpm", 120.0));
    globalBpmMultiplier.store((float)state.getProperty("bpmMult", 1.0f));
    reverbAmount.store(state.getProperty("reverb", 0.25f));
    bankReverbAmount.store(state.getProperty("bankReverb", 0.30f));
    // Migrate old preview octave into the single unified Tune parameter.
    const float oldPreviewTune = (float)state.getProperty("bankPitch", 0.0f);
    const int oldPreviewOctave = juce::jlimit(-2, 2,
        (int)state.getProperty("octShift", 0));
    bankPitchSemitones.store(juce::jlimit(-24.0f, 24.0f,
        oldPreviewTune + 12.0f * (float)oldPreviewOctave));
    padSettingsLocked.store(state.getProperty("padLocked", false));
    masterVolumeDb.store(state.getProperty("masterDb", 0.0f));
    bankModeIndex.store(state.getProperty("bankMode", 1));
    showLoopsTab.store(state.getProperty("showLoops", false));

    savedEditorWidth = juce::jlimit(920, 1920, (int)state.getProperty("uiW", 1080));
    savedEditorHeight = juce::jlimit(560, 1200, (int)state.getProperty("uiH", 640));
    savedClustersMask = (uint32_t)(int)state.getProperty("clustersMask", 0);
    savedMoodMask = (uint32_t)(int)state.getProperty("moodMask", 0);
    primarySelectedPad = juce::jlimit(0, 11, (int)state.getProperty("primaryPad", 0));

    for (int c = 0; c < 7; ++c)
    {
        savedClusterSubs[(size_t)c] =
            (uint32_t)(int)state.getProperty("cSub" + juce::String(c), 0);
    }

    if (sampleDatabase.empty())
        loadDatabaseCacheFromDisk();

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
                juce::File f(path);
                int foundDbIdx = -1;
                for (int k = 0; k < (int)sampleDatabase.size(); ++k)
                {
                    if (sampleDatabase[(size_t)k].file.getFullPathName() == path)
                    {
                        foundDbIdx = k;
                        break;
                    }
                }

                if (foundDbIdx >= 0 && f.existsAsFile())
                {
                    samplerEngine.assignSampleToPad(
                        padIdx, foundDbIdx, sampleDatabase[(size_t)foundDbIdx], false);
                }
                else if (f.existsAsFile())
                {
                    Atlas::SampleRecord rec;
                    if (Atlas::AudioAnalyzer::analyzeFile(f, formatManager, rec))
                    {
                        sampleDatabase.push_back(rec);
                        int newIdx = (int)sampleDatabase.size() - 1;
                        samplerEngine.assignSampleToPad(
                            padIdx, newIdx, sampleDatabase[(size_t)newIdx], false);
                    }
                }
                else
                {
                    // Внешний SSD отключен: сохраняем слот и помечаем как Missing (!)
                    Atlas::SampleRecord missingRec;
                    missingRec.file = f;
                    missingRec.name = padNode.getProperty(
                        "sampleName", f.getFileNameWithoutExtension()).toString();
                    missingRec.pitchClass = (int)padNode.getProperty("pc", 0);
                    missingRec.detectedBpm = (double)padNode.getProperty("bpm", 120.0);
                    missingRec.isLoop = (bool)padNode.getProperty("isLoop", false);
                    missingRec.cluster = static_cast<Atlas::ClusterGroup>(
                        juce::jlimit(0, 6, (int)padNode.getProperty("cluster", 1)));
                    missingRec.normGainLinear = (float)padNode.getProperty("normGain", 1.0f);

                    samplerEngine.setPadMissingSample(padIdx, missingRec);
                }
            }
            else
            {
                st.sampleIndex = -1;
                st.fileMissing = false;
                st.settingsViewOpen = false;
                st.syncViewOpen = false;
            }

            st.volumeDb = (float)padNode.getProperty("vol", -3.0f);
            st.pan = (float)padNode.getProperty("pan", 0.0f);
            st.semitoneOffset = (int)padNode.getProperty("semi", 0);
            st.centOffset = (int)padNode.getProperty("cent", 0);
            st.regionStart = (float)padNode.getProperty("regStart", 0.0f);
            st.regionEnd = (float)padNode.getProperty("regEnd", 1.0f);
            st.chromaticActive = (bool)padNode.getProperty("chrom", padIdx == 0);
            st.playbackMode = static_cast<Atlas::PadPlaybackMode>(
                juce::jlimit(0, 2, (int)padNode.getProperty("playMode", 0)));
            st.padSyncBpm = (bool)padNode.getProperty("padSync", true);
            st.padSpeedMult = (float)padNode.getProperty("padMult", 1.0f);
            st.padPitchMode = static_cast<Atlas::PitchMode>(
                juce::jlimit(0, 2, (int)padNode.getProperty("padPitchMode", 0)));
            st.attackSec = (float)padNode.getProperty("att", 0.005f);
            st.decaySec = (float)padNode.getProperty("dec", 1.2f);
            st.sustainLvl = (float)padNode.getProperty("sus", 1.0f);
            st.releaseSec = (float)padNode.getProperty("rel", 0.05f);
        }

        // State restoration may have appended files absent from the scan cache.
        // Publish only after all pad restoration work completes.
        publishedDatabase = std::make_shared<const std::vector<Atlas::SampleRecord>>(sampleDatabase);
        samplerEngine.setSampleDatabase(publishedDatabase);
        databaseVersion.fetch_add(1);
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
