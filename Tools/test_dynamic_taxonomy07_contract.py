#!/usr/bin/env python3
"""Static integration contract for 07. Audio-specific accuracy is assessed
by separately run offline evaluations; this checks category/filter plumbing."""
import re
import unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def src(path):
    return (ROOT/path).read_text(encoding="utf-8")
class Dynamic07Contract(unittest.TestCase):
    def test_complete_fine_vocabulary(self):
        code=src("Source/AdaptiveTaxonomy07.h")
        block=re.search(r"static const char\* names\[typeCount\] = \{(.*?)\};",code,re.S)
        self.assertIsNotNone(block)
        labels=re.findall(r'"([^"]+)"',block.group(1))
        self.assertEqual(len(labels),65)
        self.assertEqual(len(set(labels)),65)
        self.assertIn("Neurobass",labels)
        self.assertIn("Impacts",labels)
        self.assertIn("Vibraphone",labels)
        self.assertIn("Processed Guitar",labels)
    def test_all_mains_and_zero_unknown_policy(self):
        code=src("Source/AdaptiveTaxonomy07.h")
        self.assertIn('rec.identityState = IdentityState::Identified;',code)
        self.assertIn('rec.automaticCategory=family;',code)
        self.assertIn('publishManual(rec);',code)
        self.assertIn('int familyCount = 7;',code)
        self.assertIn('DynamicTaxonomy07Policy::chooseFamily',code)
    def test_manual_reassignment_round_trip_plumbing(self):
        processor=src("Source/PluginProcessor.cpp")
        editor=src("Source/PluginEditor.cpp")
        atlas=src("Source/AtlasTypes.h")
        canvas=src("Source/MapCanvasGL.h")
        self.assertIn('manualCategory',atlas)
        for key in ('"manualCategory"','"manualSubgroup"',
                    'Atlas::AdaptiveTaxonomy07::classify(newRecords)',
                    'setSampleCategoryOverride('):
            self.assertIn(key,processor)
        self.assertIn('menu.addSubMenu("Move to...",moveTo)',editor)
        self.assertIn('Reset to automatic category',editor)
        self.assertIn('selectedDynamicSubgroups',editor)
        self.assertIn('activeDynamicSubgroups',canvas)
        self.assertIn('rec.dynamicSubgroup != requested',canvas)
    def test_cache_version_and_no_extra_neural_model(self):
        processor=src("Source/PluginProcessor.cpp")
        self.assertIn('"dynamicTaxonomyVersion"',processor)
        self.assertIn('"automaticCategory"',processor)
        self.assertIn('"automaticSubgroup"',processor)
        helper=src("Source/AdaptiveTaxonomy07.h")
        self.assertNotIn("torch",helper)
        self.assertNotIn("onnxruntime",helper)
if __name__=="__main__":
    unittest.main()
