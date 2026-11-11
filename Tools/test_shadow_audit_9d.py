#!/usr/bin/env python3
import csv
import json
import tempfile
import unittest
from pathlib import Path
from analyze_shadow_9d import REQUIRED, load_report, build_summary, save_results

class Test9DShadowAnalysis(unittest.TestCase):
    def sample(self, path, stage, *, clap="1", oldsource="", top="", loop="0",
               excluded="0", drumdiff="", familydiff="", onset="0.03", margin="0.05"):
        row = {field: "" for field in REQUIRED}
        row.update({
            "relative_path": path,
            "scan_stage": stage,
            "legacy_is_loop": loop,
            "old_identity_state": "1" if oldsource else "3",
            "old_instrument_family": oldsource,
            "old_excluded_from_map": excluded,
            "clap_valid": clap,
            "shadow_top_family": top,
            "shadow_top_cosine": "0.25" if clap == "1" else "",
            "shadow_second_family": "Keys" if clap == "1" else "",
            "shadow_margin": margin if clap == "1" else "",
            "shadow_drums_top1": ("1" if top == "Drums" else "0") if clap == "1" else "",
            "old_vs_shadow_family_disagree": familydiff,
            "old_vs_shadow_drums_disagree": drumdiff,
            "onset_activity": onset,
            "tempo_evidence": "0",
            "loop_override": "-1",
            "shadow_drums_minus_melodic": "0.02" if clap == "1" else "",
        })
        return row

    def test_counts_and_unobserved_early_rejections(self):
        rows = [
            self.sample("bass,loop.wav", "classified_before_final_gate",
                        oldsource="Bass", top="Drums", loop="1",
                        drumdiff="1", familydiff="1", onset="0.24", margin="0.014"),
            self.sample("Piano.wav", "classified_before_final_gate",
                        oldsource="Keys", top="Keys"),
            self.sample("A/removed_by_dsp.wav", "early_dsp_gate_excluded",
                        clap="0", excluded="1"),
            self.sample("Drums/Kick 111.wav", "explicit_filename_gate_excluded",
                        clap="0", excluded="1"),
            self.sample("unknown synth.wav", "classified_before_final_gate",
                        top="Synth", margin="0.002"),
        ]
        summary, triage = build_summary(rows, 50)
        self.assertEqual(summary["rows_total"], 5)
        self.assertEqual(summary["with_valid_text_clap"], 3)
        self.assertEqual(summary["old_known_source_and_valid_clap"], 2)
        self.assertEqual(summary["source_disagreements"], 1)
        self.assertEqual(summary["percussion_disagreements"], 1)
        self.assertEqual(summary["old_loop_and_clap_drums_top1"], 1)
        self.assertEqual(summary["stages"]["early_dsp_gate_excluded"], 1)
        self.assertIn("drum_disagreement", triage[0]["review_reasons"])
        self.assertEqual(triage[0]["relative_path"], "bass,loop.wav")
        self.assertEqual(build_summary(rows, 1)[0]["triage_exported"], 1)

    def test_csv_quoted_names_and_serialization(self):
        with tempfile.TemporaryDirectory() as temp:
            csvpath = Path(temp) / "ClassifierShadow_9D.csv"
            row = self.sample('FX/My "broken", sample.wav',
                              "classified_before_final_gate",
                              oldsource="FX", top="Vocal",
                              familydiff="1")
            with csvpath.open("w", encoding="utf-8-sig", newline="") as out:
                writer = csv.DictWriter(out, fieldnames=sorted(REQUIRED))
                writer.writeheader()
                writer.writerow(row)
            loaded = load_report(csvpath)
            self.assertEqual(loaded[0]["relative_path"], row["relative_path"])
            summary, queue = build_summary(loaded)
            save_results(summary, queue, Path(temp))
            saved = json.loads((Path(temp) / "ClassifierShadowSummary_9D.json").read_text())
            self.assertEqual(saved["source_disagreements"], 1)
            with (Path(temp) / "ClassifierReviewQueue_9D.csv").open(
                    encoding="utf-8-sig", newline="") as out:
                self.assertEqual(next(csv.DictReader(out))["relative_path"], row["relative_path"])

    def test_invalid_stage_and_missing_fields(self):
        with tempfile.TemporaryDirectory() as temp:
            target = Path(temp) / "broken.csv"
            target.write_text("relative_path,scan_stage\na.wav,other\n")
            with self.assertRaisesRegex(ValueError, "missing"):
                load_report(target)
            with target.open("w", newline="") as out:
                writer = csv.DictWriter(out, fieldnames=sorted(REQUIRED))
                writer.writeheader()
                writer.writerow(self.sample("a.wav", "unknown_stage"))
            with self.assertRaisesRegex(ValueError, "scan_stage"):
                load_report(target)

if __name__ == "__main__":
    unittest.main()
