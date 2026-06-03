#!/usr/bin/env python3
"""MatAnyone2 native runtime/export probe scaffolding.

Introspects the MatAnyone2 model, InferenceCore, and temporal state to
determine export feasibility for the tech-stack shootout. Does NOT perform
GPU-heavy conversion runs by default.

Routes:
  introspect   — lightweight import, class, signature, and blocker analysis
  decompose    — trace InferenceCore.step() calls during a small baseline-like run
  torchscript  — placeholder; records not_implemented_pending_introspection
  torch-export — placeholder; records not_implemented_pending_introspection
  onnx         — placeholder; records not_implemented_pending_introspection
  all          — runs introspect + decompose; others are placeholders
"""
from __future__ import annotations

import argparse
import inspect
import json
import os
import subprocess
import sys
import time
import traceback
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Dict, List, Optional

REPO_ROOT = Path(__file__).resolve().parents[3]
RUNTIME_DISCOVERY = REPO_ROOT / "tools" / "ai" / "flux_provider_runtime.py"
MODEL_SEARCH_BASE = Path.home() / ".local/share/Flux/models/matanyone2"

SCHEMA = "flux.ai_native_spikes.matanyone2_export_probe.v1"


# ---------------------------------------------------------------------------
# Discovery helpers
# ---------------------------------------------------------------------------

def discover_runtime_python(runtime_id: str) -> Path:
    """Discover provider-runtime python via flux_provider_runtime.py."""
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
            f"flux_provider_runtime.py python {runtime_id} returned empty. "
            f"stderr: {result.stderr.strip()}"
        )
    py = Path(py_path)
    if not py.exists():
        raise RuntimeError(f"Provider runtime python not found: {py}")
    return py


def preflight_executable(py: Path) -> Dict[str, Any]:
    """Verify the discovered python is executable and return version info."""
    if not os.access(str(py), os.X_OK):
        return {"executable": False, "error": f"{py} is not executable"}
    proc = subprocess.run(
        [str(py), "-c", "import sys,json; print(json.dumps(list(sys.version_info[:3])))"],
        text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False,
    )
    if proc.returncode != 0:
        return {"executable": True, "version_error": proc.stderr.strip()}
    ver = tuple(json.loads(proc.stdout.strip()))
    return {"executable": True, "version": list(ver)}


# ---------------------------------------------------------------------------
# Child-script runner
# ---------------------------------------------------------------------------

def run_child_introspect(py: Path, baseline_manifest: Optional[Path]) -> Dict[str, Any]:
    """Run a child Python snippet under the provider runtime that introspects
    MatAnyone2 packages and model classes. Returns parsed JSON dict."""

    # Build the baseline manifest path string for the child script
    bl_path_str = str(baseline_manifest) if baseline_manifest else ""

    child_code = r'''
import importlib
import inspect
import json
import os
import sys
import traceback
from pathlib import Path

result = {"schema": "introspect_child.v1"}

# ---- 1. Runtime python / torch / cuda info ----
result["runtime_python"] = sys.executable
try:
    import torch
    result["torch_version"] = getattr(torch, "__version__", None)
    result["cuda_available"] = torch.cuda.is_available()
    if torch.cuda.is_available():
        result["cuda_device_count"] = torch.cuda.device_count()
        result["cuda_device_name"] = torch.cuda.get_device_name(0)
        result["cuda_version"] = torch.version.cuda
    else:
        result["cuda_device_count"] = 0
except Exception as exc:
    result["torch_import_error"] = f"{type(exc).__name__}: {exc}"
    result["torch_version"] = None
    result["cuda_available"] = False

# ---- 2. Baseline manifest summary ----
bl_path = """BL_BASELINE_PATH_PLACEHOLDER"""
if bl_path and Path(bl_path).is_file():
    try:
        with open(bl_path) as f:
            bl = json.load(f)
        inp = bl.get("inputs", {})
        vi = inp.get("video", {})
        mi = inp.get("mask", {})
        alpha_count = bl.get("alpha_output_count", 0)
        result["baseline"] = {
            "video_width": vi.get("width"),
            "video_height": vi.get("height"),
            "video_total_frames": vi.get("total_frames"),
            "mask_width": mi.get("width"),
            "mask_height": mi.get("height"),
            "alpha_output_count": alpha_count,
            "overall_status": bl.get("overall_status"),
        }
    except Exception as exc:
        result["baseline_error"] = f"{type(exc).__name__}: {exc}"
else:
    result["baseline"] = None
    result["baseline_note"] = "baseline manifest not found or not provided"

# ---- 3. MatAnyone2 import ----
try:
    import matanyone2
    result["matanyone2_import"] = {
        "available": True,
        "package_path": getattr(matanyone2, "__file__", None),
        "version": getattr(matanyone2, "__version__", None),
    }
except Exception as exc:
    result["matanyone2_import"] = {
        "available": False,
        "error": f"{type(exc).__name__}: {exc}",
    }
    print(json.dumps(result))
    sys.exit(0)

# ---- 4. MatAnyone2 model class ----
try:
    from matanyone2.model.matanyone2 import MatAnyone2
    result["matanyone2_class"] = {
        "available": True,
        "module": MatAnyone2.__module__,
        "qualname": MatAnyone2.__qualname__,
        "bases": [b.__qualname__ for b in MatAnyone2.__mro__],
        "is_nn_module": False,
    }
    try:
        import torch.nn as nn
        result["matanyone2_class"]["is_nn_module"] = issubclass(MatAnyone2, nn.Module)
    except Exception:
        pass
    # forward / __call__ signature
    if hasattr(MatAnyone2, "forward"):
        try:
            sig = inspect.signature(MatAnyone2.forward)
            result["matanyone2_class"]["forward_signature"] = str(sig)
        except (ValueError, TypeError):
            result["matanyone2_class"]["forward_signature"] = None
    # cfg attribute
    if hasattr(MatAnyone2, "cfg"):
        result["matanyone2_class"]["has_cfg_class_attribute"] = True
except Exception as exc:
    result["matanyone2_class"] = {
        "available": False,
        "error": f"{type(exc).__name__}: {exc}",
    }

# ---- 5. Model load attempt ----
model_search_base = Path.home() / ".local/share/Flux/models/matanyone2"
model_dir = None
if model_search_base.is_dir():
    for d in sorted(model_search_base.iterdir()):
        if d.is_dir() and (d / "model.safetensors").is_file():
            model_dir = d
            break

result["model_weights"] = {
    "search_base": str(model_search_base),
    "found_dir": str(model_dir) if model_dir else None,
}

if model_dir is not None:
    try:
        ma2 = MatAnyone2.from_pretrained(str(model_dir))
        result["model_load"] = {"success": True, "model_dir": str(model_dir)}
        # Inspect loaded model
        result["model_inspect"] = {}
        mi = result["model_inspect"]
        mi["model_class"] = type(ma2).__qualname__
        mi["model_module"] = type(ma2).__module__

        # cfg keys
        if hasattr(ma2, "cfg"):
            cfg = ma2.cfg
            if hasattr(cfg, "__dataclass_fields__"):
                mi["cfg_keys"] = sorted(cfg.__dataclass_fields__.keys())
            elif hasattr(cfg, "__dict__"):
                mi["cfg_keys"] = sorted(vars(cfg).keys())
            elif isinstance(cfg, dict):
                mi["cfg_keys"] = sorted(cfg.keys())
            else:
                mi["cfg_keys"] = None
                mi["cfg_type"] = type(cfg).__qualname__

        # Top-level named children
        try:
            children = list(ma2.named_children())
            mi["top_level_children_count"] = len(children)
            mi["top_level_children"] = [
                {"name": name, "type": type(child).__qualname__}
                for name, child in children
            ]
        except Exception:
            mi["top_level_children_count"] = None

        # Module counts
        try:
            all_modules = list(ma2.modules())
            mi["total_module_count"] = len(all_modules)
        except Exception:
            mi["total_module_count"] = None

        # forward signature of the instance
        if hasattr(ma2, "forward"):
            try:
                sig = inspect.signature(ma2.forward)
                mi["forward_signature"] = str(sig)
            except (ValueError, TypeError):
                mi["forward_signature"] = None

        # Clean up
        del ma2
        try:
            import torch
            if torch.cuda.is_available():
                torch.cuda.empty_cache()
        except Exception:
            pass

    except Exception as exc:
        result["model_load"] = {
            "success": False,
            "model_dir": str(model_dir),
            "error": f"{type(exc).__name__}: {exc}",
            "traceback": traceback.format_exc(),
        }
else:
    result["model_load"] = {
        "success": False,
        "note": "no model weights directory found; skipping load",
    }

# ---- 6. InferenceCore ----
try:
    from matanyone2.inference.inference_core import InferenceCore
    result["inference_core"] = {
        "available": True,
        "module": InferenceCore.__module__,
        "qualname": InferenceCore.__qualname__,
    }
    # __init__ signature
    try:
        init_sig = inspect.signature(InferenceCore.__init__)
        result["inference_core"]["init_signature"] = str(init_sig)
    except (ValueError, TypeError):
        result["inference_core"]["init_signature"] = None
    # step signature
    if hasattr(InferenceCore, "step"):
        try:
            step_sig = inspect.signature(InferenceCore.step)
            result["inference_core"]["step_signature"] = str(step_sig)
        except (ValueError, TypeError):
            result["inference_core"]["step_signature"] = None
    # output_prob_to_mask
    if hasattr(InferenceCore, "output_prob_to_mask"):
        try:
            mask_sig = inspect.signature(InferenceCore.output_prob_to_mask)
            result["inference_core"]["output_prob_to_mask_signature"] = str(mask_sig)
        except (ValueError, TypeError):
            result["inference_core"]["output_prob_to_mask_signature"] = None
    # Inspect instance attributes via source if possible
    try:
        src = inspect.getsource(InferenceCore.__init__)
        result["inference_core"]["init_source_lines"] = len(src.splitlines())
    except (OSError, TypeError):
        result["inference_core"]["init_source_lines"] = None

except Exception as exc:
    result["inference_core"] = {
        "available": False,
        "error": f"{type(exc).__name__}: {exc}",
    }

# ---- 7. Explicit export blockers ----
blockers = []

# Temporal state: InferenceCore maintains per-object memory across frames
blockers.append({
    "id": "temporal_state",
    "severity": "critical",
    "description": (
        "InferenceCore accumulates per-object memory (object_logits, "
        "long_term_memory, etc.) across frames via repeated step() calls. "
        "Each step() depends on state built by previous steps. "
        "Export requires capturing the entire step loop or flattening "
        "the state machine."
    ),
})

# Python loop: inference is an iterative frame-by-frame loop
blockers.append({
    "id": "python_loop",
    "severity": "critical",
    "description": (
        "MatAnyone2 inference uses a Python-level frame loop with warmup, "
        "first-frame masking, and continuation steps. The loop has branches "
        "(first_frame_pred, mask provision) that depend on frame index. "
        "Not directly representable as a single static graph."
    ),
})

# Dynamic sizes
blockers.append({
    "id": "dynamic_size",
    "severity": "high",
    "description": (
        "Frames and masks can have arbitrary spatial dimensions. "
        "Model internal tensors (object logits, memory) are sized "
        "dynamically based on input. ONNX/TorchScript export typically "
        "requires fixed or explicitly-dynamic dimension declarations."
    ),
})

# Non-tensor state
blockers.append({
    "id": "non_tensor_state",
    "severity": "high",
    "description": (
        "InferenceCore stores configuration objects, integer counters, "
        "and potentially dicts/lists of tensors as temporal state. "
        "Pure tensor-export formats (ONNX, TorchScript) cannot directly "
        "represent this mixed Python/tensor state."
    ),
})

# Preprocessing assumptions
blockers.append({
    "id": "preprocessing",
    "severity": "medium",
    "description": (
        "The worker divides pixel values by 255.0 (uint8→float normalization) "
        "and permutes HWC→CHW before feeding to model. Export must include or "
        "exclude this preprocessing consistently."
    ),
})

# Postprocessing assumptions
blockers.append({
    "id": "postprocessing",
    "severity": "medium",
    "description": (
        "Alpha masks are extracted via output_prob_to_mask(), then scaled to "
        "0-255 uint8 and saved as PNG. This post-processing is Python-side and "
        "would need separate handling in any exported runtime."
    ),
})

# Warmup frames
blockers.append({
    "id": "warmup_frames",
    "severity": "medium",
    "description": (
        "The worker feeds the first frame N=10 times as warmup before real "
        "inference. This stabilises temporal memory but adds latency. "
        "Export must account for or skip this warmup phase."
    ),
})

result["export_blockers"] = blockers

print(json.dumps(result, indent=2))
'''

    # Substitute baseline manifest path
    child_code = child_code.replace(
        '"""BL_BASELINE_PATH_PLACEHOLDER"""',
        json.dumps(bl_path_str),
    )

    proc = subprocess.run(
        [str(py), "-c", child_code],
        cwd=str(REPO_ROOT),
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=300,
        check=False,
    )

    if proc.returncode != 0:
        return {
            "child_error": True,
            "returncode": proc.returncode,
            "stderr": proc.stderr[-3000:] if proc.stderr else "",
            "stdout_preview": proc.stdout[-1000:] if proc.stdout else "",
        }

    try:
        # The model loader may print non-JSON progress before/after the pretty
        # JSON payload. Extract the outermost JSON object from stdout.
        stdout = proc.stdout or ""
        start = stdout.find("{")
        end = stdout.rfind("}")
        if start >= 0 and end >= start:
            return json.loads(stdout[start:end + 1])
        return json.loads(stdout)
    except json.JSONDecodeError as exc:
        return {
            "child_parse_error": True,
            "parse_error": str(exc),
            "stdout_preview": proc.stdout[-2000:] if proc.stdout else "",
            "stderr_preview": proc.stderr[-2000:] if proc.stderr else "",
        }


# ---------------------------------------------------------------------------
# Route handlers
# ---------------------------------------------------------------------------

def route_introspect(
    runtime_python: Path,
    preflight: Dict[str, Any],
    baseline_manifest: Optional[Path],
    out_dir: Path,
) -> Dict[str, Any]:
    """Run introspection route and write report."""
    report: Dict[str, Any] = {
        "schema": SCHEMA,
        "route": "introspect",
        "timestamp": datetime.now(timezone.utc).isoformat(),
        "runtime_python": str(runtime_python),
        "preflight": preflight,
    }

    if not preflight.get("executable"):
        report["status"] = "blocked"
        report["error"] = f"Runtime python not executable: {runtime_python}"
        _write_report(out_dir, report)
        return report

    # Run child introspection
    t0 = time.monotonic()
    child = run_child_introspect(runtime_python, baseline_manifest)
    elapsed = time.monotonic() - t0
    report["child_elapsed_seconds"] = round(elapsed, 3)

    if child.get("child_error") or child.get("child_parse_error"):
        report["status"] = "error"
        report["child_result"] = child
        _write_report(out_dir, report)
        return report

    # Merge child results into report
    report["status"] = "ok"
    report["runtime"] = {
        "python": child.get("runtime_python"),
        "torch_version": child.get("torch_version"),
        "cuda_available": child.get("cuda_available"),
        "cuda_device_count": child.get("cuda_device_count"),
        "cuda_device_name": child.get("cuda_device_name"),
        "cuda_version": child.get("cuda_version"),
    }
    report["baseline_summary"] = child.get("baseline")
    report["matanyone2_import"] = child.get("matanyone2_import")
    report["matanyone2_class"] = child.get("matanyone2_class")
    report["model_weights"] = child.get("model_weights")
    report["model_load"] = child.get("model_load")
    report["model_inspect"] = child.get("model_inspect")
    report["inference_core"] = child.get("inference_core")
    report["export_blockers"] = child.get("export_blockers", [])

    _write_report(out_dir, report)
    return report


# ---------------------------------------------------------------------------
# Decompose route — trace InferenceCore.step()
# ---------------------------------------------------------------------------

def run_child_decompose(
    py: Path,
    baseline_manifest: Optional[Path],
    max_trace_frames: int,
    warmup_repeats: int,
    device: str,
) -> Dict[str, Any]:
    """Run a child script under the provider runtime that loads MatAnyone2,
    constructs InferenceCore, and traces step() calls with tensor/state
    snapshots."""

    bl_path_str = str(baseline_manifest) if baseline_manifest else ""

    child_code = r'''
import importlib
import inspect
import json
import os
import sys
import traceback
from pathlib import Path
from glob import glob

result = {"schema": "decompose_child.v1"}
step_log = []
module_forward_counts = {}

# ---- Parameters from parent ----
BL_PATH = """BL_BASELINE_PATH_PLACEHOLDER"""
MAX_TRACE_FRAMES = __MAX_TRACE_FRAMES__
WARMUP_REPEATS = __WARMUP_REPEATS__
DEVICE = "__DEVICE__"

# ---- Helpers ----
def tensor_summary(t):
    """Safe summary of a tensor or None."""
    try:
        import torch
        if t is None:
            return None
        if isinstance(t, torch.Tensor):
            return {
                "shape": list(t.shape),
                "dtype": str(t.dtype),
                "device": str(t.device),
            }
    except Exception:
        pass
    return repr(t) if t is not None else None

def safe_attr_summary(obj, attr_name, max_list=5):
    """Safely summarize an attribute of an object."""
    try:
        val = getattr(obj, attr_name, None)
        if val is None:
            return None
        import torch
        if isinstance(val, torch.Tensor):
            return tensor_summary(val)
        if isinstance(val, (list, tuple)):
            items = []
            for i, v in enumerate(val[:max_list]):
                if isinstance(v, torch.Tensor):
                    items.append(tensor_summary(v))
                else:
                    items.append(type(v).__name__)
            return {"type": type(val).__name__, "length": len(val), "sample": items}
        if isinstance(val, dict):
            keys_sample = list(val.keys())[:max_list]
            return {"type": "dict", "keys_sample": keys_sample, "length": len(val)}
        return {"type": type(val).__name__, "repr": repr(val)[:200]}
    except Exception as exc:
        return {"error": f"{type(exc).__name__}: {exc}"}

def collect_state_snapshot(processor):
    """Collect a summary of InferenceCore attributes relevant to state."""
    state = {}
    # Known state-carrying attributes from introspection / source
    interesting_attrs = [
        "object_logits", "long_term_memory", "last_mem_frame",
        "current_memory", "memory_bank", "matched_idx",
        "missed_idx", "num_objects", "curr_shrink",
        "sensory_logits", "pairwise_vectors",
        "output_zeros", "maskmem_bank",
    ]
    # Also grab all non-callable, non-dunder attrs
    try:
        all_attrs = [
            a for a in dir(processor)
            if not a.startswith("__") and not callable(getattr(processor, a, None))
        ]
    except Exception:
        all_attrs = []

    for attr in sorted(set(interesting_attrs + all_attrs)):
        if attr.startswith("__"):
            continue
        try:
            val = getattr(processor, attr, None)
            if val is None:
                continue
            import torch
            if callable(val):
                continue
            if isinstance(val, torch.Tensor):
                state[attr] = tensor_summary(val)
            elif isinstance(val, (list, tuple)):
                items = []
                for v in val[:3]:
                    if isinstance(v, torch.Tensor):
                        items.append(tensor_summary(v))
                    else:
                        items.append(type(v).__name__)
                state[attr] = {"type": type(val).__name__, "length": len(val), "sample": items}
            elif isinstance(val, dict):
                state[attr] = {
                    "type": "dict",
                    "keys": list(val.keys())[:5],
                    "length": len(val),
                }
            else:
                rep = repr(val)
                if len(rep) > 200:
                    rep = rep[:200] + "..."
                state[attr] = {"type": type(val).__name__, "repr": rep}
        except Exception:
            pass
    return state

# ---- Load baseline manifest ----
frame_paths = []
mask_path = None

if BL_PATH and Path(BL_PATH).is_file():
    try:
        with open(BL_PATH) as f:
            bl = json.load(f)
        # Try frames from extraction
        frames_dir = bl.get("frame_extraction", {}).get("frames_dir")
        if frames_dir and Path(frames_dir).is_dir():
            frame_paths = sorted(glob(str(Path(frames_dir) / "frame_*.png")))
        # Mask from inputs
        mask_path = bl.get("inputs", {}).get("mask", {}).get("path")
    except Exception as exc:
        result["baseline_parse_error"] = f"{type(exc).__name__}: {exc}"

if not frame_paths:
    # Fallback: use the default baseline frames directory
    fallback_dir = Path.home() / "Flux/tools/ai_native_spikes/results/matanyone2/baseline/frames"
    if not fallback_dir.is_dir():
        fallback_dir = Path("tools/ai_native_spikes/results/matanyone2/baseline/frames")
    if fallback_dir.is_dir():
        frame_paths = sorted(glob(str(fallback_dir / "frame_*.png")))

if not frame_paths:
    result["status"] = "error"
    result["error"] = "No frame images found for decomposition trace"
    print(json.dumps(result, indent=2))
    sys.exit(0)

result["frames_found"] = len(frame_paths)
result["mask_path_used"] = mask_path
result["max_trace_frames"] = MAX_TRACE_FRAMES
result["warmup_repeats"] = WARMUP_REPEATS

# ---- Import & Load Model ----
try:
    import torch
    import torch.nn as nn
    import numpy as np
    from PIL import Image
    from matanyone2.model.matanyone2 import MatAnyone2
    from matanyone2.inference.inference_core import InferenceCore
except Exception as exc:
    result["status"] = "error"
    result["import_error"] = f"{type(exc).__name__}: {exc}"
    result["traceback"] = traceback.format_exc()
    print(json.dumps(result, indent=2))
    sys.exit(0)

# Find model weights
model_search_base = Path.home() / ".local/share/Flux/models/matanyone2"
model_dir = None
if model_search_base.is_dir():
    for d in sorted(model_search_base.iterdir()):
        if d.is_dir() and (d / "model.safetensors").is_file():
            model_dir = d
            break

if model_dir is None:
    result["status"] = "error"
    result["error"] = "Model weights not found"
    print(json.dumps(result, indent=2))
    sys.exit(0)

result["model_dir"] = str(model_dir)

try:
    model = MatAnyone2.from_pretrained(str(model_dir))
except Exception as exc:
    result["status"] = "error"
    result["model_load_error"] = f"{type(exc).__name__}: {exc}"
    result["traceback"] = traceback.format_exc()
    print(json.dumps(result, indent=2))
    sys.exit(0)

# ---- Record top-level child modules ----
top_children = []
child_forward_counts = {}
for name, child in model.named_children():
    top_children.append({"name": name, "type": type(child).__qualname__})
    child_forward_counts[name] = 0

# Install forward hooks on top-level children to count calls
def _make_hook(mod_name):
    def hook(module, input, output):
        child_forward_counts[mod_name] = child_forward_counts.get(mod_name, 0) + 1
    return hook

hook_handles = []
for name, child in model.named_children():
    h = child.register_forward_hook(_make_hook(name))
    hook_handles.append(h)

result["top_level_children"] = top_children

# ---- Build InferenceCore ----
try:
    device = torch.device(DEVICE if torch.cuda.is_available() else "cpu")
    processor = InferenceCore(model, cfg=model.cfg, device=device)
except Exception as exc:
    result["status"] = "error"
    result["inference_core_error"] = f"{type(exc).__name__}: {exc}"
    result["traceback"] = traceback.format_exc()
    print(json.dumps(result, indent=2))
    sys.exit(0)

# ---- Load mask and first frame ----
if mask_path and Path(mask_path).is_file():
    mask_img = Image.open(str(mask_path)).convert("L")
    mask_arr = np.array(mask_img)
    mask_tensor = torch.from_numpy(mask_arr).float().to(device)
else:
    # Fallback: create a dummy mask from first frame dims
    first_img = Image.open(frame_paths[0]).convert("RGB")
    mask_arr = np.ones((first_img.size[1], first_img.size[0]), dtype=np.uint8) * 255
    mask_tensor = torch.from_numpy(mask_arr.astype(np.float32)).to(device)
    result["mask_note"] = "No mask found; used synthetic full-white mask"

first_img = Image.open(frame_paths[0]).convert("RGB")
first_arr = np.array(first_img)
first_tensor = torch.from_numpy(first_arr).permute(2, 0, 1).float()

# Limit frames to trace
trace_frame_paths = frame_paths[:MAX_TRACE_FRAMES]
result["trace_frame_count"] = len(trace_frame_paths)

# Build warmup + real frame tensors
warmup_frames = [first_tensor] * WARMUP_REPEATS
real_frame_tensors = []
for fp in trace_frame_paths:
    img = Image.open(fp).convert("RGB")
    arr = np.array(img)
    real_frame_tensors.append(torch.from_numpy(arr).permute(2, 0, 1).float())

all_tensors = warmup_frames + real_frame_tensors
objects = [1]
n_warmup = WARMUP_REPEATS

# ---- Wrap step to record per-call state ----
orig_step = processor.step
call_idx = [0]  # mutable counter in closure

def traced_step(image, mask=None, objects=None, *, idx_mask=False,
                 end=False, delete_buffer=True, force_permanent=False,
                 matting=True, first_frame_pred=False, **kwargs):
    ci = call_idx[0]
    call_idx[0] += 1

    # Determine phase from the actual step flags instead of call index alone.
    # call 0 supplies the guide mask; later first_frame_pred=True calls are
    # warmup; non-first_frame_pred calls are real frame propagation.
    if mask is not None:
        phase = "warmup_init"
        frame_idx = None
    elif first_frame_pred:
        phase = "warmup"
        frame_idx = None
    else:
        phase = "frame"
        frame_idx = max(0, ci - n_warmup)

    # Record pre-call state
    pre_state = collect_state_snapshot(processor)

    # Reset forward counts for this call
    pre_counts = dict(child_forward_counts)

    # Build input record
    inputs_record = {
        "image": tensor_summary(image),
        "mask": tensor_summary(mask),
        "objects": objects,
        "idx_mask": idx_mask,
        "end": end,
        "delete_buffer": delete_buffer,
        "force_permanent": force_permanent,
        "first_frame_pred": first_frame_pred,
        "matting": matting,
    }
    # Any extra kwargs
    if kwargs:
        inputs_record["extra_kwargs"] = {
            k: (tensor_summary(v) if isinstance(v, torch.Tensor) else repr(v)[:100])
            for k, v in kwargs.items()
        }

    # Call original step
    output = orig_step(image, mask=mask, objects=objects,
                       idx_mask=idx_mask, end=end,
                       delete_buffer=delete_buffer,
                       force_permanent=force_permanent,
                       first_frame_pred=first_frame_pred,
                       matting=matting, **kwargs)

    # Record post-call state
    post_state = collect_state_snapshot(processor)
    post_counts = dict(child_forward_counts)

    # Delta: which modules were called in this step
    modules_called = {
        name: post_counts.get(name, 0) - pre_counts.get(name, 0)
        for name in post_counts
        if post_counts.get(name, 0) > pre_counts.get(name, 0)
    }

    entry = {
        "call_index": ci,
        "phase": phase,
        "frame_index": frame_idx,
        "inputs": inputs_record,
        "output": tensor_summary(output),
        "modules_called_this_step": modules_called,
        "state_changed_keys": sorted(
            set(post_state.keys()) - set(pre_state.keys())
            | {
                k for k in set(post_state.keys()) & set(pre_state.keys())
                if post_state[k] != pre_state[k]
            }
        ),
    }
    # Only include pre/post state diffs (not full dumps to keep report small)
    state_diff = {}
    for k in entry["state_changed_keys"]:
        state_diff[k] = {
            "before": pre_state.get(k),
            "after": post_state.get(k),
        }
    entry["state_diff"] = state_diff

    step_log.append(entry)
    return output

processor.step = traced_step

# ---- Run the inference loop ----
try:
    with torch.inference_mode():
        for ti, frame_tensor in enumerate(all_tensors):
            image = (frame_tensor / 255.0).float().to(device)
            if ti == 0:
                output_prob = processor.step(image, mask_tensor, objects=objects)
                output_prob = processor.step(image, first_frame_pred=True)
            elif ti < n_warmup:
                output_prob = processor.step(image, first_frame_pred=True)
            else:
                output_prob = processor.step(image)

    result["status"] = "ok"
    result["step_log"] = step_log

    # Final forward counts summary
    result["module_forward_counts"] = dict(child_forward_counts)

    # Clean up hooks
    for h in hook_handles:
        h.remove()

    # ---- Analysis: native-state-machine implications ----
    all_state_keys = set()
    tensor_state_keys = set()
    list_state_keys = set()
    dict_state_keys = set()

    for entry in step_log:
        for k, diff in entry.get("state_diff", {}).items():
            all_state_keys.add(k)
            after = diff.get("after")
            if isinstance(after, dict):
                t = after.get("type", "")
                if "shape" in after:
                    tensor_state_keys.add(k)
                elif t == "list":
                    list_state_keys.add(k)
                elif t == "dict":
                    dict_state_keys.add(k)
            elif after is not None:
                tensor_state_keys.add(k)

    # Identify submodules actually called during steps
    active_modules = set()
    for entry in step_log:
        active_modules.update(entry.get("modules_called_this_step", {}).keys())

    phases_seen = list(dict.fromkeys(e["phase"] for e in step_log))

    result["implications"] = {
        "persistent_state_keys": sorted(all_state_keys),
        "tensor_state_keys": sorted(tensor_state_keys),
        "list_state_keys": sorted(list_state_keys),
        "dict_state_keys": sorted(dict_state_keys),
        "loop_phases": phases_seen,
        "active_submodules": sorted(active_modules),
        "export_candidates": sorted(active_modules),
        "cpp_algorithm_requirements": [
            "Frame-by-frame loop with warmup phase (repeated first frame)",
            "Two-step initialization: mask step then first_frame_pred step",
            "Per-frame step() call accumulating temporal state",
            "State machine phases: warmup_init -> warmup -> frame",
            "output_prob_to_mask() post-processing per frame",
        ],
    }

except Exception as exc:
    result["status"] = "error"
    result["inference_error"] = f"{type(exc).__name__}: {exc}"
    result["traceback"] = traceback.format_exc()
    result["step_log_partial"] = step_log
    for h in hook_handles:
        try:
            h.remove()
        except Exception:
            pass

# Clean up GPU
try:
    if torch.cuda.is_available():
        torch.cuda.empty_cache()
except Exception:
    pass

print(json.dumps(result, indent=2))
'''

    # Substitute parameters
    child_code = child_code.replace(
        '"""BL_BASELINE_PATH_PLACEHOLDER"""',
        json.dumps(bl_path_str),
    )
    child_code = child_code.replace(
        "__MAX_TRACE_FRAMES__",
        str(max_trace_frames),
    )
    child_code = child_code.replace(
        "__WARMUP_REPEATS__",
        str(warmup_repeats),
    )
    child_code = child_code.replace(
        "__DEVICE__",
        device,
    )

    proc = subprocess.run(
        [str(py), "-c", child_code],
        cwd=str(REPO_ROOT),
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=600,
        check=False,
    )

    if proc.returncode != 0:
        return {
            "child_error": True,
            "returncode": proc.returncode,
            "stderr": proc.stderr[-3000:] if proc.stderr else "",
            "stdout_preview": proc.stdout[-1000:] if proc.stdout else "",
        }

    try:
        stdout = proc.stdout or ""
        start = stdout.find("{")
        end = stdout.rfind("}")
        if start >= 0 and end >= start:
            return json.loads(stdout[start:end + 1])
        return json.loads(stdout)
    except json.JSONDecodeError as exc:
        return {
            "child_parse_error": True,
            "parse_error": str(exc),
            "stdout_preview": proc.stdout[-2000:] if proc.stdout else "",
            "stderr_preview": proc.stderr[-2000:] if proc.stderr else "",
        }


def route_decompose(
    runtime_python: Path,
    preflight: Dict[str, Any],
    baseline_manifest: Optional[Path],
    out_dir: Path,
    max_trace_frames: int,
    warmup_repeats: int,
    device: str,
) -> Dict[str, Any]:
    """Run decomposition trace of InferenceCore.step() and write report."""
    report: Dict[str, Any] = {
        "schema": SCHEMA,
        "route": "decompose",
        "timestamp": datetime.now(timezone.utc).isoformat(),
        "runtime_python": str(runtime_python),
        "preflight": preflight,
        "max_trace_frames": max_trace_frames,
        "warmup_repeats": warmup_repeats,
        "device": device,
    }

    if not preflight.get("executable"):
        report["status"] = "blocked"
        report["error"] = f"Runtime python not executable: {runtime_python}"
        _write_decomposition_report(out_dir, report)
        return report

    t0 = time.monotonic()
    child = run_child_decompose(
        runtime_python, baseline_manifest,
        max_trace_frames, warmup_repeats, device,
    )
    elapsed = time.monotonic() - t0
    report["child_elapsed_seconds"] = round(elapsed, 3)

    if child.get("child_error") or child.get("child_parse_error"):
        report["status"] = "error"
        report["child_result"] = child
        _write_decomposition_report(out_dir, report)
        return report

    # Merge child results
    report["status"] = child.get("status", "unknown")
    report["frames_found"] = child.get("frames_found")
    report["mask_path_used"] = child.get("mask_path_used")
    report["model_dir"] = child.get("model_dir")
    report["top_level_children"] = child.get("top_level_children")
    report["step_log"] = child.get("step_log", [])
    report["module_forward_counts"] = child.get("module_forward_counts")
    report["implications"] = child.get("implications")

    # Propagate any errors from child
    for err_key in ("import_error", "model_load_error", "inference_core_error",
                    "inference_error", "baseline_parse_error", "mask_note"):
        if err_key in child:
            report[err_key] = child[err_key]

    _write_decomposition_report(out_dir, report)
    return report


def _write_decomposition_report(out_dir: Path, report: Dict[str, Any]) -> Path:
    out_dir.mkdir(parents=True, exist_ok=True)
    report_path = out_dir / "matanyone2_decomposition_report.json"
    report_path.write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    return report_path


def route_placeholder(
    route_name: str,
    runtime_python: Path,
    preflight: Dict[str, Any],
    out_dir: Path,
) -> Dict[str, Any]:
    """Record not_implemented_pending_introspection for non-introspect routes."""
    report: Dict[str, Any] = {
        "schema": SCHEMA,
        "route": route_name,
        "timestamp": datetime.now(timezone.utc).isoformat(),
        "runtime_python": str(runtime_python),
        "preflight": preflight,
        "status": "not_implemented_pending_introspection",
        "note": (
            f"Route '{route_name}' is deferred until introspection completes "
            "and a safe minimal attempt is identified. No GPU-heavy export "
            "attempted."
        ),
    }
    _write_report(out_dir, report)
    return report


# ---------------------------------------------------------------------------
# IO
# ---------------------------------------------------------------------------

def _write_report(out_dir: Path, report: Dict[str, Any]) -> Path:
    out_dir.mkdir(parents=True, exist_ok=True)
    report_path = out_dir / "matanyone2_export_report.json"
    report_path.write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    return report_path


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main(argv: Optional[List[str]] = None) -> int:
    parser = argparse.ArgumentParser(
        description="MatAnyone2 native runtime/export probe for tech-stack shootout",
    )
    parser.add_argument(
        "--runtime-id",
        default="sam3",
        help="Provider runtime ID (default: sam3)",
    )
    parser.add_argument(
        "--device",
        default="cuda",
        help="Device for model load (default: cuda)",
    )
    parser.add_argument(
        "--baseline-manifest",
        default=str(
            REPO_ROOT
            / "tools/ai_native_spikes/results/matanyone2/baseline/baseline_manifest.json"
        ),
        help="Path to baseline manifest JSON",
    )
    parser.add_argument(
        "--out",
        default=str(REPO_ROOT / "tools/ai_native_spikes/results/matanyone2/export"),
        help="Output directory for reports",
    )
    parser.add_argument(
        "--route",
        choices=["introspect", "decompose", "torchscript", "torch-export", "onnx", "all"],
        default="introspect",
        help="Export route to probe (default: introspect)",
    )
    parser.add_argument(
        "--max-trace-frames",
        type=int,
        default=3,
        help="Max real frames to trace in decompose route (default: 3)",
    )
    parser.add_argument(
        "--warmup-repeats",
        type=int,
        default=2,
        help="Number of warmup repeats in decompose route (default: 2)",
    )
    args = parser.parse_args(argv)

    baseline_manifest = Path(args.baseline_manifest) if args.baseline_manifest else None
    if baseline_manifest and not baseline_manifest.exists():
        print(
            f"WARNING: baseline manifest not found: {baseline_manifest}",
            file=sys.stderr,
        )
        baseline_manifest = None

    out_dir = Path(args.out)

    # Discover runtime python
    print(f"Discovering runtime python for '{args.runtime_id}'...")
    try:
        runtime_python = discover_runtime_python(args.runtime_id)
    except RuntimeError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        report = {
            "schema": SCHEMA,
            "route": args.route,
            "timestamp": datetime.now(timezone.utc).isoformat(),
            "status": "blocked",
            "error": str(exc),
        }
        _write_report(out_dir, report)
        return 2
    print(f"  Runtime python: {runtime_python}")

    # Preflight
    preflight = preflight_executable(runtime_python)
    print(f"  Preflight: {preflight}")

    # Dispatch route
    routes_to_run = (
        ["introspect", "decompose", "torchscript", "torch-export", "onnx"]
        if args.route == "all"
        else [args.route]
    )

    results = {}
    for route in routes_to_run:
        print(f"\nRunning route: {route}")
        if route == "introspect":
            results[route] = route_introspect(
                runtime_python, preflight, baseline_manifest, out_dir,
            )
        elif route == "decompose":
            decompose_dir = out_dir / "decompose"
            results[route] = route_decompose(
                runtime_python, preflight, baseline_manifest, decompose_dir,
                args.max_trace_frames, args.warmup_repeats, args.device,
            )
        else:
            route_dir = out_dir / route
            results[route] = route_placeholder(
                route, runtime_python, preflight, route_dir,
            )

    # Print summary
    report_path = out_dir / "matanyone2_export_report.json"
    print(f"\nReport written to: {report_path}")

    for route, r in results.items():
        status = r.get("status", "unknown")
        print(f"  {route}: {status}")

    # Return non-zero if any route errored
    if any(r.get("status") == "error" for r in results.values()):
        return 1
    if any(r.get("status") == "blocked" for r in results.values()):
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
