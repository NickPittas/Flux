#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SPIKE_DIR="$ROOT_DIR/tools/ai_native_spikes"
RESULTS_DIR="$SPIKE_DIR/results"
PYTHON_BIN="${PYTHON_BIN:-python3}"

usage() {
  cat <<'USAGE'
Usage:
  tools/ai_native_spikes/run_spike_matrix.sh phase0

Phase 0 writes environment/proof-asset manifests only. It does not run model
conversion/export and does not edit existing AI Paint/SAM3/MatAnyone2 code.

Optional environment:
  PYTHON_BIN=/path/to/python
  FLUX_MATANYONE2_CLIP=/path/to/short_clip.mov
  FLUX_MATANYONE2_FIRST_MASK=/path/to/first_frame_mask.png
USAGE
}

phase0() {
  mkdir -p "$RESULTS_DIR"
  echo "[phase0] repo: $ROOT_DIR"
  echo "[phase0] writing environment and asset manifests"
  (cd "$ROOT_DIR" && "$PYTHON_BIN" tools/ai_native_spikes/common/gpu_probe.py)

  echo "[phase0] provider runtime preflight"
  if command -v "$PYTHON_BIN" >/dev/null 2>&1; then
    for runtime in sam3 sam31; do
      set +e
      runtime_py="$((cd "$ROOT_DIR" && "$PYTHON_BIN" tools/ai/flux_provider_runtime.py python "$runtime") 2>/dev/null)"
      rc=$?
      set -e
      if [[ $rc -eq 0 && -n "$runtime_py" && -x "$runtime_py" ]]; then
        echo "[phase0] $runtime python: $runtime_py"
      else
        echo "[phase0] $runtime python unavailable or not executable: ${runtime_py:-<none>}"
      fi
    done
  fi

  echo "[phase0] outputs:"
  echo "  $RESULTS_DIR/environment.json"
  echo "  $RESULTS_DIR/assets_manifest.json"
}

case "${1:-}" in
  phase0)
    phase0
    ;;
  -h|--help|help|"")
    usage
    ;;
  *)
    echo "Unknown command: $1" >&2
    usage >&2
    exit 2
    ;;
esac
