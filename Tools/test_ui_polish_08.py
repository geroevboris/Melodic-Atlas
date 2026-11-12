#!/usr/bin/env python3
"""UI polish 08 static integration checks (JUCE compilation runs separately)."""
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
def read(path):
    return (ROOT/path).read_text(encoding="utf-8")

class Polish08Contract(unittest.TestCase):
    def test_exact_register_palette(self):
        src = read("Source/AtlasTypes.h").lower()
        for hexcode in ("0xfff52c29", "0xff3c83c2", "0xff05b183", "0xffffbf29"):
            self.assertIn(hexcode,src)
    def test_die_is_not_a_random_filter(self):
        editor = read("Source/PluginEditor.cpp")
        canvas = read("Source/MapCanvasGL.h")
        block = editor.split(
            "void MelodicAtlasAudioProcessorEditor::discoverRandomFilter()")[1].split(
            "void MelodicAtlasAudioProcessorEditor::showMoodsPopupMenu(")[0]
        self.assertIn("mapCanvas.rollDiscovery(36)",block)
        self.assertNotIn("selectedClustersMask",block)
        self.assertNotIn("selectedMoodMask",block)
        self.assertIn("discoveryPositions",canvas)
        self.assertIn("endDiscovery()",canvas)
        self.assertIn("if (!discoveryActive)",canvas)
        self.assertIn("visibleSampleIndices = discoveryIndices;",canvas)
    def test_three_sizes_without_glow(self):
        canvas=read("Source/MapCanvasGL.h")
        self.assertIn("sizeGroup < 62u",canvas)
        self.assertIn("sizeGroup < 88u",canvas)
        self.assertNotIn("haloR",canvas)
        self.assertIn("drawSoundIslands(g);",canvas)
    def test_character_only_four_extremes(self):
        atlas=read("Source/AtlasTypes.h")
        # AtlasTypes has more than one switch with ClusterGroup::Other;
        # anchor specifically to the Character dropdown, not the first
        # "Unknown" string conversion switch.
        text=atlas.split('{ "Clear Character"',1)[1].split("default:",1)[0]
        for s in ("Character_Bright","Character_Dark","Character_Pluck","Character_Sustain"):
            self.assertIn(s,text)
        for s in ("Character_Warm","Character_Clean","Character_Noisy",
                  "Character_Textural","Character_Rhythmic"):
            self.assertNotIn(s,text)
        self.assertIn("record.characterMask |= Character_Bright",
                      read("Source/CharacterAnalyzer.h"))
        self.assertIn("Atlas::CharacterAnalyzer::evaluate(rec);",
                      read("Source/PluginProcessor.cpp"))
    def test_default_off_and_numeric_overlay(self):
        processor=read("Source/PluginProcessor.h")
        sampler=read("Source/SamplerEngine.h")
        editor=read("Source/PluginEditor.cpp")
        for s in ("reverbAmount { 0.0f }","bankReverbAmount { 0.0f }"):
            self.assertIn(s,processor)
        for s in ("mapReverbAmt = 0.0f", "bankReverbAmt = 0.0f"):
            self.assertIn(s,sampler)
        for s in ("MAP REV: ","BANK REV: ","WET ","MAIN: ","mapCanvas.showParameterValue"):
            self.assertIn(s,editor)
    def test_popup_look_and_feel(self):
        header=read("Source/PluginEditor.h")
        self.assertIn("getPopupMenuFont() override",header)
        self.assertIn("drawPopupMenuBackground(",header)

if __name__ == "__main__":
    unittest.main()
