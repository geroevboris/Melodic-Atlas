#!/usr/bin/env python3
"""Evaluate prior CLAP predictions against blind human labels.

Never guess category from a filename; compare only pre-existing user labels.
This is a *single-label* baseline, NOT a test of multi-instrument retrieval.
Cosine thresholds are descriptive, not probabilities or calibrated decisions.
"""
import argparse
import csv
import json
from collections import Counter
from pathlib import Path

def load_csv(path):
    with Path(path).open(encoding="utf-8-sig", newline="") as f:
        return list(csv.DictReader(f))

def evaluate(gold_csv, prediction_csv):
    gold = load_csv(gold_csv)
    guesses = load_csv(prediction_csv)
    g = {(r["id"],r["file"]):r for r in gold}
    p = {(r["id"],r["file"]):r for r in guesses}
    if len(g) != len(gold) or len(p) != len(guesses):
        raise ValueError("Duplicate sample IDs")
    if g.keys() != p.keys():
        raise ValueError("Gold and prediction files do not contain the same samples")
    items = []
    for key,row in g.items():
        label = row.get("your_category", "").strip()
        if not label:
            continue
        guess = p[key]
        items.append({"id":key[0], "file":key[1], "label":label,
                      "prediction":guess["model_prediction"].strip(),
                      "score":float(guess["model_similarity_NOT_probability"])})
    if not items:
        raise ValueError("No human annotations")
    by_prediction = {}
    for label in sorted({x["prediction"] for x in items}):
        matches = [x for x in items if x["prediction"] == label]
        correct = sum(x["label"] == label for x in matches)
        by_prediction[label] = {
            "predicted":len(matches), "correct":correct,
            "precision":round(correct/len(matches),4)}
    cutoffs = [0.0,0.2,0.25,0.3,0.35,0.4,0.45,0.5,0.55]
    sweep = []
    for threshold in cutoffs:
        chosen = [x for x in items if x["score"] >= threshold]
        matches = sum(x["label"] == x["prediction"] for x in chosen)
        sweep.append({"threshold":threshold,"accepted":len(chosen),
                      "correct":matches,"coverage":round(len(chosen)/len(items),4),
                      "precision":round(matches/len(chosen),4) if chosen else None})
    correct = sum(x["label"] == x["prediction"] for x in items)
    return {"count":len(items),"correct":correct,"strict_accuracy":round(correct/len(items),4),
            "human_label_counts":dict(Counter(x["label"] for x in items)),
            "prediction_precision":by_prediction,"threshold_sweep":sweep,
            "errors":[x for x in items if x["label"] != x["prediction"]],
            "warnings":[
                "The original blind set was stratified by predicted class.",
                "Unidentified includes clips the original label menu lacked a category for.",
                "Seven previously tuned examples are NOT independent test data.",
                "Cosine similarity is not a calibrated posterior probability.",
                "Single-label scoring understates correctly detected secondary instruments."
            ]}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--gold",required=True)
    parser.add_argument("--predictions",required=True)
    parser.add_argument("--output",required=True)
    args = parser.parse_args()
    report = evaluate(args.gold,args.predictions)
    out = Path(args.output)
    out.parent.mkdir(parents=True,exist_ok=True)
    out.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding="utf-8")
    print(f"Strict matches: {report['correct']}/{report['count']} ({report['strict_accuracy']:.0%})")
    for row in report["threshold_sweep"]:
        print(f"min score {row['threshold']:.2f}: "
              f"{row['correct']}/{row['accepted']} matched; "
              f"coverage {row['coverage']:.0%}")
    print("Report:",out)

if __name__=="__main__":
    main()
