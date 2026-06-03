#!/usr/bin/env python3
"""Export MatAnyone2 active submodules to ONNX using real InferenceCore tensors.

This is a spike-only helper. It captures first real inputs seen by selected
MatAnyone2 submodules during a reduced InferenceCore warmup/frame loop, then
attempts per-submodule ONNX export. It does not try to export InferenceCore as a
single graph.
"""
from __future__ import annotations

import argparse
import json
import math
import traceback
from datetime import datetime, timezone
from glob import glob
from pathlib import Path
from typing import Any

import numpy as np
import torch
import torch.nn.functional as F
from PIL import Image

SCHEMA = "flux.ai_native_spikes.matanyone2_submodule_export.v1"
REPO_ROOT = Path(__file__).resolve().parents[3]
DEFAULT_BASELINE = REPO_ROOT / "tools/ai_native_spikes/results/matanyone2/baseline/baseline_manifest.json"
DEFAULT_OUT = REPO_ROOT / "tools/ai_native_spikes/results/matanyone2/export/submodules"
DEFAULT_TARGETS = ["pixel_encoder", "pix_feat_proj", "key_proj", "mask_encoder", "object_summarizer", "pixel_fuser", "object_transformer", "mask_decoder", "memory_readout"]


def tensor_summary(x: Any) -> Any:
    if isinstance(x, torch.Tensor):
        return {"shape": list(x.shape), "dtype": str(x.dtype), "device": str(x.device)}
    if isinstance(x, (list, tuple)):
        return {"type": type(x).__name__, "length": len(x), "items": [tensor_summary(v) for v in x[:5]]}
    if isinstance(x, dict):
        return {"type": "dict", "keys": list(x.keys())[:10]}
    if x is None:
        return None
    return {"type": type(x).__name__, "repr": repr(x)[:120]}


def clone_cpu(x: Any) -> Any:
    if isinstance(x, torch.Tensor):
        return x.detach().cpu()
    if isinstance(x, tuple):
        return tuple(clone_cpu(v) for v in x)
    if isinstance(x, list):
        return [clone_cpu(v) for v in x]
    if isinstance(x, dict):
        return {k: clone_cpu(v) for k, v in x.items()}
    return x


def flatten_tensors(x: Any) -> list[torch.Tensor]:
    out: list[torch.Tensor] = []
    if isinstance(x, torch.Tensor):
        out.append(x)
    elif isinstance(x, (list, tuple)):
        for v in x:
            out.extend(flatten_tensors(v))
    elif isinstance(x, dict):
        for k in sorted(x):
            out.extend(flatten_tensors(x[k]))
    return out


def find_model_dir() -> Path:
    base = Path.home() / ".local/share/Flux/models/matanyone2"
    for d in sorted(base.iterdir()) if base.is_dir() else []:
        if d.is_dir() and (d / "model.safetensors").is_file():
            return d
    raise FileNotFoundError(f"MatAnyone2 model.safetensors not found under {base}")


def load_inputs(baseline_manifest: Path, max_trace_frames: int, warmup_repeats: int, device: torch.device):
    frame_paths: list[str] = []
    mask_path = None
    if baseline_manifest.is_file():
        bl = json.loads(baseline_manifest.read_text(encoding="utf-8"))
        frames_dir = bl.get("frame_extraction", {}).get("frames_dir")
        if frames_dir and Path(frames_dir).is_dir():
            frame_paths = sorted(glob(str(Path(frames_dir) / "frame_*.png")))
        mask_path = bl.get("inputs", {}).get("mask", {}).get("path")
    if not frame_paths:
        fallback = REPO_ROOT / "tools/ai_native_spikes/results/matanyone2/baseline/frames"
        frame_paths = sorted(glob(str(fallback / "frame_*.png")))
    if not frame_paths:
        raise FileNotFoundError("No MatAnyone2 baseline frame_*.png files found")

    first_img = Image.open(frame_paths[0]).convert("RGB")
    if mask_path and Path(mask_path).is_file():
        mask_arr = np.array(Image.open(mask_path).convert("L"))
    else:
        mask_arr = np.ones((first_img.size[1], first_img.size[0]), dtype=np.uint8) * 255
    mask_tensor = torch.from_numpy(mask_arr.astype(np.float32)).to(device)

    first_tensor = torch.from_numpy(np.array(first_img)).permute(2, 0, 1).float()
    real_frame_tensors = []
    for fp in frame_paths[:max_trace_frames]:
        img = Image.open(fp).convert("RGB")
        real_frame_tensors.append(torch.from_numpy(np.array(img)).permute(2, 0, 1).float())
    all_tensors = [first_tensor] * warmup_repeats + real_frame_tensors
    return all_tensors, mask_tensor, frame_paths, mask_path


class MemoryReadoutModule(torch.nn.Module):
    """Standalone module implementing MatAnyone2 memory readout.

    Replaces the CPU computeMemoryReadout() with CUDA-capable ONNX ops:
      get_similarity -> top-k softmax -> value readout

    Input contract (matches C++ PropagationState layout):
      memory_key:       [CK, N]       – stored memory keys
      memory_shrinkage: [1, N]        – per-token shrinkage weights
      memory_value:     [CV, N]       – stored memory values (single object)
      query_key:        [CK, HW]      – query spatial keys
      query_selection:  [CK, HW]      – query selection weights

    Output:
      visual_readout:   [CV, HW]
    """

    def __init__(self, top_k: int = 30):
        super().__init__()
        self.top_k = top_k

    def forward(
        self,
        memory_key: torch.Tensor,
        memory_shrinkage: torch.Tensor,
        memory_value: torch.Tensor,
        query_key: torch.Tensor,
        query_selection: torch.Tensor,
    ) -> torch.Tensor:
        CK = memory_key.shape[0]
        N = memory_key.shape[1]
        CV = memory_value.shape[0]
        HW = query_key.shape[1]

        # Add batch dimension: [CK,N] -> [1,CK,N] etc.
        mk = memory_key.unsqueeze(0)           # [1, CK, N]
        ms = memory_shrinkage.view(1, N, 1)     # [1, N, 1]
        qk = query_key.unsqueeze(0)             # [1, CK, HW]
        qe = query_selection.unsqueeze(0)       # [1, CK, HW]

        # --- get_similarity (anisotropic L2) ---
        mk_t = mk.flatten(start_dim=2).transpose(1, 2)  # [1, N, CK]
        qk_f = qk.flatten(start_dim=2)                   # [1, CK, HW]
        qe_f = qe.flatten(start_dim=2)                   # [1, CK, HW]

        a_sq = mk_t.pow(2) @ qe_f                       # [1, N, HW]
        two_ab = 2.0 * (mk_t @ (qk_f * qe_f))           # [1, N, HW]
        b_sq = (qe_f * qk_f.pow(2)).sum(1, keepdim=True) # [1, 1, HW]

        similarity = (-a_sq + two_ab - b_sq) * ms / math.sqrt(float(CK))  # [1, N, HW]

        # --- top-k softmax ---
        values, indices = torch.topk(similarity, k=self.top_k, dim=1)  # [1, top_k, HW]
        x_exp = values.exp()
        x_exp = x_exp / x_exp.sum(dim=1, keepdim=True)
        affinity = torch.zeros_like(similarity).scatter_(1, indices, x_exp)  # [1, N, HW]

        # --- value readout ---
        mv = memory_value.unsqueeze(0)  # [1, CV, N]
        readout = torch.bmm(mv, affinity)  # [1, CV, HW]

        return readout.squeeze(0)  # [CV, HW]


class TensorTupleWrapper(torch.nn.Module):
    def __init__(self, module: torch.nn.Module, target: str, kwargs: dict[str, Any]):
        super().__init__()
        self.module_ref = module
        self.target = target
        self.kwargs = kwargs

    def forward(self, *flat_inputs):
        if self.target == "pixel_encoder":
            return tuple(flatten_tensors(self.module_ref(flat_inputs[0], seq_length=self.kwargs.get("seq_length"))))
        if self.target == "pix_feat_proj":
            return tuple(flatten_tensors(self.module_ref(flat_inputs[0])))
        if self.target == "key_proj":
            return tuple(flatten_tensors(self.module_ref(flat_inputs[0], need_s=bool(self.kwargs.get("need_s", False)), need_e=bool(self.kwargs.get("need_e", False)))))
        if self.target == "mask_encoder":
            others = flat_inputs[4] if len(flat_inputs) > 4 else None
            return tuple(flatten_tensors(self.module_ref(
                flat_inputs[0], flat_inputs[1], flat_inputs[2], flat_inputs[3], others,
                deep_update=bool(self.kwargs.get("deep_update", True)),
                chunk_size=int(self.kwargs.get("chunk_size", -1)),
            )))
        if self.target == "object_summarizer":
            return tuple(flatten_tensors(self.module_ref(
                flat_inputs[0], flat_inputs[1],
                need_weights=bool(self.kwargs.get("need_weights", False)),
            )))
        if self.target == "pixel_fuser":
            last_others = flat_inputs[4] if len(flat_inputs) > 4 else None
            return tuple(flatten_tensors(self.module_ref(
                flat_inputs[0], flat_inputs[1], flat_inputs[2], flat_inputs[3], last_others,
                chunk_size=int(self.kwargs.get("chunk_size", -1)),
            )))
        if self.target == "object_transformer":
            return tuple(flatten_tensors(self.module_ref(
                flat_inputs[0], flat_inputs[1],
                selector=self.kwargs.get("selector"),
                need_weights=bool(self.kwargs.get("need_weights", False)),
                seg_pass=bool(self.kwargs.get("seg_pass", False)),
            )))
        if self.target == "mask_decoder":
            # Captured flat order: ms_image_feat tensors..., memory_readout, sensory, optional last_mask.
            ms_count = int(self.kwargs.get("_ms_image_feat_count", 3))
            ms = tuple(flat_inputs[:ms_count])
            memory_readout = flat_inputs[ms_count]
            sensory = flat_inputs[ms_count + 1]
            last_mask = flat_inputs[ms_count + 2] if len(flat_inputs) > ms_count + 2 else None
            return tuple(flatten_tensors(self.module_ref(
                ms, memory_readout, sensory,
                chunk_size=int(self.kwargs.get("chunk_size", -1)),
                update_sensory=bool(self.kwargs.get("update_sensory", True)),
                seg_pass=bool(self.kwargs.get("seg_pass", False)),
                last_mask=last_mask,
                sigmoid_residual=bool(self.kwargs.get("sigmoid_residual", False)),
            )))
        return tuple(flatten_tensors(self.module_ref(*flat_inputs)))


def patch_matanyone2_group_interpolation() -> None:
    """Use fixed H/W sizes for group interpolation during ONNX tracing.

    MatAnyone2's default group helper calls F.interpolate(..., scale_factor=...)
    for area downsampling. The legacy ONNX exporter lowers some area paths to
    adaptive_avg_pool2d with a dynamic output_size, which blocks mask_decoder
    export. For this spike we trace fixed-size model inputs, so convert ratios
    to explicit output sizes.
    """
    import matanyone2.model.group_modules as group_modules

    def fixed_interpolate_groups(g: torch.Tensor, ratio: float, mode: str, align_corners: bool | None) -> torch.Tensor:
        batch_size, num_objects = g.shape[:2]
        flat = g.flatten(start_dim=0, end_dim=1)
        out_h = max(1, int(flat.shape[-2] * ratio))
        out_w = max(1, int(flat.shape[-1] * ratio))
        kwargs: dict[str, Any] = {"size": (out_h, out_w), "mode": mode}
        if align_corners is not None and mode in {"linear", "bilinear", "bicubic", "trilinear"}:
            kwargs["align_corners"] = align_corners
        out = F.interpolate(flat, **kwargs)
        return out.view(batch_size, num_objects, *out.shape[1:])

    def fixed_upsample_groups(g: torch.Tensor, ratio: float = 2, mode: str = "bilinear", align_corners: bool = False) -> torch.Tensor:
        return fixed_interpolate_groups(g, ratio, mode, align_corners)

    def fixed_downsample_groups(g: torch.Tensor, ratio: float = 1 / 2, mode: str = "area", align_corners: bool | None = None) -> torch.Tensor:
        return fixed_interpolate_groups(g, ratio, mode, align_corners)

    group_modules.interpolate_groups = fixed_interpolate_groups
    group_modules.upsample_groups = fixed_upsample_groups
    group_modules.downsample_groups = fixed_downsample_groups


def export_target(module: torch.nn.Module, target: str, captured: dict[str, Any], out_dir: Path, opset: int) -> dict[str, Any]:
    record: dict[str, Any] = {"target": target, "status": "running", "captured": captured.get("summary")}
    try:
        args = clone_cpu(captured["args"])
        kwargs = clone_cpu(captured["kwargs"])
        flat_inputs = flatten_tensors(args)
        if target == "mask_decoder" and args and isinstance(args[0], (list, tuple)):
            kwargs["_ms_image_feat_count"] = len(args[0])
        if target == "mask_decoder":
            flat_inputs = []
            flat_inputs.extend(flatten_tensors(args[0]))
            flat_inputs.extend(flatten_tensors(args[1]))
            flat_inputs.extend(flatten_tensors(args[2]))
            if kwargs.get("last_mask") is not None:
                flat_inputs.extend(flatten_tensors(kwargs["last_mask"]))
        if target == "mask_decoder":
            patch_matanyone2_group_interpolation()
            record["patches"] = ["fixed_size_group_interpolation"]
        module = module.cpu().eval()
        if target == "object_summarizer" and hasattr(module, "pos_enc"):
            module.pos_enc.cached_penc = None
        wrapper = TensorTupleWrapper(module, target, kwargs).eval()
        output_path = out_dir / f"matanyone2_{target}.onnx"
        output_path.parent.mkdir(parents=True, exist_ok=True)
        input_names = [f"input_{i}" for i in range(len(flat_inputs))]
        cpu_inputs = tuple(t.cpu() for t in flat_inputs)
        with torch.no_grad():
            probe_outputs = flatten_tensors(wrapper(*cpu_inputs))
            torch.onnx.export(
                wrapper,
                cpu_inputs,
                str(output_path),
                opset_version=opset,
                input_names=input_names,
                output_names=[f"output_{i}" for i in range(len(probe_outputs))],
                do_constant_folding=True,
                dynamo=False,
            )
        record.update({
            "status": "ok",
            "path": str(output_path),
            "size_bytes": output_path.stat().st_size,
            "input_count": len(flat_inputs),
            "input_shapes": [list(t.shape) for t in flat_inputs],
        })
        try:
            import onnx
            onnx.checker.check_model(onnx.load(str(output_path)))
            record["onnx_checker"] = "passed"
        except Exception as exc:
            record["onnx_checker"] = f"{type(exc).__name__}: {exc}"
        try:
            import onnxruntime as ort
            providers = []
            if "CUDAExecutionProvider" in ort.get_available_providers():
                providers.append("CUDAExecutionProvider")
            providers.append("CPUExecutionProvider")
            sess = ort.InferenceSession(str(output_path), providers=providers)
            input_arrays = {name: tensor.detach().cpu().numpy() for name, tensor in zip(input_names, cpu_inputs)}
            required_inputs = [inp.name for inp in sess.get_inputs()]
            feed = {name: input_arrays[name] for name in required_inputs if name in input_arrays}
            ort_outputs = sess.run(None, feed)
            parity = []
            for i, (ref_tensor, ort_out) in enumerate(zip(probe_outputs, ort_outputs)):
                ref = ref_tensor.detach().cpu().numpy()
                if ref.shape != ort_out.shape:
                    parity.append({"output": i, "error": f"shape mismatch: torch {list(ref.shape)} vs ort {list(ort_out.shape)}"})
                    continue
                diff = np.abs(ref.astype(np.float32) - ort_out.astype(np.float32))
                parity.append({
                    "output": i,
                    "mae": float(diff.mean()),
                    "max_error": float(diff.max()),
                    "shape": list(ref.shape),
                })
            record["ort_inference"] = {
                "providers_used": sess.get_providers(),
                "session_inputs": required_inputs,
                "num_outputs": len(ort_outputs),
                "output_shapes": [list(o.shape) for o in ort_outputs],
                "output_dtypes": [str(o.dtype) for o in ort_outputs],
                "torch_parity": parity,
            }
        except Exception as exc:
            record["ort_inference"] = {"error": f"{type(exc).__name__}: {exc}"}
    except Exception as exc:
        record.update({"status": "failed", "error": f"{type(exc).__name__}: {exc}", "traceback": traceback.format_exc().splitlines()[-12:]})
    return record


def export_memory_readout(out_dir: Path, opset: int, top_k: int = 30) -> dict[str, Any]:
    """Export the standalone memory_readout module to ONNX.

    This is a functional module (not a model submodule) so it uses
    representative synthetic inputs with dynamic axes for N and HW.
    """
    record: dict[str, Any] = {"target": "memory_readout", "status": "running"}
    try:
        CK, CV = 64, 256
        rep_N = 1620   # representative: one stride-16 frame
        rep_HW = 1620

        module = MemoryReadoutModule(top_k=top_k).cpu().eval()

        mk = torch.randn(CK, rep_N)
        ms = torch.randn(1, rep_N).abs()       # shrinkage is positive
        mv = torch.randn(CV, rep_N)
        qk = torch.randn(CK, rep_HW)
        qe = torch.randn(CK, rep_HW).abs()    # selection is positive

        cpu_inputs = (mk, ms, mv, qk, qe)
        input_names = ["memory_key", "memory_shrinkage", "memory_value",
                       "query_key", "query_selection"]
        dynamic_axes = {
            "memory_key": {1: "N"},
            "memory_shrinkage": {1: "N"},
            "memory_value": {1: "N"},
            "query_key": {1: "HW"},
            "query_selection": {1: "HW"},
            "visual_readout": {1: "HW"},
        }

        output_path = out_dir / "matanyone2_memory_readout.onnx"
        output_path.parent.mkdir(parents=True, exist_ok=True)

        with torch.no_grad():
            probe_outputs = module(*cpu_inputs)
            if isinstance(probe_outputs, torch.Tensor):
                probe_list = [probe_outputs]
            else:
                probe_list = list(probe_outputs)

        with torch.no_grad():
            torch.onnx.export(
                module,
                cpu_inputs,
                str(output_path),
                opset_version=opset,
                input_names=input_names,
                output_names=["visual_readout"],
                do_constant_folding=True,
                dynamic_axes=dynamic_axes,
                dynamo=False,
            )

        record.update({
            "status": "ok",
            "path": str(output_path),
            "size_bytes": output_path.stat().st_size,
            "input_count": len(cpu_inputs),
            "input_names": input_names,
            "input_shapes": [list(t.shape) for t in cpu_inputs],
            "output_shapes": [list(p.shape) for p in probe_list],
            "top_k": top_k,
            "dynamic_axes": {k: {str(d): n for d, n in v.items()} for k, v in dynamic_axes.items()},
        })

        try:
            import onnx
            onnx.checker.check_model(onnx.load(str(output_path)))
            record["onnx_checker"] = "passed"
        except Exception as exc:
            record["onnx_checker"] = f"{type(exc).__name__}: {exc}"

        # ORT inference + parity
        try:
            import onnxruntime as ort
            providers = []
            if "CUDAExecutionProvider" in ort.get_available_providers():
                providers.append("CUDAExecutionProvider")
            providers.append("CPUExecutionProvider")
            sess = ort.InferenceSession(str(output_path), providers=providers)
            input_arrays = {name: t.detach().cpu().numpy()
                            for name, t in zip(input_names, cpu_inputs)}
            required_inputs = [inp.name for inp in sess.get_inputs()]
            feed = {name: input_arrays[name] for name in required_inputs}
            ort_outputs = sess.run(None, feed)

            parity = []
            for i, (ref_tensor, ort_out) in enumerate(zip(probe_list, ort_outputs)):
                ref = ref_tensor.detach().cpu().numpy()
                if ref.shape != ort_out.shape:
                    parity.append({"output": i, "error": f"shape mismatch: torch {list(ref.shape)} vs ort {list(ort_out.shape)}"})
                    continue
                diff = np.abs(ref.astype(np.float32) - ort_out.astype(np.float32))
                parity.append({
                    "output": i,
                    "mae": float(diff.mean()),
                    "max_error": float(diff.max()),
                    "shape": list(ref.shape),
                })

            # Test dynamic shapes: smaller N and HW
            small_N, small_HW = 810, 810
            mk2 = torch.randn(CK, small_N)
            ms2 = torch.randn(1, small_N).abs()
            mv2 = torch.randn(CV, small_N)
            qk2 = torch.randn(CK, small_HW)
            qe2 = torch.randn(CK, small_HW).abs()
            feed2 = {
                "memory_key": mk2.numpy(),
                "memory_shrinkage": ms2.numpy(),
                "memory_value": mv2.numpy(),
                "query_key": qk2.numpy(),
                "query_selection": qe2.numpy(),
            }
            ort2 = sess.run(None, feed2)
            with torch.no_grad():
                ref2 = module(mk2, ms2, mv2, qk2, qe2)
            diff2 = np.abs(ref2.numpy().astype(np.float32) - ort2[0].astype(np.float32))
            dynamic_parity = {
                "shape": list(ort2[0].shape),
                "mae": float(diff2.mean()),
                "max_error": float(diff2.max()),
                "N": small_N,
                "HW": small_HW,
            }

            record["ort_inference"] = {
                "providers_used": sess.get_providers(),
                "session_inputs": required_inputs,
                "num_outputs": len(ort_outputs),
                "output_shapes": [list(o.shape) for o in ort_outputs],
                "output_dtypes": [str(o.dtype) for o in ort_outputs],
                "torch_parity": parity,
                "dynamic_shape_parity": dynamic_parity,
            }
        except Exception as exc:
            record["ort_inference"] = {"error": f"{type(exc).__name__}: {exc}"}

    except Exception as exc:
        record.update({"status": "failed", "error": f"{type(exc).__name__}: {exc}",
                        "traceback": traceback.format_exc().splitlines()[-12:]})
    return record


def main() -> int:
    parser = argparse.ArgumentParser(description="Export MatAnyone2 active submodules to ONNX")
    parser.add_argument("--baseline-manifest", type=Path, default=DEFAULT_BASELINE)
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT)
    parser.add_argument("--device", default="cuda")
    parser.add_argument("--targets", default=",".join(DEFAULT_TARGETS))
    parser.add_argument("--max-trace-frames", type=int, default=2)
    parser.add_argument("--warmup-repeats", type=int, default=2)
    parser.add_argument("--opset", type=int, default=17)
    args = parser.parse_args()

    targets = [t.strip() for t in args.targets.split(",") if t.strip()]
    report: dict[str, Any] = {
        "schema": SCHEMA,
        "timestamp": datetime.now(timezone.utc).isoformat(),
        "device_requested": args.device,
        "targets": targets,
        "exports": {},
    }

    try:
        from matanyone2.model.matanyone2 import MatAnyone2
        from matanyone2.inference.inference_core import InferenceCore
        device = torch.device(args.device if args.device == "cuda" and torch.cuda.is_available() else "cpu")
        report["device"] = str(device)
        model = MatAnyone2.from_pretrained(str(find_model_dir())).to(device).eval()
        captured: dict[str, dict[str, Any]] = {}

        def make_hook(name: str):
            def hook(module, hook_args, hook_kwargs):
                if name not in captured:
                    captured[name] = {
                        "args": clone_cpu(hook_args),
                        "kwargs": clone_cpu(hook_kwargs),
                        "summary": {"args": tensor_summary(hook_args), "kwargs": tensor_summary(hook_kwargs)},
                    }
            return hook

        handles = []
        original_read_first_frame_memory = model.read_first_frame_memory
        for name in targets:
            if name == "read_first_frame_memory":
                continue
            if name == "memory_readout":
                continue  # standalone functional module, not a model attribute
            module = getattr(model, name, None)
            if module is not None:
                handles.append(module.register_forward_pre_hook(make_hook(name), with_kwargs=True))

        def read_first_frame_memory_capture(*call_args, **call_kwargs):
            if "read_first_frame_memory" not in captured:
                captured["read_first_frame_memory"] = {
                    "args": clone_cpu(call_args),
                    "kwargs": clone_cpu(call_kwargs),
                    "summary": {"args": tensor_summary(call_args), "kwargs": tensor_summary(call_kwargs)},
                }
            return original_read_first_frame_memory(*call_args, **call_kwargs)

        if "read_first_frame_memory" in targets:
            model.read_first_frame_memory = read_first_frame_memory_capture

        processor = InferenceCore(model, cfg=model.cfg, device=device)
        all_tensors, mask_tensor, frame_paths, mask_path = load_inputs(args.baseline_manifest, args.max_trace_frames, args.warmup_repeats, device)
        report["frames_used"] = len(all_tensors)
        report["mask_path_used"] = mask_path
        with torch.inference_mode():
            for ti, frame_tensor in enumerate(all_tensors):
                image = (frame_tensor / 255.0).float().to(device)
                if ti == 0:
                    processor.step(image, mask_tensor, objects=[1])
                    processor.step(image, first_frame_pred=True)
                elif ti < args.warmup_repeats:
                    processor.step(image, first_frame_pred=True)
                else:
                    processor.step(image)
                if all(t in captured for t in targets if getattr(model, t, None) is not None):
                    break
        for h in handles:
            h.remove()
        model.read_first_frame_memory = original_read_first_frame_memory

        report["captured_targets"] = sorted(captured)
        # Export sequentially on CPU to limit CUDA pressure.
        for name in targets:
            if name == "memory_readout":
                # Standalone functional module – no model capture needed
                report["exports"][name] = export_memory_readout(args.out, args.opset, top_k=30)
                continue
            module = model if name == "read_first_frame_memory" else getattr(model, name, None)
            if module is None:
                report["exports"][name] = {"status": "missing_module"}
            elif name not in captured:
                report["exports"][name] = {"status": "not_captured"}
            else:
                report["exports"][name] = export_target(module, name, captured[name], args.out, args.opset)
        report["status"] = "ok" if any(v.get("status") == "ok" for v in report["exports"].values()) else "failed"
    except Exception as exc:
        report["status"] = "error"
        report["error"] = f"{type(exc).__name__}: {exc}"
        report["traceback"] = traceback.format_exc().splitlines()[-20:]
    finally:
        if torch.cuda.is_available():
            torch.cuda.empty_cache()

    args.out.mkdir(parents=True, exist_ok=True)
    report_path = args.out / "matanyone2_submodule_export_report.json"
    report_path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps({"status": report.get("status"), "report": str(report_path), "exports": {k: v.get("status") for k, v in report.get("exports", {}).items()}}, indent=2, sort_keys=True))
    return 0 if report.get("status") in {"ok", "partial"} else 1


if __name__ == "__main__":
    raise SystemExit(main())
