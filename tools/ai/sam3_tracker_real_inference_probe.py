#!/usr/bin/env python3
"""Standalone SAM3 image-tracker real-inference probe for Flux.

Honest proof only: exits non-zero if the local tracker API/model cannot produce
a non-empty point-prompt mask. This intentionally stays separate from the SAM3
image PCS proof and from video tracking proofs.
"""
from __future__ import annotations

import argparse, importlib, importlib.metadata, json, platform, sys, traceback
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

HERE = Path(__file__).resolve().parent
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))

MODEL_ID = "sam3_transformers"
RESULT_NAME = "sam3_tracker_real_inference_result.json"


def json_dump(payload: dict[str, Any]) -> None:
    print(json.dumps(payload, indent=2, sort_keys=True))


def import_version(name: str) -> dict[str, Any]:
    out = {"module": name, "available": False, "version": None, "error": None}
    try:
        importlib.import_module(name)
        out["available"] = True
        try:
            out["version"] = importlib.metadata.version(name)
        except importlib.metadata.PackageNotFoundError:
            out["version"] = "unknown"
    except Exception as exc:
        out["error"] = f"{type(exc).__name__}: {exc}"
    return out


def load_flux_model() -> tuple[dict[str, Any], Path, bool]:
    from flux_model_manager import default_manifest_path, find_model, flux_paths, is_installed, load_manifest, model_local_path
    manifest = load_manifest(default_manifest_path())
    model = find_model(manifest, MODEL_ID)
    local_path = model_local_path(model, flux_paths())
    return model, local_path, is_installed(model, flux_paths())


def cuda_info(torch_mod: Any | None = None) -> dict[str, Any]:
    info: dict[str, Any] = {"available": None, "device_count": None, "devices": [], "error": None}
    try:
        torch = torch_mod or importlib.import_module("torch")
        info["available"] = bool(torch.cuda.is_available())
        info["device_count"] = int(torch.cuda.device_count()) if info["available"] else 0
        if info["available"]:
            info["devices"] = [torch.cuda.get_device_name(i) for i in range(info["device_count"])]
    except Exception as exc:
        info["error"] = f"{type(exc).__name__}: {exc}"
    return info


def tracker_api_status() -> dict[str, Any]:
    out = {"available": False, "model_class": None, "processor_class": None, "error": None}
    try:
        from transformers import Sam3TrackerModel, Sam3TrackerProcessor
        out.update(available=True, model_class=Sam3TrackerModel.__name__, processor_class=Sam3TrackerProcessor.__name__)
    except Exception as exc:
        out["error"] = f"{type(exc).__name__}: {exc}"
    return out


def self_check_payload() -> dict[str, Any]:
    deps = {n: import_version(n) for n in ("torch", "transformers", "huggingface_hub", "PIL", "numpy", "cv2")}
    api = tracker_api_status()
    try:
        model, local_path, installed = load_flux_model(); model_error = None
    except Exception as exc:
        model, local_path, installed, model_error = {}, Path(""), False, f"{type(exc).__name__}: {exc}"
    blockers: list[str] = []
    for key in deps:
        if not deps[key]["available"]:
            blockers.append(f"missing Python module: {key} ({deps[key]['error']})")
    if not api["available"]:
        blockers.append(f"Transformers SAM3 tracker API unavailable: {api['error']}")
    if model_error:
        blockers.append(f"manifest/model lookup failed: {model_error}")
    elif not installed:
        blockers.append(f"SAM3 model is not installed at manifest-resolved path: {local_path}")
    elif not local_path.is_dir():
        blockers.append(f"SAM3 installed marker exists but model directory is missing: {local_path}")
    source = model.get("source", {}) if isinstance(model, dict) else {}
    return {
        "schema": "org.flux.ai.sam3-tracker-real-inference-self-check.v1",
        "ready": not blockers,
        "blockers": blockers,
        "python": {"executable": sys.executable, "version": platform.python_version()},
        "dependencies": deps,
        "cuda": cuda_info() if deps["torch"]["available"] else {"available": None, "blocker": "torch unavailable"},
        "model": {"id": model.get("id"), "repo": source.get("repo"), "revision": source.get("revision"), "local_path": str(local_path) if str(local_path) else None, "installed": installed, "lookup_error": model_error},
        "backend": {"name": "transformers", **api},
    }


def parse_point(value: str) -> dict[str, Any]:
    vals = [float(v.strip()) for v in value.split(",")]
    if len(vals) != 3:
        raise argparse.ArgumentTypeError("point must be x,y,label")
    return {"x": vals[0], "y": vals[1], "label": int(vals[2])}


def parse_box(value: str) -> list[float]:
    vals = [float(v.strip()) for v in value.split(",")]
    if len(vals) != 4:
        raise argparse.ArgumentTypeError("box must be x1,y1,x2,y2")
    return vals


def choose_device(requested: str, torch: Any) -> str:
    if requested == "auto":
        return "cuda" if torch.cuda.is_available() else "cpu"
    if requested == "cuda" and not torch.cuda.is_available():
        raise RuntimeError("--device cuda requested but CUDA is unavailable")
    return requested


def save_mask(mask: Any, path: Path, source_size: tuple[int, int], np: Any, Image: Any) -> dict[str, Any]:
    while isinstance(mask, (list, tuple)):
        if not mask:
            raise RuntimeError("backend returned an empty mask list")
        mask = mask[0]
    if hasattr(mask, "detach"):
        mask = mask.detach().cpu().numpy()
    arr = np.asarray(mask)
    while arr.ndim > 2:
        arr = arr[0]
    if arr.size == 0:
        raise RuntimeError("backend returned an empty mask array")
    if arr.dtype != np.bool_:
        arr = arr > (0.0 if float(arr.max(initial=0)) <= 1 else 127)
    img = Image.fromarray((arr.astype("uint8") * 255), mode="L")
    if img.size != source_size:
        img = img.resize(source_size, resample=Image.Resampling.NEAREST)
        arr = np.asarray(img) > 0
    path.parent.mkdir(parents=True, exist_ok=True)
    img.save(path)
    nonzero = int(np.count_nonzero(arr))
    if nonzero <= 0:
        raise RuntimeError("tracker produced an empty mask")
    if not path.is_file() or path.stat().st_size <= 0:
        raise RuntimeError(f"mask output was not written: {path}")
    return {"mask_path": str(path), "mask_size": list(img.size), "nonzero_pixels": nonzero}


def load_backend(model_path: Path, device: str) -> dict[str, Any]:
    from transformers import Sam3TrackerModel, Sam3TrackerProcessor
    processor = Sam3TrackerProcessor.from_pretrained(str(model_path), local_files_only=True)
    model = Sam3TrackerModel.from_pretrained(str(model_path), local_files_only=True)
    model.to(device); model.eval()
    return {"name": "transformers", "model": model, "processor": processor, "model_class": model.__class__.__name__, "processor_class": processor.__class__.__name__}


def run_tracker_point(backend: dict[str, Any], image: Any, point: dict[str, Any], box: list[float] | None, device: str, source_size: tuple[int, int], np: Any, Image: Any, out_path: Path) -> dict[str, Any]:
    import torch
    processor = backend["processor"]; model = backend["model"]
    kwargs: dict[str, Any] = {"images": image, "input_points": [[[[point["x"], point["y"]]]]], "input_labels": [[[point["label"]]]], "return_tensors": "pt"}
    if box is not None:
        kwargs["input_boxes"] = [[[box]]]
    inputs = processor(**kwargs)
    if hasattr(inputs, "to"):
        inputs = inputs.to(device)
    with torch.no_grad():
        output = model(**dict(inputs))
    masks = getattr(output, "pred_masks", None)
    if masks is None:
        masks = getattr(output, "masks", None)
    if masks is None:
        raise RuntimeError("tracker output did not include pred_masks/masks")
    processed = processor.post_process_masks(masks, original_sizes=[(source_size[1], source_size[0])], mask_threshold=0.0, binarize=True)
    return save_mask(processed, out_path, source_size, np, Image)


def run_real(args: argparse.Namespace) -> int:
    check = self_check_payload()
    if not check["ready"]:
        payload = {"status": "blocked", "self_check": check}
        if args.json: json_dump(payload)
        else: print("ERROR: SAM3 tracker proof blocked: " + "; ".join(check["blockers"]), file=sys.stderr)
        return 2
    import numpy as np
    from PIL import Image
    import torch
    model, local_path, _ = load_flux_model()
    device = choose_device(args.device, torch)
    image = Image.open(args.image).convert("RGB")
    args.output_dir.mkdir(parents=True, exist_ok=True)
    result: dict[str, Any] = {
        "schema": "org.flux.ai.sam3-tracker-real-inference-result.v1",
        "timestamp": datetime.now(timezone.utc).isoformat(),
        "python": check["python"], "dependencies": check["dependencies"],
        "device": {"requested": args.device, "selected": device, "cuda": cuda_info(torch)},
        "model": {"id": model["id"], "repo": model["source"].get("repo"), "revision": model["source"].get("revision"), "local_path": str(local_path)},
        "source_image": {"path": str(args.image), "width": image.size[0], "height": image.size[1]},
        "prompts": {"point": args.point, "box": args.box},
        "backend": {}, "proof": {"status": "blocked"}, "proofs": {"point": {"status": "blocked"}}, "blockers": [], "errors": [],
    }
    try:
        backend = load_backend(local_path, device)
        result["backend"] = {k: backend[k] for k in ("name", "model_class", "processor_class")}
        saved = run_tracker_point(backend, image, args.point, args.box, device, image.size, np, Image, args.output_dir / "mask_point.png")
        result["proof"] = {"status": "succeeded", **saved}
        result["proofs"]["point"] = result["proof"]
    except Exception as exc:
        result["errors"].append(f"{type(exc).__name__}: {exc}")
        result["blockers"].append("SAM3 tracker point proof failed without PCS/video fallback")
    (args.output_dir / RESULT_NAME).write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    if args.json: json_dump(result)
    if result["proof"].get("status") != "succeeded":
        result["proofs"]["point"] = result["proof"]
    return 0 if result["proof"].get("status") == "succeeded" else 4


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description="Flux standalone SAM3 image-tracker real inference proof probe")
    p.add_argument("--self-check", action="store_true")
    p.add_argument("--json", action="store_true")
    p.add_argument("--image", type=Path)
    p.add_argument("--output-dir", type=Path)
    p.add_argument("--point", type=parse_point, default=parse_point("200,220,1"))
    p.add_argument("--box", type=parse_box, help="optional box prompt x1,y1,x2,y2 if supported by tracker processor")
    p.add_argument("--device", choices=("auto", "cuda", "cpu"), default="auto")
    return p


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        if args.self_check:
            payload = self_check_payload()
            if args.json: json_dump(payload)
            else: print("ready" if payload["ready"] else "blocked: " + "; ".join(payload["blockers"]))
            return 0 if payload["ready"] else 2
        if not args.image or not args.output_dir:
            print("ERROR: --image and --output-dir are required unless --self-check is used", file=sys.stderr); return 2
        return run_real(args)
    except Exception as exc:
        payload = {"status": "blocked", "runtime_blocker": f"{type(exc).__name__}: {exc}", "traceback": traceback.format_exc().splitlines()[-5:]}
        if getattr(args, "json", False): json_dump(payload)
        else: print("ERROR: " + payload["runtime_blocker"], file=sys.stderr)
        return 1

if __name__ == "__main__":
    raise SystemExit(main())
