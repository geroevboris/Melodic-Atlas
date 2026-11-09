#!/usr/bin/env python3
"""Dependency-free smoke tests for 8M CLAP validation I/O and evaluation."""
import base64
import csv
import importlib.util
import struct
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent

def load(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / (name + ".py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module

def encode_juce_memory_block(raw):
    # Independent reference encoder following JUCE getBitRange(i*6, 6).
    alphabet = ".ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+"
    symbols = []
    for bit in range(0, len(raw)*8, 6):
        number = 0
        for offset in range(6):
            index = bit+offset
            if index < len(raw)*8:
                number |= ((raw[index//8] >> (index%8)) & 1) << offset
        symbols.append(alphabet[number])
    return str(len(raw)) + "." + "".join(symbols)

class ClapValidationTests(unittest.TestCase):
    def test_cached_native_vector_read(self):
        module = load("clap_zero_shot_experiment")
        blob = encode_juce_memory_block(struct.pack("<512f", *([0.03125] * 512)))
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "sample.xml"
            path.write_text(
                '<MelodicAtlasDatabaseCache>'
                '<Sample path="/audio/a.wav" clapEmbeddingValid="1" clapEmbedding="' + blob + '"/>'
                '<Sample path="/audio/b.wav" clapEmbeddingValid="0"/>'
                '</MelodicAtlasDatabaseCache>', encoding="utf-8")
            total, records = module.native_vectors(path)
            self.assertEqual(total, 2)
            self.assertEqual(len(records), 1)
            self.assertEqual(len(records[0][1]), 512)
            self.assertEqual(records[0][1][0], 0.03125)

    def test_juce_codec_roundtrip_for_2048_bytes(self):
        module = load("clap_zero_shot_experiment")
        data = bytes((n*29 + n//11) % 256 for n in range(2048))
        encoded = encode_juce_memory_block(data)
        self.assertTrue(encoded.startswith("2048."))
        self.assertEqual(module.decode_juce_memory_block(encoded), data)
        with self.assertRaises(ValueError):
            module.decode_juce_memory_block(encoded[:-1])
        with self.assertRaises(ValueError):
            module.decode_juce_memory_block("2047." + encoded.split(".",1)[1])

    def test_blind_score_and_coverage(self):
        module = load("evaluate_clap_blind")
        with tempfile.TemporaryDirectory() as tmp:
            g, p = Path(tmp) / "gold.csv", Path(tmp) / "pred.csv"
            with g.open("w", newline="") as f:
                w = csv.writer(f); w.writerow(["id","file","your_category"])
                w.writerows([["S01","a.wav","Synth"],["S02","b.wav","Bass"],["S03","c.wav","Keys"]])
            with p.open("w", newline="") as f:
                w = csv.writer(f); w.writerow(["id","file","model_prediction","model_similarity_NOT_probability"])
                w.writerows([["S01","a.wav","Synth","0.6"],["S02","b.wav","Drums","0.4"],["S03","c.wav","Keys","0.3"]])
            result = module.evaluate(g,p)
            self.assertEqual(result["count"],3)
            self.assertEqual(result["correct"],2)
            self.assertEqual(result["threshold_sweep"][-1]["accepted"],1)
            self.assertEqual(result["prediction_precision"]["Drums"]["correct"],0)

    def test_mismatched_labels_are_rejected(self):
        module = load("evaluate_clap_blind")
        with tempfile.TemporaryDirectory() as tmp:
            g, p = Path(tmp) / "gold.csv", Path(tmp) / "pred.csv"
            g.write_text("id,file,your_category\nS01,a.wav,Synth\n")
            p.write_text("id,file,model_prediction,model_similarity_NOT_probability\nS02,b.wav,Synth,0.7\n")
            with self.assertRaises(ValueError):
                module.evaluate(g,p)

if __name__ == "__main__":
    unittest.main()
