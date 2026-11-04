#include "PluginEditor.h"

MelodicAtlasAudioProcessorEditor::MelodicAtlasAudioProcessorEditor(MelodicAtlasAudioProcessor& p)
    : AudioProcessorEditor(&p),
      audioProcessor(p),
      mapCanvas(p.getSamplerEngine())
{
    setLookAndFeel(&atlasLookAndFeel);

    selectedClusterFilter = audioProcessor.savedClusterFilter;
    activeSubFilterMask = audioProcessor.savedSubFilterMask;
    primarySelectedPad = audioProcessor.primarySelectedPad;
    selectedPads.clear();
    selectedPads.insert(primarySelectedPad);

    setResizable(true, true);
    setResizeLimits(920, 560, 1920, 1200);
    setSize(audioProcessor.savedEditorWidth, audioProcessor.savedEditorHeight);

    addAndMakeVisible(mapCanvas);
    mapCanvas.setSampleDatabase(&audioProcessor.getSampleDatabase());
    mapCanvas.setViewFilter(audioProcessor.showLoopsTab.load(), selectedClusterFilter, activeSubFilterMask);

    mapCanvas.onSampleRightClicked = [this](int sampleIdx)
    {
        if (selectedPads.empty()) selectedPads.insert(primarySelectedPad);
        for (int pIdx : selectedPads)
        {
            audioProcessor.assignSampleToPadWithPreviewSettings(pIdx, sampleIdx, true);
            primarySelectedPad = pIdx;
            audioProcessor.primarySelectedPad = pIdx;
        }
        audioProcessor.getSamplerEngine().triggerPadFromUI(primarySelectedPad);
        repaint();
    };

    mapCanvas.onSampleDragged = [this](int sampleIdx, juce::Point<int> globalPos, bool finished)
    {
        auto localPt = getLocalPoint(nullptr, globalPos).toFloat();
        const auto& db = audioProcessor.getSampleDatabase();
        if (sampleIdx < 0 || sampleIdx >= (int)db.size()) return;

        int overPad = -1;
        for (int i = 0; i < 12; ++i)
        {
            if (padRects[(size_t)i].contains(localPt)) { overPad = i; break; }
        }

        if (overPad != hoveredDropPadIndex)
        {
            hoveredDropPadIndex = overPad;
            repaint();
        }

        if (finished)
        {
            hoveredDropPadIndex = -1;
            if (overPad >= 0)
            {
                audioProcessor.assignSampleToPadWithPreviewSettings(overPad, sampleIdx, true);
                selectedPads.clear();
                selectedPads.insert(overPad);
                primarySelectedPad = overPad;
                audioProcessor.primarySelectedPad = overPad;
                audioProcessor.getSamplerEngine().triggerPadFromUI(overPad);
                repaint();
                return;
            }
        }
        else if (localPt.x < 6.0f || localPt.x > (float)getWidth() - 6.0f ||
                 localPt.y < 6.0f || localPt.y > (float)getHeight() - 6.0f)
        {
            hoveredDropPadIndex = -1;
            if (db[(size_t)sampleIdx].file.existsAsFile())
            {
                juce::StringArray files;
                files.add(db[(size_t)sampleIdx].file.getFullPathName());
                performExternalDragDropOfFiles(files, false);
            }
        }
    };

    auto setupEncoder = [this](juce::Slider& s, const juce::String& insideName, const juce::String& tip,
                               float minV, float maxV, float defV, std::function<void(float)> onChange)
    {
        s.setName(insideName);
        s.setTooltip(tip);
        s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        s.setRange(minV, maxV);
        s.setValue(defV, juce::dontSendNotification);
        s.onValueChange = [onChange, &s]() { onChange((float)s.getValue()); };
        addAndMakeVisible(s);
    };

    setupEncoder(previewTuneSlider, "TUNE", "Preview Pitch Transpose (Semitones)",
                 -12.0f, 12.0f, audioProcessor.bankPitchSemitones.load(),
                 [this](float v) { audioProcessor.bankPitchSemitones.store(v); });

    setupEncoder(mapReverbSlider, "REV", "Map Preview Reverb Amount",
                 0.0f, 1.0f, audioProcessor.reverbAmount.load(),
                 [this](float v) { audioProcessor.reverbAmount.store(v); });

    setupEncoder(bankReverbSlider, "REV", "Bank Pads Reverb Amount",
                 0.0f, 1.0f, audioProcessor.bankReverbAmount.load(),
                 [this](float v) { audioProcessor.bankReverbAmount.store(v); });

    setupEncoder(masterVolSlider, "MAIN", "Main Output Volume (dB)",
                 -24.0f, 6.0f, audioProcessor.masterVolumeDb.load(),
                 [this](float v) { audioProcessor.masterVolumeDb.store(v); });

    updateDynamicLayoutRects();
    lastSeenDbVersion = audioProcessor.databaseVersion.load();
    startTimerHz(30);
}

MelodicAtlasAudioProcessorEditor::~MelodicAtlasAudioProcessorEditor() { setLookAndFeel(nullptr); }

void MelodicAtlasAudioProcessorEditor::resized()
{
    audioProcessor.savedEditorWidth = getWidth();
    audioProcessor.savedEditorHeight = getHeight();
    updateDynamicLayoutRects();
}

void MelodicAtlasAudioProcessorEditor::updateDynamicLayoutRects()
{
    const float w = (float)getWidth();
    const float h = (float)getHeight();
    const float filterY = 46.0f;
    const float bankSectionH = std::max(200.0f, h * 0.32f); // Увеличили высоту для 2 рядов энкодеров ADSR
    const float bankTopY = h - bankSectionH - 10.0f;
    const float mapY = filterY + 74.0f + 6.0f;
    const float mapH = std::max(180.0f, bankTopY - mapY - 8.0f);
    
    mapCanvas.setBounds(14, (int)mapY, (int)w - 28, (int)mapH);

    btnGlobalUndo      = { 156.0f, 8.0f, 26.0f, 24.0f };
    btnGlobalRedo      = { 186.0f, 8.0f, 26.0f, 24.0f };
    btnOneShots        = { 222.0f, 8.0f, 84.0f, 24.0f };
    btnLoops           = { 310.0f, 8.0f, 62.0f, 24.0f };
    btnKeyRoot         = { 414.0f, 8.0f, 42.0f, 24.0f };
    btnScaleType       = { 460.0f, 8.0f, 78.0f, 24.0f };
    btnPitchMode       = { 542.0f, 8.0f, 52.0f, 24.0f };
    btnTransposeQuality= { 598.0f, 8.0f, 68.0f, 24.0f };
    btnBpmSync         = { 706.0f, 8.0f, 62.0f, 24.0f };
    btnScanFolder      = { w - 178.0f, 8.0f, 164.0f, 24.0f };

    const juce::Font filterFont(juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::bold);
    float gx = 22.0f;
    float row1Y = filterY + 8.0f;

    float allW = filterFont.getStringWidthFloat("ALL") + 18.0f;
    groupFilterRects[0] = { gx, row1Y, allW, 24.0f };
    gx += allW + 6.0f;

    for (int c = 0; c < 8; ++c)
    {
        auto cg = static_cast<Atlas::ClusterGroup>(c);
        juce::String baseLabel = Atlas::MusicMath::getClusterName(cg);
        uint32_t subBit = selectedClusterSubgroupBit[(size_t)c];
        if (subBit != 0)
        {
            juce::String subShort = Atlas::MusicMath::getSubgroupShortLabel(cg, subBit);
            if (subShort.isNotEmpty()) baseLabel += ":" + subShort;
        }

        float textW = filterFont.getStringWidthFloat(baseLabel);
        float btnW = textW + 28.0f;
        groupFilterRects[(size_t)(c + 1)] = { gx, row1Y, btnW, 24.0f };
        groupDropdownRects[(size_t)c]     = { gx + btnW - 18.0f, row1Y, 18.0f, 24.0f };
        gx += btnW + 6.0f;
    }

    static const char* subNames[7] = { "CLEAN", "NOISY", "WARM", "PLUCK", "SUSTAIN", "RISER/SWEEP", "TEXTURAL" };
    float sx = 22.0f;
    float row2Y = filterY + 40.0f;
    for (int i = 0; i < 7; ++i)
    {
        float tw = filterFont.getStringWidthFloat(subNames[i]) + 18.0f;
        subFilterRects[(size_t)i] = { sx, row2Y, tw, 22.0f };
        sx += tw + 6.0f;
    }

    float octBlockRight = w - 22.0f;
    float octBlockX = octBlockRight - 86.0f;
    octLabelRect = { octBlockX, filterY + 7.0f, 86.0f, 16.0f };
    btnOctDown   = { octBlockX, filterY + 26.0f, 40.0f, 24.0f };
    btnOctUp     = { octBlockX + 46.0f, filterY + 26.0f, 40.0f, 24.0f };

    mapReverbSlider.setBounds  ((int)(octBlockX - 58.0f),  (int)(filterY + 13.0f), 46, 46);
    previewTuneSlider.setBounds((int)(octBlockX - 112.0f), (int)(filterY + 13.0f), 46, 46);

    btnPadTriggerMode = { 66.0f,  bankTopY, 102.0f, 20.0f };
    btnChromaticMode  = { 174.0f, bankTopY, 96.0f,  20.0f };

    const float rightPanelW = 168.0f;
    const float gridTopY = bankTopY + 26.0f;
    const float gridAvailH = h - gridTopY - 12.0f;
    const float gridAvailW = w - 28.0f - rightPanelW - 10.0f;

    const float gapX = 7.0f;
    const float gapY = 8.0f;
    const float padW = (gridAvailW - gapX * 5.0f) / 6.0f;
    const float padH = (gridAvailH - gapY) / 2.0f;

    for (int i = 0; i < 12; ++i)
    {
        int row = i / 6;
        int col = i % 6;
        float px = 14.0f + (float)col * (padW + gapX);
        float py = gridTopY + (float)row * (padH + gapY);
        padRects[(size_t)i] = { px, py, padW, padH };
    }

    float rpX = 14.0f + gridAvailW + 10.0f;
    rightBankPanelRect = { rpX, gridTopY, rightPanelW, gridAvailH };

    int knobSize = (int)std::min(56.0f, gridAvailH * 0.44f);
    int knobY = (int)(rightBankPanelRect.getY() + 10.0f);
    bankReverbSlider.setBounds((int)(rpX + rightPanelW * 0.27f) - knobSize / 2, knobY, knobSize, knobSize);
    masterVolSlider.setBounds ((int)(rpX + rightPanelW * 0.73f) - knobSize / 2, knobY, knobSize, knobSize);

    float iconRowY = rightBankPanelRect.getBottom() - 40.0f;
    float iconGap = 6.0f;
    float iconW = (rightPanelW - 20.0f - iconGap * 3.0f) / 4.0f;
    float ix = rpX + 10.0f;

    btnLockPadsIcon         = { ix + (iconW + iconGap) * 0.0f, iconRowY, iconW, 30.0f };
    btnGlobalExportModeIcon = { ix + (iconW + iconGap) * 1.0f, iconRowY, iconW, 30.0f };
    btnClearAllIcon         = { ix + (iconW + iconGap) * 2.0f, iconRowY, iconW, 30.0f };
    btnDragAllIcon          = { ix + (iconW + iconGap) * 3.0f, iconRowY, iconW, 30.0f };
}

void MelodicAtlasAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff12161d));
    drawTopHeader(g);
    drawFilterBar(g);
    drawBankAndRightPanel(g);
}

void MelodicAtlasAudioProcessorEditor::drawTopHeader(juce::Graphics& g)
{
    const juce::Font monoBold(juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::bold);
    const juce::Font monoReg (juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::plain);

    g.setColour(juce::Colour(0xff171c24));
    g.fillRect(0, 0, getWidth(), 40);
    g.setColour(juce::Colour(0xff252d3a));
    g.drawHorizontalLine(40, 0.0f, (float)getWidth());

    g.setColour(juce::Colour(0xff7ec8a3));
    g.drawLine(14.0f, 27.0f, 20.0f, 13.0f, 2.0f);
    g.drawLine(20.0f, 13.0f, 24.0f, 23.0f, 2.0f);
    g.drawLine(24.0f, 23.0f, 29.0f, 13.0f, 2.0f);
    g.drawLine(29.0f, 13.0f, 34.0f, 27.0f, 2.0f);

    g.setColour(juce::Colour(0xffe0e8f2));
    g.setFont(monoBold);
    g.drawText("MELODIC-ATLAS", 40, 0, 114, 40, juce::Justification::centredLeft, false);

    auto drawHistoryBtn = [&](juce::Rectangle<float> r, bool enabled, bool isRedo)
    {
        g.setColour(juce::Colour(0xff1d232d)); g.fillRoundedRectangle(r, 3.0f);
        g.setColour(enabled ? juce::Colour(0xff465466) : juce::Colour(0xff28303c)); g.drawRoundedRectangle(r, 3.0f, 1.0f);
        drawUndoVectorIcon(g, r.reduced(6.0f, 5.0f), enabled ? juce::Colour(0xffdce6f2) : juce::Colour(0xff4d5866), isRedo);
    };

    drawHistoryBtn(btnGlobalUndo, audioProcessor.canGlobalUndo(), false);
    drawHistoryBtn(btnGlobalRedo, audioProcessor.canGlobalRedo(), true);

    auto drawPillBtn = [&](juce::Rectangle<float> r, const juce::String& t, bool act, juce::Colour aCol = juce::Colour(0xff283f35))
    {
        g.setColour(act ? aCol : juce::Colour(0xff1d232d)); g.fillRoundedRectangle(r, 3.0f);
        g.setColour(act ? juce::Colour(0xff7ec8a3) : juce::Colour(0xff343e4c)); g.drawRoundedRectangle(r, 3.0f, 1.0f);
        g.setColour(act ? juce::Colours::white : juce::Colour(0xff9aa6b5));
        g.setFont(monoReg); g.drawText(t, r, juce::Justification::centred, false);
    };

    drawPillBtn(btnOneShots, "ONE-SHOTS", !audioProcessor.showLoopsTab.load());
    drawPillBtn(btnLoops,    "LOOPS",     audioProcessor.showLoopsTab.load());

    g.setColour(juce::Colour(0xff8c98a8));
    g.setFont(monoReg);
    g.drawText("KEY:", 380, 0, 32, 40, juce::Justification::centredLeft, false);

    juce::String keyName = Atlas::MusicMath::getNoteName(audioProcessor.projectKeyRoot.load());
    auto sType = static_cast<Atlas::ScaleType>(audioProcessor.projectScaleType.load());
    int pMode = audioProcessor.pitchModeIndex.load();
    int tQ = audioProcessor.transposeQualityIndex.load();

    drawPillBtn(btnKeyRoot,          keyName, true);
    drawDropdownTriangle(g, btnKeyRoot.withTrimmedLeft(btnKeyRoot.getWidth() - 14.0f).reduced(3.5f, 8.5f), juce::Colours::white);
    drawPillBtn(btnScaleType,        Atlas::MusicMath::getScaleShortName(sType), true);
    drawPillBtn(btnPitchMode,        (pMode == 0) ? "SNAP" : (pMode == 1 ? "ROOT" : "OFF"), pMode != 2);
    drawPillBtn(btnTransposeQuality, (tQ == 0) ? "CLEAN" : "STRETCH", true);

    g.setColour(juce::Colour(0xff8c98a8));
    g.drawText("BPM:", 672, 0, 32, 40, juce::Justification::centredLeft, false);
    bool syncAct = audioProcessor.bpmLockedToHost.load();
    drawPillBtn(btnBpmSync, juce::String(audioProcessor.currentBpm.load(), 1) + (syncAct ? "" : "*"), syncAct, juce::Colour(0xff283f35));

    juce::String scanText = audioProcessor.isCurrentlyScanning()
        ? ("SCANNING " + juce::String((int)(audioProcessor.getScanProgress() * 100.0f)) + "%")
        : "SCAN SAMPLE FOLDER";
    drawPillBtn(btnScanFolder, scanText, audioProcessor.isCurrentlyScanning(), juce::Colour(0xff354839));
}

void MelodicAtlasAudioProcessorEditor::drawFilterBar(juce::Graphics& g)
{
    const juce::Font monoBold(juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::bold);
    const juce::Font monoReg (juce::Font::getDefaultMonospacedFontName(), 10.5f, juce::Font::plain);

    g.setColour(juce::Colour(0xff161b22)); g.fillRect(14, 46, getWidth() - 28, 74);
    g.setColour(juce::Colour(0xff252d3a)); g.drawRect(14, 46, getWidth() - 28, 74, 1);

    bool allOn = (selectedClusterFilter == -1);
    const auto& rAll = groupFilterRects[0];
    g.setColour(allOn ? juce::Colour(0xff284035) : juce::Colour(0xff1d232c)); g.fillRoundedRectangle(rAll, 3.0f);
    g.setColour(allOn ? juce::Colour(0xff7ec8a3) : juce::Colour(0xff323b48)); g.drawRoundedRectangle(rAll, 3.0f, 1.0f);
    g.setColour(allOn ? juce::Colours::white : juce::Colour(0xff9aa6b5)); g.setFont(monoBold);
    g.drawText("ALL", rAll, juce::Justification::centred, false);

    for (int c = 0; c < 8; ++c)
    {
        auto cg = static_cast<Atlas::ClusterGroup>(c);
        const auto& r = groupFilterRects[(size_t)(c + 1)];
        const auto& dropR = groupDropdownRects[(size_t)c];
        bool on = (selectedClusterFilter == c);
        uint32_t subBit = selectedClusterSubgroupBit[(size_t)c];

        juce::String label = Atlas::MusicMath::getClusterName(cg);
        if (subBit != 0)
        {
            juce::String subShort = Atlas::MusicMath::getSubgroupShortLabel(cg, subBit);
            if (subShort.isNotEmpty()) label += ":" + subShort;
        }

        juce::Colour clusterCol = Atlas::MusicMath::getClusterColour(cg, 1);
        g.setColour(on ? clusterCol.withAlpha(0.28f) : juce::Colour(0xff1d232c)); g.fillRoundedRectangle(r, 3.0f);
        g.setColour(on ? clusterCol.brighter(0.25f) : juce::Colour(0xff323b48)); g.drawRoundedRectangle(r, 3.0f, 1.0f);

        g.setColour(on ? clusterCol.withAlpha(0.5f) : juce::Colour(0xff2a323e));
        g.drawVerticalLine((int)dropR.getX(), r.getY() + 4.0f, r.getBottom() - 4.0f);

        g.setColour(on ? juce::Colours::white : juce::Colour(0xffa4b0c0)); g.setFont(monoBold);
        g.drawText(label, r.withTrimmedRight(16.0f), juce::Justification::centred, false);
        drawDropdownTriangle(g, dropR.reduced(5.0f, 9.0f), on ? juce::Colours::white : juce::Colour(0xff8a96a6));
    }

    static const char* subNames[7] = { "CLEAN", "NOISY", "WARM", "PLUCK", "SUSTAIN", "RISER/SWEEP", "TEXTURAL" };
    static const uint32_t subBits[7] = { Atlas::Sub_Clean, Atlas::Sub_Noisy, Atlas::Sub_Warm, Atlas::Sub_Pluck, Atlas::Sub_Sustain, Atlas::Sub_RiserSweep, Atlas::Sub_Textural };

    for (int i = 0; i < 7; ++i)
    {
        const auto& r = subFilterRects[(size_t)i];
        bool on = ((activeSubFilterMask & subBits[i]) != 0);
        g.setColour(on ? juce::Colour(0xff284035) : juce::Colour(0xff1c222b)); g.fillRoundedRectangle(r, 2.5f);
        g.setColour(on ? juce::Colour(0xff7ec8a3) : juce::Colour(0xff303946)); g.drawRoundedRectangle(r, 2.5f, 1.0f);
        g.setColour(on ? juce::Colours::white : juce::Colour(0xff929fb0)); g.setFont(monoReg);
        g.drawText(subNames[i], r, juce::Justification::centred, false);
    }

    int oct = audioProcessor.octaveShift.load();
    g.setColour(juce::Colour(0xffa4b0c0)); g.setFont(monoBold);
    g.drawText("OCTAVE: " + (oct > 0 ? "+" + juce::String(oct) : juce::String(oct)), octLabelRect, juce::Justification::centred, false);

    auto drawOctBtn = [&](juce::Rectangle<float> r, const juce::String& txt, bool highlighted) {
        g.setColour(highlighted ? juce::Colour(0xff284035) : juce::Colour(0xff1d232c)); g.fillRoundedRectangle(r, 3.0f);
        g.setColour(highlighted ? juce::Colour(0xff7ec8a3) : juce::Colour(0xff343e4c)); g.drawRoundedRectangle(r, 3.0f, 1.0f);
        g.setColour(highlighted ? juce::Colours::white : juce::Colour(0xffb0bccb)); g.setFont(monoBold);
        g.drawText(txt, r, juce::Justification::centred, false);
    };

    drawOctBtn(btnOctDown, "-", oct < 0);
    drawOctBtn(btnOctUp,   "+", oct > 0);
}

void MelodicAtlasAudioProcessorEditor::drawBankAndRightPanel(juce::Graphics& g)
{
    const juce::Font monoBold(juce::Font::getDefaultMonospacedFontName(), 11.5f, juce::Font::bold);
    const juce::Font monoReg (juce::Font::getDefaultMonospacedFontName(), 10.5f, juce::Font::plain);

    float bankTopY = btnPadTriggerMode.getY();
    g.setColour(juce::Colour(0xffd0dae5)); g.setFont(monoBold);
    g.drawText("BANK:", 14, (int)bankTopY, 50, 20, juce::Justification::centredLeft, false);

    bool isChromatic = (audioProcessor.bankModeIndex.load() == (int)Atlas::BankMode::Chromatic);
    auto drawModeBtn = [&](juce::Rectangle<float> r, const juce::String& text, bool active) {
        g.setColour(active ? juce::Colour(0xff284035) : juce::Colour(0xff1c222b)); g.fillRoundedRectangle(r, 3.0f);
        g.setColour(active ? juce::Colour(0xff7ec8a3) : juce::Colour(0xff343e4d)); g.drawRoundedRectangle(r, 3.0f, 1.0f);
        g.setColour(active ? juce::Colours::white : juce::Colour(0xff8e9aa8)); g.setFont(monoReg);
        g.drawText(text, r, juce::Justification::centred, false);
    };
    drawModeBtn(btnPadTriggerMode, "PAD TRIGGER", !isChromatic);
    drawModeBtn(btnChromaticMode,  "CHROMATIC",   isChromatic);

    uint32_t playingMask = audioProcessor.getSamplerEngine().getActivePlayingPadsMask();
    for (int i = 0; i < 12; ++i)
        drawPadSlot(g, i, padRects[(size_t)i], ((playingMask & (1u << i)) != 0));

    g.setColour(juce::Colour(0xff161b22)); g.fillRoundedRectangle(rightBankPanelRect, 4.0f);
    g.setColour(juce::Colour(0xff28313e)); g.drawRoundedRectangle(rightBankPanelRect, 4.0f, 1.0f);

    bool locked = audioProcessor.padSettingsLocked.load();
    g.setColour(locked ? juce::Colour(0xff284035) : juce::Colour(0xff1e242d)); g.fillRoundedRectangle(btnLockPadsIcon, 3.5f);
    g.setColour(locked ? juce::Colour(0xff7ec8a3) : juce::Colour(0xff36404e)); g.drawRoundedRectangle(btnLockPadsIcon, 3.5f, 1.0f);
    drawLockVectorIcon(g, btnLockPadsIcon.reduced(8.0f, 6.0f), locked, locked ? juce::Colour(0xff7ec8a3) : juce::Colour(0xff9aa6b5));

    bool globalProc = audioProcessor.globalExportProcessed.load();
    g.setColour(globalProc ? juce::Colour(0xff284035) : juce::Colour(0xff1e242d)); g.fillRoundedRectangle(btnGlobalExportModeIcon, 3.5f);
    g.setColour(globalProc ? juce::Colour(0xff7ec8a3) : juce::Colour(0xff4a607a)); g.drawRoundedRectangle(btnGlobalExportModeIcon, 3.5f, 1.0f);
    g.setColour(juce::Colours::white); g.setFont(monoBold);
    g.drawText(globalProc ? "P" : "D", btnGlobalExportModeIcon, juce::Justification::centred, false);

    g.setColour(juce::Colour(0xff1e242d)); g.fillRoundedRectangle(btnClearAllIcon, 3.5f);
    g.setColour(juce::Colour(0xff36404e)); g.drawRoundedRectangle(btnClearAllIcon, 3.5f, 1.0f);
    drawTrashVectorIcon(g, btnClearAllIcon.reduced(8.5f, 6.5f), juce::Colour(0xffb5c0ce));

    g.setColour(juce::Colour(0xff6da37b)); g.fillRoundedRectangle(btnDragAllIcon, 3.5f);
    g.setColour(juce::Colour(0xff96d1a4)); g.drawRoundedRectangle(btnDragAllIcon, 3.5f, 1.0f);
    drawExportVectorIcon(g, btnDragAllIcon.reduced(7.5f, 6.5f), juce::Colour(0xff0d1510));
}

void MelodicAtlasAudioProcessorEditor::drawPadSlot(juce::Graphics& g, int padIdx, const juce::Rectangle<float>& r, bool isPlayingNow)
{
    const auto& padState = audioProcessor.getSamplerEngine().getPadState(padIdx);
    const auto* rec = audioProcessor.getSamplerEngine().getPadRecord(padIdx);
    auto audio = audioProcessor.getSamplerEngine().getPadAudio(padIdx);

    bool isSelected = selectedPads.count(padIdx) > 0;
    bool isDropTarget = (hoveredDropPadIndex == padIdx);

    juce::Colour bgCol = (rec != nullptr) ? juce::Colour(0xff1e242d) : juce::Colour(0xff181d25);
    if (isPlayingNow) bgCol = juce::Colour(0xff233a32);
    else if (isDropTarget) bgCol = juce::Colour(0xff273731);

    g.setColour(bgCol); g.fillRoundedRectangle(r, 3.5f);

    if (isPlayingNow) { g.setColour(juce::Colour(0xff62f5b5)); g.drawRoundedRectangle(r, 3.5f, 2.0f); }
    else if (isSelected || isDropTarget) { g.setColour(juce::Colour(0xffe6c167)); g.drawRoundedRectangle(r, 3.5f, 1.6f); }
    else { g.setColour(juce::Colour(0xff2c3542)); g.drawRoundedRectangle(r, 3.5f, 1.0f); }

    if (padState.settingsViewOpen) drawPadGearSettingsView(g, padIdx, r, padState, audio);
    else drawPadNormalView(g, padIdx, r, padState, rec, audio, isPlayingNow);
}

void MelodicAtlasAudioProcessorEditor::drawPadNormalView(juce::Graphics& g, int padIdx, const juce::Rectangle<float>& r,
                                                         const Atlas::PadSlotState& padState, const Atlas::SampleRecord* rec,
                                                         std::shared_ptr<Atlas::CachedAudioData> audio, bool isPlayingNow)
{
    const juce::Font monoNote(juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::bold);
    const juce::Font monoSmall(juce::Font::getDefaultMonospacedFontName(), 9.5f, juce::Font::bold);
    const juce::Font monoName(juce::Font::getDefaultMonospacedFontName(), 10.5f, juce::Font::plain);

    if (rec == nullptr || audio == nullptr)
    {
        // Кнопка + на пустом пэде
        juce::Rectangle<float> plusBtnZone(r.getCentreX() - 15.0f, r.getCentreY() - 15.0f, 30.0f, 30.0f);
        drawPlusVectorIcon(g, plusBtnZone, juce::Colour(0xff4a5462));
        g.setColour(juce::Colour(0xff566272)); g.setFont(monoSmall);
        g.drawText("ADD", r.withTrimmedTop(r.getHeight() * 0.7f), juce::Justification::centred, false);
        return;
    }

    static const char* defaultNotes[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    juce::String noteLabel = (rec != nullptr) ? Atlas::MusicMath::formatPadNoteAndCents(rec->pitchClass, padState.semitoneOffset, padState.centOffset) : juce::String(defaultNotes[padIdx]);

    g.setColour(isPlayingNow ? juce::Colour(0xff62f5b5) : (rec != nullptr ? juce::Colour(0xffe2eaf4) : juce::Colour(0xff687484)));
    g.setFont(monoNote); g.drawText(noteLabel, (int)r.getX() + 6, (int)r.getY() + 3, 64, 15, juce::Justification::centredLeft, false);

    juce::Rectangle<float> chromDotZone(r.getRight() - 68.0f, r.getY() + 3.0f, 14.0f, 14.0f);
    juce::Rectangle<float> pdBtnZone   (r.getRight() - 52.0f, r.getY() + 3.0f, 15.0f, 14.0f);
    juce::Rectangle<float> undoBtnZone (r.getRight() - 35.0f, r.getY() + 3.0f, 15.0f, 14.0f);
    juce::Rectangle<float> gearBtnZone (r.getRight() - 18.0f, r.getY() + 3.0f, 15.0f, 14.0f);

    auto dotEllipse = chromDotZone.reduced(3.2f);
    if (padState.chromaticActive) { g.setColour(juce::Colour(0xffffcf48)); g.fillEllipse(dotEllipse); }
    else { g.setColour(juce::Colour(0xff525e6e)); g.drawEllipse(dotEllipse, 1.3f); }

    g.setColour(padState.exportProcessed ? juce::Colour(0xff263f34) : juce::Colour(0xff232c38)); g.fillRoundedRectangle(pdBtnZone, 2.5f);
    g.setColour(padState.exportProcessed ? juce::Colour(0xff7ec8a3) : juce::Colour(0xff5c708a)); g.drawRoundedRectangle(pdBtnZone, 2.5f, 1.0f);
    g.setColour(juce::Colours::white); g.setFont(monoSmall); g.drawText(padState.exportProcessed ? "P" : "D", pdBtnZone, juce::Justification::centred, false);

    bool hasPadHistory = !padState.history.empty();
    drawUndoVectorIcon(g, undoBtnZone.reduced(2.5f), hasPadHistory ? juce::Colour(0xffb8c5d6) : juce::Colour(0xff46505e), false);
    drawGearVectorIcon(g, gearBtnZone.reduced(2.0f), juce::Colour(0xff9aa8b8));

    float midY = r.getY() + 19.0f;
    float midH = std::max(18.0f, r.getHeight() - 36.0f);
    juce::Rectangle<float> leftArrowZone (r.getX() + 3.0f, midY, 15.0f, midH);
    juce::Rectangle<float> rightArrowZone(r.getRight() - 18.0f, midY, 15.0f, midH);
    juce::Rectangle<float> waveRect      (r.getX() + 20.0f, midY + 2.0f, r.getWidth() - 40.0f, midH - 4.0f);

    drawArrowVectorIcon(g, leftArrowZone.withSizeKeepingCentre(9.0f, 11.0f),  false, juce::Colour(0xff7e8c9f));
    drawArrowVectorIcon(g, rightArrowZone.withSizeKeepingCentre(9.0f, 11.0f), true,  juce::Colour(0xff7e8c9f));

    juce::Colour waveCol = Atlas::MusicMath::getClusterColour(rec->cluster, rec->toneShadeBucket);
    const auto& peaks = audio->waveformPeaks;
    if (!peaks.empty())
    {
        const size_t step = 2;
        const size_t numBars = std::max<size_t>(1, peaks.size() / step);
        const float barW = waveRect.getWidth() / (float)numBars;
        const float centerY = waveRect.getCentreY();
        for (size_t b = 0; b < numBars; ++b)
        {
            float normPos = (float)b / (float)numBars;
            bool inRegion = (normPos >= padState.regionStart && normPos <= padState.regionEnd);
            g.setColour(inRegion ? (isPlayingNow ? waveCol.brighter(0.45f) : waveCol.withAlpha(0.88f)) : waveCol.withAlpha(0.22f));
            float h = std::max(1.5f, peaks[b * step] * waveRect.getHeight() * 0.92f);
            g.fillRect(waveRect.getX() + (float)b * barW, centerY - h * 0.5f, std::max(1.0f, barW - 0.5f), h);
        }
    }

    g.setColour(juce::Colour(0xffdce5f0)); g.setFont(monoName);
    g.drawText(rec->name.substring(0, 16), (int)r.getX() + 6, (int)r.getBottom() - 16, (int)r.getWidth() - 12, 14, juce::Justification::centred, true);
}

void MelodicAtlasAudioProcessorEditor::drawPadGearSettingsView(juce::Graphics& g, int padIdx, const juce::Rectangle<float>& r,
                                                               const Atlas::PadSlotState& padState, std::shared_ptr<Atlas::CachedAudioData> audio)
{
    juce::Rectangle<float> gearBtnZone(r.getRight() - 18.0f, r.getY() + 3.0f, 15.0f, 14.0f);
    drawGearVectorIcon(g, gearBtnZone.reduced(2.0f), juce::Colour(0xffe6c167));

    juce::Rectangle<float> regionWaveBox(r.getX() + 6.0f, r.getY() + 4.0f, r.getWidth() - 28.0f, 16.0f);
    g.setColour(juce::Colour(0xff13171e)); g.fillRoundedRectangle(regionWaveBox, 2.0f);

    if (audio != nullptr && !audio->waveformPeaks.empty())
    {
        const auto& peaks = audio->waveformPeaks;
        const size_t step = 2;
        const size_t numBars = std::max<size_t>(1, peaks.size() / step);
        const float barW = regionWaveBox.getWidth() / (float)numBars;
        const float cy = regionWaveBox.getCentreY();
        for (size_t b = 0; b < numBars; ++b)
        {
            float norm = (float)b / (float)numBars;
            bool inside = (norm >= padState.regionStart && norm <= padState.regionEnd);
            g.setColour(inside ? juce::Colour(0xff7ec8a3) : juce::Colour(0xff384352));
            float h = std::max(1.5f, peaks[b * step] * regionWaveBox.getHeight() * 0.85f);
            g.fillRect(regionWaveBox.getX() + (float)b * barW, cy - h * 0.5f, std::max(1.0f, barW - 0.4f), h);
        }
    }
    float startX = regionWaveBox.getX() + padState.regionStart * regionWaveBox.getWidth();
    float endX   = regionWaveBox.getX() + padState.regionEnd   * regionWaveBox.getWidth();
    g.setColour(juce::Colour(0xffe6c167));
    g.drawLine(startX, regionWaveBox.getY(), startX, regionWaveBox.getBottom(), 2.0f);
    g.drawLine(endX,   regionWaveBox.getY(), endX,   regionWaveBox.getBottom(), 2.0f);

    // 2 ряда по 4 энкодера
    float dialsTopY = r.getY() + 23.0f;
    float dialAvailH = r.getBottom() - dialsTopY - 2.0f;
    float rowH = dialAvailH / 2.0f;
    float dialSize = std::min(24.0f, rowH - 6.0f);
    float colW = (r.getWidth() - 8.0f) / 4.0f;

    // Ряд 1: TUNE, CENT, VOL, PAN
    juce::String panStr = (std::abs(padState.pan) < 0.05f) ? "C" : ((padState.pan < 0.0f) ? ("L" + juce::String((int)std::round(-padState.pan * 50.0f))) : ("R" + juce::String((int)std::round(padState.pan * 50.0f))));
    drawMiniEncoder(g, { r.getX() + 4.0f + colW * 0.0f + (colW - dialSize) * 0.5f, dialsTopY, dialSize, dialSize }, (padState.semitoneOffset + 24.0f) / 48.0f, "TUNE", (padState.semitoneOffset > 0 ? "+" : "") + juce::String(padState.semitoneOffset), true);
    drawMiniEncoder(g, { r.getX() + 4.0f + colW * 1.0f + (colW - dialSize) * 0.5f, dialsTopY, dialSize, dialSize }, (padState.centOffset + 100.0f) / 200.0f, "CENT", (padState.centOffset > 0 ? "+" : "") + juce::String(padState.centOffset), true);
    drawMiniEncoder(g, { r.getX() + 4.0f + colW * 2.0f + (colW - dialSize) * 0.5f, dialsTopY, dialSize, dialSize }, (padState.volumeDb + 24.0f) / 30.0f, "VOL", juce::String((int)std::round(padState.volumeDb)) + "dB", false);
    drawMiniEncoder(g, { r.getX() + 4.0f + colW * 3.0f + (colW - dialSize) * 0.5f, dialsTopY, dialSize, dialSize }, (padState.pan + 1.0f) * 0.5f, "PAN", panStr, true);

    // Ряд 2: ATT, DEC, SUS, REL
    float dialsRow2Y = dialsTopY + rowH;
    drawMiniEncoder(g, { r.getX() + 4.0f + colW * 0.0f + (colW - dialSize) * 0.5f, dialsRow2Y, dialSize, dialSize }, padState.attackSec / 2.0f, "ATT", juce::String(padState.attackSec, 2) + "s", false);
    drawMiniEncoder(g, { r.getX() + 4.0f + colW * 1.0f + (colW - dialSize) * 0.5f, dialsRow2Y, dialSize, dialSize }, padState.decaySec / 5.0f, "DEC", juce::String(padState.decaySec, 2) + "s", false);
    drawMiniEncoder(g, { r.getX() + 4.0f + colW * 2.0f + (colW - dialSize) * 0.5f, dialsRow2Y, dialSize, dialSize }, padState.sustainLvl, "SUS", juce::String((int)std::round(padState.sustainLvl * 100.0f)) + "%", false);
    drawMiniEncoder(g, { r.getX() + 4.0f + colW * 3.0f + (colW - dialSize) * 0.5f, dialsRow2Y, dialSize, dialSize }, padState.releaseSec / 5.0f, "REL", juce::String(padState.releaseSec, 2) + "s", false);
}

void MelodicAtlasAudioProcessorEditor::drawMiniEncoder(juce::Graphics& g, juce::Rectangle<float> dialRect, float normVal, const juce::String& insideLabel, const juce::String& subValStr, bool bipolar)
{
    normVal = juce::jlimit(0.0f, 1.0f, normVal);
    const float startAngle = -2.35619f;
    const float endAngle   =  2.35619f;
    const float curAngle   = startAngle + normVal * (endAngle - startAngle);
    const float radius     = dialRect.getWidth() * 0.46f;
    const auto centre      = dialRect.getCentre();

    juce::Path bgArc; bgArc.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, startAngle, endAngle, true);
    g.setColour(juce::Colour(0xff28313e)); g.strokePath(bgArc, juce::PathStrokeType(2.0f));

    juce::Path valArc;
    if (bipolar) valArc.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, std::min(0.0f, curAngle), std::max(0.0f, curAngle), true);
    else valArc.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, startAngle, curAngle, true);

    g.setColour(juce::Colour(0xff7ec8a3)); g.strokePath(valArc, juce::PathStrokeType(2.2f));
    g.setColour(juce::Colour(0xff151a22)); g.fillEllipse(centre.x - radius * 0.82f, centre.y - radius * 0.82f, radius * 1.64f, radius * 1.64f);
    g.setColour(juce::Colour(0xffdce5f0)); g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 7.0f, juce::Font::bold));
    g.drawText(insideLabel, dialRect, juce::Justification::centred, false);
    g.setColour(juce::Colour(0xff94a3b5)); g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 7.5f, juce::Font::plain));
    g.drawText(subValStr, dialRect.translated(0.0f, dialRect.getHeight() * 0.82f).withHeight(11.0f).expanded(6.0f, 0.0f), juce::Justification::centred, false);
}

// Векторные иконки
void MelodicAtlasAudioProcessorEditor::drawUndoVectorIcon(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour col, bool isRedo)
{
    g.setColour(col); juce::Path p; float cx = area.getCentreX(); float cy = area.getCentreY(); float r = std::min(area.getWidth(), area.getHeight()) * 0.42f;
    if (!isRedo) {
        p.addCentredArc(cx, cy + 1.0f, r, r, 0.0f, -1.8f, 2.1f, true); g.strokePath(p, juce::PathStrokeType(1.5f));
        juce::Point<float> tip(cx - r * std::sin(1.8f), cy + 1.0f - r * std::cos(1.8f));
        g.drawLine(tip.x, tip.y, tip.x + 3.5f, tip.y - 2.0f, 1.5f); g.drawLine(tip.x, tip.y, tip.x + 2.5f, tip.y + 3.2f, 1.5f);
    } else {
        p.addCentredArc(cx, cy + 1.0f, r, r, 0.0f, -2.1f, 1.8f, true); g.strokePath(p, juce::PathStrokeType(1.5f));
        juce::Point<float> tip(cx + r * std::sin(1.8f), cy + 1.0f - r * std::cos(1.8f));
        g.drawLine(tip.x, tip.y, tip.x - 3.5f, tip.y - 2.0f, 1.5f); g.drawLine(tip.x, tip.y, tip.x - 2.5f, tip.y + 3.2f, 1.5f);
    }
}
void MelodicAtlasAudioProcessorEditor::drawGearVectorIcon(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour col)
{
    g.setColour(col); auto c = area.getCentre(); float rOuter = std::min(area.getWidth(), area.getHeight()) * 0.46f; float rInner = rOuter * 0.52f;
    for (int i = 0; i < 6; ++i) {
        float ang = (float)i * juce::MathConstants<float>::pi / 3.0f;
        g.drawLine(c.x + std::cos(ang) * (rInner * 0.8f), c.y + std::sin(ang) * (rInner * 0.8f), c.x + std::cos(ang) * rOuter, c.y + std::sin(ang) * rOuter, 1.6f);
    }
    g.fillEllipse(c.x - rInner, c.y - rInner, rInner * 2.0f, rInner * 2.0f);
    g.setColour(juce::Colour(0xff181d25)); g.fillEllipse(c.x - rInner * 0.48f, c.y - rInner * 0.48f, rInner * 0.96f, rInner * 0.96f);
}
void MelodicAtlasAudioProcessorEditor::drawArrowVectorIcon(juce::Graphics& g, juce::Rectangle<float> area, bool pointRight, juce::Colour col)
{
    g.setColour(col); juce::Path p;
    if (pointRight) { p.startNewSubPath(area.getX(), area.getY()); p.lineTo(area.getRight(), area.getCentreY()); p.lineTo(area.getX(), area.getBottom()); }
    else { p.startNewSubPath(area.getRight(), area.getY()); p.lineTo(area.getX(), area.getCentreY()); p.lineTo(area.getRight(), area.getBottom()); }
    p.closeSubPath(); g.fillPath(p);
}
void MelodicAtlasAudioProcessorEditor::drawDropdownTriangle(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour col)
{
    g.setColour(col); juce::Path p; p.startNewSubPath(area.getX(), area.getY()); p.lineTo(area.getRight(), area.getY()); p.lineTo(area.getCentreX(), area.getBottom()); p.closeSubPath(); g.fillPath(p);
}
void MelodicAtlasAudioProcessorEditor::drawLockVectorIcon(juce::Graphics& g, juce::Rectangle<float> area, bool isLocked, juce::Colour col)
{
    g.setColour(col); juce::Rectangle<float> body(area.getX() + 1.0f, area.getCentreY() - 1.0f, area.getWidth() - 2.0f, area.getHeight() * 0.54f);
    g.drawRoundedRectangle(body, 2.0f, 1.5f); juce::Path shackle; float sw = body.getWidth() * 0.56f; float sx = body.getCentreX() - sw * 0.5f;
    if (isLocked) shackle.addCentredArc(body.getCentreX(), body.getY(), sw * 0.5f, sw * 0.55f, 0.0f, -juce::MathConstants<float>::pi, 0.0f, true);
    else shackle.addCentredArc(sx + sw * 0.85f, body.getY() - 1.0f, sw * 0.5f, sw * 0.55f, 0.0f, -juce::MathConstants<float>::pi * 0.85f, 0.1f, true);
    g.strokePath(shackle, juce::PathStrokeType(1.5f));
}
void MelodicAtlasAudioProcessorEditor::drawTrashVectorIcon(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour col)
{
    g.setColour(col); float x = area.getX(); float y = area.getY(); float w = area.getWidth(); float h = area.getHeight();
    g.drawLine(x, y + 3.0f, x + w, y + 3.0f, 1.5f); g.drawLine(x + w * 0.3f, y + 1.0f, x + w * 0.7f, y + 1.0f, 1.5f);
    g.drawRoundedRectangle(x + 2.0f, y + 3.5f, w - 4.0f, h - 4.0f, 1.5f, 1.4f);
}
void MelodicAtlasAudioProcessorEditor::drawExportVectorIcon(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour col)
{
    g.setColour(col); float cx = area.getCentreX(); float cy = area.getCentreY();
    g.drawLine(area.getX(), cy, area.getRight() - 2.0f, cy, 2.0f);
    g.drawLine(area.getRight() - 6.0f, cy - 4.5f, area.getRight() - 1.0f, cy, 2.0f);
    g.drawLine(area.getRight() - 6.0f, cy + 4.5f, area.getRight() - 1.0f, cy, 2.0f);
    g.drawLine(cx - 4.0f, area.getY() + 2.0f, cx - 4.0f, area.getBottom() - 2.0f, 1.4f);
}
void MelodicAtlasAudioProcessorEditor::drawPlusVectorIcon(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour col)
{
    g.setColour(col); float cx = area.getCentreX(); float cy = area.getCentreY(); float len = std::min(area.getWidth(), area.getHeight()) * 0.35f;
    g.drawLine(cx, cy - len, cx, cy + len, 2.5f); g.drawLine(cx - len, cy, cx + len, cy, 2.5f);
}

void MelodicAtlasAudioProcessorEditor::mouseMove(const juce::MouseEvent& e) { updateHoverTooltip(e.position); }
void MelodicAtlasAudioProcessorEditor::updateHoverTooltip(juce::Point<float> pt)
{
    if (btnGlobalUndo.contains(pt)) setTooltip("Global Undo: Revert last bank or pad change");
    else if (btnGlobalRedo.contains(pt)) setTooltip("Global Redo: Step forward in bank history");
    else if (btnLockPadsIcon.contains(pt)) setTooltip(audioProcessor.padSettingsLocked.load() ? "Pad Lock ON: Keep pad settings" : "Pad Lock OFF: Adopt preview pitch");
    else if (btnGlobalExportModeIcon.contains(pt)) setTooltip(audioProcessor.globalExportProcessed.load() ? "Global Export [P] Processed" : "Global Export [D] Dry");
    else if (btnClearAllIcon.contains(pt)) setTooltip("Clear All Bank Pads");
    else if (btnDragAllIcon.contains(pt)) setTooltip("Drag All Active Pads as Audio Files to DAW");
    else setTooltip({});
}

void MelodicAtlasAudioProcessorEditor::mouseDown(const juce::MouseEvent& e)
{
    auto pt = e.position; currentPadDragAction = PadDragAction::None; activeDragPadIndex = -1; draggingBpmValue = false;
    if (pt.y <= 42.0f) { handleTopHeaderClick(e); return; }
    if (pt.y <= 122.0f) { handleFilterBarClick(e); return; }
    if (pt.y >= btnPadTriggerMode.getY() - 4.0f) { handleBankAndRightPanelClick(e); }
}

void MelodicAtlasAudioProcessorEditor::mouseDrag(const juce::MouseEvent& e)
{
    if (draggingBpmValue) {
        double delta = (double)(padDragStartPos.y - e.position.y) * 0.4;
        audioProcessor.currentBpm.store(juce::jlimit(60.0, 200.0, std::round((bpmDragStartVal + delta) * 2.0) * 0.5));
        repaint(670, 0, 110, 40); return;
    }

    if (currentPadDragAction != PadDragAction::None && activeDragPadIndex >= 0)
    {
        const auto& r = padRects[(size_t)activeDragPadIndex];
        auto& st = audioProcessor.getSamplerEngine().getPadState(activeDragPadIndex);
        float dy = padDragStartPos.y - e.position.y;

        if (currentPadDragAction == PadDragAction::RegionStart || currentPadDragAction == PadDragAction::RegionEnd) {
            juce::Rectangle<float> waveBox(r.getX() + 6.0f, r.getY() + 4.0f, r.getWidth() - 28.0f, 16.0f);
            float normX = (e.position.x - waveBox.getX()) / std::max(1.0f, waveBox.getWidth());
            if (currentPadDragAction == PadDragAction::RegionStart) st.regionStart = juce::jlimit(0.0f, st.regionEnd - 0.04f, normX);
            else st.regionEnd = juce::jlimit(st.regionStart + 0.04f, 1.0f, normX);
        }
        else if (currentPadDragAction == PadDragAction::DialSemitones) st.semitoneOffset = juce::jlimit(-24, 24, (int)std::round(dragStartParamValue + dy * 0.25f));
        else if (currentPadDragAction == PadDragAction::DialCents) st.centOffset = juce::jlimit(-100, 100, (int)std::round(dragStartParamValue + dy * 1.5f));
        else if (currentPadDragAction == PadDragAction::DialVol) st.volumeDb = juce::jlimit(-24.0f, 6.0f, dragStartParamValue + dy * 0.25f);
        else if (currentPadDragAction == PadDragAction::DialPan) st.pan = juce::jlimit(-1.0f, 1.0f, dragStartParamValue + dy * 0.015f);
        else if (currentPadDragAction == PadDragAction::DialAttack) st.attackSec = juce::jlimit(0.0f, 2.0f, dragStartParamValue + dy * 0.01f);
        else if (currentPadDragAction == PadDragAction::DialDecay) st.decaySec = juce::jlimit(0.01f, 5.0f, dragStartParamValue + dy * 0.025f);
        else if (currentPadDragAction == PadDragAction::DialSustain) st.sustainLvl = juce::jlimit(0.0f, 1.0f, dragStartParamValue + dy * 0.005f);
        else if (currentPadDragAction == PadDragAction::DialRelease) st.releaseSec = juce::jlimit(0.005f, 5.0f, dragStartParamValue + dy * 0.025f);
        else if (currentPadDragAction == PadDragAction::DragPadToDaw) {
            if (e.position.getDistanceFrom(padDragStartPos) >= 7.0f) {
                currentPadDragAction = PadDragAction::None; activeDragPadIndex = -1;
                std::vector<int> padsToExport(selectedPads.begin(), selectedPads.end());
                exportPadsToDaw(padsToExport); return;
            }
        }
        repaint(); return;
    }

    if (e.position.getDistanceFrom(padDragStartPos) >= 6.0f && btnDragAllIcon.contains(padDragStartPos)) {
        std::vector<int> padsToExport;
        for (int i = 0; i < 12; ++i) if (audioProcessor.getSamplerEngine().getPadState(i).sampleIndex >= 0) padsToExport.push_back(i);
        padDragStartPos = { -999.0f, -999.0f }; exportPadsToDaw(padsToExport);
    }
}

void MelodicAtlasAudioProcessorEditor::mouseUp(const juce::MouseEvent&) { draggingBpmValue = false; currentPadDragAction = PadDragAction::None; activeDragPadIndex = -1; }

void MelodicAtlasAudioProcessorEditor::mouseDoubleClick(const juce::MouseEvent& e)
{
    for (int i = 0; i < 12; ++i)
    {
        if (padRects[(size_t)i].contains(e.position))
        {
            auto& st = audioProcessor.getSamplerEngine().getPadState(i);
            if (!st.settingsViewOpen)
            {
                audioProcessor.pushGlobalUndoState();
                st.sampleIndex = -1;
                repaint();
                return;
            }
            else
            {
                // Сброс параметров при двойном клике по энкодерам ADSR
                const auto& r = padRects[(size_t)i];
                float relY = (e.position.y - (r.getY() + 23.0f)) / (r.getHeight() - 25.0f);
                float relX = (e.position.x - r.getX()) / std::max(1.0f, r.getWidth());

                if (relY >= 0.0f && relY <= 1.0f)
                {
                    if (relY < 0.5f) { // Верхний ряд (TUNE, CENT, VOL, PAN)
                        if (relX < 0.25f) st.semitoneOffset = 0;
                        else if (relX < 0.50f) st.centOffset = 0;
                        else if (relX < 0.75f) st.volumeDb = -3.0f;
                        else st.pan = 0.0f;
                    } else { // Нижний ряд (ATT, DEC, SUS, REL)
                        if (relX < 0.25f) st.attackSec = 0.005f;
                        else if (relX < 0.50f) st.decaySec = 1.2f;
                        else if (relX < 0.75f) st.sustainLvl = 1.0f;
                        else st.releaseSec = 0.05f;
                    }
                    repaint();
                    return;
                }
            }
        }
    }
}

void MelodicAtlasAudioProcessorEditor::handleTopHeaderClick(const juce::MouseEvent& e)
{
    auto pt = e.position;
    if (btnGlobalUndo.contains(pt)) { audioProcessor.performGlobalUndo(); primarySelectedPad = audioProcessor.primarySelectedPad; repaint(); return; }
    if (btnGlobalRedo.contains(pt)) { audioProcessor.performGlobalRedo(); primarySelectedPad = audioProcessor.primarySelectedPad; repaint(); return; }
    if (btnOneShots.contains(pt)) { audioProcessor.showLoopsTab.store(false); mapCanvas.setViewFilter(false, selectedClusterFilter, activeSubFilterMask); repaint(); }
    else if (btnLoops.contains(pt)) { audioProcessor.showLoopsTab.store(true); mapCanvas.setViewFilter(true, selectedClusterFilter, activeSubFilterMask); repaint(); }
    else if (btnKeyRoot.contains(pt)) showKeyPopupMenu();
    else if (btnScaleType.contains(pt)) showScalePopupMenu();
    else if (btnPitchMode.contains(pt)) { audioProcessor.pitchModeIndex.store((audioProcessor.pitchModeIndex.load() + 1) % 3); repaint(); }
    else if (btnTransposeQuality.contains(pt)) { audioProcessor.transposeQualityIndex.store((audioProcessor.transposeQualityIndex.load() + 1) % 2); repaint(); }
    else if (btnBpmSync.contains(pt)) {
        if (e.mods.isPopupMenu() || !audioProcessor.bpmLockedToHost.load()) {
            draggingBpmValue = true; padDragStartPos = pt; bpmDragStartVal = audioProcessor.currentBpm.load();
            if (e.getNumberOfClicks() > 1) audioProcessor.bpmLockedToHost.store(true);
        } else {
            audioProcessor.bpmLockedToHost.store(false); draggingBpmValue = true; padDragStartPos = pt; bpmDragStartVal = audioProcessor.currentBpm.load();
        }
        repaint();
    }
    else if (btnScanFolder.contains(pt)) openFolderChooser();
}

void MelodicAtlasAudioProcessorEditor::handleFilterBarClick(const juce::MouseEvent& e)
{
    auto pt = e.position;
    if (groupFilterRects[0].contains(pt)) {
        selectedClusterFilter = -1; activeSubFilterMask = 0; audioProcessor.savedClusterFilter = -1; audioProcessor.savedSubFilterMask = 0;
        mapCanvas.setViewFilter(audioProcessor.showLoopsTab.load(), selectedClusterFilter, activeSubFilterMask); repaint(); return;
    }

    for (int c = 0; c < 8; ++c) {
        const auto& btnR = groupFilterRects[(size_t)(c + 1)]; const auto& dropR = groupDropdownRects[(size_t)c];
        if (btnR.contains(pt)) {
            if (dropR.contains(pt) || e.mods.isPopupMenu() || selectedClusterFilter == c) {
                selectedClusterFilter = c; audioProcessor.savedClusterFilter = c;
                mapCanvas.setViewFilter(audioProcessor.showLoopsTab.load(), selectedClusterFilter, activeSubFilterMask | selectedClusterSubgroupBit[(size_t)c]);
                repaint(); showClusterSubgroupPopupMenu(c, btnR); return;
            }
            selectedClusterFilter = c; audioProcessor.savedClusterFilter = c;
            mapCanvas.setViewFilter(audioProcessor.showLoopsTab.load(), selectedClusterFilter, activeSubFilterMask | selectedClusterSubgroupBit[(size_t)c]);
            repaint(); return;
        }
    }

    static const uint32_t subBits[7] = { Atlas::Sub_Clean, Atlas::Sub_Noisy, Atlas::Sub_Warm, Atlas::Sub_Pluck, Atlas::Sub_Sustain, Atlas::Sub_RiserSweep, Atlas::Sub_Textural };
    for (int i = 0; i < 7; ++i) {
        if (subFilterRects[(size_t)i].contains(pt)) {
            activeSubFilterMask ^= subBits[i]; audioProcessor.savedSubFilterMask = activeSubFilterMask;
            uint32_t clusterSub = (selectedClusterFilter >= 0) ? selectedClusterSubgroupBit[(size_t)selectedClusterFilter] : 0;
            mapCanvas.setViewFilter(audioProcessor.showLoopsTab.load(), selectedClusterFilter, activeSubFilterMask | clusterSub); repaint(); return;
        }
    }
    if (btnOctDown.contains(pt)) { audioProcessor.octaveShift.store(juce::jlimit(-2, 2, audioProcessor.octaveShift.load() - 1)); repaint(); }
    else if (btnOctUp.contains(pt)) { audioProcessor.octaveShift.store(juce::jlimit(-2, 2, audioProcessor.octaveShift.load() + 1)); repaint(); }
}

void MelodicAtlasAudioProcessorEditor::showClusterSubgroupPopupMenu(int clusterIndex, juce::Rectangle<float> targetBtn)
{
    if (clusterIndex < 0 || clusterIndex >= 8) return;
    auto cg = static_cast<Atlas::ClusterGroup>(clusterIndex); auto options = Atlas::MusicMath::getSubgroupsForCluster(cg); uint32_t currentBit = selectedClusterSubgroupBit[(size_t)clusterIndex];
    juce::PopupMenu menu; for (size_t i = 0; i < options.size(); ++i) menu.addItem((int)i + 1, options[i].fullName, true, (options[i].flagBit == currentBit));
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(localAreaToGlobal(targetBtn.toNearestInt())),
                       [this, clusterIndex, options](int res) {
                           if (res >= 1 && res <= (int)options.size()) {
                               selectedClusterSubgroupBit[(size_t)clusterIndex] = options[(size_t)(res - 1)].flagBit; selectedClusterFilter = clusterIndex; audioProcessor.savedClusterFilter = clusterIndex;
                               updateDynamicLayoutRects(); mapCanvas.setViewFilter(audioProcessor.showLoopsTab.load(), selectedClusterFilter, activeSubFilterMask | selectedClusterSubgroupBit[(size_t)clusterIndex]); repaint();
                           }
                       });
}

void MelodicAtlasAudioProcessorEditor::handleBankAndRightPanelClick(const juce::MouseEvent& e)
{
    auto pt = e.position; padDragStartPos = pt;
    if (btnPadTriggerMode.contains(pt)) { audioProcessor.bankModeIndex.store((int)Atlas::BankMode::PadTrigger); repaint(); return; }
    if (btnChromaticMode.contains(pt)) { audioProcessor.bankModeIndex.store((int)Atlas::BankMode::Chromatic); repaint(); return; }
    if (btnLockPadsIcon.contains(pt)) { audioProcessor.padSettingsLocked.store(!audioProcessor.padSettingsLocked.load()); repaint(); return; }
    if (btnGlobalExportModeIcon.contains(pt)) { audioProcessor.setGlobalExportMode(!audioProcessor.globalExportProcessed.load()); repaint(); return; }
    if (btnClearAllIcon.contains(pt)) { audioProcessor.clearAllPadsWithUndo(); repaint(); return; }
    if (btnDragAllIcon.contains(pt)) {
        std::vector<int> padsToExport; for (int i = 0; i < 12; ++i) if (audioProcessor.getSamplerEngine().getPadState(i).sampleIndex >= 0) padsToExport.push_back(i);
        exportPadsToDaw(padsToExport); return;
    }

    for (int i = 0; i < 12; ++i)
    {
        const auto& r = padRects[(size_t)i];
        if (!r.contains(pt)) continue;

        auto& padState = audioProcessor.getSamplerEngine().getPadState(i);

        // Мультивыбор пэдов (Shift или Cmd/Ctrl)
        if (e.mods.isShiftDown() || e.mods.isCommandDown()) {
            selectedPads.insert(i);
            primarySelectedPad = i;
        } else {
            selectedPads.clear();
            selectedPads.insert(i);
            primarySelectedPad = i;
        }
        audioProcessor.primarySelectedPad = primarySelectedPad;

        juce::Rectangle<float> gearBtnZone(r.getRight() - 20.0f, r.getY(), 20.0f, 20.0f);
        if (padState.sampleIndex >= 0 && gearBtnZone.contains(pt)) {
            padState.settingsViewOpen = !padState.settingsViewOpen; repaint(); return;
        }

        if (padState.settingsViewOpen) {
            audioProcessor.pushGlobalUndoState(); activeDragPadIndex = i;
            juce::Rectangle<float> regionWaveBox(r.getX() + 6.0f, r.getY() + 4.0f, r.getWidth() - 28.0f, 16.0f);
            if (regionWaveBox.expanded(2.0f, 2.0f).contains(pt)) {
                float normX = juce::jlimit(0.0f, 1.0f, (pt.x - regionWaveBox.getX()) / std::max(1.0f, regionWaveBox.getWidth()));
                float distStart = std::abs(normX - padState.regionStart); float distEnd = std::abs(normX - padState.regionEnd);
                if (distStart <= distEnd) { currentPadDragAction = PadDragAction::RegionStart; padState.regionStart = juce::jlimit(0.0f, padState.regionEnd - 0.04f, normX); }
                else { currentPadDragAction = PadDragAction::RegionEnd; padState.regionEnd = juce::jlimit(padState.regionStart + 0.04f, 1.0f, normX); }
                repaint(); return;
            }

            float relY = (pt.y - (r.getY() + 23.0f)) / (r.getHeight() - 25.0f);
            float relX = (pt.x - r.getX()) / std::max(1.0f, r.getWidth());
            if (relY < 0.5f) { // Top row
                if (relX < 0.25f) { currentPadDragAction = PadDragAction::DialSemitones; dragStartParamValue = (float)padState.semitoneOffset; }
                else if (relX < 0.50f) { currentPadDragAction = PadDragAction::DialCents; dragStartParamValue = (float)padState.centOffset; }
                else if (relX < 0.75f) { currentPadDragAction = PadDragAction::DialVol; dragStartParamValue = padState.volumeDb; }
                else { currentPadDragAction = PadDragAction::DialPan; dragStartParamValue = padState.pan; }
            } else { // Bottom row (ADSR)
                if (relX < 0.25f) { currentPadDragAction = PadDragAction::DialAttack; dragStartParamValue = padState.attackSec; }
                else if (relX < 0.50f) { currentPadDragAction = PadDragAction::DialDecay; dragStartParamValue = padState.decaySec; }
                else if (relX < 0.75f) { currentPadDragAction = PadDragAction::DialSustain; dragStartParamValue = padState.sustainLvl; }
                else { currentPadDragAction = PadDragAction::DialRelease; dragStartParamValue = padState.releaseSec; }
            }
            repaint(); return;
        }

        // Защищенное добавление на пустой пэд (Правый клик ИЛИ плюсик)
        juce::Rectangle<float> plusBtnZone(r.getCentreX() - 15.0f, r.getCentreY() - 15.0f, 30.0f, 30.0f);
        if (padState.sampleIndex < 0) {
            int activeMapIdx = mapCanvas.getActiveSampleIndex();
            const auto& db = audioProcessor.getSampleDatabase();
            if ((plusBtnZone.contains(pt) || e.mods.isPopupMenu()) && activeMapIdx >= 0 && activeMapIdx < (int)db.size()) {
                audioProcessor.assignSampleToPadWithPreviewSettings(i, activeMapIdx, true);
                audioProcessor.getSamplerEngine().triggerPadFromUI(i);
            }
            repaint(); return;
        }

        juce::Rectangle<float> chromDotZone(r.getRight() - 68.0f, r.getY(), 15.0f, 19.0f);
        juce::Rectangle<float> pdBtnZone   (r.getRight() - 52.0f, r.getY(), 16.0f, 19.0f);
        juce::Rectangle<float> undoBtnZone (r.getRight() - 35.0f, r.getY(), 16.0f, 19.0f);

        if (chromDotZone.contains(pt)) { audioProcessor.pushGlobalUndoState(); padState.chromaticActive = !padState.chromaticActive; repaint(); return; }
        if (pdBtnZone.contains(pt)) { audioProcessor.pushGlobalUndoState(); padState.exportProcessed = !padState.exportProcessed; repaint(); return; }
        if (undoBtnZone.contains(pt)) { audioProcessor.pushGlobalUndoState(); audioProcessor.getSamplerEngine().undoPadSample(i); repaint(); return; }

        float midY = r.getY() + 19.0f; float midH = std::max(18.0f, r.getHeight() - 36.0f);
        juce::Rectangle<float> leftArrowZone (r.getX(), midY, 19.0f, midH);
        juce::Rectangle<float> rightArrowZone(r.getRight() - 19.0f, midY, 19.0f, midH);

        if (leftArrowZone.contains(pt)) { audioProcessor.stepSimilarSampleInPad(i, -1); if (padState.sampleIndex >= 0) mapCanvas.selectSampleFromPad(padState.sampleIndex); repaint(); return; }
        if (rightArrowZone.contains(pt)) { audioProcessor.stepSimilarSampleInPad(i, 1); if (padState.sampleIndex >= 0) mapCanvas.selectSampleFromPad(padState.sampleIndex); repaint(); return; }

        activeDragPadIndex = i; currentPadDragAction = PadDragAction::DragPadToDaw;
        mapCanvas.selectSampleFromPad(padState.sampleIndex);
        audioProcessor.getSamplerEngine().triggerPadFromUI(i);
        repaint(); return;
    }
}

void MelodicAtlasAudioProcessorEditor::showKeyPopupMenu() {
    juce::PopupMenu m; int currentKey = audioProcessor.projectKeyRoot.load();
    for (int k = 0; k < 12; ++k) m.addItem(k + 1, Atlas::MusicMath::getNoteName(k), true, k == currentKey);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(localAreaToGlobal(btnKeyRoot.toNearestInt())),
                    [this](int res) { if (res >= 1 && res <= 12) { audioProcessor.projectKeyRoot.store(res - 1); repaint(); } });
}
void MelodicAtlasAudioProcessorEditor::showScalePopupMenu() {
    juce::PopupMenu m; int currentScale = audioProcessor.projectScaleType.load();
    for (int s = 0; s < 10; ++s) m.addItem(s + 1, Atlas::MusicMath::getScaleFullName(static_cast<Atlas::ScaleType>(s)), true, s == currentScale);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(localAreaToGlobal(btnScaleType.toNearestInt())),
                    [this](int res) { if (res >= 1 && res <= 10) { audioProcessor.projectScaleType.store(res - 1); repaint(); } });
}
void MelodicAtlasAudioProcessorEditor::exportPadsToDaw(const std::vector<int>& padIndices) {
    juce::StringArray filesToDrag;
    for (int idx : padIndices) {
        juce::File exportedFile = audioProcessor.getSamplerEngine().renderPadForExport(idx);
        if (exportedFile.existsAsFile()) filesToDrag.addIfNotAlreadyThere(exportedFile.getFullPathName());
    }
    if (!filesToDrag.isEmpty()) performExternalDragDropOfFiles(filesToDrag, false);
}
void MelodicAtlasAudioProcessorEditor::openFolderChooser() {
    folderChooser = std::make_unique<juce::FileChooser>("Select Sample Library Folder", juce::File::getSpecialLocation(juce::File::userHomeDirectory));
    folderChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this](const juce::FileChooser& fc) {
        auto folder = fc.getResult(); if (folder.isDirectory()) audioProcessor.scanFolderAsync(folder);
    });
}
bool MelodicAtlasAudioProcessorEditor::isInterestedInFileDrag(const juce::StringArray& files) { for (const auto& f : files) if (juce::File(f).isDirectory()) return true; return false; }
void MelodicAtlasAudioProcessorEditor::filesDropped(const juce::StringArray& files, int, int) { for (const auto& f : files) { juce::File dir(f); if (dir.isDirectory()) { audioProcessor.scanFolderAsync(dir); break; } } }
void MelodicAtlasAudioProcessorEditor::timerCallback()
{
    uint64_t currentVer = audioProcessor.databaseVersion.load();
    if (currentVer != lastSeenDbVersion) { lastSeenDbVersion = currentVer; mapCanvas.setSampleDatabase(&audioProcessor.getSampleDatabase()); repaint(); }
    uint32_t currentMask = audioProcessor.getSamplerEngine().getActivePlayingPadsMask();
    if (currentMask != lastPlayingMask) { lastPlayingMask = currentMask; repaint(0, (int)btnPadTriggerMode.getY() - 4, getWidth(), getHeight() - (int)btnPadTriggerMode.getY() + 4); }
    if (audioProcessor.isCurrentlyScanning()) repaint((int)btnScanFolder.getX() - 4, 0, (int)btnScanFolder.getWidth() + 8, 40);
}
