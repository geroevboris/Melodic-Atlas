#!/usr/bin/env python3
"""Acquire the pinned ONNX Runtime native libraries for offline CLAP builds.

The upstream GitHub Release assets are currently unavailable (HTTP 404).
Microsoft's versioned NuGet package contains the exact same public C++ API
headers and native runtime binaries. Verify the official package SHA-512
before extracting only the required components. Nothing is fetched at
plugin runtime; the resulting binaries are packaged into the plugin.
"""
import argparse
import base64
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import urllib.request
import zipfile

VERSION = "1.22.1"
MAC_VERSION = "1.19.2"
PLATFORMS = {
    "macos": f"onnxruntime-osx-universal2-{MAC_VERSION}",
    "windows": f"onnxruntime-win-x64-{VERSION}",
}


def fetch_verified_nuget(version: str, base: Path) -> Path:
    package = f"microsoft.ml.onnxruntime.{version}.nupkg"
    destination = base / package
    url = ("https://api.nuget.org/v3-flatcontainer/microsoft.ml.onnxruntime/"
           f"{version}/{package}")
    # NuGet does not expose <package>.nupkg.sha512 at the public flat-container
    # URL (that sidecar lives in the local NuGet cache). The official catalog
    # LEAF is the published source of packageHash / packageHashAlgorithm.
    registration = ("https://api.nuget.org/v3/registration5-semver1/"
                    f"microsoft.ml.onnxruntime/{version}.json")
    with urllib.request.urlopen(
        urllib.request.Request(registration,
            headers={"User-Agent": "MelodicAtlas-CI/8B"}), timeout=60
    ) as response:
        registered = json.load(response)
    catalog_entry = registered.get("catalogEntry")
    if isinstance(catalog_entry, dict):
        catalog_url = catalog_entry.get("@id", "")
    else:
        catalog_url = catalog_entry or ""
    if not catalog_url.startswith("https://api.nuget.org/v3/catalog"):
        raise RuntimeError(f"Unexpected official NuGet catalog URL: {catalog_url}")
    with urllib.request.urlopen(
        urllib.request.Request(catalog_url,
            headers={"User-Agent": "MelodicAtlas-CI/8B"}), timeout=60
    ) as response:
        catalog = json.load(response)
    if (catalog.get("id", "").lower() != "microsoft.ml.onnxruntime"
        or catalog.get("version") != version
        or catalog.get("packageHashAlgorithm", "").upper() != "SHA512"):
        raise RuntimeError("Unexpected NuGet catalog package identity or digest type")
    expected = base64.b64decode(catalog["packageHash"], validate=True)
    if len(expected) != 64:
        raise RuntimeError("NuGet catalog returned an invalid SHA-512 digest")

    if destination.is_file():
        with destination.open("rb") as f:
            digest = hashlib.file_digest(f, "sha512").digest()
        if digest == expected:
            return destination
        destination.unlink()

    fd, temporary = tempfile.mkstemp(prefix="atlas-ort-", dir=base)
    try:
        with os.fdopen(fd, "wb") as out:
            req = urllib.request.Request(url,
                                         headers={"User-Agent": "MelodicAtlas-CI/8B"})
            with urllib.request.urlopen(req, timeout=180) as response:
                shutil.copyfileobj(response, out)
            out.flush()
            os.fsync(out.fileno())
        with open(temporary, "rb") as f:
            actual = hashlib.file_digest(f, "sha512").digest()
        if actual != expected:
            raise RuntimeError("NuGet ONNX Runtime package SHA-512 mismatch")
        os.replace(temporary, destination)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)
    print(f"Verified official Microsoft ONNX Runtime NuGet package v{version}",
          flush=True)
    return destination


def run(system: str, base: Path) -> Path:
    version = MAC_VERSION if system == "macos" else VERSION
    root = base / PLATFORMS[system]
    library = (root / "lib" /
               ("libonnxruntime.dylib" if system == "macos" else "onnxruntime.dll"))
    if (root / "include" / "onnxruntime_cxx_api.h").is_file() and library.is_file():
        return root

    base.mkdir(parents=True, exist_ok=True)
    archive = fetch_verified_nuget(version, base)
    with zipfile.ZipFile(archive) as package:
        names = package.namelist()
        for name in names:
            rel = Path(name.replace("\\", "/"))
            if rel.is_absolute() or ".." in rel.parts or ":" in rel.parts[0]:
                raise RuntimeError(f"Unsafe NuGet archive member: {name}")

        # NuGet includes the public native API headers in one of these layouts.
        prefixes = ("build/native/include/", "buildtransitive/native/include/")
        header_paths = [name for name in names
                        if any(name.lower().startswith(prefix) for prefix in prefixes)
                        and not name.endswith("/")]
        if not header_paths:
            raise RuntimeError("ONNX Runtime NuGet has no native C++ headers")
        for name in header_paths:
            lower = name.lower()
            prefix = next(p for p in prefixes if lower.startswith(p))
            rel = name[len(prefix):]
            dest = root / "include" / rel
            dest.parent.mkdir(parents=True, exist_ok=True)
            with package.open(name) as src, dest.open("wb") as out:
                shutil.copyfileobj(src, out)

        libdir = root / "lib"
        libdir.mkdir(parents=True, exist_ok=True)
        if system == "windows":
            for filename in ("onnxruntime.dll", "onnxruntime.lib"):
                matches = [name for name in names if
                    "runtimes/win-x64/native/" in name.lower() and
                    name.lower().endswith("/" + filename)]
                if len(matches) != 1:
                    raise RuntimeError(
                        f"Expected one win-x64 {filename}, found {matches}")
                with package.open(matches[0]) as src, (libdir / filename).open("wb") as dst:
                    shutil.copyfileobj(src, dst)
        else:
            # Preserve the existing universal Apple Silicon + Intel packaging.
            # The NuGet binaries are split by CPU architecture; combine with
            # Apple's lipo, then normalize the shared library install name.
            with tempfile.TemporaryDirectory(prefix="atlas-ort-universal-") as temp:
                slices = []
                for architecture in ("x64", "arm64"):
                    matches = [name for name in names if
                        "runtimes/osx" in name.lower() and
                        f"-{architecture}/native/" in name.lower() and
                        (name.lower().endswith("/libonnxruntime.dylib") or
                         name.lower().endswith(f"/libonnxruntime.{version}.dylib"))]
                    if len(matches) != 1:
                        raise RuntimeError(
                            f"Expected one osx-{architecture} runtime dylib; "
                            f"found {matches}. Available: "
                            f"{[n for n in names if 'onnxruntime.dylib' in n.lower()]}")
                    slice_path = Path(temp) / f"{architecture}.dylib"
                    with package.open(matches[0]) as src, slice_path.open("wb") as dst:
                        shutil.copyfileobj(src, dst)
                    slices.append(str(slice_path))
                universal = libdir / "libonnxruntime.dylib"
                subprocess.run(["lipo", "-create", *slices, "-output", str(universal)],
                               check=True)
                subprocess.run(["install_name_tool", "-id",
                    "@rpath/libonnxruntime.dylib", str(universal)], check=True)
                subprocess.run(["codesign", "--force", "--sign", "-",
                                str(universal)], check=True)
                shutil.copy2(universal, libdir / f"libonnxruntime.{version}.dylib")

    if not (root / "include" / "onnxruntime_cxx_api.h").is_file():
        raise RuntimeError(f"NuGet package missing C++ API header: {root}")
    if not library.is_file():
        raise RuntimeError(f"NuGet package missing runtime binary: {library}")
    print(f"Prepared pinned ONNX Runtime {version}: {root}", flush=True)
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
