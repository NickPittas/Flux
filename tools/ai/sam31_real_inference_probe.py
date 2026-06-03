#!/usr/bin/env python3
"""Standalone SAM3.1 real-inference probe for Flux.

This script is intentionally honest: it never fabricates masks. If the local
SAM3.1 runtime/model/API cannot run text, box, and point prompts, it reports a
technical blocker and exits non-zero.
"""
from __future__ import annotations

import argparse
import importlib
import importlib.metadata
import inspect
import json
import platform
import sys
import traceback
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

HERE = Path(__file__).resolve().parent
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))

MODEL_ID = "sam31_sam3plus"
RESULT_NAME = "sam31_real_inference_result.json"


def json_dump(payload: dict[str, Any]) -> None:
    print(json.dumps(payload, indent=2, sort_keys=True))


def import_version(name: str) -> dict[str, Any]:
    out: dict[str, Any] = {"module": name, "available": False, "version": None, "error": None}
    try:
        importlib.import_module(name)
        out["available"] = True
        try:
            out["version"] = importlib.metadata.version(name)
        except importlib.metadata.PackageNotFoundError:
            out["version"] = "unknown"
    except Exception as exc:  # pragma: no cover - exact environment-dependent blocker
        out["error"] = f"{type(exc).__name__}: {exc}"
    return out


def load_flux_model() -> tuple[dict[str, Any], Path, bool]:
    from flux_model_manager import default_manifest_path, find_model, flux_paths, is_installed, load_manifest, model_local_path

    manifest = load_manifest(default_manifest_path())
    model = find_model(manifest, MODEL_ID)
    local_path = model_local_path(model, flux_paths())
    return model, local_path, is_installed(model, flux_paths())


def cuda_info(torch_mod: Any | None = None) -> dict[str, Any]:
    info = {"available": None, "device_count": None, "devices": [], "error": None}
    try:
        torch = torch_mod or importlib.import_module("torch")
        info["available"] = bool(torch.cuda.is_available())
        info["device_count"] = int(torch.cuda.device_count()) if info["available"] else 0
        if info["available"]:
            info["devices"] = [torch.cuda.get_device_name(i) for i in range(info["device_count"])]
    except Exception as exc:
        info["error"] = f"{type(exc).__name__}: {exc}"
    return info


def self_check_payload() -> dict[str, Any]:
    deps = {name: import_version(name) for name in ("torch", "transformers", "huggingface_hub", "PIL", "numpy", "cv2")}
    try:
        model, local_path, installed = load_flux_model()
        model_error = None
    except Exception as exc:
        model, local_path, installed = {}, Path(""), False
        model_error = f"{type(exc).__name__}: {exc}"
    blockers: list[str] = []
    for key in ("torch", "transformers", "huggingface_hub", "PIL", "numpy", "cv2"):
        if not deps[key]["available"]:
            blockers.append(f"missing Python module: {key} ({deps[key]['error']})")
    if model_error:
        blockers.append(f"manifest/model lookup failed: {model_error}")
    elif not installed:
        blockers.append(f"SAM3.1 model is not installed at manifest-resolved path: {local_path}")
    elif not local_path.is_dir():
        blockers.append(f"SAM3.1 installed marker exists but model directory is missing: {local_path}")
    source = model.get("source", {}) if isinstance(model, dict) else {}
    return {
        "schema": "org.flux.ai.sam31-real-inference-self-check.v1",
        "ready": not blockers,
        "blockers": blockers,
        "python": {"executable": sys.executable, "version": platform.python_version()},
        "dependencies": deps,
        "cuda": cuda_info() if deps["torch"]["available"] else {"available": None, "blocker": "torch unavailable"},
        "model": {
            "id": model.get("id"),
            "repo": source.get("repo"),
            "revision": source.get("revision"),
            "local_path": str(local_path) if str(local_path) else None,
            "installed": installed,
            "lookup_error": model_error,
        },
        "runtime_candidates": {
            "transformers_sam3": deps["transformers"]["available"],
            "meta_sam3_package": import_version("sam3")["available"],
        },
    }


def parse_box(value: str) -> list[float]:
    vals = [float(v.strip()) for v in value.split(",")]
    if len(vals) != 4:
        raise argparse.ArgumentTypeError("box must be x1,y1,x2,y2")
    return vals


def parse_point(value: str) -> dict[str, Any]:
    vals = [float(v.strip()) for v in value.split(",")]
    if len(vals) not in (2, 3):
        raise argparse.ArgumentTypeError("point must be x,y or x,y,label")
    return {"x": vals[0], "y": vals[1], "label": int(vals[2]) if len(vals) == 3 else 1}


def choose_device(requested: str, torch: Any) -> str:
    if requested == "auto":
        return "cuda" if torch.cuda.is_available() else "cpu"
    if requested == "cuda" and not torch.cuda.is_available():
        raise RuntimeError("--device cuda requested but CUDA is unavailable")
    return requested


def to_mask_array(value: Any, np: Any) -> Any:
    if value is None:
        return None
    if hasattr(value, "detach"):
        value = value.detach().cpu().numpy()
    arr = np.asarray(value)
    while arr.ndim > 2:
        arr = arr[0]
    return arr


def save_mask(mask: Any, path: Path, source_size: tuple[int, int], np: Any, Image: Any) -> dict[str, Any]:
    arr = to_mask_array(mask, np)
    if arr is None:
        raise RuntimeError("backend returned no mask array")
    if arr.dtype != np.bool_:
        arr = arr > (0.0 if arr.max(initial=0) <= 1 else 127)
    img = Image.fromarray((arr.astype("uint8") * 255), mode="L")
    if img.size != source_size:
        img = img.resize(source_size, resample=Image.Resampling.NEAREST)
        arr = np.asarray(img) > 0
    path.parent.mkdir(parents=True, exist_ok=True)
    img.save(path)
    nonzero = int(np.count_nonzero(arr))
    if not path.is_file() or path.stat().st_size <= 0:
        raise RuntimeError(f"mask output was not written: {path}")
    return {"mask_path": str(path), "mask_size": list(img.size), "nonzero_pixels": nonzero}


def call_with_supported_kwargs(callable_obj: Any, kwargs: dict[str, Any]) -> Any:
    try:
        sig = inspect.signature(callable_obj)
        if any(p.kind == p.VAR_KEYWORD for p in sig.parameters.values()):
            return callable_obj(**kwargs)
        supported = {k: v for k, v in kwargs.items() if k in sig.parameters}
        return callable_obj(**supported)
    except (TypeError, ValueError):
        return callable_obj(**kwargs)


def extract_mask(output: Any) -> Any:
    if isinstance(output, dict):
        for key in ("masks", "mask", "pred_masks", "segmentation"):
            if key in output:
                return output[key]
    for key in ("masks", "mask", "pred_masks", "segmentation"):
        if hasattr(output, key):
            return getattr(output, key)
    return output


def transformers_backend(model_path: Path, device: str) -> dict[str, Any]:
    import torch
    from transformers import AutoModel, AutoProcessor, pipeline

    backend: dict[str, Any] = {"name": "transformers", "capabilities": {}}
    errors: list[str] = []
    try:
        processor = AutoProcessor.from_pretrained(str(model_path), local_files_only=True, trust_remote_code=True)
        model = AutoModel.from_pretrained(str(model_path), local_files_only=True, trust_remote_code=True)
        model.to(device)
        model.eval()
        backend.update({"kind": "auto_model", "processor": processor, "model": model})
        proc_sig = " ".join(dir(processor)).lower()
        backend["capabilities"] = {
            "text_prompt": "text" in proc_sig,
            "box_prompt": "box" in proc_sig or "input_boxes" in proc_sig,
            "point_prompt": "point" in proc_sig or "input_points" in proc_sig,
            "mask_paint_prompt": "mask" in proc_sig,
            "video_propagation_or_later_frame_correction": "video" in proc_sig or "propagat" in proc_sig,
        }
        return backend
    except Exception as exc:
        errors.append(f"AutoProcessor/AutoModel failed: {type(exc).__name__}: {exc}")
    try:
        pipe = pipeline("mask-generation", model=str(model_path), device=0 if device == "cuda" else -1, trust_remote_code=True, local_files_only=True)
        backend.update({"kind": "pipeline_mask_generation", "pipeline": pipe})
        backend["capabilities"] = {"text_prompt": False, "box_prompt": False, "point_prompt": False, "mask_paint_prompt": False, "video_propagation_or_later_frame_correction": False}
        return backend
    except Exception as exc:
        errors.append(f"transformers pipeline failed: {type(exc).__name__}: {exc}")
    raise RuntimeError("No usable local Transformers SAM3.1 API found. " + " | ".join(errors))


def run_prompt(backend: dict[str, Any], prompt_kind: str, image: Any, prompt: Any, out_path: Path, source_size: tuple[int, int], np: Any, Image: Any) -> dict[str, Any]:
    result: dict[str, Any] = {"status": "blocked", "prompt": prompt, "runtime_blocker": None, "unsupported_by_detected_api": False}
    try:
        if backend.get("kind") != "auto_model":
            result.update(status="unsupported", unsupported_by_detected_api=True, runtime_blocker="detected backend does not expose prompted SAM calls")
            return result
        if not backend.get("capabilities", {}).get(f"{prompt_kind}_prompt", False):
            result.update(status="unsupported", unsupported_by_detected_api=True, runtime_blocker=f"detected processor API does not advertise {prompt_kind} prompts")
            return result
        processor = backend["processor"]
        model = backend["model"]
        kwargs: dict[str, Any] = {"images": image, "return_tensors": "pt"}
        if prompt_kind == "text":
            kwargs.update({"text": prompt, "text_prompt": prompt})
        elif prompt_kind == "box":
            kwargs.update({"input_boxes": [[prompt]], "boxes": [prompt]})
        elif prompt_kind == "point":
            kwargs.update({"input_points": [[[prompt["x"], prompt["y"]]]], "input_labels": [[prompt["label"]]], "points": [[[prompt["x"], prompt["y"]]]]})
        inputs = call_with_supported_kwargs(processor, kwargs)
        if hasattr(inputs, "to"):
            inputs = inputs.to(next(model.parameters()).device)
        with importlib.import_module("torch").no_grad():
            output = model(**inputs) if isinstance(inputs, dict) else model(inputs)
        mask = extract_mask(output)
        saved = save_mask(mask, out_path, source_size, np, Image)
        result.update(status="succeeded", **saved)
    except Exception as exc:
        result.update(status="blocked", runtime_blocker=f"{type(exc).__name__}: {exc}")
    return result


def run_real(args: argparse.Namespace) -> int:
    check = self_check_payload()
    if not check["ready"]:
        if args.json:
            json_dump({"status": "blocked", "self_check": check})
        else:
            print("ERROR: SAM3.1 real inference blocked: " + "; ".join(check["blockers"]), file=sys.stderr)
        return 2
    import numpy as np
    from PIL import Image
    import torch

    model, local_path, _installed = load_flux_model()
    device = choose_device(args.device, torch)
    image_path = args.image
    image = Image.open(image_path).convert("RGB")
    output_dir = args.output_dir
    output_dir.mkdir(parents=True, exist_ok=True)
    result: dict[str, Any] = {
        "schema": "org.flux.ai.sam31-real-inference-result.v1",
        "timestamp": datetime.now(timezone.utc).isoformat(),
        "python": check["python"],
        "dependencies": check["dependencies"],
        "device": {"requested": args.device, "selected": device, "cuda": cuda_info(torch)},
        "model": {"id": model["id"], "repo": model["source"].get("repo"), "revision": model["source"].get("revision"), "local_path": str(local_path)},
        "source_image": {"path": str(image_path), "width": image.size[0], "height": image.size[1]},
        "prompts": {"text": args.text, "box": args.box, "point": args.point, "mask_prompt_path": str(args.mask_prompt) if args.mask_prompt else None, "video_path": str(args.video) if args.video else None},
        "backend": {},
        "proofs": {},
        "unsupported": {},
        "errors": [],
    }
    try:
        backend = transformers_backend(local_path, device)
    except Exception as exc:
        try:
            importlib.import_module("sam3")
            result["backend"] = {"name": "meta_sam3", "status": "importable_but_not_implemented", "runtime_blocker": "Meta sam3 package importable, but no stable local API path was detected by this probe."}
        except Exception:
            result["backend"] = {"name": "none", "runtime_blocker": f"{type(exc).__name__}: {exc}"}
        result["errors"].append(result["backend"]["runtime_blocker"])
        (output_dir / RESULT_NAME).write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        if args.json:
            json_dump(result)
        return 3
    result["backend"] = {"name": backend.get("name"), "kind": backend.get("kind"), "capabilities": backend.get("capabilities", {})}
    result["proofs"]["text"] = run_prompt(backend, "text", image, args.text, output_dir / "mask_text.png", image.size, np, Image)
    result["proofs"]["box"] = run_prompt(backend, "box", image, args.box, output_dir / "mask_box.png", image.size, np, Image)
    result["proofs"]["point"] = run_prompt(backend, "point", image, args.point, output_dir / "mask_point.png", image.size, np, Image)
    result["unsupported"]["mask_paint_prompt"] = "not_proven" if args.mask_prompt else "not_requested"
    result["unsupported"]["video_propagation_or_later_frame_correction"] = "not_proven" if args.video else "not_requested"
    (output_dir / RESULT_NAME).write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    if args.json:
        json_dump(result)
    required_ok = all(result["proofs"][k]["status"] == "succeeded" for k in ("text", "box", "point"))
    return 0 if required_ok else 4


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description="Flux standalone SAM3.1 real inference proof probe")
    p.add_argument("--self-check", action="store_true", help="report runtime/model availability without running inference")
    p.add_argument("--json", action="store_true", help="emit JSON to stdout")
    p.add_argument("--image", type=Path, help="input still image")
    p.add_argument("--output-dir", type=Path, help="directory for result JSON and real mask images")
    p.add_argument("--text", default="red rectangle", help="text prompt")
    p.add_argument("--box", type=parse_box, default=parse_box("100,90,300,350"), help="box prompt x1,y1,x2,y2")
    p.add_argument("--point", type=parse_point, default=parse_point("200,220,1"), help="point prompt x,y,label")
    p.add_argument("--mask-prompt", type=Path, help="optional paint/mask prompt input")
    p.add_argument("--video", type=Path, help="optional video/frame-folder input for propagation investigation")
    p.add_argument("--device", choices=("auto", "cuda", "cpu"), default="auto")
    return p


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        if args.self_check:
            payload = self_check_payload()
            if args.json:
                json_dump(payload)
            else:
                print("ready" if payload["ready"] else "blocked: " + "; ".join(payload["blockers"]))
            return 0 if payload["ready"] else 2
        if not args.image or not args.output_dir:
            print("ERROR: --image and --output-dir are required unless --self-check is used", file=sys.stderr)
            return 2
        return run_real(args)
    except Exception as exc:
        payload = {"status": "blocked", "runtime_blocker": f"{type(exc).__name__}: {exc}", "traceback": traceback.format_exc().splitlines()[-5:]}
        if getattr(args, "json", False):
            json_dump(payload)
        else:
            print("ERROR: " + payload["runtime_blocker"], file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
