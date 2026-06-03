#!/usr/bin/env python3
"""SAM3 ONNX Preview Worker — Phase 3 single-frame guide-mask preview.

Accepts a JSON request (stdin or --request <json>) with:
  - source image path (RGB-only PPM P6 or raw RGB + metadata)
  - prompt list (point/box, positive/negative)
  - output mask path
  - preferred ORT providers

Produces JSON result to stdout with status, mask path, dimensions,
providers used, per-prompt info, elapsed time.

--self-check validates schema, deps, model paths, ORT providers without inference.

--persistent mode: worker stays alive, reads JSON-lines from stdin,
writes JSON-line events to stdout. ONNX sessions are loaded once and
cached across requests. Events: loading_model, model_ready, running,
progress, done, error, shutdown_ack.
"""

import argparse
import json
import os
import sys
import time
from pathlib import Path
from typing import Any

SCHEMA = "org.flux.ai.sam3-onnx-preview-worker.v1"

default_point_onnx_rel = "tools/ai_native_spikes/results/sam3/export/onnx/point/sam3_tracker_point.onnx"
default_box_onnx_rel = "tools/ai_native_spikes/results/sam3/export/onnx/box/sam3_tracker_box.onnx"
SAM3_TARGET_SIZE = 1008


def _resolve_repo_root() -> Path:
    """Walk up from this script to find the Flux repo root."""
    p = Path(__file__).resolve().parent
    for _ in range(6):
        if (p / "tools" / "ai").is_dir():
            return p
        p = p.parent
    return Path(__file__).resolve().parent.parent


def _resolve_model_path(
    explicit: str | None,
    env_var: str,
    dev_rel: str,
    repo_root: Path,
) -> str | None:
    """Resolve a single ONNX model path with priority order."""
    if explicit and os.path.isfile(explicit):
        return explicit
    env_val = os.environ.get(env_var, "").strip()
    if env_val and os.path.isfile(env_val):
        return env_val
    dev_path = repo_root / dev_rel
    if dev_path.is_file():
        return str(dev_path)
    return None


def _read_ppm_p6(path: str) -> tuple[bytes, int, int, int]:
    """Read an RGB-only PPM (P6) file. Returns (raw_rgb, width, height, maxval)."""
    with open(path, "rb") as f:
        magic = f.readline().strip()
        if magic != b"P6":
            raise ValueError(f"Expected P6 magic, got {magic!r}")
        # Skip comments
        line = f.readline()
        while line.startswith(b"#"):
            line = f.readline()
        dims = line.strip().split()
        while len(dims) < 2:
            line = f.readline()
            dims += line.strip().split()
        width, height = int(dims[0]), int(dims[1])
        line = f.readline()
        while line.startswith(b"#"):
            line = f.readline()
        maxval = int(line.strip())
        data = f.read()
        expected = width * height * 3
        if len(data) < expected:
            raise ValueError(f"PPM data too short: {len(data)} < {expected}")
        return data[:expected], width, height, maxval


def _read_raw_rgb(
    path: str, width: int, height: int
) -> tuple[bytes, int, int, int]:
    """Read raw RGB bytes with dimensions from metadata."""
    with open(path, "rb") as f:
        data = f.read()
    expected = width * height * 3
    if len(data) < expected:
        raise ValueError(f"Raw RGB data too short: {len(data)} < {expected}")
    return data[:expected], width, height, 255


def _write_pgm_p5(path: str, data: bytes, width: int, height: int) -> None:
    """Write an 8-bit grayscale PGM (P5) file."""
    with open(path, "wb") as f:
        f.write(f"P5\n{width} {height}\n255\n".encode("ascii"))
        f.write(data)


def _preprocess_image(
    rgb_bytes: bytes, width: int, height: int
) -> "numpy.ndarray":
    """Preprocess RGB bytes to SAM3 pixel_values tensor (1,3,1008,1008) float32.

    Standard SAM/imagenet normalization:
      mean = [0.485, 0.456, 0.406]
      std  = [0.229, 0.224, 0.225]
    Resize to 1008x1008 via nearest-neighbor.
    """
    import numpy as np

    # Parse raw bytes to (H, W, 3) uint8
    img = np.frombuffer(rgb_bytes, dtype=np.uint8).reshape(height, width, 3)

    # Convert to float32 [0, 1]
    img = img.astype(np.float32) / 255.0

    # Resize to 1008x1008 using nearest-neighbor
    target_h, target_w = SAM3_TARGET_SIZE, SAM3_TARGET_SIZE
    row_indices = (np.arange(target_h) * height / target_h).astype(int)
    col_indices = (np.arange(target_w) * width / target_w).astype(int)
    row_indices = np.clip(row_indices, 0, height - 1)
    col_indices = np.clip(col_indices, 0, width - 1)
    resized = img[row_indices[:, None], col_indices[None, :], :]  # (1008, 1008, 3)

    # Normalize with ImageNet stats
    mean = np.array([0.485, 0.456, 0.406], dtype=np.float32)
    std = np.array([0.229, 0.224, 0.225], dtype=np.float32)
    resized = (resized - mean) / std

    # HWC -> CHW, add batch dim
    pixel_values = resized.transpose(2, 0, 1)[np.newaxis, :, :, :]  # (1, 3, 1008, 1008)
    return pixel_values.astype(np.float32)


def _scale_point_to_model(point_x: float, point_y: float, width: int, height: int) -> tuple[float, float]:
    """Convert source-frame pixels to SAM3 tracker model coordinates.

    Sam3TrackerProcessor normalizes prompt coordinates to the model target size
    before calling Sam3TrackerModel. Our ONNX wrapper receives the already-
    preprocessed 1008x1008 `pixel_values`, so prompts must be scaled the same
    way here. Passing original 2496x1056 pixels caused >1000px prompt drift.
    """
    return point_x * (SAM3_TARGET_SIZE / float(width)), point_y * (SAM3_TARGET_SIZE / float(height))


def _scale_box_to_model(x1: float, y1: float, x2: float, y2: float, width: int, height: int) -> tuple[float, float, float, float]:
    sx1, sy1 = _scale_point_to_model(x1, y1, width, height)
    sx2, sy2 = _scale_point_to_model(x2, y2, width, height)
    return sx1, sy1, sx2, sy2


def _postprocess_mask(
    mask_array: "numpy.ndarray", orig_w: int, orig_h: int
) -> bytes:
    """Postprocess ONNX output (1,1,3,288,288) to 8-bit grayscale bytes.

    Takes the max across the 3 multimask outputs, bilinear-upscales to
    original dimensions, maps continuous sigmoid output to 0–255 uint8
    (soft alpha). No binary threshold.
    """
    import numpy as np

    # mask_array shape: (1, 1, 3, 288, 288)
    masks = mask_array[0, 0]  # (3, 288, 288)

    # Take max across the 3 multimask outputs
    composite = np.max(masks, axis=0)  # (288, 288)

    # Bilinear upscale to original dimensions
    src_h, src_w = composite.shape
    if src_h == orig_h and src_w == orig_w:
        resized = composite
    else:
        # Pixel-center aligned bilinear: maps destination pixel centers to
        # source pixel centres, eliminating the ~0.5 source-pixel left/top bias
        # that the old edge-origin convention produced.
        y = (np.arange(orig_h, dtype=np.float32) + 0.5) * (src_h / orig_h) - 0.5
        x = (np.arange(orig_w, dtype=np.float32) + 0.5) * (src_w / orig_w) - 0.5
        y = np.clip(y, 0, src_h - 1.0)
        x = np.clip(x, 0, src_w - 1.0)
        y0 = np.floor(y).astype(int)
        x0 = np.floor(x).astype(int)
        y1 = np.minimum(y0 + 1, src_h - 1)
        x1 = np.minimum(x0 + 1, src_w - 1)
        wy = (y - y0).astype(np.float32)
        wx = (x - x0).astype(np.float32)
        tl = composite[y0[:, None], x0[None, :]]
        tr = composite[y0[:, None], x1[None, :]]
        bl = composite[y1[:, None], x0[None, :]]
        br = composite[y1[:, None], x1[None, :]]
        top = tl * (1.0 - wx[None, :]) + tr * wx[None, :]
        bot = bl * (1.0 - wx[None, :]) + br * wx[None, :]
        resized = top * (1.0 - wy[:, None]) + bot * wy[:, None]

    # Map continuous [0,1] to soft uint8 alpha — no binary threshold
    alpha = np.clip(resized * 255.0, 0, 255).astype(np.uint8)
    return alpha.tobytes()


def _make_cuda_session(ort: Any, model_path: str) -> Any:
    """Create an ONNX Runtime session that proves CUDA is active.

    The exported SAM3 ONNX graph contains shape/control-flow nodes that ORT
    assigns to the CPU EP; disabling CPU EP fallback makes session creation fail.
    We therefore register CUDA first and CPU second, then require CUDA to be the
    active first provider. This prevents CPU-only execution while allowing ORT's
    small CPU-assigned bookkeeping nodes.
    """
    sess = ort.InferenceSession(
        model_path,
        providers=["CUDAExecutionProvider", "CPUExecutionProvider"],
    )
    providers = sess.get_providers()
    if not providers or providers[0] != "CUDAExecutionProvider":
        raise RuntimeError(f"SAM3 preview requires CUDA-first session; got providers={providers}")
    return sess


def _run_point_inference(
    session: Any,
    pixel_values: "numpy.ndarray",
    point_x: float,
    point_y: float,
    label: int,
) -> "numpy.ndarray":
    """Run a single point prompt through the ONNX point model."""
    import numpy as np

    input_points = np.array([[[[point_x, point_y]]]], dtype=np.float32)
    input_labels = np.array([[[label]]], dtype=np.int64)

    result = session.run(None, {
        "pixel_values": pixel_values,
        "input_points": input_points,
        "input_labels": input_labels,
    })
    return result[0]  # pred_masks (1, 1, 3, 288, 288)


def _run_box_inference(
    session: Any,
    pixel_values: "numpy.ndarray",
    x1: float,
    y1: float,
    x2: float,
    y2: float,
) -> "numpy.ndarray":
    """Run a single box prompt through the ONNX box model."""
    import numpy as np

    input_boxes = np.array([[[x1, y1, x2, y2]]], dtype=np.float32)

    result = session.run(None, {
        "pixel_values": pixel_values,
        "input_boxes": input_boxes,
    })
    return result[0]  # pred_masks (1, 1, 3, 288, 288)


def _composite_masks(
    positive_masks: list[bytes],
    negative_masks: list[bytes],
    width: int,
    height: int,
) -> bytes:
    """Max-composite positive masks, then smooth-suppress negative masks.

    Negative suppression is proportional: each negative mask attenuates the
    composite by (1 - neg_weight), where neg_weight is the normalised 0–1
    alpha.  This produces smooth soft-edge suppression without hard edges.
    """
    import numpy as np

    n = width * height
    composite = np.zeros(n, dtype=np.float32)

    for m in positive_masks:
        arr = np.frombuffer(m, dtype=np.uint8).astype(np.float32)
        composite = np.maximum(composite, arr)

    for m in negative_masks:
        arr = np.frombuffer(m, dtype=np.uint8).astype(np.float32)
        # Smooth proportional suppression (no hard threshold)
        weight = arr / 255.0  # 0..1
        composite *= (1.0 - weight)

    return np.clip(composite, 0, 255).astype(np.uint8).tobytes()


def do_self_check(
    args: argparse.Namespace,
) -> dict[str, Any]:
    """Validate dependencies, model paths, and ORT providers without inference."""
    result: dict[str, Any] = {
        "schema": SCHEMA,
        "mode": "self-check",
        "status": "ok",
        "dependencies": {},
        "model_paths": {},
        "providers": {"available": [], "used": []},
        "errors": [],
        "warnings": [],
    }

    # Check numpy
    try:
        import numpy as np
        result["dependencies"]["numpy"] = {"available": True, "version": np.__version__}
    except ImportError:
        result["dependencies"]["numpy"] = {"available": False}
        result["errors"].append("numpy not available")
        result["status"] = "blocked"

    # Check onnxruntime
    try:
        import onnxruntime as ort
        available_providers = ort.get_available_providers()
        result["dependencies"]["onnxruntime"] = {
            "available": True,
            "version": ort.__version__,
        }
        result["providers"]["available"] = available_providers
        used = ["CUDAExecutionProvider", "CPUExecutionProvider"] if "CUDAExecutionProvider" in available_providers else []
        result["providers"]["used"] = used
        if "CUDAExecutionProvider" not in available_providers:
            result["errors"].append(
                "CUDAExecutionProvider not available; SAM3 preview cannot run in GPU mode"
            )
            result["status"] = "blocked"
    except ImportError:
        result["dependencies"]["onnxruntime"] = {"available": False}
        result["errors"].append("onnxruntime not available")
        result["status"] = "blocked"

    # Resolve model paths
    repo_root = _resolve_repo_root()
    point_onnx = args.point_onnx or os.environ.get("FLUX_SAM3_POINT_ONNX", "").strip()
    box_onnx = args.box_onnx or os.environ.get("FLUX_SAM3_BOX_ONNX", "").strip()

    resolved_point = _resolve_model_path(
        point_onnx or None, "FLUX_SAM3_POINT_ONNX", default_point_onnx_rel, repo_root
    )
    resolved_box = _resolve_model_path(
        box_onnx or None, "FLUX_SAM3_BOX_ONNX", default_box_onnx_rel, repo_root
    )

    result["model_paths"]["point_onnx"] = {
        "resolved": resolved_point,
        "exists": resolved_point is not None and os.path.isfile(resolved_point),
    }
    result["model_paths"]["box_onnx"] = {
        "resolved": resolved_box,
        "exists": resolved_box is not None and os.path.isfile(resolved_box),
    }

    if not resolved_point:
        result["warnings"].append(
            "Point ONNX model not found. Set FLUX_SAM3_POINT_ONNX or place at dev artifact path."
        )
    if not resolved_box:
        result["warnings"].append(
            "Box ONNX model not found. Set FLUX_SAM3_BOX_ONNX or place at dev artifact path."
        )

    if result["errors"]:
        result["status"] = "blocked"
    elif not resolved_point and not resolved_box:
        result["status"] = "no_models"
        result["warnings"].append(
            "No ONNX models found; inference will fail until models are available."
        )

    return result


def do_inference(request: dict[str, Any]) -> dict[str, Any]:
    """Run SAM3 ONNX preview inference from a request dict."""
    t0 = time.monotonic()
    result: dict[str, Any] = {
        "schema": SCHEMA,
        "mode": "inference",
        "status": "ok",
        "errors": [],
        "warnings": [],
        "per_prompt": [],
    }

    # Validate request basics
    source_path = request.get("source", {}).get("path", "")
    source_w = request.get("source", {}).get("width", 0)
    source_h = request.get("source", {}).get("height", 0)
    source_format = request.get("source", {}).get("format", "ppm")
    output_path = request.get("output", {}).get("mask_path", "")
    prompts = request.get("prompts", [])
    preferred = request.get("providers", ["CUDAExecutionProvider", "CPUExecutionProvider"])

    if not source_path or not os.path.isfile(source_path):
        return {**result, "status": "error", "errors": [f"Source image not found: {source_path}"]}
    if not output_path:
        return {**result, "status": "error", "errors": ["No output mask path specified"]}
    if not prompts:
        return {**result, "status": "error", "errors": ["No prompts provided"]}

    # Read source image
    try:
        if source_format == "ppm":
            rgb_bytes, width, height, _ = _read_ppm_p6(source_path)
        elif source_format == "raw_rgb":
            if source_w <= 0 or source_h <= 0:
                return {**result, "status": "error", "errors": ["raw_rgb requires width/height"]}
            rgb_bytes, width, height, _ = _read_raw_rgb(source_path, source_w, source_h)
        else:
            return {**result, "status": "error", "errors": [f"Unsupported source format: {source_format}"]}
    except Exception as exc:
        return {**result, "status": "error", "errors": [f"Failed to read source: {exc}"]}

    # Import deps
    try:
        import numpy as np
    except ImportError:
        return {**result, "status": "error", "errors": ["numpy not available"]}

    try:
        import onnxruntime as ort
    except ImportError:
        return {**result, "status": "error", "errors": ["onnxruntime not available"]}

    available = ort.get_available_providers()
    require_cuda = request.get("require_cuda", False)
    if require_cuda:
        preferred = ["CUDAExecutionProvider", "CPUExecutionProvider"]
    used = [p for p in preferred if p in available]
    result["providers"] = {"available": available, "used": used}

    if not used or "CUDAExecutionProvider" not in used:
        return {**result, "status": "error", "errors": ["CUDAExecutionProvider not available; SAM3 preview cannot run in GPU mode"]}

    # --- require_cuda enforcement ---
    has_cuda = "CUDAExecutionProvider" in available
    if require_cuda and not has_cuda:
        return {
            **result,
            "status": "blocked",
            "errors": [
                "CUDAExecutionProvider not available and require_cuda is true."
                " Install onnxruntime-gpu with CUDA support or set require_cuda false."
            ],
        }

    # Resolve model paths
    repo_root = _resolve_repo_root()
    request_models = request.get("models", {})
    point_onnx = _resolve_model_path(
        request_models.get("point_onnx"), "FLUX_SAM3_POINT_ONNX",
        default_point_onnx_rel, repo_root
    )
    box_onnx = _resolve_model_path(
        request_models.get("box_onnx"), "FLUX_SAM3_BOX_ONNX",
        default_box_onnx_rel, repo_root
    )

    # Preprocess source image
    try:
        pixel_values = _preprocess_image(rgb_bytes, width, height)
    except Exception as exc:
        return {**result, "status": "error", "errors": [f"Preprocessing failed: {exc}"]}

    positive_masks: list[bytes] = []
    negative_masks: list[bytes] = []

    for i, prompt in enumerate(prompts):
        ptype = prompt.get("type", "")
        role = prompt.get("role", "positive")
        prompt_result: dict[str, Any] = {
            "index": i, "type": ptype, "role": role,
            "status": "ok", "warnings": [],
        }

        try:
            if ptype == "point":
                if not point_onnx:
                    prompt_result["status"] = "skipped"
                    prompt_result["warnings"].append("No point ONNX model available")
                    result["per_prompt"].append(prompt_result)
                    continue
                sess = _make_cuda_session(ort, point_onnx)
                px = float(prompt.get("source_xy", [0, 0])[0])
                py = float(prompt.get("source_xy", [0, 0])[1])
                model_px, model_py = _scale_point_to_model(px, py, width, height)
                prompt_result["source_xy"] = [px, py]
                prompt_result["model_xy"] = [model_px, model_py]
                label = 1 if role == "positive" else 0
                mask_out = _run_point_inference(sess, pixel_values, model_px, model_py, label)
                mask_bytes = _postprocess_mask(mask_out, width, height)
                if role == "positive":
                    positive_masks.append(mask_bytes)
                else:
                    negative_masks.append(mask_bytes)
                prompt_result["model_used"] = "point_onnx"

            elif ptype == "box":
                if not box_onnx:
                    prompt_result["status"] = "skipped"
                    prompt_result["warnings"].append("No box ONNX model available")
                    result["per_prompt"].append(prompt_result)
                    continue
                sess = _make_cuda_session(ort, box_onnx)
                coords = prompt.get("source_xyxy", [0, 0, 0, 0])
                x1, y1, x2, y2 = [float(c) for c in coords]
                mx1, my1, mx2, my2 = _scale_box_to_model(x1, y1, x2, y2, width, height)
                prompt_result["source_xyxy"] = [x1, y1, x2, y2]
                prompt_result["model_xyxy"] = [mx1, my1, mx2, my2]
                mask_out = _run_box_inference(sess, pixel_values, mx1, my1, mx2, my2)
                mask_bytes = _postprocess_mask(mask_out, width, height)
                # Boxes are always positive in Phase 3
                positive_masks.append(mask_bytes)
                prompt_result["model_used"] = "box_onnx"

            else:
                prompt_result["status"] = "skipped"
                prompt_result["warnings"].append(f"Unsupported prompt type: {ptype}")

        except Exception as exc:
            prompt_result["status"] = "error"
            prompt_result["error"] = str(exc)

        result["per_prompt"].append(prompt_result)

    if not positive_masks and not negative_masks:
        result["status"] = "error"
        result["errors"].append("No successful prompt inferences")
        return result

    # Composite all masks
    try:
        final_mask = _composite_masks(positive_masks, negative_masks, width, height)
    except Exception as exc:
        return {**result, "status": "error", "errors": [f"Mask compositing failed: {exc}"]}

    # Write output PGM
    try:
        out_dir = os.path.dirname(output_path)
        if out_dir:
            os.makedirs(out_dir, exist_ok=True)
        _write_pgm_p5(output_path, final_mask, width, height)
    except Exception as exc:
        return {**result, "status": "error", "errors": [f"Failed to write output mask: {exc}"]}

    elapsed_ms = (time.monotonic() - t0) * 1000
    result.update({
        "output": {
            "mask_path": output_path,
            "width": width,
            "height": height,
        },
        "prompt_count": len(prompts),
        "positive_count": len(positive_masks),
        "negative_count": len(negative_masks),
        "elapsed_ms": round(elapsed_ms, 1),
    })

    return result


class PersistentWorker:
    """Persistent SAM3 ONNX worker that caches sessions across requests."""

    def __init__(self, args: argparse.Namespace) -> None:
        self.args = args
        self._point_session: Any = None
        self._box_session: Any = None
        self._point_onnx_path: str | None = None
        self._box_onnx_path: str | None = None
        self._providers: list[str] = []
        self._repo_root = _resolve_repo_root()

    def _emit(self, event: dict[str, Any]) -> None:
        """Write a JSON-line event to stdout and flush."""
        sys.stdout.write(json.dumps(event) + "\n")
        sys.stdout.flush()

    def _ensure_providers(self) -> list[str]:
        """Resolve and cache ORT providers."""
        if self._providers:
            return self._providers
        import onnxruntime as ort
        available = ort.get_available_providers()
        if "CUDAExecutionProvider" not in available:
            raise RuntimeError("CUDAExecutionProvider not available; SAM3 preview cannot run in GPU mode")
        self._providers = ["CUDAExecutionProvider", "CPUExecutionProvider"]
        return self._providers

    def _ensure_session(self, model_type: str, request: dict) -> Any:
        """Get or create an ONNX session, emitting progress events."""
        import onnxruntime as ort

        if model_type == "point":
            if self._point_session is not None:
                return self._point_session
            request_models = request.get("models", {})
            path = _resolve_model_path(
                request_models.get("point_onnx"),
                "FLUX_SAM3_POINT_ONNX",
                default_point_onnx_rel,
                self._repo_root,
            )
            if not path:
                return None
            self._emit({"event": "loading_model", "model": "point", "path": path})
            self._ensure_providers()
            self._point_session = _make_cuda_session(ort, path)
            self._point_onnx_path = path
            self._emit({"event": "model_ready", "model": "point", "session_providers": self._point_session.get_providers()})
            return self._point_session
        elif model_type == "box":
            if self._box_session is not None:
                return self._box_session
            request_models = request.get("models", {})
            path = _resolve_model_path(
                request_models.get("box_onnx"),
                "FLUX_SAM3_BOX_ONNX",
                default_box_onnx_rel,
                self._repo_root,
            )
            if not path:
                return None
            self._emit({"event": "loading_model", "model": "box", "path": path})
            self._ensure_providers()
            self._box_session = _make_cuda_session(ort, path)
            self._box_onnx_path = path
            self._emit({"event": "model_ready", "model": "box", "session_providers": self._box_session.get_providers()})
            return self._box_session
        return None

    def _handle_infer(self, request: dict[str, Any]) -> None:
        """Handle an inference request using cached sessions."""
        request_id = request.get("request_id", 0)
        t0 = time.monotonic()

        source_path = request.get("source", {}).get("path", "")
        source_w = request.get("source", {}).get("width", 0)
        source_h = request.get("source", {}).get("height", 0)
        source_format = request.get("source", {}).get("format", "ppm")
        output_path = request.get("output", {}).get("mask_path", "")
        prompts = request.get("prompts", [])

        # Validate
        if not source_path or not os.path.isfile(source_path):
            self._emit({"event": "error", "request_id": request_id,
                        "errors": [f"Source image not found: {source_path}"]})
            return
        if not output_path:
            self._emit({"event": "error", "request_id": request_id,
                        "errors": ["No output mask path specified"]})
            return
        if not prompts:
            self._emit({"event": "error", "request_id": request_id,
                        "errors": ["No prompts provided"]})
            return

        # CUDA enforcement
        require_cuda = request.get("require_cuda", False)
        if require_cuda:
            import onnxruntime as ort
            has_cuda = "CUDAExecutionProvider" in ort.get_available_providers()
            if not has_cuda:
                self._emit({"event": "error", "request_id": request_id,
                            "errors": ["CUDAExecutionProvider not available and require_cuda is true."]})
                return

        # Read source image
        try:
            if source_format == "ppm":
                rgb_bytes, width, height, _ = _read_ppm_p6(source_path)
            elif source_format == "raw_rgb":
                if source_w <= 0 or source_h <= 0:
                    self._emit({"event": "error", "request_id": request_id,
                                "errors": ["raw_rgb requires width/height"]})
                    return
                rgb_bytes, width, height, _ = _read_raw_rgb(source_path, source_w, source_h)
            else:
                self._emit({"event": "error", "request_id": request_id,
                            "errors": [f"Unsupported source format: {source_format}"]})
                return
        except Exception as exc:
            self._emit({"event": "error", "request_id": request_id,
                        "errors": [f"Failed to read source: {exc}"]})
            return

        # Preprocess
        try:
            pixel_values = _preprocess_image(rgb_bytes, width, height)
        except Exception as exc:
            self._emit({"event": "error", "request_id": request_id,
                        "errors": [f"Preprocessing failed: {exc}"]})
            return

        positive_masks: list[bytes] = []
        negative_masks: list[bytes] = []
        total_prompts = len(prompts)

        for i, prompt in enumerate(prompts):
            ptype = prompt.get("type", "")
            role = prompt.get("role", "positive")

            self._emit({"event": "running", "request_id": request_id,
                        "prompt_index": i, "prompt_type": ptype,
                        "percent": int((i / max(total_prompts, 1)) * 80) + 10})

            try:
                if ptype == "point":
                    sess = self._ensure_session("point", request)
                    if sess is None:
                        continue
                    px = float(prompt.get("source_xy", [0, 0])[0])
                    py = float(prompt.get("source_xy", [0, 0])[1])
                    model_px, model_py = _scale_point_to_model(px, py, width, height)
                    label = 1 if role == "positive" else 0
                    mask_out = _run_point_inference(sess, pixel_values, model_px, model_py, label)
                    mask_bytes = _postprocess_mask(mask_out, width, height)
                    if role == "positive":
                        positive_masks.append(mask_bytes)
                    else:
                        negative_masks.append(mask_bytes)

                elif ptype == "box":
                    sess = self._ensure_session("box", request)
                    if sess is None:
                        continue
                    coords = prompt.get("source_xyxy", [0, 0, 0, 0])
                    x1, y1, x2, y2 = [float(c) for c in coords]
                    mx1, my1, mx2, my2 = _scale_box_to_model(x1, y1, x2, y2, width, height)
                    mask_out = _run_box_inference(sess, pixel_values, mx1, my1, mx2, my2)
                    mask_bytes = _postprocess_mask(mask_out, width, height)
                    positive_masks.append(mask_bytes)

            except Exception as exc:
                self._emit({"event": "error", "request_id": request_id,
                            "errors": [f"Prompt {i} inference failed: {exc}"]})
                return

        if not positive_masks and not negative_masks:
            self._emit({"event": "error", "request_id": request_id,
                        "errors": ["No successful prompt inferences"]})
            return

        # Composite
        try:
            final_mask = _composite_masks(positive_masks, negative_masks, width, height)
        except Exception as exc:
            self._emit({"event": "error", "request_id": request_id,
                        "errors": [f"Mask compositing failed: {exc}"]})
            return

        # Write output
        try:
            out_dir = os.path.dirname(output_path)
            if out_dir:
                os.makedirs(out_dir, exist_ok=True)
            _write_pgm_p5(output_path, final_mask, width, height)
        except Exception as exc:
            self._emit({"event": "error", "request_id": request_id,
                        "errors": [f"Failed to write output mask: {exc}"]})
            return

        elapsed_ms = (time.monotonic() - t0) * 1000
        self._emit({
            "event": "done",
            "request_id": request_id,
            "result": {
                "output": {
                    "mask_path": output_path,
                    "width": width,
                    "height": height,
                },
                "prompt_count": len(prompts),
                "positive_count": len(positive_masks),
                "negative_count": len(negative_masks),
                "elapsed_ms": round(elapsed_ms, 1),
            },
        })

    def run(self) -> None:
        """Main persistent loop: read JSON lines from stdin."""
        self._emit({"event": "worker_ready"})
        for line in sys.stdin:
            line = line.strip()
            if not line:
                continue
            try:
                msg = json.loads(line)
            except json.JSONDecodeError as exc:
                self._emit({"event": "error", "errors": [f"Invalid JSON: {exc}"]})
                continue

            cmd = msg.get("cmd", "")
            if cmd == "shutdown":
                self._emit({"event": "shutdown_ack"})
                break
            elif cmd == "infer":
                self._handle_infer(msg)
            else:
                self._emit({"event": "error", "errors": [f"Unknown command: {cmd}"]})


def main() -> None:
    parser = argparse.ArgumentParser(description="SAM3 ONNX Preview Worker")
    parser.add_argument(
        "--self-check", action="store_true",
        help="Validate deps/models/providers without inference"
    )
    parser.add_argument(
        "--require-cuda", action="store_true",
        help="Require CUDAExecutionProvider; block if unavailable"
    )
    parser.add_argument(
        "--persistent", action="store_true",
        help="Run in persistent mode (JSON-lines over stdin/stdout)"
    )
    parser.add_argument("--request", type=str, default=None, help="JSON request string")
    parser.add_argument("--request-file", type=str, default=None, help="Path to JSON request file")
    parser.add_argument("--point-onnx", type=str, default=None, help="Explicit path to point ONNX model")
    parser.add_argument("--box-onnx", type=str, default=None, help="Explicit path to box ONNX model")
    args = parser.parse_args()

    if args.self_check:
        result = do_self_check(args)
        json.dump(result, sys.stdout, indent=2)
        print()
        if result.get("status") in ("blocked", "error"):
            sys.exit(1)
    elif args.persistent:
        worker = PersistentWorker(args)
        worker.run()
    elif args.request:
        request = json.loads(args.request)
        result = do_inference(request)
        json.dump(result, sys.stdout, indent=2)
        print()
        if result.get("status") in ("blocked", "error"):
            sys.exit(1)
    elif args.request_file:
        with open(args.request_file) as f:
            request = json.load(f)
        result = do_inference(request)
        json.dump(result, sys.stdout, indent=2)
        print()
        if result.get("status") in ("blocked", "error"):
            sys.exit(1)
    else:
        # Read JSON from stdin (one-shot)
        raw = sys.stdin.read()
        if not raw.strip():
            print(json.dumps({"schema": SCHEMA, "status": "error", "errors": ["No input provided"]}))
            sys.exit(1)
        request = json.loads(raw)
        result = do_inference(request)
        json.dump(result, sys.stdout, indent=2)
        print()
        if result.get("status") in ("blocked", "error"):
            sys.exit(1)


if __name__ == "__main__":
    main()
