#!/usr/bin/env python3
"""Small mask/alpha comparison utility for native-runtime spike parity checks."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any, Dict

import numpy as np
from PIL import Image


def load_gray(path: Path) -> np.ndarray:
    img = Image.open(path).convert("L")
    return np.asarray(img, dtype=np.float32) / 255.0


def compare(reference: Path, candidate: Path, threshold: float) -> Dict[str, Any]:
    ref = load_gray(reference)
    cand = load_gray(candidate)
    if ref.shape != cand.shape:
        raise ValueError(f"shape mismatch: reference {ref.shape}, candidate {cand.shape}")
    ref_bin = ref >= threshold
    cand_bin = cand >= threshold
    intersection = np.logical_and(ref_bin, cand_bin).sum(dtype=np.float64)
    union = np.logical_or(ref_bin, cand_bin).sum(dtype=np.float64)
    diff = np.abs(ref - cand)
    return {
        "schema": "flux.ai_native_spikes.metrics.v1",
        "reference": str(reference),
        "candidate": str(candidate),
        "shape": list(ref.shape),
        "threshold": threshold,
        "iou": float(intersection / union) if union else 1.0,
        "alpha_mae": float(diff.mean()),
        "alpha_max_error": float(diff.max()),
        "reference_nonzero": int(ref_bin.sum()),
        "candidate_nonzero": int(cand_bin.sum()),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Compare two grayscale masks/alpha images")
    parser.add_argument("--reference", required=True, type=Path)
    parser.add_argument("--candidate", required=True, type=Path)
    parser.add_argument("--threshold", type=float, default=0.5)
    parser.add_argument("--out", type=Path, default=None)
    args = parser.parse_args()

    payload = compare(args.reference, args.candidate, args.threshold)
    text = json.dumps(payload, indent=2, sort_keys=True) + "\n"
    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(text, encoding="utf-8")
    print(text, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
