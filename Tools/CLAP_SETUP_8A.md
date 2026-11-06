# Stage 8A — real CLAP → UMAP → Melodic Atlas map

This stage is **experimental**. It runs CLAP inference **outside the plugin** in a Python process. No model inference or file-system writes run on the audio callback. Stage 7C stays untouched. It does not claim to automatically run on Scan Folder: this is a manual preparatory step.

## Install on Mac M1 (Python 3.11)

From Terminal:

```bash
python3.11 -m venv ~/atlas-clap-venv
source ~/atlas-clap-venv/bin/activate
python -m pip install --upgrade pip
python -m pip install laion-clap umap-learn joblib
```

LAION CLAP also pulls PyTorch, audio dependencies and pretrained checkpoint data. Installation / first checkpoint download requires internet, may be large, and macOS version/PyTorch compatibility must be verified locally. CPU is chosen by default to avoid MPS incompatibilities. Python 3.11 support of third-party dependencies has not been validated in CI.

Download [build_clap_map_8a.py](https://github.com/geroevboris/Melodic-Atlas/raw/refs/heads/dev/8a-real-clap-umap-pipeline/Tools/build_clap_map_8a.py) to `~/Downloads`.

Run the no-model self-test first:

```bash
python ~/Downloads/build_clap_map_8a.py --self-test
```

Start with a **small separate test library** (e.g., 40–100 melodic WAVs, no source data moved). Run:

```bash
python ~/Downloads/build_clap_map_8a.py "/absolute/path/to/your/test/library"
```

After successful publication, open the Stage 8A plugin and **Scan Folder** on the *same folder*. It will load the published neural coordinates. When its plugin scanner rejects extra percussion, the import accepts the corresponding subset. If a file was added/modified after publication, the importer rejects the stale manifest and keeps the DSP fallback map; rerun the Python command first.

Repeat the same Python command when you add files: unchanged embeddings are reused, and saved UMAP transforms new embeddings without moving existing dots. Never run `--rebuild` unless you intentionally want to recalculate and move every dot.

## Generated data (inside the library folder)

* `.atlas_clap_vectors_v1.json` — per-file CLAP embeddings, size and timestamp cache
* `.atlas_umap_v1.joblib` — fitted UMAP model used for new samples
* `.atlas_clap_projection_v1.json` — stable coordinates + reference ranges
* `.atlas_neural_map_v1.json` — validated manifest imported by the plugin

**Keep these four files together** when moving your library. The manifest stores the absolute path to the library folder; after moving, rerun the Python script and rescan. `joblib` files can execute Python pickle code on load: never use a model file obtained from untrusted sources.

## Limitations / pending testing

* The Python inference code uses LAION's documented `CLAP_Module(enable_fusion=False)`, `load_ckpt()` and `get_audio_embedding_from_filelist`. A full CLAP runtime has **not** yet been verified on this Mac or in CI.
* The current pipeline computes one embedding per file, not segmented multi-window embeddings. Long loops may be less representative.
* It does not implement semantic text filters or character scoring from CLAP yet. Those are a separate stage.
* It excludes explicit drum-labeled files and dedicated folders before inference, matching the plugin's gate, but cannot reliably acoustically identify every unnamed drum.
* Persistent map coordinates stay unchanged after additions. UMAP.transform may place some newcomers imperfectly; an intentional full rebuild may eventually be useful.
* The full inference manifest must be valid for **every sample accepted** by the plugin's scanner. A single mismatched file causes fallback to the DSP map. This avoids mixing two incompatible coordinate systems.
* This stage does not include automatic Python launch from the plugin, live re-inference, or background processing during playback.
