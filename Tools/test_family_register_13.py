#!/usr/bin/env python3
"""Map 13 contracts: regroup cached CLAP families without new inference."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
def read(path):
    return (ROOT / path).read_text(encoding="utf-8")

class FamilyRegister13(unittest.TestCase):
    def test_18_clap_families_not_72_forced_spectral_islands(self):
        layout = read("Source/SoundIslandLayout.h")
        self.assertIn("static constexpr int geometryVersion = 13;", layout)
        self.assertIn("FamilyRegisterLayout13::arrange(members)", layout)
        self.assertIn("rec.visualIslandGroup = rec.islandGroup;", layout)
        self.assertNotIn("rec.visualIslandGroup = rec.islandGroup * 4 + region;", layout)
        self.assertIn("rec.colourGroup = pt.zone;", layout)
        self.assertIn("rec.projectionX = rec.neuralMapX;", layout)

    def test_low_to_high_colour_follows_x_without_family_shear(self):
        engine = read("Source/FamilyRegisterLayout13.h")
        canvas = read("Source/MapCanvasGL.h")
        self.assertIn("output[index] = {x, y, zoneForX(x)};", engine)
        self.assertIn("const bool wholeFamilyLayout =", canvas)
        self.assertIn("wholeFamilyCount > 0 && legacySplitCount == 0;", canvas)
        self.assertIn("for (const auto& p : organic)", canvas)
        self.assertIn("ColourRegionBlend12::arrange(points, colourRegions)", canvas)
        self.assertIn("AtlasPointCoverage10::pack(packedSources,", canvas)

    def test_previous_cache_can_migrate_from_original_projection(self):
        processor = read("Source/PluginProcessor.cpp")
        self.assertIn("savedMapVersion < Atlas::SoundIslandLayout::geometryVersion", processor)
        self.assertIn("migratedGeometry = Atlas::SoundIslandLayout::apply(loaded);", processor)
        self.assertIn('item.getProperty("projectionX", -1.0f)', processor)
        self.assertIn('item.getProperty("projectionY", -1.0f)', processor)
        self.assertIn("if (projectedByNativeClap)", processor)
        self.assertIn("islandsReady = Atlas::SoundIslandLayout::apply(newRecords);", processor)

if __name__ == "__main__":
    unittest.main()
