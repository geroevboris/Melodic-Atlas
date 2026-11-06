#!/usr/bin/env python3
"""Fail native release packaging if the AU/VST3 lacks model or CPU runtime."""
from pathlib import Path
import hashlib
import platform
import sys

EXPECTED = "0763d8c6d03fe1675a3905b96ae3ff9ebfe316e3c0af9b64f8658ece57f0d5a5"
SIZE = 119654416


def verify_model(path: Path) -> None:
    if path.stat().st_size != SIZE:
        raise RuntimeError(f"Missing/truncated model: {path}")
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(2 * 1024 * 1024), b""):
            digest.update(chunk)
    if digest.hexdigest() != EXPECTED:
        raise RuntimeError(f"Model checksum mismatch: {path}")


def main():
    root = Path("build/MelodicAtlas_artefacts/Release")
    if not root.is_dir():
        raise RuntimeError(f"Release plugin output not found: {root}")
    bundles = sorted(root.rglob("*.vst3"))
    # On Windows a VST3 may include a nested host architecture directory;
    # only bundle directories count.
    bundles = [p for p in bundles if p.is_dir()]
    if not bundles:
        raise RuntimeError("No VST3 plugin bundle was generated")
    for bundle in bundles:
        resources = bundle / "Contents" / "Resources"
        verify_model(resources / "clap_audio.onnx")
        if platform.system() == "Darwin":
            runtime = bundle / "Contents" / "Frameworks" / "libonnxruntime.dylib"
        else:
            runtime = bundle / "Contents" / "x86_64-win" / "onnxruntime.dll"
        if not runtime.is_file() or runtime.stat().st_size < 100_000:
            raise RuntimeError(f"ONNX Runtime missing: {runtime}")
        print(f"Self-contained bundle OK: {bundle}")
    print("Native VST3 package contains verified CLAP weights and runtime.")


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
