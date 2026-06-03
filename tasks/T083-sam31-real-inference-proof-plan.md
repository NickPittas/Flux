# Planner Report

## Status
ready

## Rationale
This plan is sufficient and scoped because it creates one non-GUI, independently verifiable SAM3.1 proof task: add a dedicated real-inference probe that reuses the existing model manifest/path logic, validates the Python/runtime dependencies up front, and produces real mask media plus JSON evidence or stops without placeholder success.

# Task Packet

## User Goal
Prove SAM3.1 real inference outside the GUI first. Use SAM3.1 as the primary target; verify text, box, and point prompt inference; investigate paint/mask prompt and later-frame refinement/propagation only if the installed SAM API actually supports them. Produce real mask media files and result JSON. Do not use the existing no-op worker as success proof.

## Mode
general-coding

## Relevant Locations
- file: `tasks/T083-restart-real-inference-plan.md`
  symbol: T083 restart source of truth
  approximate lines: whole file
  stable anchor: `SAM3.1`
  reason: Establishes SAM3.1-first restart direction, no placeholder success, real media/result gates, and licensing/access as technical availability only.
  confidence: high
- file: `tools/ai/model_manifest.json`
  symbol: `models[]` entry with id `sam31_sam3plus`
  approximate lines: start of file, SAM3.1 model object
  stable anchor: `"id": "sam31_sam3plus"`
  reason: Provides repo `facebook/sam3.1`, revision `daa63191845a41281374e725f4c9e51c7a824460`, runtime type, warning text, and allowed model files.
  confidence: high
- file: `tools/ai/flux_model_manager.py`
  symbol: `flux_paths()`, `default_manifest_path()`, `load_manifest()`, `find_model()`, `model_local_path()`, `is_installed()`
  approximate lines: top-level path/model helper functions near file start
  stable anchor: `def model_local_path(m, paths):`
  reason: Existing canonical model path logic must be reused instead of duplicating install path construction.
  confidence: high
- file: `tools/ai/flux_ai_worker.py`
  symbol: no-op worker bridge
  approximate lines: whole file
  stable anchor: `Flux AI no-op worker bridge for T083-1B`
  reason: Explicit negative evidence: this placeholder writes `placeholder.txt` and must not be invoked or accepted as proof.
  confidence: high
- file: `tools/ai/proof_assets/sam31_probe_source.ppm`
  symbol: raster proof asset
  approximate lines: whole file
  stable anchor: `Flux SAM3.1 proof asset`
  reason: Concrete 640x480 RGB PPM validation asset with a red rectangle at roughly x=90..310, y=80..360 for the required text/box/point proof command. Read-only validation input; do not modify during implementation.
  confidence: high

## Allowed Edit Files
- `tools/ai/sam31_real_inference_probe.py`

## Read-Only Context Files
- `tasks/T083-restart-real-inference-plan.md`
- `tools/ai/model_manifest.json`
- `tools/ai/flux_model_manager.py`
- `tools/ai/flux_ai_worker.py`
- `tools/ai/proof_assets/sam31_probe_source.ppm`

## Required Change
Create `tools/ai/sam31_real_inference_probe.py` as a standalone CLI proof script for real SAM3.1 inference outside the GUI.

The script must:
1. Load the SAM manifest entry `sam31_sam3plus` using `flux_model_manager.default_manifest_path()`, `load_manifest()`, `find_model()`, `flux_paths()`, `model_local_path()`, and `is_installed()`; do not hardcode the model store beyond reporting the resolved installed path.
2. Validate runtime before inference and exit nonzero with a clear JSON/error message if any required runtime is unavailable: `torch`, `transformers`, `huggingface_hub`, and image I/O support. Current default Python is known to lack `torch`, `transformers`, `huggingface_hub`, and `cv2`; PIL/numpy are present. Do not silently install dependencies.
3. Refuse to use `tools/ai/flux_ai_worker.py` or any no-op/placeholder path. Generated outputs must be real image masks and JSON evidence, never `placeholder.txt` or contract-only manifests.
4. Accept CLI arguments for:
   - input image path
   - output directory
   - text prompt
   - box prompt coordinates
   - point prompt coordinates and labels
   - optional paint/mask prompt input path
   - optional video/frame-folder input for propagation/refinement investigation
   - optional `--device cuda|cpu|auto` with CUDA preferred when available
5. Load the local installed SAM3.1/SAM3+ model from the resolved model path. If the local path is missing, incomplete, incompatible with the available libraries, gated, or otherwise unusable, STOP and report the technical blocker; do not download or modify installer behavior in this task.
6. Probe the actual SAM3.1 API available from the installed model/repo/runtime. Implement only prompt paths supported by that API. Required proof attempts:
   - text prompt on still image
   - box prompt on still image
   - point prompt on still image, including positive/negative labels if the API supports labels
7. Investigate paint/mask prompt and later-frame refinement/propagation:
   - If the API exposes mask/paint prompt input, run it against a real mask prompt and emit real output.
   - If the API exposes video propagation/refinement/stateful sessions, run the smallest real frame/video proof and emit real output.
   - If either path is unsupported or unclear, record `unsupported_by_detected_api` / `not_proven` in result JSON and exit successfully only if the required text/box/point proofs succeeded; do not fake support.
8. Write output files under the requested output directory:
   - one real mask media file per successful prompt proof, preferably PNG (`mask_text.png`, `mask_box.png`, `mask_point.png`, plus optional mask/video outputs)
   - optional overlay/contact-sheet PNGs if easy with PIL/numpy
   - `sam31_real_inference_result.json` containing schema/version, timestamp, Python executable/version, dependency versions, device/CUDA details, manifest model id/repo/revision, resolved local model path, input paths, prompt payloads, output file paths, mask dimensions/nonzero pixel counts, scores/object IDs/boxes when provided by the model, unsupported prompt paths, and errors if any
9. Ensure outputs are real by checking each successful mask exists, is readable, has image dimensions matching or traceably related to the source, and has nonzero foreground pixels unless the model explicitly returns an empty mask with score metadata.
10. Keep the script task-local and implementation minimal. Avoid GUI, nodegraph, installer, task status, phase, or worker protocol changes.

## Non-Goals
- No GUI edits.
- No nodegraph/Natron integration.
- No installer/model-manager changes unless the script reports a runtime/setup blocker.
- No edits to `tools/ai/flux_ai_worker.py`; it is placeholder evidence only.
- No fake placeholder outputs, dummy masks, random masks, or success based on dependency/import checks alone.
- No licensing-based deprioritization/blocking; token/access/license acceptance is only a technical availability condition for installed model access.

## Validation
Commands:
- `cd /home/npittas/Flux && python3 tools/ai/sam31_real_inference_probe.py --help`
- `cd /home/npittas/Flux && python3 tools/ai/sam31_real_inference_probe.py --self-check --json`
- If self-check reports all required runtime and installed model are available, run this concrete real image proof against the required raster repo asset:
  `cd /home/npittas/Flux && python3 tools/ai/sam31_real_inference_probe.py --image tools/ai/proof_assets/sam31_probe_source.ppm --output-dir /tmp/flux-sam31-proof --text "red rectangle" --box 100,90,300,350 --point 200,220,1 --device auto --json`
- Inspect result files:
  `test -s /tmp/flux-sam31-proof/sam31_real_inference_result.json`
  `python3 - <<'PY'
import json
from pathlib import Path
from PIL import Image
p=Path('/tmp/flux-sam31-proof/sam31_real_inference_result.json')
data=json.loads(p.read_text())
assert data['model']['id']=='sam31_sam3plus'
for key in ('text','box','point'):
    r=data['proofs'][key]
    assert r['status']=='succeeded', (key, r)
    img=Image.open(r['mask_path'])
    assert img.size[0] > 0 and img.size[1] > 0
    assert r.get('nonzero_pixels', 0) > 0 or r.get('empty_mask_returned_by_model') is True
print('SAM3.1 proof JSON/media OK')
PY`

Expected result:
- `--help` works without importing heavy ML modules unnecessarily.
- `--self-check --json` reports dependency versions, CUDA/device status, manifest revision, resolved local model path, and either `ready: true` or a nonzero/blocked status explaining the missing runtime/model issue.
- Real proof command produces `sam31_real_inference_result.json` and real mask PNG files for text, box, and point prompts.
- Paint/mask prompt and video propagation/refinement are either proven with real outputs or explicitly recorded as unsupported/not proven by the detected API.

## Stop Conditions
Stop and report if:
- target manifest id `sam31_sam3plus` is missing or its repo/revision differs from the locator evidence without an explicit orchestrator update
- the installed model path is missing or lacks usable model files/metadata
- `torch`, `transformers`, `huggingface_hub`, PIL, or numpy are unavailable in the runtime selected for the proof
- CUDA is required by the detected SAM runtime and unavailable
- the installed SAM3.1 API cannot be loaded locally from the manifest-resolved path
- text, box, or point prompt inference cannot be implemented against the actual API
- the change requires editing files outside `tools/ai/sam31_real_inference_probe.py`
- the only possible success path would use `flux_ai_worker.py`, placeholders, dummy masks, or synthetic proof outputs
- validation cannot run on a real image asset
- product/API behavior requires judgment beyond recording detected support vs unsupported/not proven

## Required Return Contract
Return only a task-focused summary. Do not include transcript, tool logs, raw file dumps, large code blocks, or broad unrelated issues. Include status, files inspected/changed, validation evidence, blockers, and task-specific risks.
