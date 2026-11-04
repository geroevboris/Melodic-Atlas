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
        if (selectedPads.empty()) 
            selectedPads.insert(primarySelectedPad);
            
        for (int pIdx : selectedPads) 
        {
            audioProcessor.assignSampleToPadWithPreviewSettings(pIdx, sampleIdx, true);
            primarySelectedPad = pIdx; 
            audioProcessor.primarySelectedPad = pIdx;
        }
        audioProcessor.getSamplerEngine().triggerPadFromUI(primarySelectedPad); 
        repaint();
    };

    auto setupEncoder = [this](juce::Slider& s, const juce::String& n, const juce::String& tip, float minV, float maxV, float defV, std::function<void(float)> cb) 
    {
        s.setName(n); 
        s.setTooltip(tip); 
        s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag); 
        s.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        s.setRange(minV, maxV); 
        s.setValue(defV, juce::dontSendNotification); 
        s.onValueChange = [cb, &s]() { cb((float)s.getValue()); }; 
        addAndMakeVisible(s);
    };
    
    setupEncoder(previewTuneSlider, "TUNE", "Preview Pitch", -12.0f, 12.0f, audioProcessor.bankPitchSemitones.load(), [this](float v){ audioProcessor.bankPitchSemitones.store(v); });
    setupEncoder(mapReverbSlider, "REV", "Map Reverb", 0.0f, 1.0f, audioProcessor.reverbAmount.load(), [this](float v){ audioProcessor.reverbAmount.store(v); });
    setupEncoder(bankReverbSlider, "REV", "Bank Reverb", 0.0f, 1.0f, audioProcessor.bankReverbAmount.load(), [this](float v){ audioProcessor.bankReverbAmount.store(v); });
    setupEncoder(masterVolSlider, "MAIN", "Main Vol", -24.0f, 6.0f, audioProcessor.masterVolumeDb.load(), [this](float v){ audioProcessor.masterVolumeDb.store(v); });

    updateDynamicLayoutRects(); 
    lastSeenDbVersion = audioProcessor.databaseVersion.load(); 
    startTimerHz(30);
}

MelodicAtlasAudioProcessorEditor::~MelodicAtlasAudioProcessorEditor() 
{ 
    setLookAndFeel(nullptr); 
}

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
    const float bankSectionH = std::max(200.0f, h * 0.32f); 
    const float bankTopY = h - bankSectionH - 10.0f; 
    
    // Карта больше, так как фильтры теперь в один ряд
    const float mapY = filterY + 44.0f; 
    const float mapH = std::max(180.0f, bankTopY - mapY - 8.0f);
    mapCanvas.setBounds(14, (int)mapY, (int)w - 28, (int)mapH);

    btnGlobalUndo = { 156.0f, 8.0f, 26.0f, 24.0f }; 
    btnGlobalRedo = { 186.0f, 8.0f, 26.0f, 24.0f };
    btnOneShots   = { 222.0f, 8.0f, 84.0f, 24.0f }; 
    btnLoops      = { 310.0f, 8.0f, 62.0f, 24.0f };
    
    btnKeyRoot          = { 414.0f, 8.0f, 42.0f, 24.0f }; 
    btnScaleType        = { 460.0f, 8.0f, 78.0f, 24.0f };
    btnPitchMode        = { 542.0f, 8.0f, 52.0f, 24.0f }; 
    btnTransposeQuality = { 598.0f, 8.0f, 68.0f, 24.0f };
    
    btnBpmHalf   = { 684.0f, 8.0f, 24.0f, 24.0f };
    btnBpmSync   = { 712.0f, 8.0f, 56.0f, 24.0f };
    btnBpmDouble = { 772.0f, 8.0f, 24.0f, 24.0f };
    btnScanFolder = { w - 178.0f, 8.0f, 164.0f, 24.0f };

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
            if (subShort.isNotEmpty()) 
                baseLabel += ":" + subShort; 
        }
        
        float textW = filterFont.getStringWidthFloat(baseLabel); 
        float btnW = textW + 28.0f;
        groupFilterRects[(size_t)(c + 1)] = { gx, row1Y, btnW, 24.0f }; 
        groupDropdownRects[(size_t)c] = { gx + btnW - 18.0f, row1Y, 18.0f, 24.0f };
        gx += btnW + 6.0f;
    }

    float octBlockRight = w - 22.0f; 
    float octBlockX = octBlockRight - 86.0f;
    octLabelRect = { octBlockX, filterY - 2.0f, 86.0f, 16.0f };
    btnOctDown = { octBlockX, filterY + 16.0f, 40.0f, 24.0f }; 
    btnOctUp = { octBlockX + 46.0f, filterY + 16.0f, 40.0f, 24.0f };
    
    mapReverbSlider.setBounds((int)(octBlockX - 58.0f), (int)(filterY + 5.0f), 46, 46);
    previewTuneSlider.setBounds((int)(octBlockX - 112.0f), (int)(filterY + 5.0f), 46, 46);

    btnPadTriggerMode = { 66.0f, bankTopY, 102.0f, 20.0f }; 
    btnChromaticMode = { 174.0f, bankTopY, 96.0f, 20.0f };

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
        padRects[(size_t)i] = { 14.0f + (float)col * (padW + gapX), gridTopY + (float)row * (padH + gapY), padW, padH };
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
    
    btnLockPadsIcon = { ix, iconRowY, iconW, 30.0f }; 
    btnGlobalExportModeIcon = { ix + iconW + iconGap, iconRowY, iconW, 30.0f };
    btnClearAllIcon = { ix + (iconW + iconGap) * 2.0f, iconRowY, iconW, 30.0f }; 
    btnDragAllIcon = { ix + (iconW + iconGap) * 3.0f, iconRowY, iconW, 30.0f };
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
    const juce::Font monoReg(juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::plain);
    
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

    auto drawBtn = [&](juce::Rectangle<float> r, const juce::String& t, bool act, juce::Colour aCol = juce::Colour(0xff283f35)) 
    {
        g.setColour(act ? aCol : juce::Colour(0xff1d232d)); 
        g.fillRoundedRectangle(r, 3.0f); 
        g.setColour(act ? juce::Colour(0xff7ec8a3) : juce::Colour(0xff343e4c)); 
        g.drawRoundedRectangle(r, 3.0f, 1.0f);
        g.setColour(act ? juce::Colours::white : juce::Colour(0xff9aa6b5)); 
        g.setFont(monoReg); 
        g.drawText(t, r, juce::Justification::centred, false);
    };

    g.setColour(audioProcessor.canGlobalUndo() ? juce::Colour(0xff465466) : juce::Colour(0xff28303c)); 
    g.drawRoundedRectangle(btnGlobalUndo, 3.0f, 1.0f); 
    drawUndoVectorIcon(g, btnGlobalUndo.reduced(6.0f, 5.0f), audioProcessor.canGlobalUndo() ? juce::Colour(0xffdce6f2) : juce::Colour(0xff4d5866), false);
    
    g.setColour(audioProcessor.canGlobalRedo() ? juce::Colour(0xff465466) : juce::Colour(0xff28303c)); 
    g.drawRoundedRectangle(btnGlobalRedo, 3.0f, 1.0f); 
    drawUndoVectorIcon(g, btnGlobalRedo.reduced(6.0f, 5.0f), audioProcessor.canGlobalRedo() ? juce::Colour(0xffdce6f2) : juce::Colour(0xff4d5866), true);

    drawBtn(btnOneShots, "ONE-SHOTS", !audioProcessor.showLoopsTab.load()); 
    drawBtn(btnLoops, "LOOPS", audioProcessor.showLoopsTab.load());
    
    g.setColour(juce::Colour(0xff8c98a8)); 
    g.setFont(monoReg); 
    g.drawText("KEY:", 380, 0, 32, 40, juce::Justification::centredLeft, false);
    
    drawBtn(btnKeyRoot, Atlas::MusicMath::getNoteName(audioProcessor.projectKeyRoot.load()), true); 
    drawDropdownTriangle(g, btnKeyRoot.withTrimmedLeft(btnKeyRoot.getWidth() - 14.0f).reduced(3.5f, 8.5f), juce::Colours::white);
    drawBtn(btnScaleType, Atlas::MusicMath::getScaleShortName(static_cast<Atlas::ScaleType>(audioProcessor.projectScaleType.load())), true);
    
    int pMode = audioProcessor.pitchModeIndex.load(); 
    drawBtn(btnPitchMode, (pMode == 0) ? "SNAP" : (pMode == 1 ? "ROOT" : "OFF"), pMode != 2);
    drawBtn(btnTransposeQuality, (audioProcessor.transposeQualityIndex.load() == 0) ? "CLEAN" : "STRETCH", true);

    g.setColour(juce::Colour(0xff8c98a8)); 
    g.drawText("BPM:", 648, 0, 32, 40, juce::Justification::centredLeft, false);
    
    drawBtn(btnBpmHalf, "/2", false); 
    drawBtn(btnBpmSync, juce::String(audioProcessor.currentBpm.load(), 1) + (audioProcessor.bpmLockedToHost.load() ? "*" : ""), audioProcessor.bpmLockedToHost.load(), juce::Colour(0xff283f35));
    drawBtn(btnBpmDouble, "x2", false);

    drawBtn(btnScanFolder, audioProcessor.isCurrentlyScanning() ? ("SCANNING " + juce::String((int)(audioProcessor.getScanProgress() * 100.0f)) + "%") : "SCAN SAMPLE FOLDER", audioProcessor.isCurrentlyScanning(), juce::Colour(0xff354839));
}

void MelodicAtlasAudioProcessorEditor::drawFilterBar(juce::Graphics& g) 
{
    const juce::Font monoBold(juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::bold);
    g.setColour(juce::Colour(0xff161b22)); 
    g.fillRect(14, 46, getWidth() - 28, 44); 
    g.setColour(juce::Colour(0xff252d3a)); 
    g.drawRect(14, 46, getWidth() - 28, 44, 1);

    bool allOn = (selectedClusterFilter == -1);
    g.setColour(allOn ? juce::Colour(0xff284035) : juce::Colour(0xff1d232c)); 
    g.fillRoundedRectangle(groupFilterRects[0], 3.0f); 
    g.setColour(allOn ? juce::Colour(0xff7ec8a3) : juce::Colour(0xff323b48)); 
    g.drawRoundedRectangle(groupFilterRects[0], 3.0f, 1.0f);
    g.setColour(allOn ? juce::Colours::white : juce::Colour(0xff9aa6b5)); 
    g.setFont(monoBold); 
    g.drawText("ALL", groupFilterRects[0], juce::Justification::centred, false);

    for (int c = 0; c < 8; ++c) 
    {
        auto cg = static_cast<Atlas::ClusterGroup>(c); 
        bool on = (selectedClusterFilter == c);
        juce::String label = Atlas::MusicMath::getClusterName(cg);
        
        if (selectedClusterSubgroupBit[(size_t)c] != 0) 
            label += ":" + Atlas::MusicMath::getSubgroupShortLabel(cg, selectedClusterSubgroupBit[(size_t)c]);
            
        juce::Colour col = Atlas::MusicMath::getClusterColour(cg, 1);
        
        g.setColour(on ? col.withAlpha(0.28f) : juce::Colour(0xff1d232c)); 
        g.fillRoundedRectangle(groupFilterRects[(size_t)(c + 1)], 3.0f);
        
        g.setColour(on ? col.brighter(0.25f) : juce::Colour(0xff323b48)); 
        g.drawRoundedRectangle(groupFilterRects[(size_t)(c + 1)], 3.0f, 1.0f);
        
        g.setColour(on ? col.withAlpha(0.5f) : juce::Colour(0xff2a323e)); 
        g.drawVerticalLine((int)groupDropdownRects[(size_t)c].getX(), groupFilterRects[(size_t)(c + 1)].getY() + 4.0f, groupFilterRects[(size_t)(c + 1)].getBottom() - 4.0f);
        
        g.setColour(on ? juce::Colours::white : juce::Colour(0xffa4b0c0)); 
        g.drawText(label, groupFilterRects[(size_t)(c + 1)].withTrimmedRight(16.0f), juce::Justification::centred, false);
        
        drawDropdownTriangle(g, groupDropdownRects[(size_t)c].reduced(5.0f, 9.0f), on ? juce::Colours::white : juce::Colour(0xff8a96a6));
    }
    
    int oct = audioProcessor.octaveShift.load(); 
    g.setColour(juce::Colour(0xffa4b0c0)); 
    g.setFont(monoBold);
    g.drawText("OCTAVE: " + (oct > 0 ? "+" + juce::String(oct) : juce::String(oct)), octLabelRect, juce::Justification::centred, false);
    
    g.setColour(oct < 0 ? juce::Colour(0xff284035) : juce::Colour(0xff1d232c)); 
    g.fillRoundedRectangle(btnOctDown, 3.0f); 
    g.setColour(oct < 0 ? juce::Colour(0xff7ec8a3) : juce::Colour(0xff343e4c)); 
    g.drawRoundedRectangle(btnOctDown, 3.0f, 1.0f); 
    g.setColour(juce::Colours::white); 
    g.drawText("-", btnOctDown, juce::Justification::centred, false);
    
    g.setColour(oct > 0 ? juce::Colour(0xff284035) : juce::Colour(0xff1d232c)); 
    g.fillRoundedRectangle(btnOctUp, 3.0f); 
    g.setColour(oct > 0 ? juce::Colour(0xff7ec8a3) : juce::Colour(0xff343e4c)); 
    g.drawRoundedRectangle(btnOctUp, 3.0f, 1.0f); 
    g.setColour(juce::Colours::white); 
    g.drawText("+", btnOctUp, juce::Justification::centred, false);
}

void MelodicAtlasAudioProcessorEditor::drawBankAndRightPanel(juce::Graphics& g) 
{
    const juce::Font monoBold(juce::Font::getDefaultMonospacedFontName(), 11.5f, juce::Font::bold); 
    const juce::Font monoReg(juce::Font::getDefaultMonospacedFontName(), 10.5f, juce::Font::plain);
    
    g.setColour(juce::Colour(0xffd0dae5)); 
    g.setFont(monoBold); 
    g.drawText("BANK:", 14, (int)btnPadTriggerMode.getY(), 50, 20, juce::Justification::centredLeft, false);
    
    bool isChrom = (audioProcessor.bankModeIndex.load() == (int)Atlas::BankMode::Chromatic);
    
    g.setColour(!isChrom ? juce::Colour(0xff284035) : juce::Colour(0xff1c222b)); 
    g.fillRoundedRectangle(btnPadTriggerMode, 3.0f); 
    g.setColour(!isChrom ? juce::Colour(0xff7ec8a3) : juce::Colour(0xff343e4d)); 
    g.drawRoundedRectangle(btnPadTriggerMode, 3.0f, 1.0f); 
    g.setColour(!isChrom ? juce::Colours::white : juce::Colour(0xff8e9aa8)); 
    g.setFont(monoReg); 
    g.drawText("PAD TRIGGER", btnPadTriggerMode, juce::Justification::centred, false);
    
    g.setColour(isChrom ? juce::Colour(0xff284035) : juce::Colour(0xff1c222b)); 
    g.fillRoundedRectangle(btnChromaticMode, 3.0f); 
    g.setColour(isChrom ? juce::Colour(0xff7ec8a3) : juce::Colour(0xff343e4d)); 
    g.drawRoundedRectangle(btnChromaticMode, 3.0f, 1.0f); 
    g.setColour(isChrom ? juce::Colours::white : juce::Colour(0xff8e9aa8)); 
    g.drawText("CHROMATIC", btnChromaticMode, juce::Justification::centred, false);

    uint32_t playingMask = audioProcessor.getSamplerEngine().getActivePlayingPadsMask();
    for (int i = 0; i < 12; ++i) 
    {
        drawPadSlot(g, i, padRects[(size_t)i], ((playingMask & (1u << i)) != 0));
    }

    g.setColour(juce::Colour(0xff161b22)); 
    g.fillRoundedRectangle(rightBankPanelRect, 4.0f); 
    g.setColour(juce::Colour(0xff28313e)); 
    g.drawRoundedRectangle(rightBankPanelRect, 4.0f, 1.0f);
    
    bool locked = audioProcessor.padSettingsLocked.load(); 
    g.setColour(locked ? juce::Colour(0xff284035) : juce::Colour(0xff1e242d)); 
    g.fillRoundedRectangle(btnLockPadsIcon, 3.5f); 
    g.setColour(locked ? juce::Colour(0xff7ec8a3) : juce::Colour(0xff36404e)); 
    g.drawRoundedRectangle(btnLockPadsIcon, 3.5f, 1.0f); 
    drawLockVectorIcon(g, btnLockPadsIcon.reduced(8.0f, 6.0f), locked, locked ? juce::Colour(0xff7ec8a3) : juce::Colour(0xff9aa6b5));
    
    bool globalProc = audioProcessor.globalExportProcessed.load(); 
    g.setColour(globalProc ? juce::Colour(0xff284035) : juce::Colour(0xff1e242d)); 
    g.fillRoundedRectangle(btnGlobalExportModeIcon, 3.5f); 
    g.setColour(globalProc ? juce::Colour(0xff7ec8a3) : juce::Colour(0xff4a607a)); 
    g.drawRoundedRectangle(btnGlobalExportModeIcon, 3.5f, 1.0f); 
    g.setColour(juce::Colours::white); 
    g.setFont(monoBold); 
    g.drawText(globalProc ? "P" : "D", btnGlobalExportModeIcon, juce::Justification::centred, false);
    
    g.setColour(juce::Colour(0xff1e242d)); 
    g.fillRoundedRectangle(btnClearAllIcon, 3.5f); 
    g.setColour(juce::Colour(0xff36404e)); 
    g.drawRoundedRectangle(btnClearAllIcon, 3.5f, 1.0f); 
    drawTrashVectorIcon(g, btnClearAllIcon.reduced(8.5f, 6.5f), juce::Colour(0xffb5c0ce));
    
    g.setColour(juce::Colour(0xff6da37b)); 
    g.fillRoundedRectangle(btnDragAllIcon, 3.5f); 
    g.setColour(juce::Colour(0xff96d1a4)); 
    g.drawRoundedRectangle(btnDragAllIcon, 3.5f, 1.0f); 
    drawExportVectorIcon(g, btnDragAllIcon.reduced(7.5f, 6.5f), juce::Colour(0xff0d1510));
}

void MelodicAtlasAudioProcessorEditor::drawPadSlot(juce::Graphics& g, int padIdx, const juce::Rectangle<float>& r, bool isPlayingNow) 
{
    const auto& st = audioProcessor.getSamplerEngine().getPadState(padIdx); 
    const auto* rec = audioProcessor.getSamplerEngine().getPadRecord(padIdx); 
    auto audio = audioProcessor.getSamplerEngine().getPadAudio(padIdx);
    
    bool isSel = selectedPads.count(padIdx) > 0; 
    bool isDrop = (hoveredDropPadIndex == padIdx);
    juce::Colour bgCol = (rec != nullptr) ? juce::Colour(0xff1e242d) : juce::Colour(0xff181d25);
    
    if (isPlayingNow) bgCol = juce::Colour(0xff233a32); 
    else if (isDrop) bgCol = juce::Colour(0xff273731);
    
    g.setColour(bgCol); 
    g.fillRoundedRectangle(r, 3.5f);
    
    if (isPlayingNow) { 
        g.setColour(juce::Colour(0xff62f5b5)); 
        g.drawRoundedRectangle(r, 3.5f, 2.0f); 
    }
    else if (isSel || isDrop) { 
        g.setColour(juce::Colour(0xffe6c167)); 
        g.drawRoundedRectangle(r, 3.5f, 1.6f); 
    }
    else { 
        g.setColour(juce::Colour(0xff2c3542)); 
        g.drawRoundedRectangle(r, 3.5f, 1.0f); 
    }
    
    if (st.settingsViewOpen) {
        drawPadGearSettingsView(g, padIdx, r, st, audio); 
    }
    else {
        drawPadNormalView(g, padIdx, r, st, rec, audio, isPlayingNow);
    }
}

void MelodicAtlasAudioProcessorEditor::drawPadNormalView(juce::Graphics& g, int padIdx, const juce::Rectangle<float>& r, const Atlas::PadSlotState& st, const Atlas::SampleRecord* rec, std::shared_ptr<Atlas::CachedAudioData> audio, bool isPlayingNow) 
{
    if (rec == nullptr || audio == nullptr) 
    {
        drawPlusVectorIcon(g, juce::Rectangle<float>(r.getCentreX() - 15.0f, r.getCentreY() - 15.0f, 30.0f, 30.0f), juce::Colour(0xff4a5462));
        g.setColour(juce::Colour(0xff566272)); 
        g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 9.5f, juce::Font::bold)); 
        g.drawText("ADD", r.withTrimmedTop(r.getHeight() * 0.7f), juce::Justification::centred, false); 
        return;
    }
    
    const juce::Font monoNote(juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::bold); 
    const juce::Font monoName(juce::Font::getDefaultMonospacedFontName(), 10.5f, juce::Font::plain);
    
    g.setColour(isPlayingNow ? juce::Colour(0xff62f5b5) : juce::Colour(0xffe2eaf4)); 
    g.setFont(monoNote); 
    g.drawText(Atlas::MusicMath::formatPadNoteAndCents(rec->pitchClass, st.semitoneOffset, st.centOffset), (int)r.getX() + 6, (int)r.getY() + 3, 64, 15, juce::Justification::centredLeft, false);

    juce::Rectangle<float> closeBtnZone(r.getRight() - 18.0f, r.getY() + 3.0f, 15.0f, 14.0f);
    juce::Rectangle<float> gearBtnZone (r.getRight() - 36.0f, r.getY() + 3.0f, 15.0f, 14.0f);
    juce::Rectangle<float> undoBtnZone (r.getRight() - 54.0f, r.getY() + 3.0f, 15.0f, 14.0f);
    juce::Rectangle<float> pdBtnZone   (r.getRight() - 72.0f, r.getY() + 3.0f, 15.0f, 14.0f);
    juce::Rectangle<float> chromDotZone(r.getRight() - 88.0f, r.getY() + 3.0f, 14.0f, 14.0f);

    drawCloseVectorIcon(g, closeBtnZone.reduced(2.5f), juce::Colour(0xff8490a0));
    drawGearVectorIcon(g, gearBtnZone.reduced(2.0f), juce::Colour(0xff9aa8b8));
    drawUndoVectorIcon(g, undoBtnZone.reduced(2.5f), !st.history.empty() ? juce::Colour(0xffb8c5d6) : juce::Colour(0xff46505e), false);
    
    g.setColour(st.exportProcessed ? juce::Colour(0xff263f34) : juce::Colour(0xff232c38)); 
    g.fillRoundedRectangle(pdBtnZone, 2.5f); 
    g.setColour(st.exportProcessed ? juce::Colour(0xff7ec8a3) : juce::Colour(0xff5c708a)); 
    g.drawRoundedRectangle(pdBtnZone, 2.5f, 1.0f); 
    g.setColour(juce::Colours::white); 
    g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 9.5f, juce::Font::bold)); 
    g.drawText(st.exportProcessed ? "P" : "D", pdBtnZone, juce::Justification::centred, false);
    
    if (st.chromaticActive) { 
        g.setColour(juce::Colour(0xffffcf48)); 
        g.fillEllipse(chromDotZone.reduced(3.2f)); 
    } 
    else { 
        g.setColour(juce::Colour(0xff525e6e)); 
        g.drawEllipse(chromDotZone.reduced(3.2f), 1.3f); 
    }

    float midY = r.getY() + 19.0f; 
    float midH = std::max(18.0f, r.getHeight() - 36.0f);
    drawArrowVectorIcon(g, juce::Rectangle<float>(r.getX() + 3.0f, midY, 15.0f, midH).withSizeKeepingCentre(9.0f, 11.0f), false, juce::Colour(0xff7e8c9f));
    drawArrowVectorIcon(g, juce::Rectangle<float>(r.getRight() - 18.0f, midY, 15.0f, midH).withSizeKeepingCentre(9.0f, 11.0f), true, juce::Colour(0xff7e8c9f));

    g.setColour(juce::Colour(0xffdce5f0)); 
    g.setFont(monoName); 
    g.drawText(rec->name.substring(0, 16), (int)r.getX() + 6, (int)r.getBottom() - 16, (int)r.getWidth() - 12, 14, juce::Justification::centred, true);
}

void MelodicAtlasAudioProcessorEditor::drawPadGearSettingsView(juce::Graphics& g, int padIdx, const juce::Rectangle<float>& r, const Atlas::PadSlotState& st, std::shared_ptr<Atlas::CachedAudioData> audio) 
{
    drawGearVectorIcon(g, juce::Rectangle<float>(r.getRight() - 36.0f, r.getY() + 3.0f, 15.0f, 14.0f).reduced(2.0f), juce::Colour(0xffe6c167));
    drawCloseVectorIcon(g, juce::Rectangle<float>(r.getRight() - 18.0f, r.getY() + 3.0f, 15.0f, 14.0f).reduced(2.5f), juce::Colour(0xff8490a0));

    float dialsTopY = r.getY() + 23.0f; 
    float rowH = (r.getBottom() - dialsTopY - 2.0f) / 2.0f; 
    float dialSize = std::min(24.0f, rowH - 6.0f); 
    float colW = (r.getWidth() - 8.0f) / 4.0f;
    
    juce::String panStr = (std::abs(st.pan) < 0.05f) ? "C" : ((st.pan < 0.0f) ? ("L" + juce::String((int)std::round(-st.pan * 50.0f))) : ("R" + juce::String((int)std::round(st.pan * 50.0f))));
    
    drawMiniEncoder(g, { r.getX() + 4.0f + colW * 0.0f + (colW - dialSize) * 0.5f, dialsTopY, dialSize, dialSize }, (st.semitoneOffset + 24.0f) / 48.0f, "TUNE", (st.semitoneOffset > 0 ? "+" : "") + juce::String(st.semitoneOffset), true);
    drawMiniEncoder(g, { r.getX() + 4.0f + colW * 1.0f + (colW - dialSize) * 0.5f, dialsTopY, dialSize, dialSize }, (st.centOffset + 100.0f) / 200.0f, "CENT", (st.centOffset > 0 ? "+" : "") + juce::String(st.centOffset), true);
    drawMiniEncoder(g, { r.getX() + 4.0f + colW * 2.0f + (colW - dialSize) * 0.5f, dialsTopY, dialSize, dialSize }, (st.volumeDb + 24.0f) / 30.0f, "VOL", juce::String((int)std::round(st.volumeDb)) + "dB", false);
    drawMiniEncoder(g, { r.getX() + 4.0f + colW * 3.0f + (colW - dialSize) * 0.5f, dialsTopY, dialSize, dialSize }, (st.pan + 1.0f) * 0.5f, "PAN", panStr, true);
    
    float dY2 = dialsTopY + rowH;
    drawMiniEncoder(g, { r.getX() + 4.0f + colW * 0.0f + (colW - dialSize) * 0.5f, dY2, dialSize, dialSize }, st.attackSec / 2.0f, "ATT", juce::String(st.attackSec, 2) + "s", false);
    drawMiniEncoder(g, { r.getX() + 4.0f + colW * 1.0f + (colW - dialSize) * 0.5f, dY2, dialSize, dialSize }, st.decaySec / 5.0f, "DEC", juce::String(st.decaySec, 2) + "s", false);
    drawMiniEncoder(g, { r.getX() + 4.0f + colW * 2.0f + (colW - dialSize) * 0.5f, dY2, dialSize, dialSize }, st.sustainLvl, "SUS", juce::String((int)std::round(st.sustainLvl * 100.0f)) + "%", false);
    drawMiniEncoder(g, { r.getX() + 4.0f + colW * 3.0f + (colW - dialSize) * 0.5f, dY2, dialSize, dialSize }, st.releaseSec / 5.0f, "REL", juce::String(st.releaseSec, 2) + "s", false);
}

void MelodicAtlasAudioProcessorEditor::drawMiniEncoder(juce::Graphics& g, juce::Rectangle<float> dialRect, float normVal, const juce::String& insideLabel, const juce::String& subValStr, bool bipolar) 
{
    normVal = juce::jlimit(0.0f, 1.0f, normVal); 
    const float startAngle = -2.35619f; 
    const float endAngle = 2.35619f; 
    const float curAngle = startAngle + normVal * (endAngle - startAngle); 
    const float radius = dialRect.getWidth() * 0.46f; 
    const auto centre = dialRect.getCentre();
    
    juce::Path bgArc; 
    bgArc.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, startAngle, endAngle, true); 
    g.setColour(juce::Colour(0xff28313e)); 
    g.strokePath(bgArc, juce::PathStrokeType(2.0f));
    
    juce::Path valArc; 
    if (bipolar) {
        valArc.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, std::min(0.0f, curAngle), std::max(0.0f, curAngle), true); 
    }
    else {
        valArc.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, startAngle, curAngle, true); 
    }
    
    g.setColour(juce::Colour(0xff7ec8a3)); 
    g.strokePath(valArc, juce::PathStrokeType(2.2f)); 
    g.setColour(juce::Colour(0xff151a22)); 
    g.fillEllipse(centre.x - radius * 0.82f, centre.y - radius * 0.82f, radius * 1.64f, radius * 1.64f);
    
    g.setColour(juce::Colour(0xffdce5f0)); 
    g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 7.0f, juce::Font::bold)); 
    g.drawText(insideLabel, dialRect, juce::Justification::centred, false);
    
    g.setColour(juce::Colour(0xff94a3b5)); 
    g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 7.5f, juce::Font::plain)); 
    g.drawText(subValStr, dialRect.translated(0.0f, dialRect.getHeight() * 0.82f).withHeight(11.0f).expanded(6.0f, 0.0f), juce::Justification::centred, false);
}

// ВЕКТОРНЫЕ ИКОНКИ
void MelodicAtlasAudioProcessorEditor::drawUndoVectorIcon(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour col, bool isRedo) 
{
    g.setColour(col); 
    juce::Path p; 
    float cx = area.getCentreX(); 
    float cy = area.getCentreY(); 
    float r = std::min(area.getWidth(), area.getHeight()) * 0.42f;
    
    if (!isRedo) 
    { 
        p.addCentredArc(cx, cy + 1.0f, r, r, 0.0f, -1.8f, 2.1f, true); 
        g.strokePath(p, juce::PathStrokeType(1.5f)); 
        juce::Point<float> tip(cx - r * std::sin(1.8f), cy + 1.0f - r * std::cos(1.8f)); 
        g.drawLine(tip.x, tip.y, tip.x + 3.5f, tip.y - 2.0f, 1.5f); 
        g.drawLine(tip.x, tip.y, tip.x + 2.5f, tip.y + 3.2f, 1.5f); 
    }
    else 
    { 
        p.addCentredArc(cx, cy + 1.0f, r, r, 0.0f, -2.1f, 1.8f, true); 
        g.strokePath(p, juce::PathStrokeType(1.5f)); 
        juce::Point<float> tip(cx + r * std::sin(1.8f), cy + 1.0f - r * std::cos(1.8f)); 
        g.drawLine(tip.x, tip.y, tip.x - 3.5f, tip.y - 2.0f, 1.5f); 
        g.drawLine(tip.x, tip.y, tip.x - 2.5f, tip.y + 3.2f, 1.5f); 
    }
}

void MelodicAtlasAudioProcessorEditor::drawGearVectorIcon(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour col) 
{
    g.setColour(col); 
    auto c = area.getCentre(); 
    float rOuter = std::min(area.getWidth(), area.getHeight()) * 0.46f; 
    float rInner = rOuter * 0.52f;
    
    for (int i = 0; i < 6; ++i) 
    { 
        float ang = (float)i * juce::MathConstants<float>::pi / 3.0f; 
        g.drawLine(c.x + std::cos(ang) * (rInner * 0.8f), c.y + std::sin(ang) * (rInner * 0.8f), c.x + std::cos(ang) * rOuter, c.y + std::sin(ang) * rOuter, 1.6f); 
    }
    
    g.fillEllipse(c.x - rInner, c.y - rInner, rInner * 2.0f, rInner * 2.0f); 
    g.setColour(juce::Colour(0xff181d25)); 
    g.fillEllipse(c.x - rInner * 0.48f, c.y - rInner * 0.48f, rInner * 0.96f, rInner * 0.96f);
}

void MelodicAtlasAudioProcessorEditor::drawArrowVectorIcon(juce::Graphics& g, juce::Rectangle<float> area, bool pointRight, juce::Colour col) 
{
    g.setColour(col); 
    juce::Path p; 
    
    if (pointRight) { 
        p.startNewSubPath(area.getX(), area.getY()); 
        p.lineTo(area.getRight(), area.getCentreY()); 
        p.lineTo(area.getX(), area.getBottom()); 
    } 
    else { 
        p.startNewSubPath(area.getRight(), area.getY()); 
        p.lineTo(area.getX(), area.getCentreY()); 
        p.lineTo(area.getRight(), area.getBottom()); 
    } 
    
    p.closeSubPath(); 
    g.fillPath(p);
}

void MelodicAtlasAudioProcessorEditor::drawDropdownTriangle(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour col) 
{
    g.setColour(col); 
    juce::Path p; 
    p.startNewSubPath(area.getX(), area.getY()); 
    p.lineTo(area.getRight(), area.getY()); 
    p.lineTo(area.getCentreX(), area.getBottom()); 
    p.closeSubPath(); 
    g.fillPath(p);
}

void MelodicAtlasAudioProcessorEditor::drawLockVectorIcon(juce::Graphics& g, juce::Rectangle<float> area, bool isLocked, juce::Colour col) 
{
    g.setColour(col); 
    juce::Rectangle<float> body(area.getX() + 1.0f, area.getCentreY() - 1.0f, area.getWidth() - 2.0f, area.getHeight() * 0.54f); 
    g.drawRoundedRectangle(body, 2.0f, 1.5f); 
    
    juce::Path shackle; 
    float sw = body.getWidth() * 0.56f; 
    float sx = body.getCentreX() - sw * 0.5f;
    
    if (isLocked) {
        shackle.addCentredArc(body.getCentreX(), body.getY(), sw * 0.5f, sw * 0.55f, 0.0f, -juce::MathConstants<float>::pi, 0.0f, true); 
    }
    else {
        shackle.addCentredArc(sx + sw * 0.85f, body.getY() - 1.0f, sw * 0.5f, sw * 0.55f, 0.0f, -juce::MathConstants<float>::pi * 0.85f, 0.1f, true);
    }
    
    g.strokePath(shackle, juce::PathStrokeType(1.5f));
}

void MelodicAtlasAudioProcessorEditor::drawTrashVectorIcon(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour col) 
{
    g.setColour(col); 
    float x = area.getX(); float y = area.getY(); 
    float w = area.getWidth(); float h = area.getHeight();
    
    g.drawLine(x, y + 3.0f, x + w, y + 3.0f, 1.5f); 
    g.drawLine(x + w * 0.3f, y + 1.0f, x + w * 0.7f, y + 1.0f, 1.5f); 
    g.drawRoundedRectangle(x + 2.0f, y + 3.5f, w - 4.0f, h - 4.0f, 1.5f, 1.4f);
}

void MelodicAtlasAudioProcessorEditor::drawExportVectorIcon(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour col) 
{
    g.setColour(col); 
    float cx = area.getCentreX(); 
    float cy = area.getCentreY();
    
    g.drawLine(area.getX(), cy, area.getRight() - 2.0f, cy, 2.0f); 
    g.drawLine(area.getRight() - 6.0f, cy - 4.5f, area.getRight() - 1.0f, cy, 2.0f); 
    g.drawLine(area.getRight() - 6.0f, cy + 4.5f, area.getRight() - 1.0f, cy, 2.0f); 
    g.drawLine(cx - 4.0f, area.getY() + 2.0f, cx - 4.0f, area.getBottom() - 2.0f, 1.4f);
}

void MelodicAtlasAudioProcessorEditor::drawPlusVectorIcon(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour col) 
{
    g.setColour(col); 
    float cx = area.getCentreX(); 
    float cy = area.getCentreY(); 
    float len = std::min(area.getWidth(), area.getHeight()) * 0.35f;
    
    g.drawLine(cx, cy - len, cx, cy + len, 2.5f); 
    g.drawLine(cx - len, cy, cx + len, cy, 2.5f);
}

void MelodicAtlasAudioProcessorEditor::drawCloseVectorIcon(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour col) 
{
    g.setColour(col); 
    float r = area.getRight(); 
    float b = area.getBottom(); 
    float x = area.getX(); 
    float y = area.getY();
    
    g.drawLine(x, y, r, b, 2.2f); 
    g.drawLine(r, y, x, b, 2.2f);
}

void MelodicAtlasAudioProcessorEditor::mouseMove(const juce::MouseEvent& e) 
{ 
    updateHoverTooltip(e.position); 
}

void MelodicAtlasAudioProcessorEditor::updateHoverTooltip(juce::Point<float> pt) 
{
    if (btnLockPadsIcon.contains(pt)) 
        setTooltip(audioProcessor.padSettingsLocked.load() ? "Pad Lock ON" : "Pad Lock OFF"); 
    else 
        setTooltip({});
}

void MelodicAtlasAudioProcessorEditor::mouseDown(const juce::MouseEvent& e) 
{
    auto pt = e.position; 
    currentPadDragAction = PadDragAction::None; 
    activeDragPadIndex = -1; 
    draggingBpmValue = false;
    
    if (pt.y <= 42.0f) { handleTopHeaderClick(e); return; } 
    if (pt.y <= 92.0f) { handleFilterBarClick(e); return; }
    if (pt.y >= btnPadTriggerMode.getY() - 4.0f) { handleBankAndRightPanelClick(e); }
}

void MelodicAtlasAudioProcessorEditor::mouseDrag(const juce::MouseEvent& e) 
{
    if (draggingBpmValue) 
    {
        double delta = (double)(padDragStartPos.y - e.position.y) * 0.4; 
        audioProcessor.currentBpm.store(juce::jlimit(60.0, 200.0, std::round((bpmDragStartVal + delta) * 2.0) * 0.5)); 
        repaint(670, 0, 150, 40); 
        return;
    }
    
    if (currentPadDragAction != PadDragAction::None && activeDragPadIndex >= 0) 
    {
        auto& st = audioProcessor.getSamplerEngine().getPadState(activeDragPadIndex); 
        float dy = padDragStartPos.y - e.position.y;
        
        if (currentPadDragAction == PadDragAction::DialSemitones) st.semitoneOffset = juce::jlimit(-24, 24, (int)std::round(dragStartParamValue + dy * 0.25f));
        else if (currentPadDragAction == PadDragAction::DialCents) st.centOffset = juce::jlimit(-100, 100, (int)std::round(dragStartParamValue + dy * 1.5f));
        else if (currentPadDragAction == PadDragAction::DialVol) st.volumeDb = juce::jlimit(-24.0f, 6.0f, dragStartParamValue + dy * 0.25f);
        else if (currentPadDragAction == PadDragAction::DialPan) st.pan = juce::jlimit(-1.0f, 1.0f, dragStartParamValue + dy * 0.015f);
        else if (currentPadDragAction == PadDragAction::DialAttack) st.attackSec = juce::jlimit(0.0f, 2.0f, dragStartParamValue + dy * 0.01f);
        else if (currentPadDragAction == PadDragAction::DialDecay) st.decaySec = juce::jlimit(0.01f, 5.0f, dragStartParamValue + dy * 0.025f);
        else if (currentPadDragAction == PadDragAction::DialSustain) st.sustainLvl = juce::jlimit(0.0f, 1.0f, dragStartParamValue + dy * 0.005f);
        else if (currentPadDragAction == PadDragAction::DialRelease) st.releaseSec = juce::jlimit(0.005f, 5.0f, dragStartParamValue + dy * 0.025f);
        else if (currentPadDragAction == PadDragAction::DragPadToDaw && e.position.getDistanceFrom(padDragStartPos) >= 7.0f) 
        {
            currentPadDragAction = PadDragAction::None; 
            activeDragPadIndex = -1; 
            std::vector<int> padsToExport(selectedPads.begin(), selectedPads.end()); 
            exportPadsToDaw(padsToExport); 
            return;
        }
        repaint(); 
        return;
    }
    
    if (e.position.getDistanceFrom(padDragStartPos) >= 6.0f && btnDragAllIcon.contains(padDragStartPos)) 
    {
        std::vector<int> padsToExport; 
        for (int i = 0; i < 12; ++i) 
        {
            if (audioProcessor.getSamplerEngine().getPadState(i).sampleIndex >= 0) 
                padsToExport.push_back(i);
        }
        padDragStartPos = { -999.0f, -999.0f }; 
        exportPadsToDaw(padsToExport);
    }
}

void MelodicAtlasAudioProcessorEditor::mouseUp(const juce::MouseEvent&) 
{ 
    draggingBpmValue = false; 
    currentPadDragAction = PadDragAction::None; 
    activeDragPadIndex = -1; 
}

void MelodicAtlasAudioProcessorEditor::mouseDoubleClick(const juce::MouseEvent& e) 
{
    for (int i = 0; i < 12; ++i) 
    {
        if (padRects[(size_t)i].contains(e.position)) 
        {
            auto& st = audioProcessor.getSamplerEngine().getPadState(i);
            
            if (st.settingsViewOpen) 
            {
                const auto& r = padRects[(size_t)i]; 
                float relY = (e.position.y - (r.getY() + 23.0f)) / (r.getHeight() - 25.0f); 
                float relX = (e.position.x - r.getX()) / std::max(1.0f, r.getWidth());
                
                if (relY >= 0.0f && relY <= 1.0f) 
                {
                    if (relY < 0.5f) { 
                        if (relX < 0.25f) st.semitoneOffset = 0; 
                        else if (relX < 0.50f) st.centOffset = 0; 
                        else if (relX < 0.75f) st.volumeDb = -3.0f; 
                        else st.pan = 0.0f; 
                    }
                    else { 
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
    
    else if (btnBpmHalf.contains(pt)) { audioProcessor.currentBpm.store(audioProcessor.currentBpm.load() * 0.5); repaint(); }
    else if (btnBpmDouble.contains(pt)) { audioProcessor.currentBpm.store(std::min(400.0, audioProcessor.currentBpm.load() * 2.0)); repaint(); }
    else if (btnBpmSync.contains(pt)) {
        if (e.mods.isPopupMenu() || !audioProcessor.bpmLockedToHost.load()) { 
            draggingBpmValue = true; padDragStartPos = pt; bpmDragStartVal = audioProcessor.currentBpm.load(); 
            if (e.getNumberOfClicks() > 1) audioProcessor.bpmLockedToHost.store(true); 
        }
        else { 
            audioProcessor.bpmLockedToHost.store(false); 
        } 
        repaint();
    }
    else if (btnScanFolder.contains(pt)) openFolderChooser();
}

void MelodicAtlasAudioProcessorEditor::handleFilterBarClick(const juce::MouseEvent& e) 
{
    auto pt = e.position;
    if (groupFilterRects[0].contains(pt)) 
    { 
        selectedClusterFilter = -1; activeSubFilterMask = 0; 
        audioProcessor.savedClusterFilter = -1; audioProcessor.savedSubFilterMask = 0; 
        mapCanvas.setViewFilter(audioProcessor.showLoopsTab.load(), -1, 0); 
        repaint(); 
        return; 
    }
    
    for (int c = 0; c < 8; ++c) 
    {
        if (groupFilterRects[(size_t)(c + 1)].contains(pt)) 
        {
            if (groupDropdownRects[(size_t)c].contains(pt) || e.mods.isPopupMenu() || selectedClusterFilter == c) 
            {
                selectedClusterFilter = c; audioProcessor.savedClusterFilter = c; 
                mapCanvas.setViewFilter(audioProcessor.showLoopsTab.load(), c, activeSubFilterMask | selectedClusterSubgroupBit[(size_t)c]); 
                repaint(); 
                showClusterSubgroupPopupMenu(c, groupFilterRects[(size_t)(c + 1)]); 
                return;
            }
            selectedClusterFilter = c; audioProcessor.savedClusterFilter = c; 
            mapCanvas.setViewFilter(audioProcessor.showLoopsTab.load(), c, activeSubFilterMask | selectedClusterSubgroupBit[(size_t)c]); 
            repaint(); 
            return;
        }
    }
    
    if (btnOctDown.contains(pt)) { audioProcessor.octaveShift.store(juce::jlimit(-2, 2, audioProcessor.octaveShift.load() - 1)); repaint(); }
    else if (btnOctUp.contains(pt)) { audioProcessor.octaveShift.store(juce::jlimit(-2, 2, audioProcessor.octaveShift.load() + 1)); repaint(); }
}

void MelodicAtlasAudioProcessorEditor::showClusterSubgroupPopupMenu(int clusterIndex, juce::Rectangle<float> targetBtn) 
{
    if (clusterIndex < 0 || clusterIndex >= 8) return;
    auto cg = static_cast<Atlas::ClusterGroup>(clusterIndex); 
    auto options = Atlas::MusicMath::getSubgroupsForCluster(cg); 
    uint32_t currentBit = selectedClusterSubgroupBit[(size_t)clusterIndex];
    juce::PopupMenu menu; 
    
    for (size_t i = 0; i < options.size(); ++i) 
    {
        menu.addItem((int)i + 1, options[i].fullName, true, (options[i].flagBit == currentBit));
    }
    
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(localAreaToGlobal(targetBtn.toNearestInt())), [this, clusterIndex, options](int res) 
    {
        if (res >= 1 && res <= (int)options.size()) 
        { 
            selectedClusterSubgroupBit[(size_t)clusterIndex] = options[(size_t)(res - 1)].flagBit; 
            selectedClusterFilter = clusterIndex; 
            audioProcessor.savedClusterFilter = clusterIndex; 
            updateDynamicLayoutRects(); 
            mapCanvas.setViewFilter(audioProcessor.showLoopsTab.load(), selectedClusterFilter, activeSubFilterMask | selectedClusterSubgroupBit[(size_t)clusterIndex]); 
            repaint(); 
        }
    });
}

void MelodicAtlasAudioProcessorEditor::handleBankAndRightPanelClick(const juce::MouseEvent& e) 
{
    auto pt = e.position; 
    padDragStartPos = pt;
    
    if (btnPadTriggerMode.contains(pt)) { audioProcessor.bankModeIndex.store((int)Atlas::BankMode::PadTrigger); repaint(); return; }
    if (btnChromaticMode.contains(pt)) { audioProcessor.bankModeIndex.store((int)Atlas::BankMode::Chromatic); repaint(); return; }
    if (btnLockPadsIcon.contains(pt)) { audioProcessor.padSettingsLocked.store(!audioProcessor.padSettingsLocked.load()); repaint(); return; }
    if (btnGlobalExportModeIcon.contains(pt)) { audioProcessor.setGlobalExportMode(!audioProcessor.globalExportProcessed.load()); repaint(); return; }
    if (btnClearAllIcon.contains(pt)) { audioProcessor.clearAllPadsWithUndo(); repaint(); return; }
    
    if (btnDragAllIcon.contains(pt)) { 
        std::vector<int> padsToExport; 
        for (int i = 0; i < 12; ++i) {
            if (audioProcessor.getSamplerEngine().getPadState(i).sampleIndex >= 0) padsToExport.push_back(i); 
        }
        exportPadsToDaw(padsToExport); return; 
    }

    for (int i = 0; i < 12; ++i) 
    {
        const auto& r = padRects[(size_t)i]; 
        if (!r.contains(pt)) continue;
        
        auto& padState = audioProcessor.getSamplerEngine().getPadState(i);

        if (e.mods.isShiftDown() || e.mods.isCommandDown()) { selectedPads.insert(i); primarySelectedPad = i; } 
        else { selectedPads.clear(); selectedPads.insert(i); primarySelectedPad = i; }
        audioProcessor.primarySelectedPad = primarySelectedPad;

        if (padState.sampleIndex >= 0) 
        {
            juce::Rectangle<float> closeBtnZone(r.getRight() - 18.0f, r.getY() + 3.0f, 15.0f, 14.0f);
            if (closeBtnZone.contains(pt)) { 
                audioProcessor.pushGlobalUndoState(); 
                audioProcessor.getSamplerEngine().clearPad(i); 
                repaint(); return; 
            }
            
            juce::Rectangle<float> gearBtnZone(r.getRight() - 36.0f, r.getY() + 3.0f, 15.0f, 14.0f);
            if (gearBtnZone.contains(pt)) { padState.settingsViewOpen = !padState.settingsViewOpen; repaint(); return; }
        }

        if (padState.settingsViewOpen) 
        {
            audioProcessor.pushGlobalUndoState(); 
            activeDragPadIndex = i;
            float relY = (pt.y - (r.getY() + 23.0f)) / (r.getHeight() - 25.0f); 
            float relX = (pt.x - r.getX()) / std::max(1.0f, r.getWidth());
            
            if (relY < 0.5f) { 
                if (relX < 0.25f) { currentPadDragAction = PadDragAction::DialSemitones; dragStartParamValue = (float)padState.semitoneOffset; } 
                else if (relX < 0.50f) { currentPadDragAction = PadDragAction::DialCents; dragStartParamValue = (float)padState.centOffset; } 
                else if (relX < 0.75f) { currentPadDragAction = PadDragAction::DialVol; dragStartParamValue = padState.volumeDb; } 
                else { currentPadDragAction = PadDragAction::DialPan; dragStartParamValue = padState.pan; } 
            }
            else { 
                if (relX < 0.25f) { currentPadDragAction = PadDragAction::DialAttack; dragStartParamValue = padState.attackSec; } 
                else if (relX < 0.50f) { currentPadDragAction = PadDragAction::DialDecay; dragStartParamValue = padState.decaySec; } 
                else if (relX < 0.75f) { currentPadDragAction = PadDragAction::DialSustain; dragStartParamValue = padState.sustainLvl; } 
                else { currentPadDragAction = PadDragAction::DialRelease; dragStartParamValue = padState.releaseSec; } 
            }
            repaint(); return;
        }

        juce::Rectangle<float> plusBtnZone(r.getCentreX() - 15.0f, r.getCentreY() - 15.0f, 30.0f, 30.0f);
        if (padState.sampleIndex < 0) 
        {
            int activeMapIdx = mapCanvas.getActiveSampleIndex(); 
            const auto& db = audioProcessor.getSampleDatabase();
            if ((plusBtnZone.contains(pt) || e.mods.isPopupMenu()) && activeMapIdx >= 0 && activeMapIdx < (int)db.size()) 
            { 
                audioProcessor.assignSampleToPadWithPreviewSettings(i, activeMapIdx, true); 
                audioProcessor.getSamplerEngine().triggerPadFromUI(i); 
            }
            repaint(); return;
        }

        juce::Rectangle<float> undoBtnZone (r.getRight() - 54.0f, r.getY(), 16.0f, 19.0f);
        juce::Rectangle<float> pdBtnZone   (r.getRight() - 72.0f, r.getY(), 16.0f, 19.0f);
        juce::Rectangle<float> chromDotZone(r.getRight() - 88.0f, r.getY(), 15.0f, 19.0f);
        
        if (undoBtnZone.contains(pt)) { audioProcessor.pushGlobalUndoState(); audioProcessor.getSamplerEngine().undoPadSample(i); repaint(); return; }
        if (pdBtnZone.contains(pt)) { audioProcessor.pushGlobalUndoState(); padState.exportProcessed = !padState.exportProcessed; repaint(); return; }
        if (chromDotZone.contains(pt)) { audioProcessor.pushGlobalUndoState(); padState.chromaticActive = !padState.chromaticActive; repaint(); return; }

        float midY = r.getY() + 19.0f; 
        float midH = std::max(18.0f, r.getHeight() - 36.0f);
        juce::Rectangle<float> leftArrowZone(r.getX(), midY, 19.0f, midH); 
        juce::Rectangle<float> rightArrowZone(r.getRight() - 19.0f, midY, 19.0f, midH);
        
        if (leftArrowZone.contains(pt)) { audioProcessor.stepSimilarSampleInPad(i, -1); if (padState.sampleIndex >= 0) mapCanvas.selectSampleFromPad(padState.sampleIndex); repaint(); return; }
        if (rightArrowZone.contains(pt)) { audioProcessor.stepSimilarSampleInPad(i, 1); if (padState.sampleIndex >= 0) mapCanvas.selectSampleFromPad(padState.sampleIndex); repaint(); return; }

        activeDragPadIndex = i; 
        currentPadDragAction = PadDragAction::DragPadToDaw; 
        mapCanvas.selectSampleFromPad(padState.sampleIndex); 
        audioProcessor.getSamplerEngine().triggerPadFromUI(i); 
        repaint(); 
        return;
    }
}

void MelodicAtlasAudioProcessorEditor::showKeyPopupMenu() 
{ 
    juce::PopupMenu m; 
    int currentKey = audioProcessor.projectKeyRoot.load(); 
    for (int k = 0; k < 12; ++k) m.addItem(k + 1, Atlas::MusicMath::getNoteName(k), true, k == currentKey); 
    m.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(localAreaToGlobal(btnKeyRoot.toNearestInt())), [this](int res) { if (res >= 1 && res <= 12) { audioProcessor.projectKeyRoot.store(res - 1); repaint(); } }); 
}

void MelodicAtlasAudioProcessorEditor::showScalePopupMenu() 
{ 
    juce::PopupMenu m; 
    int currentScale = audioProcessor.projectScaleType.load(); 
    for (int s = 0; s < 10; ++s) m.addItem(s + 1, Atlas::MusicMath::getScaleFullName(static_cast<Atlas::ScaleType>(s)), true, s == currentScale); 
    m.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(localAreaToGlobal(btnScaleType.toNearestInt())), [this](int res) { if (res >= 1 && res <= 10) { audioProcessor.projectScaleType.store(res - 1); repaint(); } }); 
}

void MelodicAtlasAudioProcessorEditor::exportPadsToDaw(const std::vector<int>& padIndices) 
{ 
    juce::StringArray filesToDrag; 
    for (int idx : padIndices) 
    { 
        juce::File exportedFile = audioProcessor.getSamplerEngine().renderPadForExport(idx); 
        if (exportedFile.existsAsFile()) filesToDrag.addIfNotAlreadyThere(exportedFile.getFullPathName()); 
    } 
    if (!filesToDrag.isEmpty()) performExternalDragDropOfFiles(filesToDrag, false); 
}

void MelodicAtlasAudioProcessorEditor::openFolderChooser() 
{ 
    folderChooser = std::make_unique<juce::FileChooser>("Select Sample Library Folder", juce::File::getSpecialLocation(juce::File::userHomeDirectory)); 
    folderChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this](const juce::FileChooser& fc) { auto folder = fc.getResult(); if (folder.isDirectory()) audioProcessor.scanFolderAsync(folder); }); 
}

bool MelodicAtlasAudioProcessorEditor::isInterestedInFileDrag(const juce::StringArray& files) 
{ 
    for (const auto& f : files) if (juce::File(f).isDirectory()) return true; 
    return false; 
}

void MelodicAtlasAudioProcessorEditor::filesDropped(const juce::StringArray& files, int, int) 
{ 
    for (const auto& f : files) { juce::File dir(f); if (dir.isDirectory()) { audioProcessor.scanFolderAsync(dir); break; } } 
}

void MelodicAtlasAudioProcessorEditor::timerCallback() 
{ 
    uint64_t currentVer = audioProcessor.databaseVersion.load(); 
    if (currentVer != lastSeenDbVersion) { 
        lastSeenDbVersion = currentVer; 
        mapCanvas.setSampleDatabase(&audioProcessor.getSampleDatabase()); 
        repaint(); 
    } 
    uint32_t currentMask = audioProcessor.getSamplerEngine().getActivePlayingPadsMask(); 
    if (currentMask != lastPlayingMask) { 
        lastPlayingMask = currentMask; 
        repaint(0, (int)btnPadTriggerMode.getY() - 4, getWidth(), getHeight() - (int)btnPadTriggerMode.getY() + 4); 
    } 
    if (audioProcessor.isCurrentlyScanning()) {
        repaint((int)btnScanFolder.getX() - 4, 0, (int)btnScanFolder.getWidth() + 8, 40); 
    }
}
