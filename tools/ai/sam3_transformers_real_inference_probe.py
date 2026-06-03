#!/usr/bin/env python3
"""Standalone SAM3 Transformers real-inference probe for Flux.

This script is intentionally honest: it never fabricates masks. It runs exactly
one caller-selected text, box, or point prompt and reports a technical blocker if
the local SAM3 Transformers runtime/model/API cannot satisfy that prompt.
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

MODEL_ID = "sam3_transformers"
RESULT_NAME = "sam3_transformers_real_inference_result.json"


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
        blockers.append(f"SAM3 Transformers model is not installed at manifest-resolved path: {local_path}")
    elif not local_path.is_dir():
        blockers.append(f"SAM3 Transformers installed marker exists but model directory is missing: {local_path}")
    source = model.get("source", {}) if isinstance(model, dict) else {}
    return {
        "schema": "org.flux.ai.sam3-transformers-real-inference-self-check.v1",
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
            "sam3_transformers": deps["transformers"]["available"],
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
    return {"x": int(vals[0]), "y": int(vals[1]), "label": int(vals[2]) if len(vals) == 3 else 1}


def parse_xy_float(value: str) -> list[float]:
    vals = [float(v.strip()) for v in value.split(",")]
    if len(vals) != 2:
        raise argparse.ArgumentTypeError("coordinate must be x,y")
    return vals


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
    sig = inspect.signature(callable_obj)
    supported = {k: v for k, v in kwargs.items() if k in sig.parameters}
    return callable_obj(**supported)


def extract_mask(output: Any) -> Any:
    if isinstance(output, dict):
        for key in ("masks", "mask", "pred_masks", "segmentation"):
            if key in output:
                return output[key]
    for key in ("masks", "mask", "pred_masks", "segmentation"):
        if hasattr(output, key):
            return getattr(output, key)
    return output


def postprocess_mask(processor: Any, output: Any, source_size: tuple[int, int]) -> Any:
    if hasattr(processor, "post_process_instance_segmentation"):
        try:
            processed = processor.post_process_instance_segmentation(output, threshold=0.5, target_sizes=[(source_size[1], source_size[0])])
            if processed and "masks" in processed[0]:
                return processed[0]["masks"]
        except Exception:
            pass
    return extract_mask(output)


def inspect_callable_signature(callable_obj: Any) -> dict[str, Any]:
    try:
        sig = inspect.signature(callable_obj)
        return {"signature": str(sig), "parameters": list(sig.parameters)}
    except Exception as exc:
        return {"signature": None, "parameters": [], "error": f"{type(exc).__name__}: {exc}"}


def decode_temporal_frames(video_path: Path, max_frames: int = 3, start_frame: int = 0) -> tuple[list[Any], dict[str, Any]]:
    if not video_path.is_file():
        raise RuntimeError(f"video is missing: {video_path}")
    cv2 = importlib.import_module("cv2")
    Image = importlib.import_module("PIL.Image")
    cap = cv2.VideoCapture(str(video_path))
    if not cap.isOpened():
        raise RuntimeError(f"video is unreadable: {video_path}")
    cap.set(cv2.CAP_PROP_POS_FRAMES, int(start_frame))
    frames = []
    while len(frames) < max_frames:
        ok, bgr = cap.read()
        if not ok:
            break
        rgb = cv2.cvtColor(bgr, cv2.COLOR_BGR2RGB)
        frames.append(Image.fromarray(rgb))
    cap.release()
    if not frames:
        raise RuntimeError("video decoding produced zero frames")
    return frames, {"type": "video", "path": str(video_path), "start_frame": start_frame, "max_frames": max_frames}


def extract_temporal_masks(output: Any) -> Any:
    if isinstance(output, dict):
        for key in ("pred_masks", "masks", "mask_logits"):
            if key in output:
                return output[key]
    for key in ("pred_masks", "masks", "mask_logits"):
        if hasattr(output, key):
            return getattr(output, key)
    return output


def normalize_temporal_masks_for_postprocess(masks: Any) -> Any:
    if hasattr(masks, "dim"):
        if masks.dim() == 2:
            return masks.unsqueeze(0).unsqueeze(0)
        if masks.dim() == 3:
            return masks.unsqueeze(1)
    return masks


def temporal_mask_to_png(mask: Any, path: Path, source_size: tuple[int, int], np: Any, Image: Any) -> dict[str, Any]:
    while isinstance(mask, (list, tuple)):
        if not mask:
            raise RuntimeError("backend returned empty mask list")
        mask = mask[0]
    arr = to_mask_array(mask, np)
    if arr is None or arr.size == 0:
        raise RuntimeError("backend returned empty mask array")
    if arr.dtype != np.bool_:
        arr = arr > (127 if arr.dtype == np.uint8 else 0.0)
    img = Image.fromarray((arr.astype("uint8") * 255), mode="L")
    if img.size != source_size:
        img = img.resize(source_size, resample=Image.Resampling.NEAREST)
        arr = np.asarray(img) > 0
    path.parent.mkdir(parents=True, exist_ok=True)
    img.save(path)
    nonzero = int(np.count_nonzero(arr))
    if nonzero <= 0:
        raise RuntimeError("tracker-video produced an empty mask")
    return {"mask_path": str(path), "mask_size": list(img.size), "nonzero_pixels": nonzero}


def temporal_api_proof(model_path: Path, device: str = "auto", output_dir: Path | None = None, video_path: Path | None = None) -> dict[str, Any]:
    result: dict[str, Any] = {
        "schema": "org.flux.ai.sam3-transformers-temporal-api-proof.v1",
        "timestamp": datetime.now(timezone.utc).isoformat(),
        "status": "blocked",
        "backend": {"name": "transformers", "kind": "sam3_tracker_video"},
        "capabilities": {"video_tracker_api": False, "video_propagation_or_later_frame_correction": False},
        "classes": {"processor": "Sam3TrackerVideoProcessor", "model": "Sam3TrackerVideoModel"},
        "signatures": {},
        "blockers": [],
        "proof": {"invocation_attempted": False, "video_path": str(video_path) if video_path else None, "fake_masks_generated": False},
    }
    if output_dir is not None:
        output_dir.mkdir(parents=True, exist_ok=True)
    try:
        transformers = importlib.import_module("transformers")
    except Exception as exc:
        result["blockers"].append(f"transformers import failed: {type(exc).__name__}: {exc}")
        return result
    missing = [name for name in ("Sam3TrackerVideoProcessor", "Sam3TrackerVideoModel") if not hasattr(transformers, name)]
    if missing:
        result["blockers"].append("official Transformers temporal SAM3 classes unavailable: " + ", ".join(missing))
        return result
    processor_cls = getattr(transformers, "Sam3TrackerVideoProcessor")
    model_cls = getattr(transformers, "Sam3TrackerVideoModel")
    result["signatures"]["processor_init"] = inspect_callable_signature(processor_cls.__init__)
    result["signatures"]["processor_call"] = inspect_callable_signature(getattr(processor_cls, "__call__", processor_cls))
    result["signatures"]["model_init"] = inspect_callable_signature(model_cls.__init__)
    result["signatures"]["model_forward"] = inspect_callable_signature(getattr(model_cls, "forward", model_cls.__call__))
    result["capabilities"]["video_tracker_api"] = True
    try:
        torch = importlib.import_module("torch")
        np = importlib.import_module("numpy")
        Image = importlib.import_module("PIL.Image")
        selected_device = choose_device(device, torch)
        result["device"] = {"requested": device, "selected": selected_device, "cuda": cuda_info(torch)}
        if video_path is None:
            raise RuntimeError("--video is required for real temporal API proof")
        frames, input_source = decode_temporal_frames(video_path, max_frames=3, start_frame=0)
        width, height = frames[0].size
        result["input_source"] = input_source
        result["decoded_frame_count"] = len(frames)
        result["frame_dimensions"] = {"width": width, "height": height}
        processor = processor_cls.from_pretrained(str(model_path), local_files_only=True, trust_remote_code=True)
        model = model_cls.from_pretrained(str(model_path), local_files_only=True, trust_remote_code=True)
        model.to(selected_device)
        model.eval()
    except Exception as exc:
        result["blockers"].append(f"official temporal API setup failed: {type(exc).__name__}: {exc}")
        return result
    result["proof"].update({"model_loaded": True, "frames": [], "successful_frame_count": 0, "total_nonzero_pixels": 0})
    result["signatures"]["loaded_processor_call"] = inspect_callable_signature(processor.__call__)
    result["signatures"]["loaded_model_forward"] = inspect_callable_signature(getattr(model, "forward", model.__call__))
    result["signatures"]["init_video_session"] = inspect_callable_signature(processor.init_video_session)
    result["signatures"]["add_inputs_to_inference_session"] = inspect_callable_signature(processor.add_inputs_to_inference_session)
    result["signatures"]["propagate_in_video_iterator"] = inspect_callable_signature(model.propagate_in_video_iterator)
    try:
        result["proof"]["invocation_attempted"] = True
        session = call_with_supported_kwargs(processor.init_video_session, {"video": frames, "inference_device": selected_device, "processing_device": selected_device})
        prompt = {"x": 200.0, "y": 220.0, "label": 1}
        prompt_kwargs = {
            "inference_session": session,
            "frame_idx": 0,
            "obj_ids": [1],
            "input_points": [[[[prompt["x"], prompt["y"]]]]],
            "input_labels": [[[prompt["label"]]]],
            "original_size": (height, width),
        }
        prompt_out = call_with_supported_kwargs(processor.add_inputs_to_inference_session, prompt_kwargs)
        call_with_supported_kwargs(model.forward, {"inference_session": session, "frame_idx": 0})
        result["session"] = {"status": "initialized_and_prompted", "prompt_output_type": type(prompt_out).__name__}
        result["prompts"] = {"point": prompt, "box": None}
        iterator = call_with_supported_kwargs(model.propagate_in_video_iterator, {"inference_session": session, "start_frame_idx": 0, "max_frame_num_to_track": len(frames), "show_progress_bar": False})
        for item in iterator:
            frame_idx = int(getattr(item, "frame_idx", item.get("frame_idx", len(result["proof"]["frames"])) if isinstance(item, dict) else len(result["proof"]["frames"])))
            masks = normalize_temporal_masks_for_postprocess(extract_temporal_masks(item))
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
            saved = temporal_mask_to_png(masks, (output_dir or Path("/tmp/flux-sam3-temporal-proof")) / f"mask_{frame_idx:06d}.png", (width, height), np, Image)
            if post_process_error:
                saved["post_process_masks_error"] = post_process_error
            saved["frame_idx"] = frame_idx
            result["proof"]["frames"].append(saved)
            if len(result["proof"]["frames"]) >= len(frames):
                break
        total = sum(int(f["nonzero_pixels"]) for f in result["proof"]["frames"])
        result["proof"].update(successful_frame_count=len(result["proof"]["frames"]), total_nonzero_pixels=total)
        if total > 0:
            result["status"] = "succeeded"
            result["capabilities"]["video_propagation_or_later_frame_correction"] = True
        else:
            result["blockers"].append("all propagated masks were empty")
    except Exception as exc:
        result["blockers"].append(f"SAM3 tracker-video temporal invocation failed without API fallback or fake masks: {type(exc).__name__}: {exc}")
    return result


def transformers_backend(model_path: Path, device: str) -> dict[str, Any]:
    from transformers import Sam3Model, Sam3Processor, Sam3TrackerModel, Sam3TrackerProcessor, pipeline

    backend: dict[str, Any] = {"name": "transformers", "kind": "sam3_concept_and_tracker", "capabilities": {}, "blockers": {}}
    errors: list[str] = []
    concept_loaded = False
    tracker_loaded = False
    try:
        processor = Sam3Processor.from_pretrained(str(model_path), local_files_only=True)
        model = Sam3Model.from_pretrained(str(model_path), local_files_only=True)
        model.to(device)
        model.eval()
        backend.update({"processor": processor, "model": model})
        proc_sig = inspect.signature(processor.__call__)
        params = set(proc_sig.parameters)
        temporal = temporal_api_proof(model_path, device)
        backend["capabilities"].update({
            "concept_text_prompt": "text" in params,
            "concept_box_prompt": "input_boxes" in params,
            "text_prompt": "text" in params,
            "mask_paint_prompt": "segmentation_maps" in params,
            "video_tracker_api": bool(temporal.get("capabilities", {}).get("video_tracker_api")),
            "video_propagation_or_later_frame_correction": temporal.get("status") == "succeeded",
        })
        if temporal.get("status") != "succeeded":
            backend["blockers"]["video_propagation_or_later_frame_correction"] = "; ".join(temporal.get("blockers", [])) or "official temporal/video SAM3 API not proven"
            backend["temporal_api_proof"] = temporal
        else:
            backend["temporal_api_proof"] = temporal
        concept_loaded = True
    except Exception as exc:
        errors.append(f"Sam3Processor/Sam3Model concept backend failed: {type(exc).__name__}: {exc}")
    try:
        tracker_processor = Sam3TrackerProcessor.from_pretrained(str(model_path), local_files_only=True)
        tracker_model = Sam3TrackerModel.from_pretrained(str(model_path), local_files_only=True)
        tracker_model.to(device)
        tracker_model.eval()
        backend.update({"tracker_processor": tracker_processor, "tracker_model": tracker_model})
        backend["capabilities"].update({"tracker_point_prompt": True, "point_prompt": True, "tracker_box_prompt": True, "box_prompt": True})
        tracker_loaded = True
    except Exception as exc:
        blocker = f"Sam3TrackerProcessor/Sam3TrackerModel tracker backend failed: {type(exc).__name__}: {exc}"
        backend["blockers"]["tracker_point_prompt"] = blocker
        backend["blockers"]["tracker_box_prompt"] = blocker
        backend["capabilities"].update({"tracker_point_prompt": False, "point_prompt": False, "tracker_box_prompt": False, "box_prompt": False})
    if concept_loaded:
        return backend
    try:
        pipe = pipeline("mask-generation", model=str(model_path), device=0 if device == "cuda" else -1, trust_remote_code=True, local_files_only=True)
        backend.update({"kind": "pipeline_mask_generation", "pipeline": pipe})
        temporal = temporal_api_proof(model_path, device)
        backend["capabilities"].update({"concept_text_prompt": False, "concept_box_prompt": False, "text_prompt": False, "box_prompt": tracker_loaded, "tracker_box_prompt": tracker_loaded, "point_prompt": tracker_loaded, "tracker_point_prompt": tracker_loaded, "mask_paint_prompt": False, "video_tracker_api": bool(temporal.get("capabilities", {}).get("video_tracker_api")), "video_propagation_or_later_frame_correction": temporal.get("status") == "succeeded"})
        backend["temporal_api_proof"] = temporal
        if temporal.get("status") != "succeeded":
            backend["blockers"]["video_propagation_or_later_frame_correction"] = "; ".join(temporal.get("blockers", [])) or "official temporal/video SAM3 API not proven"
        if tracker_loaded:
            return backend
        errors.append("tracker backend unavailable")
        return backend
    except Exception as exc:
        errors.append(f"transformers pipeline failed: {type(exc).__name__}: {exc}")
    if tracker_loaded:
        return backend
    raise RuntimeError("No usable local Transformers SAM3 API found. " + " | ".join(errors))


def run_prompt(backend: dict[str, Any], prompt_kind: str, image: Any, prompt: Any, out_path: Path, source_size: tuple[int, int], np: Any, Image: Any) -> dict[str, Any]:
    result: dict[str, Any] = {"status": "blocked", "prompt": prompt, "runtime_blocker": None, "unsupported_by_detected_api": False}
    try:
        if prompt_kind in {"point", "box"}:
            tracker_capability = f"tracker_{prompt_kind}_prompt"
            if not backend.get("capabilities", {}).get(tracker_capability, False):
                blocker = backend.get("blockers", {}).get(tracker_capability, "official SAM3 tracker backend is unavailable")
                result.update(status="unsupported", unsupported_by_detected_api=True, runtime_blocker=blocker)
                return result
            processor = backend["tracker_processor"]
            model = backend["tracker_model"]
            if prompt_kind == "point":
                kwargs = {"images": image, "input_points": [[[[prompt["x"], prompt["y"]]]]], "input_labels": [[[prompt["label"]]]], "return_tensors": "pt"}
            else:
                kwargs = {"images": image, "input_boxes": [[prompt]], "input_boxes_labels": [[1]], "return_tensors": "pt"}
            inputs = call_with_supported_kwargs(processor, kwargs)
            if hasattr(inputs, "to"):
                inputs = inputs.to(next(model.parameters()).device)
            with importlib.import_module("torch").no_grad():
                outputs = model(**dict(inputs))
            masks = processor.post_process_masks(outputs.pred_masks.cpu(), inputs["original_sizes"])[0]
            saved = save_mask(masks, out_path, source_size, np, Image)
            if int(saved["nonzero_pixels"]) <= 0:
                raise RuntimeError(f"official tracker returned an empty {prompt_kind} mask")
            result.update(status="succeeded", **saved)
            return result
        if "model" not in backend or "processor" not in backend:
            result.update(status="unsupported", unsupported_by_detected_api=True, runtime_blocker="detected backend does not expose concept SAM prompted calls")
            return result
        if not backend.get("capabilities", {}).get(f"concept_{prompt_kind}_prompt", False):
            result.update(status="unsupported", unsupported_by_detected_api=True, runtime_blocker=f"detected concept processor API does not advertise {prompt_kind} prompts")
            return result
        processor = backend["processor"]
        model = backend["model"]
        kwargs: dict[str, Any] = {"images": image, "return_tensors": "pt"}
        if prompt_kind == "text":
            kwargs.update({"text": prompt})
        inputs = call_with_supported_kwargs(processor, kwargs)
        if hasattr(inputs, "to"):
            inputs = inputs.to(next(model.parameters()).device)
        with importlib.import_module("torch").no_grad():
            output = model(**dict(inputs))
        mask = postprocess_mask(processor, output, source_size)
        saved = save_mask(mask, out_path, source_size, np, Image)
        if int(saved["nonzero_pixels"]) <= 0:
            raise RuntimeError(f"concept backend returned an empty {prompt_kind} mask")
        result.update(status="succeeded", **saved)
    except Exception as exc:
        result.update(status="blocked", runtime_blocker=f"{type(exc).__name__}: {exc}")
    return result


def run_temporal_api_proof(args: argparse.Namespace) -> int:
    check = self_check_payload()
    output_dir = args.output_dir or Path("/tmp/flux-sam3-temporal-proof")
    output_dir.mkdir(parents=True, exist_ok=True)
    result: dict[str, Any] = {"status": "blocked", "self_check": check, "temporal_api_proof": None}
    try:
        _model, local_path, installed = load_flux_model()
        if not installed or not local_path.is_dir():
            raise RuntimeError(f"SAM3 Transformers model is not installed at manifest-resolved path: {local_path}")
        result["temporal_api_proof"] = temporal_api_proof(local_path, args.device, output_dir, args.video)
    except Exception as exc:
        result["temporal_api_proof"] = {
            "schema": "org.flux.ai.sam3-transformers-temporal-api-proof.v1",
            "status": "blocked",
            "backend": {"name": "transformers", "kind": "sam3_tracker_video"},
            "capabilities": {"video_tracker_api": False, "video_propagation_or_later_frame_correction": False},
            "classes": {"processor": "Sam3TrackerVideoProcessor", "model": "Sam3TrackerVideoModel"},
            "blockers": [f"manifest/model lookup failed: {type(exc).__name__}: {exc}"],
            "proof": {"invocation_attempted": False, "fake_masks_generated": False},
        }
    proof = result["temporal_api_proof"] or {}
    result["status"] = "succeeded" if proof.get("status") == "succeeded" else "blocked"
    (output_dir / "sam3_transformers_temporal_api_proof_result.json").write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    if args.json:
        json_dump(result)
    else:
        print(result["status"])
    return 0 if result["status"] == "succeeded" else 4


def run_real(args: argparse.Namespace) -> int:
    check = self_check_payload()
    if not check["ready"]:
        if args.json:
            json_dump({"status": "blocked", "self_check": check})
        else:
            print("ERROR: SAM3 Transformers real inference blocked: " + "; ".join(check["blockers"]), file=sys.stderr)
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
    kind = args.prompt_kind
    prompt_value: Any = {"text": args.text, "box": args.box, "point": args.point}[kind]
    prompt_contract: dict[str, Any] = {"kind": kind, "source_width": args.source_width, "source_height": args.source_height}
    if kind == "text":
        prompt_contract["text"] = args.text
    elif kind == "point":
        prompt_contract["source_xy"] = args.point
        if args.point_source_float is not None:
            prompt_contract["source_float"] = args.point_source_float
    elif kind == "box":
        prompt_contract["source_xyxy"] = args.box
        if args.box_source_float_min is not None:
            prompt_contract["source_float_min"] = args.box_source_float_min
        if args.box_source_float_max is not None:
            prompt_contract["source_float_max"] = args.box_source_float_max
    result: dict[str, Any] = {
        "schema": "org.flux.ai.sam3-transformers-real-inference-result.v1",
        "timestamp": datetime.now(timezone.utc).isoformat(),
        "python": check["python"],
        "dependencies": check["dependencies"],
        "device": {"requested": args.device, "selected": device, "cuda": cuda_info(torch)},
        "model": {"id": model["id"], "repo": model["source"].get("repo"), "revision": model["source"].get("revision"), "local_path": str(local_path)},
        "source_image": {"path": str(image_path), "width": image.size[0], "height": image.size[1], "declared_width": args.source_width, "declared_height": args.source_height},
        "selected_prompt_kind": kind,
        "prompt_contract": prompt_contract,
        "prompts": {kind: prompt_value, "mask_prompt_path": str(args.mask_prompt) if args.mask_prompt else None, "video_path": str(args.video) if args.video else None},
        "backend": {},
        "proofs": {},
        "selected_mask_path": None,
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
    result["backend"] = {"name": backend.get("name"), "kind": backend.get("kind"), "capabilities": backend.get("capabilities", {}), "blockers": backend.get("blockers", {})}
    proof = run_prompt(backend, kind, image, prompt_value, output_dir / f"mask_{kind}.png", image.size, np, Image)
    result["proofs"][kind] = proof
    if proof.get("status") == "succeeded":
        result["selected_mask_path"] = proof.get("mask_path")
    result["unsupported"]["mask_paint_prompt"] = "not_proven" if args.mask_prompt else "not_requested"
    result["unsupported"]["video_propagation_or_later_frame_correction"] = "not_proven" if args.video else "not_requested"
    (output_dir / RESULT_NAME).write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    if args.json:
        json_dump(result)
    return 0 if proof.get("status") == "succeeded" else 4


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description="Flux standalone SAM3 Transformers real inference proof probe")
    p.add_argument("--self-check", action="store_true", help="report runtime/model availability without running inference")
    p.add_argument("--temporal-api-proof", action="store_true", help="prove or structured-block official Sam3TrackerVideoProcessor/Sam3TrackerVideoModel temporal API")
    p.add_argument("--json", action="store_true", help="emit JSON to stdout")
    p.add_argument("--image", type=Path, help="input still image")
    p.add_argument("--output-dir", type=Path, help="directory for result JSON and real mask images")
    p.add_argument("--prompt-kind", choices=("text", "point", "box"), help="selected prompt kind to run; exactly one of text, point, or box")
    p.add_argument("--source-width", type=int, help="exported source-frame width in pixels")
    p.add_argument("--source-height", type=int, help="exported source-frame height in pixels")
    p.add_argument("--text", help="selected text prompt; required when --prompt-kind text")
    p.add_argument("--box", type=parse_box, help="selected box prompt x1,y1,x2,y2; required when --prompt-kind box")
    p.add_argument("--point", type=parse_point, help="selected point prompt x,y,label; required when --prompt-kind point")
    p.add_argument("--point-source-float", type=parse_xy_float, help="original source-frame float point x,y metadata")
    p.add_argument("--box-source-float-min", type=parse_xy_float, help="original source-frame float box min x,y metadata")
    p.add_argument("--box-source-float-max", type=parse_xy_float, help="original source-frame float box max x,y metadata")
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
        if args.temporal_api_proof:
            return run_temporal_api_proof(args)
        if not args.image or not args.output_dir:
            print("ERROR: --image and --output-dir are required unless --self-check is used", file=sys.stderr)
            return 2
        if not args.prompt_kind or not args.source_width or not args.source_height:
            print("ERROR: --prompt-kind, --source-width, and --source-height are required", file=sys.stderr)
            return 2
        if args.source_width <= 0 or args.source_height <= 0:
            print("ERROR: --source-width and --source-height must be positive", file=sys.stderr)
            return 2
        if args.prompt_kind == "text" and not (args.text and args.text.strip()):
            print("ERROR: --text is required when --prompt-kind text", file=sys.stderr)
            return 2
        if args.prompt_kind == "point" and args.point is None:
            print("ERROR: --point is required when --prompt-kind point", file=sys.stderr)
            return 2
        if args.prompt_kind == "box" and args.box is None:
            print("ERROR: --box is required when --prompt-kind box", file=sys.stderr)
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
