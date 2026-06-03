#!/usr/bin/env python3
"""Native MatAnyone2 JSON-lines worker for Flux.

Loads the model directly from local weights, runs frame-by-frame inference
using the matanyone2 InferenceCore API. No external repos or env vars.
"""
from __future__ import annotations

import os
os.environ['PYTORCH_CUDA_ALLOC_CONF'] = 'expandable_segments:True'

import json
import sys
import traceback
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Callable

SCHEMA = "org.flux.ai.matanyone2-worker.v1"
WARNING_TEXT = (
    "NON-COMMERCIAL: MatAnyone2 is non-commercial/research licensed. "
    "Visible warning required. Commercial production use is not cleared."
)

MODEL_SEARCH_BASE = Path.home() / ".local/share/Flux/models/matanyone2"


def _find_model_dir() -> Path | None:
    """Find the local MatAnyone2 model directory with safetensors weights."""
    if not MODEL_SEARCH_BASE.is_dir():
        return None
    for d in sorted(MODEL_SEARCH_BASE.iterdir()):
        if d.is_dir() and (d / "model.safetensors").is_file():
            return d
    return None


def _check_dependencies() -> list[str]:
    """Return a list of blockers that prevent this worker from running."""
    blockers: list[str] = []
    try:
        import torch  # noqa: F401
    except ImportError:
        blockers.append("PyTorch (torch) is not installed.")
    try:
        import matanyone2  # noqa: F401
    except ImportError:
        blockers.append(
            "matanyone2 Python package is not installed. "
            "Install with: pip install -e <path-to-MatAnyone2-repo>"
        )
    if _find_model_dir() is None:
        blockers.append(
            f"MatAnyone2 model weights not found under {MODEL_SEARCH_BASE}. "
            "Use the Flux model manager to download them."
        )
    return blockers


def self_check_payload() -> dict[str, Any]:
    """Return a self-check payload for --self-check and status commands."""
    blockers = _check_dependencies()
    model_dir = _find_model_dir()
    cuda_available = False
    try:
        import torch
        cuda_available = torch.cuda.is_available()
    except ImportError:
        pass
    if not cuda_available and len(blockers) == 0:
        blockers.append(
            "CUDA is not available. MatAnyone2 requires an NVIDIA GPU with CUDA. "
            "No CPU fallback is provided."
        )
    payload: dict[str, Any] = {
        "worker_schema": SCHEMA,
        "ready": len(blockers) == 0,
        "warning_text": WARNING_TEXT,
        "model_dir": str(model_dir) if model_dir else None,
        "cuda_available": cuda_available,
    }
    if blockers:
        payload["blockers"] = blockers
    return payload


def _choose_device() -> str:
    try:
        import torch
        if torch.cuda.is_available():
            return "cuda"
        raise RuntimeError(
            "CUDA is not available. MatAnyone2 requires an NVIDIA GPU with CUDA. "
            "No CPU fallback is provided."
        )
    except ImportError:
        raise RuntimeError(
            "PyTorch (torch) is not installed. MatAnyone2 requires PyTorch with CUDA support. "
            "No CPU fallback is provided."
        )


class Worker:
    """Persistent MatAnyone2 native inference worker."""

    def __init__(self) -> None:
        self.model: Any | None = None
        self.device: str | None = None
        self.loaded: bool = False
        self.torch: Any | None = None
        self.np: Any | None = None
        self.cancelled: set[str] = set()

    def status(self) -> dict[str, Any]:
        payload = self_check_payload()
        payload["loaded"] = self.loaded
        payload["device"] = self.device
        return payload

    def load(self, request: dict[str, Any], progress: Callable | None = None) -> dict[str, Any]:
        if self.loaded:
            return {"status": "ok", "loaded": True, "device": self.device}

        blockers = _check_dependencies()
        if blockers:
            return {"status": "blocked", "blockers": blockers, "warning_text": WARNING_TEXT}

        # Enforce CUDA-only: _choose_device raises if CUDA is unavailable
        try:
            self.device = _choose_device()
        except RuntimeError as e:
            return {"status": "blocked", "blockers": [str(e)], "warning_text": WARNING_TEXT}

        import torch
        import numpy as np
        from matanyone2.model.matanyone2 import MatAnyone2

        self.torch = torch
        self.np = np

        # Free any stale CUDA allocations before model load
        if torch.cuda.is_available():
            torch.cuda.empty_cache()

        if progress:
            progress({"message": "Loading MatAnyone2 model weights", "percent": 10})

        model_dir = _find_model_dir()
        assert model_dir is not None  # checked above

        try:
            # MatAnyone2 uses PyTorchModelHubMixin, so from_pretrained handles safetensors
            self.model = MatAnyone2.from_pretrained(str(model_dir))
            self.model.to(self.device).eval()
        except RuntimeError as e:
            # Catch CUDA OOM during model load
            if 'out of memory' in str(e).lower() or 'CUDA' in str(e):
                del self.model
                self.model = None
                if torch.cuda.is_available():
                    torch.cuda.empty_cache()
                return {
                    "status": "blocked",
                    "blockers": [
                        f"CUDA out of memory loading MatAnyone2 model: {e}. "
                        "Try lowering FLUX_MATANYONE2_MAX_FRAMES or using a lower resolution source."
                    ],
                    "warning_text": WARNING_TEXT,
                }
            raise

        # Release unused CUDA memory after model load
        if torch.cuda.is_available():
            torch.cuda.empty_cache()

        self.loaded = True
        if progress:
            progress({"message": "MatAnyone2 model loaded", "percent": 30})

        return {
            "status": "ok",
            "loaded": True,
            "device": self.device,
            "model_dir": str(model_dir),
            "warning_text": WARNING_TEXT,
        }

    def unload(self, request: dict[str, Any]) -> dict[str, Any]:
        if self.model is not None:
            del self.model
            self.model = None
        if self.torch is not None and self.torch.cuda.is_available():
            self.torch.cuda.empty_cache()
        self.loaded = False
        return {"status": "ok", "loaded": False}

    def infer_video(self, request: dict[str, Any], progress: Callable | None = None) -> dict[str, Any]:
        if not self.loaded or self.model is None:
            return {"status": "blocked", "blockers": ["Model not loaded. Send 'load' first."], "warning_text": WARNING_TEXT}

        errors = self._validate_request(request)
        if errors:
            return {"status": "blocked", "blockers": errors, "warning_text": WARNING_TEXT}

        from matanyone2.inference.inference_core import InferenceCore
        import torch
        import torch.nn.functional as F
        import numpy as np
        from PIL import Image

        # Free any stale CUDA allocations before inference starts
        if self.torch is not None and self.torch.cuda.is_available():
            self.torch.cuda.empty_cache()

        frames_meta = request.get("frames", [])
        mask_path = request.get("first_frame_mask")
        output_dir = Path(str(request.get("output_dir")))
        output_dir.mkdir(parents=True, exist_ok=True)
        source_frames = [int(f.get("source_frame", i + 1)) for i, f in enumerate(frames_meta)]
        n_warmup = 10

        # Load mask
        mask_img = Image.open(str(mask_path)).convert("L")
        mask_arr = np.array(mask_img)
        mask_tensor = torch.from_numpy(mask_arr).float().to(self.device)

        # Initialize processor
        processor = InferenceCore(self.model, cfg=self.model.cfg, device=self.device)

        if progress:
            progress({"message": "MatAnyone2 starting inference", "percent": 5})

        # Warmup: repeat first frame
        first_frame_path = Path(str(frames_meta[0]["path"]))
        first_img = Image.open(first_frame_path).convert("RGB")
        first_arr = np.array(first_img)
        first_tensor = torch.from_numpy(first_arr).permute(2, 0, 1).float()
        warmup_frames = [first_tensor] * n_warmup

        # Load all subsequent frames
        real_frames = []
        for fm in frames_meta:
            p = Path(str(fm["path"]))
            img = Image.open(p).convert("RGB")
            arr = np.array(img)
            real_frames.append(torch.from_numpy(arr).permute(2, 0, 1).float())

        all_frames = warmup_frames + real_frames
        objects = [1]

        phas: list[dict[str, Any]] = []
        request_id = request.get("id", "")

        # Run entire inference loop under inference_mode to prevent
        # gradient tracking (fixes .numpy() on grad tensor errors and
        # avoids wasting GPU memory on unnecessary autograd graphs)
        try:
            with torch.inference_mode():
                for ti, frame_tensor in enumerate(all_frames):
                    if request_id in self.cancelled:
                        self.cancelled.discard(request_id)
                        return {"status": "blocked", "blockers": ["Cancelled by user."], "warning_text": WARNING_TEXT}

                    image = (frame_tensor / 255.0).float().to(self.device)

                    if ti == 0:
                        output_prob = processor.step(image, mask_tensor, objects=objects)
                        output_prob = processor.step(image, first_frame_pred=True)
                    elif ti <= n_warmup:
                        output_prob = processor.step(image, first_frame_pred=True)
                    else:
                        output_prob = processor.step(image)

                    # Extract alpha
                    alpha_mask = processor.output_prob_to_mask(output_prob)
                    alpha_np = alpha_mask.unsqueeze(2).detach().cpu().numpy()  # H W 1
                    alpha_np = np.round(np.clip(alpha_np * 255.0, 0, 255)).astype(np.uint8)

                    # Skip warmup frames
                    real_idx = ti - n_warmup
                    if real_idx >= 0 and real_idx < len(source_frames):
                        frame_num = source_frames[real_idx]
                        dest_name = f"alpha_{frame_num:06d}.png"
                        dest = output_dir / dest_name

                        alpha_img = Image.fromarray(alpha_np.squeeze(2), mode="L")
                        alpha_img.save(str(dest))
                        # Also save PGM for Engine-side overlay loading
                        pgm_dest = dest.with_suffix(".pgm")
                        alpha_arr_save = np.asarray(alpha_img)
                        with open(str(pgm_dest), "wb") as pgm_f:
                            h, w = alpha_arr_save.shape
                            pgm_f.write(f"P5\n{w} {h}\n255\n".encode())
                            pgm_f.write(alpha_arr_save.tobytes())

                        nonzero = 0
                        try:
                            arr_check = np.asarray(alpha_img)
                            nonzero = int(np.count_nonzero(arr_check))
                        except Exception:
                            nonzero = -1  # could not check

                        phas.append({
                            "path": str(dest),
                            "source_frame": frame_num,
                            "timeline_frame": int(frames_meta[real_idx].get("timeline_frame", frame_num)),
                            "nonzero_pixels": nonzero,
                        })

                        if progress:
                            pct = 10 + int(((real_idx + 1) / max(1, len(real_frames))) * 85)
                            progress({
                                "message": f"MatAnyone2 frame {real_idx + 1}/{len(real_frames)}",
                                "percent": pct,
                                "completed": real_idx + 1,
                                "total": len(real_frames),
                            })
        except RuntimeError as oom_err:
            # Explicit CUDA OOM catch — return a clear error payload instead of traceback crash
            if 'out of memory' in str(oom_err).lower() or 'CUDA' in str(oom_err):
                if self.torch is not None and self.torch.cuda.is_available():
                    self.torch.cuda.empty_cache()
                saved_count = len(phas)
                return {
                    "status": "error",
                    "error": (
                        f"CUDA out of memory during MatAnyone2 inference (saved {saved_count}/{len(real_frames)} frames). "
                        f"Try lowering FLUX_MATANYONE2_MAX_FRAMES (currently {len(real_frames)}) or using a lower resolution source."
                    ),
                    "saved_frames": saved_count,
                    "total_frames": len(real_frames),
                    "frames": phas,
                    "warning_text": WARNING_TEXT,
                }
            raise

        successful_count = len(phas)
        total_nonzero = sum(p.get("nonzero_pixels", 0) for p in phas if p.get("nonzero_pixels", 0) > 0)
        has_unverified = any(p.get("nonzero_pixels", 0) == -1 for p in phas)
        verified = total_nonzero > 0 or has_unverified

        selected_mask_path = str(phas[0]["path"]) if phas else None
        selected_pattern = str(output_dir / "alpha_######.png")

        return {
            "status": "ok" if successful_count > 0 and verified else "blocked",
            "selected_mask_path": selected_mask_path,
            "selected_mask_sequence_pattern": selected_pattern,
            "successful_frame_count": successful_count,
            "frames": phas,
            "proofs": {
                "video": {
                    "status": "succeeded" if verified else "blocked",
                    "nonzero_pixels": total_nonzero,
                    "nonzero_unverified": has_unverified,
                    "successful_frame_count": successful_count,
                },
            },
            "warning_text": WARNING_TEXT,
            "schema": SCHEMA,
        }

    def _validate_request(self, request: dict[str, Any]) -> list[str]:
        errors: list[str] = []
        frames = request.get("frames")
        if not frames or not isinstance(frames, list) or len(frames) == 0:
            errors.append("Request must contain a non-empty 'frames' array.")
        mask = request.get("first_frame_mask")
        if not mask:
            errors.append("Request must contain 'first_frame_mask'.")
        elif not Path(str(mask)).is_file():
            errors.append(f"First-frame mask file not found: {mask}")
        out = request.get("output_dir")
        if not out:
            errors.append("Request must contain 'output_dir'.")
        rs = request.get("source_range_start")
        re_ = request.get("source_range_end")
        if rs is not None and re_ is not None and int(rs) >= int(re_):
            errors.append(f"source_range_start ({rs}) must be less than source_range_end ({re_}).")
        # Verify frame files exist
        if frames:
            for i, f in enumerate(frames[:3]):
                p = Path(str(f.get("path", "")))
                if not p.is_file():
                    errors.append(f"Source frame file not found: {p}")
                    break
        return errors

    def cancel(self, request: dict[str, Any]) -> dict[str, Any]:
        rid = request.get("id", "")
        if rid:
            self.cancelled.add(rid)
        return {"status": "ok", "cancelled": rid}

    def shutdown(self, request: dict[str, Any]) -> dict[str, Any]:
        self.unload(request)
        return {"status": "ok", "action": "shutdown"}


def main() -> None:
    import argparse

    parser = argparse.ArgumentParser(description="Flux native MatAnyone2 worker")
    parser.add_argument("--self-check", action="store_true", help="Emit self-check JSON and exit.")
    args = parser.parse_args()

    if args.self_check:
        payload = self_check_payload()
        print(json.dumps(payload, indent=2))
        sys.exit(0 if payload["ready"] else 2)

    worker = Worker()

    # Emit ready
    ready = {"event": "ready", "schema": SCHEMA, "warning_text": WARNING_TEXT}
    print(json.dumps(ready), flush=True)

    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            request = json.loads(line)
        except json.JSONDecodeError:
            print(json.dumps({"event": "error", "message": "Invalid JSON"}), flush=True)
            continue

        command = request.get("command", "")
        request_id = request.get("id", "")

        def make_progress(req_id: str, cmd: str) -> Callable:
            def progress(payload: dict) -> None:
                out = {"event": "progress", "id": req_id, "command": cmd}
                out.update(payload)
                print(json.dumps(out), flush=True)
            return progress

        try:
            if command == "status" or command == "self_check":
                result = worker.status()
            elif command == "load":
                result = worker.load(request, progress=make_progress(request_id, command))
            elif command == "unload":
                result = worker.unload(request)
            elif command == "infer_video":
                result = worker.infer_video(request, progress=make_progress(request_id, command))
            elif command == "cancel":
                result = worker.cancel(request)
            elif command == "shutdown":
                result = worker.shutdown(request)
                response = {"id": request_id, "command": command, "ok": True, "payload": result}
                print(json.dumps(response), flush=True)
                sys.exit(0)
            else:
                result = {"status": "blocked", "blockers": [f"Unknown command: {command}"]}
        except Exception as exc:
            result = {
                "status": "error",
                "error": f"{type(exc).__name__}: {exc}",
                "traceback": traceback.format_exc(),
            }

        ok = result.get("status") in ("ok",) or (command in ("load",) and result.get("loaded"))
        response = {"id": request_id, "command": command, "ok": ok, "payload": result, "schema": SCHEMA}
        print(json.dumps(response), flush=True)


if __name__ == "__main__":
    main()
