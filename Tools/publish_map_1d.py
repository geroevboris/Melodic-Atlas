#!/usr/bin/env python3
"""Bridge Map Prototype 1D results into Melodic Atlas (Python 3.11, stdlib only).

This does NOT run CLAP or perform a second audio analysis. It extracts the
already-computed Balanced projection from the 1D HTML and publishes a checked
manifest to the chosen library folder. Plugin scan/cache load picks it up.
"""
import argparse
import json
import os
from pathlib import Path


def publish(library: Path, html: Path) -> int:
    library = library.expanduser().resolve()
    html = html.expanduser().resolve()
    if not library.is_dir() or not html.is_file():
        raise ValueError("Library folder or Atlas_Map_1D.html not found")
    source = html.read_text(encoding="utf-8")
    start_marker = "const DATA="
    end_marker = ";\nconst samples="
    start = source.find(start_marker)
    end = source.find(end_marker, start + len(start_marker))
    if start < 0 or end < 0:
        raise ValueError("This does not look like an Atlas Map Prototype 1D result")
    raw = source[start + len(start_marker):end]
    payload = json.loads(raw)
    report = payload.get("report", {})
    if (report.get("version") != "Map Prototype 1D" or
            report.get("model") != "laion/clap-htsat-unfused"):
        raise ValueError("Only real Pure CLAP / Map Prototype 1D data are accepted; DSP smoke tests are rejected")
    samples = payload.get("samples", [])
    if not samples or len(samples) != report.get("files"):
        raise ValueError("Incomplete or inconsistent projection")

    # Match the plug-in's supported audio formats; no partial projections.
    extensions = {".wav", ".aif", ".aiff", ".flac", ".ogg", ".mp3"}
    actual_files = {p.relative_to(library).as_posix()
                    for p in library.rglob("*")
                    if p.is_file() and p.suffix.lower() in extensions}
    entries = []
    found = set()
    for item in samples:
        relative = item["file"]
        if (not isinstance(relative, str) or
                not relative or relative.startswith("/") or
                ".." in Path(relative).parts or "\\" in relative or
                relative in found):
            raise ValueError("Unsafe or duplicate file name in map result")
        path = (library / relative).resolve()
        if not path.is_file() or not path.is_relative_to(library):
            raise ValueError(f"Missing library file: {relative}")
        point = item["p"]["balanced"]
        if len(point) != 2 or not all(isinstance(v, (float, int)) and
                                      0 <= v <= 1 for v in point):
            raise ValueError(f"Invalid coordinates: {relative}")
        st = path.stat()
        entries.append({"file": relative, "x": point[0], "y": point[1],
                        "bytes": st.st_size, "modifiedMs": int(st.st_mtime * 1000)})
        found.add(relative)
    if found != actual_files:
        missing = actual_files - found
        raise ValueError(f"Library changed or 1D skipped some files; {len(missing)} files lack coordinates. Regenerate a complete 1D map.")

    dest = library / ".atlas_neural_map_v1.json"
    tmp = library / ".atlas_neural_map_v1.json.tmp"
    document = {"schema": 1, "method": "clap-umap-balanced",
                "source": str(library), "samples": entries}
    try:
        with tmp.open("w", encoding="utf-8") as f:
            json.dump(document, f, ensure_ascii=False, separators=(",", ":"))
            f.flush()
            os.fsync(f.fileno())
        os.replace(tmp, dest)
    finally:
        if tmp.exists():
            tmp.unlink()
    print(f"Published {len(entries)} Pure CLAP / UMAP Balanced coordinates")
    print(f"Manifest: {dest}")
    print("The plugin can read these coordinates on its next scan or cache load.")
    return len(entries)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("library", nargs="?", type=Path)
    ap.add_argument("map_html", nargs="?", type=Path)
    args = ap.parse_args()
    library, html = args.library, args.map_html
    if library is None or html is None:
        import tkinter as tk
        from tkinter import filedialog
        root = tk.Tk()
        root.withdraw()
        library_s = filedialog.askdirectory(title="Select the WAV library used in Prototype 1D")
        html_s = filedialog.askopenfilename(title="Select Atlas_Map_1D.html",
                                          filetypes=[("HTML", "*.html")])
        root.destroy()
        if not library_s or not html_s:
            raise SystemExit("Cancelled")
        library, html = Path(library_s), Path(html_s)
    publish(library, html)


if __name__ == "__main__":
    main()
