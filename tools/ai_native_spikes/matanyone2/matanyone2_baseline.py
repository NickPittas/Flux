#!/usr/bin/env python3
"""Phase-1 MatAnyone2 baseline runner.

Extracts frames from a video clip, builds a JSON-lines request for
tools/ai/matanyone2_worker.py, runs it via provider runtime Python, and writes
an immutable baseline_manifest.json.
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Dict, List, Optional

REPO_ROOT = Path(__file__).resolve().parents[3]
RUNTIME_DISCOVERY = REPO_ROOT / "tools" / "ai" / "flux_provider_runtime.py"
WORKER_SCRIPT = REPO_ROOT / "tools" / "ai" / "matanyone2_worker.py"
NICK_CLIP = "/home/npittas/Videos/For_Test/lightx2v_lora_rank_comparison.mp4"
NICK_MASK = "/home/npittas/Videos/For_Test/mask_1.png"


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
    return py


def get_video_info(video_path: Path) -> Dict[str, Any]:
    """Get video dimensions and frame count via ffprobe."""
    cmd = [
        "ffprobe", "-v", "error",
        "-select_streams", "v:0",
        "-show_entries", "stream=width,height,nb_frames,r_frame_rate",
        "-of", "json",
        str(video_path),
    ]
    proc = subprocess.run(cmd, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
    if proc.returncode != 0:
        raise RuntimeError(f"ffprobe failed: {proc.stderr.strip()}")
    info = json.loads(proc.stdout)
    streams = info.get("streams", [])
    if not streams:
        raise RuntimeError("ffprobe returned no streams")
    s = streams[0]
    return {
        "width": int(s["width"]),
        "height": int(s["height"]),
        "nb_frames": int(s.get("nb_frames", 0)),
        "r_frame_rate": s.get("r_frame_rate"),
    }


def get_mask_info(mask_path: Path) -> Dict[str, Any]:
    """Get mask dimensions via PIL."""
    from PIL import Image
    img = Image.open(mask_path)
    return {"width": img.size[0], "height": img.size[1], "mode": img.mode}


def extract_frames(video_path: Path, frames_dir: Path, max_frames: int) -> int:
    """Extract frames from video as PNG using ffmpeg. Returns frame count."""
    frames_dir.mkdir(parents=True, exist_ok=True)
    cmd = [
        "ffmpeg", "-y",
        "-i", str(video_path),
    ]
    if max_frames > 0:
        cmd.extend(["-frames:v", str(max_frames)])
    cmd.extend([
        "-qscale:v", "1",
        "-qmin", "1",
        str(frames_dir / "frame_%06d.png"),
    ])
    proc = subprocess.run(cmd, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
    if proc.returncode != 0:
        raise RuntimeError(f"ffmpeg frame extraction failed: {proc.stderr[-2000:]}")

    # Count extracted frames
    frames = sorted(frames_dir.glob("frame_*.png"))
    if not frames:
        raise RuntimeError("No frames extracted from video")
    return len(frames)


def build_worker_requests(
    frames_dir: Path,
    frame_count: int,
    mask_path: Path,
    alpha_dir: Path,
) -> List[str]:
    """Build JSON-lines worker requests: load, infer_video, shutdown."""
    frames_meta = []
    for i in range(1, frame_count + 1):
        frame_file = frames_dir / f"frame_{i:06d}.png"
        if frame_file.exists():
            frames_meta.append({
                "path": str(frame_file),
                "source_frame": i,
                "timeline_frame": i,
            })

    alpha_dir.mkdir(parents=True, exist_ok=True)

    requests = [
        json.dumps({"command": "load", "id": "bl-load"}),
        json.dumps({
            "command": "infer_video",
            "id": "bl-infer",
            "frames": frames_meta,
            "first_frame_mask": str(mask_path),
            "output_dir": str(alpha_dir),
            "source_range_start": 1,
            "source_range_end": frame_count,
        }),
        json.dumps({"command": "shutdown", "id": "bl-shutdown"}),
    ]
    return requests


def run_worker(
    runtime_python: Path,
    jsonlines_input: str,
    timeout: int = 3600,
) -> Dict[str, Any]:
    """Run matanyone2_worker.py with JSON-lines input and capture output."""
    cmd = [str(runtime_python), str(WORKER_SCRIPT)]
    t0 = time.monotonic()
    proc = subprocess.run(
        cmd,
        cwd=str(REPO_ROOT),
        text=True,
        input=jsonlines_input,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=timeout,
        check=False,
    )
    elapsed = time.monotonic() - t0

    # Parse JSON-lines responses
    responses: List[Dict[str, Any]] = []
    for line in proc.stdout.strip().splitlines():
        line = line.strip()
        if not line:
            continue
        try:
            responses.append(json.loads(line))
        except json.JSONDecodeError:
            responses.append({"raw": line, "parse_error": True})

    return {
        "command": cmd,
        "returncode": proc.returncode,
        "elapsed_seconds": round(elapsed, 3),
        "responses": responses,
        "stderr_preview": proc.stderr[-3000:] if proc.stderr else "",
    }


def main(argv: Optional[List[str]] = None) -> int:
    parser = argparse.ArgumentParser(description="Phase-1 MatAnyone2 baseline runner")
    parser.add_argument(
        "--video",
        default=os.environ.get("FLUX_MATANYONE2_CLIP", NICK_CLIP),
        help="Input video path",
    )
    parser.add_argument(
        "--first-frame-mask",
        default=os.environ.get("FLUX_MATANYONE2_FIRST_MASK", NICK_MASK),
        help="First-frame mask path",
    )
    parser.add_argument(
        "--out",
        default=str(REPO_ROOT / "tools" / "ai_native_spikes" / "results" / "matanyone2" / "baseline"),
        help="Output directory",
    )
    parser.add_argument(
        "--runtime-id",
        default="sam3",
        help="Provider runtime ID for python (default: sam3)",
    )
    parser.add_argument(
        "--max-frames",
        type=int,
        default=0,
        help="Max frames to extract (0=all)",
    )
    args = parser.parse_args(argv)

    video_path = Path(args.video).resolve()
    mask_path = Path(args.first_frame_mask).resolve()
    out_dir = Path(args.out).resolve()

    # Verify inputs
    if not video_path.exists():
        print(f"ERROR: video not found: {video_path}", file=sys.stderr)
        return 2
    if not mask_path.exists():
        print(f"ERROR: mask not found: {mask_path}", file=sys.stderr)
        return 2
    if not WORKER_SCRIPT.exists():
        print(f"ERROR: worker script not found: {WORKER_SCRIPT}", file=sys.stderr)
        return 2

    # Discover runtime python
    print(f"Discovering runtime python for '{args.runtime_id}'...")
    try:
        runtime_python = discover_runtime_python(args.runtime_id)
    except RuntimeError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2
    print(f"Runtime python: {runtime_python}")

    # Verify video info
    print(f"\nProbing video: {video_path}")
    try:
        video_info = get_video_info(video_path)
    except RuntimeError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2
    print(f"  Video: {video_info['width']}x{video_info['height']}, {video_info['nb_frames']} frames, r_frame_rate={video_info['r_frame_rate']}")

    # Verify mask info
    try:
        mask_info = get_mask_info(mask_path)
    except Exception as exc:
        print(f"ERROR: could not read mask: {exc}", file=sys.stderr)
        return 2
    print(f"  Mask: {mask_info['width']}x{mask_info['height']}, mode={mask_info['mode']}")

    # Verify dimensions match
    if video_info["width"] != mask_info["width"] or video_info["height"] != mask_info["height"]:
        print(
            f"ERROR: video ({video_info['width']}x{video_info['height']}) and mask "
            f"({mask_info['width']}x{mask_info['height']}) dimensions do not match",
            file=sys.stderr,
        )
        return 2

    # Extract frames
    frames_dir = out_dir / "frames"
    max_frames = args.max_frames if args.max_frames > 0 else 0
    total_frames = video_info["nb_frames"]
    extract_count = max_frames if max_frames > 0 else total_frames
    print(f"\nExtracting {extract_count} frames to {frames_dir}...")

    t_extract_start = time.monotonic()
    try:
        frame_count = extract_frames(video_path, frames_dir, max_frames)
    except RuntimeError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2
    t_extract_elapsed = time.monotonic() - t_extract_start
    print(f"  Extracted {frame_count} frames in {t_extract_elapsed:.1f}s")

    # Build JSON-lines input for worker
    alpha_dir = out_dir / "alpha"
    jsonlines = build_worker_requests(frames_dir, frame_count, mask_path, alpha_dir)
    worker_input = "\n".join(jsonlines) + "\n"
    print(f"\nRunning MatAnyone2 worker ({frame_count} frames)...")

    # Run worker
    t_worker_start = time.monotonic()
    try:
        worker_result = run_worker(runtime_python, worker_input, timeout=7200)
    except subprocess.TimeoutExpired:
        print("ERROR: worker timed out after 7200s", file=sys.stderr)
        return 3
    t_worker_elapsed = time.monotonic() - t_worker_start

    rc = worker_result["returncode"]
    responses = worker_result["responses"]
    print(f"  Worker returncode={rc}, {len(responses)} responses, {t_worker_elapsed:.1f}s")

    # Parse worker responses for status
    load_ok = False
    infer_result = None
    for resp in responses:
        cmd = resp.get("command", "")
        ok = resp.get("ok", False)
        if cmd == "load":
            load_ok = ok
            print(f"  load: ok={ok}")
        elif cmd == "infer_video":
            infer_result = resp.get("payload", {})
            infer_ok = ok
            frame_count_out = infer_result.get("successful_frame_count", 0)
            print(f"  infer_video: ok={ok}, frames={frame_count_out}")
        elif cmd == "shutdown":
            print(f"  shutdown: ok={ok}")

    # Check alpha outputs
    alpha_dir_path = Path(alpha_dir)
    alpha_files = sorted(alpha_dir_path.glob("alpha_*.png")) if alpha_dir_path.exists() else []
    print(f"  Alpha outputs: {len(alpha_files)} files")

    # Determine failure
    critical_failure = None
    if not load_ok:
        critical_failure = "worker load failed"
    elif infer_result and infer_result.get("status") != "ok":
        critical_failure = f"infer_video status: {infer_result.get('status')}"
    elif not alpha_files:
        critical_failure = "no alpha outputs produced"

    if critical_failure:
        print(f"\nFAILURE: {critical_failure}", file=sys.stderr)

    # Save stderr if present
    stderr_path = None
    if worker_result["stderr_preview"]:
        stderr_path = out_dir / "worker_stderr.txt"
        stderr_path.parent.mkdir(parents=True, exist_ok=True)
        stderr_path.write_text(worker_result["stderr_preview"], encoding="utf-8")

    # Write baseline manifest
    manifest: Dict[str, Any] = {
        "schema": "flux.ai_native_spikes.matanyone2_baseline.v1",
        "timestamp": datetime.now(timezone.utc).isoformat(),
        "runtime_id": args.runtime_id,
        "runtime_python": str(runtime_python),
        "inputs": {
            "video": {
                "path": str(video_path),
                "width": video_info["width"],
                "height": video_info["height"],
                "total_frames": video_info["nb_frames"],
                "r_frame_rate": video_info["r_frame_rate"],
            },
            "mask": {
                "path": str(mask_path),
                "width": mask_info["width"],
                "height": mask_info["height"],
                "mode": mask_info["mode"],
            },
        },
        "frame_extraction": {
            "max_frames": max_frames,
            "extracted": frame_count,
            "frames_dir": str(frames_dir),
            "elapsed_seconds": round(t_extract_elapsed, 3),
        },
        "worker_command": worker_result["command"],
        "worker_returncode": worker_result["returncode"],
        "worker_elapsed_seconds": worker_result["elapsed_seconds"],
        "worker_responses": responses,
        "worker_stderr_path": str(stderr_path) if stderr_path else None,
        "alpha_output_count": len(alpha_files),
        "alpha_dir": str(alpha_dir),
        "overall_status": "ok" if not critical_failure else "failed",
        "critical_failure": critical_failure,
    }

    out_dir.mkdir(parents=True, exist_ok=True)
    manifest_path = out_dir / "baseline_manifest.json"
    manifest_path.write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(f"\nBaseline manifest: {manifest_path}")
    print(f"Overall status: {manifest['overall_status']}")

    return 1 if critical_failure else 0


if __name__ == "__main__":
    raise SystemExit(main())
