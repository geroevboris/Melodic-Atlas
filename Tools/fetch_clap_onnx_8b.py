#!/usr/bin/env python3
"""Build-time only model asset acquisition for offline Melodic Atlas distribution.

This script is NOT shipped to or run by the user. A release build downloads
exactly one pinned ONNX audio encoder, validates SHA-256 and file size, then
bundles it alongside each plugin binary once native inference is enabled.
"""
import argparse
import hashlib
import os
from pathlib import Path
import tempfile
import urllib.request

MODEL_URL = ("https://huggingface.co/lquint/clap-htsat-unfused-onnx/"
             "resolve/main/model.onnx")
MODEL_SHA256 = "0763d8c6d03fe1675a3905b96ae3ff9ebfe316e3c0af9b64f8658ece57f0d5a5"
MODEL_BYTES = 119654416
CHUNK_SIZE = 2 * 1024 * 1024


def verify(path: Path) -> bool:
    if not path.is_file() or path.stat().st_size != MODEL_BYTES:
        return False
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(CHUNK_SIZE), b""):
            digest.update(chunk)
    return digest.hexdigest() == MODEL_SHA256


def fetch(destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    if verify(destination):
        print("Pinned CLAP ONNX weights already cached and verified.")
        return
    # Never quietly trust an existing model of unknown provenance.
    if destination.exists():
        raise RuntimeError(f"Existing model fails checksum; remove it manually: {destination}")
    fd, temp = tempfile.mkstemp(prefix="atlas-clap-", suffix=".tmp", dir=destination.parent)
    try:
        written = 0
        digest = hashlib.sha256()
        with os.fdopen(fd, "wb") as out:
            request = urllib.request.Request(MODEL_URL, headers={
                "User-Agent": "MelodicAtlas-BuildModelFetcher/8B"
            })
            with urllib.request.urlopen(request, timeout=120) as source:
                while True:
                    chunk = source.read(CHUNK_SIZE)
                    if not chunk:
                        break
                    written += len(chunk)
                    if written > MODEL_BYTES:
                        raise RuntimeError("Model asset exceeds pinned expected size")
                    out.write(chunk)
                    digest.update(chunk)
            out.flush()
            os.fsync(out.fileno())
        if written != MODEL_BYTES or digest.hexdigest() != MODEL_SHA256:
            raise RuntimeError("Downloaded CLAP weights have invalid size or SHA-256")
        os.replace(temp, destination)
        print(f"Downloaded and verified pinned model: {destination}")
    finally:
        if os.path.exists(temp):
            os.unlink(temp)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination", type=Path,
                        help="Build asset path, e.g. build/assets/clap_audio.onnx")
    parser.add_argument("--verify-only", action="store_true",
                        help="Never download, only validate local asset")
    args = parser.parse_args()
    if args.verify_only:
        if not verify(args.destination):
            raise SystemExit("Missing or invalid CLAP model asset")
        print("SHA-256 verified.")
    else:
        fetch(args.destination)


if __name__ == "__main__":
    main()
