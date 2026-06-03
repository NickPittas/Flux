#!/usr/bin/env python3
"""Shared IO/schema helpers for Flux AI native-runtime spikes.

This module intentionally has no dependency on Flux AI panel state. It only
records reproducible facts for standalone spike runs.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any, Dict, Iterable, Optional


REPO_ROOT = Path(__file__).resolve().parents[3]
RESULTS_DIR = REPO_ROOT / "tools" / "ai_native_spikes" / "results"
SAM3_PROOF_ASSET = REPO_ROOT / "tools" / "ai" / "proof_assets" / "sam31_probe_source.ppm"


def write_json(path: Path, payload: Dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def sha256_file(path: Path) -> Optional[str]:
    if not path.exists() or not path.is_file():
        return None
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def ppm_dimensions(path: Path) -> Optional[Dict[str, int]]:
    """Return width/height for simple PPM assets without external libraries."""
    if not path.exists():
        return None
    with path.open("rb") as f:
        magic = f.readline().strip()
        if magic not in {b"P3", b"P6"}:
            return None
        tokens = []
        while len(tokens) < 2:
            line = f.readline()
            if not line:
                return None
            stripped = line.strip()
            if not stripped or stripped.startswith(b"#"):
                continue
            tokens.extend(stripped.split())
        return {"width": int(tokens[0]), "height": int(tokens[1])}


def run_json_command(cmd: Iterable[str], timeout: int = 60) -> Dict[str, Any]:
    argv = list(cmd)
    try:
        proc = subprocess.run(
            argv,
            cwd=str(REPO_ROOT),
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=timeout,
            check=False,
        )
        stdout = proc.stdout.strip()
        parsed = None
        if stdout:
            try:
                parsed = json.loads(stdout)
            except json.JSONDecodeError:
                parsed = None
        return {
            "command": argv,
            "returncode": proc.returncode,
            "stdout": stdout,
            "stderr": proc.stderr.strip(),
            "json": parsed,
        }
    except Exception as exc:  # noqa: BLE001 - diagnostics tool
        return {"command": argv, "error": repr(exc)}


def provider_runtime_status(runtime_id: str) -> Dict[str, Any]:
    script = REPO_ROOT / "tools" / "ai" / "flux_provider_runtime.py"
    if not script.exists():
        return {"runtime_id": runtime_id, "available": False, "error": "missing flux_provider_runtime.py"}
    status = run_json_command([sys.executable, str(script), "status", runtime_id, "--json"])
    py = run_json_command([sys.executable, str(script), "python", runtime_id])
    py_path = (py.get("stdout") or "").strip()
    return {
        "runtime_id": runtime_id,
        "status": status,
        "python_path": py_path,
        "python_exists": bool(py_path and Path(py_path).exists()),
        "python_executable": bool(py_path and os.access(py_path, os.X_OK)),
    }


def collect_assets_manifest() -> Dict[str, Any]:
    mat_clip = os.environ.get("FLUX_MATANYONE2_CLIP", "")
    mat_mask = os.environ.get("FLUX_MATANYONE2_FIRST_MASK", "")
    sam3_dims = ppm_dimensions(SAM3_PROOF_ASSET)
    return {
        "schema": "flux.ai_native_spikes.assets.v1",
        "sam3": {
            "proof_asset": str(SAM3_PROOF_ASSET.relative_to(REPO_ROOT)),
            "exists": SAM3_PROOF_ASSET.exists(),
            "sha256": sha256_file(SAM3_PROOF_ASSET),
            "dimensions": sam3_dims,
            "recommended_center_point": (
                {"x": sam3_dims["width"] // 2, "y": sam3_dims["height"] // 2, "label": 1}
                if sam3_dims else None
            ),
        },
        "matanyone2": {
            "clip_env": "FLUX_MATANYONE2_CLIP",
            "clip": mat_clip,
            "clip_exists": bool(mat_clip and Path(mat_clip).exists()),
            "first_mask_env": "FLUX_MATANYONE2_FIRST_MASK",
            "first_mask": mat_mask,
            "first_mask_exists": bool(mat_mask and Path(mat_mask).exists()),
            "ready_for_baseline": bool(mat_clip and Path(mat_clip).exists() and mat_mask and Path(mat_mask).exists()),
        },
    }


def collect_environment() -> Dict[str, Any]:
    nvidia_smi = shutil.which("nvidia-smi")
    env: Dict[str, Any] = {
        "schema": "flux.ai_native_spikes.environment.v1",
        "repo_root": str(REPO_ROOT),
        "python": sys.executable,
        "python_version": sys.version,
        "platform": platform.platform(),
        "cuda_visible_devices": os.environ.get("CUDA_VISIBLE_DEVICES"),
        "nvidia_smi": None,
        "provider_runtimes": {
            "sam3": provider_runtime_status("sam3"),
            "sam31": provider_runtime_status("sam31"),
        },
        "python_packages": {},
    }
    if nvidia_smi:
        env["nvidia_smi"] = run_json_command([
            nvidia_smi,
            "--query-gpu=name,driver_version,memory.total,memory.used",
            "--format=csv,noheader,nounits",
        ])
    for package in ("torch", "onnx", "onnxruntime", "tensorrt", "numpy", "PIL"):
        try:
            mod = __import__(package)
            env["python_packages"][package] = getattr(mod, "__version__", "unknown")
        except Exception as exc:  # noqa: BLE001 - diagnostics tool
            env["python_packages"][package] = {"available": False, "error": repr(exc)}
    return env


def main() -> int:
    parser = argparse.ArgumentParser(description="Flux AI native spike IO contract utilities")
    parser.add_argument("command", choices=["environment", "assets"], help="manifest to write")
    parser.add_argument("--out", type=Path, default=None, help="output JSON path")
    args = parser.parse_args()

    if args.command == "environment":
        payload = collect_environment()
        out = args.out or RESULTS_DIR / "environment.json"
    else:
        payload = collect_assets_manifest()
        out = args.out or RESULTS_DIR / "assets_manifest.json"
    write_json(out, payload)
    print(out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
