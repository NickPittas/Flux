#!/usr/bin/env python3
"""Phase-0 GPU/runtime probe for Flux AI native-runtime spikes."""

from __future__ import annotations

from pathlib import Path

from io_contracts import RESULTS_DIR, collect_assets_manifest, collect_environment, write_json


def main() -> int:
    RESULTS_DIR.mkdir(parents=True, exist_ok=True)
    env_path = RESULTS_DIR / "environment.json"
    assets_path = RESULTS_DIR / "assets_manifest.json"
    write_json(env_path, collect_environment())
    write_json(assets_path, collect_assets_manifest())
    print(f"wrote {env_path}")
    print(f"wrote {assets_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
