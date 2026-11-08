#!/bin/bash
# One-run experimental zero-shot benchmark against the plugin's saved CLAP cache.
# Does not rescan, move, rename or alter any WAV, VST, AU or cache.
set -euo pipefail
cd "$(dirname "$0")/.."

PYTHON="python3"
if command -v python3.11 >/dev/null 2>&1; then
  PYTHON="python3.11"
fi
echo "Melodic Atlas 8M — CLAP instrument benchmark"
echo "Uses cached native 512D vectors; does not rescan the library."
echo "Results: $HOME/Desktop/Atlas-CLAP-8M"
echo
"$PYTHON" Tools/clap_zero_shot_experiment.py \
  --output "$HOME/Desktop/Atlas-CLAP-8M" \
  --verify-native 8
echo
echo "Benchmark finished. Leave instrument filters unchanged pending validation."
