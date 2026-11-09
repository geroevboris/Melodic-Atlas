#!/usr/bin/env python3
"""Experimental CLAP instrument matching from EXISTING Melodic Atlas embeddings.

No samples are modified, no scan is started, and NO filter labels are changed.
Only text CLAP vectors are newly calculated. Native audio embeddings are read
from ScannedDatabaseCache_v4.xml. Raw cosine values are NOT probabilities.

Usage:
 python Tools/clap_zero_shot_experiment.py --output ~/Desktop/Atlas-CLAP-8M
 python Tools/clap_zero_shot_experiment.py --verify-native 8 --output ~/Desktop/Atlas-CLAP-8M
Requires: torch, transformers, numpy; verification additionally needs soundfile, scipy.
"""
import argparse
import base64
import csv
import hashlib
import json
import math
import struct
import xml.etree.ElementTree as ET
from pathlib import Path

MODEL_ID = "laion/clap-htsat-unfused"
# Distinguish SOURCE from timbral character: no automatic instrument tag is
# asserted merely because a sound happens to resemble one of these prompts.
PROMPTS = {
 "Piano": ("Keys", ["a recording of a piano playing", "solo acoustic piano notes"]),
 "Rhodes": ("Keys", ["a recording of electric piano notes", "a Rhodes electric piano"]),
 "Organ": ("Keys", ["a recording of an organ playing"]),
 "Mallet": ("Keys", ["a recording of a vibraphone playing", "a recording of a marimba"]),
 "SynthLead": ("Synth", ["a recording of a distorted synthesizer lead", "a solo electronic synth melody"]),
 "SynthPad": ("Synth", ["a recording of a synthetic atmospheric pad", "a sustained electronic synthesizer sound"]),
 "SynthPluck": ("Synth", ["a short electronic synthesizer pluck", "a synthetic plucked note"]),
 "SynthArp": ("Synth", ["a recording of a synthesizer arpeggio"]),
 "SubBass": ("Bass", ["a low sub bass note"]),
 "SynthBass": ("Bass", ["a recording of synthesized bass", "an electronic bass synthesizer"]),
 "ElectricBass": ("Bass", ["a recording of electric bass guitar"]),
 "AcousticBass": ("Bass", ["a recording of acoustic upright double bass"]),
 "Singing": ("Vocal", ["a recording of a person singing", "a recording of processed autotuned vocals"]),
 "Rap": ("Vocal", ["a recording of a person rapping a cappella", "a human speaking"]),
 "Choir": ("Vocal", ["a recording of a vocal choir"]),
 "VocalChops": ("Vocal", ["a recording of chopped human vocal phrases"]),
 "Violin": ("Strings", ["a recording of a solo bowed violin", "a recording of an orchestral violin"]),
 "Viola": ("Strings", ["a recording of a bowed viola"]),
 "Cello": ("Strings", ["a recording of a bowed cello"]),
 "StringsSection": ("Strings", ["a recording of an orchestral strings section"]),
 "Brass": ("Strings", ["a recording of a trumpet playing", "a recording of a trombone playing"]),
 "Woodwind": ("Strings", ["a recording of a flute playing", "a recording of a clarinet or saxophone"]),
 "ElectricGuitar": ("Guitar", ["a recording of a distorted electric guitar riff", "a short crunchy electric guitar note"]),
 "AcousticGuitar": ("Guitar", ["a recording of an acoustic guitar", "a plucked acoustic guitar note"]),
 "ProcessedGuitar": ("Guitar", ["a recording of heavily processed electric guitar"]),
 "Atmosphere": ("FX", ["a recording of an ambient noise texture", "a cinematic atmospheric sound effect"]),
 "Riser": ("FX", ["a rising electronic sound effect", "a cinematic transition sweep"]),
 "Impact": ("FX", ["a recording of a cinematic impact sound effect"]),
 "Foley": ("FX", ["a recording of an environmental foley sound effect"]),
 "Kick": ("Drums", ["a recording of a kick drum hit", "an electronic bass drum"]),
 "Snare": ("Drums", ["a recording of a snare drum hit"]),
 "Cymbal": ("Drums", ["a recording of a hi hat or cymbal hit"]),
 "Percussion": ("Drums", ["a recording of a metallic gong", "a clap or percussion hit"]),
 "DrumLoop": ("Drums", ["a recording of a percussion drum loop"])
}

# JUCE MemoryBlock::toBase64Encoding is NOT RFC-4648 Base64.
# The payload is "<decimal byte size>.<6-bit symbols>", where symbols
# encode successive little-endian bit ranges. See JUCE juce_MemoryBlock.cpp.
JUCE_RADIX64 = ".ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+"

def decode_juce_memory_block(value, expected_size=512*4):
    if not isinstance(value, str):
        raise ValueError("JUCE encoded memory block must be text")
    prefix, sep, symbols = value.partition(".")
    if sep != "." or not prefix.isdecimal():
        raise ValueError("JUCE memory block missing length prefix")
    size = int(prefix)
    if size != expected_size:
        raise ValueError(f"Unexpected JUCE memory block size: {size}")
    expected_symbols = (size*8+5)//6
    if len(symbols) != expected_symbols:
        raise ValueError("JUCE memory block symbol count mismatch")
    result = bytearray(size)
    for i, symbol in enumerate(symbols):
        v = JUCE_RADIX64.find(symbol)
        if v < 0:
            raise ValueError("Invalid JUCE radix64 symbol")
        bit = i*6
        byte = bit // 8
        offset = bit % 8
        result[byte] |= (v << offset) & 0xff
        if byte+1 < size:
            result[byte+1] |= (v >> (8-offset)) & 0xff
    return bytes(result)

def native_vectors(path):
    """Extract exactly the 512 native ONNX floats already cached by JUCE."""
    result = []
    root = ET.parse(path).getroot()
    records = sum(1 for _ in root.iter("Sample"))
    for node in root.iter("Sample"):
        if node.get("clapEmbeddingValid", "").lower() not in ("1", "true"):
            continue
        text = node.get("clapEmbedding", "")
        if not text:
            continue
        try:
            raw = decode_juce_memory_block(text)
            if len(raw) != 512 * 4:
                continue
            vec = struct.unpack("<512f", raw)
            if not all(math.isfinite(float(v)) for v in vec):
                continue
            name = node.get("path", "")
            if name:
                result.append((name, vec))
        except (ValueError, struct.error):
            continue
    return records, result

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--cache", type=Path, default=Path.home() / "Library" / "Application Support" / "MelodicAtlas" / "ScannedDatabaseCache_v4.xml")
    ap.add_argument("--output", type=Path, required=True)
    ap.add_argument("--limit", type=int, default=0)
    ap.add_argument("--cpu", action="store_true")
    ap.add_argument("--verify-native", type=int, default=0, metavar="N", help="compare N cached vectors against original official Hugging Face audio embeddings")
    args = ap.parse_args()

    import numpy as np
    import torch
    from transformers import ClapModel, AutoTokenizer

    if not args.cache.is_file():
        ap.error("Existing CLAP cache not found: " + str(args.cache))
    total, entries = native_vectors(args.cache)
    if args.limit > 0:
        entries = entries[:args.limit]
    if not entries:
        ap.error("No valid native CLAP vectors. Scan with native CLAP first.")
    torch.set_num_threads(2)
    device = "cpu" if args.cpu or not torch.backends.mps.is_available() else "mps"
    model = ClapModel.from_pretrained(MODEL_ID).to(device).eval()
    tokenizer = AutoTokenizer.from_pretrained(MODEL_ID)

    text_prompts = []
    bounds = {}
    labels = list(PROMPTS)
    for label, (_, group) in PROMPTS.items():
        begin = len(text_prompts)
        text_prompts.extend(group)
        bounds[label] = (begin, len(text_prompts))
    tokens = tokenizer(text_prompts, padding=True, truncation=True, return_tensors="pt").to(device)
    with torch.inference_mode():
        text_embeddings = model.get_text_features(**tokens)
    text_embeddings = text_embeddings.detach().float().cpu().numpy()
    text_embeddings /= np.maximum(np.linalg.norm(text_embeddings, axis=1, keepdims=True), 1e-12)

    source_embeddings = np.asarray([vec for _, vec in entries], dtype=np.float32)
    source_embeddings /= np.maximum(np.linalg.norm(source_embeddings, axis=1, keepdims=True), 1e-12)
    cosine = source_embeddings @ text_embeddings.T

    args.output.mkdir(parents=True, exist_ok=True)
    bank = {
        "model": MODEL_ID, "dim": 512,
        "description": "Uncalibrated candidate prototypes; do not use as production filter truth",
        "prompts": [{"family": PROMPTS[label][0], "subtype": label, "text": prompt,
                     "embedding": text_embeddings[j].round(8).tolist()}
                    for label, (start, end) in bounds.items()
                    for j, prompt in enumerate(text_prompts[start:end], start)]
    }
    bank_path = args.output / "clap_text_bank.json"
    bank_path.write_text(json.dumps(bank, ensure_ascii=False), encoding="utf-8")
    scores = []
    for index, (path, _) in enumerate(entries):
        by_subtype = {label: float(np.max(cosine[index, lo:hi]))
                      for label, (lo, hi) in bounds.items()}
        ranked = sorted(by_subtype, key=by_subtype.get, reverse=True)
        first, second = ranked[:2]
        scores.append({"file": path, "subtype": first, "family": PROMPTS[first][0],
                       "top_cosine_NOT_probability": round(by_subtype[first], 6),
                       "margin_NOT_probability": round(by_subtype[first]-by_subtype[second], 6),
                       "second_subtype": second,
                       **{"cosine_"+k: round(by_subtype[k], 6) for k in labels}})
    with (args.output / "clap_subtype_scores.csv").open("w", encoding="utf-8-sig", newline="") as out:
        w = csv.DictWriter(out, fieldnames=list(scores[0]))
        w.writeheader()
        w.writerows(scores)

    # Optional true *pairwise* encoder validation, not an accuracy metric.
    comparison = []
    if args.verify_native > 0:
        import soundfile as sf
        from scipy.signal import resample_poly
        from transformers import ClapFeatureExtractor
        # macOS 12 MPS is missing upsample_bicubic2d used by the CLAP audio
        # encoder; text embeddings above can still be computed with MPS.
        # Use CPU ONLY for the small reference-audio parity test.
        model = model.to("cpu").eval()
        extractor = ClapFeatureExtractor.from_pretrained(MODEL_ID)
        for path, cached in entries[:args.verify_native]:
            try:
                with sf.SoundFile(path) as f:
                    rate = f.samplerate
                    y = f.read(frames=min(f.frames, rate*10), dtype="float32", always_2d=True).mean(axis=1)
                if rate != 48000:
                    d = math.gcd(rate, 48000)
                    y = resample_poly(y, 48000//d, rate//d).astype(np.float32)
                features = extractor([y], sampling_rate=48000, return_tensors="pt")
                with torch.inference_mode():
                    official = model.get_audio_features(**{k: v.to("cpu") for k,v in features.items()})
                official = official.detach().float().cpu().numpy()[0]
                official /= max(float(np.linalg.norm(official)), 1e-12)
                comparison.append({"file": path, "native_vs_hf_cosine": float(source_embeddings[[p for p,_ in entries].index(path)] @ official)})
            except Exception as exc:
                comparison.append({"file": path, "error": repr(exc)})
        (args.output / "native_vs_official.json").write_text(json.dumps(comparison, indent=2), encoding="utf-8")
        successful = [item for item in comparison if "native_vs_hf_cosine" in item]
        if not successful:
            raise RuntimeError(
                "Native-to-official CLAP verification failed for every file; "
                "see native_vs_official.json. No encoder parity conclusion can be drawn."
            )

    summary = {"version":"8M-experimental", "cache_records":total,
               "valid_native_embeddings":len(entries), "device":device,
               "prompts":len(text_prompts), "subtypes":len(PROMPTS),
               "bank_sha256":hashlib.sha256(bank_path.read_bytes()).hexdigest(),
               "prototypes_are_probabilities":False,
               "production_filters_modified":False,
               "native_verification":comparison}
    (args.output / "summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
    print(f"Scored {len(entries)} cached files against {len(PROMPTS)} instrument subtypes.")
    print("Results:", args.output)
    print("IMPORTANT: compare audio encoder vectors before trusting any scores.")

if __name__ == "__main__":
    main()
