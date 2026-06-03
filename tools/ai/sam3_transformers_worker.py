#!/usr/bin/env python3
"""Long-lived JSON-lines SAM3 Transformers worker for Flux."""
from __future__ import annotations

import contextlib
import gc
import importlib
import inspect
import json
import sys
import traceback
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Callable

from sam3_transformers_real_inference_probe import (
    RESULT_NAME,
    choose_device,
    cuda_info,
    load_flux_model,
    run_prompt,
    self_check_payload,
    temporal_api_proof,
    transformers_backend,
)

SCHEMA = "org.flux.ai.sam3-transformers-worker.v1"


def call_supported(fn: Any, kwargs: dict[str, Any]) -> Any:
    try:
        params = set(inspect.signature(fn).parameters)
    except Exception:
        return fn(**kwargs)
    return fn(**{k: v for k, v in kwargs.items() if k in params})


def extract_video_masks(output: Any) -> Any:
    if isinstance(output, dict):
        for key in ("pred_masks", "masks", "mask_logits"):
            if key in output:
                return output[key]
    for key in ("pred_masks", "masks", "mask_logits"):
        if hasattr(output, key):
            return getattr(output, key)
    return output


def normalize_video_masks_for_postprocess(masks: Any) -> Any:
    if hasattr(masks, "dim"):
        if masks.dim() == 2:
            return masks.unsqueeze(0).unsqueeze(0)
        if masks.dim() == 3:
            return masks.unsqueeze(1)
    return masks


def save_video_mask(mask: Any, path: Path, source_size: tuple[int, int], np: Any, Image: Any) -> dict[str, Any]:
    while isinstance(mask, (list, tuple)):
        if not mask:
            raise RuntimeError("backend returned empty mask list")
        mask = mask[0]
    if hasattr(mask, "detach"):
        mask = mask.detach().cpu().numpy()
    arr = np.asarray(mask)
    while arr.ndim > 2:
        arr = arr[0]
    if arr.size == 0:
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


class Worker:
    def __init__(self) -> None:
        self.backend: dict[str, Any] | None = None
        self.device = "auto"
        self.selected_device: str | None = None
        self.model: dict[str, Any] | None = None
        self.local_path: Path | None = None
        self.torch: Any | None = None
        self.np: Any | None = None
        self.Image: Any | None = None
        self.video_processor: Any | None = None
        self.video_model: Any | None = None
        self.cancelled: set[str] = set()

    def status(self) -> dict[str, Any]:
        payload = self_check_payload()
        payload.update({
            "worker_schema": SCHEMA,
            "loaded": self.backend is not None,
            "selected_device": self.selected_device,
            "backend": self.public_backend(),
        })
        return payload

    def public_backend(self) -> dict[str, Any] | None:
        if not self.backend:
            return None
        return {
            "name": self.backend.get("name"),
            "kind": self.backend.get("kind"),
            "capabilities": self.backend.get("capabilities", {}),
            "blockers": self.backend.get("blockers", {}),
            "temporal_api_proof": self.backend.get("temporal_api_proof"),
        }

    def load(self, request: dict[str, Any]) -> dict[str, Any]:
        if self.backend is None:
            check = self_check_payload()
            if not check.get("ready"):
                return {"status": "blocked", "self_check": check}
            import numpy as np
            from PIL import Image
            import torch

            self.np = np
            self.Image = Image
            self.torch = torch
            self.device = str(request.get("device") or "auto")
            self.selected_device = choose_device(self.device, torch)
            self.model, self.local_path, _installed = load_flux_model()
            self.backend = transformers_backend(self.local_path, self.selected_device)
        return {
            "status": "ok",
            "loaded": True,
            "python": {"executable": sys.executable},
            "device": {"requested": self.device, "selected": self.selected_device, "cuda": cuda_info(self.torch)},
            "model": {
                "id": self.model.get("id") if self.model else None,
                "repo": (self.model.get("source", {}) if self.model else {}).get("repo"),
                "revision": (self.model.get("source", {}) if self.model else {}).get("revision"),
                "local_path": str(self.local_path) if self.local_path else None,
            },
            "backend": self.public_backend(),
        }

    def unload(self) -> dict[str, Any]:
        self.backend = None
        self.video_processor = None
        self.video_model = None
        self.model = None
        self.local_path = None
        gc.collect()
        if self.torch is not None:
            try:
                if self.torch.cuda.is_available():
                    self.torch.cuda.empty_cache()
                    self.torch.cuda.ipc_collect()
            except Exception:
                pass
        return {"status": "ok", "loaded": False}

    def normalize_prompt(self, prompt: dict[str, Any]) -> tuple[str, Any, dict[str, Any]]:
        kind = str(prompt.get("type") or prompt.get("kind") or "")
        contract = dict(prompt)
        if kind == "text":
            value = str(prompt.get("text") or prompt.get("prompt") or "")
            if not value.strip():
                raise ValueError("text prompt is empty")
            return kind, value, contract
        if kind == "point":
            if isinstance(prompt.get("point"), dict):
                p = prompt["point"]
                value = {"x": int(p["x"]), "y": int(p["y"]), "label": int(p.get("label", prompt.get("label", 1)))}
            elif isinstance(prompt.get("source_xy"), dict):
                p = prompt["source_xy"]
                value = {"x": int(p["x"]), "y": int(p["y"]), "label": int(prompt.get("label", 1))}
            else:
                xy = prompt.get("source_xy") or prompt.get("xy")
                value = {"x": int(xy[0]), "y": int(xy[1]), "label": int(prompt.get("label", 1))}
            return kind, value, contract
        if kind == "box":
            box = prompt.get("box") or prompt.get("source_xyxy")
            if not isinstance(box, list) or len(box) != 4:
                raise ValueError("box prompt must contain four coordinates")
            return kind, [float(v) for v in box], contract
        raise ValueError(f"unsupported prompt kind: {kind}")

    def ensure_video_backend(self) -> tuple[Any, Any]:
        if self.video_processor is not None and self.video_model is not None:
            return self.video_processor, self.video_model
        if not self.local_path:
            raise RuntimeError("SAM3 model path is unavailable; load the worker before video inference")
        transformers = importlib.import_module("transformers")
        processor_cls = getattr(transformers, "Sam3TrackerVideoProcessor")
        model_cls = getattr(transformers, "Sam3TrackerVideoModel")
        processor = processor_cls.from_pretrained(str(self.local_path), local_files_only=True, trust_remote_code=True)
        model = model_cls.from_pretrained(str(self.local_path), local_files_only=True, trust_remote_code=True)
        model.to(self.selected_device or "cpu")
        model.eval()
        self.video_processor = processor
        self.video_model = model
        return processor, model

    def infer_still(self, request: dict[str, Any], progress: Callable[[dict[str, Any]], None] | None = None) -> dict[str, Any]:
        if progress:
            progress({"message": "Loading SAM3", "percent": 5})
        load_result = self.load(request)
        if load_result.get("status") != "ok":
            return load_result
        image_path = Path(str(request.get("image") or request.get("source_image") or request.get("source_image_path") or ""))
        output_dir = Path(str(request.get("output_dir") or ""))
        prompts = request.get("prompts") or []
        source_width = int(request.get("source_width") or request.get("width") or 0)
        source_height = int(request.get("source_height") or request.get("height") or 0)
        if not image_path.is_file():
            raise ValueError(f"source image missing: {image_path}")
        if not prompts:
            raise ValueError("infer_still requires at least one prompt")
        output_dir.mkdir(parents=True, exist_ok=True)
        image = self.Image.open(image_path).convert("RGB")
        source_size = (source_width or image.size[0], source_height or image.size[1])
        if progress:
            progress({"message": "Preparing prompts", "percent": 10, "total": len(prompts), "completed": 0})
        result: dict[str, Any] = {
            "schema": "org.flux.ai.sam3-transformers-worker-inference-result.v1",
            "timestamp": datetime.now(timezone.utc).isoformat(),
            "source_image": {"path": str(image_path), "width": image.size[0], "height": image.size[1], "declared_width": source_size[0], "declared_height": source_size[1]},
            "backend": self.public_backend(),
            "proofs": {},
            "masks": {},
            "errors": [],
        }
        combined = None
        for index, prompt in enumerate(prompts):
            if progress:
                progress({"message": f"Running SAM3 prompt {index + 1}/{len(prompts)}", "percent": 10 + int((index / max(1, len(prompts))) * 80), "total": len(prompts), "completed": index})
            kind, value, contract = self.normalize_prompt(prompt)
            key = str(prompt.get("id") or f"{kind}_{index}")
            out_path = output_dir / f"mask_{key}.png"
            proof = run_prompt(self.backend, kind, image, value, out_path, source_size, self.np, self.Image)
            proof["prompt_contract"] = contract
            result["proofs"][key] = proof
            if proof.get("status") == "succeeded" and int(proof.get("nonzero_pixels") or 0) > 0:
                result["masks"][key] = proof.get("mask_path")
                mask = self.Image.open(out_path).convert("L")
                combined = mask if combined is None else self.Image.fromarray(self.np.maximum(self.np.array(combined), self.np.array(mask)).astype("uint8"), mode="L")
            else:
                result["errors"].append({"prompt": key, "status": proof.get("status"), "runtime_blocker": proof.get("runtime_blocker")})
            if progress:
                progress({"message": f"Completed SAM3 prompt {index + 1}/{len(prompts)}", "percent": 10 + int(((index + 1) / max(1, len(prompts))) * 80), "total": len(prompts), "completed": index + 1})
        if combined is not None:
            if progress:
                progress({"message": "Writing combined mask", "percent": 92})
            combined_path = output_dir / "mask_live_combined.png"
            combined.save(combined_path)
            result["masks"]["combined"] = str(combined_path)
            result["selected_mask_path"] = str(combined_path)
            result["combined_prompt_count"] = len(result["masks"]) - 1
        else:
            result["selected_mask_path"] = None
        result_path = output_dir / RESULT_NAME
        if progress:
            progress({"message": "Writing SAM3 result", "percent": 96})
        result_path.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        if progress:
            progress({"message": "SAM3 inference complete", "percent": 100})
        return {"status": "ok" if result["masks"] else "blocked", "result_path": str(result_path), "result": result, "loaded": self.backend is not None}

    def infer_video(self, request: dict[str, Any], progress: Callable[[dict[str, Any]], None] | None = None) -> dict[str, Any]:
        if progress:
            progress({"message": "Loading SAM3 video tracker", "percent": 3})
        load_result = self.load(request)
        if load_result.get("status") != "ok":
            return load_result
        frames_spec = request.get("frames") or []
        prompts = request.get("prompts") or []
        output_dir = Path(str(request.get("output_dir") or ""))
        source_width = int(request.get("source_width") or request.get("width") or 0)
        source_height = int(request.get("source_height") or request.get("height") or 0)
        if not frames_spec:
            raise ValueError("infer_video requires exported source frames")
        if not prompts:
            raise ValueError("infer_video requires at least one prompt")
        output_dir.mkdir(parents=True, exist_ok=True)

        frames = []
        frame_numbers: list[int] = []
        source_frames: list[int] = []
        for index, frame_info in enumerate(frames_spec):
            info = frame_info if isinstance(frame_info, dict) else {"path": str(frame_info)}
            frame_path = Path(str(info.get("path") or ""))
            if not frame_path.is_file():
                raise ValueError(f"source video frame missing: {frame_path}")
            frames.append(self.Image.open(frame_path).convert("RGB"))
            frame_numbers.append(int(info.get("timeline_frame") or index + 1))
            source_frames.append(int(info.get("source_frame") or index + 1))
        source_size = (source_width or frames[0].size[0], source_height or frames[0].size[1])
        if progress:
            progress({"message": "Preparing SAM3 video session", "percent": 8, "total": len(frames), "completed": 0})

        # ── Build source_frame → sequence_index lookup ──
        source_frame_to_index: dict[int, int] = {}
        for i, sf in enumerate(source_frames):
            source_frame_to_index[sf] = i

        # ── Resolve each prompt's frame_idx and obj_id ──
        processor, model = self.ensure_video_backend()
        resolved_prompts: list[dict[str, Any]] = []
        for prompt_index, prompt in enumerate(prompts):
            kind, value, contract = self.normalize_prompt(prompt)
            if kind not in {"point", "box"}:
                continue
            provenance = contract.get("ai_paint_prompt", {})
            raw_source_frame = provenance.get("sourceFrame")
            raw_timeline_frame = provenance.get("timelineFrame")
            frame_idx = source_frame_to_index.get(int(raw_source_frame)) if raw_source_frame is not None else None
            frame_fallback = False
            if frame_idx is None:
                frame_idx = 0
                frame_fallback = True
            obj_id = prompt_index + 1
            resolved_prompts.append({
                "kind": kind,
                "value": value,
                "contract": contract,
                "obj_id": obj_id,
                "prompt_index": prompt_index,
                "frame_idx": frame_idx,
                "frame_fallback": frame_fallback,
                "seeded_frame_timeline": frame_numbers[frame_idx] if frame_idx < len(frame_numbers) else None,
            })
        if not resolved_prompts:
            raise ValueError("infer_video requires at least one point or box prompt")

        first_contract = resolved_prompts[0]["contract"]
        result: dict[str, Any] = {
            "schema": "org.flux.ai.sam3-transformers-worker-video-inference-result.v1",
            "timestamp": datetime.now(timezone.utc).isoformat(),
            "source_frames": {"count": len(frames), "timeline_frames": frame_numbers, "source_frames": source_frames, "declared_width": source_size[0], "declared_height": source_size[1]},
            "backend": {"name": "transformers", "kind": "sam3_tracker_video", "model_class": model.__class__.__name__, "processor_class": processor.__class__.__name__},
            "proofs": {},
            "masks": {},
            "errors": [],
            "prompts": {"used": first_contract, "provided_count": len(prompts), "resolved_count": len(resolved_prompts)},
        }

        def seed_video_session() -> Any:
            session = call_supported(processor.init_video_session, {"video": frames, "inference_device": self.selected_device, "processing_device": self.selected_device})
            for rp in resolved_prompts:
                fi = rp["frame_idx"]
                oid = rp["obj_id"]
                pk: dict[str, Any] = {"inference_session": session, "frame_idx": fi, "obj_ids": [oid], "original_size": (source_size[1], source_size[0])}
                if rp["kind"] == "point":
                    v = rp["value"]
                    pk.update({"input_points": [[[[v["x"], v["y"]]]]], "input_labels": [[[v["label"]]]]})
                else:
                    pk.update({"input_boxes": [[[rp["value"]]]]})
                call_supported(processor.add_inputs_to_inference_session, pk)
                with self.torch.inference_mode():
                    call_supported(model.forward, {"inference_session": session, "frame_idx": fi})
            return session

        session = seed_video_session()
        seeded_frame_indices: set[int] = {int(rp["frame_idx"]) for rp in resolved_prompts}
        result["session"] = {"status": "initialized_and_prompted", "seeded_frame_indices": sorted(seeded_frame_indices)}

        # ── Propagate in both temporal directions from the earliest prompt frame ──
        #
        # SAM3's tracker API only walks one direction per iterator call. Starting
        # at the earliest prompt and walking forward lets later prompted frames
        # act as corrections for the forward pass (e.g. prompts at 5 and 10 cover
        # 5→20). A second reverse pass from that same earliest prompt covers the
        # frames before the first reference (e.g. 5→1). For a single prompt at
        # frame 10 this yields 10→20 and 10→1 instead of only 10→20.
        start_frame_idx = min(seeded_frame_indices)
        propagation_passes: list[tuple[str, bool, int, int]] = [
            ("forward", False, start_frame_idx, len(frames) - start_frame_idx),
        ]
        if start_frame_idx > 0:
            propagation_passes.append(("reverse", True, start_frame_idx, start_frame_idx + 1))

        # ── Prepare per-object output directory ──
        multi_object = len(resolved_prompts) > 1
        per_object_dir = output_dir / "per_object"
        if multi_object:
            if per_object_dir.is_dir():
                for _old in per_object_dir.iterdir():
                    if _old.is_file():
                        _old.unlink()
            per_object_dir.mkdir(parents=True, exist_ok=True)

        proof_frames: list[dict[str, Any]] = []
        selected_mask_path = None
        per_object_nonzero: dict[int, int] = {rp["obj_id"]: 0 for rp in resolved_prompts}

        emitted_frame_indices: set[int] = set()
        completed_frames = 0
        for pass_name, reverse, pass_start_frame_idx, max_frame_num_to_track in propagation_passes:
            if pass_name != "forward":
                session = seed_video_session()
            with self.torch.inference_mode():
                iterator = call_supported(model.propagate_in_video_iterator, {"inference_session": session, "start_frame_idx": pass_start_frame_idx, "max_frame_num_to_track": max_frame_num_to_track, "reverse": reverse, "show_progress_bar": False})
                for sequence_index, item in enumerate(iterator):
                    item_frame_idx = getattr(item, "frame_idx", None)
                    if item_frame_idx is None and isinstance(item, dict):
                        item_frame_idx = item.get("frame_idx")
                        absolute_frame_index = int(item_frame_idx)
                    else:
                        absolute_frame_index = pass_start_frame_idx - sequence_index if reverse else pass_start_frame_idx + sequence_index
                    if absolute_frame_index < 0 or absolute_frame_index >= len(frames):
                        break
                    if absolute_frame_index in emitted_frame_indices:
                        continue
                    emitted_frame_indices.add(absolute_frame_index)
                    timeline_frame = frame_numbers[absolute_frame_index]
                    masks_raw = normalize_video_masks_for_postprocess(extract_video_masks(item))
                    post_arg = masks_raw if isinstance(masks_raw, list) else [masks_raw]
                    try:
                        processed_masks = processor.post_process_masks(post_arg, original_sizes=[(source_size[1], source_size[0])], mask_threshold=0.0, binarize=True)
                        if processed_masks is not None:
                            masks_raw = processed_masks
                    except TypeError:
                        processed_masks = processor.post_process_masks(post_arg, original_sizes=[(source_size[1], source_size[0])])
                        if processed_masks is not None:
                            masks_raw = processed_masks
    
                    # ── Split per-object masks ──
                    arr_combined: Any = None
                    np = self.np
                    obj_masks_raw: dict[int, Any] = {}
                    if multi_object:
                        tensor = masks_raw
                        if isinstance(tensor, list):
                            tensor = tensor[0] if len(tensor) == 1 else tensor
                        if hasattr(tensor, "detach"):
                            arr_all = tensor.detach().cpu().numpy()
                        elif isinstance(tensor, list):
                            arr_all = np.stack([np.asarray(t) for t in tensor]) if len(tensor) > 1 else np.asarray(tensor[0])
                        else:
                            arr_all = np.asarray(tensor)
                        while arr_all.ndim > 4:
                            arr_all = arr_all[0]
                        if arr_all.ndim == 4 and arr_all.shape[0] >= len(resolved_prompts):
                            for rp in resolved_prompts:
                                oid = rp["obj_id"]
                                obj_arr = arr_all[oid - 1]
                                while obj_arr.ndim > 2:
                                    obj_arr = obj_arr[0]
                                obj_masks_raw[oid] = obj_arr
                            combined_arr = np.zeros_like(arr_all[0])
                            for oid in obj_masks_raw:
                                combined_arr = np.logical_or(combined_arr, obj_masks_raw[oid] > 0)
                            arr_combined = combined_arr
                        else:
                            while arr_all.ndim > 2:
                                arr_all = arr_all[0]
                            obj_masks_raw[1] = arr_all
                            arr_combined = arr_all
                    else:
                        obj_masks_raw[1] = masks_raw
    
                    # ── Save per-object masks ──
                    for oid, mask_data in obj_masks_raw.items():
                        if multi_object:
                            obj_path = per_object_dir / f"mask_obj{oid}_{timeline_frame:06d}.png"
                            if hasattr(mask_data, "detach"):
                                mask_data = mask_data.detach().cpu().numpy()
                            arr_obj = np.asarray(mask_data)
                            while arr_obj.ndim > 2:
                                arr_obj = arr_obj[0]
                            obj_img = self.Image.fromarray((arr_obj.astype("uint8") * 255) if arr_obj.dtype == np.bool_ else (arr_obj > 0).astype("uint8") * 255, mode="L")
                            if obj_img.size != source_size:
                                obj_img = obj_img.resize(source_size, resample=self.Image.Resampling.NEAREST)
                            obj_path.parent.mkdir(parents=True, exist_ok=True)
                            obj_img.save(obj_path)
                            per_object_nonzero[oid] = per_object_nonzero.get(oid, 0) + int(np.count_nonzero(arr_obj))
    
                    # ── Save combined mask ──
                    out_path = output_dir / f"mask_{timeline_frame:06d}.png"
                    if arr_combined is not None:
                        if hasattr(arr_combined, "detach"):
                            arr_combined = arr_combined.detach().cpu().numpy()
                        arr_c = np.asarray(arr_combined)
                        while arr_c.ndim > 2:
                            arr_c = arr_c[0]
                        if arr_c.dtype != np.bool_:
                            arr_c = arr_c > 0
                        combined_img = self.Image.fromarray((arr_c.astype("uint8") * 255), mode="L")
                        if combined_img.size != source_size:
                            combined_img = combined_img.resize(source_size, resample=self.Image.Resampling.NEAREST)
                            arr_c = np.asarray(combined_img) > 0
                        out_path.parent.mkdir(parents=True, exist_ok=True)
                        combined_img.save(out_path)
                        nonzero = int(np.count_nonzero(arr_c))
                        if selected_mask_path is None and nonzero > 0:
                            selected_mask_path = str(out_path)
                        proof_frames.append({"mask_path": str(out_path), "mask_size": list(combined_img.size), "nonzero_pixels": nonzero, "timeline_frame": timeline_frame, "source_frame": source_frames[absolute_frame_index], "propagation_pass": pass_name})
                    else:
                        saved = save_video_mask(masks_raw, out_path, source_size, np, self.Image)
                        saved["timeline_frame"] = timeline_frame
                        saved["source_frame"] = source_frames[absolute_frame_index]
                        saved["propagation_pass"] = pass_name
                        proof_frames.append(saved)
                        if selected_mask_path is None:
                            selected_mask_path = saved["mask_path"]
    
                    result["masks"][str(timeline_frame)] = str(out_path)
                    completed_frames = len(proof_frames)
                    if progress:
                        progress({"message": f"Tracked SAM3 {pass_name} frame {completed_frames}/{len(frames)}", "percent": 10 + int((completed_frames / max(1, len(frames))) * 85), "total": len(frames), "completed": completed_frames})
                    if completed_frames >= len(frames):
                        break

        total_nonzero = sum(int(frame["nonzero_pixels"]) for frame in proof_frames)
        result["proofs"]["video"] = {"status": "succeeded" if total_nonzero > 0 else "blocked", "frames": proof_frames, "nonzero_pixels": total_nonzero, "successful_frame_count": len(proof_frames)}
        if selected_mask_path:
            result["masks"]["combined"] = selected_mask_path
            result["selected_mask_path"] = selected_mask_path
            result["selected_mask_sequence_pattern"] = str(output_dir / "mask_######.png")
            result["combined_prompt_count"] = len(resolved_prompts)
        else:
            result["selected_mask_path"] = None
            result["errors"].append({"status": "blocked", "runtime_blocker": "SAM3 tracker-video produced no usable masks"})

        # ── Per-prompt manifest ──
        result["per_prompt"] = []
        for rp in resolved_prompts:
            result["per_prompt"].append({"obj_id": rp["obj_id"], "prompt_index": rp["prompt_index"], "kind": rp["kind"], "frame_idx": rp["frame_idx"], "frame_fallback": rp["frame_fallback"], "seeded_frame_timeline": rp["seeded_frame_timeline"]})
        if multi_object:
            result["masks"]["per_object_patterns"] = {str(rp["obj_id"]): str(per_object_dir / f"mask_obj{rp['obj_id']}_######.png") for rp in resolved_prompts}
            result["masks"]["per_object_nonzero"] = {str(oid): nz for oid, nz in per_object_nonzero.items()}

        result_path = output_dir / "sam3_transformers_video_inference_result.json"
        result_path.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        if progress:
            progress({"message": "SAM3 video inference complete", "percent": 100})
        return {"status": "ok" if selected_mask_path and total_nonzero > 0 else "blocked", "result_path": str(result_path), "result": result, "loaded": self.backend is not None}

    def temporal_api_proof(self, request: dict[str, Any]) -> dict[str, Any]:
        output_dir = Path(str(request.get("output_dir") or "/tmp/flux-sam3-temporal-worker-proof"))
        check = self_check_payload()
        try:
            self.model, self.local_path, installed = load_flux_model()
            if not installed or not self.local_path.is_dir():
                raise RuntimeError(f"SAM3 Transformers model is not installed at manifest-resolved path: {self.local_path}")
            proof = temporal_api_proof(self.local_path, str(request.get("device") or self.device or "auto"), output_dir, Path(str(request["video"])) if request.get("video") else None)
        except Exception as exc:
            proof = {
                "schema": "org.flux.ai.sam3-transformers-temporal-api-proof.v1",
                "status": "blocked",
                "backend": {"name": "transformers", "kind": "sam3_tracker_video"},
                "capabilities": {"video_tracker_api": False, "video_propagation_or_later_frame_correction": False},
                "classes": {"processor": "Sam3TrackerVideoProcessor", "model": "Sam3TrackerVideoModel"},
                "blockers": [f"manifest/model lookup failed: {type(exc).__name__}: {exc}"],
                "proof": {"invocation_attempted": False, "fake_masks_generated": False},
            }
        result = {
            "schema": "org.flux.ai.sam3-transformers-worker-temporal-api-proof-result.v1",
            "timestamp": datetime.now(timezone.utc).isoformat(),
            "status": "succeeded" if proof.get("status") == "succeeded" else "blocked",
            "backend": proof.get("backend"),
            "capabilities": proof.get("capabilities", {}),
            "blockers": proof.get("blockers", []),
            "temporal_api_proof": proof,
        }
        output_dir.mkdir(parents=True, exist_ok=True)
        result_path = output_dir / "sam3_transformers_temporal_api_proof_result.json"
        result_path.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        return {"status": result["status"], "result_path": str(result_path), "result": result, "loaded": self.backend is not None}

    def cancel(self, request: dict[str, Any]) -> dict[str, Any]:
        target = request.get("target_id") or request.get("request_id")
        if target:
            self.cancelled.add(str(target))
        return {"status": "ok", "cancelled": str(target) if target else None, "note": "cooperative cancel recorded; active synchronous inference cannot be interrupted safely"}


def emit(out: Any, payload: dict[str, Any]) -> None:
    out.write(json.dumps(payload, sort_keys=True, separators=(",", ":")) + "\n")
    out.flush()


def main() -> int:
    worker = Worker()
    real_stdout = sys.stdout
    sys.stdout = sys.stderr
    emit(real_stdout, {"schema": SCHEMA, "event": "ready"})
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        request_id = None
        try:
            request = json.loads(line)
            request_id = request.get("id") or request.get("request_id")
            command = request.get("command") or request.get("cmd")
            with contextlib.redirect_stdout(sys.stderr):
                if command == "status":
                    payload = worker.status()
                elif command == "load":
                    payload = worker.load(request)
                elif command == "infer_still":
                    def progress(progress_payload: dict[str, Any]) -> None:
                        emit(real_stdout, {"schema": SCHEMA, "event": "progress", "id": request_id, "command": command, "payload": progress_payload})
                    payload = worker.infer_still(request, progress)
                elif command == "infer_video":
                    def progress(progress_payload: dict[str, Any]) -> None:
                        emit(real_stdout, {"schema": SCHEMA, "event": "progress", "id": request_id, "command": command, "payload": progress_payload})
                    payload = worker.infer_video(request, progress)
                elif command == "temporal_api_proof":
                    payload = worker.temporal_api_proof(request)
                elif command == "unload":
                    payload = worker.unload()
                elif command == "cancel":
                    payload = worker.cancel(request)
                elif command == "shutdown":
                    payload = worker.unload()
                    payload["shutdown"] = True
                    emit(real_stdout, {"schema": SCHEMA, "id": request_id, "command": command, "ok": True, "payload": payload})
                    return 0
                else:
                    raise ValueError(f"unknown command: {command}")
            emit(real_stdout, {"schema": SCHEMA, "id": request_id, "command": command, "ok": payload.get("status") not in {"blocked", "error"}, "payload": payload})
        except Exception as exc:
            emit(real_stdout, {"schema": SCHEMA, "id": request_id, "ok": False, "error": f"{type(exc).__name__}: {exc}", "traceback": traceback.format_exc().splitlines()[-5:]})
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
