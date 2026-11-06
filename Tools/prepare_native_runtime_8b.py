#!/usr/bin/env python3
"""Build-time ONNX Runtime acquisition for native offline CLAP artifacts.

Not distributed to end users. Runs only in CI/release packaging. The version
is pinned so link-time and runtime ABI match.
"""
import argparse
import hashlib
import os
from pathlib import Path
import shutil
import sys
import tarfile
import tempfile
import urllib.request
import zipfile

VERSION = "1.22.1"
MAC_VERSION = "1.22.0"
PLATFORMS = {
    "macos": f"onnxruntime-osx-universal2-{MAC_VERSION}.tgz",
    "windows": f"onnxruntime-win-x64-{VERSION}.zip",
}
KNOWN_SHA256 = {
    # SHA-256 published for the official Windows CPU release archive.
    "windows": "855276cd4be3cda14fe636c69eb038d75bf5bcd552bda1193a5d79c51f436dfe",
}


def run(system: str, base: Path) -> Path:
    name = PLATFORMS[system]
    root = base / name.rsplit(".", 1)[0]
    if root.is_dir() and (root / "include" / "onnxruntime_cxx_api.h").is_file():
        return root

    base.mkdir(parents=True, exist_ok=True)
    archive = base / name
    if not archive.is_file():
        fd, temp = tempfile.mkstemp(prefix="ort-download-", dir=base)
        try:
            with os.fdopen(fd, "wb") as out:
                release_version = MAC_VERSION if system == "macos" else VERSION
                release_url = ("https://github.com/microsoft/onnxruntime/releases/"
                               f"download/v{release_version}/{name}")
                req = urllib.request.Request(release_url,
                    headers={"User-Agent": "MelodicAtlas-Build/8B"})
                with urllib.request.urlopen(req, timeout=180) as response:
                    shutil.copyfileobj(response, out)
                out.flush()
                os.fsync(out.fileno())
            os.replace(temp, archive)
        finally:
            if os.path.exists(temp):
                os.unlink(temp)
    if system in KNOWN_SHA256:
        digest = hashlib.sha256()
        with archive.open("rb") as inp:
            for block in iter(lambda: inp.read(2 * 1024 * 1024), b""):
                digest.update(block)
        if digest.hexdigest() != KNOWN_SHA256[system]:
            raise RuntimeError("Pinned ONNX Runtime archive checksum mismatch")

    # All source archives are hosted by Microsoft's official GitHub release.
    # Reject traversal even if a release archive becomes malformed.
    if archive.suffix == ".zip":
        with zipfile.ZipFile(archive) as z:
            for entry in z.infolist():
                relative = Path(entry.filename)
                if relative.is_absolute() or ".." in relative.parts:
                    raise RuntimeError("Unsafe ONNX Runtime ZIP entry")
            z.extractall(base)
    else:
        with tarfile.open(archive, "r:gz") as tar:
            for entry in tar.getmembers():
                relative = Path(entry.name)
                if relative.is_absolute() or ".." in relative.parts:
                    raise RuntimeError("Unsafe ONNX Runtime tar entry")
            tar.extractall(base, filter="data")
    if not (root / "include" / "onnxruntime_cxx_api.h").is_file():
        raise RuntimeError(f"ONNX Runtime archive missing C++ headers: {root}")
    print(f"Prepared ONNX Runtime {MAC_VERSION if system == 'macos' else VERSION}: {root}")
    return root


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("platform", choices=tuple(PLATFORMS))
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    run(args.platform, args.output)


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(f"ERROR: {error}", file=sys.stderr)
        raise SystemExit(1)
