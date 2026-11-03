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

    // Правый клик по точке на карте — мгновенное добавление звука в выделенный пэд
    mapCanvas.onSampleRightClicked = [this](int sampleIdx)
    {
        if (selectedPads.empty())
            selectedPads.insert(primarySelectedPad);

        for (int pIdx : selectedPads)
        {
            audioProcessor.assignSampleToPadWithPreviewSettings(pIdx, sampleIdx);
            primarySelectedPad = pIdx;
        }
        syncKnobsFromSelectedPad();
        audioProcessor.getSamplerEngine().triggerPadFromUI(primarySelectedPad);
        repaint();
    };

    // Перетаскивание сэмпла в пэд работает строго по зажатию карточки волны
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
                audioProcessor.assignSampleToPadWithPreviewSettings(overPad, sampleIdx);
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

    // Глобальные и мастер-регуляторы
    setupKnob(previewTuneSlider, -12.0f, 12.0f, 0.0f, [this](float v){ audioProcessor.bankPitchSemitones.store(v); });
    setupKnob(mapReverbSlider,   0.0f,   1.0f,  audioProcessor.reverbAmount.load(),     [this](float v){ audioProcessor.reverbAmount.store(v); });
    setupKnob(bankReverbSlider,  0.0f,   1.0f,  audioProcessor.bankReverbAmount.load(), [this](float v){ audioProcessor.bankReverbAmount.store(v); });
    setupKnob(masterVolSlider,   -24.0f, 6.0f,  audioProcessor.masterVolumeDb.load(),   [this](float v){ audioProcessor.masterVolumeDb.store(v); });

    lastSeenDbVersion = audioProcessor.databaseVersion.load();
    startTimerHz(30);
}

MelodicAtlasAudioProcessorEditor::~MelodicAtlasAudioProcessorEditor()
{
    setLookAndFeel(nullptr);
}

void MelodicAtlasAudioProcessorEditor::resized()
{
    mapCanvas.setBounds(14, 134, getWidth() - 28, 264);

    // Верхние и мастер-крутилки
    previewTuneSlider.setBounds(654, 48, 44, 44);
    mapReverbSlider.setBounds  (718, 48, 44, 44);
    bankReverbSlider.setBounds (782, 48, 44, 44);
    masterVolSlider.setBounds  (846, 48, 44, 44);

    // Сетка 12 пэдов внизу
    const float padStartX = 14.0f;
    const float padStartY = 416.0f;
    const float padW = 138.0f;
    const float padH = 68.0f;
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
    const juce::Font monoReg (juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::plain);

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

    // Кнопка KEY и лады
    g.setColour(juce::Colour(0xff8c98a8));
    g.drawText("KEY:", 334, 0, 32, 40, juce::Justification::centredLeft, false);

    btnKeyRoot         = { 368.0f, 8.0f, 42.0f, 24.0f };
    btnScaleType       = { 414.0f, 8.0f, 84.0f, 24.0f };
    btnPitchMode       = { 502.0f, 8.0f, 52.0f, 24.0f };
    btnTransposeQuality= { 558.0f, 8.0f, 70.0f, 24.0f };

    juce::String keyName = Atlas::MusicMath::getNoteName(audioProcessor.projectKeyRoot.load()) + " v";
    auto sType = static_cast<Atlas::ScaleType>(audioProcessor.projectScaleType.load());
    juce::String scaleStr = Atlas::MusicMath::getScaleShortName(sType);
    int pMode = audioProcessor.pitchModeIndex.load();
    juce::String modeStr = (pMode == 0) ? "[SNAP]" : (pMode == 1 ? "[ROOT]" : "[OFF]");
    int tQ = audioProcessor.transposeQualityIndex.load();
    juce::String qualStr = (tQ == 0) ? "[CLEAN]" : "[STRETCH]";

    drawPillButton(btnKeyRoot,          keyName,  true);
    drawPillButton(btnScaleType,        scaleStr, true);
    drawPillButton(btnPitchMode,        modeStr,  pMode != 2);
    drawPillButton(btnTransposeQuality, qualStr,  true);

    // Кнопка BPM без отдельного замка: горит = синхронизация с DAW активна, не горит = ручной режим
    g.setColour(juce::Colour(0xff8c98a8));
    g.drawText("BPM:", 634, 0, 34, 40, juce::Justification::centredLeft, false);

    btnBpmSync = { 668.0f, 8.0f, 64.0f, 24.0f };
    bool syncActive = audioProcessor.bpmLockedToHost.load();
    juce::String bpmTxt = syncActive ? juce::String(audioProcessor.currentBpm.load(), 1) : (juce::String(audioProcessor.currentBpm.load(), 1) + "*");
    drawPillButton(btnBpmSync, bpmTxt, syncActive, juce::Colour(0xff294236));

    // Надписи над крутилками
    g.setColour(juce::Colour(0xffc7d2de));
    g.setFont(monoReg);
    g.drawText("TUNE",  650, 0, 52, 40, juce::Justification::centred, false);
    g.drawText("MAP REV", 710, 0, 60, 40, juce::Justification::centred, false);
    g.drawText("BANK REV", 774, 0, 60, 40, juce::Justification::centred, false);
    g.drawText("MASTER", 838, 0, 60, 40, juce::Justification::centred, false);

    btnScanFolder = { 900.0f, 8.0f, 166.0f, 24.0f };
    juce::String scanText = audioProcessor.isCurrentlyScanning()
        ? ("SCANNING " + juce::String((int)(audioProcessor.getScanProgress() * 100.0f)) + "%")
        : "[SCAN SAMPLE FOLDER]";
    drawPillButton(btnScanFolder, scanText, audioProcessor.isCurrentlyScanning(), juce::Colour(0xff354839));
}

void MelodicAtlasAudioProcessorEditor::drawFilterBar(juce::Graphics& g)
{
    const juce::Font monoReg(juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::plain);

    g.setColour(juce::Colour(0xff161b22));
    g.fillRect(14, 46, getWidth() - 28, 80);
    g.setColour(juce::Colour(0xff262e3b));
    g.drawRect(14, 46, getWidth() - 28, 80, 1);

    auto drawTag = [&](juce::Rectangle<float> r, const juce::String& label, bool on)
    {
        g.setColour(on ? juce::Colour(0xff294236) : juce::Colour(0xff1e242d));
        g.fillRoundedRectangle(r, 2.5f);
        g.setColour(on ? juce::Colour(0xff7ec8a3) : juce::Colour(0xff323b48));
        g.drawRoundedRectangle(r, 2.5f, 1.0f);
        g.setColour(on ? juce::Colours::white : juce::Colour(0xff9aa6b5));
        g.setFont(monoReg);
        g.drawText(label, r, juce::Justification::centred, false);
    };

    // 1. Ряд основных групп карты ([ALL] + 8 групп)
    static const char* groupNames[9] = { "[ALL]", "[KEYS]", "[SYNTH]", "[BASS]", "[VOX]", "[STRINGS]", "[GUITAR]", "[FX]", "[OTHER]" };
    float gx = 24.0f;
    for (int i = 0; i < 9; ++i)
    {
        float w = (i == 0) ? 52.0f : 78.0f;
        groupFilterRects[(size_t)i] = { gx, 52.0f, w, 22.0f };
        bool on = (selectedClusterFilter == (i - 1));
        drawTag(groupFilterRects[(size_t)i], groupNames[i], on);
        gx += w + 6.0f;
    }

    // 2. Второй ряд: уточняющие подгруппы характера / фильтры
    static const char* subNames[7] = { "[CLEAN]", "[NOISY]", "[WARM]", "[PLUCK]", "[SUSTAIN]", "[RISER/SWEEP]", "[TEXTURAL]" };
    static const uint32_t subBits[7] = {
        Atlas::Sub_Clean, Atlas::Sub_Noisy, Atlas::Sub_Warm,
        Atlas::Sub_Pluck, Atlas::Sub_Sustain, Atlas::Sub_RiserSweep, Atlas::Sub_Textural
    };

    float sx = 24.0f;
    for (int i = 0; i < 7; ++i)
    {
        float w = (i == 5) ? 96.0f : 80.0f;
        subFilterRects[(size_t)i] = { sx, 82.0f, w, 22.0f };
        bool on = ((activeSubFilterMask & subBits[i]) != 0);
        drawTag(subFilterRects[(size_t)i], subNames[i], on);
        sx += w + 6.0f;
    }

    // Управление октавой предпрослушивания в правом углу фильтр-бара
    g.setColour(juce::Colour(0xff9aa6b5));
    g.drawText("OCT: (" + juce::String(audioProcessor.octaveShift.load()) + ")", 874, 82, 95, 22, juce::Justification::centredLeft, false);

    btnOctDown = { 968.0f, 82.0f, 44.0f, 22.0f };
    btnOctUp   = { 1016.0f, 82.0f, 44.0f, 22.0f };
    drawTag(btnOctDown, "[-]", audioProcessor.octaveShift.load() < 0);
    drawTag(btnOctUp,   "[+]", audioProcessor.octaveShift.load() > 0);
}

void MelodicAtlasAudioProcessorEditor::drawBankSection(juce::Graphics& g)
{
    const juce::Font monoBold(juce::Font::getDefaultMonospacedFontName(), 11.5f, juce::Font::bold);
    const juce::Font monoReg (juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::plain);

    g.setColour(juce::Colour(0xffd0dae5));
    g.setFont(monoBold);
    g.drawText("BANK:", 14, 396, 50, 20, juce::Justification::centredLeft, false);

    bool isChromatic = (audioProcessor.bankModeIndex.load() == (int)Atlas::BankMode::Chromatic);
    btnPadTriggerMode  = { 68.0f, 395.0f, 102.0f, 20.0f };
    btnChromaticMode   = { 176.0f, 395.0f, 108.0f, 20.0f };
    btnLockPadSettings = { 290.0f, 395.0f, 114.0f, 20.0f };

    auto drawBtn = [&](juce::Rectangle<float> r, const juce::String& text, bool active)
    {
        g.setColour(active ? juce::Colour(0xff294236) : juce::Colour(0xff1c222b));
        g.fillRoundedRectangle(r, 3.0f);
        g.setColour(active ? juce::Colour(0xff7ec8a3) : juce::Colour(0xff343e4d));
        g.drawRoundedRectangle(r, 3.0f, 1.0f);
        g.setColour(active ? juce::Colours::white : juce::Colour(0xff8e9aa8));
        g.setFont(monoReg);
        g.drawText(text, r, juce::Justification::centred, false);
    };

    drawBtn(btnPadTriggerMode,  "[PAD TRIGGER]", !isChromatic);
    drawBtn(btnChromaticMode,   "[CHROMATIC]",  isChromatic);
    drawBtn(btnLockPadSettings, audioProcessor.padSettingsLocked.load() ? "[LOCK PADS*]" : "[LOCK PADS]", audioProcessor.padSettingsLocked.load());

    uint32_t playingMask = audioProcessor.getSamplerEngine().getActivePlayingPadsMask();
    for (int i = 0; i < 12; ++i)
    {
        bool isPlayingNow = ((playingMask & (1u << i)) != 0);
        drawPadSlot(g, i, padRects[(size_t)i], isPlayingNow);
    }

    btnClearAllBank = { 14.0f,  596.0f, 142.0f, 30.0f };
    btnDragAllAudio = { 876.0f, 596.0f, 190.0f, 30.0f };

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

    bool isSelected = (primarySelectedPad == padIdx);
    bool isDropTarget = (hoveredDropPadIndex == padIdx);

    juce::Colour bgCol = (rec != nullptr) ? juce::Colour(0xff202630) : juce::Colour(0xff1a1f27);
    if (isPlayingNow)
        bgCol = juce::Colour(0xff263d35);
    else if (isDropTarget)
        bgCol = juce::Colour(0xff2a3b34);

    g.setColour(bgCol);
    g.fillRoundedRectangle(r, 3.0f);

    // Рамка пэда: изумрудная при игре, янтарно-золотистая при выделении для настройки
    if (isPlayingNow)
    {
        g.setColour(juce::Colour(0xff62f5b5));
        g.drawRoundedRectangle(r, 3.0f, 2.2f);
    }
    else if (isSelected || isDropTarget)
    {
        g.setColour(juce::Colour(0xffe6c167));
        g.drawRoundedRectangle(r, 3.0f, 1.8f);
    }
    else
    {
        g.setColour(juce::Colour(0xff2e3744));
        g.drawRoundedRectangle(r, 3.0f, 1.0f);
    }

    if (padState.settingsViewOpen)
    {
        drawPadGearSettingsView(g, padIdx, r, padState, audio);
    }
    else
    {
        drawPadNormalView(g, padIdx, r, padState, rec, audio, isPlayingNow);
    }
}

void MelodicAtlasAudioProcessorEditor::drawPadNormalView(juce::Graphics& g, int padIdx,
                                                         const juce::Rectangle<float>& r,
                                                         const Atlas::PadSlotState& padState,
                                                         const Atlas::SampleRecord* rec,
                                                         std::shared_ptr<Atlas::CachedAudioData> audio,
                                                         bool isPlayingNow)
{
    const juce::Font monoSmall(juce::Font::getDefaultMonospacedFontName(), 10.0f, juce::Font::plain);
    const juce::Font monoName (juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::bold);

    static const char* padMidiNames[12] = { "C1", "C#1", "D1", "D#1", "E1", "F1", "F#1", "G1", "G#1", "A1", "A#1", "B1" };
    juce::String topStr = juce::String(padIdx + 1).paddedLeft('0', 2) + " [" + padMidiNames[padIdx] + "]";
    g.setColour(isPlayingNow ? juce::Colour(0xff62f5b5) : juce::Colour(0xff7c8898));
    g.setFont(monoSmall);
    g.drawText(topStr, (int)r.getX() + 6, (int)r.getY() + 3, 62, 14, juce::Justification::centredLeft, false);

    // Кнопка шестерёнки ⚙ в правом верхнем углу пэда
    g.setColour(juce::Colour(0xff8a97a6));
    g.drawText("[⚙]", (int)r.getRight() - 26, (int)r.getY() + 2, 22, 16, juce::Justification::centred, false);

    // Стрелочки листания ◄ / ► и Undo (↶) и плюс (+)
    g.setColour(juce::Colour(0xff7a8694));
    g.drawText("◄", (int)r.getX() + 72, (int)r.getY() + 3, 14, 14, juce::Justification::centred, false);
    g.drawText("►", (int)r.getX() + 86, (int)r.getY() + 3, 14, 14, juce::Justification::centred, false);
    g.drawText("↶", (int)r.getX() + 102, (int)r.getY() + 3, 14, 14, juce::Justification::centred, false);
    g.drawText("+", (int)r.getX() + 116, (int)r.getY() + 3, 14, 14, juce::Justification::centred, false);

    if (rec == nullptr || audio == nullptr)
    {
        g.setColour(juce::Colour(0xff697584));
        g.setFont(monoSmall);
        g.drawText("[+] DRAG SOUND HERE", r.withTrimmedTop(16.0f).withTrimmedBottom(14.0f), juce::Justification::centred, false);
        return;
    }

    // Мини-волна в стиле XO
    auto waveRect = r.reduced(6.0f, 16.0f).withTrimmedBottom(10.0f);
    juce::Colour waveCol = Atlas::MusicMath::getClusterColour(rec->cluster, rec->toneShadeBucket);
    g.setColour(isPlayingNow ? waveCol.brighter(0.4f) : waveCol.withAlpha(0.82f));

    const auto& peaks = audio->waveformPeaks;
    if (!peaks.empty())
    {
        const size_t step = 2;
        const float barW = waveRect.getWidth() / (float)(peaks.size() / step);
        const float midY = waveRect.getCentreY();

        for (size_t k = 0; k < peaks.size(); k += step)
        {
            float h = std::max(1.5f, peaks[k] * waveRect.getHeight() * 0.92f);
            float bx = waveRect.getX() + (float)(k / step) * barW;
            g.fillRect(bx, midY - h * 0.5f, std::max(1.0f, barW - 0.4f), h);
        }
    }

    // Имя сэмплов и строка параметров внизу пэда
    g.setColour(juce::Colours::white);
    g.setFont(monoName);
    g.drawText(rec->name.substring(0, 14), (int)r.getX() + 6, (int)r.getBottom() - 25, (int)r.getWidth() - 12, 14,
               juce::Justification::centredLeft, true);

    juce::String panStr = (std::abs(padState.pan) < 0.05f) ? "C"
                        : ((padState.pan < 0.0f) ? ("L" + juce::String((int)std::round(-padState.pan * 50.0f)))
                                                 : ("R" + juce::String((int)std::round(padState.pan * 50.0f))));
    juce::String tuneStr = (padState.semitoneOffset >= 0 ? "+" : "") + juce::String(padState.semitoneOffset) + "st";
    juce::String paramLine = "V:" + juce::String((int)std::round(padState.volumeDb)) + "dB P:" + panStr + " T:" + tuneStr;

    g.setColour(juce::Colour(0xff94a3b5));
    g.setFont(monoSmall);
    g.drawText(paramLine, (int)r.getX() + 6, (int)r.getBottom() - 13, (int)r.getWidth() - 12, 13,
               juce::Justification::centredLeft, false);
}

void MelodicAtlasAudioProcessorEditor::drawPadGearSettingsView(juce::Graphics& g, int padIdx,
                                                               const juce::Rectangle<float>& r,
                                                               const Atlas::PadSlotState& padState,
                                                               std::shared_ptr<Atlas::CachedAudioData> audio)
{
    const juce::Font monoSmall(juce::Font::getDefaultMonospacedFontName(), 9.5f, juce::Font::plain);

    g.setColour(juce::Colour(0xffe6c167));
    g.setFont(monoSmall);
    g.drawText("PAD [" + juce::String(padIdx + 1).paddedLeft('0', 2) + "] SETTINGS [⚙]",
               (int)r.getX() + 6, (int)r.getY() + 3, 140, 14, juce::Justification::centredLeft, false);

    // Отрисовка мини-регуляторов прямо в пэде под шестерёнкой
    float dialY = r.getY() + 20.0f;
    float dialW = 22.0f;
    float startX = r.getX() + 6.0f;
    float stepX = 18.0f;

    drawMiniDial(g, { startX + stepX * 0.0f, dialY, dialW, dialW }, (padState.semitoneOffset + 24.0f) / 48.0f, "TUNE", juce::String(padState.semitoneOffset) + "st", true);
    drawMiniDial(g, { startX + stepX * 4.1f, dialY, dialW, dialW }, (padState.centOffset + 100.0f) / 200.0f, "CENT", juce::String(padState.centOffset) + "c", true);
    drawMiniDial(g, { startX + stepX * 8.2f, dialY, dialW, dialW }, (padState.volumeDb + 24.0f) / 30.0f, "VOL", juce::String((int)std::round(padState.volumeDb)) + "dB");
    drawMiniDial(g, { startX + stepX * 12.3f, dialY, dialW, dialW }, (padState.pan + 1.0f) * 0.5f, "PAN", "", true);

    // ADSR слайдеры в нижней части пэда
    float adsrY = r.getBottom() - 16.0f;
    g.setColour(juce::Colour(0xff8a97a6));
    juce::String adsrStr = "A:" + juce::String(padState.attackSec, 2) + " D:" + juce::String(padState.decaySec, 1) +
                           " S:" + juce::String(padState.sustainLvl, 1) + " R:" + juce::String(padState.releaseSec, 1);
    g.drawText(adsrStr, (int)r.getX() + 6, (int)adsrY, (int)r.getWidth() - 12, 14, juce::Justification::centredLeft, false);
}

void MelodicAtlasAudioProcessorEditor::drawMiniDial(juce::Graphics& g, juce::Rectangle<float> rect, float normVal,
                                                    const juce::String& label, const juce::String& valStr, bool)
{
    g.setColour(juce::Colour(0xff181d25));
    g.fillEllipse(rect);
    g.setColour(juce::Colour(0xff4a5666));
    g.drawEllipse(rect, 1.0f);

    float angle = -2.0944f + normVal * 4.18879f; // -120 .. +120 deg
    juce::Line<float> line(rect.getCentre(), rect.getCentre().getPointOnCircumference(rect.getWidth() * 0.35f, angle));
    g.setColour(juce::Colour(0xff7ec8a3));
    g.drawLine(line, 1.4f);

    g.setColour(juce::Colour(0xff9aa6b5));
    g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 8.0f, juce::Font::plain));
    g.drawText(label, rect.translated(0, 14), juce::Justification::centred, false);
}

void MelodicAtlasAudioProcessorEditor::mouseDown(const juce::MouseEvent& e)
{
    auto pt = e.position;
    currentPadDragAction = PadDragAction::None;
    activeDragPadIndex = -1;
    draggingBpmValue = false;

    if (pt.y <= 40.0f)
    {
        handleTopHeaderClick(pt);
        return;
    }
    if (pt.y <= 126.0f)
    {
        handleFilterBarClick(pt);
        return;
    }
    if (pt.y >= 392.0f)
    {
        handleBankClick(e);
    }
}

void MelodicAtlasAudioProcessorEditor::mouseDrag(const juce::MouseEvent& e)
{
    // Ручная настройка BPM перетаскиванием мыши вверх/вниз при выключенной синхронизации с DAW
    if (draggingBpmValue)
    {
        double delta = (double)(padDragStartPos.y - e.position.y) * 0.4;
        audioProcessor.currentBpm.store(juce::jlimit(60.0, 200.0, std::round((bpmDragStartVal + delta) * 2.0) * 0.5));
        repaint(630, 0, 110, 40);
        return;
    }

    // Редактирование параметров внутри пэда слайдерами
    if (currentPadDragAction != PadDragAction::None && activeDragPadIndex >= 0)
    {
        float dy = padDragStartPos.y - e.position.y;
        auto& st = audioProcessor.getSamplerEngine().getPadState(activeDragPadIndex);

        if (currentPadDragAction == PadDragAction::DialSemitones)
            st.semitoneOffset = juce::jlimit(-24, 24, (int)std::round(dragStartParamValue + dy * 0.25f));
        else if (currentPadDragAction == PadDragAction::DialCents)
            st.centOffset = juce::jlimit(-100, 100, (int)std::round(dragStartParamValue + dy * 1.5f));
        else if (currentPadDragAction == PadDragAction::DialVol)
            st.volumeDb = juce::jlimit(-24.0f, 6.0f, dragStartParamValue + dy * 0.25f);
        else if (currentPadDragAction == PadDragAction::DialPan)
            st.pan = juce::jlimit(-1.0f, 1.0f, dragStartParamValue + dy * 0.015f);

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
        mapCanvas.setViewFilter(false, selectedClusterFilter, activeSubFilterMask);
        repaint();
    }
    else if (btnLoops.contains(pt))
    {
        audioProcessor.showLoopsTab.store(true);
        mapCanvas.setViewFilter(true, selectedClusterFilter, activeSubFilterMask);
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
    else if (btnTransposeQuality.contains(pt))
    {
        int nextQ = (audioProcessor.transposeQualityIndex.load() + 1) % 2;
        audioProcessor.transposeQualityIndex.store(nextQ);
        repaint();
    }
    else if (btnBpmSync.contains(pt))
    {
        bool curSync = audioProcessor.bpmLockedToHost.load();
        if (curSync)
        {
            // Кликнули — выключаем синхронизацию (кнопка гаснет, включается ручной режим BPM)
            audioProcessor.bpmLockedToHost.store(false);
        }
        else
        {
            // Зажатие или клик при выключенной кнопке — включаем обратно Sync с DAW или открываем драг
            audioProcessor.bpmLockedToHost.store(true);
        }
        repaint(630, 0, 110, 40);
    }
    else if (!audioProcessor.bpmLockedToHost.load() && juce::Rectangle<float>(668, 8, 64, 24).contains(pt))
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
    // 1. Клик по группам карты ([ALL] + 8 групп)
    for (int i = 0; i < 9; ++i)
    {
        if (groupFilterRects[(size_t)i].contains(pt))
        {
            selectedClusterFilter = i - 1; // -1 = ALL, 0..7 = ClusterGroup
            mapCanvas.setViewFilter(audioProcessor.showLoopsTab.load(), selectedClusterFilter, activeSubFilterMask);
            repaint();
            return;
        }
    }

    // 2. Клик по подгруппам характера
    static const uint32_t subBits[7] = {
        Atlas::Sub_Clean, Atlas::Sub_Noisy, Atlas::Sub_Warm,
        Atlas::Sub_Pluck, Atlas::Sub_Sustain, Atlas::Sub_RiserSweep, Atlas::Sub_Textural
    };
    for (int i = 0; i < 7; ++i)
    {
        if (subFilterRects[(size_t)i].contains(pt))
        {
            activeSubFilterMask ^= subBits[i];
            mapCanvas.setViewFilter(audioProcessor.showLoopsTab.load(), selectedClusterFilter, activeSubFilterMask);
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
    if (btnLockPadSettings.contains(pt))
    {
        audioProcessor.padSettingsLocked.store(!audioProcessor.padSettingsLocked.load());
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

            // Клик по иконке шестерёнки ⚙ в правом верхнем углу пэда
            juce::Rectangle<float> gearZone(r.getRight() - 28.0f, r.getY(), 26.0f, 20.0f);
            if (gearZone.contains(pt))
            {
                padState.settingsViewOpen = !padState.settingsViewOpen;
                repaint();
                return;
            }

            // Клик по кнопке Undo (↶)
            juce::Rectangle<float> undoZone(r.getX() + 98.0f, r.getY(), 18.0f, 18.0f);
            if (undoZone.contains(pt))
            {
                audioProcessor.getSamplerEngine().undoPadSample(i);
                repaint();
                return;
            }

            // Клик по стрелочкам листания похожих звуков ◄ / ► прямо в пэде
            juce::Rectangle<float> leftArrowZone(r.getX() + 68.0f, r.getY(), 16.0f, 18.0f);
            juce::Rectangle<float> rightArrowZone(r.getX() + 84.0f, r.getY(), 16.0f, 18.0f);
            if (leftArrowZone.contains(pt))
            {
                audioProcessor.stepSimilarSampleInPad(i, -1);
                repaint();
                return;
            }
            if (rightArrowZone.contains(pt))
            {
                audioProcessor.stepSimilarSampleInPad(i, 1);
                repaint();
                return;
            }

            // Клик по кнопке [+] для назначения текущего выбранного на карте звука в пэд
            juce::Rectangle<float> plusZone(r.getX() + 112.0f, r.getY(), 18.0f, 18.0f);
            if (plusZone.contains(pt))
            {
                int activeMapIdx = mapCanvas.getActiveSampleIndex();
                const auto& db = audioProcessor.getSampleDatabase();
                if (activeMapIdx >= 0 && activeMapIdx < (int)db.size())
                {
                    audioProcessor.assignSampleToPadWithPreviewSettings(i, activeMapIdx);
                    audioProcessor.getSamplerEngine().triggerPadFromUI(i);
                    repaint();
                }
                return;
            }

            // Если открыты настройки шестерёнки — обрабатываем слайдеры
            if (padState.settingsViewOpen)
            {
                activeDragPadIndex = i;
                float relX = pt.x - r.getX();
                if (relX < 35.0f)       { currentPadDragAction = PadDragAction::DialSemitones; dragStartParamValue = (float)padState.semitoneOffset; }
                else if (relX < 70.0f)  { currentPadDragAction = PadDragAction::DialCents;     dragStartParamValue = (float)padState.centOffset; }
                else if (relX < 105.0f) { currentPadDragAction = PadDragAction::DialVol;       dragStartParamValue = padState.volumeDb; }
                else                    { currentPadDragAction = PadDragAction::DialPan;       dragStartParamValue = padState.pan; }
                repaint();
                return;
            }

            // Если пэд пустой, по клику назначаем текущий активный сэмпл с карты
            int activeMapIdx = mapCanvas.getActiveSampleIndex();
            const auto& db = audioProcessor.getSampleDatabase();
            if (padState.sampleIndex < 0 && activeMapIdx >= 0 && activeMapIdx < (int)db.size())
            {
                audioProcessor.assignSampleToPadWithPreviewSettings(i, activeMapIdx);
                audioProcessor.getSamplerEngine().triggerPadFromUI(i);
                repaint();
                return;
            }

            selectedPads.clear();
            selectedPads.insert(i);
            audioProcessor.getSamplerEngine().triggerPadFromUI(i);
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
        repaint(0, 400, getWidth(), 200);
    }

    if (audioProcessor.isCurrentlyScanning())
        repaint(900, 0, 180, 40);
}
