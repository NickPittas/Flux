#!/usr/bin/env python3
"""Standalone SAM3 tracker-video real-inference probe for Flux.

Honest proof only: uses the official Hugging Face tracker-video API and exits
non-zero unless local facebook/sam3 can produce non-empty propagated masks.
"""
from __future__ import annotations

import argparse, importlib, importlib.metadata, inspect, json, platform, sys, traceback
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

HERE = Path(__file__).resolve().parent
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))

MODEL_ID = "sam3_transformers"
RESULT_NAME = "sam3_tracker_video_real_inference_result.json"
SELF_SCHEMA = "org.flux.ai.sam3-tracker-video-real-inference-self-check.v1"
RESULT_SCHEMA = "org.flux.ai.sam3-tracker-video-real-inference-result.v1"
DEFAULT_VIDEO = Path("/home/npittas/Videos/For_Test/Video_For_Test.mov")


def json_dump(payload: dict[str, Any]) -> None:
    print(json.dumps(payload, indent=2, sort_keys=True))


def import_version(name: str) -> dict[str, Any]:
    out = {"module": name, "available": False, "version": None, "error": None}
    try:
        importlib.import_module(name); out["available"] = True
        try: out["version"] = importlib.metadata.version(name)
        except importlib.metadata.PackageNotFoundError: out["version"] = "unknown"
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


def tracker_video_api_status() -> dict[str, Any]:
    out = {"available": False, "model_class": None, "processor_class": None, "required_methods": {}, "error": None}
    try:
        from transformers import Sam3TrackerVideoModel, Sam3TrackerVideoProcessor
        methods = {name: hasattr(Sam3TrackerVideoProcessor, name) for name in ("init_video_session", "add_inputs_to_inference_session", "post_process_masks")}
        methods["propagate_in_video_iterator"] = hasattr(Sam3TrackerVideoModel, "propagate_in_video_iterator")
        out.update(available=all(methods.values()), model_class=Sam3TrackerVideoModel.__name__, processor_class=Sam3TrackerVideoProcessor.__name__, required_methods=methods)
        if not out["available"]: out["error"] = "official tracker-video class/method set is incomplete"
    except Exception as exc:
        out["error"] = f"{type(exc).__name__}: {exc}"
    return out


def self_check_payload() -> dict[str, Any]:
    deps = {n: import_version(n) for n in ("torch", "transformers", "huggingface_hub", "PIL", "numpy", "cv2")}
    api = tracker_video_api_status()
    try: model, local_path, installed, model_error = (*load_flux_model(), None)
    except Exception as exc: model, local_path, installed, model_error = {}, Path(""), False, f"{type(exc).__name__}: {exc}"
    blockers: list[str] = []
    for key, dep in deps.items():
        if not dep["available"]: blockers.append(f"missing Python module: {key} ({dep['error']})")
    if not api["available"]: blockers.append(f"Transformers SAM3 tracker-video API unavailable: {api['error']}")
    if model_error: blockers.append(f"manifest/model lookup failed: {model_error}")
    elif not installed: blockers.append(f"SAM3 model is not installed at manifest-resolved path: {local_path}")
    elif not local_path.is_dir(): blockers.append(f"SAM3 installed marker exists but model directory is missing: {local_path}")
    source = model.get("source", {}) if isinstance(model, dict) else {}
    return {"schema": SELF_SCHEMA, "ready": not blockers, "blockers": blockers,
        "python": {"executable": sys.executable, "version": platform.python_version()}, "dependencies": deps,
        "cuda": cuda_info() if deps["torch"]["available"] else {"available": None, "blocker": "torch unavailable"},
        "model": {"id": model.get("id"), "repo": source.get("repo"), "revision": source.get("revision"), "local_path": str(local_path) if str(local_path) else None, "installed": installed, "lookup_error": model_error},
        "backend": {"name": "transformers", **api}}


def parse_point(value: str) -> dict[str, Any]:
    vals = [float(v.strip()) for v in value.split(",")]
    if len(vals) != 3: raise argparse.ArgumentTypeError("point must be x,y,label")
    return {"x": vals[0], "y": vals[1], "label": int(vals[2])}


def parse_box(value: str) -> list[float]:
    vals = [float(v.strip()) for v in value.split(",")]
    if len(vals) != 4: raise argparse.ArgumentTypeError("box must be x1,y1,x2,y2")
    return vals


def choose_device(requested: str, torch: Any) -> str:
    if requested == "auto": return "cuda" if torch.cuda.is_available() else "cpu"
    if requested == "cuda" and not torch.cuda.is_available(): raise RuntimeError("--device cuda requested but CUDA is unavailable")
    return requested


def call_supported(fn: Any, kwargs: dict[str, Any]) -> Any:
    try: params = set(inspect.signature(fn).parameters)
    except Exception: return fn(**kwargs)
    return fn(**{k: v for k, v in kwargs.items() if k in params})


def decode_frames(args: argparse.Namespace, Image: Any, cv2: Any) -> tuple[list[Any], dict[str, Any]]:
    if args.frames_dir:
        paths = sorted([p for p in args.frames_dir.iterdir() if p.suffix.lower() in (".png", ".jpg", ".jpeg", ".tif", ".tiff", ".webp")])
        if not paths: raise RuntimeError(f"no image frames found in --frames-dir: {args.frames_dir}")
        selected = paths[args.start_frame:args.start_frame + args.max_frames]
        frames = [Image.open(p).convert("RGB") for p in selected]
        return frames, {"type": "frames_dir", "path": str(args.frames_dir), "selected_files": [str(p) for p in selected]}
    if not args.video: raise RuntimeError("one of --video or --frames-dir is required unless --self-check")
    if not args.video.is_file(): raise RuntimeError(f"video is missing: {args.video}")
    cap = cv2.VideoCapture(str(args.video))
    if not cap.isOpened(): raise RuntimeError(f"video is unreadable: {args.video}")
    cap.set(cv2.CAP_PROP_POS_FRAMES, int(args.start_frame)); frames = []
    while len(frames) < args.max_frames:
        ok, bgr = cap.read()
        if not ok: break
        rgb = cv2.cvtColor(bgr, cv2.COLOR_BGR2RGB)
        frames.append(Image.fromarray(rgb))
    cap.release()
    if not frames: raise RuntimeError("video decoding produced zero frames")
    return frames, {"type": "video", "path": str(args.video)}


def load_backend(model_path: Path, device: str) -> dict[str, Any]:
    from transformers import Sam3TrackerVideoModel, Sam3TrackerVideoProcessor
    processor = Sam3TrackerVideoProcessor.from_pretrained(str(model_path), local_files_only=True, trust_remote_code=True)
    model = Sam3TrackerVideoModel.from_pretrained(str(model_path), local_files_only=True, trust_remote_code=True)
    model.to(device); model.eval()
    return {"name": "transformers", "model": model, "processor": processor, "model_class": model.__class__.__name__, "processor_class": processor.__class__.__name__}


def mask_to_png(mask: Any, path: Path, source_size: tuple[int, int], np: Any, Image: Any) -> dict[str, Any]:
    while isinstance(mask, (list, tuple)):
        if not mask: raise RuntimeError("backend returned empty mask list")
        mask = mask[0]
    if hasattr(mask, "detach"): mask = mask.detach().cpu().numpy()
    arr = np.asarray(mask)
    while arr.ndim > 2: arr = arr[0]
    if arr.size == 0: raise RuntimeError("backend returned empty mask array")
    if arr.dtype != np.bool_: arr = arr > (127 if arr.dtype == np.uint8 else 0.0)
    img = Image.fromarray((arr.astype("uint8") * 255), mode="L")
    if img.size != source_size:
        img = img.resize(source_size, resample=Image.Resampling.NEAREST); arr = np.asarray(img) > 0
    path.parent.mkdir(parents=True, exist_ok=True); img.save(path)
    nonzero = int(np.count_nonzero(arr))
    if nonzero <= 0: raise RuntimeError("tracker-video produced an empty mask")
    return {"mask_path": str(path), "mask_size": list(img.size), "nonzero_pixels": nonzero}


def extract_masks(output: Any) -> Any:
    if isinstance(output, dict):
        for k in ("pred_masks", "masks", "mask_logits"):
            if k in output: return output[k]
    for k in ("pred_masks", "masks", "mask_logits"):
        if hasattr(output, k): return getattr(output, k)
    return output


def normalize_masks_for_postprocess(masks: Any) -> Any:
    if hasattr(masks, "dim"):
        if masks.dim() == 2:
            return masks.unsqueeze(0).unsqueeze(0)
        if masks.dim() == 3:
            return masks.unsqueeze(1)
    return masks


def run_real(args: argparse.Namespace) -> int:
    check = self_check_payload()
    if not check["ready"]:
        payload = {"status": "blocked", "self_check": check}
        json_dump(payload) if args.json else print("ERROR: " + "; ".join(check["blockers"]), file=sys.stderr)
        return 2
    import cv2, numpy as np, torch
    from PIL import Image
    model_meta, local_path, _ = load_flux_model(); device = choose_device(args.device, torch)
    args.output_dir.mkdir(parents=True, exist_ok=True)
    frames, input_source = decode_frames(args, Image, cv2)
    width, height = frames[0].size
    result: dict[str, Any] = {"schema": RESULT_SCHEMA, "timestamp": datetime.now(timezone.utc).isoformat(), "python": check["python"], "dependencies": check["dependencies"],
        "device": {"requested": args.device, "selected": device, "cuda": cuda_info(torch)}, "model": {"id": model_meta["id"], "repo": model_meta["source"].get("repo"), "revision": model_meta["source"].get("revision"), "local_path": str(local_path)},
        "backend": {}, "input_source": input_source, "decoded_frame_count": len(frames), "frame_dimensions": {"width": width, "height": height}, "start_frame": args.start_frame, "max_frames": args.max_frames,
        "prompts": {"point": args.point, "box": args.box}, "session": {"status": "blocked"}, "proof": {"status": "blocked", "frames": [], "successful_frame_count": 0, "total_nonzero_pixels": 0}, "blockers": [], "errors": []}
    try:
        backend = load_backend(local_path, device); result["backend"] = {k: backend[k] for k in ("name", "model_class", "processor_class")}
        processor, model = backend["processor"], backend["model"]
        session = call_supported(processor.init_video_session, {"video": frames, "inference_device": device, "processing_device": device})
        prompt_kwargs = {"inference_session": session, "frame_idx": 0, "obj_ids": [1], "input_points": [[[[args.point["x"], args.point["y"]]]]], "input_labels": [[[args.point["label"]]]], "input_boxes": [[[args.box]]] if args.box else None, "original_size": (height, width)}
        prompt_kwargs = {k: v for k, v in prompt_kwargs.items() if v is not None}
        prompt_out = call_supported(processor.add_inputs_to_inference_session, prompt_kwargs)
        model_kwargs = {"inference_session": session, "frame_idx": 0}
        call_supported(model.forward, model_kwargs)
        result["session"] = {"status": "initialized_and_prompted", "prompt_output_type": type(prompt_out).__name__}
        iterator = call_supported(model.propagate_in_video_iterator, {"inference_session": session, "start_frame_idx": 0, "max_frame_num_to_track": len(frames), "show_progress_bar": False})
        for item in iterator:
            frame_idx = int(getattr(item, "frame_idx", item.get("frame_idx", len(result["proof"]["frames"])) if isinstance(item, dict) else len(result["proof"]["frames"])))
            masks = normalize_masks_for_postprocess(extract_masks(item))
            post_process_error = None
            post_arg = masks if isinstance(masks, list) else [masks]
            try:
                processed_masks = processor.post_process_masks(post_arg, original_sizes=[(height, width)], mask_threshold=0.0, binarize=True)
                if processed_masks is not None:
                    masks = processed_masks
            except TypeError:
                processed_masks = processor.post_process_masks(post_arg, original_sizes=[(height, width)])
                if processed_masks is not None:
                    masks = processed_masks
            except Exception as exc:
                post_process_error = f"{type(exc).__name__}: {exc}"
            saved = mask_to_png(masks, args.output_dir / f"mask_{frame_idx:06d}.png", (width, height), np, Image)
            if post_process_error:
                saved["post_process_masks_error"] = post_process_error
            saved["frame_idx"] = frame_idx; result["proof"]["frames"].append(saved)
            if len(result["proof"]["frames"]) >= len(frames): break
        total = sum(f["nonzero_pixels"] for f in result["proof"]["frames"])
        result["proof"].update(status="succeeded" if total > 0 else "blocked", successful_frame_count=len(result["proof"]["frames"]), total_nonzero_pixels=total)
        if total <= 0: result["blockers"].append("all propagated masks were empty")
    except Exception as exc:
        result["errors"].append(f"{type(exc).__name__}: {exc}"); result["blockers"].append("SAM3 tracker-video proof failed without API fallback or fake masks")
    (args.output_dir / RESULT_NAME).write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    if args.json: json_dump(result)
    return 0 if result["proof"].get("status") == "succeeded" else 4


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description="Flux standalone SAM3 tracker-video real inference proof probe")
    p.add_argument("--self-check", action="store_true"); p.add_argument("--json", action="store_true")
    p.add_argument("--video", type=Path, default=None); p.add_argument("--frames-dir", type=Path)
    p.add_argument("--output-dir", type=Path); p.add_argument("--point", type=parse_point, default=parse_point("200,220,1"))
    p.add_argument("--box", type=parse_box); p.add_argument("--start-frame", type=int, default=0); p.add_argument("--max-frames", type=int, default=8)
    p.add_argument("--device", choices=("auto", "cuda", "cpu"), default="auto")
    return p


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        if args.self_check:
            payload = self_check_payload(); json_dump(payload) if args.json else print("ready" if payload["ready"] else "blocked: " + "; ".join(payload["blockers"]))
            return 0 if payload["ready"] else 2
        if not args.output_dir or (not args.video and not args.frames_dir):
            print("ERROR: --output-dir and one of --video/--frames-dir are required unless --self-check is used", file=sys.stderr); return 2
        if args.max_frames <= 0 or args.start_frame < 0:
            print("ERROR: --max-frames must be > 0 and --start-frame must be >= 0", file=sys.stderr); return 2
        return run_real(args)
    except Exception as exc:
        payload = {"status": "blocked", "runtime_blocker": f"{type(exc).__name__}: {exc}", "traceback": traceback.format_exc().splitlines()[-5:]}
        json_dump(payload) if getattr(args, "json", False) else print("ERROR: " + payload["runtime_blocker"], file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
