#!/usr/bin/env python3
"""Compute real LAION CLAP audio embeddings and a stable UMAP map for Melodic Atlas.

Heavy inference runs OUTSIDE the JUCE plug-in. Initial run learns a UMAP model;
subsequent runs reuse cached audio embeddings and UMAP.transform() for new files.
Existing coordinates never move unless --rebuild is explicitly requested.
"""
from __future__ import annotations

import argparse
import json
import math
import os
from pathlib import Path
import sys
import tempfile

MODEL_ID = "laion/clap-htsat-unfused"
SCHEMA = 1
AUDIO_EXTS = {".wav", ".aif", ".aiff", ".flac", ".ogg", ".mp3"}
DRUM_TOKENS = {
    "kick", "kicks", "bd", "snare", "snares", "snr", "sd", "clap", "claps", "cp",
    "hihat", "hihats", "hh", "closedhat", "openhat", "hat", "hats",
    "cymbal", "cymbals", "crash", "ride", "rimshot", "rim",
    "tambourine", "shaker", "shakers", "cowbell", "tom", "toms",
    "drum", "drums", "percussion", "perc", "conga", "bongo", "clave",
    "claves", "woodblock", "tamb", "timbale",
}
DRUM_DIRS = {
    "drums", "drum", "percussion", "perc", "kicks", "kick", "snares",
    "snare", "claps", "clap", "hats", "hihat", "hi hats",
    "cymbals", "shakers", "toms",
}


def explicit_drum(path: Path) -> bool:
    import re
    tokens = set(filter(None, re.split(r"[ _\\-.()\\[\\]{}0123456789]+", path.stem.lower())))
    return bool(tokens & DRUM_TOKENS) or any(
        parent.name.lower() in DRUM_DIRS for parent in (path.parent, path.parent.parent)
    )


def atomic_json(path: Path, obj: object) -> None:
    fd, temp = tempfile.mkstemp(prefix=path.name + ".", suffix=".tmp", dir=path.parent)
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as handle:
            json.dump(obj, handle, ensure_ascii=False, separators=(",", ":"), allow_nan=False)
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(temp, path)
    finally:
        if os.path.exists(temp):
            os.unlink(temp)


def fingerprint(path: Path) -> dict:
    stat = path.stat()
    return {"bytes": stat.st_size, "modifiedMs": int(stat.st_mtime_ns // 1_000_000)}


def catalog(folder: Path) -> dict[str, Path]:
    return {
        p.relative_to(folder).as_posix(): p
        for p in sorted(folder.rglob("*"))
        if p.is_file() and p.suffix.lower() in AUDIO_EXTS and not explicit_drum(p)
    }


def load_previous(path: Path) -> dict:
    if not path.is_file():
        return {}
    with path.open(encoding="utf-8") as h:
        data = json.load(h)
    if data.get("schema") != SCHEMA or data.get("model") != MODEL_ID:
        raise ValueError(f"Incompatible cache: {path}. Use --rebuild deliberately.")
    return data


def projection(points, fitted_bounds):
    import numpy as np
    lo, span = np.asarray(fitted_bounds[0]), np.asarray(fitted_bounds[1])
    norm = (np.asarray(points) - lo) / np.maximum(span, 1e-6)
    return np.clip(norm, 0.0, 1.0).tolist()


def self_test():
    import tempfile
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        (root / "Snare_07.wav").touch()
        (root / "Soft_Pluck_01.wav").touch()
        (root / "Drums").mkdir()
        (root / "Drums" / "sample.wav").touch()
        (root / "Keys").mkdir()
        (root / "Keys" / "Warm Piano.wav").touch()
        files = catalog(root)
        assert set(files) == {"Soft_Pluck_01.wav", "Keys/Warm Piano.wav"}, files
        atomic_json(root / "test.json", {"test": [1, 2, 3]})
        assert json.loads((root / "test.json").read_text())["test"] == [1, 2, 3]
    print("Self-test PASS: token gate, directory gate, atomic JSON, catalog.")


def run(folder: Path, *, rebuild: bool, batch_size: int, device: str) -> None:
    try:
        import numpy as np
        import joblib
        import umap
        import laion_clap
    except ImportError as exc:
        raise RuntimeError("Missing dependency. Install from Tools/CLAP_SETUP_8A.md") from exc

    folder = folder.expanduser().resolve()
    if not folder.is_dir():
        raise ValueError(f"No such library folder: {folder}")
    items = catalog(folder)
    if len(items) < 4:
        raise ValueError("Need at least four non-drum audio samples to fit a map.")
    print(f"Selected {len(items)} files (explicit drums excluded).", flush=True)

    vector_file = folder / ".atlas_clap_vectors_v1.json"
    state_file = folder / ".atlas_clap_projection_v1.json"
    model_file = folder / ".atlas_umap_v1.joblib"
    manifest_file = folder / ".atlas_neural_map_v1.json"
    previous_vectors = {} if rebuild else load_previous(vector_file).get("samples", {})
    previous_map = {} if rebuild else load_previous(state_file)
    if previous_map and not model_file.is_file():
        raise RuntimeError("Stored UMAP model is missing. Restore it or use --rebuild.")
    if model_file.is_file() and not previous_map and not rebuild:
        raise RuntimeError("Stored UMAP map state is missing. Restore it or use --rebuild.")

    valid_vectors: dict[str, dict] = {}
    needing_inference = []
    for rel, file in items.items():
        stat = fingerprint(file)
        old = previous_vectors.get(rel)
        if (old is not None and old.get("bytes") == stat["bytes"]
            and old.get("modifiedMs") == stat["modifiedMs"]
            and isinstance(old.get("embedding"), list)
            and len(old["embedding"]) >= 64
            and all(isinstance(v, (float, int)) and math.isfinite(v)
                    for v in old["embedding"])):
            valid_vectors[rel] = old
        else:
            needing_inference.append(rel)

    print(f"Cached: {len(valid_vectors)}; CLAP inference: {len(needing_inference)}", flush=True)
    if needing_inference:
        model = laion_clap.CLAP_Module(enable_fusion=False, device=device)
        model.load_ckpt()
        for offset in range(0, len(needing_inference), batch_size):
            batch = needing_inference[offset:offset + batch_size]
            paths = [str(items[rel]) for rel in batch]
            # An exception leaves the last published map intact.
            embeddings = np.asarray(
                model.get_audio_embedding_from_filelist(x=paths, use_tensor=False),
                dtype=np.float32,
            )
            if embeddings.ndim != 2 or embeddings.shape[0] != len(batch):
                raise RuntimeError(f"CLAP returned an unexpected shape: {embeddings.shape}")
            for rel, vec in zip(batch, embeddings):
                norm = float(np.linalg.norm(vec))
                if not np.isfinite(vec).all() or norm < 1e-8:
                    raise RuntimeError(f"Invalid CLAP embedding: {rel}")
                valid_vectors[rel] = {
                    **fingerprint(items[rel]),
                    "embedding": (vec / norm).astype(float).tolist(),
                }
            print(f"  CLAP {min(offset + batch_size, len(needing_inference))}/{len(needing_inference)}", flush=True)

    ordered = sorted(items)
    matrix = np.asarray([valid_vectors[rel]["embedding"] for rel in ordered], dtype=np.float32)
    if len({len(vec) for vec in matrix}) != 1:
        raise RuntimeError("CLAP embedding dimension changed; use --rebuild.")

    if previous_map:
        print("Reusing saved UMAP; existing dots remain fixed.", flush=True)
        # joblib files use pickle. Only load one produced locally by this script.
        mapper = joblib.load(model_file)
        bounds = previous_map["bounds"]
        old_points = previous_map["samples"]
        coords = {}
        new_names = []
        for rel in ordered:
            stat = fingerprint(items[rel])
            old = old_points.get(rel)
            if old and old.get("bytes") == stat["bytes"] and old.get("modifiedMs") == stat["modifiedMs"]:
                coords[rel] = old["xy"]
            else:
                new_names.append(rel)
        if new_names:
            new_matrix = np.asarray([valid_vectors[n]["embedding"] for n in new_names], dtype=np.float32)
            mapped = mapper.transform(new_matrix)
            for name, xy in zip(new_names, projection(mapped, bounds)):
                coords[name] = xy
    else:
        print("Fitting UMAP to real CLAP embeddings.", flush=True)
        mapper = umap.UMAP(
            n_components=2, n_neighbors=min(20, len(ordered) - 1),
            min_dist=0.15, metric="cosine", random_state=42,
            transform_seed=42,
        )
        mapped = mapper.fit_transform(matrix)
        low = mapped.min(axis=0)
        span = mapped.max(axis=0) - low
        bounds = [low.astype(float).tolist(), span.astype(float).tolist()]
        coords = dict(zip(ordered, projection(mapped, bounds)))

    map_samples = {}
    manifest = []
    for name in ordered:
        stat = fingerprint(items[name])
        xy = coords[name]
        if len(xy) != 2 or any(not math.isfinite(v) or not 0 <= v <= 1 for v in xy):
            raise RuntimeError(f"Invalid map coordinates for {name}")
        map_samples[name] = {**stat, "xy": xy}
        manifest.append({"file": name, "x": xy[0], "y": xy[1], **stat})

    # Save the fitted UMAP model only when first fitting or rebuilding.
    if not previous_map:
        fd, tmp = tempfile.mkstemp(prefix=".atlas_umap_v1.", suffix=".tmp", dir=folder)
        os.close(fd)
        try:
            joblib.dump(mapper, tmp)
            os.replace(tmp, model_file)
        finally:
            if os.path.exists(tmp):
                os.unlink(tmp)

    atomic_json(vector_file, {"schema": SCHEMA, "model": MODEL_ID, "samples": valid_vectors})
    atomic_json(state_file, {"schema": SCHEMA, "model": MODEL_ID,
                             "bounds": bounds, "samples": map_samples})
    # Publish last: the plug-in never sees half-written coordinates.
    atomic_json(manifest_file, {
        "schema": 1, "method": "clap-umap-balanced",
        "source": str(folder), "samples": manifest,
    })
    print(f"Published {len(manifest)} neural map positions: {manifest_file}", flush=True)
    print("Open Melodic Atlas and Scan Folder to import this map.", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("folder", nargs="?", type=Path)
    parser.add_argument("--rebuild", action="store_true", help="Refit UMAP and move all dots (explicit opt-in)")
    parser.add_argument("--batch-size", type=int, default=4)
    parser.add_argument("--device", default="cpu", help="cpu by default; MPS requires compatible PyTorch/CLAP")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    if args.folder is None:
        parser.error("Provide a folder or --self-test.")
    if args.batch_size < 1 or args.batch_size > 64:
        parser.error("Batch size must be between 1 and 64.")
    run(args.folder, rebuild=args.rebuild, batch_size=args.batch_size, device=args.device)


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
