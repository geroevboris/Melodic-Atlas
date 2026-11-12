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
