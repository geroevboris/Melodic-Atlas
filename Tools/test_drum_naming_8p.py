#!/usr/bin/env python3
"""Regression tests for the narrow literal filename rules in PercussionGate.h.

This checks the same literal patterns passed to JUCE String::contains in
isExplicitDrumFile. It is intentionally not an audio accuracy benchmark.
"""
import re
import unittest
from pathlib import Path

SOURCE = Path(__file__).resolve().parents[1] / "Source" / "PercussionGate.h"


def literal_patterns():
    text = SOURCE.read_text(encoding="utf-8")
    match = re.search(r"explicitPatterns\[\]\s*=\s*\{(.*?)\};", text, re.S)
    if match is None:
        raise AssertionError("C++ filename pattern array not found")
    without_comments = re.sub(r"//[^\n]*", "", match.group(1))
    return tuple(p.lower() for p in re.findall(r'"([^"]+)"', without_comments))


class TestDrumFilenamePatterns(unittest.TestCase):
    def test_positive_examples(self):
        patterns = literal_patterns()
        for filename in (
            "LOFI - Percloop 7 [95 BPM].wav",
            "130_FGPercs_SP_21.wav",
            "133_FGBeats_SP_15.wav",
            "EQ-B42 06 Cng Loop 090.wav",
            "EQ-B25 08 HhSd Loop 100.wav",
            "128_ManifoldDrums_01_698.wav",
            "72_4_BEAT_FILL_07.wav",
        ):
            with self.subTest(filename=filename):
                self.assertTrue(any(p in filename.lower() for p in patterns))

    def test_melodic_loop_examples(self):
        patterns = literal_patterns()
        for filename in (
            "TRKTRN_FMGSPBS_180_Electric_Guitar_Loop_OnlyYou_Rhythm_Emin.wav",
            "Cymatics - Omega Piano Loop 15 - 100 BPM G Min.wav",
            "120_A_MelodyLoop_04.wav",
            "LOFI - String Loop 1 [90 BPM].wav",
        ):
            with self.subTest(filename=filename):
                self.assertFalse(any(p in filename.lower() for p in patterns))

    def test_no_generic_rhythm_or_beat(self):
        self.assertNotIn("rhythm", literal_patterns())
        self.assertNotIn("groove", literal_patterns())
        self.assertNotIn("beat", literal_patterns())


if __name__ == "__main__":
    unittest.main()
