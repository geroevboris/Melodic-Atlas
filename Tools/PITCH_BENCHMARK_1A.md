# Pitch Benchmark 1A — Melodic Atlas

This is a **diagnostic** tool, not a plugin build or an auto-tuning algorithm.
It compares the legacy single-window autocorrelation detector with a
NumPy implementation of the scanner-only 6B YIN-like detector. The 6B
implementation's thresholds/window selection follow `TonalFeatureExtractor.h`.
The Python tool does **not** run the compiled C++ code; scores require
verification against the native scanner before promotion into playback.

## Run on macOS (M1)

Open Terminal in the repository folder:

```sh
python3.11 -m pip install numpy soundfile
python3.11 Tools/pitch_benchmark_1a.py --self-test
python3.11 Tools/pitch_benchmark_1a.py "/Volumes/SSD/Your Samples"
```

Replace the sample folder with the actual test-library path. For a small trial:

```sh
python3.11 Tools/pitch_benchmark_1a.py "/Volumes/SSD/Your Samples" --limit 50
```

Reports are written **outside** the library in a sibling folder ending
`_pitch_benchmark_1a`, unless `--out` is provided:

- `summary.txt`: timing and distribution of uncertain/candidate estimates
- `all_results.csv`: both algorithms' outputs for every scanned file
- `review_queue.csv`: up to 72 selected disagreements and controls
- `review.html`: local playback UI to audition the same review queue
- `errors.txt`: unsupported or unreadable files, if any

Open `review.html` from Finder. If the browser blocks local audio,
open the files directly in Finder using paths from `review_queue.csv`.
The review HTML does not save human labels; for a formal blind evaluation,
copy `review_queue.csv` and add columns for true note or “unknown”,
polyphonic/monophonic/atonal and whether YIN/Legacy is correct.

## Limitations

- Pitch confidence/evidence are uncalibrated heuristic scores, not
  probabilities.
- A stable harmonic chord can be labeled `StableToneCandidate`; this
  **must not** be interpreted as proven monophony.
- The baseline intentionally mirrors the old detector's flawed octave
  selection. MIDI pitch from filename overrides in older plugin versions
  are **not** reproduced by this baseline. Interpret differences accordingly.
- A disagreement between two estimators **does not establish** which one
  is right; reference tones and labeled listening are required.
- Actual project `In Key`, Tune and BPM Sync are **not changed**.
- It reads up to six seconds of each file. Supports WAV, AIFF and FLAC
  via libsndfile; files larger than six seconds are only partially inspected.
- The synthetic self-test checks isolated sine and harmonic-rich notes
  and noise. It does not validate real instruments, chords or polyphonic
  content, and does not benchmark JUCE C++ scanner throughput.
