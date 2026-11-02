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

    // Перетаскивание точки с карты в пэд или напрямую в Ableton
    mapCanvas.onSampleDragged = [this](int sampleIdx, juce::Point<int> globalPos)
    {
        auto localPt = getLocalPoint(nullptr, globalPos).toFloat();
        const auto& db = audioProcessor.getSampleDatabase();
        if (sampleIdx < 0 || sampleIdx >= (int)db.size())
            return;

        // Если отпустили/повели на пэд — загружаем в пэд
        for (int i = 0; i < 12; ++i)
        {
            if (padRects[i].contains(localPt))
            {
                audioProcessor.getSamplerEngine().assignSampleToPad(i, sampleIdx, db[sampleIdx]);
                selectedPads.clear();
                selectedPads.insert(i);
                repaint();
                return;
            }
        }

        // Иначе запускаем внешний Drag-and-Drop .wav файла в Ableton Live
        if (db[sampleIdx].file.existsAsFile())
        {
            juce::StringArray files;
            files.add(db[sampleIdx].file.getFullPathName());
            performExternalDragDropOfFiles(files, false);
        }
    };

    auto setupKnob = [this](juce::Slider& s, float minV, float maxV, float defV, std::function<void(float)> onChange)
    {
        s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        s.setRange(minV, maxV);
        s.setValue(defV, juce::dontSendNotification);
        s.onValueChange = [this, &s, onChange]() { onChange((float)s.getValue()); };
        addAndMakeVisible(s);
    };

    setupKnob(reverbSlider,  0.0f,   1.0f, audioProcessor.reverbAmount.load(), [this](float v){ audioProcessor.reverbAmount.store(v); });
    setupKnob(attackSlider,  0.002f, 2.5f, audioProcessor.adsrAttack.load(),   [this](float v){ audioProcessor.adsrAttack.store(v); });
    setupKnob(decaySlider,   0.02f,  3.5f, audioProcessor.adsrDecay.load(),    [this](float v){ audioProcessor.adsrDecay.store(v); });
    setupKnob(sustainSlider, 0.0f,   1.0f, audioProcessor.adsrSustain.load(),  [this](float v){ audioProcessor.adsrSustain.store(v); });
    setupKnob(releaseSlider, 0.02f,  4.5f, audioProcessor.adsrRelease.load(),  [this](float v){ audioProcessor.adsrRelease.store(v); });
    setupKnob(glideSlider,   0.0f,   0.6f, audioProcessor.glideTime.load(),    [this](float v){ audioProcessor.glideTime.store(v); });

    lastSeenDbVersion = audioProcessor.databaseVersion.load();
    startTimerHz(24);
}

MelodicAtlasAudioProcessorEditor::~MelodicAtlasAudioProcessorEditor()
{
    setLookAndFeel(nullptr);
}

void MelodicAtlasAudioProcessorEditor::resized()
{
    // 1. Карта созвездий по центру
    mapCanvas.setBounds(14, 108, getWidth() - 28, 290);

    // 2. Крутилка REVERB в правой части панели фильтров
    reverbSlider.setBounds(818, 48, 52, 52);

    // 3. Сетка 12 пэдов (2 ряда по 6 колонок)
    const float padStartX = 14.0f;
    const float padStartY = 438.0f;
    const float padW = 142.0f;
    const float padH = 72.0f;
    const float gapX = 8.0f;
    const float gapY = 8.0f;

    for (int i = 0; i < 12; ++i)
    {
        int row = i / 6;
        int col = i % 6;
        float x = padStartX + (float)col * (padW + gapX);
        float y = padStartY + (float)row * (padH + gapY);
        padRects[i] = { x, y, padW, padH };
    }

    // 4. Блок крутилок ADSR & GLIDE справа от 12 пэдов
    const int knobBaseX = 918;
    const int knobBaseY = 436;
    attackSlider.setBounds (knobBaseX,      knobBaseY,      46, 46);
    decaySlider.setBounds  (knobBaseX + 52, knobBaseY,      46, 46);
    sustainSlider.setBounds(knobBaseX + 104,knobBaseY,      46, 46);
    releaseSlider.setBounds(knobBaseX + 18, knobBaseY + 76, 46, 46);
    glideSlider.setBounds  (knobBaseX + 82, knobBaseY + 76, 46, 46);
}

void MelodicAtlasAudioProcessorEditor::paint(juce::Graphics& g)
{
    // Базовый индустриальный темно-графитовый фон
    g.fillAll(juce::Colour(0xff14181f));

    drawTopHeader(g);
    drawFilterBar(g);
    drawBankSection(g);
}

void MelodicAtlasAudioProcessorEditor::drawTopHeader(juce::Graphics& g)
{
    const juce::Font monoBold(juce::Font::getDefaultMonospacedFontName(), 13.5f, juce::Font::bold);
    const juce::Font monoReg (juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain);

    g.setColour(juce::Colour(0xff181d25));
    g.fillRect(0, 0, getWidth(), 40);
    g.setColour(juce::Colour(0xff262e3b));
    g.drawHorizontalLine(40, 0.0f, (float)getWidth());

    // Логотип M MELODIC-ATLAS
    g.setColour(juce::Colour(0xff96b382));
    g.drawLine(16.0f, 27.0f, 22.0f, 13.0f, 2.0f);
    g.drawLine(22.0f, 13.0f, 26.0f, 23.0f, 2.0f);
    g.drawLine(26.0f, 23.0f, 31.0f, 13.0f, 2.0f);
    g.drawLine(31.0f, 13.0f, 36.0f, 27.0f, 2.0f);

    g.setColour(juce::Colour(0xffdce4ee));
    g.setFont(monoBold);
    g.drawText("MELODIC-ATLAS", 44, 0, 130, 40, juce::Justification::centredLeft, false);

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

    // Вкладки [ONE-SHOTS] и [LOOPS]
    bool loopsTab = audioProcessor.showLoopsTab.load();
    btnOneShots = { 184.0f, 8.0f, 92.0f, 24.0f };
    btnLoops    = { 282.0f, 8.0f, 70.0f, 24.0f };
    drawPillButton(btnOneShots, "[ONE-SHOTS]", !loopsTab);
    drawPillButton(btnLoops,    "[LOOPS]",     loopsTab);

    // Блок KEY / SCALE / SNAP / LOCK
    g.setColour(juce::Colour(0xff8c98a8));
    g.drawText("KEY:", 370, 0, 36, 40, juce::Justification::centredLeft, false);

    btnKeyRoot   = { 406.0f, 8.0f, 32.0f, 24.0f };
    btnScaleType = { 442.0f, 8.0f, 44.0f, 24.0f };
    btnPitchMode = { 490.0f, 8.0f, 54.0f, 24.0f };
    btnKeyLock   = { 548.0f, 8.0f, 56.0f, 24.0f };

    juce::String keyName = Atlas::MusicMath::getNoteName(audioProcessor.projectKeyRoot.load());
    juce::String scaleStr = audioProcessor.projectIsMinor.load() ? "[MIN]" : "[MAJ]";
    int pMode = audioProcessor.pitchModeIndex.load();
    juce::String modeStr = (pMode == 0) ? "[SNAP]" : (pMode == 1 ? "[ROOT]" : "[OFF]");

    drawPillButton(btnKeyRoot,   keyName,  true);
    drawPillButton(btnScaleType, scaleStr, true);
    drawPillButton(btnPitchMode, modeStr,  pMode != 2);
    drawPillButton(btnKeyLock,   audioProcessor.keyLocked.load() ? "[LOCK *]" : "[UNLOCK]", audioProcessor.keyLocked.load());

    // Блок BPM и SYNC
    g.setColour(juce::Colour(0xff8c98a8));
    g.drawText("BPM:", 618, 0, 36, 40, juce::Justification::centredLeft, false);

    juce::Rectangle<float> bpmBox(652.0f, 8.0f, 52.0f, 24.0f);
    btnBpmSync = { 708.0f, 8.0f, 74.0f, 24.0f };
    drawPillButton(bpmBox, juce::String(audioProcessor.currentBpm.load(), 1), true);
    drawPillButton(btnBpmSync, audioProcessor.syncToHostBpm.load() ? "[SYNC 1/1]" : "[FREE]", audioProcessor.syncToHostBpm.load());

    // Заголовок REVERB и кнопки OCTAVE / SCAN
    g.setColour(juce::Colour(0xffc7d2de));
    g.drawText("REVERB", 808, 0, 72, 40, juce::Justification::centred, false);

    btnScanFolder = { 892.0f, 8.0f, 174.0f, 24.0f };
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

    // Переключатели категории фильтра слева: ● [TIMBRE] / ○ [CHARACTER]
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

    // Верхний ряд тегов: [KEYS] [GUITAR] [VOX] [SYNTH] [STRINGS]
    static const char* timbreLabels[5] = { "[KEYS]", "[GUITAR]", "[VOX]", "[SYNTH]", "[STRINGS]" };
    static const uint32_t timbreBits[5] = {
        Atlas::Timbre_Keys, Atlas::Timbre_Guitar, Atlas::Timbre_Vox, Atlas::Timbre_Synth, Atlas::Timbre_Strings
    };

    float tx = 156.0f;
    for (int i = 0; i < 5; ++i)
    {
        timbreTagRects[i] = { tx, 51.0f, 86.0f, 21.0f };
        bool active = isTimbre && ((activeTimbreMask & timbreBits[i]) != 0);
        drawFilterTag(timbreTagRects[i], timbreLabels[i], active);
        tx += 94.0f;
    }

    // Нижний ряд тегов: [WARM] [DEEP] [ETHY] [CLEAN] [DIRTY] [PLUCK] [SUSTAIN]
    static const char* charLabels[7] = { "[WARM]", "[DEEP]", "[ETHY]", "[CLEAN]", "[DIRTY]", "[PLUCK]", "[SUSTAIN]" };
    static const uint32_t charBits[7] = {
        Atlas::Char_Warm, Atlas::Char_Deep, Atlas::Char_Ethy, Atlas::Char_Clean,
        Atlas::Char_Dirty, Atlas::Char_Pluck, Atlas::Char_Sustain
    };

    float cx = 156.0f;
    for (int i = 0; i < 7; ++i)
    {
        float w = (i == 6) ? 84.0f : 74.0f;
        charTagRects[i] = { cx, 76.0f, w, 21.0f };
        bool active = !isTimbre && ((activeCharacterMask & charBits[i]) != 0);
        drawFilterTag(charTagRects[i], charLabels[i], active);
        cx += w + 8.0f;
    }

    // Справа от крутилки REVERB: блок OCTAVE [-OCT] [+OCT]
    g.setColour(juce::Colour(0xff9aa6b5));
    g.drawText("OCTAVE: (" + juce::String(audioProcessor.octaveShift.load()) + ")", 892, 50, 95, 20, juce::Justification::centredLeft, false);

    btnOctDown = { 892.0f, 73.0f, 82.0f, 22.0f };
    btnOctUp   = { 982.0f, 73.0f, 82.0f, 22.0f };
    drawFilterTag(btnOctDown, "[-OCT]", audioProcessor.octaveShift.load() < 0);
    drawFilterTag(btnOctUp,   "[+OCT]", audioProcessor.octaveShift.load() > 0);
}

void MelodicAtlasAudioProcessorEditor::drawBankSection(juce::Graphics& g)
{
    const juce::Font monoBold(juce::Font::getDefaultMonospacedFontName(), 12.5f, juce::Font::bold);
    const juce::Font monoReg (juce::Font::getDefaultMonospacedFontName(), 11.5f, juce::Font::plain);

    // Заголовок BANK ENGINE: [PAD TRIGGER] (● CHROMATIC)
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

    // Отрисовка 12 пэдов
    for (int i = 0; i < 12; ++i)
        drawPadSlot(g, i, padRects[i]);

    // Подписи под крутилками ADSR & GLIDE
    g.setColour(juce::Colour(0xff9aa6b5));
    g.setFont(monoReg);
    g.drawText("A",     918,  482, 46, 16, juce::Justification::centred, false);
    g.drawText("D",     970,  482, 46, 16, juce::Justification::centred, false);
    g.drawText("S",     1022, 482, 46, 16, juce::Justification::centred, false);
    g.drawText("R",     936,  558, 46, 16, juce::Justification::centred, false);
    g.drawText("GLIDE", 1000, 558, 46, 16, juce::Justification::centred, false);

    // Нижние кнопки: [CLEAR ALL BANK] и контрастная зеленая [DRAG ALL AS AUDIO ⤓]
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

void MelodicAtlasAudioProcessorEditor::drawPadSlot(juce::Graphics& g, int padIdx, const juce::Rectangle<float>& r)
{
    const auto& padState = audioProcessor.getSamplerEngine().getPadState(padIdx);
    const auto* rec = audioProcessor.getSamplerEngine().getPadRecord(padIdx);
    auto audio = audioProcessor.getSamplerEngine().getPadAudio(padIdx);
    bool isSelected = (selectedPads.count(padIdx) > 0);

    // Фон карточки пэда
    g.setColour(rec != nullptr ? juce::Colour(0xff202630) : juce::Colour(0xff1a1f27));
    g.fillRoundedRectangle(r, 3.0f);

    g.setColour(isSelected ? juce::Colour(0xff84c99c) : juce::Colour(0xff2e3744));
    g.drawRoundedRectangle(r, 3.0f, isSelected ? 1.6f : 1.0f);

    const juce::Font monoSmall(juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::plain);
    const juce::Font monoName (juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::bold);

    // Номер слота 01..12
    juce::String numStr = juce::String(padIdx + 1).paddedLeft('0', 2);
    g.setColour(juce::Colour(0xff7c8898));
    g.setFont(monoSmall);
    g.drawText(numStr, (int)r.getX() + 7, (int)r.getY() + 5, 28, 15, juce::Justification::centredLeft, false);

    // Индикатор активности в Chromatic режиме (● или ○ в правом верхнем углу пэда)
    juce::Point<float> dotCenter(r.getRight() - 12.0f, r.getY() + 12.0f);
    if (rec != nullptr)
    {
        if (padState.chromaticActive)
        {
            g.setColour(juce::Colour(0xffe6c167));
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
        // Пустой слот
        g.setColour(juce::Colour(0xff697584));
        g.setFont(monoSmall);
        g.drawText("[+] DRAG SOUND", r.withTrimmedTop(8.0f).withTrimmedBottom(14.0f), juce::Justification::centred, false);

        g.setColour(juce::Colour(0xff4a5462));
        g.drawText("volume:     tuning", r.reduced(7.0f, 4.0f).removeFromBottom(15.0f), juce::Justification::centredLeft, false);
        return;
    }

    // Мини-волна внутри загруженного пэда
    auto waveRect = r.reduced(8.0f, 20.0f).withTrimmedBottom(4.0f);
    juce::Colour waveCol = Atlas::MusicMath::getKeyColour(rec->pitchClass, 0.82f);
    g.setColour(waveCol.withAlpha(0.75f));

    const auto& peaks = audio->waveformPeaks;
    const int step = 2;
    const float barW = waveRect.getWidth() / (float)(peaks.size() / step);
    const float midY = waveRect.getCentreY();

    for (size_t k = 0; k < peaks.size(); k += step)
    {
        float h = std::max(1.5f, peaks[k] * waveRect.getHeight() * 0.95f);
        float bx = waveRect.getX() + (float)(k / step) * barW;
        g.fillRect(bx, midY - h * 0.5f, std::max(1.0f, barW - 0.4f), h);
    }

    // Имя звука и MIDI-нота пэда внизу карточки
    static const char* padMidiNames[12] = { "C1", "C#1", "D1", "D#1", "E1", "F1", "F#1", "G1", "G#1", "A1", "A#1", "B1" };
    auto bottomRow = r.reduced(7.0f, 4.0f).removeFromBottom(16.0f);

    g.setColour(juce::Colours::white);
    g.setFont(monoName);
    g.drawText(rec->name.substring(0, 11), bottomRow.withTrimmedRight(36.0f), juce::Justification::centredLeft, true);

    g.setColour(juce::Colour(0xff9aa8b8));
    g.setFont(monoSmall);
    g.drawText(juce::String("[") + padMidiNames[padIdx] + "]", bottomRow, juce::Justification::centredRight, false);
}

void MelodicAtlasAudioProcessorEditor::mouseDown(const juce::MouseEvent& e)
{
    auto pt = e.position;
    draggingPadIndex = -1;

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
    if (e.position.getDistanceFrom(padDragStartPos) < 6.0f)
        return;

    // Если потянули кнопку [DRAG ALL AS AUDIO ->]
    if (btnDragAllAudio.contains(padDragStartPos))
    {
        std::vector<int> padsToExport;
        if (!selectedPads.empty())
        {
            for (int idx : selectedPads)
                padsToExport.push_back(idx);
        }
        else
        {
            for (int i = 0; i < 12; ++i)
                padsToExport.push_back(i);
        }
        exportPadsToDaw(padsToExport);
        return;
    }

    // Если потянули конкретный пэд (или группу выделенных через Cmd пэдов) в Ableton Live
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

        int padIdxToReset = draggingPadIndex;
        draggingPadIndex = -1;
        exportPadsToDaw(padsToExport);
        juce::ignoreUnused(padIdxToReset);
    }
}

void MelodicAtlasAudioProcessorEditor::mouseDoubleClick(const juce::MouseEvent& e)
{
    // Двойной клик по пэду очищает его
    for (int i = 0; i < 12; ++i)
    {
        if (padRects[i].contains(e.position))
        {
            audioProcessor.getSamplerEngine().getPadState(i).sampleIndex = -1;
            repaint();
            return;
        }
    }
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
        int nextKey = (audioProcessor.projectKeyRoot.load() + 1) % 12;
        audioProcessor.projectKeyRoot.store(nextKey);
        repaint();
    }
    else if (btnScaleType.contains(pt))
    {
        audioProcessor.projectIsMinor.store(!audioProcessor.projectIsMinor.load());
        repaint();
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
    else if (btnBpmSync.contains(pt))
    {
        audioProcessor.syncToHostBpm.store(!audioProcessor.syncToHostBpm.load());
        repaint();
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
        if (timbreTagRects[i].contains(pt))
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
        if (charTagRects[i].contains(pt))
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

    // Проверка клика по одному из 12 пэдов
    for (int i = 0; i < 12; ++i)
    {
        if (padRects[i].contains(pt))
        {
            auto& padState = audioProcessor.getSamplerEngine().getPadState(i);

            // Клик в правый верхний угол пэда переключает активность (● / ○) в CHROMATIC режиме
            juce::Rectangle<float> dotZone(padRects[i].getRight() - 24.0f, padRects[i].getY(), 24.0f, 24.0f);
            if (padState.sampleIndex >= 0 && dotZone.contains(pt))
            {
                padState.chromaticActive = !padState.chromaticActive;
                repaint();
                return;
            }

            // Если пэд пустой, а на карте выбран звук — загружаем этот звук в пэд в один клик
            int activeMapIdx = mapCanvas.getActiveSampleIndex();
            const auto& db = audioProcessor.getSampleDatabase();
            if (padState.sampleIndex < 0 && activeMapIdx >= 0 && activeMapIdx < (int)db.size())
            {
                audioProcessor.getSamplerEngine().assignSampleToPad(i, activeMapIdx, db[activeMapIdx]);
                selectedPads.clear();
                selectedPads.insert(i);
                audioProcessor.getSamplerEngine().triggerPadFromUI(i);
                repaint();
                return;
            }

            // Мультивыделение через Command / Ctrl
            if (e.mods.isCommandDown() || e.mods.isCtrlDown())
            {
                if (selectedPads.count(i) > 0)
                    selectedPads.erase(i);
                else
                    selectedPads.insert(i);
            }
            else
            {
                if (selectedPads.count(i) == 0)
                {
                    selectedPads.clear();
                    selectedPads.insert(i);
                }
            }

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

    if (audioProcessor.isCurrentlyScanning())
        repaint(880, 0, 200, 42);
}
