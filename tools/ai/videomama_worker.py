#!/usr/bin/env python3
"""VideoMaMa JSON-lines worker for Flux.

Loads VideoInferencePipeline from a user-managed VideoMaMa/SVD model directory,
runs chunked video diffusion inference using cond_frames + mask_frames,
and writes per-frame grayscale alpha PNGs.

VideoMaMa is CC BY-NC 4.0 licensed; visible non-commercial warning required.

T083 policy: pipeline code and model weights are user-managed/manual downloads.
The worker does NOT bundle or assume a bundled pipeline. It loads
pipeline_svd_mask_numpy.py from:
  (a) next to model weights in the model directory,
  (b) FLUX_VIDEOMAMA_CODE_ROOT env var, or
  (c) importable via PYTHONPATH.
Clear error if absent.
"""
from __future__ import annotations

import os
os.environ['PYTORCH_CUDA_ALLOC_CONF'] = 'expandable_segments:True'

import json
import re
import sys
import time
import traceback
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Callable

SCHEMA = "org.flux.ai.videomama-worker.v1"
WARNING_TEXT = (
    "NON-COMMERCIAL: VideoMaMa is CC BY-NC 4.0 licensed. "
    "Visible warning required. Commercial production use is not cleared."
)

# Model search roots: Flux model dir + optional env override
_DEFAULT_MODEL_ROOT = Path.home() / ".local/share/Flux/models/videomama"


def _model_search_roots() -> list[Path]:
    """Return ordered list of directories to search for VideoMaMa model."""
    roots: list[Path] = []
    env_root = os.environ.get("FLUX_VIDEOMAMA_MODEL_ROOT")
    if env_root:
        roots.append(Path(env_root))
    roots.append(_DEFAULT_MODEL_ROOT)
    return roots


def _find_model_dir() -> Path | None:
    """Find the VideoMaMa model directory containing model_index.json."""
    for root in _model_search_roots():
        if not root.is_dir():
            continue
        for d in sorted(root.iterdir()):
            if d.is_dir() and (d / "model_index.json").is_file():
                # Also check that vae and unet subfolders exist
                if (d / "vae" / "config.json").is_file() and (d / "unet" / "config.json").is_file():
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
        import diffusers  # noqa: F401
    except ImportError:
        blockers.append(
            "diffusers is not installed. "
            "Install with: pip install diffusers>=0.24.0"
        )
    try:
        import numpy  # noqa: F401
    except ImportError:
        blockers.append("numpy is not installed.")
    try:
        from PIL import Image  # noqa: F401
    except ImportError:
        blockers.append("Pillow is not installed.")
    model_dir = _find_model_dir()
    if model_dir is None:
        blockers.append(
            "VideoMaMa model weights not found. The model requires TWO sources:\n"
            "  1) SVD base model from stabilityai/stable-video-diffusion-img2vid-xt "
            "(provides vae/, scheduler/, feature_extractor/, image_encoder/)\n"
            "  2) VideoMaMa UNet from SammyLim/VideoMaMa "
            "(provides unet/ with fine-tuned weights)\n"
            "Both must be merged into a single directory under "
            + str(_DEFAULT_MODEL_ROOT)
            + "/ with model_index.json, unet/, and vae/ subdirectories.\n"
            "Use the Flux model manager or manual download. "
            "Searched: "
            + ", ".join(str(r) for r in _model_search_roots())
        )
    else:
        # Check pipeline code availability (user-managed, not bundled)
        pipeline_in_model = model_dir / "pipeline_svd_mask_numpy.py"
        code_root_env = os.environ.get("FLUX_VIDEOMAMA_CODE_ROOT")
        pipeline_in_env = Path(code_root_env) / "pipeline_svd_mask_numpy.py" if code_root_env else None
        try:
            from videomama.pipeline_svd_mask_numpy import VideoInferencePipeline  # noqa: F401
            pipeline_importable = True
        except ImportError:
            pipeline_importable = False

        if not pipeline_in_model.is_file() and not pipeline_importable and (not pipeline_in_env or not pipeline_in_env.is_file()):
            blockers.append(
                "VideoMaMa pipeline code (pipeline_svd_mask_numpy.py) not found. "
                "Pipeline code is NOT bundled per T083 non-commercial policy — user must download it. "
                "Options:\n"
                "  1) Place pipeline_svd_mask_numpy.py next to model weights at "
                + str(model_dir) + "/\n"
                "  2) Set FLUX_VIDEOMAMA_CODE_ROOT to the VideoMaMa repo root "
                "(https://github.com/SammyLim/VideoMaMa)\n"
                "  3) Add the VideoMaMa repo to PYTHONPATH"
            )
    return blockers


def self_check_payload() -> dict[str, Any]:
    """Return a self-check payload for --self-check and status commands.

    This is the authoritative readiness check for VideoMaMa — it checks
    Python deps, model weights, pipeline code, and CUDA availability.
    The flux_provider_runtime import probe is a lighter check; this worker
    self-check is the source of truth for actual readiness.
    """
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
            "CUDA is not available. VideoMaMa requires an NVIDIA GPU with CUDA. "
            "No CPU fallback is provided."
        )
    # Check pipeline code location for diagnostics
    pipeline_location = None
    if model_dir:
        pipeline_in_model = model_dir / "pipeline_svd_mask_numpy.py"
        if pipeline_in_model.is_file():
            pipeline_location = str(pipeline_in_model)
    if not pipeline_location:
        code_root = os.environ.get("FLUX_VIDEOMAMA_CODE_ROOT")
        if code_root:
            pipeline_in_env = Path(code_root) / "pipeline_svd_mask_numpy.py"
            if pipeline_in_env.is_file():
                pipeline_location = str(pipeline_in_env)
    payload: dict[str, Any] = {
        "worker_schema": SCHEMA,
        "ready": len(blockers) == 0,
        "warning_text": WARNING_TEXT,
        "model_dir": str(model_dir) if model_dir else None,
        "pipeline_code_location": pipeline_location,
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
            "CUDA is not available. VideoMaMa requires an NVIDIA GPU with CUDA. "
            "No CPU fallback is provided."
        )
    except ImportError:
        raise RuntimeError(
            "PyTorch (torch) is not installed. VideoMaMa requires PyTorch with CUDA support."
        )


def _resolve_mask_path_for_frame(
    mask_sequence_pattern: str | None,
    first_frame_mask: str | None,
    source_frame: int,
    project_dir: str | None,
) -> Path | None:
    """Resolve a per-frame mask path from a sequence pattern.

    If mask_sequence_pattern contains ###### or %06d-style, substitute the frame
    number and check if the file exists. Falls back to first_frame_mask.
    """
    if mask_sequence_pattern:
        # Normalize separators
        pat = mask_sequence_pattern
        # Handle project-relative: if not absolute, resolve against project_dir
        if not os.path.isabs(pat) and project_dir:
            pat = os.path.join(project_dir, pat)

        # Replace ###### with zero-padded frame number
        def _sub_hashes(m: re.Match) -> str:
            width = len(m.group(0))
            return str(source_frame).zfill(width)

        resolved = re.sub(r'#{2,}', _sub_hashes, pat)

        # Also handle printf-style %06d
        if '#' not in mask_sequence_pattern and '%' in mask_sequence_pattern:
            try:
                resolved = mask_sequence_pattern % source_frame
                if not os.path.isabs(resolved) and project_dir:
                    resolved = os.path.join(project_dir, resolved)
            except (TypeError, ValueError):
                pass

        p = Path(resolved)
        if p.is_file():
            return p

    # Fallback to first_frame_mask
    if first_frame_mask:
        p = Path(first_frame_mask)
        if p.is_file():
            return p

    return None


class Worker:
    """Persistent VideoMaMa inference worker."""

    def __init__(self) -> None:
        self.pipeline: Any | None = None
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

        try:
            self.device = _choose_device()
        except RuntimeError as e:
            return {"status": "blocked", "blockers": [str(e)], "warning_text": WARNING_TEXT}

        import torch
        import numpy as np

        self.torch = torch
        self.np = np

        if torch.cuda.is_available():
            torch.cuda.empty_cache()

        if progress:
            progress({"message": "Loading VideoMaMa model weights", "percent": 10})

        model_dir = _find_model_dir()
        assert model_dir is not None  # checked above

        try:
            pipeline_cls = self._resolve_pipeline_class(model_dir)

            self.pipeline = pipeline_cls(
                base_model_path=str(model_dir),
                unet_checkpoint_path=str(model_dir),
                weight_dtype=torch.float16,
                device=self.device,
                enable_model_cpu_offload=False,
                vae_encode_chunk_size=1,
                attention_mode="auto",
                enable_vae_tiling=False,
                enable_vae_slicing=True,
            )
        except RuntimeError as e:
            if 'out of memory' in str(e).lower() or 'CUDA' in str(e):
                del self.pipeline
                self.pipeline = None
                if torch.cuda.is_available():
                    torch.cuda.empty_cache()
                return {
                    "status": "blocked",
                    "blockers": [
                        f"CUDA out of memory loading VideoMaMa model: {e}. "
                        "Try reducing the frame range or using a lower resolution source."
                    ],
                    "warning_text": WARNING_TEXT,
                }
            raise
        except Exception as e:
            del self.pipeline
            self.pipeline = None
            if torch.cuda.is_available():
                torch.cuda.empty_cache()
            return {
                "status": "blocked",
                "blockers": [
                    f"Failed to load VideoMaMa pipeline: {e}. "
                    "Ensure the model directory contains valid SVD base model files "
                    "(model_index.json, vae/config.json from stabilityai/stable-video-diffusion-img2vid-xt) "
                    "and VideoMaMa UNet weights (unet/config.json from SammyLim/VideoMaMa)."
                ],
                "warning_text": WARNING_TEXT,
            }

        if torch.cuda.is_available():
            torch.cuda.empty_cache()

        self.loaded = True
        if progress:
            progress({"message": "VideoMaMa model loaded", "percent": 30})
        sys.stdout.flush()
        time.sleep(0)

        return {
            "status": "ok",
            "loaded": True,
            "device": self.device,
            "model_dir": str(model_dir),
            "warning_text": WARNING_TEXT,
        }

    def _resolve_pipeline_class(self, model_dir: Path) -> type:
        """Resolve the VideoInferencePipeline class.

        T083 policy: pipeline code is user-managed, NOT bundled.
        Search order:
          1. Pipeline script co-located with model weights (user places it there)
          2. FLUX_VIDEOMAMA_CODE_ROOT env var pointing to the videomama repo root
          3. sys.path / PYTHONPATH import (pip install -e or PYTHONPATH)
        """
        # 1. Co-located with model weights
        pipeline_script = model_dir / "pipeline_svd_mask_numpy.py"
        if pipeline_script.is_file():
            import importlib.util
            spec = importlib.util.spec_from_file_location("videomama_pipeline", str(pipeline_script))
            if spec and spec.loader:
                mod = importlib.util.module_from_spec(spec)
                spec.loader.exec_module(mod)
                return mod.VideoInferencePipeline

        # 2. FLUX_VIDEOMAMA_CODE_ROOT env var
        code_root = os.environ.get("FLUX_VIDEOMAMA_CODE_ROOT")
        if code_root:
            code_script = Path(code_root) / "pipeline_svd_mask_numpy.py"
            if code_script.is_file():
                import importlib.util
                spec = importlib.util.spec_from_file_location("videomama_pipeline", str(code_script))
                if spec and spec.loader:
                    mod = importlib.util.module_from_spec(spec)
                    spec.loader.exec_module(mod)
                    return mod.VideoInferencePipeline

        # 3. sys.path / PYTHONPATH import
        try:
            from videomama.pipeline_svd_mask_numpy import VideoInferencePipeline  # type: ignore[import-untyped]
            return VideoInferencePipeline
        except ImportError:
            pass

        raise ImportError(
            "Cannot find VideoInferencePipeline (pipeline_svd_mask_numpy.py). "
            "VideoMaMa pipeline code is NOT bundled — it must be user-downloaded per "
            "T083 non-commercial model/code policy. Place pipeline_svd_mask_numpy.py "
            f"next to the model weights at {model_dir}/, set FLUX_VIDEOMAMA_CODE_ROOT "
            "to the VideoMaMa repo root (https://github.com/SammyLim/VideoMaMa), "
            "or add the videomama repo to PYTHONPATH."
        )

    def unload(self, request: dict[str, Any]) -> dict[str, Any]:
        if self.pipeline is not None:
            try:
                if hasattr(self.pipeline, 'unet'):
                    self.pipeline.unet = None
                if hasattr(self.pipeline, 'vae'):
                    self.pipeline.vae = None
            except Exception:
                pass
            del self.pipeline
            self.pipeline = None
        if self.torch is not None and self.torch.cuda.is_available():
            self.torch.cuda.empty_cache()
        self.loaded = False
        return {"status": "ok", "loaded": False}

    def infer_video(self, request: dict[str, Any], progress: Callable | None = None) -> dict[str, Any]:
        if not self.loaded or self.pipeline is None:
            return {"status": "blocked", "blockers": ["Model not loaded. Send 'load' first."], "warning_text": WARNING_TEXT}

        errors = self._validate_request(request)
        if errors:
            return {"status": "blocked", "blockers": errors, "warning_text": WARNING_TEXT}

        import torch
        import numpy as np
        from PIL import Image

        if self.torch is not None and self.torch.cuda.is_available():
            self.torch.cuda.empty_cache()

        frames_meta = request.get("frames", [])
        mask_path = request.get("first_frame_mask")
        output_dir = Path(str(request.get("output_dir")))
        output_dir.mkdir(parents=True, exist_ok=True)
        source_frames = [int(f.get("source_frame", i + 1)) for i, f in enumerate(frames_meta)]
        batch_size = int(request.get("chunk_size", 16))
        overlap = int(request.get("overlap", 2))

        # Mask sequence pattern: per-frame masks (from SAM3 tracking or similar)
        mask_sequence_pattern = request.get("mask_sequence_pattern")
        # Project dir for resolving relative paths
        project_dir = request.get("project_dir")

        # Load first-frame mask as grayscale (fallback)
        first_mask_arr: np.ndarray | None = None
        if mask_path:
            mask_img = Image.open(str(mask_path)).convert("L")
            first_mask_arr = np.array(mask_img)

        if progress:
            progress({"message": "VideoMaMa loading frames", "percent": 5})

        def _resize_uint8_array_to_shape(arr_in: np.ndarray, target_shape: tuple[int, int], resample: int) -> np.ndarray:
            """Resize a grayscale/RGB uint8 array to (height, width) when VideoMaMa changes size."""
            target_h, target_w = target_shape
            arr_np = np.asarray(arr_in)
            if arr_np.shape[:2] == (target_h, target_w):
                return arr_np.astype(np.uint8, copy=False)
            img_in = Image.fromarray(arr_np.astype(np.uint8, copy=False))
            img_out = img_in.resize((target_w, target_h), resample)
            return np.array(img_out)

        # Load all source frames and per-frame masks
        cond_frames: list[np.ndarray] = []
        mask_frames: list[np.ndarray] = []

        for idx, fm in enumerate(frames_meta):
            p = Path(str(fm["path"]))
            img = Image.open(p).convert("RGB")
            arr = np.array(img)
            cond_frames.append(arr)

            # Resolve per-frame mask
            source_frame = source_frames[idx]
            timeline_frame = int(frames_meta[idx].get("timeline_frame", source_frame))
            frame_mask_path = _resolve_mask_path_for_frame(
                mask_sequence_pattern=mask_sequence_pattern,
                first_frame_mask=mask_path,
                source_frame=timeline_frame,
                project_dir=project_dir,
            )

            if frame_mask_path is not None:
                frame_mask = Image.open(str(frame_mask_path)).convert("L")
                frame_mask_arr = np.array(frame_mask)
                # Resize mask to match frame if needed
                if frame_mask_arr.shape[:2] != arr.shape[:2]:
                    frame_mask = frame_mask.resize((arr.shape[1], arr.shape[0]), Image.NEAREST)
                    frame_mask_arr = np.array(frame_mask)
                mask_frames.append(frame_mask_arr)
            elif first_mask_arr is not None:
                # Fallback to first-frame mask repeated
                mask_frames.append(first_mask_arr.copy())
            else:
                return {
                    "status": "blocked",
                    "blockers": [
                        f"No mask available for source frame {source_frame} / timeline frame {timeline_frame} and no first_frame_mask fallback."
                    ],
                    "warning_text": WARNING_TEXT,
                }

        n_frames = len(cond_frames)
        request_id = request.get("id", "")

        # Generate sliding windows for chunked processing
        windows = self._generate_windows(n_frames, batch_size, overlap)
        phas: list[dict[str, Any]] = []

        # Track soft overlap masks for continuity between chunks
        previous_overlap_masks: list[np.ndarray] | None = None

        if progress:
            progress({"message": f"VideoMaMa starting inference ({len(windows)} batches)", "percent": 8})

        try:
            with torch.inference_mode():
                for batch_idx, (win_start, win_end) in enumerate(windows):
                    # Check cancellation between chunks
                    # Note: in-flight pipeline.run cannot be interrupted,
                    # but next chunk will stop if cancel was requested.
                    if request_id in self.cancelled:
                        self.cancelled.discard(request_id)
                        return {
                            "status": "cancelled",
                            "blockers": ["Cancelled by user."],
                            "saved_frames": len(phas),
                            "total_frames": n_frames,
                            "frames": phas,
                            "warning_text": WARNING_TEXT,
                        }

                    batch_cond = cond_frames[win_start:win_end]
                    batch_masks = mask_frames[win_start:win_end]

                    # Inject previous overlap masks for continuity
                    is_first_batch = (batch_idx == 0)
                    output_start_offset = 0 if is_first_batch else overlap

                    if not is_first_batch and previous_overlap_masks is not None:
                        for i in range(min(overlap, len(batch_masks), len(previous_overlap_masks))):
                            batch_masks[i] = _resize_uint8_array_to_shape(
                                previous_overlap_masks[i],
                                batch_masks[i].shape[:2],
                                Image.BILINEAR,
                            )

                    # Run the pipeline
                    output_frames = self.pipeline.run(
                        cond_frames=batch_cond,
                        mask_frames=batch_masks,
                        seed=42,
                    )

                    # Capture soft overlap masks for next batch
                    if overlap > 0 and len(output_frames) >= overlap:
                        previous_overlap_masks = []
                        for frame_out in output_frames[-overlap:]:
                            if frame_out.ndim == 3 and frame_out.shape[2] == 3:
                                alpha_out = np.mean(frame_out, axis=2).astype(np.uint8)
                            else:
                                alpha_out = frame_out
                            previous_overlap_masks.append(alpha_out)

                    # Commit output frames (skip overlap on non-first batches)
                    committed = output_frames[output_start_offset:]

                    for i, frame_out in enumerate(committed):
                        real_idx = win_start + output_start_offset + i
                        if real_idx < 0 or real_idx >= n_frames:
                            continue

                        # Convert RGB output to grayscale alpha
                        if frame_out.ndim == 3 and frame_out.shape[2] == 3:
                            alpha_np = np.mean(frame_out, axis=2).astype(np.uint8)
                        else:
                            alpha_np = np.array(frame_out, dtype=np.uint8)
                            if alpha_np.ndim == 3:
                                alpha_np = alpha_np[:, :, 0]

                        alpha_np = _resize_uint8_array_to_shape(
                            alpha_np,
                            cond_frames[real_idx].shape[:2],
                            Image.BILINEAR,
                        )

                        frame_num = source_frames[real_idx]
                        dest_name = f"alpha_{frame_num:06d}.png"
                        dest = output_dir / dest_name

                        alpha_img_out = Image.fromarray(alpha_np, mode="L")
                        alpha_img_out.save(str(dest))

                        # Save PGM for Engine-side overlay loading
                        pgm_dest = dest.with_suffix(".pgm")
                        alpha_arr_save = np.asarray(alpha_img_out)
                        with open(str(pgm_dest), "wb") as pgm_f:
                            h, w = alpha_arr_save.shape
                            pgm_f.write(f"P5\n{w} {h}\n255\n".encode())
                            pgm_f.write(alpha_arr_save.tobytes())

                        nonzero = 0
                        try:
                            nonzero = int(np.count_nonzero(alpha_arr_save))
                        except Exception:
                            nonzero = -1

                        phas.append({
                            "path": str(dest),
                            "source_frame": frame_num,
                            "timeline_frame": int(frames_meta[real_idx].get("timeline_frame", frame_num)),
                            "nonzero_pixels": nonzero,
                        })

                    if progress:
                        pct = 10 + int(((batch_idx + 1) / max(1, len(windows))) * 85)
                        progress({
                            "message": f"VideoMaMa batch {batch_idx + 1}/{len(windows)}",
                            "percent": pct,
                            "completed": batch_idx + 1,
                            "total": len(windows),
                        })

                    # Free GPU between batches
                    if self.torch is not None and self.torch.cuda.is_available():
                        self.torch.cuda.empty_cache()

        except RuntimeError as oom_err:
            if 'out of memory' in str(oom_err).lower() or 'CUDA' in str(oom_err):
                if self.torch is not None and self.torch.cuda.is_available():
                    self.torch.cuda.empty_cache()
                saved_count = len(phas)
                return {
                    "status": "error",
                    "error": (
                        f"CUDA out of memory during VideoMaMa inference "
                        f"(saved {saved_count}/{n_frames} frames). "
                        f"Try reducing the frame range or using a lower resolution source."
                    ),
                    "saved_frames": saved_count,
                    "total_frames": n_frames,
                    "frames": phas,
                    "warning_text": WARNING_TEXT,
                }
            raise

        successful_count = len(phas)
        total_nonzero = sum(p.get("nonzero_pixels", 0) for p in phas if p.get("nonzero_pixels", 0) > 0)
        has_unverified = any(p.get("nonzero_pixels", 0) == -1 for p in phas)
        verified = total_nonzero > 0 or has_unverified

        # Use project-relative path for output when possible
        output_dir_str = str(output_dir)
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

    def _generate_windows(self, total_frames: int, batch_size: int, overlap: int) -> list[tuple[int, int]]:
        """Generate sliding window (start, end) pairs. end is exclusive."""
        step = batch_size - overlap
        if step <= 0:
            overlap = batch_size - 1
            step = 1
        if total_frames <= batch_size:
            return [(0, total_frames)]
        windows = []
        pos = 0
        while pos < total_frames:
            end = min(pos + batch_size, total_frames)
            windows.append((pos, end))
            if end >= total_frames:
                break
            pos += step
        return windows

    def _validate_request(self, request: dict[str, Any]) -> list[str]:
        errors: list[str] = []
        frames = request.get("frames")
        if not frames or not isinstance(frames, list) or len(frames) == 0:
            errors.append("Request must contain a non-empty 'frames' array.")
        mask = request.get("first_frame_mask")
        mask_seq = request.get("mask_sequence_pattern")
        if not mask and not mask_seq:
            errors.append("Request must contain 'first_frame_mask' or 'mask_sequence_pattern'.")
        if mask and not Path(str(mask)).is_file():
            errors.append(f"First-frame mask file not found: {mask}")
        out = request.get("output_dir")
        if not out:
            errors.append("Request must contain 'output_dir'.")
        rs = request.get("source_range_start")
        re_ = request.get("source_range_end")
        if rs is not None and re_ is not None and int(rs) >= int(re_):
            errors.append(f"source_range_start ({rs}) must be less than source_range_end ({re_}).")
        if frames:
            for i, f in enumerate(frames[:3]):
                p = Path(str(f.get("path", "")))
                if not p.is_file():
                    errors.append(f"Source frame file not found: {p}")
                    break
        return errors

    def cancel(self, request: dict[str, Any]) -> dict[str, Any]:
        # GUI sends 'target_id', worker protocol uses 'id' — accept both
        rid = request.get("target_id") or request.get("id", "")
        if rid:
            self.cancelled.add(rid)
        return {"status": "ok", "cancelled": rid}

    def shutdown(self, request: dict[str, Any]) -> dict[str, Any]:
        self.unload(request)
        return {"status": "ok", "action": "shutdown"}


def main() -> None:
    import argparse

    parser = argparse.ArgumentParser(description="Flux VideoMaMa worker")
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
