#!/usr/bin/env python3
"""Check the shipped 9C compressed text bank without torch/JUCE or audio."""
import base64
import math
import re
import struct
import unittest
from pathlib import Path

HEADER = (Path(__file__).resolve().parents[1]
          / "Source" / "ClapTextBank9CData.h")

def parse_bank():
    text = HEADER.read_text(encoding="utf-8")
    match = re.search(r"static constexpr const char\* encodedQ15\s*=\s*(.*?);", text, re.S)
    if match is None:
        raise AssertionError("encodedQ15 not present")
    chunks = re.findall(r'"([A-Za-z0-9+/=]+)"', match.group(1))
    if not chunks:
        raise AssertionError("no base64 data")
    data = base64.b64decode("".join(chunks), validate=True)
    meta_match = re.search(r"static constexpr std::array<PromptInfo, promptCount> prompts\s*\{\{(.*?)\}\};", text, re.S)
    if meta_match is None:
        raise AssertionError("metadata not present")
    info = re.findall(
        r'\{\s*(\d+),\s*"([^"]+)",\s*"([^"]+)",\s*([0-9.eE+\-]+)f\s*\}',
        meta_match.group(1))
    return data, [(int(i), sub, prompt, float(scale)) for i,sub,prompt,scale in info]

class TestBank9C(unittest.TestCase):
    def test_dimensions_and_9_family_ids(self):
        data, meta = parse_bank()
        self.assertEqual(len(data), 52*512*2)
        self.assertEqual(len(meta), 52)
        families = set(m[0] for m in meta)
        self.assertEqual(families, set(range(9)))

    def test_quantized_norm_and_known_reference(self):
        data, meta = parse_bank()
        for prompt_id, (family, subtype, desc, scale) in enumerate(meta):
            self.assertTrue(math.isfinite(scale) and 0 < scale < 0.001)
            values = struct.unpack_from("<512h", data, prompt_id*512*2)
            floats = [x * scale for x in values]
            norm = math.sqrt(sum(x*x for x in floats))
            self.assertAlmostEqual(norm, 1.0, delta=0.001)
            self.assertTrue(subtype and desc)
        # First prompt is a piano vector; compare against the Mac verified bank.
        self.assertEqual(meta[0][1], "Piano")
        self.assertEqual(meta[0][0], 0)  # Keys
        self.assertAlmostEqual(struct.unpack_from("<h",data,0)[0]*meta[0][3],
                               0.00035262,delta=0.00001)
        self.assertAlmostEqual(struct.unpack_from("<h",data,2)[0]*meta[0][3],
                               -0.02808110,delta=0.00001)

    def test_winds_separate_from_strings(self):
        _, meta = parse_bank()
        self.assertEqual([m[0] for m in meta if m[1] in ("Brass","Woodwind")], [5,5,5,5])
        self.assertTrue(all(m[0]==4 for m in meta if m[1] in
                            ("Violin","Viola","Cello","StringsSection")))
        self.assertTrue(all(m[0]==8 for m in meta if m[1] in
                            ("Kick","Snare","Cymbal","Percussion","DrumLoop")))

if __name__ == "__main__":
    unittest.main()
