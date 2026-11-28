#!/usr/bin/env python3
"""09 regression contracts for MIDI voice ownership and bounded discovery."""
import unittest
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
def source(name):
    return (ROOT/"Source"/name).read_text(encoding="utf-8")

class Navigation09(unittest.TestCase):
    def test_midi_voices_not_killed_by_mouse_release_or_editor_close(self):
        engine=source("SamplerEngine.h")
        editor=source("PluginEditor.cpp")
        start=engine.split("void releasePadFromUI(int padIdx)",1)[1].split(
            "uint32_t getActivePlayingPadsMask",1)[0]
        self.assertIn("v.triggeredByUI",start)
        self.assertIn("v.triggeredByUI == fromUI",engine)
        self.assertIn("triggerPadVoice(padIdx, 60, 0.9f, 0.0f, true);",engine)
        self.assertIn("chosen->triggeredByUI = fromUI;",engine)
        self.assertIn("!v.triggeredByUI && !v.releasing",engine)
        self.assertIn("releasePadFromUI(-1)",editor)
        self.assertIn("void triggerPadVoice",engine)
    def test_two_pad_rows_have_distinct_midi_octaves(self):
        engine=source("SamplerEngine.h")
        routing=source("PadMidiRouting12.h")
        self.assertIn("PadMidiRouting12::padForNote(midiNote)",engine)
        self.assertIn("if (padIdx >= 0)",engine)
        self.assertNotIn("((midiNote % 12) + 12) % 12",engine)
        self.assertIn("midiNote - 60",routing)
        self.assertIn("pitch - 6",routing)

    def test_bank_move_and_alt_copy_preserve_export_drag(self):
        editor=source("PluginEditor.cpp")
        header=source("PluginEditor.h")
        engine=source("SamplerEngine.h")
        self.assertIn("padDragCopyMode = e.mods.isAltDown()",editor)
        self.assertIn("transferPad(\n                        source, target, padDragCopyMode)",editor)
        self.assertIn("const auto padGrid = padRects[0].getUnion(",editor)
        self.assertIn("exportPadsToDaw(padsToExport);",editor)
        self.assertIn("padDragGestureActive",header)
        self.assertIn("bool transferPad(int source, int target, bool copy)",engine)
        self.assertIn("if (!copy)",engine)
        self.assertIn("padStates[(size_t)target].history = targetState.history;",engine)
        self.assertIn("padStates[(size_t)target].chromaticActive = targetState.chromaticActive;",engine)

    def test_history_right_click_uses_existing_pad_assignment(self):
        canvas=source("MapCanvasGL.h")
        editor=source("PluginEditor.cpp")
        history=canvas.split("if (pt.y >= mapBottomY)",1)[1].split(
            "int hit = findNearestSample(pt, 12.0f)",1)[0]
        self.assertIn("if (e.mods.isPopupMenu())",history)
        self.assertIn("onSampleRightClicked(histSampleIdx);",history)
        self.assertIn("triggerSamplePreview(histSampleIdx, false);",history)
        # History and the main map must use exactly the same assignment callback.
        self.assertIn("mapCanvas.onSampleRightClicked = [this](int sampleIdx)",editor)
        self.assertIn("audioProcessor.assignSampleToPadWithPreviewSettings(",editor)

    def test_selected_pad_sample_has_independent_top_layer_highlight(self):
        canvas=source("MapCanvasGL.h")
        select=canvas.split("void selectSampleFromPad(int sampleIdx)",1)[1].split(
            "std::function<void(int sampleIdx)> onSampleRightClicked",1)[0]
        self.assertIn("activeSampleIndex = sampleIdx;",select)
        self.assertIn("mapToScreenForIndex(sampleIdx)",select)
        self.assertIn("clampPanOffset();",select)
        # Selection must not rely on the semantic-zoom representative list.
        overlay=canvas.split("// The selected sample must never disappear",1)[1].split(
            "drawTopOverlayAndZoomControls(g, w);",1)[0]
        self.assertIn("mapToScreenForIndex(activeSampleIndex)",overlay)
        self.assertIn("g.drawEllipse(pt.x - radius - 2.4f",overlay)
        self.assertIn("g.fillEllipse(pt.x - radius",overlay)
        self.assertNotIn("visibleSampleIndices",overlay)

    def test_midi_key_lights_empty_pad_and_releases_with_note_off(self):
        engine=source("SamplerEngine.h")
        editor=source("PluginEditor.cpp")
        self.assertIn("getPressedMidiPadsMask() const",engine)
        self.assertIn("heldMidiPadNotes[(size_t)midiNote] = true;",engine)
        self.assertIn("heldMidiPadNotes[(size_t)midiNote] = false;",engine)
        self.assertIn("heldMidiPadNotes.fill(false);",engine)
        self.assertIn("pressedMidiPadsMask.store(0);",engine)
        self.assertIn("refreshPressedMidiPadsMask();",engine)
        self.assertIn("const uint32_t padFeedbackMask = playingMask | midiDownMask;",editor)
        self.assertIn("getPressedMidiPadsMask();",editor)
        # Held-pad feedback must occur before the sound-voice guard.
        handler=engine.split("void handleMidiNoteOn(int midiNote",1)[1].split(
            "void handleMidiNoteOff(int midiNote)",1)[0]
        self.assertLess(handler.index("heldMidiPadNotes[(size_t)midiNote] = true;"),
                        handler.index("triggerPadVoice(padIdx, midiNote"))

    def test_random_mode_header_and_restored_tabs(self):
        editor=source("PluginEditor.cpp")
        header=editor.split("void MelodicAtlasAudioProcessorEditor::drawTopHeader",1)[1].split(
            "void MelodicAtlasAudioProcessorEditor::drawFilterBar",1)[0]
        self.assertIn('"< RANDOM"',header)
        self.assertIn("drawBtn(btnOneShots",header)
        self.assertIn("drawBtn(btnLoops",header)
        self.assertIn("g.fillRoundedRectangle(btnDiscover",header)
        click=editor.split("void MelodicAtlasAudioProcessorEditor::handleTopHeaderClick",1)[1].split(
            "void MelodicAtlasAudioProcessorEditor::handleFilterBarClick",1)[0]
        self.assertIn("mapCanvas.endDiscovery()",click)
        self.assertIn("if (btnDiscover.contains(pt))",click)
        self.assertIn("if (mapCanvas.isDiscoveryActive()",click)
        filterbar=editor.split("void MelodicAtlasAudioProcessorEditor::drawFilterBar",1)[1].split(
            "void MelodicAtlasAudioProcessorEditor::drawBankAndRightPanel",1)[0]
        self.assertNotIn("g.fillRoundedRectangle(btnDiscover",filterbar)
    def test_bounded_zoom_reveal_and_reverse_cloud(self):
        canvas=source("MapCanvasGL.h")
        self.assertIn("rollDiscovery(int count = 200)",canvas)
        self.assertIn("discoveryVisibleCount() const",canvas)
        for literal in ("60,100,140,180,200","1.0f,1.5f,2.0f,3.0f,4.0f"):
            self.assertIn(literal,canvas)
        self.assertIn("juce::jmin(pool,shown)",canvas)
        self.assertIn("discoveryExitStartPixels",canvas)
        self.assertIn("discoveryReturning = true",canvas)
        self.assertIn("discoveryReturning = false",canvas)
        self.assertIn("visibleSampleIndices.assign(",canvas)
    def test_smooth_random_return_keeps_old_and_new_dot_layers(self):
        canvas=source("MapCanvasGL.h")
        self.assertIn("discoveryReturnStartZoom = zoomFactor;",canvas)
        self.assertIn("discoveryReturnStartMs",canvas)
        self.assertIn("(float)elapsed / 550.0f",canvas)
        self.assertIn("appendReturningDiscoverySamples();",canvas)
        self.assertIn("returningNormalIndices",canvas)
        self.assertIn("drawSoundIslands(g, fade);",canvas)
        # Returning never jumps to the old camera before animation begins.
        returning=canvas.split("void endDiscovery()",1)[1].split(
            "int discoveryVisibleCount()",1)[0]
        self.assertNotIn("zoomFactor = originalZoom;",returning)
        self.assertNotIn("panOffset = originalPan;",returning)

    def test_resize_debounces_expensive_packing_without_touching_discovery_timer(self):
        canvas=source("MapCanvasGL.h")
        resize=canvas.split("void resized() override",1)[1].split(
            "void paint(juce::Graphics& g) override",1)[0]
        self.assertIn("resizeSettleTimer.schedule();",resize)
        self.assertIn("coveragePositions.size() != database->size()",resize)
        self.assertIn("rebuildCoveragePositions();",resize)
        self.assertIn("rebuildIslandOverlays();",resize)
        self.assertIn("struct ResizeSettleTimer : private juce::Timer",canvas)
        self.assertIn("void schedule() { startTimer(180); }",canvas)
        self.assertIn("owner.rebuildCoveragePositions();",canvas)
        self.assertIn("owner.rebuildIslandOverlays();",canvas)
        self.assertIn("resizeSettleTimer.cancel();",canvas)
        self.assertIn("void timerCallback() override",canvas)

    def test_rollback_reconstructs_geometry_from_cached_clap(self):
        layout=source("SoundIslandLayout.h")
        processor=source("PluginProcessor.cpp")
        self.assertIn("static constexpr int geometryVersion = 9;",layout)
        self.assertIn("rec.visualIslandGroup = rec.islandGroup * 4 + region;",layout)
        self.assertIn(
            "savedMapVersion != Atlas::SoundIslandLayout::geometryVersion",
            processor)
        self.assertIn(
            "migratedGeometry = Atlas::SoundIslandLayout::apply(loaded);",
            processor)
        self.assertIn(
            "savedMapVersion == Atlas::SoundIslandLayout::geometryVersion",
            processor)
        self.assertNotIn("FamilyRegisterLayout13",layout)
        self.assertNotIn("familyRegister13",source("MapCanvasGL.h"))

    def test_geometric_view_transform_not_saved_to_cache(self):
        canvas=source("MapCanvasGL.h")
        self.assertIn("0.86f*(point.x-0.5f)",canvas)
        self.assertIn("1.38f*(point.y-0.5f)",canvas)
        self.assertIn("const auto position=coverageMapPosition(idx);",canvas)
        self.assertIn("effectiveMapCoordinates(rec);",canvas)
        processor=source("PluginProcessor.cpp")
        # The cache remains a copy of the actual stored 9F/UMAP geometry;
        # the new factors occur only in the canvas view code.
        self.assertNotIn("1.38f*(point.y-0.5f)",processor)

if __name__=="__main__":
    unittest.main()
