#!/usr/bin/env python3
"""Offline summary of Melodic Atlas 9D shadow report. Python stdlib only.

Usage: python Tools/analyze_shadow_9d.py ~/Library/Application\ Support/MelodicAtlas/ClassifierShadow_9D.csv
This ONLY compares candidate scores with old classifier decisions. It never
treats a CLAP top-1 label as ground truth or changes any sound or project.
"""
import argparse
import collections
import csv
import json
from pathlib import Path

REQUIRED = {
    "relative_path", "scan_stage", "legacy_is_loop", "old_identity_state",
    "old_instrument_family", "old_excluded_from_map", "clap_valid",
    "shadow_top_family", "shadow_top_cosine", "shadow_second_family",
    "shadow_margin", "shadow_drums_top1", "old_vs_shadow_family_disagree",
    "old_vs_shadow_drums_disagree", "onset_activity", "tempo_evidence",
    "loop_override", "shadow_drums_minus_melodic",
}
EXPECTED_STAGES = {
    "classified_before_final_gate",
    "early_dsp_gate_excluded",
    "explicit_filename_gate_excluded",
}

def load_report(path):
    with open(path, encoding="utf-8-sig", newline="") as stream:
        reader = csv.DictReader(stream)
        if reader.fieldnames is None:
            raise ValueError("Empty shadow CSV")
        missing = REQUIRED - set(reader.fieldnames)
        if missing:
            raise ValueError("Required shadow columns missing: " + ", ".join(sorted(missing)))
        rows = list(reader)
    for index, row in enumerate(rows, start=2):
        if row["scan_stage"] not in EXPECTED_STAGES:
            raise ValueError(f"Unexpected scan_stage on line {index}: {row['scan_stage']}")
    return rows

def numeric(value, default=None):
    try:
        return float(value)
    except (ValueError, TypeError):
        return default

def build_summary(rows, limit=50):
    stages = collections.Counter(r["scan_stage"] for r in rows)
    measured = [r for r in rows if r["clap_valid"] == "1"]
    old_known = [r for r in measured if r["old_instrument_family"]]
    drum_disagree = [r for r in measured if r["old_vs_shadow_drums_disagree"] == "1"]
    family_disagree = [r for r in old_known if r["old_vs_shadow_family_disagree"] == "1"]
    old_loops = [r for r in rows if r["legacy_is_loop"] == "1"]
    both = [r for r in measured if r["legacy_is_loop"] == "1" and r["shadow_drums_top1"] == "1"]

    # Review priority only. These are not correctness judgements.
    triage = []
    for row in rows:
        reasons = []
        priority = 0
        if row["scan_stage"] == "early_dsp_gate_excluded":
            reasons.append("excluded_before_clap")
            priority += 5
        elif row["scan_stage"] == "explicit_filename_gate_excluded":
            reasons.append("filename_exclusion_not_audio_checked")
            priority += 2
        if row["old_vs_shadow_drums_disagree"] == "1":
            reasons.append("drum_disagreement")
            priority += 6
        if row["old_vs_shadow_family_disagree"] == "1":
            reasons.append("family_disagreement")
            priority += 5
        margin = numeric(row["shadow_margin"])
        if margin is not None and margin < 0.025:
            reasons.append("close_candidate_scores")
            priority += 2
        if row["legacy_is_loop"] == "1":
            onset = numeric(row["onset_activity"])
            if onset is not None and onset >= 0.1:
                reasons.append("loop_with_multiple_transients_check_rhythm")
                priority += 2
            if row["shadow_drums_top1"] == "1":
                reasons.append("loop_drum_candidate_unconfirmed")
                priority += 4
        if row["loop_override"] not in ("", "-1", None):
            reasons.append("manual_loop_override")
            priority += 3
        if priority > 0:
            triage.append({**row, "review_priority": priority,
                           "review_reasons": "|".join(reasons)})
    triage.sort(key=lambda r: (-r["review_priority"], r["relative_path"].casefold()))
    summary = {
        "report_kind": "9D shadow hypotheses only; not accuracy",
        "rows_total": len(rows),
        "stages": dict(stages),
        "with_valid_text_clap": len(measured),
        "without_valid_text_clap": len(rows) - len(measured),
        "old_known_source_and_valid_clap": len(old_known),
        "source_disagreements": len(family_disagree),
        "percussion_disagreements": len(drum_disagree),
        "old_loop_rows": len(old_loops),
        "old_loop_and_clap_drums_top1": len(both),
        "shadow_top_family_counts": dict(collections.Counter(
            r["shadow_top_family"] for r in measured)),
        "triage_candidates": len(triage),
        "triage_exported": min(limit, len(triage)),
        "disclaimer": (
            "A disagreement is not a mistake; CLAP similarities are not probabilities. "
            "Early and explicit filename exclusions have no native CLAP vector. "
            "This report does not establish loop or rhythmic correctness."
        ),
    }
    return summary, triage[:limit]

def save_results(summary, triage, destination):
    destination.mkdir(parents=True, exist_ok=True)
    (destination / "ClassifierShadowSummary_9D.json").write_text(
        json.dumps(summary, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    if triage:
        with (destination / "ClassifierReviewQueue_9D.csv").open(
                "w", newline="", encoding="utf-8-sig") as out:
            columns = list(triage[0].keys())
            writer = csv.DictWriter(out, fieldnames=columns)
            writer.writeheader()
            writer.writerows(triage)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv", type=Path, help="ClassifierShadow_9D.csv")
    parser.add_argument("--output", type=Path, default=None)
    parser.add_argument("--limit", type=int, default=50)
    args = parser.parse_args()
    rows = load_report(args.csv)
    summary, triage = build_summary(rows, limit=max(0, args.limit))
    save_results(summary, triage, args.output or args.csv.parent)
    print(json.dumps(summary, ensure_ascii=False, indent=2))

if __name__ == "__main__":
    main()
