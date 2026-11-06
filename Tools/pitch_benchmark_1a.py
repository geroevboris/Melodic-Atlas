#!/usr/bin/env python3
"""Melodic Atlas Pitch Benchmark 1A: legacy vs 6B, no plugin changes.

Dependencies: python3.11 -m pip install numpy soundfile
Usage: python3.11 Tools/pitch_benchmark_1a.py "/path/to/samples" --out "/path/to/report"
       python3.11 Tools/pitch_benchmark_1a.py --self-test
Outputs: all_results.csv, review_queue.csv, review.html, summary.txt.
"""
import argparse
import csv
import html
import math
import time
from pathlib import Path

import numpy as np
import soundfile as sf

EXTS = {".wav", ".aif", ".aiff", ".flac"}
NOTES = ("C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B")


def name_note(midi):
    if midi is None or not math.isfinite(midi):
        return "—"
    n = int(round(midi))
    return f"{NOTES[n % 12]}{n // 12 - 1}"


def read_excerpt(path):
    with sf.SoundFile(str(path)) as file:
        sr = int(file.samplerate)
        if sr < 8000 or file.frames <= 0:
            raise ValueError("unsupported sampling rate or empty audio")
        frames = min(file.frames, 6 * sr)
        channels = file.channels
        # Legacy checks the first channel. 6B uses the mono downmix.
        data = file.read(frames=frames, dtype="float32", always_2d=True)
    if data.shape[0] < 512:
        raise ValueError("too short")
    return data[:, 0], data.mean(axis=1), sr, channels


def legacy_pitch(first, sr):
    """Follows AudioAnalyzer::detectPitchAutocorr, including its limitations."""
    n = int(first.size)
    window = min(n, 2048)
    offset = min(max(0, n // 6), n - window)
    seg = np.asarray(first[offset:offset + window], dtype=np.float64)
    minlag = max(8, int(sr / 1000.0))
    maxlag = min(window // 2, int(sr / 45.0))
    length = window // 2
    energy = float(np.dot(seg[:length], seg[:length]) + 0.0001)
    if maxlag <= minlag:
        return None, 0.0
    # Vectorized (rather than hundreds of Python loops) for large libraries.
    correlations = np.array([
        np.dot(seg[:length], seg[lag:lag + length]) / energy
        for lag in range(minlag, maxlag)
    ])
    idx = int(np.argmax(correlations))
    lag = minlag + idx
    freq = sr / max(1, lag)
    midi = 69 + 12 * math.log2(max(20.0, freq) / 440.0)
    return float(np.clip(round(midi), 24, 96)), float(np.clip(correlations[idx], 0.1, 0.98))


def yin_window(x, rate):
    """Vectorized equivalent of the YIN-style CMND implementation in 6B.

    Same fixed-length difference and first sub-0.16 minimum; correlation is
    computed with an FFT to make reporting feasible for ~1,800 WAVs.
    """
    n = len(x)
    seglen = n // 2
    lo = max(7, int(rate / 1100.0))
    hi = min(n - seglen - 1, int(rate / 55.0))
    if hi <= lo:
        return None, 0.0
    head = np.asarray(x[:seglen], dtype=np.float64)
    energy = float(np.dot(head, head))
    if energy < seglen * 1e-8:
        return None, 0.0

    # diff[t] = sum_i (x[i] - x[i+t])^2
    #         = energy + rolling_energy[t] - 2 * correlation[t].
    v = np.asarray(x, dtype=np.float64)
    squared = np.concatenate(([0.0], np.cumsum(v * v)))
    rolling = squared[seglen:seglen + hi + 1] - squared[:hi + 1]
    fftsize = 1 << (n + seglen - 2).bit_length()
    cross = np.fft.irfft(
        np.conj(np.fft.rfft(head, fftsize)) * np.fft.rfft(v, fftsize),
        fftsize
    )[:hi + 1]
    diff = np.maximum(0.0, energy + rolling - 2.0 * cross)
    cmnd = np.ones(hi + 1, dtype=np.float64)
    accum = np.cumsum(diff[1:])
    lags = np.arange(1, hi + 1, dtype=np.float64)
    valid = accum > 1e-12
    cmnd[1:][valid] = diff[1:][valid] * lags[valid] / accum[valid]
    lag = 0
    t = lo
    while t < hi:
        if cmnd[t] < 0.16:
            while t + 1 <= hi and cmnd[t + 1] < cmnd[t]:
                t += 1
            lag = t
            break
        t += 1
    if not lag:
        return None, 0.0
    refined = float(lag)
    if 1 < lag < hi:
        left, center, right = cmnd[lag - 1:lag + 2]
        denom = left - 2 * center + right
        if abs(denom) > 1e-12:
            refined += float(np.clip(0.5 * (left - right) / denom, -0.5, 0.5))
    midi = 69 + 12 * math.log2(rate / refined / 440.0)
    if not math.isfinite(midi):
        return None, 0.0
    return midi, float(np.clip(1.0 - cmnd[lag], 0, 1))


def pitch_6b(mono, sr):
    stride = max(1, int(math.floor(sr / 12000.0 + 0.5)))
    count = len(mono) // stride
    if count < 768:
        return None, 0.0, 0.0, 0, "NoReliableTone"
    reduced = mono[:count * stride].reshape(count, stride).mean(axis=1)
    n = min(2048, count)
    available = count - n
    starts = (available // 10, available // 2, available * 9 // 10)
    measurements = [yin_window(reduced[s:s + n], sr / stride) for s in starts]
    measurements = [(pitch, strength) for pitch, strength in measurements
                    if pitch is not None and 24 <= pitch <= 96 and strength >= 0.52]
    valid = len(measurements)
    if not valid:
        return None, 0.0, 0.0, 0, "NoReliableTone"
    pitches = sorted(x[0] for x in measurements)
    median = pitches[valid // 2]
    periodicity = float(np.clip(np.mean([x[1] for x in measurements]), 0, 1))
    deviation = math.sqrt(sum((x - median) ** 2 for x in pitches) / valid) if valid >= 2 else 0
    stability = float(np.clip(1 - deviation / 1.25, 0, 1)) if valid >= 2 else 0
    evidence = periodicity * stability * valid / 3
    if valid == 3 and periodicity >= 0.75 and stability >= 0.9:
        state = "StableToneCandidate"
    elif valid < 2 or periodicity < 0.38:
        state = "NoReliableTone"
    else:
        state = "ComplexOrMoving"
    return median, evidence, periodicity, valid, state


def benchmark_file(path):
    first, mono, sr, channels = read_excerpt(path)
    start = time.perf_counter()
    old, old_conf = legacy_pitch(first, sr)
    legacy_ms = (time.perf_counter() - start) * 1000
    start = time.perf_counter()
    new, ev, periodicity, valid, state = pitch_6b(mono, sr)
    new_ms = (time.perf_counter() - start) * 1000
    disagreement = abs(old - round(new)) if new is not None and old is not None else None
    return {
        "file": str(path.resolve()), "sr": sr, "channels": channels,
        "legacy_note": name_note(old), "legacy_midi": old,
        "legacy_conf": round(old_conf, 4),
        "yin_note": name_note(new), "yin_midi": round(new, 3) if new is not None else "",
        "yin_evidence": round(ev, 4), "yin_periodicity": round(periodicity, 4),
        "yin_valid_windows": valid, "yin_state": state,
        "disagreement_st": disagreement if disagreement is not None else "",
        "legacy_ms": round(legacy_ms, 2), "yin_ms": round(new_ms, 2),
    }


FIELDS = ("file", "sr", "channels", "legacy_note", "legacy_midi",
          "legacy_conf", "yin_note", "yin_midi", "yin_evidence",
          "yin_periodicity", "yin_valid_windows", "yin_state",
          "disagreement_st", "legacy_ms", "yin_ms")


def write_csv(path, rows):
    with path.open("w", newline="", encoding="utf-8-sig") as f:
        w = csv.DictWriter(f, fieldnames=FIELDS)
        w.writeheader()
        w.writerows(rows)


def review_selection(rows, limit):
    # Mixed examples: disagreement, no estimate, stable candidates, other.
    buckets = [
        sorted([r for r in rows if isinstance(r["disagreement_st"], (int, float)) and
                r["disagreement_st"] >= 1], key=lambda r: -r["disagreement_st"]),
        [r for r in rows if r["yin_note"] == "—"],
        [r for r in rows if r["yin_state"] == "StableToneCandidate"],
        [r for r in rows if r["yin_state"] == "ComplexOrMoving"],
    ]
    selected, seen = [], set()
    # Round-robin, prevents one category dominating the review.
    while len(selected) < limit and any(buckets):
        for bucket in buckets:
            if not bucket:
                continue
            item = bucket.pop(0)
            if item["file"] not in seen:
                selected.append(item)
                seen.add(item["file"])
                if len(selected) == limit:
                    break
    return selected


def write_html(path, rows):
    # Links are file:// URLs: HTML should be opened locally.
    parts = ["""<!doctype html><html lang="ru"><meta charset="utf-8">
<title>Melodic Atlas · Pitch Review 1A</title>
<style>body{background:#111821;color:#f1f4f8;font:15px system-ui;max-width:1040px;margin:30px auto;padding:0 18px}
.card{background:#1c2633;border:1px solid #344053;border-radius:12px;padding:14px;margin:12px 0}
small{color:#aebdca} audio{display:block;width:100%;margin:10px 0}code{color:#bde7cf}
label{margin-right:20px}select{font-size:14px}</style>
<h1>Pitch Benchmark 1A — прослушивание</h1><p>Откройте этот HTML локально.
Сначала слушайте звук, затем оцените определение ноты. Поле оценки работает
в браузере, но результаты не сохраняются: записывайте их в review_queue.csv.</p>"""]
    for i, r in enumerate(rows, 1):
        u = Path(r["file"]).as_uri()
        parts.append(
            f'<div class="card"><strong>{i}. {html.escape(Path(r["file"]).name)}</strong>'
            f'<audio controls preload="none" src="{html.escape(u, quote=True)}"></audio>'
            f'<small>Legacy: <code>{html.escape(r["legacy_note"])}</code> '
            f'({r["legacy_conf"]}) · YIN: <code>{html.escape(r["yin_note"])}</code> '
            f'({r["yin_state"]}, evidence {r["yin_evidence"]}) · '
            f'разница: {r["disagreement_st"]} st</small></div>'
        )
    parts.append("</html>")
    path.write_text("\n".join(parts), encoding="utf-8")


def self_test():
    rate = 48000
    t = np.arange(rate, dtype=np.float64) / rate
    checks = []
    for note in [36, 48, 57, 60, 69, 81]:
        freq = 440 * 2 ** ((note - 69) / 12)
        for kind in ("sine", "harmonics"):
            x = np.sin(2 * np.pi * freq * t)
            if kind == "harmonics":
                x = 0.6 * x + 0.25 * np.sin(2 * np.pi * 2 * freq * t) + \
                    0.15 * np.sin(2 * np.pi * 3 * freq * t)
            p, strength, _, _, status = pitch_6b(x.astype(np.float32), rate)
            ok = p is not None and abs(p - note) < 0.4
            checks.append(ok)
            print(f"{kind:9s} expected={name_note(note):4s} "
                  f"detected={name_note(p):4s} state={status:22s} "
                  f"strength={strength:.3f} {'PASS' if ok else 'FAIL'}")
    noise = np.random.default_rng(2026).normal(size=rate).astype(np.float32) * .1
    p, _, _, _, status = pitch_6b(noise, rate)
    ok = p is None
    checks.append(ok)
    print(f"noise      expected=none detected={name_note(p)} "
          f"state={status} {'PASS' if ok else 'FAIL'}")
    print(f"\nSelf-test: {sum(checks)}/{len(checks)} passed")
    return all(checks)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawTextHelpFormatter)
    ap.add_argument("folder", nargs="?", type=Path, help="Folder with audio files")
    ap.add_argument("--out", type=Path, help="Report directory (default: sibling of samples)")
    ap.add_argument("--review", type=int, default=72, help="Review list length (default 72)")
    ap.add_argument("--limit", type=int, default=0, help="Only analyze first N files")
    ap.add_argument("--self-test", action="store_true", help="Synthetic known-note checks")
    args = ap.parse_args()
    if args.self_test:
        raise SystemExit(0 if self_test() else 1)
    if args.folder is None or not args.folder.is_dir():
        ap.error("Provide an existing sample folder, or --self-test")

    folder = args.folder.resolve()
    out = (args.out or folder.parent / (folder.name + "_pitch_benchmark_1a")).resolve()
    if out == folder or folder in out.parents:
        ap.error("Report directory must be outside the sample library")
    out.mkdir(parents=True, exist_ok=True)
    paths = sorted(p for p in folder.rglob("*") if p.is_file() and p.suffix.lower() in EXTS)
    if args.limit > 0:
        paths = paths[:args.limit]
    rows, failures = [], []
    t0 = time.perf_counter()
    for index, path in enumerate(paths, 1):
        try:
            rows.append(benchmark_file(path))
        except Exception as exc:
            failures.append((str(path), str(exc)))
        if index % 100 == 0 or index == len(paths):
            print(f"Scanned {index}/{len(paths)}; valid={len(rows)} errors={len(failures)}", flush=True)

    selected = review_selection(rows, max(0, args.review))
    write_csv(out / "all_results.csv", rows)
    write_csv(out / "review_queue.csv", selected)
    write_html(out / "review.html", selected)
    total = time.perf_counter() - t0
    notes = [
        f"Files: {len(paths)}, analyzed: {len(rows)}, failed: {len(failures)}",
        f"Wall time: {total:.2f}s",
        f"YIN stable candidates: {sum(r['yin_state'] == 'StableToneCandidate' for r in rows)}",
        f"YIN inconclusive: {sum(r['yin_state'] == 'NoReliableTone' for r in rows)}",
        f"Disagreements >=1st: {sum(isinstance(r['disagreement_st'], (int,float)) and r['disagreement_st'] >= 1 for r in rows)}",
        "NOTE: Algorithm disagreements do not establish correctness.",
        "NOTE: This script models 6B; it does not execute the compiled JUCE detector.",
        "NOTE: Confidence is uncalibrated; chords can appear as stable tones.",
        "NOTE: Filenames/metadata are not used as truth labels.",
    ]
    (out / "summary.txt").write_text("\n".join(notes) + "\n", encoding="utf-8")
    (out / "errors.txt").write_text("\n".join(f"{a}\t{b}" for a, b in failures), encoding="utf-8")
    print("\n".join(notes))
    print(f"\nReports: {out}")


if __name__ == "__main__":
    main()
