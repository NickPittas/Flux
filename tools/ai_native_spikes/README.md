# Flux AI Native Runtime Spikes

Standalone feasibility spikes for running the **actual current SAM3/SAM3.1 and MatAnyone2 models** through native/runtime export routes before any Flux OFX or render-path integration.

## Hard boundary

Do **not** edit or depend on mutable state from the existing working AI implementation:

- `Engine/AIPaint.*`
- `Engine/AIPaintContext.*`
- `Gui/FluxAiPanel.*`
- `tools/ai/sam3_transformers_worker.py`
- `tools/ai/sam3_transformers_real_inference_probe.py`
- `tools/ai/matanyone2_worker.py`
- current AI mask apply/copy paths

Those files are read-only baselines. All spike outputs stay under:

- `tools/ai_native_spikes/results/`
- `/tmp/flux-ai-native-spikes/`

## Phase 0 scope

Phase 0 only prepares reproducible spike infrastructure:

1. Capture environment and GPU/runtime facts.
2. Verify current provider runtime Python paths exist.
3. Verify proof assets and record dimensions/checksums.
4. Define result schemas and comparison utilities.
5. Prepare baseline command guidance.

No model conversion/export happens in Phase 0.

## GPU scheduling rule

Run SAM3 and MatAnyone2 conversion/export phases **sequentially on the same GPU** unless there is explicit GPU isolation (`CUDA_VISIBLE_DEVICES`, MIG, or separate GPUs). ONNX export and TensorRT builds can spike VRAM and poison the CUDA process after OOM.

## Current known assets

SAM3 still proof asset:

- `tools/ai/proof_assets/sam31_probe_source.ppm`
- Dimensions: probe at runtime; current repo asset is expected to be **640x480**.

MatAnyone2 requires a real clip and first-frame mask. Provide these via environment variables before Phase 1:

```bash
export FLUX_MATANYONE2_CLIP=/path/to/short_clip.mov
export FLUX_MATANYONE2_FIRST_MASK=/path/to/first_frame_mask.png
```

## Phase 0 command

```bash
./tools/ai_native_spikes/run_spike_matrix.sh phase0
```

Expected outputs:

- `tools/ai_native_spikes/results/environment.json`
- `tools/ai_native_spikes/results/assets_manifest.json`

## Phase 1 commands

Phase 1 runs immutable Python baselines using the current working implementations.
Run SAM3 and MatAnyone2 **sequentially** on the same GPU — do not overlap them.

### SAM3 still-image baseline

```bash
python3 tools/ai_native_spikes/sam3/sam3_baseline.py \
  --device cuda --runtime-id sam3
```

Output: `tools/ai_native_spikes/results/sam3/baseline_manifest.json`

### MatAnyone2 video baseline

```bash
python3 tools/ai_native_spikes/matanyone2/matanyone2_baseline.py \
  --runtime-id sam3 \
  --video /home/npittas/Videos/For_Test/lightx2v_lora_rank_comparison.mp4 \
  --first-frame-mask /home/npittas/Videos/For_Test/mask_1.png
```

Output: `tools/ai_native_spikes/results/matanyone2/baseline/baseline_manifest.json`

If the full 81-frame run is too slow or OOMs, retry with a subset:

```bash
python3 tools/ai_native_spikes/matanyone2/matanyone2_baseline.py \
  --runtime-id sam3 --max-frames 10 \
  --video /home/npittas/Videos/For_Test/lightx2v_lora_rank_comparison.mp4 \
  --first-frame-mask /home/npittas/Videos/For_Test/mask_1.png
```

## Correct SAM3 baseline dimensions

When using `tools/ai/proof_assets/sam31_probe_source.ppm`, use the actual probed dimensions, not stale sample values. If the asset is 640x480, prompt coordinates and `--source-width/--source-height` must match that space.

Example skeleton:

```bash
SAM3_PY="$(python3 tools/ai/flux_provider_runtime.py python sam3)"
test -x "$SAM3_PY"
"$SAM3_PY" tools/ai/sam3_transformers_real_inference_probe.py \
  --json --device cuda \
  --image tools/ai/proof_assets/sam31_probe_source.ppm \
  --output-dir /tmp/flux-ai-native-spikes/sam3_baseline_point \
  --prompt-kind point --point 320,240,1 \
  --source-width 640 --source-height 480
```

## SAM3 export probe (Phase 2 scaffolding)

Introspects the SAM3 Transformers model classes, signatures, processor methods,
and export blockers for single-frame guide-mask prompting (point/box).
No GPU-heavy conversion runs by default.

```bash
python3 tools/ai_native_spikes/sam3/sam3_export_probe.py \
  --runtime-id sam3 \
  --device cuda \
  --route introspect \
  --prompt-kind point
```

Routes:
- `introspect` — discover classes, signatures, versions, blocker analysis (default)
- `torchscript`, `torch-export`, `onnx` — deferred; records `not_implemented_pending_introspection`
- `all` — runs introspect only in first pass

Prompt kinds: `point` (default), `box`

Output: `tools/ai_native_spikes/results/sam3/export/sam3_export_report.json`

## MatAnyone2 export probe (Phase 3 scaffolding)

Introspects the MatAnyone2 model, InferenceCore, and temporal state to assess
export feasibility. No GPU-heavy conversion runs by default.

```bash
python3 tools/ai_native_spikes/matanyone2/matanyone2_export_probe.py \
  --runtime-id sam3 \
  --device cuda \
  --route introspect
```

Routes:
- `introspect` — import, class, signature, blocker analysis (default)
- `torchscript`, `torch-export`, `onnx` — deferred; records `not_implemented_pending_introspection`
- `all` — runs introspect; others are placeholders

Output: `tools/ai_native_spikes/results/matanyone2/export/matanyone2_export_report.json`

## Future phases

- Phase 1: generate immutable Python baselines.
- Phase 2: SAM3 export/runtime matrix.
- Phase 3: MatAnyone2 export/runtime matrix.
- Phase 4: standalone native C++ harness only after export succeeds.
- Phase 5: OFX/render-path integration only after Nick approves a specific native route.
