#!/usr/bin/env python3
"""MatAnyone2 ORT submodule state-machine proof.

Runs the real Python InferenceCore algorithm twice on a reduced baseline:
1. native PyTorch modules
2. same InferenceCore with selected MatAnyone2 submodules replaced by ONNX
   Runtime CUDA module wrappers

This proves whether the exported submodules can participate in the temporal
state machine before attempting any C++ implementation. Spike-only.
"""
from __future__ import annotations

import argparse
import json
import time
import traceback
from datetime import datetime, timezone
from glob import glob
from pathlib import Path
from typing import Any

import numpy as np
import onnxruntime as ort
import torch
from PIL import Image

REPO_ROOT = Path(__file__).resolve().parents[3]
DEFAULT_BASELINE = REPO_ROOT / "tools/ai_native_spikes/results/matanyone2/baseline/baseline_manifest.json"
DEFAULT_ONNX_DIR = REPO_ROOT / "tools/ai_native_spikes/results/matanyone2/export/submodules"
DEFAULT_OUT = REPO_ROOT / "tools/ai_native_spikes/results/matanyone2/ort_state_machine"
SCHEMA = "flux.ai_native_spikes.matanyone2_ort_state_machine.v1"


class OrtModule(torch.nn.Module):
    def __init__(self, target: str, path: Path, device: torch.device):
        super().__init__()
        self.target = target
        self.device = device
        providers = []
        if "CUDAExecutionProvider" in ort.get_available_providers():
            providers.append("CUDAExecutionProvider")
        providers.append("CPUExecutionProvider")
        self.session = ort.InferenceSession(str(path), providers=providers)
        self.input_names = [i.name for i in self.session.get_inputs()]
        self.providers_used = self.session.get_providers()

    def _run(self, tensors: list[torch.Tensor]) -> list[torch.Tensor]:
        arrays = {f"input_{i}": t.detach().cpu().numpy() for i, t in enumerate(tensors)}
        feed = {name: arrays[name] for name in self.input_names if name in arrays}
        outputs = self.session.run(None, feed)
        return [torch.from_numpy(o).to(self.device) for o in outputs]

    def forward(self, *args, **kwargs):  # noqa: D401 - mirrors wrapped modules
        if self.target == "pixel_encoder":
            return tuple(self._run([args[0]]))
        if self.target == "pix_feat_proj":
            return self._run([args[0]])[0]
        if self.target == "key_proj":
            return tuple(self._run([args[0]]))
        if self.target == "mask_encoder":
            tensors = [args[0], args[1], args[2], args[3]]
            if len(args) > 4 and isinstance(args[4], torch.Tensor):
                tensors.append(args[4])
            return tuple(self._run(tensors))
        if self.target == "mask_decoder":
            ms = list(args[0])
            tensors = [*ms, args[1], args[2]]
            last_mask = kwargs.get("last_mask")
            if isinstance(last_mask, torch.Tensor):
                tensors.append(last_mask)
            return tuple(self._run(tensors))
        raise RuntimeError(f"unsupported ORT target: {self.target}")


def find_model_dir() -> Path:
    base = Path.home() / ".local/share/Flux/models/matanyone2"
    for d in sorted(base.iterdir()) if base.is_dir() else []:
        if d.is_dir() and (d / "model.safetensors").is_file():
            return d
    raise FileNotFoundError(f"MatAnyone2 weights missing under {base}")


def load_baseline_inputs(manifest: Path, max_frames: int, warmup_repeats: int, device: torch.device):
    frame_paths: list[str] = []
    mask_path = None
    if manifest.is_file():
        data = json.loads(manifest.read_text(encoding="utf-8"))
        frames_dir = data.get("frame_extraction", {}).get("frames_dir")
        if frames_dir and Path(frames_dir).is_dir():
            frame_paths = sorted(glob(str(Path(frames_dir) / "frame_*.png")))
        mask_path = data.get("inputs", {}).get("mask", {}).get("path")
    if not frame_paths:
        fallback = REPO_ROOT / "tools/ai_native_spikes/results/matanyone2/baseline/frames"
        frame_paths = sorted(glob(str(fallback / "frame_*.png")))
    if not frame_paths:
        raise FileNotFoundError("baseline frame_*.png files not found")

    first = Image.open(frame_paths[0]).convert("RGB")
    if mask_path and Path(mask_path).is_file():
        mask_arr = np.array(Image.open(mask_path).convert("L"), dtype=np.float32)
    else:
        mask_arr = np.ones((first.size[1], first.size[0]), dtype=np.float32) * 255.0
        mask_path = None
    mask = torch.from_numpy(mask_arr).to(device)

    first_tensor = torch.from_numpy(np.array(first)).permute(2, 0, 1).float()
    real_frames = []
    for fp in frame_paths[:max_frames]:
        img = Image.open(fp).convert("RGB")
        real_frames.append(torch.from_numpy(np.array(img)).permute(2, 0, 1).float())
    return [first_tensor] * warmup_repeats + real_frames, mask, frame_paths[:max_frames], mask_path


def save_mask(mask: torch.Tensor, path: Path) -> dict[str, Any]:
    arr = mask.detach().cpu().numpy()
    arr = np.squeeze(arr)
    if arr.ndim > 2:
        arr = arr[0]
    arr = np.clip(arr, 0.0, 1.0)
    img = Image.fromarray((arr * 255.0).astype(np.uint8), mode="L")
    path.parent.mkdir(parents=True, exist_ok=True)
    img.save(path)
    return {"path": str(path), "nonzero": int(np.count_nonzero(np.asarray(img) > 0)), "size": list(img.size)}


def run_core(model, frames: list[torch.Tensor], mask: torch.Tensor, device: torch.device, warmup_repeats: int, out_dir: Path, label: str) -> dict[str, Any]:
    from matanyone2.inference.inference_core import InferenceCore

    processor = InferenceCore(model, cfg=model.cfg, device=device)
    outputs = []
    t0 = time.monotonic()
    with torch.inference_mode():
        for ti, frame_tensor in enumerate(frames):
            image = (frame_tensor / 255.0).float().to(device)
            if ti == 0:
                output_prob = processor.step(image, mask, objects=[1])
                output_prob = processor.step(image, first_frame_pred=True)
            elif ti < warmup_repeats:
                output_prob = processor.step(image, first_frame_pred=True)
            else:
                output_prob = processor.step(image)
            if ti >= warmup_repeats:
                out_mask = processor.output_prob_to_mask(output_prob, matting=True)
                outputs.append(out_mask.detach().cpu())
    elapsed = time.monotonic() - t0
    saved = []
    for i, m in enumerate(outputs):
        saved.append(save_mask(m, out_dir / label / f"mask_{i:04d}.png"))
    return {"elapsed_seconds": round(elapsed, 3), "masks": outputs, "saved": saved}


def compare_masks(refs: list[torch.Tensor], cands: list[torch.Tensor]) -> list[dict[str, Any]]:
    rows = []
    for i, (ref_t, cand_t) in enumerate(zip(refs, cands)):
        ref = np.squeeze(ref_t.numpy()).astype(np.float32)
        cand = np.squeeze(cand_t.numpy()).astype(np.float32)
        if ref.shape != cand.shape:
            rows.append({"frame": i, "error": f"shape mismatch {ref.shape} vs {cand.shape}"})
            continue
        diff = np.abs(ref - cand)
        ref_bin = ref >= 0.5
        cand_bin = cand >= 0.5
        inter = np.logical_and(ref_bin, cand_bin).sum(dtype=np.float64)
        union = np.logical_or(ref_bin, cand_bin).sum(dtype=np.float64)
        rows.append({
            "frame": i,
            "shape": list(ref.shape),
            "iou": float(inter / union) if union else 1.0,
            "mae": float(diff.mean()),
            "max_error": float(diff.max()),
            "reference_nonzero": int(ref_bin.sum()),
            "candidate_nonzero": int(cand_bin.sum()),
        })
    return rows


def attach_ort_modules(model, onnx_dir: Path, device: torch.device) -> dict[str, Any]:
    targets = ["pixel_encoder", "pix_feat_proj", "key_proj", "mask_encoder", "mask_decoder"]
    info = {}
    for target in targets:
        path = onnx_dir / f"matanyone2_{target}.onnx"
        if not path.is_file():
            raise FileNotFoundError(f"missing ONNX submodule: {path}")
        wrapper = OrtModule(target, path, device)
        setattr(model, target, wrapper)
        info[target] = {"path": str(path), "providers": wrapper.providers_used, "session_inputs": wrapper.input_names}
    return info


def main() -> int:
    parser = argparse.ArgumentParser(description="MatAnyone2 ORT submodule state-machine proof")
    parser.add_argument("--baseline-manifest", type=Path, default=DEFAULT_BASELINE)
    parser.add_argument("--onnx-dir", type=Path, default=DEFAULT_ONNX_DIR)
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT)
    parser.add_argument("--device", default="cuda")
    parser.add_argument("--max-frames", type=int, default=2)
    parser.add_argument("--warmup-repeats", type=int, default=2)
    args = parser.parse_args()

    report: dict[str, Any] = {"schema": SCHEMA, "timestamp": datetime.now(timezone.utc).isoformat()}
    try:
        from matanyone2.model.matanyone2 import MatAnyone2
        device = torch.device(args.device if args.device == "cuda" and torch.cuda.is_available() else "cpu")
        report["device"] = str(device)
        frames, mask, frame_paths, mask_path = load_baseline_inputs(args.baseline_manifest, args.max_frames, args.warmup_repeats, device)
        report["inputs"] = {"frame_paths": frame_paths, "mask_path": mask_path, "warmup_repeats": args.warmup_repeats}
        model_dir = find_model_dir()
        report["model_dir"] = str(model_dir)

        torch_model = MatAnyone2.from_pretrained(str(model_dir)).to(device).eval()
        torch_run = run_core(torch_model, frames, mask, device, args.warmup_repeats, args.out, "torch")
        report["torch"] = {k: v for k, v in torch_run.items() if k != "masks"}
        del torch_model
        if torch.cuda.is_available():
            torch.cuda.empty_cache()

        ort_model = MatAnyone2.from_pretrained(str(model_dir)).to(device).eval()
        report["ort_modules"] = attach_ort_modules(ort_model, args.onnx_dir, device)
        ort_run = run_core(ort_model, frames, mask, device, args.warmup_repeats, args.out, "ort")
        report["ort"] = {k: v for k, v in ort_run.items() if k != "masks"}
        report["parity"] = compare_masks(torch_run["masks"], ort_run["masks"])
        report["status"] = "ok"
    except Exception as exc:
        report["status"] = "error"
        report["error"] = f"{type(exc).__name__}: {exc}"
        report["traceback"] = traceback.format_exc().splitlines()[-30:]
    finally:
        if torch.cuda.is_available():
            torch.cuda.empty_cache()

    args.out.mkdir(parents=True, exist_ok=True)
    report_path = args.out / "matanyone2_ort_state_machine_report.json"
    report_path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps({"status": report.get("status"), "report": str(report_path), "parity": report.get("parity")}, indent=2, sort_keys=True))
    return 0 if report.get("status") == "ok" else 1


if __name__ == "__main__":
    raise SystemExit(main())
