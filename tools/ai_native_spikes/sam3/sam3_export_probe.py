#!/usr/bin/env python3
"""SAM3 native stack probe: single-frame guide-mask export scaffolding.

Introspects the SAM3 Transformers runtime to discover model classes, signatures,
processor methods, and export blockers. Supports point/box guide-mask prompting
route analysis without running GPU-heavy conversions.

Routes:
  introspect  — discover classes, signatures, versions (default)
  torchscript — reserved (records not_implemented_pending_introspection)
  torch-export — reserved (records not_implemented_pending_introspection)
  onnx        — attempt actual ONNX export of Sam3TrackerModel (point/box)
  all         — run introspect + onnx
"""
from __future__ import annotations

import argparse
import inspect
import json
import subprocess
import sys
import time
import traceback
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Dict, List, Optional

REPO_ROOT = Path(__file__).resolve().parents[3]
RUNTIME_DISCOVERY = REPO_ROOT / "tools" / "ai" / "flux_provider_runtime.py"
DEFAULT_IMAGE = REPO_ROOT / "tools" / "ai" / "proof_assets" / "sam31_probe_source.ppm"
DEFAULT_BASELINE = REPO_ROOT / "tools" / "ai_native_spikes" / "results" / "sam3" / "baseline_manifest.json"
DEFAULT_OUT = REPO_ROOT / "tools" / "ai_native_spikes" / "results" / "sam3" / "export"

MODEL_ID = "sam3_transformers"

# ---------------------------------------------------------------------------
# Runtime discovery
# ---------------------------------------------------------------------------


def discover_runtime_python(runtime_id: str) -> Path:
    """Discover provider runtime python via flux_provider_runtime.py."""
    result = subprocess.run(
        [sys.executable, str(RUNTIME_DISCOVERY), "python", runtime_id],
        cwd=str(REPO_ROOT),
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    py_path = result.stdout.strip()
    if not py_path:
        raise RuntimeError(
            f"flux_provider_runtime.py python {runtime_id} returned empty path. "
            f"stderr: {result.stderr.strip()}"
        )
    py = Path(py_path)
    if not py.exists():
        raise RuntimeError(f"Provider runtime python does not exist: {py}")
    if not py.stat().st_mode & 0o111:
        raise RuntimeError(f"Provider runtime python is not executable: {py}")
    return py


# ---------------------------------------------------------------------------
# Child introspection script (runs under provider runtime python)
# ---------------------------------------------------------------------------

INTROSPECT_SCRIPT = r'''
import importlib
import inspect
import json
import sys
import traceback
from pathlib import Path
from typing import Any, Dict

MODEL_ID = "sam3_transformers"

def import_version(name):
    out = {"module": name, "available": False, "version": None, "error": None}
    try:
        importlib.import_module(name)
        out["available"] = True
        try:
            import importlib.metadata as md
            out["version"] = md.version(name)
        except Exception:
            out["version"] = "unknown"
    except Exception as exc:
        out["error"] = f"{type(exc).__name__}: {exc}"
    return out


def cuda_info():
    info = {"available": None, "device_count": None, "devices": [], "error": None}
    try:
        import torch
        info["available"] = bool(torch.cuda.is_available())
        info["device_count"] = int(torch.cuda.device_count()) if info["available"] else 0
        if info["available"]:
            info["devices"] = [torch.cuda.get_device_name(i) for i in range(info["device_count"])]
    except Exception as exc:
        info["error"] = f"{type(exc).__name__}: {exc}"
    return info


def load_flux_model():
    # Add tools/ai to path so flux_model_manager is importable
    repo_root = Path(__file__).resolve().parents[3] if "__file__" in dir() else Path(".")
    sys.path.insert(0, str(repo_root / "tools" / "ai"))
    from flux_model_manager import (
        default_manifest_path,
        find_model,
        flux_paths,
        is_installed,
        load_manifest,
        model_local_path,
    )
    manifest = load_manifest(default_manifest_path())
    model = find_model(manifest, MODEL_ID)
    local_path = model_local_path(model, flux_paths())
    return model, local_path, is_installed(model, flux_paths())


def inspect_class(cls):
    result = {"name": cls.__name__, "module": getattr(cls, "__module__", None)}
    try:
        init_sig = inspect.signature(cls.__init__)
        result["init_signature"] = str(init_sig)
        result["init_params"] = list(init_sig.parameters)
    except Exception as exc:
        result["init_signature_error"] = f"{type(exc).__name__}: {exc}"
    forward = getattr(cls, "forward", None) or getattr(cls, "__call__", None)
    if forward:
        try:
            fwd_sig = inspect.signature(forward)
            result["forward_signature"] = str(fwd_sig)
            result["forward_params"] = list(fwd_sig.parameters)
        except Exception as exc:
            result["forward_signature_error"] = f"{type(exc).__name__}: {exc}"
    return result


def inspect_processor_methods(proc):
    methods = {}
    for name in sorted(dir(proc)):
        if "process" in name.lower():
            obj = getattr(proc, name, None)
            if callable(obj):
                try:
                    sig = inspect.signature(obj)
                    methods[name] = {"signature": str(sig), "params": list(sig.parameters)}
                except Exception as exc:
                    methods[name] = {"error": f"{type(exc).__name__}: {exc}"}
    return methods


def main():
    report = {"status": "ok", "errors": []}

    # Versions
    deps = {name: import_version(name) for name in ("torch", "transformers", "huggingface_hub")}
    report["dependencies"] = deps
    report["cuda"] = cuda_info()
    report["python"] = sys.executable

    # Model manifest info
    try:
        model_meta, local_path, installed = load_flux_model()
        source = model_meta.get("source", {}) if isinstance(model_meta, dict) else {}
        report["model"] = {
            "id": model_meta.get("id"),
            "repo": source.get("repo"),
            "revision": source.get("revision"),
            "local_path": str(local_path),
            "installed": installed,
            "is_dir": local_path.is_dir() if installed else False,
        }
    except Exception as exc:
        report["model"] = {"error": f"{type(exc).__name__}: {exc}"}
        report["errors"].append(f"model lookup failed: {exc}")
        local_path = None
        installed = False

    # Discover classes in transformers
    try:
        import transformers
        candidate_names = [
            "Sam3Model", "Sam3Processor", "Sam3TrackerModel", "Sam3TrackerProcessor",
            "Sam3TrackerVideoModel", "Sam3TrackerVideoProcessor",
            "Sam3ImageProcessor", "Sam3Config",
        ]
        discovered = {}
        for name in candidate_names:
            cls = getattr(transformers, name, None)
            if cls is not None:
                discovered[name] = inspect_class(cls)
            else:
                discovered[name] = {"available": False}
        # Also scan for any Sam3-related names
        all_sam3 = [n for n in dir(transformers) if "sam3" in n.lower() or "Sam3" in n]
        report["transformers_classes"] = discovered
        report["transformers_all_sam3_names"] = sorted(all_sam3)
        report["transformers_version"] = getattr(transformers, "__version__", "unknown")
    except Exception as exc:
        report["transformers_classes"] = {"error": f"{type(exc).__name__}: {exc}"}
        report["errors"].append(f"transformers scan failed: {exc}")

    # Load concept model/processor if model is installed
    concept_load = {"attempted": False, "success": False}
    if installed and local_path and local_path.is_dir():
        concept_load["attempted"] = True
        try:
            from transformers import Sam3Processor, Sam3Model
            proc = Sam3Processor.from_pretrained(str(local_path), local_files_only=True)
            model = Sam3Model.from_pretrained(str(local_path), local_files_only=True)
            concept_load["success"] = True
            concept_load["processor_class"] = type(proc).__name__
            concept_load["model_class"] = type(model).__name__
            # Module counts
            module_count = sum(1 for _ in model.modules())
            concept_load["module_count"] = module_count
            # Top-level children
            top_children = {}
            for cname, child in model.named_children():
                top_children[cname] = type(child).__name__
            concept_load["top_level_children"] = top_children
            # Processor call signature
            try:
                call_sig = inspect.signature(proc.__call__)
                concept_load["processor_call_signature"] = str(call_sig)
                concept_load["processor_call_params"] = list(call_sig.parameters)
            except Exception as exc:
                concept_load["processor_call_signature_error"] = f"{type(exc).__name__}: {exc}"
            # Forward signature of loaded model
            try:
                fwd_sig = inspect.signature(model.forward)
                concept_load["model_forward_signature"] = str(fwd_sig)
                concept_load["model_forward_params"] = list(fwd_sig.parameters)
            except Exception as exc:
                concept_load["model_forward_signature_error"] = f"{type(exc).__name__}: {exc}"
            # Processor methods containing 'process'
            concept_load["processor_process_methods"] = inspect_processor_methods(proc)
        except Exception as exc:
            concept_load["error"] = f"{type(exc).__name__}: {exc}"
            concept_load["traceback"] = traceback.format_exc().splitlines()[-8:]
            report["errors"].append(f"concept model load failed: {exc}")
    report["concept_load"] = concept_load

    # Load tracker model/processor
    tracker_load = {"attempted": False, "success": False}
    if installed and local_path and local_path.is_dir():
        tracker_load["attempted"] = True
        try:
            from transformers import Sam3TrackerProcessor, Sam3TrackerModel
            tproc = Sam3TrackerProcessor.from_pretrained(str(local_path), local_files_only=True)
            tmodel = Sam3TrackerModel.from_pretrained(str(local_path), local_files_only=True)
            tracker_load["success"] = True
            tracker_load["processor_class"] = type(tproc).__name__
            tracker_load["model_class"] = type(tmodel).__name__
            module_count = sum(1 for _ in tmodel.modules())
            tracker_load["module_count"] = module_count
            top_children = {}
            for cname, child in tmodel.named_children():
                top_children[cname] = type(child).__name__
            tracker_load["top_level_children"] = top_children
            # Tracker processor call signature
            try:
                call_sig = inspect.signature(tproc.__call__)
                tracker_load["processor_call_signature"] = str(call_sig)
                tracker_load["processor_call_params"] = list(call_sig.parameters)
            except Exception as exc:
                tracker_load["processor_call_signature_error"] = f"{type(exc).__name__}: {exc}"
            # Tracker forward signature
            try:
                fwd_sig = inspect.signature(tmodel.forward)
                tracker_load["model_forward_signature"] = str(fwd_sig)
                tracker_load["model_forward_params"] = list(fwd_sig.parameters)
            except Exception as exc:
                tracker_load["model_forward_signature_error"] = f"{type(exc).__name__}: {exc}"
            # Tracker processor methods containing 'process'
            tracker_load["processor_process_methods"] = inspect_processor_methods(tproc)
        except Exception as exc:
            tracker_load["error"] = f"{type(exc).__name__}: {exc}"
            tracker_load["traceback"] = traceback.format_exc().splitlines()[-8:]
            report["errors"].append(f"tracker model load failed: {exc}")
    report["tracker_load"] = tracker_load

    if report["errors"]:
        report["status"] = "partial"

    print(json.dumps(report, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
'''


# ---------------------------------------------------------------------------
# Route handlers
# ---------------------------------------------------------------------------


def run_introspect(
    runtime_python: Path,
    device: str,
    prompt_kind: str,
    baseline_manifest_path: Path,
    out_dir: Path,
) -> Dict[str, Any]:
    """Run introspection route: discover classes, signatures, versions."""
    report: Dict[str, Any] = {
        "schema": "org.flux.ai.sam3-export-probe.introspect.v1",
        "timestamp": datetime.now(timezone.utc).isoformat(),
        "route": "introspect",
        "runtime_python": str(runtime_python),
        "device": device,
        "prompt_kind": prompt_kind,
        "status": "ok",
        "errors": [],
    }

    # Load baseline manifest if available
    baseline: Optional[Dict[str, Any]] = None
    if baseline_manifest_path.is_file():
        try:
            baseline = json.loads(baseline_manifest_path.read_text(encoding="utf-8"))
            report["baseline_manifest"] = {
                "path": str(baseline_manifest_path),
                "overall_status": baseline.get("overall_status"),
                "runtime_python": baseline.get("runtime_python"),
                "device": baseline.get("device"),
            }
        except Exception as exc:
            report["baseline_manifest"] = {"path": str(baseline_manifest_path), "error": str(exc)}
    else:
        report["baseline_manifest"] = {"path": str(baseline_manifest_path), "available": False}

    # Run child introspection script
    report["introspection"] = {"attempted": True, "success": False}
    t0 = time.monotonic()
    try:
        proc = subprocess.run(
            [str(runtime_python), "-c", INTROSPECT_SCRIPT],
            cwd=str(REPO_ROOT),
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=300,
            check=False,
        )
        elapsed = time.monotonic() - t0
        report["introspection"]["elapsed_seconds"] = round(elapsed, 3)
        report["introspection"]["returncode"] = proc.returncode

        if proc.returncode != 0:
            report["introspection"]["error"] = f"child returned {proc.returncode}"
            report["introspection"]["stderr_preview"] = (proc.stderr or "")[:3000]
            report["errors"].append(f"introspection child failed with rc={proc.returncode}")
        else:
            try:
                child_report = json.loads(proc.stdout)
                report["introspection"]["success"] = True
                report["introspection"]["child_report"] = child_report
                # Propagate child errors
                for err in child_report.get("errors", []):
                    report["errors"].append(f"child: {err}")
            except json.JSONDecodeError as exc:
                report["introspection"]["error"] = f"JSON parse failed: {exc}"
                report["introspection"]["stdout_preview"] = (proc.stdout or "")[:2000]
                report["errors"].append(f"introspection output not valid JSON: {exc}")
    except subprocess.TimeoutExpired:
        report["introspection"]["error"] = "child timed out after 300s"
        report["errors"].append("introspection child timed out")
    except Exception as exc:
        report["introspection"]["error"] = f"{type(exc).__name__}: {exc}"
        report["errors"].append(f"introspection invocation failed: {exc}")

    # Build guide-mask route summary
    report["guide_mask_route_summary"] = _build_route_summary(report, prompt_kind)

    if report["errors"]:
        report["status"] = "partial" if report["introspection"].get("success") else "failed"

    return report


def _build_route_summary(report: Dict[str, Any], prompt_kind: str) -> Dict[str, Any]:
    """Derive point/box guide-mask route summary from introspection data."""
    summary: Dict[str, Any] = {
        "prompt_kind": prompt_kind,
        "tracker_model_for_point": None,
        "tracker_model_for_box": None,
        "export_blockers": [],
    }
    child = report.get("introspection", {}).get("child_report", {})
    if not child:
        summary["export_blockers"].append("introspection child report unavailable")
        return summary

    # Check tracker load for point/box route
    tracker = child.get("tracker_load", {})
    if tracker.get("success"):
        tracker_info = {
            "model_class": tracker.get("model_class"),
            "processor_class": tracker.get("processor_class"),
            "module_count": tracker.get("module_count"),
            "forward_signature": tracker.get("model_forward_signature"),
            "processor_call_params": tracker.get("processor_call_params"),
        }
        summary["tracker_model_for_point"] = tracker_info
        summary["tracker_model_for_box"] = tracker_info
    else:
        summary["tracker_model_for_point"] = {"available": False, "error": tracker.get("error")}
        summary["tracker_model_for_box"] = {"available": False, "error": tracker.get("error")}
        summary["export_blockers"].append(
            f"tracker model unavailable: {tracker.get('error', 'unknown')}"
        )

    # Check concept model for context
    concept = child.get("concept_load", {})
    if concept.get("success"):
        summary["concept_model"] = {
            "model_class": concept.get("model_class"),
            "processor_class": concept.get("processor_class"),
            "module_count": concept.get("module_count"),
        }
    else:
        summary["concept_model"] = {"available": False, "error": concept.get("error")}

    # General export blockers
    model_info = child.get("model", {})
    if not model_info.get("installed"):
        summary["export_blockers"].append("SAM3 model not installed locally")

    deps = child.get("dependencies", {})
    torch_dep = deps.get("torch", {})
    if not torch_dep.get("available"):
        summary["export_blockers"].append("torch not available in provider runtime")
    transformers_dep = deps.get("transformers", {})
    if not transformers_dep.get("available"):
        summary["export_blockers"].append("transformers not available in provider runtime")

    # TorchScript / torch.export / ONNX blockers
    if tracker.get("success"):
        summary["export_blockers"].append(
            "TorchScript: tracker model has custom ops / trust_remote_code; "
            "forward signature must be traced to confirm static graph compatibility"
        )
        summary["export_blockers"].append(
            "torch.export: same concern as TorchScript plus dynamo tracing of transformers internals"
        )
        summary["export_blockers"].append(
            "ONNX: requires verifying opset coverage for all tracker submodules; "
            "post_process_masks uses dynamic control flow"
        )

    return summary


# ---------------------------------------------------------------------------
# ONNX export child script (runs under provider runtime python)
# ---------------------------------------------------------------------------

ONNX_EXPORT_SCRIPT = r'''
import importlib
import json
import os
import sys
import time
import traceback
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple

# ---- Configuration passed via environment or arguments ----
import argparse

parser = argparse.ArgumentParser()
parser.add_argument("--model-path", required=True)
parser.add_argument("--image-path", required=True)
parser.add_argument("--prompt-kind", choices=["point", "box"], default="point")
parser.add_argument("--device", default="cuda")
parser.add_argument("--out-dir", required=True)
parser.add_argument("--opset", type=int, default=17)
parser.add_argument("--image-width", type=int, default=640)
parser.add_argument("--image-height", type=int, default=480)
args = parser.parse_args()

report: Dict[str, Any] = {
    "status": "running",
    "errors": [],
    "steps": {},
}


def step(name: str):
    """Record timing for a step."""
    report["steps"][name] = {"status": "running", "t0": time.monotonic()}


def step_done(name: str, info: Optional[Dict] = None):
    t0 = report["steps"][name]["t0"]
    elapsed = time.monotonic() - t0
    report["steps"][name]["status"] = "done"
    report["steps"][name]["elapsed_seconds"] = round(elapsed, 3)
    if info:
        report["steps"][name].update(info)


def step_fail(name: str, error: str, tb_lines: Optional[List[str]] = None):
    t0 = report["steps"][name].get("t0", time.monotonic())
    elapsed = time.monotonic() - t0
    report["steps"][name]["status"] = "failed"
    report["steps"][name]["elapsed_seconds"] = round(elapsed, 3)
    report["steps"][name]["error"] = error
    if tb_lines:
        report["steps"][name]["traceback"] = tb_lines
    report["errors"].append(f"{name}: {error}")


# ---- Step 1: Load model and processor ----
step("load_model")
try:
    import torch
    from transformers import Sam3TrackerModel, Sam3TrackerProcessor

    model_path = args.model_path
    device = torch.device(args.device if torch.cuda.is_available() else "cpu")

    processor = Sam3TrackerProcessor.from_pretrained(model_path, local_files_only=True)
    model = Sam3TrackerModel.from_pretrained(model_path, local_files_only=True)
    model.eval()
    model.to(device)

    step_done("load_model", {
        "model_class": type(model).__name__,
        "processor_class": type(processor).__name__,
        "device": str(device),
        "module_count": sum(1 for _ in model.modules()),
    })
except Exception as exc:
    step_fail("load_model", f"{type(exc).__name__}: {exc}", traceback.format_exc().splitlines()[-10:])
    report["status"] = "failed"
    print(json.dumps(report, indent=2))
    sys.exit(1)

# ---- Step 2: Load proof image and preprocess ----
step("preprocess")
try:
    from PIL import Image
    import numpy as np

    img = Image.open(args.image_path).convert("RGB")
    w, h = img.size
    report["image"] = {"path": args.image_path, "width": w, "height": h}

    if args.prompt_kind == "point":
        cx, cy = w // 2, h // 2
        # input_points: list of batch of points per image: [[[[x, y]]]]
        # input_labels: list of batch of labels per image: [[[1]]]
        inputs = processor(
            images=img,
            input_points=[[[[cx, cy]]]],
            input_labels=[[[1]]],
            return_tensors="pt",
        )
    else:  # box
        x1 = w // 4
        y1 = h // 4
        x2 = 3 * w // 4
        y2 = 3 * h // 4
        # Sam3TrackerProcessor emits input_boxes as (B, N, 4). The wrapper
        # must expose that tensor as the second positional arg for box export;
        # otherwise torch.onnx.export binds it to input_points.
        inputs = processor(
            images=img,
            input_boxes=[[[x1, y1, x2, y2]]],
            return_tensors="pt",
        )

    # Move tensors to device
    input_tensors = {k: v.to(device) for k, v in inputs.items()}
    input_shapes = {k: list(v.shape) for k, v in input_tensors.items()}
    input_dtypes = {k: str(v.dtype) for k, v in input_tensors.items()}

    step_done("preprocess", {
        "prompt_kind": args.prompt_kind,
        "input_keys": sorted(input_tensors.keys()),
        "input_shapes": input_shapes,
        "input_dtypes": input_dtypes,
    })
except Exception as exc:
    step_fail("preprocess", f"{type(exc).__name__}: {exc}", traceback.format_exc().splitlines()[-10:])
    report["status"] = "failed"
    print(json.dumps(report, indent=2))
    sys.exit(1)

# ---- Step 3: Dry-run forward to inspect output structure ----
step("dry_run_forward")
output_info = {}
try:
    with torch.no_grad():
        outputs = model(**input_tensors)

    # Inspect output object
    output_info["output_class"] = type(outputs).__name__
    if hasattr(outputs, "pred_masks"):
        output_info["has_pred_masks"] = True
        pm = outputs.pred_masks
        output_info["pred_masks_shape"] = list(pm.shape) if pm is not None else None
        output_info["pred_masks_dtype"] = str(pm.dtype) if pm is not None else None
    else:
        output_info["has_pred_masks"] = False

    # Try to get all attributes that look like tensors
    output_info["output_attributes"] = []
    for attr in dir(outputs):
        if attr.startswith("_"):
            continue
        val = getattr(outputs, attr, None)
        if isinstance(val, torch.Tensor):
            output_info["output_attributes"].append({
                "name": attr,
                "shape": list(val.shape),
                "dtype": str(val.dtype),
            })

    # Also check to_dict or items if available
    if hasattr(outputs, "to_tuple"):
        try:
            tup = outputs.to_tuple()
            output_info["tuple_length"] = len(tup)
            output_info["tuple_shapes"] = [
                list(t.shape) if isinstance(t, torch.Tensor) else str(type(t))
                for t in tup
            ]
        except Exception:
            pass
    if hasattr(outputs, "__getitem__"):
        try:
            # Try integer indexing
            first = outputs[0]
            output_info["index_0_shape"] = list(first.shape) if isinstance(first, torch.Tensor) else str(type(first))
        except Exception:
            pass

    step_done("dry_run_forward", output_info)
except Exception as exc:
    step_fail("dry_run_forward", f"{type(exc).__name__}: {exc}", traceback.format_exc().splitlines()[-10:])
    report["status"] = "failed"
    print(json.dumps(report, indent=2))
    sys.exit(1)

# ---- Step 4: Build wrapper module for ONNX export ----
step("build_wrapper")
try:
    # Determine which inputs to expose and output keys
    # For point: pixel_values, input_points, input_labels
    # For box: pixel_values, input_boxes
    if args.prompt_kind == "point":
        forward_input_names = ["pixel_values", "input_points", "input_labels"]
    else:
        forward_input_names = ["pixel_values", "input_boxes"]

    def collect_pred_masks(outputs):
        if hasattr(outputs, "pred_masks") and outputs.pred_masks is not None:
            return (outputs.pred_masks,)
        for attr in dir(outputs):
            if attr.startswith("_"):
                continue
            val = getattr(outputs, attr, None)
            if isinstance(val, torch.Tensor):
                return (val,)
        raise RuntimeError("No tensor output found from model forward")

    class Sam3TrackerPointONNXWrapper(torch.nn.Module):
        """Point wrapper with positional args matching exported inputs."""
        def __init__(self, tracker_model):
            super().__init__()
            self.tracker_model = tracker_model

        def forward(self, pixel_values, input_points, input_labels):
            outputs = self.tracker_model(
                pixel_values=pixel_values,
                input_points=input_points,
                input_labels=input_labels,
            )
            return collect_pred_masks(outputs)

    class Sam3TrackerBoxONNXWrapper(torch.nn.Module):
        """Box wrapper with input_boxes as the second positional arg."""
        def __init__(self, tracker_model):
            super().__init__()
            self.tracker_model = tracker_model

        def forward(self, pixel_values, input_boxes):
            outputs = self.tracker_model(
                pixel_values=pixel_values,
                input_boxes=input_boxes,
            )
            return collect_pred_masks(outputs)

    wrapper = Sam3TrackerPointONNXWrapper(model) if args.prompt_kind == "point" else Sam3TrackerBoxONNXWrapper(model)
    wrapper.eval()

    # Build sample inputs for the wrapper in the right order
    sample_args = []
    for name in forward_input_names:
        sample_args.append(input_tensors[name])

    step_done("build_wrapper", {
        "forward_input_names": forward_input_names,
        "sample_shapes": {n: list(input_tensors[n].shape) for n in forward_input_names},
    })
except Exception as exc:
    step_fail("build_wrapper", f"{type(exc).__name__}: {exc}", traceback.format_exc().splitlines()[-10:])
    report["status"] = "failed"
    print(json.dumps(report, indent=2))
    sys.exit(1)

# ---- Step 5: torch.onnx.export ----
step("onnx_export")
onnx_path = Path(args.out_dir) / f"sam3_tracker_{args.prompt_kind}.onnx"
onnx_path.parent.mkdir(parents=True, exist_ok=True)
exported_ok = False

# Move model + inputs to CPU for tracing (avoids CUDA/CPU device mismatch
# during repeat_interleave in mask_decoder tracing)
wrapper.cpu()
cpu_sample_args = [t.cpu() for t in sample_args]

# Try multiple export strategies
# Strategy 1: dynamo=False (legacy torchscript-based exporter, avoids onnxscript dep)
# Strategy 2: default (requires onnxscript)
strategies = [
    ("legacy_cpu_dynamo_false", {"dynamo": False}),
    ("default_cpu", {}),
]
last_strategy_name = None
for strategy_name, strategy_kwargs in strategies:
    if exported_ok:
        break
    last_strategy_name = strategy_name
    step(f"onnx_export_{strategy_name}")
    try:
        export_kwargs = dict(
            opset_version=args.opset,
            input_names=forward_input_names,
            output_names=["pred_masks"],
            do_constant_folding=True,
        )
        export_kwargs.update(strategy_kwargs)

        with torch.no_grad():
            torch.onnx.export(
                wrapper,
                tuple(cpu_sample_args),
                str(onnx_path),
                **export_kwargs,
            )

        file_size = onnx_path.stat().st_size if onnx_path.exists() else 0
        step_done(f"onnx_export_{strategy_name}", {
            "onnx_path": str(onnx_path),
            "file_size_bytes": file_size,
            "file_size_mb": round(file_size / (1024 * 1024), 2),
            "opset": args.opset,
            "dynamic_axes": False,
            "strategy": strategy_name,
            "device": "cpu",
            "success": True,
        })
        exported_ok = True
    except TypeError as te:
        # dynamo kwarg might not be supported in this torch version
        tb = traceback.format_exc().splitlines()[-10:]
        step_fail(f"onnx_export_{strategy_name}", f"{type(te).__name__}: {te}", tb)
        if strategy_name == strategies[0][0]:
            continue  # try next strategy
        else:
            break
    except Exception as exc:
        tb = traceback.format_exc().splitlines()[-15:]
        # Check for common unsupported-op errors
        err_str = str(exc)
        unsupported_op = None
        if "Unsupported" in err_str or "unsupported" in err_str.lower():
            unsupported_op = err_str
        if "onnx" in err_str.lower() and "op" in err_str.lower():
            unsupported_op = err_str
        step_fail(f"onnx_export_{strategy_name}", f"{type(exc).__name__}: {exc}", tb)
        if unsupported_op:
            report["steps"][f"onnx_export_{strategy_name}"]["unsupported_op_detail"] = unsupported_op
        # Check for onnxscript import error
        if "onnxscript" in err_str:
            report["steps"][f"onnx_export_{strategy_name}"]["note"] = (
                "onnxscript not installed in provider runtime env"
            )
        if strategy_name == strategies[0][0]:
            continue  # try next strategy
        else:
            break

if exported_ok:
    step_done("onnx_export", {"success": True, "strategy_used": last_strategy_name})
else:
    step_fail("onnx_export", "all export strategies failed")
    report["status"] = "export_failed"

# ---- Step 6: onnx.checker ----
step("onnx_checker")
if onnx_path.exists():
    try:
        import onnx
        model_proto = onnx.load(str(onnx_path))
        onnx.checker.check_model(model_proto)
        step_done("onnx_checker", {"result": "passed"})
    except ImportError:
        step_done("onnx_checker", {"result": "onnx_package_not_available"})
    except Exception as exc:
        step_fail("onnx_checker", f"{type(exc).__name__}: {exc}", traceback.format_exc().splitlines()[-5:])
else:
    step_done("onnx_checker", {"result": "skipped_no_onnx_file"})

# ---- Step 7: onnxruntime inference ----
step("ort_inference")
if onnx_path.exists():
    try:
        import onnxruntime as ort
        # Try CUDA first, fall back to CPU
        providers = []
        if "CUDAExecutionProvider" in ort.get_available_providers():
            providers.append("CUDAExecutionProvider")
        providers.append("CPUExecutionProvider")

        sess = ort.InferenceSession(str(onnx_path), providers=providers)

        # Build feed dict with numpy arrays
        feed = {}
        for name in forward_input_names:
            t = input_tensors[name].cpu()
            feed[name] = t.numpy()

        ort_outputs = sess.run(None, feed)
        ort_info = {
            "providers_used": sess.get_providers(),
            "num_outputs": len(ort_outputs),
            "output_shapes": [list(o.shape) for o in ort_outputs],
            "output_dtypes": [str(o.dtype) for o in ort_outputs],
        }
        try:
            import numpy as np
            from PIL import Image
            pred = torch.from_numpy(ort_outputs[0])
            processed = processor.post_process_masks(
                pred,
                input_tensors["original_sizes"].cpu(),
                mask_threshold=0.0,
                binarize=True,
            )[0]
            mask = processed
            while isinstance(mask, (list, tuple)):
                mask = mask[0]
            if isinstance(mask, torch.Tensor):
                mask_np = mask.detach().cpu().numpy()
            else:
                mask_np = np.asarray(mask)
            mask_np = np.squeeze(mask_np)
            if mask_np.ndim > 2:
                mask_np = mask_np[0]
            mask_img = (mask_np > 0).astype(np.uint8) * 255
            parity_path = Path(args.out_dir) / f"sam3_tracker_{args.prompt_kind}_ort_mask.png"
            Image.fromarray(mask_img, mode="L").resize((w, h), resample=Image.Resampling.NEAREST).save(parity_path)
            ort_info["postprocessed_mask_path"] = str(parity_path)
            ort_info["postprocessed_mask_nonzero"] = int((mask_img > 0).sum())
            baseline_path = Path(args.out_dir).parents[2] / args.prompt_kind / f"mask_{args.prompt_kind}.png"
            if baseline_path.is_file():
                ref = np.asarray(Image.open(baseline_path).convert("L"), dtype=np.float32) / 255.0
                cand = np.asarray(Image.open(parity_path).convert("L"), dtype=np.float32) / 255.0
                if ref.shape == cand.shape:
                    ref_bin = ref >= 0.5
                    cand_bin = cand >= 0.5
                    inter = np.logical_and(ref_bin, cand_bin).sum(dtype=np.float64)
                    union = np.logical_or(ref_bin, cand_bin).sum(dtype=np.float64)
                    diff = np.abs(ref - cand)
                    ort_info["baseline_parity"] = {
                        "reference": str(baseline_path),
                        "candidate": str(parity_path),
                        "shape": list(ref.shape),
                        "iou": float(inter / union) if union else 1.0,
                        "alpha_mae": float(diff.mean()),
                        "alpha_max_error": float(diff.max()),
                        "reference_nonzero": int(ref_bin.sum()),
                        "candidate_nonzero": int(cand_bin.sum()),
                    }
                else:
                    ort_info["baseline_parity"] = {"error": f"shape mismatch: reference {ref.shape}, candidate {cand.shape}"}
            else:
                ort_info["baseline_parity"] = {"result": "skipped_missing_baseline", "reference": str(baseline_path)}
        except Exception as parity_exc:
            ort_info["postprocess_or_parity_error"] = f"{type(parity_exc).__name__}: {parity_exc}"
        step_done("ort_inference", ort_info)
    except ImportError:
        step_done("ort_inference", {"result": "onnxruntime_not_available"})
    except Exception as exc:
        step_fail("ort_inference", f"{type(exc).__name__}: {exc}", traceback.format_exc().splitlines()[-10:])
else:
    step_done("ort_inference", {"result": "skipped_no_onnx_file"})

# ---- Finalize ----
if not report["errors"]:
    report["status"] = "ok"
elif onnx_path.exists():
    report["status"] = "partial"
else:
    report["status"] = "export_failed"

print(json.dumps(report, indent=2, sort_keys=True))
'''


def run_onnx_route(
    runtime_python: Path,
    device: str,
    prompt_kind: str,
    image_path: Path,
    out_dir: Path,
    opset: int = 17,
) -> Dict[str, Any]:
    """Run ONNX export route: attempt actual SAM3TrackerModel export."""
    import sys as _sys
    _tools_ai = str(REPO_ROOT / "tools" / "ai")
    if _tools_ai not in _sys.path:
        _sys.path.insert(0, _tools_ai)
    from flux_model_manager import (
        default_manifest_path,
        find_model,
        flux_paths,
        is_installed,
        load_manifest,
        model_local_path,
    )

    report: Dict[str, Any] = {
        "schema": f"org.flux.ai.sam3-export-probe.onnx.v1",
        "timestamp": datetime.now(timezone.utc).isoformat(),
        "route": "onnx",
        "runtime_python": str(runtime_python),
        "device": device,
        "prompt_kind": prompt_kind,
        "opset": opset,
        "status": "running",
        "errors": [],
    }

    # Discover model path from manifest
    try:
        manifest = load_manifest(default_manifest_path())
        model = find_model(manifest, MODEL_ID)
        local_path = model_local_path(model, flux_paths())
        installed = is_installed(model, flux_paths())
    except Exception as exc:
        report["status"] = "failed"
        report["errors"].append(f"manifest lookup failed: {exc}")
        return report

    if not installed or not local_path.is_dir():
        report["status"] = "failed"
        report["errors"].append(f"model not installed or path not a directory: {local_path}")
        return report

    report["model"] = {"local_path": str(local_path), "installed": True}

    # Determine output subdirectory
    onnx_out = out_dir / "onnx" / prompt_kind
    onnx_out.mkdir(parents=True, exist_ok=True)

    # Verify proof image
    if not image_path.exists():
        report["status"] = "failed"
        report["errors"].append(f"proof image not found: {image_path}")
        return report

    # Get image dimensions from baseline if available, otherwise default
    img_w, img_h = 640, 480

    # Run the ONNX export child script under provider runtime python
    report["export_attempt"] = {"attempted": True, "success": False}
    t0 = time.monotonic()
    try:
        cmd = [
            str(runtime_python), "-c", ONNX_EXPORT_SCRIPT,
            "--model-path", str(local_path),
            "--image-path", str(image_path),
            "--prompt-kind", prompt_kind,
            "--device", device,
            "--out-dir", str(onnx_out),
            "--opset", str(opset),
            "--image-width", str(img_w),
            "--image-height", str(img_h),
        ]
        proc = subprocess.run(
            cmd,
            cwd=str(REPO_ROOT),
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=600,  # 10 min max
            check=False,
        )
        elapsed = time.monotonic() - t0
        report["export_attempt"]["elapsed_seconds"] = round(elapsed, 3)
        report["export_attempt"]["returncode"] = proc.returncode

        def parse_child_stdout(stdout: str) -> Dict[str, Any]:
            # torch.onnx may print progress lines before the JSON report. Extract
            # the outermost JSON object from stdout instead of requiring the
            # whole stream to be JSON.
            start = stdout.find("{")
            end = stdout.rfind("}")
            if start >= 0 and end >= start:
                return json.loads(stdout[start:end + 1])
            return json.loads(stdout)

        if proc.returncode != 0:
            report["export_attempt"]["error"] = f"child returned {proc.returncode}"
            report["export_attempt"]["stderr_preview"] = (proc.stderr or "")[:5000]
            report["errors"].append(f"ONNX export child failed with rc={proc.returncode}")
            # Try to parse stdout anyway for partial results
            try:
                child_report = parse_child_stdout(proc.stdout or "")
                report["export_attempt"]["child_report"] = child_report
                report["export_attempt"]["success"] = child_report.get("status") != "failed"
                for err in child_report.get("errors", []):
                    report["errors"].append(f"child: {err}")
            except (json.JSONDecodeError, TypeError):
                report["export_attempt"]["stdout_preview"] = (proc.stdout or "")[:3000]
        else:
            try:
                child_report = parse_child_stdout(proc.stdout or "")
                report["export_attempt"]["child_report"] = child_report
                report["export_attempt"]["success"] = child_report.get("status") not in {"failed", "export_failed"}
                for err in child_report.get("errors", []):
                    report["errors"].append(f"child: {err}")
            except json.JSONDecodeError as exc:
                report["export_attempt"]["error"] = f"JSON parse failed: {exc}"
                report["export_attempt"]["stdout_preview"] = (proc.stdout or "")[:3000]
                report["errors"].append(f"export output not valid JSON: {exc}")

    except subprocess.TimeoutExpired:
        report["export_attempt"]["error"] = "child timed out after 600s"
        report["errors"].append("ONNX export child timed out")
    except Exception as exc:
        report["export_attempt"]["error"] = f"{type(exc).__name__}: {exc}"
        report["errors"].append(f"ONNX export invocation failed: {exc}")

    # Check for output artifacts
    onnx_file = onnx_out / f"sam3_tracker_{prompt_kind}.onnx"
    if onnx_file.exists():
        report["onnx_artifact"] = {
            "path": str(onnx_file),
            "size_bytes": onnx_file.stat().st_size,
            "size_mb": round(onnx_file.stat().st_size / (1024 * 1024), 2),
        }

    # Derive final status
    if not report["errors"]:
        report["status"] = "ok"
    elif onnx_file.exists():
        report["status"] = "partial"
    else:
        report["status"] = "export_failed"

    return report


def run_reserved_route(
    route: str,
    runtime_python: Path,
    device: str,
    prompt_kind: str,
    out_dir: Path,
) -> Dict[str, Any]:
    """Handle torchscript/torch-export routes — not implemented in this pass."""
    return {
        "schema": f"org.flux.ai.sam3-export-probe.{route}.v1",
        "timestamp": datetime.now(timezone.utc).isoformat(),
        "route": route,
        "runtime_python": str(runtime_python),
        "device": device,
        "prompt_kind": prompt_kind,
        "status": "not_implemented_pending_introspection",
        "note": (
            f"{route} export route requires completed introspection and approved "
            "strategy before attempting model conversion. "
            "Run --route introspect first."
        ),
    }


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------


def main(argv: Optional[List[str]] = None) -> int:
    parser = argparse.ArgumentParser(
        description="SAM3 native stack export probe (single-frame guide-mask)"
    )
    parser.add_argument(
        "--runtime-id", default="sam3",
        help="Provider runtime ID (default: sam3)",
    )
    parser.add_argument(
        "--device", default="cuda",
        help="Device (default: cuda)",
    )
    parser.add_argument(
        "--image", default=str(DEFAULT_IMAGE),
        help="Input image path",
    )
    parser.add_argument(
        "--baseline-manifest", default=str(DEFAULT_BASELINE),
        help="Path to baseline manifest JSON",
    )
    parser.add_argument(
        "--out", default=str(DEFAULT_OUT),
        help="Output directory",
    )
    parser.add_argument(
        "--route",
        choices=["introspect", "torchscript", "torch-export", "onnx", "all"],
        default="introspect",
        help="Export route to probe (default: introspect)",
    )
    parser.add_argument(
        "--prompt-kind",
        choices=["point", "box"],
        default="point",
        help="Guide-mask prompt kind (default: point)",
    )
    args = parser.parse_args(argv)

    # Discover runtime python
    print(f"Discovering runtime python for '{args.runtime_id}'...")
    try:
        runtime_python = discover_runtime_python(args.runtime_id)
    except RuntimeError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2
    print(f"Runtime python: {runtime_python}")

    out_dir = Path(args.out).resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    baseline_path = Path(args.baseline_manifest).resolve()

    routes: List[str]
    if args.route == "all":
        routes = ["introspect", "onnx"]
    else:
        routes = [args.route]

    all_reports: List[Dict[str, Any]] = []
    critical_errors: List[str] = []

    for route in routes:
        print(f"\nRunning route: {route}")
        t0 = time.monotonic()
        try:
            if route == "introspect":
                report = run_introspect(
                    runtime_python=runtime_python,
                    device=args.device,
                    prompt_kind=args.prompt_kind,
                    baseline_manifest_path=baseline_path,
                    out_dir=out_dir,
                )
            elif route == "onnx":
                report = run_onnx_route(
                    runtime_python=runtime_python,
                    device=args.device,
                    prompt_kind=args.prompt_kind,
                    image_path=Path(args.image).resolve(),
                    out_dir=out_dir,
                )
            else:
                report = run_reserved_route(
                    route=route,
                    runtime_python=runtime_python,
                    device=args.device,
                    prompt_kind=args.prompt_kind,
                    out_dir=out_dir,
                )
        except Exception as exc:
            report = {
                "route": route,
                "status": "error",
                "error": f"{type(exc).__name__}: {exc}",
                "traceback": traceback.format_exc().splitlines()[-5:],
            }
            critical_errors.append(f"{route}: {exc}")

        elapsed = time.monotonic() - t0
        report["elapsed_seconds"] = round(elapsed, 3)
        all_reports.append(report)

        # Write per-route log. Include prompt kind for ONNX so point/box runs
        # do not overwrite each other.
        route_suffix = f"{route}_{args.prompt_kind}" if route == "onnx" else route
        route_log = out_dir / f"sam3_export_{route_suffix}_log.json"
        route_log.write_text(
            json.dumps(report, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        print(f"  status={report.get('status')}, elapsed={elapsed:.1f}s -> {route_log}")

    # Write aggregate report
    aggregate: Dict[str, Any] = {
        "schema": "org.flux.ai.sam3-export-probe.v1",
        "timestamp": datetime.now(timezone.utc).isoformat(),
        "runtime_id": args.runtime_id,
        "runtime_python": str(runtime_python),
        "device": args.device,
        "image": args.image,
        "baseline_manifest": args.baseline_manifest,
        "prompt_kind": args.prompt_kind,
        "routes_requested": routes,
        "reports": all_reports,
        "overall_status": "ok",
        "critical_errors": critical_errors if critical_errors else None,
    }
    if critical_errors:
        aggregate["overall_status"] = "failed"
    elif any(r.get("status") == "partial" for r in all_reports):
        aggregate["overall_status"] = "partial"
    elif any(r.get("status") == "not_implemented_pending_introspection" for r in all_reports):
        aggregate["overall_status"] = "not_implemented"

    report_path = out_dir / "sam3_export_report.json"
    report_path.write_text(
        json.dumps(aggregate, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(f"\nAggregate report: {report_path}")
    print(f"Overall status: {aggregate['overall_status']}")

    return 1 if critical_errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
