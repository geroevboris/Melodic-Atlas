#pragma once
#include "PluginProcessor.h"
#include "MapCanvasGL.h"

namespace Atlas {
    class AtlasLookAndFeel : public juce::LookAndFeel_V4 {
    public:
        AtlasLookAndFeel() {
            setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xff161b23));
            setColour(juce::PopupMenu::textColourId, juce::Colour(0xffdce5f0));
            setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(0xff294236));
            setColour(juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
        }
        void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height, float sliderPos, float rotStart, float rotEnd, juce::Slider& slider) override {
            float size = (float)juce::jmin(width, height); float radius = size * 0.42f;
            float cx = (float)x + width * 0.5f; float cy = (float)y + height * 0.5f;
            float toAngle = rotStart + sliderPos * (rotEnd - rotStart);
            juce::Path bg; bg.addCentredArc(cx, cy, radius + 2.5f, radius + 2.5f, 0.0f, rotStart, rotEnd, true);
            g.setColour(juce::Colour(0xff252d38)); g.strokePath(bg, juce::PathStrokeType(2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            bool isBipolar = (slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0);
            if (isBipolar) {
                float midAngle = (rotStart + rotEnd) * 0.5f;
                if (std::abs(toAngle - midAngle) > 0.02f) {
                    juce::Path valArc; valArc.addCentredArc(cx, cy, radius + 2.5f, radius + 2.5f, 0.0f, std::min(midAngle, toAngle), std::max(midAngle, toAngle), true);
                    g.setColour(juce::Colour(0xff7ec8a3)); g.strokePath(valArc, juce::PathStrokeType(2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                }
            } else if (sliderPos > 0.005f) {
                juce::Path valArc; valArc.addCentredArc(cx, cy, radius + 2.5f, radius + 2.5f, 0.0f, rotStart, toAngle, true);
                g.setColour(juce::Colour(0xff7ec8a3)); g.strokePath(valArc, juce::PathStrokeType(2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            g.setColour(juce::Colour(0xff171c23)); g.fillEllipse(cx - radius, cy - radius, radius * 2.0f, radius * 2.0f);
            g.setColour(slider.isMouseOverOrDragging() ? juce::Colour(0xff7ec8a3) : juce::Colour(0xff343f4e)); g.drawEllipse(cx - radius, cy - radius, radius * 2.0f, radius * 2.0f, 1.2f);
            g.setColour(juce::Colour(0xffdce6f2)); g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), std::max(8.5f, radius * 0.52f), juce::Font::bold));
            g.drawText(slider.getName(), juce::Rectangle<float>(cx - radius, cy - radius * 0.6f, radius * 2.0f, radius * 1.2f), juce::Justification::centred, false);
        }
    };
}

class MelodicAtlasAudioProcessorEditor : public juce::AudioProcessorEditor, public juce::SettableTooltipClient, public juce::DragAndDropContainer, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    explicit MelodicAtlasAudioProcessorEditor(MelodicAtlasAudioProcessor&);
    ~MelodicAtlasAudioProcessorEditor() override;
    void paint(juce::Graphics&) override; void resized() override;
    void mouseMove(const juce::MouseEvent& e) override; void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override; void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    bool isInterestedInFileDrag(const juce::StringArray& files) override; void filesDropped(const juce::StringArray& files, int x, int y) override;

private:
    void timerCallback() override; void updateDynamicLayoutRects();
    void drawTopHeader(juce::Graphics& g); void drawFilterBar(juce::Graphics& g); void drawBankAndRightPanel(juce::Graphics& g);
    void drawPadSlot(juce::Graphics& g, int padIdx, const juce::Rectangle<float>& r, bool isPlayingNow);
    void drawPadNormalView(juce::Graphics& g, int padIdx, const juce::Rectangle<float>& r, const Atlas::PadSlotState& padState, const Atlas::SampleRecord* rec, std::shared_ptr<Atlas::CachedAudioData> audio, bool isPlayingNow);
    void drawPadGearSettingsView(juce::Graphics& g, int padIdx, const juce::Rectangle<float>& r, const Atlas::PadSlotState& padState, std::shared_ptr<Atlas::CachedAudioData> audio);
    void drawMiniEncoder(juce::Graphics& g, juce::Rectangle<float> dialRect, float normVal, const juce::String& insideLabel, const juce::String& subValStr, bool bipolar = false);

    void drawUndoVectorIcon(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour col, bool isRedo = false);
    void drawGearVectorIcon(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour col);
    void drawArrowVectorIcon(juce::Graphics& g, juce::Rectangle<float> area, bool pointRight, juce::Colour col);
    void drawDropdownTriangle(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour col);
    void drawLockVectorIcon(juce::Graphics& g, juce::Rectangle<float> area, bool isLocked, juce::Colour col);
    void drawTrashVectorIcon(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour col);
    void drawExportVectorIcon(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour col);
    void drawPlusVectorIcon(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour col);
    void drawCloseVectorIcon(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour col);

    void handleTopHeaderClick(const juce::MouseEvent& e); void handleFilterBarClick(const juce::MouseEvent& e); void handleBankAndRightPanelClick(const juce::MouseEvent& e);
    void showKeyPopupMenu(); void showScalePopupMenu(); void showClusterSubgroupPopupMenu(int clusterIndex, juce::Rectangle<float> targetBtn);
    void exportPadsToDaw(const std::vector<int>& padIndices); void openFolderChooser(); void updateHoverTooltip(juce::Point<float> pt);

    MelodicAtlasAudioProcessor& audioProcessor; Atlas::AtlasLookAndFeel atlasLookAndFeel; Atlas::MapCanvasComponent mapCanvas;
    juce::TooltipWindow tooltipWindow { this, 300 };
    juce::Slider previewTuneSlider, mapReverbSlider, bankReverbSlider, masterVolSlider;

    int selectedClusterFilter = -1; uint32_t activeSubFilterMask = 0;
    std::array<uint32_t, 8> selectedClusterSubgroupBit { 0,0,0,0,0,0,0,0 };
    std::set<int> selectedPads { 0 }; int primarySelectedPad = 0; int hoveredDropPadIndex = -1; uint32_t lastPlayingMask = 0;

    enum class PadDragAction { None=0, DragPadToDaw, RegionStart, RegionEnd, DialSemitones, DialCents, DialVol, DialPan, DialAttack, DialDecay, DialSustain, DialRelease };
    PadDragAction currentPadDragAction = PadDragAction::None; int activeDragPadIndex = -1; float dragStartParamValue = 0.0f; juce::Point<float> padDragStartPos;
    bool draggingBpmValue = false; double bpmDragStartVal = 120.0;

    juce::Rectangle<float> btnGlobalUndo, btnGlobalRedo, btnOneShots, btnLoops, btnKeyRoot, btnScaleType, btnPitchMode, btnTransposeQuality;
    juce::Rectangle<float> btnBpmSync, btnBpmHalf, btnBpmDouble, btnScanFolder; 
    std::array<juce::Rectangle<float>, 9> groupFilterRects; std::array<juce::Rectangle<float>, 8> groupDropdownRects;
    juce::Rectangle<float> octLabelRect, btnOctDown, btnOctUp, btnPadTriggerMode, btnChromaticMode;
    std::array<juce::Rectangle<float>, 12> padRects;
    juce::Rectangle<float> rightBankPanelRect, btnLockPadsIcon, btnGlobalExportModeIcon, btnClearAllIcon, btnDragAllIcon;
    uint64_t lastSeenDbVersion = 0; std::unique_ptr<juce::FileChooser> folderChooser;
};
