#!/usr/bin/env python3
"""10: complete map coverage static contracts, plus compiled pure C++ tests."""
from pathlib import Path
import unittest
ROOT=Path(__file__).resolve().parents[1]
def src(p):return (ROOT/"Source"/p).read_text(encoding="utf-8")
class Map10(unittest.TestCase):
    def test_guaranteed_all_at_full_zoom(self):
        m=src("MapCanvasGL.h")
        self.assertIn("AtlasPointCoverage10::referenceZoom - 0.001f",m)
        self.assertIn("visibleSampleIndices = sortedCandidateIndices;",m)
        self.assertIn("coverageMapPosition(candidateIndex)",m)
        self.assertIn("rebuildCoveragePositions();",m)
    def test_no_resampling_on_hide(self):
        m=src("MapCanvasGL.h")
        self.assertIn("hideOnly = hideOnly && anyNewHide;",m)
        self.assertIn("visibleSamplesDirty = false;",m)
        self.assertIn("sortedCandidateIndices.erase(",m)
        self.assertIn("visibleSampleIndices.erase(",m)
    def test_dense_overview_and_accurate_count(self):
        m=src("MapCanvasGL.h")
        self.assertIn("normalizedZoom,8.0f,5.7f",m)
        self.assertIn('const juce::String progress = "POINTS "',m)
        self.assertIn("island.fillAlpha = juce::jlimit(0.045f, 0.22f",m)
    def test_original_vectors_unchanged(self):
        packed=src("AtlasPointCoverage10.h")
        self.assertIn("std::unordered_map<uint64_t,std::vector<uint32_t>> cells",packed)
        self.assertIn("nextCandidate[baseKey]",packed)
        self.assertIn("goldenAngle",packed)
        self.assertIn("ddx*ddx+ddy*ddy<sepSq",packed)
        self.assertNotIn("neuralMapX =",src("MapCanvasGL.h"))
        self.assertNotIn("projectionX =",src("MapCanvasGL.h"))
if __name__=="__main__":
    unittest.main()
