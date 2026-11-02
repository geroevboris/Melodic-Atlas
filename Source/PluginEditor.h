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
            bgArc.addCentredArc(centreX, centreY, radius + 3.2f, radius + 3.2f,
                                0.0f, rotaryStartAngle, rotaryEndAngle, true);
            g.setColour(juce::Colour(0xff262e38));
            g.strokePath(bgArc, juce::PathStrokeType(2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            if (sliderPosProportional > 0.005f)
            {
                juce::Path valArc;
                valArc.addCentredArc(centreX, centreY, radius + 3.2f, radius + 3.2f,
                                     0.0f, rotaryStartAngle, toAngle, true);
                g.setColour(juce::Colour(0xff7ec8a3));
                g.strokePath(valArc, juce::PathStrokeType(2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }

            g.setColour(juce::Colour(0xff171c22));
            g.fillEllipse(centreX - radius, centreY - radius, radius * 2.0f, radius * 2.0f);
            g.setColour(juce::Colour(0xff384352));
            g.drawEllipse(centreX - radius, centreY - radius, radius * 2.0f, radius * 2.0f, 1.2f);

            juce::Path pointer;
            const float pointerLen = radius * 0.72f;
            pointer.addRectangle(-1.1f, -radius + 2.2f, 2.2f, pointerLen);
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

    void handleTopHeaderClick(juce::Point<float> pt);
    void handleFilterBarClick(juce::Point<float> pt);
    void handleBankClick(const juce::MouseEvent& e);

    void showKeyPopupMenu();
    void showScalePopupMenu();
    void syncKnobsFromSelectedPad();
    void applyToTargetPads(std::function<void(Atlas::PadSlotState&)> fn);

    void exportPadsToDaw(const std::vector<int>& padIndices);
    void openFolderChooser();

    MelodicAtlasAudioProcessor& audioProcessor;
    Atlas::AtlasLookAndFeel atlasLookAndFeel;
    Atlas::MapCanvasComponent mapCanvas;

    // Верхние мастер-крутилки
    juce::Slider reverbSlider;
    juce::Slider masterVolSlider;

    // 8 крутилок инспектора пэда справа внизу (VOL, PAN, TUNE, GLIDE + A, D, S, R)
    juce::Slider padVolSlider;
    juce::Slider padPanSlider;
    juce::Slider padTuneSlider;
    juce::Slider glideSlider;

    juce::Slider attackSlider;
    juce::Slider decaySlider;
    juce::Slider sustainSlider;
    juce::Slider releaseSlider;

    Atlas::FilterCategory activeFilterCategory = Atlas::FilterCategory::Timbre;
    uint32_t activeTimbreMask = 0;
    uint32_t activeCharacterMask = 0;

    std::set<int> selectedPads { 0 };
    int primarySelectedPad = 0;
    int hoveredDropPadIndex = -1;
    uint32_t lastPlayingMask = 0;

    // Состояние перетаскивания параметров прямо на пэде или BPM
    int draggingPadIndex = -1;
    int inlineEditPadIndex = -1;
    int inlineEditParamType = 0; // 1 = Vol, 2 = Pan, 3 = Tune
    float inlineEditStartVal = 0.0f;
    bool draggingBpmValue = false;
    double bpmDragStartVal = 128.0;
    juce::Point<float> padDragStartPos;

    // Геометрия кнопок
    std::array<juce::Rectangle<float>, 12> padRects;
    juce::Rectangle<float> btnOneShots, btnLoops;
    juce::Rectangle<float> btnKeyRoot, btnScaleType, btnPitchMode, btnKeyLock;
    juce::Rectangle<float> btnBpmValue, btnBpmLock, btnOctDown, btnOctUp, btnScanFolder;
    juce::Rectangle<float> btnModeTimbre, btnModeCharacter;
    std::array<juce::Rectangle<float>, 5> timbreTagRects;
    std::array<juce::Rectangle<float>, 7> charTagRects;
    juce::Rectangle<float> btnPadTriggerMode, btnChromaticMode;
    juce::Rectangle<float> btnClearAllBank, btnDragAllAudio;

    uint64_t lastSeenDbVersion = 0;
    std::unique_ptr<juce::FileChooser> folderChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MelodicAtlasAudioProcessorEditor)
};
