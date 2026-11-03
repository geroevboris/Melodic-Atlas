#pragma once

#include "PluginProcessor.h"
#include "MapCanvasGL.h"

namespace Atlas
{
    class AtlasLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        AtlasLookAndFeel()
        {
            setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xff181e27));
            setColour(juce::PopupMenu::textColourId, juce::Colour(0xffdce5f0));
            setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(0xff2c473a));
            setColour(juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
        }

        void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                              float sliderPosProportional, float rotaryStartAngle,
                              float rotaryEndAngle, juce::Slider&) override
        {
            const float radius = (float)juce::jmin(width, height) * 0.39f;
            const float centreX = (float)x + (float)width * 0.5f;
            const float centreY = (float)y + (float)height * 0.5f;
            const float toAngle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);

            juce::Path bgArc;
            bgArc.addCentredArc(centreX, centreY, radius + 3.0f, radius + 3.0f,
                                0.0f, rotaryStartAngle, rotaryEndAngle, true);
            g.setColour(juce::Colour(0xff262e38));
            g.strokePath(bgArc, juce::PathStrokeType(2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            if (sliderPosProportional > 0.005f)
            {
                juce::Path valArc;
                valArc.addCentredArc(centreX, centreY, radius + 3.0f, radius + 3.0f,
                                     0.0f, rotaryStartAngle, toAngle, true);
                g.setColour(juce::Colour(0xff7ec8a3));
                g.strokePath(valArc, juce::PathStrokeType(2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }

            g.setColour(juce::Colour(0xff171c22));
            g.fillEllipse(centreX - radius, centreY - radius, radius * 2.0f, radius * 2.0f);
            g.setColour(juce::Colour(0xff384352));
            g.drawEllipse(centreX - radius, centreY - radius, radius * 2.0f, radius * 2.0f, 1.2f);

            juce::Path pointer;
            const float pointerLen = radius * 0.72f;
            pointer.addRectangle(-1.1f, -radius + 2.0f, 2.2f, pointerLen);
            g.setColour(juce::Colour(0xffe8eef5));
            g.fillPath(pointer, juce::AffineTransform::rotation(toAngle).translated(centreX, centreY));
        }
    };
}

class MelodicAtlasAudioProcessorEditor : public juce::AudioProcessorEditor,
                                         public juce::DragAndDropContainer,
                                         public juce::FileDragAndDropTarget,
                                         private juce::Timer
{
public:
    explicit MelodicAtlasAudioProcessorEditor(MelodicAtlasAudioProcessor&);
    ~MelodicAtlasAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

private:
    void timerCallback() override;

    void drawTopHeader(juce::Graphics& g);
    void drawFilterBar(juce::Graphics& g);
    void drawBankSection(juce::Graphics& g);
    void drawPadSlot(juce::Graphics& g, int padIdx, const juce::Rectangle<float>& r, bool isPlayingNow);
    void drawPadNormalView(juce::Graphics& g, int padIdx, const juce::Rectangle<float>& r,
                           const Atlas::PadSlotState& padState, const Atlas::SampleRecord* rec,
                           std::shared_ptr<Atlas::CachedAudioData> audio, bool isPlayingNow);
    void drawPadGearSettingsView(juce::Graphics& g, int padIdx, const juce::Rectangle<float>& r,
                                 const Atlas::PadSlotState& padState,
                                 std::shared_ptr<Atlas::CachedAudioData> audio);
    void drawMiniDial(juce::Graphics& g, juce::Rectangle<float> dialRect, float normVal,
                      const juce::String& label, const juce::String& valStr, bool bipolar = false);

    void handleTopHeaderClick(juce::Point<float> pt);
    void handleFilterBarClick(juce::Point<float> pt);
    void handleBankClick(const juce::MouseEvent& e);

    void showKeyPopupMenu();
    void showScalePopupMenu();
    void exportPadsToDaw(const std::vector<int>& padIndices);
    void openFolderChooser();

    MelodicAtlasAudioProcessor& audioProcessor;
    Atlas::AtlasLookAndFeel atlasLookAndFeel;
    Atlas::MapCanvasComponent mapCanvas;

    // Глобальные крутилки (верхняя панель + мастер-панель банка справа)
    juce::Slider previewTuneSlider; // Общая крутилка высоты тона для предпрослушивания
    juce::Slider mapReverbSlider;   // Реверб предпрослушивания на карте
    juce::Slider bankReverbSlider;  // Отдельная ручка реверба для банка с отобранными сэмплами
    juce::Slider glideSlider;       // Портаменто банка
    juce::Slider masterVolSlider;   // Выходная громкость

    // Фильтры единой карты: -1 = ВСЕ, 0..7 = конкретная группа (Keys, Synth, Bass, Vox, Strings, Guitar, FX, Other)
    int selectedClusterFilter = -1;
    uint32_t activeSubFilterMask = 0;

    std::set<int> selectedPads { 0 };
    int primarySelectedPad = 0;
    int hoveredDropPadIndex = -1;
    uint32_t lastPlayingMask = 0;

    // Интерактивное редактирование внутри пэда (под шестерёнкой ⚙ или перетаскивание)
    enum class PadDragAction
    {
        None = 0,
        DragPadToDaw,
        RegionStart,
        RegionEnd,
        DialSemitones,
        DialCents,
        DialVol,
        DialPan,
        DialAttack,
        DialDecay,
        DialSustain,
        DialRelease
    };

    PadDragAction currentPadDragAction = PadDragAction::None;
    int activeDragPadIndex = -1;
    float dragStartParamValue = 0.0f;
    juce::Point<float> padDragStartPos;

    // Ручная подстройка BPM при выключенной кнопке синхронизации
    bool draggingBpmValue = false;
    double bpmDragStartVal = 120.0;

    // Прямоугольники элементов интерфейса
    std::array<juce::Rectangle<float>, 12> padRects;
    juce::Rectangle<float> btnOneShots, btnLoops;
    juce::Rectangle<float> btnKeyRoot, btnScaleType, btnPitchMode, btnTransposeQuality;
    juce::Rectangle<float> btnBpmSync; // Единая кнопка BPM (горит = Sync ON, не горит = Sync OFF)
    juce::Rectangle<float> btnOctDown, btnOctUp, btnScanFolder;

    std::array<juce::Rectangle<float>, 9> groupFilterRects; // [ALL] + 8 групп
    std::array<juce::Rectangle<float>, 7> subFilterRects;   // 7 уточняющих подгрупп

    juce::Rectangle<float> btnPadTriggerMode, btnChromaticMode, btnLockPadSettings;
    juce::Rectangle<float> btnClearAllBank, btnDragAllAudio;

    uint64_t lastSeenDbVersion = 0;
    std::unique_ptr<juce::FileChooser> folderChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MelodicAtlasAudioProcessorEditor)
};
