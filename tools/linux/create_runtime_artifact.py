#!/usr/bin/env python3
"""Build, deploy, and package a Flux Linux runtime artifact.

This is a developer helper for the common loop:
  1. build the app/renderer from the current checkout;
  2. refresh the local Flux runtime install under ~/.local/share/Flux;
  3. package that installed runtime into a tarball for the artifact installer.

It intentionally delegates packaging to tools/linux/flux-linux-setup.sh so the
artifact manifest and install-time verification stay in one backend.
"""

from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
SETUP = ROOT / "tools" / "linux" / "flux-linux-setup.sh"


def run(cmd: list[str], *, input_text: str | None = None) -> None:
    print("+", " ".join(cmd), flush=True)
    try:
        subprocess.run(
            cmd,
            cwd=ROOT,
            input=input_text,
            text=True,
            check=True,
            env={**os.environ, "FLUX_SETUP_INTERNAL_DISPATCH": "1"},
        )
    except subprocess.CalledProcessError as exc:
        raise SystemExit(exc.returncode) from exc


def output_name() -> str:
    try:
        short = subprocess.check_output(
            ["git", "rev-parse", "--short", "HEAD"], cwd=ROOT, text=True
        ).strip()
    except subprocess.CalledProcessError:
        short = "unknown"
    return f"flux-linux-x86_64-{short}-runtime.tar.gz"


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as fp:
        for chunk in iter(lambda: fp.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--output",
        type=Path,
        default=ROOT / "dist" / output_name(),
        help="artifact tarball path (default: dist/flux-linux-x86_64-<commit>-runtime.tar.gz)",
    )
    parser.add_argument(
        "--copy-to",
        type=Path,
        default=None,
        help="optional directory to copy the finished artifact into, e.g. /mnt/Dagobah",
    )
    parser.add_argument(
        "--skip-build",
        action="store_true",
        help="reuse the existing build/App/Natron and build/Renderer/NatronRenderer outputs",
    )
    parser.add_argument(
        "--skip-deploy",
        action="store_true",
        help="reuse the existing ~/.local/share/Flux runtime install",
    )
    parser.add_argument(
        "--jobs",
        type=str,
        default=str(os.cpu_count() or 4),
        help="parallel build jobs passed to cmake --build (default: CPU count)",
    )
    args = parser.parse_args(argv)

    if not SETUP.is_file():
        raise SystemExit(f"Missing setup backend: {SETUP}")

    output = args.output if args.output.is_absolute() else ROOT / args.output
    output.parent.mkdir(parents=True, exist_ok=True)

    if not args.skip_build:
        run(["cmake", "--build", "build", "--target", "Natron", "NatronRenderer", "-j", args.jobs])

    if not args.skip_deploy:
        # yes: refresh existing Flux-managed files; no: skip default AI model install.
        run([str(SETUP), "__flux_setup_action", "deploy-runtime"], input_text="y\nn\n")

    run([str(SETUP), "__flux_setup_action", "package-artifact", str(output)])

    digest = sha256(output)
    print(f"ARTIFACT={output}")
    print(f"SHA256={digest}")

    if args.copy_to is not None:
        copy_dir = args.copy_to if args.copy_to.is_absolute() else ROOT / args.copy_to
        copy_dir.mkdir(parents=True, exist_ok=True)
        copied = copy_dir / output.name
        shutil.copy2(output, copied)
        copied_digest = sha256(copied)
        if copied_digest != digest:
            raise SystemExit(f"Copied artifact hash mismatch: {copied}")
        print(f"COPIED={copied}")
        print(f"COPIED_SHA256={copied_digest}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
