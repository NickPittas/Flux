#!/usr/bin/env python3
"""Phase-1 SAM3 baseline runner.

Invokes tools/ai/sam3_transformers_real_inference_probe.py through the provider
runtime Python for point, box, and (optionally) text prompts against a proof
asset. Writes an immutable baseline_manifest.json with all results.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Dict, List, Optional

REPO_ROOT = Path(__file__).resolve().parents[3]
PROBE_SCRIPT = REPO_ROOT / "tools" / "ai" / "sam3_transformers_real_inference_probe.py"
RUNTIME_DISCOVERY = REPO_ROOT / "tools" / "ai" / "flux_provider_runtime.py"
DEFAULT_IMAGE = REPO_ROOT / "tools" / "ai" / "proof_assets" / "sam31_probe_source.ppm"


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


def ppm_dimensions(path: Path) -> Optional[Dict[str, int]]:
    """Read width/height from a PPM file header."""
    with path.open("rb") as f:
        magic = f.readline().strip()
        if magic not in {b"P3", b"P6"}:
            return None
        tokens: list[bytes] = []
        while len(tokens) < 2:
            line = f.readline()
            if not line:
                return None
            stripped = line.strip()
            if not stripped or stripped.startswith(b"#"):
                continue
            tokens.extend(stripped.split())
        return {"width": int(tokens[0]), "height": int(tokens[1])}


def run_probe(
    runtime_python: Path,
    device: str,
    image: Path,
    output_dir: Path,
    prompt_kind: str,
    source_width: int,
    source_height: int,
    point: Optional[str] = None,
    box: Optional[str] = None,
    text: Optional[str] = None,
) -> Dict[str, Any]:
    """Run one SAM3 probe invocation and return structured result."""
    cmd: List[str] = [
        str(runtime_python),
        str(PROBE_SCRIPT),
        "--json",
        "--device", device,
        "--image", str(image),
        "--output-dir", str(output_dir),
        "--prompt-kind", prompt_kind,
        "--source-width", str(source_width),
        "--source-height", str(source_height),
    ]
    if prompt_kind == "point" and point:
        cmd.extend(["--point", point])
    elif prompt_kind == "box" and box:
        cmd.extend(["--box", box])
    elif prompt_kind == "text" and text:
        cmd.extend(["--text", text])

    t0 = time.monotonic()
    proc = subprocess.run(
        cmd,
        cwd=str(REPO_ROOT),
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=600,
        check=False,
    )
    elapsed = time.monotonic() - t0

    parsed_json = None
    if proc.stdout.strip():
        try:
            parsed_json = json.loads(proc.stdout.strip())
        except json.JSONDecodeError:
            pass

    return {
        "prompt_kind": prompt_kind,
        "command": cmd,
        "returncode": proc.returncode,
        "elapsed_seconds": round(elapsed, 3),
        "stdout_path": None,
        "stderr_path": None,
        "parsed_json": parsed_json,
        "stderr_preview": proc.stderr[:2000] if proc.stderr else "",
    }


def main(argv: Optional[List[str]] = None) -> int:
    parser = argparse.ArgumentParser(description="Phase-1 SAM3 baseline runner")
    parser.add_argument("--device", default="cuda", help="Device (default: cuda)")
    parser.add_argument("--runtime-id", default="sam3", help="Provider runtime ID (default: sam3)")
    parser.add_argument("--image", default=str(DEFAULT_IMAGE), help="Input image path")
    parser.add_argument("--out", default=str(REPO_ROOT / "tools" / "ai_native_spikes" / "results" / "sam3"), help="Output directory")
    parser.add_argument("--prompt-set", default="default", help="Prompt set name (default: default)")
    parser.add_argument("--skip-text", action="store_true", help="Skip text prompt")
    args = parser.parse_args(argv)

    image_path = Path(args.image).resolve()
    out_dir = Path(args.out).resolve()

    if not image_path.exists():
        print(f"ERROR: image not found: {image_path}", file=sys.stderr)
        return 2
    if not PROBE_SCRIPT.exists():
        print(f"ERROR: probe script not found: {PROBE_SCRIPT}", file=sys.stderr)
        return 2

    # Discover runtime python
    print(f"Discovering runtime python for '{args.runtime_id}'...")
    try:
        runtime_python = discover_runtime_python(args.runtime_id)
    except RuntimeError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2
    print(f"Runtime python: {runtime_python}")

    # Probe image dimensions
    dims = ppm_dimensions(image_path)
    if dims is None:
        print(f"ERROR: could not read PPM dimensions from {image_path}", file=sys.stderr)
        return 2
    w, h = dims["width"], dims["height"]
    print(f"Image dimensions: {w}x{h}")

    # Build prompts based on dimensions
    center_x, center_y = w // 2, h // 2
    prompts = [
        {
            "kind": "point",
            "output_subdir": "point",
            "point": f"{center_x},{center_y},1",
        },
        {
            "kind": "box",
            "output_subdir": "box",
            "box": f"{w // 4},{h // 4},{3 * w // 4},{3 * h // 4}",
        },
    ]
    if not args.skip_text:
        prompts.append({
            "kind": "text",
            "output_subdir": "text",
            "text": "object",
        })

    # Run prompts sequentially
    results: List[Dict[str, Any]] = []
    manifest_start = time.monotonic()
    critical_failures: List[str] = []

    for prompt in prompts:
        kind = prompt["kind"]
        sub_out = out_dir / prompt["output_subdir"]
        print(f"\nRunning {kind} prompt -> {sub_out}")
        result = run_probe(
            runtime_python=runtime_python,
            device=args.device,
            image=image_path,
            output_dir=sub_out,
            prompt_kind=kind,
            source_width=w,
            source_height=h,
            point=prompt.get("point"),
            box=prompt.get("box"),
            text=prompt.get("text"),
        )
        # Save stdout/stderr to files
        if result["parsed_json"]:
            stdout_path = sub_out / "probe_stdout.json"
            stdout_path.parent.mkdir(parents=True, exist_ok=True)
            stdout_path.write_text(
                json.dumps(result["parsed_json"], indent=2, sort_keys=True) + "\n",
                encoding="utf-8",
            )
            result["stdout_path"] = str(stdout_path)
        if result["stderr_preview"]:
            stderr_path = sub_out / "probe_stderr.txt"
            stderr_path.parent.mkdir(parents=True, exist_ok=True)
            stderr_path.write_text(result["stderr_preview"], encoding="utf-8")
            result["stderr_path"] = str(stderr_path)

        results.append(result)
        rc = result["returncode"]
        status = result.get("parsed_json", {}).get("status", "unknown") if result.get("parsed_json") else "no_json"
        print(f"  returncode={rc}, status={status}, elapsed={result['elapsed_seconds']}s")

        if kind in ("point", "box") and rc != 0:
            critical_failures.append(f"{kind} prompt failed with returncode {rc}")
        elif kind == "text" and rc != 0:
            print(f"  text prompt failed (non-critical, recorded)")

    manifest_elapsed = time.monotonic() - manifest_start

    # Write baseline manifest
    manifest: Dict[str, Any] = {
        "schema": "flux.ai_native_spikes.sam3_baseline.v1",
        "timestamp": datetime.now(timezone.utc).isoformat(),
        "runtime_id": args.runtime_id,
        "runtime_python": str(runtime_python),
        "device": args.device,
        "image": {
            "path": str(image_path),
            "width": w,
            "height": h,
        },
        "prompt_set": args.prompt_set,
        "prompts": {
            "point": f"{center_x},{center_y},1",
            "box": f"{w // 4},{h // 4},{3 * w // 4},{3 * h // 4}",
            "text": None if args.skip_text else "object",
        },
        "results": results,
        "elapsed_seconds": round(manifest_elapsed, 3),
        "overall_status": "ok" if not critical_failures else "failed",
        "critical_failures": critical_failures if critical_failures else None,
    }

    out_dir.mkdir(parents=True, exist_ok=True)
    manifest_path = out_dir / "baseline_manifest.json"
    manifest_path.write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(f"\nBaseline manifest: {manifest_path}")
    print(f"Overall status: {manifest['overall_status']}")

    return 1 if critical_failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
