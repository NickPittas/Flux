# Planner Report

## Status
ready

## Rationale
This plan is sufficient and scoped because the existing AI model/runtime tooling is manifest-driven, the current SAM3.1 probe already contains the needed honest local-load/prompt-proof structure, and Nick’s instruction changes only the immediate proof target from SAM3.1 to the Transformers `facebook/sam3` baseline. The task is limited to manifest/runtime/probe wiring and real CLI validation; it explicitly excludes GUI binding and the later SAM3.1 Meta/multiplex video propagation target.

# Task Packet

## User Goal
Switch the immediate proof target to SAM3 Transformers baseline per Nick’s instruction “Use sam 3”: add a `facebook/sam3` model/runtime/probe path, run real local inference against the existing proof asset, and fail honestly if local Transformers loading or text/box/point prompt APIs are unsupported.

## Mode
general-coding

## Relevant Locations
- file: `tools/ai/model_manifest.json`
  symbol: `models[]` entry for `sam31_sam3plus`
  approximate lines: 5-39
  stable anchor: `"id": "sam31_sam3plus"`
  reason: Current immediate SAM entry points at `facebook/sam3.1`; add a separate SAM3 Transformers entry rather than renaming/faking SAM3.1.
  confidence: high
- file: `tools/ai/provider_runtime_manifest.json`
  symbol: `runtimes[]` entry for `sam31`
  approximate lines: 5-21
  stable anchor: `"id": "sam31"`
  reason: Runtime manifest maps runtime ids to model ids, env dir, packages, self-check script, and CLI runtime commands; add a separate `sam3` runtime to avoid false SAM3.1 naming.
  confidence: high
- file: `tools/ai/flux_model_manager.py`
  symbol: `cmd_install`, `model_local_path`, `install_hf`, parser for `install/status/verify`
  approximate lines: 54-107, 161-230, 360-384
  stable anchor: `def install_hf(m,args,paths):`
  reason: Model manager resolves model install path from manifest id/revision and downloads Hugging Face snapshots using `allow_patterns`.
  confidence: high
- file: `tools/ai/flux_provider_runtime.py`
  symbol: `status_payload`, `cmd_install`, `cmd_self`, parser for `install/status/python/self-check`
  approximate lines: 42-168
  stable anchor: `def cmd_self(args):`
  reason: Provider runtime manager is manifest-driven and runs the configured self-check through the provider env python.
  confidence: high
- file: `tools/ai/sam31_real_inference_probe.py`
  symbol: whole probe; especially `MODEL_ID`, `RESULT_NAME`, `self_check_payload`, `transformers_backend`, `run_real`, `build_parser`
  approximate lines: 1-346
  stable anchor: `MODEL_ID = "sam31_sam3plus"`
  reason: Copy/adapt this honest real-inference probe to SAM3 Transformers naming and `facebook/sam3` local model id; do not keep SAM3.1 result/schema names.
  confidence: high
- file: `tools/ai/proof_assets/sam31_probe_source.ppm`
  symbol: existing proof asset
  approximate lines: n/a
  stable anchor: file path
  reason: Read-only validation asset for real proof command despite legacy `sam31` filename.
  confidence: high

## Allowed Edit Files
- `tools/ai/model_manifest.json`
- `tools/ai/provider_runtime_manifest.json`
- `tools/ai/sam3_transformers_real_inference_probe.py`

## Read-Only Context Files
- `tools/ai/flux_model_manager.py`
- `tools/ai/flux_provider_runtime.py`
- `tools/ai/sam31_real_inference_probe.py`
- `tools/ai/proof_assets/sam31_probe_source.ppm`

## Required Change
1. In `tools/ai/model_manifest.json`, add a new model entry, preferably before or near the existing SAM entry, with exact intent:
   - `id`: `sam3_transformers`
   - `display_name`: `SAM3 Transformers baseline`
   - `category`: `segmentation`
   - `role`: promptable still-image segmentation baseline proof for text/box/point prompts
   - `provider`: `Meta SAM / Hugging Face Transformers`
   - `runtime_type`: `python_cuda_huggingface`
   - `install_policy`: Hugging Face user download; do not claim bundling/default production readiness
   - `bundling_policy`: not bundled by default/user download only
   - `license_summary` and `warning_text`: preserve visible upstream license/access warning appropriate for Meta/Facebook SAM; do not weaken existing license cautions without evidence
   - `gated_token_requirement`: set according to actual Hugging Face access behavior if known from install; if not known, use a conservative value that causes the manager to request a token rather than silently failing
   - `default_enabled`: `false`
   - `default_installable`: `true`
   - `output_use_notes`: immediate still-image Transformers baseline for text/box/point proof only; SAM3.1 Meta/multiplex remains a later video propagation target
   - `source.type`: `huggingface`
   - `source.repo`: `facebook/sam3`
   - `source.revision`: pin to the exact resolved revision used by Hugging Face install if available during implementation; otherwise stop and report that the revision could not be pinned safely
   - `source.allow_patterns`: include `*.json`, `*.txt`, `*.md`, `*.py`, `*.safetensors`, `*.yaml`, `*.yml`; include tokenizer/processor assets if present and required by Transformers local load. Prefer `model.safetensors`; do not add `.pt`/`.pth` patterns unless the `facebook/sam3` repo demonstrably requires them for Transformers load.
2. In `tools/ai/provider_runtime_manifest.json`, add a separate runtime entry:
   - `id`: `sam3`
   - `display_name`: `SAM3 Transformers provider runtime`
   - `models`: `["sam3_transformers"]`
   - `env_dir_name`: `sam3`
   - keep Python support and CUDA index structure consistent with `sam31`
   - packages must include at least `torch`, `torchvision`, `transformers`, `accelerate`, `huggingface_hub`, `pillow`, `numpy`, `opencv-python-headless`; add only packages required by the actual Transformers SAM3 local load
   - `self_check.script`: `tools/ai/sam3_transformers_real_inference_probe.py`
   - `self_check.args`: `["--self-check", "--json"]`
   - do not rename or remove the existing `sam31` runtime; SAM3.1 remains a later target for Meta/multiplex video propagation.
3. Create `tools/ai/sam3_transformers_real_inference_probe.py` by adapting `tools/ai/sam31_real_inference_probe.py`:
   - Change `MODEL_ID` to `sam3_transformers`.
   - Change result file name to `sam3_transformers_real_inference_result.json`.
   - Change schemas/descriptions/user-facing strings from SAM3.1 to SAM3 Transformers.
   - Keep honest blocker behavior: never fabricate masks, never print fake success, and exit non-zero when local load or required prompt proofs fail.
   - Use local manifest-resolved model path through `flux_model_manager` exactly as the current probe does.
   - Prefer Transformers local load for `facebook/sam3` using `AutoProcessor`/`AutoModel`/documented SAM3 Transformers APIs, `local_files_only=True`, and `trust_remote_code=True` only if required by the repo.
   - Prove text, box, and point prompts against one still image. Required result JSON must include model id/repo/revision/local path, backend kind/capabilities, device/CUDA info, source image dimensions, prompt inputs, per-prompt proof records, and blockers/errors if any.
   - Write three real mask files on success: `mask_text.png`, `mask_box.png`, `mask_point.png` in the chosen output directory.
   - Treat unsupported text/box/point APIs as blockers, not skipped success.
   - Keep mask-paint and video propagation as `not_requested`/`not_proven`; do not implement video propagation here.

## Non-Goals
- No GUI work.
- No object binding, viewer integration, timeline/layer integration, or product UX decisions.
- No SAM3.1 Meta/multiplex video propagation work in this task; keep the existing `sam31_sam3plus` and `sam31` entries for that later target.
- No fake outputs, placeholder masks, synthetic success JSON, or broad refactors of the model/runtime managers.
- Do not edit source/product files outside the three allowed edit files.

## Validation
Commands:
- `cd /home/npittas/Flux && python3 tools/ai/flux_model_manager.py verify --offline`
- `cd /home/npittas/Flux && python3 tools/ai/flux_provider_runtime.py list --json`
- Model download/install: `cd /home/npittas/Flux && python3 tools/ai/flux_model_manager.py install sam3_transformers --yes`
- Runtime install: `cd /home/npittas/Flux && python3 tools/ai/flux_provider_runtime.py install sam3 --cuda cu128 --json`
- Runtime status: `cd /home/npittas/Flux && python3 tools/ai/flux_provider_runtime.py status sam3 --json`
- Runtime self-check: `cd /home/npittas/Flux && python3 tools/ai/flux_provider_runtime.py self-check sam3 --json`
- Resolve provider env python: `cd /home/npittas/Flux && SAM3_PY=$(python3 tools/ai/flux_provider_runtime.py python sam3) && "$SAM3_PY" --version`
- Real proof command using provider env python and existing proof asset: `cd /home/npittas/Flux && SAM3_PY=$(python3 tools/ai/flux_provider_runtime.py python sam3) && "$SAM3_PY" tools/ai/sam3_transformers_real_inference_probe.py --json --image tools/ai/proof_assets/sam31_probe_source.ppm --output-dir /tmp/flux_sam3_transformers_proof --text "red rectangle" --box 100,90,300,350 --point 200,220,1 --device auto`
- JSON/mask assertions: `cd /home/npittas/Flux && python3 - <<'PY'
import json
from pathlib import Path
p=Path('/tmp/flux_sam3_transformers_proof/sam3_transformers_real_inference_result.json')
data=json.loads(p.read_text())
assert data['model']['id']=='sam3_transformers'
assert data['model']['repo']=='facebook/sam3'
assert data.get('backend',{}).get('name') in {'transformers','sam3_transformers'}
for key in ('text','box','point'):
    proof=data['proofs'][key]
    assert proof['status']=='succeeded', (key, proof)
    mp=Path(proof['mask_path'])
    assert mp.is_file() and mp.stat().st_size>0, mp
    assert int(proof['nonzero_pixels'])>0, proof
for name in ('mask_text.png','mask_box.png','mask_point.png'):
    mp=Path('/tmp/flux_sam3_transformers_proof')/name
    assert mp.is_file() and mp.stat().st_size>0, mp
print('SAM3 Transformers proof assertions OK')
PY`

Expected result:
- Manifest verification succeeds offline.
- `flux_provider_runtime.py status sam3 --json` reports `ready: true` after model/runtime install, or reports explicit blockers without fake readiness.
- `self-check sam3 --json` emits JSON and exits 0 only when dependencies and `sam3_transformers` local model are available.
- Real proof command exits 0 only if local `facebook/sam3` Transformers loading succeeds and text, box, and point prompts each produce a non-empty real mask.
- Result JSON and all three mask PNGs exist under `/tmp/flux_sam3_transformers_proof` and pass the assertions.

## Stop Conditions
Stop and report if:
- target symbol is missing
- required fix exceeds allowed files
- validation cannot run
- existing architecture contradicts the requested change
- task requires product/design judgment not in packet
- `facebook/sam3` cannot be installed/pinned through Hugging Face with a `model.safetensors` Transformers-compatible snapshot
- `facebook/sam3` cannot be loaded locally through Transformers from the manifest-resolved path
- the detected API does not support text, box, and point prompts for still-image segmentation
- the proof asset `tools/ai/proof_assets/sam31_probe_source.ppm` is missing or unreadable
- any prompt proof would require fabricating output, accepting empty masks, or marking unsupported APIs as success
- implementing the proof requires GUI/object binding or SAM3.1 Meta/multiplex video propagation work

## Required Return Contract
Return only a task-focused summary. Do not include transcript, tool logs, raw file dumps, large code blocks, or broad unrelated issues. Include status, files inspected/changed, validation evidence, blockers, and task-specific risks.
