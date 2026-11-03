#include "PluginEditor.h"

MelodicAtlasAudioProcessorEditor::MelodicAtlasAudioProcessorEditor(MelodicAtlasAudioProcessor& p)
    : AudioProcessorEditor(&p),
      audioProcessor(p),
      mapCanvas(p.getSamplerEngine())
{
    setLookAndFeel(&atlasLookAndFeel);
    setSize(1080, 640);

    addAndMakeVisible(mapCanvas);
    mapCanvas.setSampleDatabase(&audioProcessor.getSampleDatabase());

    // Прямой Drag-and-Drop точки с 2D-карты в пэд или сразу в Ableton Live
    mapCanvas.onSampleDragged = [this](int sampleIdx, juce::Point<int> globalPos, bool finished)
    {
        auto localPt = getLocalPoint(nullptr, globalPos).toFloat();
        const auto& db = audioProcessor.getSampleDatabase();
        if (sampleIdx < 0 || sampleIdx >= (int)db.size())
            return;

        int overPad = -1;
        for (int i = 0; i < 12; ++i)
        {
            if (padRects[(size_t)i].contains(localPt))
            {
                overPad = i;
                break;
            }
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
                audioProcessor.getSamplerEngine().assignSampleToPad(overPad, sampleIdx, db[(size_t)sampleIdx]);
                selectedPads.clear();
                selectedPads.insert(overPad);
                primarySelectedPad = overPad;
                syncKnobsFromSelectedPad();
                audioProcessor.getSamplerEngine().triggerPadFromUI(overPad);
                repaint();
                return;
            }
        }
        else if (localPt.x < 6.0f || localPt.x > (float)getWidth() - 6.0f ||
                 localPt.y < 6.0f || localPt.y > (float)getHeight() - 6.0f)
        {
            // Как только курсор с точкой ведут к границе окна плагина — отдаём .wav напрямую в Ableton Live
            hoveredDropPadIndex = -1;
            if (db[(size_t)sampleIdx].file.existsAsFile())
            {
                juce::StringArray files;
                files.add(db[(size_t)sampleIdx].file.getFullPathName());
                performExternalDragDropOfFiles(files, false);
            }
        }
    };

    auto setupKnob = [this](juce::Slider& s, float minV, float maxV, float defV, std::function<void(float)> onChange)
    {
        s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        s.setRange(minV, maxV);
        s.setValue(defV, juce::dontSendNotification);
        s.onValueChange = [onChange, &s]() { onChange((float)s.getValue()); };
        addAndMakeVisible(s);
    };

    // Глобальные REVERB и MASTER
    setupKnob(reverbSlider,    0.0f,   1.0f,  audioProcessor.reverbAmount.load(),   [this](float v){ audioProcessor.reverbAmount.store(v); });
    setupKnob(masterVolSlider, -24.0f, 6.0f,  audioProcessor.masterVolumeDb.load(), [this](float v){ audioProcessor.masterVolumeDb.store(v); });

    // Индивидуальные настройки пэда (VOL, PAN, TUNE, GLIDE + ADSR)
    setupKnob(padVolSlider,  -24.0f, 6.0f,  -3.0f, [this](float v){ applyToTargetPads([v](Atlas::PadSlotState& st){ st.volumeDb = v; }); repaint(); });
    setupKnob(padPanSlider,  -1.0f,  1.0f,   0.0f, [this](float v){ applyToTargetPads([v](Atlas::PadSlotState& st){ st.pan = v; }); repaint(); });
    setupKnob(padTuneSlider, -24.0f, 24.0f,  0.0f, [this](float v){ applyToTargetPads([v](Atlas::PadSlotState& st){ st.semitoneOffset = (int)std::round(v); }); repaint(); });
    setupKnob(glideSlider,    0.0f,  0.6f,   0.0f, [this](float v){ audioProcessor.glideTime.store(v); });

    setupKnob(attackSlider,  0.002f, 2.5f, 0.015f, [this](float v){ applyToTargetPads([v](Atlas::PadSlotState& st){ st.attackSec  = v; }); });
    setupKnob(decaySlider,   0.02f,  3.5f, 1.20f,  [this](float v){ applyToTargetPads([v](Atlas::PadSlotState& st){ st.decaySec   = v; }); });
    setupKnob(sustainSlider, 0.0f,   1.0f, 0.80f,  [this](float v){ applyToTargetPads([v](Atlas::PadSlotState& st){ st.sustainLvl = v; }); });
    setupKnob(releaseSlider, 0.02f,  4.5f, 0.45f,  [this](float v){ applyToTargetPads([v](Atlas::PadSlotState& st){ st.releaseSec = v; }); });

    syncKnobsFromSelectedPad();
    lastSeenDbVersion = audioProcessor.databaseVersion.load();
    startTimerHz(30);
}

MelodicAtlasAudioProcessorEditor::~MelodicAtlasAudioProcessorEditor()
{
    setLookAndFeel(nullptr);
}

void MelodicAtlasAudioProcessorEditor::syncKnobsFromSelectedPad()
{
    const auto& st = audioProcessor.getSamplerEngine().getPadState(primarySelectedPad);
    padVolSlider.setValue(st.volumeDb, juce::dontSendNotification);
    padPanSlider.setValue(st.pan, juce::dontSendNotification);
    padTuneSlider.setValue((double)st.semitoneOffset, juce::dontSendNotification);
    attackSlider.setValue(st.attackSec, juce::dontSendNotification);
    decaySlider.setValue(st.decaySec, juce::dontSendNotification);
    sustainSlider.setValue(st.sustainLvl, juce::dontSendNotification);
    releaseSlider.setValue(st.releaseSec, juce::dontSendNotification);
}

void MelodicAtlasAudioProcessorEditor::applyToTargetPads(std::function<void(Atlas::PadSlotState&)> fn)
{
    if (!selectedPads.empty())
    {
        for (int idx : selectedPads)
            fn(audioProcessor.getSamplerEngine().getPadState(idx));
    }
    else
    {
        fn(audioProcessor.getSamplerEngine().getPadState(primarySelectedPad));
    }
}

void MelodicAtlasAudioProcessorEditor::resized()
{
    mapCanvas.setBounds(14, 108, getWidth() - 28, 290);

    // Крутилки REVERB и MASTER в верхнем блоке справа
    reverbSlider.setBounds   (778, 48, 48, 48);
    masterVolSlider.setBounds(838, 48, 48, 48);

    // Сетка 12 пэдов
    const float padStartX = 14.0f;
    const float padStartY = 438.0f;
    const float padW = 138.0f;
    const float padH = 72.0f;
    const float gapX = 7.0f;
    const float gapY = 8.0f;

    for (int i = 0; i < 12; ++i)
    {
        int row = i / 6;
        int col = i % 6;
        float x = padStartX + (float)col * (padW + gapX);
        float y = padStartY + (float)row * (padH + gapY);
        padRects[(size_t)i] = { x, y, padW, padH };
    }

    // 8 крутилок инспектора пэда (2 ряда по 4 штуки: VOL, PAN, TUNE, GLIDE / A, D, S, R)
    const int knobBaseX = 886;
    const int knobBaseY = 432;
    const int stepX = 46;

    padVolSlider.setBounds (knobBaseX,             knobBaseY,      42, 42);
    padPanSlider.setBounds (knobBaseX + stepX,     knobBaseY,      42, 42);
    padTuneSlider.setBounds(knobBaseX + stepX * 2, knobBaseY,      42, 42);
    glideSlider.setBounds  (knobBaseX + stepX * 3, knobBaseY,      42, 42);

    attackSlider.setBounds (knobBaseX,             knobBaseY + 74, 42, 42);
    decaySlider.setBounds  (knobBaseX + stepX,     knobBaseY + 74, 42, 42);
    sustainSlider.setBounds(knobBaseX + stepX * 2, knobBaseY + 74, 42, 42);
    releaseSlider.setBounds(knobBaseX + stepX * 3, knobBaseY + 74, 42, 42);
}

void MelodicAtlasAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff14181f));

    drawTopHeader(g);
    drawFilterBar(g);
    drawBankSection(g);
}

void MelodicAtlasAudioProcessorEditor::drawTopHeader(juce::Graphics& g)
{
    const juce::Font monoBold(juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::bold);
    const juce::Font monoReg (juce::Font::getDefaultMonospacedFontName(), 11.5f, juce::Font::plain);

    g.setColour(juce::Colour(0xff181d25));
    g.fillRect(0, 0, getWidth(), 40);
    g.setColour(juce::Colour(0xff262e3b));
    g.drawHorizontalLine(40, 0.0f, (float)getWidth());

    g.setColour(juce::Colour(0xff96b382));
    g.drawLine(14.0f, 27.0f, 20.0f, 13.0f, 2.0f);
    g.drawLine(20.0f, 13.0f, 24.0f, 23.0f, 2.0f);
    g.drawLine(24.0f, 23.0f, 29.0f, 13.0f, 2.0f);
    g.drawLine(29.0f, 13.0f, 34.0f, 27.0f, 2.0f);

    g.setColour(juce::Colour(0xffdce4ee));
    g.setFont(monoBold);
    g.drawText("MELODIC-ATLAS", 40, 0, 122, 40, juce::Justification::centredLeft, false);

    auto drawPillButton = [&](juce::Rectangle<float> r, const juce::String& text, bool active, juce::Colour activeCol = juce::Colour(0xff2c3e35))
    {
        g.setColour(active ? activeCol : juce::Colour(0xff1d232d));
        g.fillRoundedRectangle(r, 3.0f);
        g.setColour(active ? juce::Colour(0xff7ec8a3) : juce::Colour(0xff36404e));
        g.drawRoundedRectangle(r, 3.0f, 1.0f);
        g.setColour(active ? juce::Colours::white : juce::Colour(0xff9aa6b5));
        g.setFont(monoReg);
        g.drawText(text, r, juce::Justification::centred, false);
    };

    bool loopsTab = audioProcessor.showLoopsTab.load();
    btnOneShots = { 166.0f, 8.0f, 88.0f, 24.0f };
    btnLoops    = { 258.0f, 8.0f, 66.0f, 24.0f };
    drawPillButton(btnOneShots, "[ONE-SHOTS]", !loopsTab);
    drawPillButton(btnLoops,    "[LOOPS]",     loopsTab);

    // KEY + Выпадающие списки для ноты и лада
    g.setColour(juce::Colour(0xff8c98a8));
    g.drawText("KEY:", 332, 0, 32, 40, juce::Justification::centredLeft, false);

    btnKeyRoot   = { 366.0f, 8.0f, 44.0f, 24.0f };
    btnScaleType = { 414.0f, 8.0f, 86.0f, 24.0f };
    btnPitchMode = { 504.0f, 8.0f, 52.0f, 24.0f };
    btnKeyLock   = { 560.0f, 8.0f, 52.0f, 24.0f };

    juce::String keyName = Atlas::MusicMath::getNoteName(audioProcessor.projectKeyRoot.load()) + " v";
    auto sType = static_cast<Atlas::ScaleType>(audioProcessor.projectScaleType.load());
    juce::String scaleStr = Atlas::MusicMath::getScaleShortName(sType);
    int pMode = audioProcessor.pitchModeIndex.load();
    juce::String modeStr = (pMode == 0) ? "[SNAP]" : (pMode == 1 ? "[ROOT]" : "[OFF]");

    drawPillButton(btnKeyRoot,   keyName,  true);
    drawPillButton(btnScaleType, scaleStr, true);
    drawPillButton(btnPitchMode, modeStr,  pMode != 2);
    drawPillButton(btnKeyLock,   audioProcessor.keyLocked.load() ? "[LOCK*]" : "[FREE]", audioProcessor.keyLocked.load());

    // BPM с отдельным замком (по умолчанию закрыт)
    g.setColour(juce::Colour(0xff8c98a8));
    g.drawText("BPM:", 618, 0, 34, 40, juce::Justification::centredLeft, false);

    btnBpmValue = { 652.0f, 8.0f, 50.0f, 24.0f };
    btnBpmLock  = { 706.0f, 8.0f, 64.0f, 24.0f };
    bool bpmLocked = audioProcessor.bpmLockedToHost.load();
    drawPillButton(btnBpmValue, juce::String(audioProcessor.currentBpm.load(), 1), true);
    drawPillButton(btnBpmLock,  bpmLocked ? "[LOCK*]" : "[FREE]", bpmLocked);

    // Заголовки REVERB и MASTER
    g.setColour(juce::Colour(0xffc7d2de));
    g.drawText("REVERB", 772, 0, 60, 40, juce::Justification::centred, false);
    g.drawText("MASTER", 832, 0, 60, 40, juce::Justification::centred, false);

    btnScanFolder = { 896.0f, 8.0f, 170.0f, 24.0f };
    juce::String scanText = audioProcessor.isCurrentlyScanning()
        ? ("SCANNING " + juce::String((int)(audioProcessor.getScanProgress() * 100.0f)) + "%")
        : "[SCAN SAMPLE FOLDER]";
    drawPillButton(btnScanFolder, scanText, audioProcessor.isCurrentlyScanning(), juce::Colour(0xff354839));
}

void MelodicAtlasAudioProcessorEditor::drawFilterBar(juce::Graphics& g)
{
    const juce::Font monoReg(juce::Font::getDefaultMonospacedFontName(), 11.5f, juce::Font::plain);

    g.setColour(juce::Colour(0xff161b22));
    g.fillRect(14, 46, getWidth() - 28, 56);
    g.setColour(juce::Colour(0xff262e3b));
    g.drawRect(14, 46, getWidth() - 28, 56, 1);

    btnModeTimbre    = { 24.0f, 52.0f, 110.0f, 20.0f };
    btnModeCharacter = { 24.0f, 76.0f, 110.0f, 20.0f };

    bool isTimbre = (activeFilterCategory == Atlas::FilterCategory::Timbre);

    g.setColour(isTimbre ? juce::Colour(0xff7ec8a3) : juce::Colour(0xff525e6e));
    g.fillEllipse(28.0f, 58.0f, 8.0f, 8.0f);
    g.setColour(isTimbre ? juce::Colours::white : juce::Colour(0xff8793a3));
    g.setFont(monoReg);
    g.drawText("[TIMBRE]", 42, 52, 90, 20, juce::Justification::centredLeft, false);

    g.setColour(!isTimbre ? juce::Colour(0xff7ec8a3) : juce::Colour(0xff525e6e));
    g.drawEllipse(28.0f, 82.0f, 8.0f, 8.0f, 1.5f);
    if (!isTimbre) g.fillEllipse(28.0f, 82.0f, 8.0f, 8.0f);
    g.setColour(!isTimbre ? juce::Colours::white : juce::Colour(0xff8793a3));
    g.drawText("[CHARACTER]", 42, 76, 90, 20, juce::Justification::centredLeft, false);

    auto drawFilterTag = [&](juce::Rectangle<float> r, const juce::String& label, bool on)
    {
        g.setColour(on ? juce::Colour(0xff294236) : juce::Colour(0xff1e242d));
        g.fillRoundedRectangle(r, 2.5f);
        g.setColour(on ? juce::Colour(0xff7ec8a3) : juce::Colour(0xff323b48));
        g.drawRoundedRectangle(r, 2.5f, 1.0f);
        g.setColour(on ? juce::Colours::white : juce::Colour(0xff9aa6b5));
        g.drawText(label, r, juce::Justification::centred, false);
    };

    static const char* timbreLabels[5] = { "[KEYS]", "[GUITAR]", "[VOX]", "[SYNTH]", "[STRINGS]" };
    static const uint32_t timbreBits[5] = {
        Atlas::Timbre_Keys, Atlas::Timbre_Guitar, Atlas::Timbre_Vox, Atlas::Timbre_Synth, Atlas::Timbre_Strings
    };

    float tx = 148.0f;
    for (int i = 0; i < 5; ++i)
    {
        timbreTagRects[(size_t)i] = { tx, 51.0f, 84.0f, 21.0f };
        bool active = isTimbre && ((activeTimbreMask & timbreBits[i]) != 0);
        drawFilterTag(timbreTagRects[(size_t)i], timbreLabels[i], active);
        tx += 92.0f;
    }

    static const char* charLabels[7] = { "[WARM]", "[DEEP]", "[ETHY]", "[CLEAN]", "[DIRTY]", "[PLUCK]", "[SUSTAIN]" };
    static const uint32_t charBits[7] = {
        Atlas::Char_Warm, Atlas::Char_Deep, Atlas::Char_Ethy, Atlas::Char_Clean,
        Atlas::Char_Dirty, Atlas::Char_Pluck, Atlas::Char_Sustain
    };

    float cx = 148.0f;
    for (int i = 0; i < 7; ++i)
    {
        float w = (i == 6) ? 80.0f : 72.0f;
        charTagRects[(size_t)i] = { cx, 76.0f, w, 21.0f };
        bool active = !isTimbre && ((activeCharacterMask & charBits[i]) != 0);
        drawFilterTag(charTagRects[(size_t)i], charLabels[i], active);
        cx += w + 7.0f;
    }

    g.setColour(juce::Colour(0xff9aa6b5));
    g.drawText("OCTAVE: (" + juce::String(audioProcessor.octaveShift.load()) + ")", 896, 50, 105, 20, juce::Justification::centredLeft, false);

    btnOctDown = { 896.0f, 73.0f, 78.0f, 22.0f };
    btnOctUp   = { 982.0f, 73.0f, 78.0f, 22.0f };
    drawFilterTag(btnOctDown, "[-OCT]", audioProcessor.octaveShift.load() < 0);
    drawFilterTag(btnOctUp,   "[+OCT]", audioProcessor.octaveShift.load() > 0);
}

void MelodicAtlasAudioProcessorEditor::drawBankSection(juce::Graphics& g)
{
    const juce::Font monoBold(juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::bold);
    const juce::Font monoReg (juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::plain);

    g.setColour(juce::Colour(0xffd0dae5));
    g.setFont(monoBold);
    g.drawText("BANK ENGINE:", 14, 406, 105, 24, juce::Justification::centredLeft, false);

    bool isChromatic = (audioProcessor.bankModeIndex.load() == (int)Atlas::BankMode::Chromatic);
    btnPadTriggerMode = { 120.0f, 407.0f, 112.0f, 22.0f };
    btnChromaticMode  = { 238.0f, 407.0f, 124.0f, 22.0f };

    g.setColour(!isChromatic ? juce::Colour(0xff294236) : juce::Colour(0xff1c222b));
    g.fillRoundedRectangle(btnPadTriggerMode, 3.0f);
    g.setColour(!isChromatic ? juce::Colour(0xff7ec8a3) : juce::Colour(0xff343e4d));
    g.drawRoundedRectangle(btnPadTriggerMode, 3.0f, 1.0f);
    g.setColour(!isChromatic ? juce::Colours::white : juce::Colour(0xff8e9aa8));
    g.setFont(monoReg);
    g.drawText("[PAD TRIGGER]", btnPadTriggerMode, juce::Justification::centred, false);

    g.setColour(isChromatic ? juce::Colour(0xff294236) : juce::Colour(0xff1c222b));
    g.fillRoundedRectangle(btnChromaticMode, 3.0f);
    g.setColour(isChromatic ? juce::Colour(0xff7ec8a3) : juce::Colour(0xff343e4d));
    g.drawRoundedRectangle(btnChromaticMode, 3.0f, 1.0f);
    g.setColour(isChromatic ? juce::Colours::white : juce::Colour(0xff8e9aa8));
    g.drawText(isChromatic ? "(* CHROMATIC)" : "(o CHROMATIC)", btnChromaticMode, juce::Justification::centred, false);

    // Индикатор текущего редактируемого пэда над блоком крутилок
    g.setColour(juce::Colour(0xff8ab89f));
    juce::String padHeader = "PAD [" + juce::String(primarySelectedPad + 1).paddedLeft('0', 2) + "] CONTROLS";
    g.drawText(padHeader, 886, 407, 180, 22, juce::Justification::centred, false);

    uint32_t playingMask = audioProcessor.getSamplerEngine().getActivePlayingPadsMask();
    for (int i = 0; i < 12; ++i)
    {
        bool isPlayingNow = ((playingMask & (1u << i)) != 0);
        drawPadSlot(g, i, padRects[(size_t)i], isPlayingNow);
    }

    // Подписи под 8 крутилками инспектора пэда
    const int knobBaseX = 886;
    const int stepX = 46;
    g.setColour(juce::Colour(0xff9aa6b5));
    g.setFont(monoReg);
    g.drawText("VOL",   knobBaseX,             473, 42, 15, juce::Justification::centred, false);
    g.drawText("PAN",   knobBaseX + stepX,     473, 42, 15, juce::Justification::centred, false);
    g.drawText("TUNE",  knobBaseX + stepX * 2, 473, 42, 15, juce::Justification::centred, false);
    g.drawText("GLIDE", knobBaseX + stepX * 3, 473, 42, 15, juce::Justification::centred, false);

    g.drawText("A",     knobBaseX,             547, 42, 15, juce::Justification::centred, false);
    g.drawText("D",     knobBaseX + stepX,     547, 42, 15, juce::Justification::centred, false);
    g.drawText("S",     knobBaseX + stepX * 2, 547, 42, 15, juce::Justification::centred, false);
    g.drawText("R",     knobBaseX + stepX * 3, 547, 42, 15, juce::Justification::centred, false);

    btnClearAllBank = { 14.0f,  598.0f, 148.0f, 30.0f };
    btnDragAllAudio = { 876.0f, 598.0f, 190.0f, 30.0f };

    g.setColour(juce::Colour(0xff1e242d));
    g.fillRoundedRectangle(btnClearAllBank, 3.5f);
    g.setColour(juce::Colour(0xff36404e));
    g.drawRoundedRectangle(btnClearAllBank, 3.5f, 1.0f);
    g.setColour(juce::Colour(0xffc3cdd8));
    g.drawText("CLEAR ALL BANK", btnClearAllBank, juce::Justification::centred, false);

    g.setColour(juce::Colour(0xff74a87e));
    g.fillRoundedRectangle(btnDragAllAudio, 3.5f);
    g.setColour(juce::Colour(0xff9cd4a6));
    g.drawRoundedRectangle(btnDragAllAudio, 3.5f, 1.0f);
    g.setColour(juce::Colour(0xff0f1712));
    g.setFont(monoBold);
    g.drawText("DRAG ALL AS AUDIO ->", btnDragAllAudio, juce::Justification::centred, false);
}

void MelodicAtlasAudioProcessorEditor::drawPadSlot(juce::Graphics& g, int padIdx,
                                                   const juce::Rectangle<float>& r, bool isPlayingNow)
{
    const auto& padState = audioProcessor.getSamplerEngine().getPadState(padIdx);
    const auto* rec = audioProcessor.getSamplerEngine().getPadRecord(padIdx);
    auto audio = audioProcessor.getSamplerEngine().getPadAudio(padIdx);
    bool isSelected = (selectedPads.count(padIdx) > 0);
    bool isDropTarget = (hoveredDropPadIndex == padIdx);

    // Подсветка фона пэда при воспроизведении (в обоих режимах)
    juce::Colour bgCol = (rec != nullptr) ? juce::Colour(0xff202630) : juce::Colour(0xff1a1f27);
    if (isPlayingNow)
        bgCol = juce::Colour(0xff263d35);
    else if (isDropTarget)
        bgCol = juce::Colour(0xff2a3b34);

    g.setColour(bgCol);
    g.fillRoundedRectangle(r, 3.0f);

    // Контур пэда: ярко-изумрудный при игре, мягко-зелёный при выделении
    if (isPlayingNow)
    {
        g.setColour(juce::Colour(0xff62f5b5));
        g.drawRoundedRectangle(r, 3.0f, 2.2f);
    }
    else if (isSelected || isDropTarget)
    {
        g.setColour(juce::Colour(0xff84c99c));
        g.drawRoundedRectangle(r, 3.0f, 1.5f);
    }
    else
    {
        g.setColour(juce::Colour(0xff2e3744));
        g.drawRoundedRectangle(r, 3.0f, 1.0f);
    }

    const juce::Font monoSmall(juce::Font::getDefaultMonospacedFontName(), 10.5f, juce::Font::plain);
    const juce::Font monoName (juce::Font::getDefaultMonospacedFontName(), 11.5f, juce::Font::bold);

    static const char* padMidiNames[12] = { "C1", "C#1", "D1", "D#1", "E1", "F1", "F#1", "G1", "G#1", "A1", "A#1", "B1" };
    juce::String topStr = juce::String(padIdx + 1).paddedLeft('0', 2) + " [" + padMidiNames[padIdx] + "]";
    g.setColour(isPlayingNow ? juce::Colour(0xff62f5b5) : juce::Colour(0xff7c8898));
    g.setFont(monoSmall);
    g.drawText(topStr, (int)r.getX() + 6, (int)r.getY() + 4, 68, 14, juce::Justification::centredLeft, false);

    // Индикатор активности в Chromatic режиме (● / ○)
    juce::Point<float> dotCenter(r.getRight() - 11.0f, r.getY() + 11.0f);
    if (rec != nullptr)
    {
        if (padState.chromaticActive)
        {
            g.setColour(isPlayingNow ? juce::Colour(0xff62f5b5) : juce::Colour(0xffe6c167));
            g.fillEllipse(dotCenter.x - 4.0f, dotCenter.y - 4.0f, 8.0f, 8.0f);
        }
        else
        {
            g.setColour(juce::Colour(0xff6b7787));
            g.drawEllipse(dotCenter.x - 4.0f, dotCenter.y - 4.0f, 8.0f, 8.0f, 1.4f);
        }
    }

    if (rec == nullptr || audio == nullptr)
    {
        g.setColour(juce::Colour(0xff697584));
        g.setFont(monoSmall);
        g.drawText("[+] DRAG SOUND", r.withTrimmedTop(8.0f).withTrimmedBottom(14.0f), juce::Justification::centred, false);

        g.setColour(juce::Colour(0xff4a5462));
        g.drawText("V:0dB  P:C  T:0st", r.reduced(6.0f, 3.0f).removeFromBottom(14.0f), juce::Justification::centredLeft, false);
        return;
    }

    // Мини-волна
    auto waveRect = r.reduced(7.0f, 18.0f).withTrimmedBottom(12.0f);
    juce::Colour waveCol = Atlas::MusicMath::getClusterColour(rec->cluster, 0.80f);
    g.setColour(isPlayingNow ? waveCol.brighter(0.4f) : waveCol.withAlpha(0.82f));

    const auto& peaks = audio->waveformPeaks;
    const size_t step = 2;
    const float barW = waveRect.getWidth() / (float)(peaks.size() / step);
    const float midY = waveRect.getCentreY();

    for (size_t k = 0; k < peaks.size(); k += step)
    {
        float h = std::max(1.5f, peaks[k] * waveRect.getHeight() * 0.95f);
        float bx = waveRect.getX() + (float)(k / step) * barW;
        g.fillRect(bx, midY - h * 0.5f, std::max(1.0f, barW - 0.4f), h);
    }

    // Имя звука + живая строка параметров V / P / T внизу каждого пэда
    g.setColour(juce::Colours::white);
    g.setFont(monoName);
    g.drawText(rec->name.substring(0, 14), (int)r.getX() + 6, (int)r.getBottom() - 28, (int)r.getWidth() - 12, 14,
               juce::Justification::centredLeft, true);

    juce::String panStr = (std::abs(padState.pan) < 0.05f) ? "C"
                        : ((padState.pan < 0.0f) ? ("L" + juce::String((int)std::round(-padState.pan * 50.0f)))
                                                 : ("R" + juce::String((int)std::round(padState.pan * 50.0f))));
    juce::String tuneStr = (padState.semitoneOffset >= 0 ? "+" : "") + juce::String(padState.semitoneOffset) + "st";
    juce::String paramLine = "V:" + juce::String((int)std::round(padState.volumeDb)) + "dB P:" + panStr + " T:" + tuneStr;

    g.setColour(juce::Colour(0xff94a3b5));
    g.setFont(monoSmall);
    g.drawText(paramLine, (int)r.getX() + 6, (int)r.getBottom() - 14, (int)r.getWidth() - 12, 13,
               juce::Justification::centredLeft, false);
}

void MelodicAtlasAudioProcessorEditor::mouseDown(const juce::MouseEvent& e)
{
    auto pt = e.position;
    draggingPadIndex = -1;
    inlineEditPadIndex = -1;
    draggingBpmValue = false;

    if (pt.y <= 40.0f)
    {
        handleTopHeaderClick(pt);
        return;
    }
    if (pt.y <= 104.0f)
    {
        handleFilterBarClick(pt);
        return;
    }
    if (pt.y >= 404.0f)
    {
        handleBankClick(e);
    }
}

void MelodicAtlasAudioProcessorEditor::mouseDrag(const juce::MouseEvent& e)
{
    // Ручное изменение BPM перетаскиванием мыши вверх/вниз (когда замок BPM открыт)
    if (draggingBpmValue)
    {
        double delta = (double)(padDragStartPos.y - e.position.y) * 0.4;
        audioProcessor.currentBpm.store(juce::jlimit(60.0, 200.0, std::round((bpmDragStartVal + delta) * 2.0) * 0.5));
        repaint(640, 0, 140, 40);
        return;
    }

    // Редактирование V / P / T прямо на нижней полоске пэда
    if (inlineEditPadIndex >= 0)
    {
        float dy = padDragStartPos.y - e.position.y;
        auto& st = audioProcessor.getSamplerEngine().getPadState(inlineEditPadIndex);
        if (inlineEditParamType == 1)
            st.volumeDb = juce::jlimit(-24.0f, 6.0f, inlineEditStartVal + dy * 0.25f);
        else if (inlineEditParamType == 2)
            st.pan = juce::jlimit(-1.0f, 1.0f, inlineEditStartVal + dy * 0.015f);
        else if (inlineEditParamType == 3)
            st.semitoneOffset = juce::jlimit(-24, 24, (int)std::round(inlineEditStartVal + dy * 0.2f));

        syncKnobsFromSelectedPad();
        repaint();
        return;
    }

    if (e.position.getDistanceFrom(padDragStartPos) < 6.0f)
        return;

    if (btnDragAllAudio.contains(padDragStartPos))
    {
        std::vector<int> padsToExport;
        for (int i = 0; i < 12; ++i)
            if (audioProcessor.getSamplerEngine().getPadState(i).sampleIndex >= 0)
                padsToExport.push_back(i);
        exportPadsToDaw(padsToExport);
        return;
    }

    if (draggingPadIndex >= 0)
    {
        std::vector<int> padsToExport;
        if (selectedPads.count(draggingPadIndex) > 0)
        {
            for (int idx : selectedPads)
                padsToExport.push_back(idx);
        }
        else
        {
            padsToExport.push_back(draggingPadIndex);
        }

        draggingPadIndex = -1;
        exportPadsToDaw(padsToExport);
    }
}

void MelodicAtlasAudioProcessorEditor::mouseUp(const juce::MouseEvent&)
{
    draggingBpmValue = false;
    inlineEditPadIndex = -1;
    draggingPadIndex = -1;
}

void MelodicAtlasAudioProcessorEditor::mouseDoubleClick(const juce::MouseEvent& e)
{
    for (int i = 0; i < 12; ++i)
    {
        if (padRects[(size_t)i].contains(e.position))
        {
            audioProcessor.getSamplerEngine().getPadState(i).sampleIndex = -1;
            repaint();
            return;
        }
    }
}

void MelodicAtlasAudioProcessorEditor::showKeyPopupMenu()
{
    juce::PopupMenu m;
    int currentKey = audioProcessor.projectKeyRoot.load();
    for (int k = 0; k < 12; ++k)
        m.addItem(k + 1, Atlas::MusicMath::getNoteName(k), true, k == currentKey);

    m.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(localAreaToGlobal(btnKeyRoot.toNearestInt())),
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
        auto st = static_cast<Atlas::ScaleType>(s);
        m.addItem(s + 1, Atlas::MusicMath::getScaleFullName(st), true, s == currentScale);
    }

    m.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(localAreaToGlobal(btnScaleType.toNearestInt())),
                    [this](int res)
                    {
                        if (res >= 1 && res <= 10)
                        {
                            audioProcessor.projectScaleType.store(res - 1);
                            repaint();
                        }
                    });
}

void MelodicAtlasAudioProcessorEditor::handleTopHeaderClick(juce::Point<float> pt)
{
    if (btnOneShots.contains(pt))
    {
        audioProcessor.showLoopsTab.store(false);
        mapCanvas.setViewFilter(false, activeFilterCategory,
                                activeFilterCategory == Atlas::FilterCategory::Timbre ? activeTimbreMask : activeCharacterMask);
        repaint();
    }
    else if (btnLoops.contains(pt))
    {
        audioProcessor.showLoopsTab.store(true);
        mapCanvas.setViewFilter(true, activeFilterCategory,
                                activeFilterCategory == Atlas::FilterCategory::Timbre ? activeTimbreMask : activeCharacterMask);
        repaint();
    }
    else if (btnKeyRoot.contains(pt))
    {
        showKeyPopupMenu();
    }
    else if (btnScaleType.contains(pt))
    {
        showScalePopupMenu();
    }
    else if (btnPitchMode.contains(pt))
    {
        int nextMode = (audioProcessor.pitchModeIndex.load() + 1) % 3;
        audioProcessor.pitchModeIndex.store(nextMode);
        repaint();
    }
    else if (btnKeyLock.contains(pt))
    {
        audioProcessor.keyLocked.store(!audioProcessor.keyLocked.load());
        repaint();
    }
    else if (btnBpmLock.contains(pt))
    {
        audioProcessor.bpmLockedToHost.store(!audioProcessor.bpmLockedToHost.load());
        repaint();
    }
    else if (btnBpmValue.contains(pt) && !audioProcessor.bpmLockedToHost.load())
    {
        draggingBpmValue = true;
        padDragStartPos = pt;
        bpmDragStartVal = audioProcessor.currentBpm.load();
    }
    else if (btnScanFolder.contains(pt))
    {
        openFolderChooser();
    }
}

void MelodicAtlasAudioProcessorEditor::handleFilterBarClick(juce::Point<float> pt)
{
    if (btnModeTimbre.contains(pt))
    {
        activeFilterCategory = Atlas::FilterCategory::Timbre;
        mapCanvas.setViewFilter(audioProcessor.showLoopsTab.load(), activeFilterCategory, activeTimbreMask);
        repaint();
        return;
    }
    if (btnModeCharacter.contains(pt))
    {
        activeFilterCategory = Atlas::FilterCategory::Character;
        mapCanvas.setViewFilter(audioProcessor.showLoopsTab.load(), activeFilterCategory, activeCharacterMask);
        repaint();
        return;
    }

    static const uint32_t timbreBits[5] = {
        Atlas::Timbre_Keys, Atlas::Timbre_Guitar, Atlas::Timbre_Vox, Atlas::Timbre_Synth, Atlas::Timbre_Strings
    };
    for (int i = 0; i < 5; ++i)
    {
        if (timbreTagRects[(size_t)i].contains(pt))
        {
            activeFilterCategory = Atlas::FilterCategory::Timbre;
            activeTimbreMask ^= timbreBits[i];
            mapCanvas.setViewFilter(audioProcessor.showLoopsTab.load(), activeFilterCategory, activeTimbreMask);
            repaint();
            return;
        }
    }

    static const uint32_t charBits[7] = {
        Atlas::Char_Warm, Atlas::Char_Deep, Atlas::Char_Ethy, Atlas::Char_Clean,
        Atlas::Char_Dirty, Atlas::Char_Pluck, Atlas::Char_Sustain
    };
    for (int i = 0; i < 7; ++i)
    {
        if (charTagRects[(size_t)i].contains(pt))
        {
            activeFilterCategory = Atlas::FilterCategory::Character;
            activeCharacterMask ^= charBits[i];
            mapCanvas.setViewFilter(audioProcessor.showLoopsTab.load(), activeFilterCategory, activeCharacterMask);
            repaint();
            return;
        }
    }

    if (btnOctDown.contains(pt))
    {
        int cur = audioProcessor.octaveShift.load();
        audioProcessor.octaveShift.store(juce::jlimit(-2, 2, cur - 1));
        repaint();
    }
    else if (btnOctUp.contains(pt))
    {
        int cur = audioProcessor.octaveShift.load();
        audioProcessor.octaveShift.store(juce::jlimit(-2, 2, cur + 1));
        repaint();
    }
}

void MelodicAtlasAudioProcessorEditor::handleBankClick(const juce::MouseEvent& e)
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
    if (btnClearAllBank.contains(pt))
    {
        audioProcessor.getSamplerEngine().clearAllPads();
        selectedPads.clear();
        repaint();
        return;
    }

    for (int i = 0; i < 12; ++i)
    {
        const auto& r = padRects[(size_t)i];
        if (r.contains(pt))
        {
            auto& padState = audioProcessor.getSamplerEngine().getPadState(i);
            primarySelectedPad = i;

            // Клик по точке ● / ○ в правом верхнем углу пэда
            juce::Rectangle<float> dotZone(r.getRight() - 24.0f, r.getY(), 24.0f, 22.0f);
            if (padState.sampleIndex >= 0 && dotZone.contains(pt))
            {
                padState.chromaticActive = !padState.chromaticActive;
                syncKnobsFromSelectedPad();
                repaint();
                return;
            }

            // Клик и перетаскивание по нижней полоске V / P / T внутри пэда
            if (padState.sampleIndex >= 0 && pt.y >= r.getBottom() - 16.0f)
            {
                selectedPads.clear();
                selectedPads.insert(i);
                syncKnobsFromSelectedPad();

                inlineEditPadIndex = i;
                float relX = (pt.x - r.getX()) / r.getWidth();
                if (relX < 0.36f)      { inlineEditParamType = 1; inlineEditStartVal = padState.volumeDb; }
                else if (relX < 0.68f) { inlineEditParamType = 2; inlineEditStartVal = padState.pan; }
                else                   { inlineEditParamType = 3; inlineEditStartVal = (float)padState.semitoneOffset; }
                repaint();
                return;
            }

            // Если пэд пустой, загружаем в него выбранный на карте сэмпл
            int activeMapIdx = mapCanvas.getActiveSampleIndex();
            const auto& db = audioProcessor.getSampleDatabase();
            if (padState.sampleIndex < 0 && activeMapIdx >= 0 && activeMapIdx < (int)db.size())
            {
                audioProcessor.getSamplerEngine().assignSampleToPad(i, activeMapIdx, db[(size_t)activeMapIdx]);
                selectedPads.clear();
                selectedPads.insert(i);
                syncKnobsFromSelectedPad();
                audioProcessor.getSamplerEngine().triggerPadFromUI(i);
                repaint();
                return;
            }

            if (e.mods.isCommandDown() || e.mods.isCtrlDown())
            {
                if (selectedPads.count(i) > 0)
                    selectedPads.erase(i);
                else
                    selectedPads.insert(i);
            }
            else
            {
                selectedPads.clear();
                selectedPads.insert(i);
            }

            syncKnobsFromSelectedPad();

            if (padState.sampleIndex >= 0)
            {
                draggingPadIndex = i;
                audioProcessor.getSamplerEngine().triggerPadFromUI(i);
            }

            repaint();
            return;
        }
    }
}

void MelodicAtlasAudioProcessorEditor::exportPadsToDaw(const std::vector<int>& padIndices)
{
    juce::StringArray filesToDrag;
    for (int idx : padIndices)
    {
        if (const auto* rec = audioProcessor.getSamplerEngine().getPadRecord(idx))
        {
            if (rec->file.existsAsFile())
                filesToDrag.addIfNotAlreadyThere(rec->file.getFullPathName());
        }
    }

    if (!filesToDrag.isEmpty())
        performExternalDragDropOfFiles(filesToDrag, false);
}

void MelodicAtlasAudioProcessorEditor::openFolderChooser()
{
    folderChooser = std::make_unique<juce::FileChooser>(
        "Select Sample Library Folder for Melodic-Atlas",
        juce::File::getSpecialLocation(juce::File::userHomeDirectory));

    folderChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                               [this](const juce::FileChooser& fc)
                               {
                                   auto folder = fc.getResult();
                                   if (folder.isDirectory())
                                       audioProcessor.scanFolderAsync(folder);
                               });
}

bool MelodicAtlasAudioProcessorEditor::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& f : files)
        if (juce::File(f).isDirectory())
            return true;
    return false;
}

void MelodicAtlasAudioProcessorEditor::filesDropped(const juce::StringArray& files, int, int)
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
        mapCanvas.setSampleDatabase(&audioProcessor.getSampleDatabase());
        repaint();
    }

    uint32_t currentMask = audioProcessor.getSamplerEngine().getActivePlayingPadsMask();
    if (currentMask != lastPlayingMask)
    {
        lastPlayingMask = currentMask;
        repaint(0, 420, getWidth(), 200);
    }

    if (audioProcessor.isCurrentlyScanning())
        repaint(880, 0, 200, 42);
}
