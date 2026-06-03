#!/usr/bin/env python3
"""Flux AI no-op worker bridge for T083-1B.

Stdlib-only placeholder. It writes the reusable result-manifest contract only.
"""

from __future__ import annotations

import argparse
import json
import platform
import sys
from pathlib import Path
from typing import Any, Dict


def emit(payload: Dict[str, Any]) -> None:
    print(json.dumps(payload, sort_keys=True), flush=True)


def load_job(path: Path) -> Dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        job = json.load(handle)
    if not isinstance(job, dict):
        raise ValueError("job root must be an object")
    if job.get("mode") != "noop_cuda_probe":
        raise ValueError("unsupported job mode")
    for key in ("task", "model_id", "mode"):
        if key not in job:
            raise ValueError(f"missing job field: {key}")
    return job


def runtime_info() -> Dict[str, Any]:
    return {
        "python_executable": sys.executable,
        "python_version": platform.python_version(),
        "platform": platform.platform(),
        "cuda_available": None,
        "cuda_message": "Real CUDA detection is deferred to T083-3/T083-5 provider work.",
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Flux AI T083-1B no-op worker")
    parser.add_argument("--job", required=True, type=Path, help="Path to JSON job file")
    args = parser.parse_args()

    try:
        job = load_job(args.job)
    except (OSError, json.JSONDecodeError, ValueError) as exc:
        print(f"ERROR: invalid job: {exc}", file=sys.stderr)
        return 1

    info = runtime_info()
    emit({"event": "started", "job": job, "runtime": info})

    try:
        output_dir = Path(job["output_dir"])
        output_dir_project_relative = job["output_dir_project_relative"]
        manifest_path = Path(job["result_manifest_path"])
        manifest_path_project_relative = job["result_manifest_path_project_relative"]
    except KeyError as exc:
        print(f"ERROR: invalid job: missing job field: {exc.args[0]}", file=sys.stderr)
        return 1

    emit({"event": "progress", "message": "Preparing project-relative output manifest"})
    try:
        output_dir.mkdir(parents=True, exist_ok=True)
        manifest_path.parent.mkdir(parents=True, exist_ok=True)
        placeholder_path = output_dir / "placeholder.txt"
        placeholder_path.write_text("T083-1B placeholder; no media generated.", encoding="utf-8")
        placeholder_project_relative = output_dir_project_relative.rstrip("/") + "/placeholder.txt"

        outputs = [{
            "kind": "placeholder",
            "path": placeholder_project_relative,
            "channels": [],
            "intended_use": "contract_only_no_media_created",
        }]
        prompt = job.get("prompt")
        manifest = {
            "schema": "org.flux.ai.result-manifest.v1",
            "run_id": job["run_id"],
            "task": job["task"],
            "model_id": job["model_id"],
            "mode": "noop_cuda_probe",
            "output_dir": str(output_dir),
            "output_dir_project_relative": output_dir_project_relative,
            "outputs": outputs,
            "notes": "T083-1B manifest contract only; no media generated.",
        }
        if prompt is not None:
            manifest["prompt"] = prompt
        emit({"event": "progress", "message": "Writing T083-1B result manifest contract"})
        manifest_path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    except OSError as exc:
        print(f"ERROR: could not write result manifest: {exc}", file=sys.stderr)
        return 1

    finished = {
        "event": "finished",
        "success": True,
        "result_manifest_path": str(manifest_path),
        "result_manifest_path_project_relative": manifest_path_project_relative,
        "outputs": outputs,
        "job": job,
        "runtime": info,
    }
    if job.get("prompt") is not None:
        finished["prompt"] = job.get("prompt")
    emit(finished)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
