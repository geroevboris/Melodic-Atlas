#include "PluginEditor.h"

MelodicAtlasAudioProcessorEditor::MelodicAtlasAudioProcessorEditor(
    MelodicAtlasAudioProcessor& p)
    : AudioProcessorEditor(&p),
      audioProcessor(p),
      mapCanvas(p.getSamplerEngine())
{
    setLookAndFeel(&atlasLookAndFeel);

    selectedClustersMask = audioProcessor.savedClustersMask;
    selectedClusterSubgroupBit = audioProcessor.savedClusterSubs;
    // Previous versions used different subcategory meanings. A removed
    // selection must not silently hide every sample in the new taxonomy.
    for (int c = 0; c < 7; ++c)
    {
        const auto options = Atlas::MusicMath::getSubgroupsForCluster(
            (Atlas::ClusterGroup)c);
        bool known = selectedClusterSubgroupBit[(size_t)c] == 0;
        for (const auto& option : options)
            known = known
                || option.flagBit == selectedClusterSubgroupBit[(size_t)c];
        if (!known)
            selectedClusterSubgroupBit[(size_t)c] = 0;
    }
    selectedMoodMask = audioProcessor.savedMoodMask;
    primarySelectedPad = audioProcessor.primarySelectedPad;

    selectedPads.clear();
    selectedPads.insert(primarySelectedPad);

    setResizable(true, true);
    setResizeLimits(920, 560, 1920, 1200);
    setSize(audioProcessor.savedEditorWidth, audioProcessor.savedEditorHeight);

    addAndMakeVisible(mapCanvas);
    mapCanvas.setSampleDatabase(audioProcessor.getSampleDatabaseSnapshot());
    applyCurrentFiltersToMap();

    // Right-click a real sound to correct automatic decisions or hide
    // it/the containing CLAP island. Source audio is never deleted.
    mapCanvas.onSampleRightClicked = [this](int sampleIdx)
    {
        const auto db = audioProcessor.getSampleDatabaseSnapshot();
        if (!db || sampleIdx < 0 || sampleIdx >= (int)db->size())
            return;
        const auto& selected = (*db)[(size_t)sampleIdx];
        const int islandId = selected.islandGroup;
        std::vector<int> islandIndices;
        if (islandId >= 0)
            for (int i = 0; i < (int)db->size(); ++i)
                if ((*db)[(size_t)i].islandGroup == islandId
                    && (*db)[(size_t)i].isLoop == selected.isLoop
                    && !(*db)[(size_t)i].hiddenFromAtlas)
                    islandIndices.push_back(i);

        juce::PopupMenu menu;
        menu.addItem(1, "Assign to selected pad");
        menu.addSeparator();
        menu.addItem(2, "Hide this sound from map");
        menu.addItem(3, "Hide this island (" +
            juce::String((int)islandIndices.size()) + " sounds)",
            !islandIndices.empty());
        menu.addSeparator();
        menu.addItem(4, "Move to One-shots", selected.isLoop);
        menu.addItem(5, "Move to Loops", !selected.isLoop);
        menu.showMenuAsync(
            juce::PopupMenu::Options().withTargetComponent(&mapCanvas),
            [this, sampleIdx, islandIndices](int choice)
            {
                if (choice == 1)
                {
                    if (selectedPads.empty())
                        selectedPads.insert(primarySelectedPad);
                    for (int pIdx : selectedPads)
                    {
                        audioProcessor.assignSampleToPadWithPreviewSettings(
                            pIdx, sampleIdx, true);
                        primarySelectedPad = pIdx;
                        audioProcessor.primarySelectedPad = pIdx;
                    }
                }
                else if (choice == 2)
                    audioProcessor.hideAtlasSamples({sampleIdx});
                else if (choice == 3)
                    audioProcessor.hideAtlasSamples(islandIndices);
                else if (choice == 4 || choice == 5)
                    audioProcessor.setSampleLoopOverride(
                        sampleIdx, choice == 5 ? 1 : 0);
                repaint();
            });
    };
    mapCanvas.onEmptyMapRightClicked = [this]()
    {
        const int hidden = audioProcessor.getHiddenAtlasSampleCount();
        juce::PopupMenu menu;
        menu.addItem(1, "Restore all hidden sounds (" +
            juce::String(hidden) + ")", hidden > 0);
        menu.showMenuAsync(
            juce::PopupMenu::Options().withTargetComponent(&mapCanvas),
            [this](int choice)
            {
                if (choice == 1)
                    audioProcessor.restoreHiddenAtlasSamples();
            });
    };

    auto setupEncoder = [this](juce::Slider& s, const juce::String& n,
                               const juce::String& tip, float minV, float maxV,
                               float defV, std::function<void(float)> cb)
    {
        s.setName(n);
        s.setTooltip(tip);
        s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        s.setRange(minV, maxV);
        s.setDoubleClickReturnValue(true, defV);
        s.setValue(defV, juce::dontSendNotification);
        s.onValueChange = [this, cb, &s]()
        {
            cb((float)s.getValue());
            s.repaint();
        };
        addAndMakeVisible(s);
    };

    setupEncoder(previewTuneSlider, "TUNE", "Preview Pitch (Semitones) - Double-click to reset",
                 -24.0f, 24.0f, 0.0f,
                  [this](float v) { audioProcessor.bankPitchSemitones.store(v); });
    previewTuneSlider.setRange(-24.0, 24.0, 1.0);
    previewTuneSlider.setValue(audioProcessor.bankPitchSemitones.load(),
                               juce::dontSendNotification);

    setupEncoder(mapReverbSlider, "REV", "Map Preview Reverb - Double-click to reset",
                 0.0f, 1.0f, 0.25f,
                 [this](float v) { audioProcessor.reverbAmount.store(v); });
    mapReverbSlider.setValue(audioProcessor.reverbAmount.load(),
                             juce::dontSendNotification);

    setupEncoder(bankReverbSlider, "REV", "Bank Pads Reverb - Double-click to reset",
                 0.0f, 1.0f, 0.30f,
                 [this](float v) { audioProcessor.bankReverbAmount.store(v); });
    bankReverbSlider.setValue(audioProcessor.bankReverbAmount.load(),
                              juce::dontSendNotification);

    setupEncoder(masterVolSlider, "MAIN", "Main Output Volume (dB) - Double-click to reset",
                 -24.0f, 6.0f, 0.0f,
                 [this](float v) { audioProcessor.masterVolumeDb.store(v); });
    masterVolSlider.setValue(audioProcessor.masterVolumeDb.load(),
                             juce::dontSendNotification);

    updateDynamicLayoutRects();
    lastSeenDbVersion = audioProcessor.databaseVersion.load();
    lastDisplayedBpm = audioProcessor.currentBpm.load();
    startTimerHz(30);
}

MelodicAtlasAudioProcessorEditor::~MelodicAtlasAudioProcessorEditor()
{
    audioProcessor.getSamplerEngine().releasePadFromUI(-1);
    setLookAndFeel(nullptr);
}

void MelodicAtlasAudioProcessorEditor::resized()
{
    audioProcessor.savedEditorWidth = getWidth();
    audioProcessor.savedEditorHeight = getHeight();
    updateDynamicLayoutRects();
}

void MelodicAtlasAudioProcessorEditor::applyCurrentFiltersToMap()
{
    audioProcessor.savedClustersMask = selectedClustersMask;
    audioProcessor.savedClusterSubs = selectedClusterSubgroupBit;
    audioProcessor.savedMoodMask = selectedMoodMask;

    mapCanvas.setViewFilter(audioProcessor.showLoopsTab.load(),
                            selectedClustersMask,
                            selectedClusterSubgroupBit,
                            selectedMoodMask);
}

void MelodicAtlasAudioProcessorEditor::updateDynamicLayoutRects()
{
    const float w = (float)getWidth();
    const float h = (float)getHeight();
    const float filterY = 46.0f;
    const float filterBarH = 48.0f;
    const float bankSectionH = std::max(200.0f, h * 0.32f);
    const float bankTopY = h - bankSectionH - 10.0f;

    const float mapY = filterY + filterBarH + 6.0f;
    const float mapH = std::max(180.0f, bankTopY - mapY - 8.0f);
    mapCanvas.setBounds(14, (int)mapY, (int)w - 28, (int)mapH);

    // Адаптивная раскладка верхней шапки (исключает наезд кнопок при ресайзе)
    const float extraW = std::max(0.0f, w - 920.0f);
    const float gapScale = std::min(1.0f, extraW / 200.0f);
    const float groupGap = 8.0f + 12.0f * gapScale;

    float hx = 148.0f;
    btnGlobalUndo = { hx, 8.0f, 24.0f, 24.0f }; hx += 27.0f;
    btnGlobalRedo = { hx, 8.0f, 24.0f, 24.0f }; hx += 24.0f + groupGap * 0.5f;

    btnOneShots = { hx, 8.0f, 76.0f, 24.0f }; hx += 80.0f;
    btnLoops    = { hx, 8.0f, 54.0f, 24.0f }; hx += 54.0f + groupGap;

    // Retired auto key/scale and map BPM controls.
    lblKeyRect = btnKeyRoot = btnScaleType = btnPitchMode = btnTransposeQuality = {};
    lblBpmRect = btnBpmHalf = btnBpmSync = btnBpmDouble = {};

    // Persistent library and explicit Refresh are separate operations.
    const float refreshW = 75.0f;
    float scanW = juce::jlimit(126.0f, 164.0f, w - hx - refreshW - 26.0f);
    float scanX = std::max(hx + 8.0f, w - scanW - refreshW - 20.0f);
    btnScanFolder = { scanX, 8.0f, scanW, 24.0f };
    btnRefreshLibrary = { scanX + scanW + 6.0f, 8.0f, refreshW, 24.0f };

    // Кнопки фильтров кластеров и MOODS
    const juce::Font filterFont(
        juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::bold);
    float gx = 22.0f;
    float row1Y = filterY + 12.0f;

    float allW = filterFont.getStringWidthFloat("ALL") + 18.0f;
    groupFilterRects[0] = { gx, row1Y, allW, 24.0f };
    gx += allW + 6.0f;

    // Discover belongs after the instrument and character filters.
    for (int c = 0; c < 8; ++c)
    {
        auto cg = static_cast<Atlas::ClusterGroup>(c);
        juce::String baseLabel = c == 7 ? "CHAR" : Atlas::MusicMath::getClusterName(cg);

        if (c < 7)
        {
            uint32_t subBit = selectedClusterSubgroupBit[(size_t)c];
            if (subBit != 0)
            {
                juce::String subShort = Atlas::MusicMath::getSubgroupShortLabel(cg, subBit);
                if (subShort.isNotEmpty())
                    baseLabel += ":" + subShort;
            }
        }
        else if (selectedMoodMask != 0)
        {
            juce::String mShort = Atlas::MusicMath::getSubgroupShortLabel(
                Atlas::ClusterGroup::Other, selectedMoodMask);
            baseLabel += mShort.isNotEmpty() ? (":" + mShort) : ":ON";
        }

        // Character traits are an independent AND-filter, not an
        // eighth instrument category. A visual gap separates the two.
        if (c == 7)
            gx += 12.0f;
        float textW = filterFont.getStringWidthFloat(baseLabel);
        float btnW = textW + 28.0f;
        groupFilterRects[(size_t)(c + 1)] = { gx, row1Y, btnW, 24.0f };
        groupDropdownRects[(size_t)c]     = { gx + btnW - 18.0f, row1Y, 18.0f, 24.0f };
        gx += btnW + 6.0f;
    }
    // Keep Discover next to the last filter, never on the left by ALL.
    btnDiscover = { gx + 4.0f, row1Y, 59.0f, 24.0f };

    // One global preview Tune knob; no separate octave.
    btnOctUp = btnOctDown = octValueRect = octLabelRect = {};
    mapReverbSlider.setBounds((int)(w - 84.0f), (int)(filterY + 3.0f), 42, 42);
    previewTuneSlider.setBounds((int)(w - 132.0f), (int)(filterY + 3.0f), 42, 42);

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
        padRects[(size_t)i] = {
            14.0f + (float)col * (padW + gapX),
            gridTopY + (float)row * (padH + gapY),
            padW,
            padH
        };
    }

    float rpX = 14.0f + gridAvailW + 10.0f;
    rightBankPanelRect = { rpX, gridTopY, rightPanelW, gridAvailH };

    int knobSize = (int)std::min(56.0f, gridAvailH * 0.44f);
    int knobY = (int)(rightBankPanelRect.getY() + 10.0f);
    bankReverbSlider.setBounds(
        (int)(rpX + rightPanelW * 0.27f) - knobSize / 2, knobY, knobSize, knobSize);
    masterVolSlider.setBounds(
        (int)(rpX + rightPanelW * 0.73f) - knobSize / 2, knobY, knobSize, knobSize);

    // 3 кнопки в нижней части правой панели (Lock, Clear All, Export All — без P/D)
    float iconRowY = rightBankPanelRect.getBottom() - 40.0f;
    float iconGap = 8.0f;
    float iconW = (rightPanelW - 20.0f - iconGap * 2.0f) / 3.0f;
    float ix = rpX + 10.0f;

    btnLockPadsIcon = { ix, iconRowY, iconW, 30.0f };
    btnClearAllIcon = { ix + iconW + iconGap, iconRowY, iconW, 30.0f };
    btnDragAllIcon  = { ix + (iconW + iconGap) * 2.0f, iconRowY, iconW, 30.0f };
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
    const juce::Font monoBold(
        juce::Font::getDefaultMonospacedFontName(), 12.5f, juce::Font::bold);
    const juce::Font monoReg(
        juce::Font::getDefaultMonospacedFontName(), 10.5f, juce::Font::plain);

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
    g.drawText("MELODIC-ATLAS", 38, 0, 108, 40, juce::Justification::centredLeft, false);

    auto drawBtn = [&](juce::Rectangle<float> r, const juce::String& t,
                       bool act, juce::Colour aCol = juce::Colour(0xff283f35))
    {
        g.setColour(act ? aCol : juce::Colour(0xff1d232d));
        g.fillRoundedRectangle(r, 3.0f);
        g.setColour(act ? juce::Colour(0xff7ec8a3) : juce::Colour(0xff343e4c));
        g.drawRoundedRectangle(r, 3.0f, 1.0f);
        g.setColour(act ? juce::Colours::white : juce::Colour(0xff9aa6b5));
        g.setFont(monoReg);
        g.drawText(t, r, juce::Justification::centred, false);
    };

    g.setColour(juce::Colour(0xff1d232d));
    g.fillRoundedRectangle(btnGlobalUndo, 3.0f);
    g.fillRoundedRectangle(btnGlobalRedo, 3.0f);
    g.setColour(audioProcessor.canGlobalUndo()
                ? juce::Colour(0xff465466) : juce::Colour(0xff28303c));
    g.drawRoundedRectangle(btnGlobalUndo, 3.0f, 1.0f);
    drawUndoVectorIcon(g, btnGlobalUndo.reduced(5.5f, 5.0f),
                       audioProcessor.canGlobalUndo()
                       ? juce::Colour(0xffdce6f2) : juce::Colour(0xff4d5866), false);

    g.setColour(audioProcessor.canGlobalRedo()
                ? juce::Colour(0xff465466) : juce::Colour(0xff28303c));
    g.drawRoundedRectangle(btnGlobalRedo, 3.0f, 1.0f);
    drawUndoVectorIcon(g, btnGlobalRedo.reduced(5.5f, 5.0f),
                       audioProcessor.canGlobalRedo()
                       ? juce::Colour(0xffdce6f2) : juce::Colour(0xff4d5866), true);

    drawBtn(btnOneShots, "ONE-SHOTS", !audioProcessor.showLoopsTab.load());
    drawBtn(btnLoops,    "LOOPS",     audioProcessor.showLoopsTab.load());

    juce::String scanTxt = audioProcessor.isCurrentlyScanning()
        ? ("SCAN " + juce::String((int)(audioProcessor.getScanProgress() * 100.0f)) + "%")
        : (btnScanFolder.getWidth() < 142.0f ? "SCAN FOLDER" : "SCAN SAMPLE FOLDER");
    drawBtn(btnScanFolder, scanTxt,
            audioProcessor.isCurrentlyScanning(), juce::Colour(0xff354839));
    drawBtn(btnRefreshLibrary, "REFRESH",
            audioProcessor.isCurrentlyScanning(), juce::Colour(0xff334053));
}

void MelodicAtlasAudioProcessorEditor::drawFilterBar(juce::Graphics& g)
{
    const juce::Font monoBold(
        juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::bold);

    g.setColour(juce::Colour(0xff161b22));
    g.fillRect(14, 46, getWidth() - 28, 48);
    g.setColour(juce::Colour(0xff252d3a));
    g.drawRect(14, 46, getWidth() - 28, 48, 1);

    bool allOn = (selectedClustersMask == 0 && selectedMoodMask == 0);
    g.setColour(allOn ? juce::Colour(0xff284035) : juce::Colour(0xff1d232c));
    g.fillRoundedRectangle(groupFilterRects[0], 3.0f);
    g.setColour(allOn ? juce::Colour(0xff7ec8a3) : juce::Colour(0xff323b48));
    g.drawRoundedRectangle(groupFilterRects[0], 3.0f, 1.0f);
    g.setColour(allOn ? juce::Colours::white : juce::Colour(0xff9aa6b5));
    g.setFont(monoBold);
    g.drawText("ALL", groupFilterRects[0], juce::Justification::centred, false);

    // A small die-shaped Discover control alongside the filter choices.
    g.setColour(juce::Colour(0xff28313f));
    g.fillRoundedRectangle(btnDiscover, 4.0f);
    g.setColour(juce::Colour(0xff91b6cd));
    g.drawRoundedRectangle(btnDiscover, 4.0f, 1.0f);
    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(),
                        9.5f, juce::Font::bold));
    g.drawText("DISCOVER", btnDiscover, juce::Justification::centred, false);

    for (int c = 0; c < 8; ++c)
    {
        auto cg = static_cast<Atlas::ClusterGroup>(c);
        const auto& btnR = groupFilterRects[(size_t)(c + 1)];
        const auto& dropR = groupDropdownRects[(size_t)c];

        bool on = (c < 7)
            ? ((selectedClustersMask & (1u << c)) != 0)
            : (selectedMoodMask != 0);

        juce::String label = c == 7 ? "CHAR" : Atlas::MusicMath::getClusterName(cg);
        if (c < 7)
        {
            uint32_t subBit = selectedClusterSubgroupBit[(size_t)c];
            if (subBit != 0)
            {
                juce::String subShort = Atlas::MusicMath::getSubgroupShortLabel(cg, subBit);
                if (subShort.isNotEmpty())
                    label += ":" + subShort;
            }
        }
        else if (selectedMoodMask != 0)
        {
            int count = 0;
            for (uint32_t bits = selectedMoodMask; bits != 0; bits &= (bits - 1))
                ++count;
            juce::String mShort = count == 1
                ? Atlas::MusicMath::getSubgroupShortLabel(
                    Atlas::ClusterGroup::Other, selectedMoodMask)
                : juce::String(count);
            label += ":" + mShort;
        }

        // Instrument filter highlights stay neutral. Their colours must
        // not imply an instrument owns one of the CLAP map colour families.
        juce::Colour col = juce::Colour(0xff92a6cd);

        g.setColour(on ? col.withAlpha(0.32f) : juce::Colour(0xff1d232c));
        g.fillRoundedRectangle(btnR, 3.0f);

        g.setColour(on ? col.brighter(0.30f) : juce::Colour(0xff323b48));
        g.drawRoundedRectangle(btnR, 3.0f, 1.2f);

        g.setColour(on ? col.withAlpha(0.55f) : juce::Colour(0xff2a323e));
        g.drawVerticalLine((int)dropR.getX(), btnR.getY() + 4.0f, btnR.getBottom() - 4.0f);

        g.setColour(on ? juce::Colours::white : juce::Colour(0xffa4b0c0));
        g.drawText(label, btnR.withTrimmedRight(16.0f), juce::Justification::centred, false);

        drawDropdownTriangle(g, dropR.reduced(5.0f, 9.0f),
                             on ? juce::Colours::white : juce::Colour(0xff8a96a6));
    }

    // No separate octave control.
}

void MelodicAtlasAudioProcessorEditor::drawBankAndRightPanel(juce::Graphics& g)
{
    const juce::Font monoBold(
        juce::Font::getDefaultMonospacedFontName(), 11.5f, juce::Font::bold);
    const juce::Font monoReg(
        juce::Font::getDefaultMonospacedFontName(), 10.5f, juce::Font::plain);

    g.setColour(juce::Colour(0xffd0dae5));
    g.setFont(monoBold);
    g.drawText("BANK:", 14, (int)btnPadTriggerMode.getY(), 50, 20,
               juce::Justification::centredLeft, false);

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
        drawPadSlot(g, i, padRects[(size_t)i], ((playingMask & (1u << i)) != 0));

    g.setColour(juce::Colour(0xff161b22));
    g.fillRoundedRectangle(rightBankPanelRect, 4.0f);
    g.setColour(juce::Colour(0xff28313e));
    g.drawRoundedRectangle(rightBankPanelRect, 4.0f, 1.0f);

    bool locked = audioProcessor.padSettingsLocked.load();
    g.setColour(locked ? juce::Colour(0xff284035) : juce::Colour(0xff1e242d));
    g.fillRoundedRectangle(btnLockPadsIcon, 3.5f);
    g.setColour(locked ? juce::Colour(0xff7ec8a3) : juce::Colour(0xff36404e));
    g.drawRoundedRectangle(btnLockPadsIcon, 3.5f, 1.0f);
    drawLockVectorIcon(g, btnLockPadsIcon.reduced(10.0f, 6.0f), locked,
                       locked ? juce::Colour(0xff7ec8a3) : juce::Colour(0xff9aa6b5));

    g.setColour(juce::Colour(0xff1e242d));
    g.fillRoundedRectangle(btnClearAllIcon, 3.5f);
    g.setColour(juce::Colour(0xff36404e));
    g.drawRoundedRectangle(btnClearAllIcon, 3.5f, 1.0f);
    drawTrashVectorIcon(g, btnClearAllIcon.reduced(11.0f, 6.5f), juce::Colour(0xffb5c0ce));

    g.setColour(juce::Colour(0xff6da37b));
    g.fillRoundedRectangle(btnDragAllIcon, 3.5f);
    g.setColour(juce::Colour(0xff96d1a4));
    g.drawRoundedRectangle(btnDragAllIcon, 3.5f, 1.0f);
    drawExportVectorIcon(g, btnDragAllIcon.reduced(10.0f, 6.5f), juce::Colour(0xff0d1510));
}

void MelodicAtlasAudioProcessorEditor::drawPadSlot(juce::Graphics& g, int padIdx,
                                                   const juce::Rectangle<float>& r,
                                                   bool isPlayingNow)
{
    const auto& st = audioProcessor.getSamplerEngine().getPadState(padIdx);
    const auto* rec = audioProcessor.getSamplerEngine().getPadRecord(padIdx);
    auto audio = audioProcessor.getSamplerEngine().getPadAudio(padIdx);

    bool isSel = selectedPads.count(padIdx) > 0;
    bool isDrop = (hoveredDropPadIndex == padIdx);
    juce::Colour bgCol = (rec != nullptr)
                       ? juce::Colour(0xff1e242d) : juce::Colour(0xff181d25);

    if (st.fileMissing && rec != nullptr)
        bgCol = juce::Colour(0xff291f1b);
    else if (isPlayingNow)
        bgCol = juce::Colour(0xff233a32);
    else if (isDrop)
        bgCol = juce::Colour(0xff273731);

    g.setColour(bgCol);
    g.fillRoundedRectangle(r, 3.5f);

    if (st.fileMissing && rec != nullptr)
    {
        g.setColour(juce::Colour(0xfff07838));
        g.drawRoundedRectangle(r, 3.5f, isSel ? 1.8f : 1.2f);
    }
    else if (isPlayingNow)
    {
        g.setColour(juce::Colour(0xff62f5b5));
        g.drawRoundedRectangle(r, 3.5f, 2.0f);
    }
    else if (isSel || isDrop)
    {
        g.setColour(juce::Colour(0xffe6c167));
        g.drawRoundedRectangle(r, 3.5f, 1.6f);
    }
    else
    {
        g.setColour(juce::Colour(0xff2c3542));
        g.drawRoundedRectangle(r, 3.5f, 1.0f);
    }

    if (st.settingsViewOpen && rec != nullptr)
        drawPadGearSettingsView(g, padIdx, r, st, audio);
    else if (st.syncViewOpen && rec != nullptr)
        drawPadSyncView(g, padIdx, r, st);
    else
        drawPadNormalView(g, padIdx, r, st, rec, audio, isPlayingNow);
}

void MelodicAtlasAudioProcessorEditor::drawPadNormalView(
    juce::Graphics& g, int padIdx, const juce::Rectangle<float>& r,
    const Atlas::PadSlotState& st, const Atlas::SampleRecord* rec,
    std::shared_ptr<Atlas::CachedAudioData> audio, bool isPlayingNow)
{
    juce::ignoreUnused(padIdx);

    if (rec == nullptr)
    {
        drawPlusVectorIcon(
            g,
            juce::Rectangle<float>(r.getCentreX() - 15.0f, r.getCentreY() - 15.0f, 30.0f, 30.0f),
            juce::Colour(0xff4a5462));
        g.setColour(juce::Colour(0xff566272));
        g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 9.5f, juce::Font::bold));
        g.drawText("ADD", r.withTrimmedTop(r.getHeight() * 0.7f),
                   juce::Justification::centred, false);
        return;
    }

    const juce::Font monoNote(
        juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::bold);
    const juce::Font monoName(
        juce::Font::getDefaultMonospacedFontName(), 10.5f, juce::Font::plain);

    g.setColour(isPlayingNow ? juce::Colour(0xff62f5b5) : juce::Colour(0xffe2eaf4));
    g.setFont(monoNote);
    g.drawText(Atlas::MusicMath::formatPadNoteAndCents(
                   rec->pitchClass, st.semitoneOffset, st.centOffset),
               (int)r.getX() + 6, (int)r.getY() + 3, 60, 15,
               juce::Justification::centredLeft, false);

    // Верхний ряд кнопок пэда: Chromatic Dot, Playback Mode (→/↻/←), Sync (S), Gear, Close (X)
    juce::Rectangle<float> closeBtnZone   (r.getRight() - 18.0f, r.getY() + 3.0f, 15.0f, 14.0f);
    juce::Rectangle<float> gearBtnZone    (r.getRight() - 35.0f, r.getY() + 3.0f, 15.0f, 14.0f);
    juce::Rectangle<float> syncBtnZone    (r.getRight() - 52.0f, r.getY() + 3.0f, 15.0f, 14.0f);
    juce::Rectangle<float> playModeBtnZone(r.getRight() - 70.0f, r.getY() + 3.0f, 16.0f, 14.0f);
    juce::Rectangle<float> chromDotZone   (r.getRight() - 86.0f, r.getY() + 3.0f, 14.0f, 14.0f);

    drawCloseVectorIcon(g, closeBtnZone.reduced(2.5f), juce::Colour(0xff8490a0));
    drawGearVectorIcon(g, gearBtnZone.reduced(2.0f), juce::Colour(0xff9aa8b8));

    // Кнопка S (открывает настройки BPM-синхронизации и тона пэда)
    bool syncHighlighted = st.padSyncBpm || st.syncViewOpen;
    g.setColour(syncHighlighted ? juce::Colour(0xff263f34) : juce::Colour(0xff222a35));
    g.fillRoundedRectangle(syncBtnZone, 2.5f);
    g.setColour(syncHighlighted ? juce::Colour(0xff7ec8a3) : juce::Colour(0xff4a586b));
    g.drawRoundedRectangle(syncBtnZone, 2.5f, 1.0f);
    g.setColour(syncHighlighted ? juce::Colours::white : juce::Colour(0xff9aa6b5));
    g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 9.5f, juce::Font::bold));
    g.drawText("S", syncBtnZone, juce::Justification::centred, false);

    // Кнопка режима воспроизведения (One-Shot → / Loop ↻ / Reverse ←)
    bool modeActive = (st.playbackMode != Atlas::PadPlaybackMode::OneShot);
    g.setColour(modeActive ? juce::Colour(0xff263f34) : juce::Colour(0xff222a35));
    g.fillRoundedRectangle(playModeBtnZone, 2.5f);
    g.setColour(modeActive ? juce::Colour(0xff7ec8a3) : juce::Colour(0xff4a586b));
    g.drawRoundedRectangle(playModeBtnZone, 2.5f, 1.0f);
    drawPlaybackModeIcon(g, playModeBtnZone.reduced(2.5f, 2.5f), st.playbackMode,
                         modeActive ? juce::Colours::white : juce::Colour(0xffb8c5d6));

    if (st.chromaticActive)
    {
        g.setColour(juce::Colour(0xffffcf48));
        g.fillEllipse(chromDotZone.reduced(3.2f));
    }
    else
    {
        g.setColour(juce::Colour(0xff525e6e));
        g.drawEllipse(chromDotZone.reduced(3.2f), 1.3f);
    }

    float midY = r.getY() + 19.0f;
    float midH = std::max(18.0f, r.getHeight() - 36.0f);
    juce::Rectangle<float> leftUndoZone  (r.getX() + 2.0f, midY, 17.0f, midH);
    juce::Rectangle<float> rightArrowZone(r.getRight() - 18.0f, midY, 15.0f, midH);
    juce::Rectangle<float> waveRect      (r.getX() + 21.0f, midY + 2.0f,
                                          r.getWidth() - 41.0f, midH - 4.0f);

    // На месте левой стрелки — локальный многоуровневый Undo пэда
    bool canUndoPad = !st.history.empty();
    drawUndoVectorIcon(g, leftUndoZone.withSizeKeepingCentre(12.0f, 12.0f),
                       canUndoPad ? juce::Colour(0xffdce6f2) : juce::Colour(0xff444e5c),
                       false);

    // Правая стрелка — следующий похожий сэмпл
    drawArrowVectorIcon(g, rightArrowZone.withSizeKeepingCentre(9.0f, 11.0f),
                        true, juce::Colour(0xff8e9db0));

    // Если внешний SSD отключен и файл не найден — рисуем рыжий восклицательный знак (!)
    if (st.fileMissing || audio == nullptr)
    {
        juce::Rectangle<float> warnIconRect(
            waveRect.getCentreX() - 9.0f, waveRect.getY() + 1.0f, 18.0f, 16.0f);
        drawWarningVectorIcon(g, warnIconRect, juce::Colour(0xffff8533));

        g.setColour(juce::Colour(0xffff9b54));
        g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 8.5f, juce::Font::bold));
        g.drawText("MISSING FILE",
                   juce::Rectangle<float>(waveRect.getX(), warnIconRect.getBottom() + 1.0f,
                                          waveRect.getWidth(), 11.0f),
                   juce::Justification::centred, false);
    }
    else
    {
        juce::Colour waveCol = Atlas::MusicMath::getClapFamilyColour(*rec);
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
                bool inRegion = (normPos >= st.regionStart && normPos <= st.regionEnd);
                g.setColour(inRegion
                            ? (isPlayingNow ? waveCol.brighter(0.45f) : waveCol.withAlpha(0.88f))
                            : waveCol.withAlpha(0.22f));
                float barH = std::max(1.5f, peaks[b * step] * waveRect.getHeight() * 0.92f);
                g.fillRect(waveRect.getX() + (float)b * barW,
                           centerY - barH * 0.5f,
                           std::max(1.0f, barW - 0.5f),
                           barH);
            }
        }
    }

    g.setColour(st.fileMissing ? juce::Colour(0xffb58d76) : juce::Colour(0xffdce5f0));
    g.setFont(monoName);
    g.drawText(rec->name.substring(0, 16),
               (int)r.getX() + 6, (int)r.getBottom() - 16,
               (int)r.getWidth() - 12, 14,
               juce::Justification::centred, true);
}

void MelodicAtlasAudioProcessorEditor::drawPadSyncView(
    juce::Graphics& g, int padIdx, const juce::Rectangle<float>& r,
    const Atlas::PadSlotState& st)
{
    const auto* rec = audioProcessor.getSamplerEngine().getPadRecord(padIdx);
    const juce::Font monoBold(
        juce::Font::getDefaultMonospacedFontName(), 9.5f, juce::Font::bold);

    if (rec != nullptr)
    {
        g.setColour(juce::Colour(0xffe2eaf4));
        g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 10.5f, juce::Font::bold));
        g.drawText(Atlas::MusicMath::formatPadNoteAndCents(
                       rec->pitchClass, st.semitoneOffset, st.centOffset),
                   (int)r.getX() + 6, (int)r.getY() + 3, 56, 15,
                   juce::Justification::centredLeft, false);
    }

    juce::Rectangle<float> closeBtnZone(r.getRight() - 18.0f, r.getY() + 3.0f, 15.0f, 14.0f);
    juce::Rectangle<float> gearBtnZone (r.getRight() - 35.0f, r.getY() + 3.0f, 15.0f, 14.0f);
    juce::Rectangle<float> syncBtnZone (r.getRight() - 52.0f, r.getY() + 3.0f, 15.0f, 14.0f);

    drawCloseVectorIcon(g, closeBtnZone.reduced(2.5f), juce::Colour(0xff8490a0));
    drawGearVectorIcon(g, gearBtnZone.reduced(2.0f), juce::Colour(0xff9aa8b8));

    g.setColour(juce::Colour(0xffe6c167));
    g.fillRoundedRectangle(syncBtnZone, 2.5f);
    g.setColour(juce::Colour(0xff14181f));
    g.setFont(monoBold);
    g.drawText("S", syncBtnZone, juce::Justification::centred, false);

    float contentTop = r.getY() + 22.0f;
    float contentH = r.getBottom() - contentTop - 5.0f;
    float rowH = (contentH - 4.0f) * 0.5f;
    float colW = (r.getWidth() - 16.0f - 8.0f) / 3.0f;
    float startX = r.getX() + 8.0f;

    auto drawMiniChoiceBtn = [&](juce::Rectangle<float> br,
                                 const juce::String& txt, bool on)
    {
        g.setColour(on ? juce::Colour(0xff284035) : juce::Colour(0xff151a22));
        g.fillRoundedRectangle(br, 2.5f);
        g.setColour(on ? juce::Colour(0xff7ec8a3) : juce::Colour(0xff343e4c));
        g.drawRoundedRectangle(br, 2.5f, 1.0f);
        g.setColour(on ? juce::Colours::white : juce::Colour(0xff9aa6b5));
        g.setFont(monoBold);
        g.drawText(txt, br, juce::Justification::centred, false);
    };

    // Tempo controls only; no SNAP / ROOT pitch auto-correction.
    float y1 = contentTop;
    drawMiniChoiceBtn({ startX, y1, colW, rowH }, "BPM", st.padSyncBpm);
    drawMiniChoiceBtn({ startX + colW + 4.0f, y1, colW, rowH },
                      "/2", std::abs(st.padSpeedMult - 0.5f) < 0.05f);
    drawMiniChoiceBtn({ startX + (colW + 4.0f) * 2.0f, y1, colW, rowH },
                      "x2", std::abs(st.padSpeedMult - 2.0f) < 0.05f);
}

void MelodicAtlasAudioProcessorEditor::drawPadGearSettingsView(
    juce::Graphics& g, int padIdx, const juce::Rectangle<float>& r,
    const Atlas::PadSlotState& st, std::shared_ptr<Atlas::CachedAudioData> audio)
{
    juce::ignoreUnused(padIdx);

    juce::Rectangle<float> closeBtnZone(r.getRight() - 18.0f, r.getY() + 3.0f, 15.0f, 14.0f);
    juce::Rectangle<float> gearBtnZone (r.getRight() - 35.0f, r.getY() + 3.0f, 15.0f, 14.0f);
    juce::Rectangle<float> syncBtnZone (r.getRight() - 52.0f, r.getY() + 3.0f, 15.0f, 14.0f);

    drawCloseVectorIcon(g, closeBtnZone.reduced(2.5f), juce::Colour(0xff8490a0));
    drawGearVectorIcon(g, gearBtnZone.reduced(2.0f), juce::Colour(0xffe6c167));

    g.setColour(st.padSyncBpm ? juce::Colour(0xff263f34) : juce::Colour(0xff222a35));
    g.fillRoundedRectangle(syncBtnZone, 2.5f);
    g.setColour(st.padSyncBpm ? juce::Colour(0xff7ec8a3) : juce::Colour(0xff4a586b));
    g.drawRoundedRectangle(syncBtnZone, 2.5f, 1.0f);
    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 9.5f, juce::Font::bold));
    g.drawText("S", syncBtnZone, juce::Justification::centred, false);

    juce::Rectangle<float> regionWaveBox(r.getX() + 6.0f, r.getY() + 4.0f,
                                         r.getWidth() - 62.0f, 15.0f);
    g.setColour(juce::Colour(0xff13171e));
    g.fillRoundedRectangle(regionWaveBox, 2.0f);

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
            bool inside = (norm >= st.regionStart && norm <= st.regionEnd);
            g.setColour(inside ? juce::Colour(0xff7ec8a3) : juce::Colour(0xff384352));
            float barH = std::max(1.5f, peaks[b * step] * regionWaveBox.getHeight() * 0.85f);
            g.fillRect(regionWaveBox.getX() + (float)b * barW,
                       cy - barH * 0.5f,
                       std::max(1.0f, barW - 0.4f),
                       barH);
        }
    }

    float startX = regionWaveBox.getX() + st.regionStart * regionWaveBox.getWidth();
    float endX   = regionWaveBox.getX() + st.regionEnd   * regionWaveBox.getWidth();
    g.setColour(juce::Colour(0xffe6c167));
    g.drawLine(startX, regionWaveBox.getY(), startX, regionWaveBox.getBottom(), 2.0f);
    g.drawLine(endX,   regionWaveBox.getY(), endX,   regionWaveBox.getBottom(), 2.0f);

    float dialsTopY = r.getY() + 22.0f;
    float rowH = (r.getBottom() - dialsTopY - 2.0f) / 2.0f;
    float dialSize = std::min(22.0f, rowH - 6.0f);
    float colW = (r.getWidth() - 8.0f) / 4.0f;

    juce::String panStr = (std::abs(st.pan) < 0.05f)
        ? "C"
        : ((st.pan < 0.0f)
           ? ("L" + juce::String((int)std::round(-st.pan * 50.0f)))
           : ("R" + juce::String((int)std::round(st.pan * 50.0f))));

    drawMiniEncoder(
        g, { r.getX() + 4.0f + colW * 0.0f + (colW - dialSize) * 0.5f, dialsTopY, dialSize, dialSize },
        (st.semitoneOffset + 24.0f) / 48.0f, "TUNE",
        (st.semitoneOffset > 0 ? "+" : "") + juce::String(st.semitoneOffset), true);

    drawMiniEncoder(
        g, { r.getX() + 4.0f + colW * 1.0f + (colW - dialSize) * 0.5f, dialsTopY, dialSize, dialSize },
        (st.centOffset + 100.0f) / 200.0f, "CENT",
        (st.centOffset > 0 ? "+" : "") + juce::String(st.centOffset), true);

    drawMiniEncoder(
        g, { r.getX() + 4.0f + colW * 2.0f + (colW - dialSize) * 0.5f, dialsTopY, dialSize, dialSize },
        (st.volumeDb + 24.0f) / 30.0f, "VOL",
        juce::String((int)std::round(st.volumeDb)) + "dB", false);

    drawMiniEncoder(
        g, { r.getX() + 4.0f + colW * 3.0f + (colW - dialSize) * 0.5f, dialsTopY, dialSize, dialSize },
        (st.pan + 1.0f) * 0.5f, "PAN", panStr, true);

    float dY2 = dialsTopY + rowH;
    drawMiniEncoder(
        g, { r.getX() + 4.0f + colW * 0.0f + (colW - dialSize) * 0.5f, dY2, dialSize, dialSize },
        st.attackSec / 2.0f, "ATT", juce::String(st.attackSec, 2) + "s", false);

    drawMiniEncoder(
        g, { r.getX() + 4.0f + colW * 1.0f + (colW - dialSize) * 0.5f, dY2, dialSize, dialSize },
        st.decaySec / 5.0f, "DEC", juce::String(st.decaySec, 2) + "s", false);

    drawMiniEncoder(
        g, { r.getX() + 4.0f + colW * 2.0f + (colW - dialSize) * 0.5f, dY2, dialSize, dialSize },
        st.sustainLvl, "SUS", juce::String((int)std::round(st.sustainLvl * 100.0f)) + "%", false);

    drawMiniEncoder(
        g, { r.getX() + 4.0f + colW * 3.0f + (colW - dialSize) * 0.5f, dY2, dialSize, dialSize },
        st.releaseSec / 5.0f, "REL", juce::String(st.releaseSec, 2) + "s", false);
}

void MelodicAtlasAudioProcessorEditor::drawMiniEncoder(
    juce::Graphics& g, juce::Rectangle<float> dialRect, float normVal,
    const juce::String& insideLabel, const juce::String& subValStr, bool bipolar)
{
    normVal = juce::jlimit(0.0f, 1.0f, normVal);
    const float startAngle = -2.35619f;
    const float endAngle   =  2.35619f;
    const float curAngle   = startAngle + normVal * (endAngle - startAngle);
    const float radius     = dialRect.getWidth() * 0.46f;
    const auto centre      = dialRect.getCentre();

    juce::Path bgArc;
    bgArc.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, startAngle, endAngle, true);
    g.setColour(juce::Colour(0xff28313e));
    g.strokePath(bgArc, juce::PathStrokeType(2.0f));

    juce::Path valArc;
    if (bipolar)
        valArc.addCentredArc(centre.x, centre.y, radius, radius, 0.0f,
                             std::min(0.0f, curAngle), std::max(0.0f, curAngle), true);
    else
        valArc.addCentredArc(centre.x, centre.y, radius, radius, 0.0f,
                             startAngle, curAngle, true);

    g.setColour(juce::Colour(0xff7ec8a3));
    g.strokePath(valArc, juce::PathStrokeType(2.2f));
    g.setColour(juce::Colour(0xff151a22));
    g.fillEllipse(centre.x - radius * 0.82f, centre.y - radius * 0.82f,
                  radius * 1.64f, radius * 1.64f);

    g.setColour(juce::Colour(0xffdce5f0));
    g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 7.0f, juce::Font::bold));
    g.drawText(insideLabel, dialRect, juce::Justification::centred, false);

    g.setColour(juce::Colour(0xff94a3b5));
    g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 7.5f, juce::Font::plain));
    g.drawText(subValStr,
               dialRect.translated(0.0f, dialRect.getHeight() * 0.82f)
                   .withHeight(11.0f).expanded(6.0f, 0.0f),
               juce::Justification::centred, false);
}

void MelodicAtlasAudioProcessorEditor::drawUndoVectorIcon(
    juce::Graphics& g, juce::Rectangle<float> area, juce::Colour col, bool isRedo)
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

void MelodicAtlasAudioProcessorEditor::drawGearVectorIcon(
    juce::Graphics& g, juce::Rectangle<float> area, juce::Colour col)
{
    g.setColour(col);
    auto c = area.getCentre();
    float rOuter = std::min(area.getWidth(), area.getHeight()) * 0.46f;
    float rInner = rOuter * 0.52f;

    for (int i = 0; i < 6; ++i)
    {
        float ang = (float)i * juce::MathConstants<float>::pi / 3.0f;
        g.drawLine(c.x + std::cos(ang) * (rInner * 0.8f),
                   c.y + std::sin(ang) * (rInner * 0.8f),
                   c.x + std::cos(ang) * rOuter,
                   c.y + std::sin(ang) * rOuter, 1.6f);
    }

    g.fillEllipse(c.x - rInner, c.y - rInner, rInner * 2.0f, rInner * 2.0f);
    g.setColour(juce::Colour(0xff181d25));
    g.fillEllipse(c.x - rInner * 0.48f, c.y - rInner * 0.48f,
                  rInner * 0.96f, rInner * 0.96f);
}

void MelodicAtlasAudioProcessorEditor::drawArrowVectorIcon(
    juce::Graphics& g, juce::Rectangle<float> area, bool pointRight, juce::Colour col)
{
    g.setColour(col);
    juce::Path p;
    if (pointRight)
    {
        p.startNewSubPath(area.getX(), area.getY());
        p.lineTo(area.getRight(), area.getCentreY());
        p.lineTo(area.getX(), area.getBottom());
    }
    else
    {
        p.startNewSubPath(area.getRight(), area.getY());
        p.lineTo(area.getX(), area.getCentreY());
        p.lineTo(area.getRight(), area.getBottom());
    }
    p.closeSubPath();
    g.fillPath(p);
}

void MelodicAtlasAudioProcessorEditor::drawVerticalArrowIcon(
    juce::Graphics& g, juce::Rectangle<float> area, bool pointUp, juce::Colour col)
{
    g.setColour(col);
    juce::Path p;
    if (pointUp)
    {
        p.startNewSubPath(area.getX(), area.getBottom());
        p.lineTo(area.getCentreX(), area.getY());
        p.lineTo(area.getRight(), area.getBottom());
    }
    else
    {
        p.startNewSubPath(area.getX(), area.getY());
        p.lineTo(area.getCentreX(), area.getBottom());
        p.lineTo(area.getRight(), area.getY());
    }
    p.closeSubPath();
    g.fillPath(p);
}

void MelodicAtlasAudioProcessorEditor::drawPlaybackModeIcon(
    juce::Graphics& g, juce::Rectangle<float> area,
    Atlas::PadPlaybackMode mode, juce::Colour col)
{
    g.setColour(col);
    float cx = area.getCentreX();
    float cy = area.getCentreY();

    if (mode == Atlas::PadPlaybackMode::OneShot)
    {
        // Стрелка вперед (→)
        g.drawLine(area.getX() + 1.0f, cy, area.getRight() - 1.5f, cy, 1.5f);
        g.drawLine(area.getRight() - 4.5f, cy - 3.0f, area.getRight() - 1.0f, cy, 1.5f);
        g.drawLine(area.getRight() - 4.5f, cy + 3.0f, area.getRight() - 1.0f, cy, 1.5f);
    }
    else if (mode == Atlas::PadPlaybackMode::Loop)
    {
        // Иконка петли (↻)
        juce::Path loopArc;
        float r = std::min(area.getWidth(), area.getHeight()) * 0.42f;
        loopArc.addCentredArc(cx, cy, r, r, 0.0f, -2.2f, 2.2f, true);
        g.strokePath(loopArc, juce::PathStrokeType(1.4f));
        juce::Point<float> tip(cx + r * std::sin(2.2f), cy - r * std::cos(2.2f));
        g.drawLine(tip.x, tip.y, tip.x - 3.0f, tip.y - 1.5f, 1.4f);
        g.drawLine(tip.x, tip.y, tip.x + 1.5f, tip.y - 3.0f, 1.4f);
    }
    else
    {
        // Стрелка назад (←)
        g.drawLine(area.getRight() - 1.0f, cy, area.getX() + 1.5f, cy, 1.5f);
        g.drawLine(area.getX() + 4.5f, cy - 3.0f, area.getX() + 1.0f, cy, 1.5f);
        g.drawLine(area.getX() + 4.5f, cy + 3.0f, area.getX() + 1.0f, cy, 1.5f);
    }
}

void MelodicAtlasAudioProcessorEditor::drawWarningVectorIcon(
    juce::Graphics& g, juce::Rectangle<float> area, juce::Colour col)
{
    juce::Path tri;
    tri.startNewSubPath(area.getCentreX(), area.getY());
    tri.lineTo(area.getRight(), area.getBottom());
    tri.lineTo(area.getX(), area.getBottom());
    tri.closeSubPath();

    g.setColour(col.withAlpha(0.22f));
    g.fillPath(tri);
    g.setColour(col);
    g.strokePath(tri, juce::PathStrokeType(1.5f, juce::PathStrokeType::mitered,
                                           juce::PathStrokeType::rounded));

    float cx = area.getCentreX();
    g.drawLine(cx, area.getY() + 5.0f, cx, area.getBottom() - 5.0f, 1.8f);
    g.fillEllipse(cx - 1.1f, area.getBottom() - 3.6f, 2.2f, 2.2f);
}

void MelodicAtlasAudioProcessorEditor::drawDropdownTriangle(
    juce::Graphics& g, juce::Rectangle<float> area, juce::Colour col)
{
    g.setColour(col);
    juce::Path p;
    p.startNewSubPath(area.getX(), area.getY());
    p.lineTo(area.getRight(), area.getY());
    p.lineTo(area.getCentreX(), area.getBottom());
    p.closeSubPath();
    g.fillPath(p);
}

void MelodicAtlasAudioProcessorEditor::drawLockVectorIcon(
    juce::Graphics& g, juce::Rectangle<float> area, bool isLocked, juce::Colour col)
{
    g.setColour(col);
    juce::Rectangle<float> body(area.getX() + 1.0f, area.getCentreY() - 1.0f,
                                area.getWidth() - 2.0f, area.getHeight() * 0.54f);
    g.drawRoundedRectangle(body, 2.0f, 1.5f);

    juce::Path shackle;
    float sw = body.getWidth() * 0.56f;
    float sx = body.getCentreX() - sw * 0.5f;

    if (isLocked)
        shackle.addCentredArc(body.getCentreX(), body.getY(), sw * 0.5f, sw * 0.55f,
                              0.0f, -juce::MathConstants<float>::pi, 0.0f, true);
    else
        shackle.addCentredArc(sx + sw * 0.85f, body.getY() - 1.0f, sw * 0.5f, sw * 0.55f,
                              0.0f, -juce::MathConstants<float>::pi * 0.85f, 0.1f, true);

    g.strokePath(shackle, juce::PathStrokeType(1.5f));
}

void MelodicAtlasAudioProcessorEditor::drawTrashVectorIcon(
    juce::Graphics& g, juce::Rectangle<float> area, juce::Colour col)
{
    g.setColour(col);
    float x = area.getX();
    float y = area.getY();
    float w = area.getWidth();
    float h = area.getHeight();

    g.drawLine(x, y + 3.0f, x + w, y + 3.0f, 1.5f);
    g.drawLine(x + w * 0.3f, y + 1.0f, x + w * 0.7f, y + 1.0f, 1.5f);
    g.drawRoundedRectangle(x + 2.0f, y + 3.5f, w - 4.0f, h - 4.0f, 1.5f, 1.4f);
}

void MelodicAtlasAudioProcessorEditor::drawExportVectorIcon(
    juce::Graphics& g, juce::Rectangle<float> area, juce::Colour col)
{
    g.setColour(col);
    float cx = area.getCentreX();
    float cy = area.getCentreY();

    g.drawLine(area.getX(), cy, area.getRight() - 2.0f, cy, 2.0f);
    g.drawLine(area.getRight() - 6.0f, cy - 4.5f, area.getRight() - 1.0f, cy, 2.0f);
    g.drawLine(area.getRight() - 6.0f, cy + 4.5f, area.getRight() - 1.0f, cy, 2.0f);
    g.drawLine(cx - 4.0f, area.getY() + 2.0f, cx - 4.0f, area.getBottom() - 2.0f, 1.4f);
}

void MelodicAtlasAudioProcessorEditor::drawPlusVectorIcon(
    juce::Graphics& g, juce::Rectangle<float> area, juce::Colour col)
{
    g.setColour(col);
    float cx = area.getCentreX();
    float cy = area.getCentreY();
    float len = std::min(area.getWidth(), area.getHeight()) * 0.35f;

    g.drawLine(cx, cy - len, cx, cy + len, 2.5f);
    g.drawLine(cx - len, cy, cx + len, cy, 2.5f);
}

void MelodicAtlasAudioProcessorEditor::drawCloseVectorIcon(
    juce::Graphics& g, juce::Rectangle<float> area, juce::Colour col)
{
    g.setColour(col);
    g.drawLine(area.getX(), area.getY(), area.getRight(), area.getBottom(), 2.0f);
    g.drawLine(area.getRight(), area.getY(), area.getX(), area.getBottom(), 2.0f);
}

void MelodicAtlasAudioProcessorEditor::mouseMove(const juce::MouseEvent& e)
{
    updateHoverTooltip(e.position);
}

void MelodicAtlasAudioProcessorEditor::updateHoverTooltip(juce::Point<float> pt)
{
    if (btnLockPadsIcon.contains(pt))
    {
        setTooltip(audioProcessor.padSettingsLocked.load()
                   ? "Pad Lock ON (Keeps ADSR & Tune)" : "Pad Lock OFF");
        return;
    }

    for (int i = 0; i < 12; ++i)
    {
        if (padRects[(size_t)i].contains(pt))
        {
            const auto& st = audioProcessor.getSamplerEngine().getPadState(i);
            const auto* rec = audioProcessor.getSamplerEngine().getPadRecord(i);
            if (st.fileMissing && rec != nullptr)
            {
                setTooltip("Missing sample: " + rec->file.getFullPathName() +
                           " (Connect drive to auto-relink)");
                return;
            }
        }
    }

    setTooltip({});
}

void MelodicAtlasAudioProcessorEditor::mouseDown(const juce::MouseEvent& e)
{
    auto pt = e.position;
    currentPadDragAction = PadDragAction::None;
    activeDragPadIndex = -1;
    currentlyHeldPadIndex = -1;

    if (pt.y <= 42.0f)
    {
        handleTopHeaderClick(e);
        return;
    }
    if (pt.y <= 96.0f)
    {
        handleFilterBarClick(e);
        return;
    }
    if (pt.y >= btnPadTriggerMode.getY() - 4.0f)
    {
        handleBankAndRightPanelClick(e);
    }
}

void MelodicAtlasAudioProcessorEditor::mouseDrag(const juce::MouseEvent& e)
{
    if (currentPadDragAction != PadDragAction::None && activeDragPadIndex >= 0)
    {
        const auto& r = padRects[(size_t)activeDragPadIndex];
        auto& st = audioProcessor.getSamplerEngine().getPadState(activeDragPadIndex);
        float dy = padDragStartPos.y - e.position.y;

        if (currentPadDragAction == PadDragAction::RegionStart ||
            currentPadDragAction == PadDragAction::RegionEnd)
        {
            juce::Rectangle<float> waveBox(r.getX() + 6.0f, r.getY() + 4.0f,
                                           r.getWidth() - 62.0f, 15.0f);
            float normX = (e.position.x - waveBox.getX()) /
                          std::max(1.0f, waveBox.getWidth());
            if (currentPadDragAction == PadDragAction::RegionStart)
                st.regionStart = juce::jlimit(0.0f, st.regionEnd - 0.04f, normX);
            else
                st.regionEnd = juce::jlimit(st.regionStart + 0.04f, 1.0f, normX);
        }
        else if (currentPadDragAction == PadDragAction::DialSemitones)
            st.semitoneOffset = juce::jlimit(-24, 24, (int)std::round(dragStartParamValue + dy * 0.25f));
        else if (currentPadDragAction == PadDragAction::DialCents)
            st.centOffset = juce::jlimit(-100, 100, (int)std::round(dragStartParamValue + dy * 1.5f));
        else if (currentPadDragAction == PadDragAction::DialVol)
            st.volumeDb = juce::jlimit(-24.0f, 6.0f, dragStartParamValue + dy * 0.25f);
        else if (currentPadDragAction == PadDragAction::DialPan)
            st.pan = juce::jlimit(-1.0f, 1.0f, dragStartParamValue + dy * 0.015f);
        else if (currentPadDragAction == PadDragAction::DialAttack)
            st.attackSec = juce::jlimit(0.002f, 2.0f, dragStartParamValue + dy * 0.01f);
        else if (currentPadDragAction == PadDragAction::DialDecay)
            st.decaySec = juce::jlimit(0.01f, 5.0f, dragStartParamValue + dy * 0.025f);
        else if (currentPadDragAction == PadDragAction::DialSustain)
            st.sustainLvl = juce::jlimit(0.0f, 1.0f, dragStartParamValue + dy * 0.005f);
        else if (currentPadDragAction == PadDragAction::DialRelease)
            st.releaseSec = juce::jlimit(0.008f, 5.0f, dragStartParamValue + dy * 0.025f);
        else if (currentPadDragAction == PadDragAction::DragPadToDaw &&
                 e.position.getDistanceFrom(padDragStartPos) >= 7.0f)
        {
            audioProcessor.getSamplerEngine().releasePadFromUI(-1);
            currentlyHeldPadIndex = -1;
            currentPadDragAction = PadDragAction::None;
            activeDragPadIndex = -1;
            std::vector<int> padsToExport(selectedPads.begin(), selectedPads.end());
            exportPadsToDaw(padsToExport);
            return;
        }
        repaint();
        return;
    }

    if (e.position.getDistanceFrom(padDragStartPos) >= 6.0f &&
        btnDragAllIcon.contains(padDragStartPos))
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
    audioProcessor.getSamplerEngine().releasePadFromUI(-1);
    currentlyHeldPadIndex = -1;
    currentPadDragAction = PadDragAction::None;
    activeDragPadIndex = -1;
    repaint();
}

void MelodicAtlasAudioProcessorEditor::mouseDoubleClick(const juce::MouseEvent& e)
{
    // Двойной клик по числу октавы сбрасывает октаву в 0
    if (octValueRect.expanded(4.0f).contains(e.position))
    {
        audioProcessor.octaveShift.store(0);
        repaint();
        return;
    }

    for (int i = 0; i < 12; ++i)
    {
        if (padRects[(size_t)i].contains(e.position))
        {
            auto& st = audioProcessor.getSamplerEngine().getPadState(i);
            if (st.settingsViewOpen)
            {
                const auto& r = padRects[(size_t)i];
                float relY = (e.position.y - (r.getY() + 22.0f)) / (r.getHeight() - 24.0f);
                float relX = (e.position.x - r.getX()) / std::max(1.0f, r.getWidth());

                if (relY >= 0.0f && relY <= 1.0f)
                {
                    audioProcessor.getSamplerEngine().saveCurrentPadToHistory(i);
                    if (relY < 0.5f)
                    {
                        if (relX < 0.25f)      st.semitoneOffset = 0;
                        else if (relX < 0.50f) st.centOffset = 0;
                        else if (relX < 0.75f) st.volumeDb = -3.0f;
                        else                   st.pan = 0.0f;
                    }
                    else
                    {
                        if (relX < 0.25f)      st.attackSec = 0.005f;
                        else if (relX < 0.50f) st.decaySec = 1.2f;
                        else if (relX < 0.75f) st.sustainLvl = 1.0f;
                        else                   st.releaseSec = 0.05f;
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

    if (btnGlobalUndo.contains(pt))
    {
        audioProcessor.performGlobalUndo();
        primarySelectedPad = audioProcessor.primarySelectedPad;
        repaint();
        return;
    }
    if (btnGlobalRedo.contains(pt))
    {
        audioProcessor.performGlobalRedo();
        primarySelectedPad = audioProcessor.primarySelectedPad;
        repaint();
        return;
    }

    if (btnOneShots.contains(pt))
    {
        audioProcessor.showLoopsTab.store(false);
        applyCurrentFiltersToMap();
        repaint();
    }
    else if (btnLoops.contains(pt))
    {
        audioProcessor.showLoopsTab.store(true);
        applyCurrentFiltersToMap();
        repaint();
    }
    else if (btnScanFolder.contains(pt))
    {
        if (!audioProcessor.isCurrentlyScanning())
            openFolderChooser();
    }
    else if (btnRefreshLibrary.contains(pt))
    {
        if (!audioProcessor.isCurrentlyScanning())
            audioProcessor.refreshLibraryAsync();
    }
}

void MelodicAtlasAudioProcessorEditor::handleFilterBarClick(const juce::MouseEvent& e)
{
    auto pt = e.position;

    if (btnDiscover.contains(pt))
    {
        discoverRandomFilter();
        return;
    }

    if (groupFilterRects[0].contains(pt))
    {
        selectedClustersMask = 0;
        selectedMoodMask = 0;
        updateDynamicLayoutRects();
        applyCurrentFiltersToMap();
        repaint();
        return;
    }

    for (int c = 0; c < 8; ++c)
    {
        const auto& btnR = groupFilterRects[(size_t)(c + 1)];
        const auto& dropR = groupDropdownRects[(size_t)c];

        if (!btnR.contains(pt))
            continue;

        if (c == 7)
        {
            showMoodsPopupMenu(btnR);
            return;
        }

        uint32_t bit = (1u << c);

        if (dropR.contains(pt) || e.mods.isPopupMenu())
        {
            if (!e.mods.isCommandDown() && !e.mods.isShiftDown() &&
                (selectedClustersMask & bit) == 0)
            {
                selectedClustersMask = bit;
            }
            else
            {
                selectedClustersMask |= bit;
            }
            applyCurrentFiltersToMap();
            repaint();
            showClusterSubgroupPopupMenu(c, btnR);
            return;
        }

        if (e.mods.isCommandDown() || e.mods.isShiftDown())
        {
            selectedClustersMask ^= bit;
        }
        else
        {
            if (selectedClustersMask == bit)
            {
                showClusterSubgroupPopupMenu(c, btnR);
                return;
            }
            selectedClustersMask = bit;
        }

        applyCurrentFiltersToMap();
        repaint();
        return;
    }

}

void MelodicAtlasAudioProcessorEditor::showClusterSubgroupPopupMenu(
    int clusterIndex, juce::Rectangle<float> targetBtn)
{
    if (clusterIndex < 0 || clusterIndex >= 7)
        return;

    auto cg = static_cast<Atlas::ClusterGroup>(clusterIndex);
    auto options = Atlas::MusicMath::getSubgroupsForCluster(cg);
    uint32_t currentBit = selectedClusterSubgroupBit[(size_t)clusterIndex];

    // Build available subgroups from the actual library, not a fixed menu.
    // Evidence comes from explicit sample metadata and documented DSP tags;
    // CLAP audio embeddings do not contain named instrument tags.
    const auto db = audioProcessor.getSampleDatabaseSnapshot();
    std::vector<int> counts(options.size(), 0);
    int familyTotal = 0;
    if (db != nullptr)
        for (const auto& rec : *db)
        {
            if (rec.isLoop != audioProcessor.showLoopsTab.load()
                || Atlas::PercussionGate::shouldExcludeFromAtlas(rec)
                || rec.identityState != Atlas::IdentityState::Identified
                || rec.identityCluster != cg)
                continue;
            ++familyTotal;
            for (size_t i = 1; i < options.size(); ++i)
                if ((rec.subMask & options[i].flagBit) != 0)
                    ++counts[i];
        }
    const int minimum = familyTotal > 200 ? 2 : 1;
    std::vector<size_t> displayed;
    juce::PopupMenu menu;
    for (size_t i = 0; i < options.size(); ++i)
    {
        if (i != 0 && counts[i] < minimum
            && options[i].flagBit != currentBit)
            continue;
        displayed.push_back(i);
        const int count = i == 0 ? familyTotal : counts[i];
        menu.addItem((int)displayed.size(),
            options[i].fullName + " (" + juce::String(count) + ")",
            true, options[i].flagBit == currentBit);
    }

    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetScreenArea(
            localAreaToGlobal(targetBtn.toNearestInt())),
        [this, clusterIndex, options, displayed](int res)
        {
            if (res >= 1 && res <= (int)displayed.size())
            {
                selectedClusterSubgroupBit[(size_t)clusterIndex] =
                    options[displayed[(size_t)(res - 1)]].flagBit;
                selectedClustersMask |= (1u << clusterIndex);
                updateDynamicLayoutRects();
                applyCurrentFiltersToMap();
                repaint();
            }
        });
}

void MelodicAtlasAudioProcessorEditor::discoverRandomFilter()
{
    // The die randomizes the filter itself, not a sample. Empty selections
    // are valid: this is for unexpected creative searches, not guaranteed hits.
    auto& rng = juce::Random::getSystemRandom();
    const uint32_t flags[] = {
        Atlas::Character_Dark, Atlas::Character_Bright,
        Atlas::Character_Warm, Atlas::Character_Clean,
        Atlas::Character_Noisy, Atlas::Character_Pluck,
        Atlas::Character_Sustain, Atlas::Character_Textural
    };

    // Usually one instrument category, occasionally all or two categories.
    selectedClustersMask = 0;
    const int categoryMode = rng.nextInt(10);
    if (categoryMode < 8)
    {
        const int first = rng.nextInt(7);
        selectedClustersMask = (1u << first);
        if (categoryMode == 7)
        {
            const int second = (first + 1 + rng.nextInt(6)) % 7;
            selectedClustersMask |= (1u << second);
        }
    }

    selectedClusterSubgroupBit.fill(0);
    selectedMoodMask = 0;

    // One to three independently selected qualities, with strict AND
    // semantics. No lookup against sample contents, no relaxed matching.
    const int qualityCount = 1 + rng.nextInt(3);
    int selectedCount = 0;
    while (selectedCount < qualityCount)
    {
        const uint32_t flag = flags[rng.nextInt(8)];
        if ((selectedMoodMask & flag) == 0)
        {
            selectedMoodMask |= flag;
            ++selectedCount;
        }
    }

    updateDynamicLayoutRects();
    applyCurrentFiltersToMap();
    repaint();
}

void MelodicAtlasAudioProcessorEditor::showMoodsPopupMenu(
    juce::Rectangle<float> targetBtn)
{
    auto options = Atlas::MusicMath::getSubgroupsForCluster(Atlas::ClusterGroup::Other);
    juce::PopupMenu menu;

    for (size_t i = 0; i < options.size(); ++i)
    {
        bool ticked = (options[i].flagBit == 0)
            ? (selectedMoodMask == 0)
            : ((selectedMoodMask & options[i].flagBit) != 0);
        menu.addItem((int)i + 1, options[i].fullName, true, ticked);
    }

    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetScreenArea(
            localAreaToGlobal(targetBtn.toNearestInt())),
        [this, options](int res)
        {
            if (res >= 1 && res <= (int)options.size())
            {
                const uint32_t flag = options[(size_t)(res - 1)].flagBit;
                if (flag == 0)
                    selectedMoodMask = 0;
                else
                    selectedMoodMask ^= flag;
                updateDynamicLayoutRects();
                applyCurrentFiltersToMap();
                repaint();
            }
        });
}

void MelodicAtlasAudioProcessorEditor::handleBankAndRightPanelClick(
    const juce::MouseEvent& e)
{
    auto pt = e.position;
    padDragStartPos = pt;

    if (btnPadTriggerMode.contains(pt))
    {
        audioProcessor.bankModeIndex.store((int)Atlas::BankMode::PadTrigger);
        repaint();
        return;
    }
    if (btnChromaticMode.contains(pt))
    {
        audioProcessor.bankModeIndex.store((int)Atlas::BankMode::Chromatic);
        repaint();
        return;
    }
    if (btnLockPadsIcon.contains(pt))
    {
        audioProcessor.padSettingsLocked.store(!audioProcessor.padSettingsLocked.load());
        repaint();
        return;
    }
    if (btnClearAllIcon.contains(pt))
    {
        audioProcessor.clearAllPadsWithUndo();
        repaint();
        return;
    }
    if (btnDragAllIcon.contains(pt))
    {
        std::vector<int> padsToExport;
        for (int i = 0; i < 12; ++i)
        {
            if (audioProcessor.getSamplerEngine().getPadState(i).sampleIndex >= 0)
                padsToExport.push_back(i);
        }
        exportPadsToDaw(padsToExport);
        return;
    }

    for (int i = 0; i < 12; ++i)
    {
        const auto& r = padRects[(size_t)i];
        if (!r.contains(pt))
            continue;

        auto& padState = audioProcessor.getSamplerEngine().getPadState(i);

        if (e.mods.isShiftDown() || e.mods.isCommandDown())
        {
            selectedPads.insert(i);
            primarySelectedPad = i;
        }
        else
        {
            selectedPads.clear();
            selectedPads.insert(i);
            primarySelectedPad = i;
        }
        audioProcessor.primarySelectedPad = primarySelectedPad;

        if (padState.sampleIndex >= 0)
        {
            juce::Rectangle<float> closeBtnZone(
                r.getRight() - 18.0f, r.getY() + 3.0f, 15.0f, 14.0f);
            if (closeBtnZone.contains(pt))
            {
                audioProcessor.pushGlobalUndoState();
                audioProcessor.getSamplerEngine().clearPad(i);
                repaint();
                return;
            }

            juce::Rectangle<float> gearBtnZone(
                r.getRight() - 35.0f, r.getY() + 3.0f, 15.0f, 14.0f);
            if (gearBtnZone.contains(pt))
            {
                padState.settingsViewOpen = !padState.settingsViewOpen;
                if (padState.settingsViewOpen)
                    padState.syncViewOpen = false;
                repaint();
                return;
            }

            juce::Rectangle<float> syncBtnZone(
                r.getRight() - 52.0f, r.getY() + 3.0f, 15.0f, 14.0f);
            if (syncBtnZone.contains(pt))
            {
                padState.syncViewOpen = !padState.syncViewOpen;
                if (padState.syncViewOpen)
                    padState.settingsViewOpen = false;
                repaint();
                return;
            }
        }

        // Обработка кликов внутри меню S (Sync & Pitch)
        if (padState.syncViewOpen && padState.sampleIndex >= 0)
        {
            float contentTop = r.getY() + 22.0f;
            float contentH = r.getBottom() - contentTop - 5.0f;
            float rowH = (contentH - 4.0f) * 0.5f;
            float colW = (r.getWidth() - 16.0f - 8.0f) / 3.0f;
            float startX = r.getX() + 8.0f;

            juce::Rectangle<float> bSync(startX, contentTop, colW, rowH);
            juce::Rectangle<float> bHalf(startX + colW + 4.0f, contentTop, colW, rowH);
            juce::Rectangle<float> bDoub(startX + (colW + 4.0f) * 2.0f, contentTop, colW, rowH);

            audioProcessor.getSamplerEngine().saveCurrentPadToHistory(i);

            if (bSync.contains(pt))
            {
                padState.padSyncBpm = !padState.padSyncBpm;
                if (!padState.padSyncBpm)
                    padState.padSpeedMult = 1.0f;
            }
            else if (bHalf.contains(pt))
            {
                padState.padSyncBpm = true;
                padState.padSpeedMult = (std::abs(padState.padSpeedMult - 0.5f) < 0.05f)
                                      ? 1.0f : 0.5f;
            }
            else if (bDoub.contains(pt))
            {
                padState.padSyncBpm = true;
                padState.padSpeedMult = (std::abs(padState.padSpeedMult - 2.0f) < 0.05f)
                                      ? 1.0f : 2.0f;
            }
            repaint();
            return;
        }

        // Обработка кликов внутри меню шестеренки (Settings)
        if (padState.settingsViewOpen && padState.sampleIndex >= 0)
        {
            audioProcessor.pushGlobalUndoState();
            audioProcessor.getSamplerEngine().saveCurrentPadToHistory(i);
            activeDragPadIndex = i;

            juce::Rectangle<float> regionWaveBox(
                r.getX() + 6.0f, r.getY() + 4.0f, r.getWidth() - 62.0f, 15.0f);
            if (regionWaveBox.expanded(2.0f, 2.0f).contains(pt))
            {
                float normX = juce::jlimit(
                    0.0f, 1.0f,
                    (pt.x - regionWaveBox.getX()) / std::max(1.0f, regionWaveBox.getWidth()));
                float distStart = std::abs(normX - padState.regionStart);
                float distEnd   = std::abs(normX - padState.regionEnd);
                if (distStart <= distEnd)
                {
                    currentPadDragAction = PadDragAction::RegionStart;
                    padState.regionStart = juce::jlimit(0.0f, padState.regionEnd - 0.04f, normX);
                }
                else
                {
                    currentPadDragAction = PadDragAction::RegionEnd;
                    padState.regionEnd = juce::jlimit(padState.regionStart + 0.04f, 1.0f, normX);
                }
                repaint();
                return;
            }

            float relY = (pt.y - (r.getY() + 22.0f)) / (r.getHeight() - 24.0f);
            float relX = (pt.x - r.getX()) / std::max(1.0f, r.getWidth());

            if (relY < 0.5f)
            {
                if (relX < 0.25f)
                {
                    currentPadDragAction = PadDragAction::DialSemitones;
                    dragStartParamValue = (float)padState.semitoneOffset;
                }
                else if (relX < 0.50f)
                {
                    currentPadDragAction = PadDragAction::DialCents;
                    dragStartParamValue = (float)padState.centOffset;
                }
                else if (relX < 0.75f)
                {
                    currentPadDragAction = PadDragAction::DialVol;
                    dragStartParamValue = padState.volumeDb;
                }
                else
                {
                    currentPadDragAction = PadDragAction::DialPan;
                    dragStartParamValue = padState.pan;
                }
            }
            else
            {
                if (relX < 0.25f)
                {
                    currentPadDragAction = PadDragAction::DialAttack;
                    dragStartParamValue = padState.attackSec;
                }
                else if (relX < 0.50f)
                {
                    currentPadDragAction = PadDragAction::DialDecay;
                    dragStartParamValue = padState.decaySec;
                }
                else if (relX < 0.75f)
                {
                    currentPadDragAction = PadDragAction::DialSustain;
                    dragStartParamValue = padState.sustainLvl;
                }
                else
                {
                    currentPadDragAction = PadDragAction::DialRelease;
                    dragStartParamValue = padState.releaseSec;
                }
            }
            repaint();
            return;
        }

        juce::Rectangle<float> plusBtnZone(
            r.getCentreX() - 15.0f, r.getCentreY() - 15.0f, 30.0f, 30.0f);
        if (padState.sampleIndex < 0)
        {
            int activeMapIdx = mapCanvas.getActiveSampleIndex();
            const auto db = audioProcessor.getSampleDatabaseSnapshot();
            if ((plusBtnZone.contains(pt) || e.mods.isPopupMenu()) &&
                activeMapIdx >= 0 && activeMapIdx < (int)db->size())
            {
                audioProcessor.assignSampleToPadWithPreviewSettings(i, activeMapIdx, true);
                currentlyHeldPadIndex = i;
                audioProcessor.getSamplerEngine().triggerPadFromUI(i);
            }
            repaint();
            return;
        }

        juce::Rectangle<float> playModeBtnZone(r.getRight() - 70.0f, r.getY(), 17.0f, 19.0f);
        juce::Rectangle<float> chromDotZone   (r.getRight() - 86.0f, r.getY(), 15.0f, 19.0f);

        if (playModeBtnZone.contains(pt))
        {
            audioProcessor.pushGlobalUndoState();
            audioProcessor.getSamplerEngine().saveCurrentPadToHistory(i);
            int nextMode = ((int)padState.playbackMode + 1) % 3;
            padState.playbackMode = static_cast<Atlas::PadPlaybackMode>(nextMode);
            repaint();
            return;
        }
        if (chromDotZone.contains(pt))
        {
            audioProcessor.pushGlobalUndoState();
            audioProcessor.getSamplerEngine().saveCurrentPadToHistory(i);
            padState.chromaticActive = !padState.chromaticActive;
            repaint();
            return;
        }

        float midY = r.getY() + 19.0f;
        float midH = std::max(18.0f, r.getHeight() - 36.0f);
        juce::Rectangle<float> leftUndoZone  (r.getX(), midY, 20.0f, midH);
        juce::Rectangle<float> rightArrowZone(r.getRight() - 19.0f, midY, 19.0f, midH);

        // Клик по левой кнопке Undo возвращает предыдущий звук пэда со всеми его настройками
        if (leftUndoZone.contains(pt))
        {
            if (!padState.history.empty())
            {
                audioProcessor.pushGlobalUndoState();
                audioProcessor.getSamplerEngine().undoPadSample(i);
                if (padState.sampleIndex >= 0)
                    mapCanvas.selectSampleFromPad(padState.sampleIndex);
                currentlyHeldPadIndex = i;
                audioProcessor.getSamplerEngine().triggerPadFromUI(i);
                repaint();
            }
            return;
        }

        // Клик по правой стрелке переключает на похожий звук (сбрасывая регион в 0..1)
        if (rightArrowZone.contains(pt))
        {
            currentlyHeldPadIndex = i;
            audioProcessor.stepSimilarSampleInPad(i, 1);
            if (padState.sampleIndex >= 0)
                mapCanvas.selectSampleFromPad(padState.sampleIndex);
            repaint();
            return;
        }

        // Если файл был помечен как потерянный, пробуем подхватить его при клике
        if (padState.fileMissing)
        {
            audioProcessor.getSamplerEngine().relinkMissingFiles();
        }

        activeDragPadIndex = i;
        currentlyHeldPadIndex = i;
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
    for (int k = 0; k < 12; ++k)
        m.addItem(k + 1, Atlas::MusicMath::getNoteName(k), true, k == currentKey);

    m.showMenuAsync(
        juce::PopupMenu::Options().withTargetScreenArea(
            localAreaToGlobal(btnKeyRoot.toNearestInt())),
        [this](int res)
        {
            if (res >= 1 && res <= 12)
            {
                audioProcessor.projectKeyRoot.store(res - 1);
                repaint();
            }
        });
}

void MelodicAtlasAudioProcessorEditor::showScalePopupMenu()
{
    juce::PopupMenu m;
    int currentScale = audioProcessor.projectScaleType.load();
    for (int s = 0; s < 10; ++s)
    {
        m.addItem(s + 1,
                  Atlas::MusicMath::getScaleFullName(static_cast<Atlas::ScaleType>(s)),
                  true, s == currentScale);
    }

    m.showMenuAsync(
        juce::PopupMenu::Options().withTargetScreenArea(
            localAreaToGlobal(btnScaleType.toNearestInt())),
        [this](int res)
        {
            if (res >= 1 && res <= 10)
            {
                audioProcessor.projectScaleType.store(res - 1);
                repaint();
            }
        });
}

void MelodicAtlasAudioProcessorEditor::exportPadsToDaw(
    const std::vector<int>& padIndices)
{
    juce::StringArray filesToDrag;
    for (int idx : padIndices)
    {
        juce::File exportedFile =
            audioProcessor.getSamplerEngine().getPadFileForExport(idx);
        if (exportedFile.existsAsFile())
            filesToDrag.addIfNotAlreadyThere(exportedFile.getFullPathName());
    }
    if (!filesToDrag.isEmpty())
        performExternalDragDropOfFiles(filesToDrag, false);
}

void MelodicAtlasAudioProcessorEditor::openFolderChooser()
{
    folderChooser = std::make_unique<juce::FileChooser>(
        "Select Sample Library Folder",
        juce::File::getSpecialLocation(juce::File::userHomeDirectory));

    folderChooser->launchAsync(
        juce::FileBrowserComponent::openMode |
        juce::FileBrowserComponent::canSelectDirectories,
        [this](const juce::FileChooser& fc)
        {
            auto folder = fc.getResult();
            if (folder.isDirectory())
                audioProcessor.scanFolderAsync(folder);
        });
}

bool MelodicAtlasAudioProcessorEditor::isInterestedInFileDrag(
    const juce::StringArray& files)
{
    for (const auto& f : files)
    {
        if (juce::File(f).isDirectory())
            return true;
    }
    return false;
}

void MelodicAtlasAudioProcessorEditor::filesDropped(
    const juce::StringArray& files, int, int)
{
    for (const auto& f : files)
    {
        juce::File dir(f);
        if (dir.isDirectory())
        {
            audioProcessor.scanFolderAsync(dir);
            break;
        }
    }
}

void MelodicAtlasAudioProcessorEditor::timerCallback()
{
    uint64_t currentVer = audioProcessor.databaseVersion.load();
    if (currentVer != lastSeenDbVersion)
    {
        lastSeenDbVersion = currentVer;
        mapCanvas.setSampleDatabase(audioProcessor.getSampleDatabaseSnapshot());
        repaint();
    }

    // Живое обновление BPM на экране при изменении темпа в Ableton
    double curBpm = audioProcessor.currentBpm.load();
    if (std::abs(curBpm - lastDisplayedBpm) > 0.05)
    {
        lastDisplayedBpm = curBpm;
        repaint((int)btnBpmHalf.getX() - 4, 0, 160, 40);
    }

    // Авто-подхват сэмплов (Hot-Relink) каждые 2 секунды, если подключили внешний SSD
    if (++relinkCheckCounter >= 60)
    {
        relinkCheckCounter = 0;
        if (audioProcessor.getSamplerEngine().relinkMissingFiles())
            repaint();
    }

    uint32_t currentMask =
        audioProcessor.getSamplerEngine().getActivePlayingPadsMask();
    if (currentMask != lastPlayingMask)
    {
        lastPlayingMask = currentMask;
        repaint(0, (int)btnPadTriggerMode.getY() - 4,
                getWidth(), getHeight() - (int)btnPadTriggerMode.getY() + 4);
    }

    if (audioProcessor.isCurrentlyScanning())
    {
        repaint((int)btnScanFolder.getX() - 4, 0,
                (int)(btnRefreshLibrary.getRight() - btnScanFolder.getX()) + 8, 40);
    }
}
