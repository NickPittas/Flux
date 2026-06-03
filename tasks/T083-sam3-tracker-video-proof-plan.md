# Planner Report

## Status
ready

## Rationale
The first SAM3 image tracker proof has shipped as a separate script, so the smallest safe follow-up is one new tracker-video proof script that uses only the official Hugging Face `Sam3TrackerVideoModel`/`Sam3TrackerVideoProcessor` interactive video API. This keeps the existing SAM3 PCS image text/box proof and the shipped image tracker proof intact, avoids API mixing, and gives the worker an honest validation target against Nick’s real test video or an extracted short frame folder.

# Task Packet

## User Goal
Add a narrow SAM3 TrackerVideo real-inference proof for interactive video PVS/tracking using the official Hugging Face `facebook/sam3` tracker-video API. Keep the existing SAM3 PCS text/box image proof and shipped SAM3 image tracker proof unchanged.

## Mode
general-coding

## Relevant Locations
- file: `tools/ai/sam3_tracker_real_inference_probe.py`
  symbol: `MODEL_ID`, `RESULT_NAME`, `self_check_payload`, `tracker_api_status`, `load_backend`, `run_real`, `build_parser`
  approximate lines: 1-253
  stable anchor: `from transformers import Sam3TrackerModel, Sam3TrackerProcessor`
  reason: Shipped/reviewed image tracker proof. Use only as structural reference for manifest lookup, dependency checks, CLI/result JSON style, honest blockers, device selection, and mask output handling; do not edit or import this script for video tracking behavior.
  confidence: high
- file: `tools/ai/sam3_transformers_real_inference_probe.py`
  symbol: `MODEL_ID`, `RESULT_NAME`, `self_check_payload`, `run_real`, `build_parser`
  approximate lines: 1-346
  stable anchor: `RESULT_NAME = "sam3_transformers_real_inference_result.json"`
  reason: Existing SAM3 PCS image text/box/point proof. It must remain intact and must not be used as a fallback for tracker-video success.
  confidence: high
- file: `tools/ai/model_manifest.json`
  symbol: `models[]` entry for `sam3_transformers`
  approximate lines: 5-39
  stable anchor: `"id": "sam3_transformers"`
  reason: Existing `facebook/sam3` local snapshot/revision used by SAM3 proof scripts. The new tracker-video probe should reuse this manifest-resolved local path if it supports the official tracker-video classes.
  confidence: high
- file: `tools/ai/provider_runtime_manifest.json`
  symbol: `runtimes[]` entry for `sam3`
  approximate lines: 5-42
  stable anchor: `"script": "tools/ai/sam3_transformers_real_inference_probe.py"`
  reason: Existing SAM3 provider runtime/env and self-check remain owned by the PCS proof. Use its Python env via `flux_provider_runtime.py`; do not replace the runtime self-check for this task.
  confidence: high

## Allowed Edit Files
- `tools/ai/sam3_tracker_video_real_inference_probe.py`

## Read-Only Context Files
- `tools/ai/sam3_tracker_real_inference_probe.py`
- `tools/ai/sam3_transformers_real_inference_probe.py`
- `tools/ai/model_manifest.json`
- `tools/ai/provider_runtime_manifest.json`

## Required Change
Create `tools/ai/sam3_tracker_video_real_inference_probe.py` as a standalone real-inference video tracking proof.

1. Use only the official Hugging Face tracker-video API documented for `facebook/sam3`:
   - `Sam3TrackerVideoModel`
   - `Sam3TrackerVideoProcessor`
   - `processor.init_video_session(...)`
   - `processor.add_inputs_to_inference_session(...)`
   - `model(..., frame_idx=...)`
   - `processor.propagate_in_video_iterator(...)`
   - `processor.post_process_masks(...)`
2. Do not import, call, or fall back to `Sam3Model`, `Sam3Processor`, `Sam3TrackerModel`, `Sam3TrackerProcessor`, `Sam3VideoModel`, or `Sam3VideoProcessor` to claim tracker-video success.
3. Reuse manifest id `sam3_transformers` and its local `facebook/sam3` path through the same manifest helper pattern used by the existing probes. Use `local_files_only=True`; use `trust_remote_code=True` only if the installed official Transformers/HF loading path requires it.
4. Keep the probe honest:
   - no fake masks
   - no synthetic success JSON
   - no accepting empty masks/tracks as success
   - exit non-zero with explicit blockers if dependencies, local model files, official tracker-video classes, video/frame inputs, or non-empty masks are unavailable
5. Support real input from either:
   - `/home/npittas/Videos/For_Test/Video_For_Test.mov`, decoded by the script into a short temporary/existing frame set using available local dependencies such as OpenCV, or
   - an explicit extracted short frame folder supplied by CLI.
6. Required CLI shape:
   - `--self-check`
   - `--json`
   - `--video PATH`
   - `--frames-dir PATH`
   - `--output-dir PATH`
   - `--point x,y,label`
   - `--box x1,y1,x2,y2` only if the official tracker-video processor accepts it for interactive initialization
   - `--start-frame N`
   - `--max-frames N` with a small default such as 8-16 frames
   - `--device auto|cuda|cpu`
   At least one of `--video` or `--frames-dir` is required unless `--self-check` is used.
7. Result artifacts:
   - result JSON name: `sam3_tracker_video_real_inference_result.json`
   - result JSON schema: `org.flux.ai.sam3-tracker-video-real-inference-result.v1`
   - self-check schema: `org.flux.ai.sam3-tracker-video-real-inference-self-check.v1`
   - write per-frame real mask PNGs, for example `mask_000000.png`, under the output directory for every successful propagated frame
8. Result JSON must include:
   - model id/repo/revision/local path
   - selected device and CUDA info
   - dependency versions
   - backend class names showing `Sam3TrackerVideoModel` and `Sam3TrackerVideoProcessor`
   - input source path/type, decoded frame count, frame dimensions, start frame, max frames
   - prompt inputs
   - session/proof status
   - per-frame mask paths, mask sizes, and nonzero pixel counts
   - aggregate successful frame count and total nonzero pixels
   - blockers/errors when blocked
9. Preserve existing files exactly:
   - do not edit `tools/ai/sam3_transformers_real_inference_probe.py`
   - do not edit `tools/ai/sam3_tracker_real_inference_probe.py`
   - do not edit `tools/ai/model_manifest.json`
   - do not edit `tools/ai/provider_runtime_manifest.json`
10. If the installed `facebook/sam3` snapshot or Transformers package lacks the official tracker-video API/signature, stop with an honest blocker in JSON/stdout and non-zero exit instead of modifying other scripts or manifests.

## Non-Goals
- No UI integration, viewer integration, timeline/layer integration, or product UX work.
- No text/video PCS proof in this task.
- No changes to existing SAM3 PCS text/box image proof.
- No changes to existing SAM3 image tracker proof.
- No provider/runtime manager refactor and no runtime self-check replacement.
- No manifest edits unless a later approved task explicitly asks for them.
- No API mixing or fallback from tracker-video classes to image PCS/image tracker/video PCS classes.
- No fake masks, placeholder outputs, or treating empty masks as success.

## Validation
Commands:
- `cd /home/npittas/Flux && python3 tools/ai/flux_model_manager.py verify --offline`
- `cd /home/npittas/Flux && python3 tools/ai/flux_provider_runtime.py list --json`
- `cd /home/npittas/Flux && python3 tools/ai/flux_provider_runtime.py status sam3 --json`
- `cd /home/npittas/Flux && python3 tools/ai/flux_provider_runtime.py self-check sam3 --json`
- `cd /home/npittas/Flux && SAM3_PY=$(python3 tools/ai/flux_provider_runtime.py python sam3) && "$SAM3_PY" tools/ai/sam3_tracker_video_real_inference_probe.py --self-check --json`
- `cd /home/npittas/Flux && SAM3_PY=$(python3 tools/ai/flux_provider_runtime.py python sam3) && "$SAM3_PY" tools/ai/sam3_tracker_video_real_inference_probe.py --json --video /home/npittas/Videos/For_Test/Video_For_Test.mov --output-dir /tmp/flux_sam3_tracker_video_proof --point 200,220,1 --start-frame 0 --max-frames 8 --device auto`
- `cd /home/npittas/Flux && python3 - <<'PY'
import json
from pathlib import Path
p=Path('/tmp/flux_sam3_tracker_video_proof/sam3_tracker_video_real_inference_result.json')
data=json.loads(p.read_text())
assert data['schema']=='org.flux.ai.sam3-tracker-video-real-inference-result.v1'
assert data['model']['id']=='sam3_transformers'
assert data['model']['repo']=='facebook/sam3'
assert 'Sam3TrackerVideoModel' in data.get('backend',{}).get('model_class','')
assert 'Sam3TrackerVideoProcessor' in data.get('backend',{}).get('processor_class','')
assert data['proof']['status']=='succeeded', data.get('blockers') or data.get('errors')
frames=data['proof'].get('frames', [])
assert frames, data['proof']
assert data['proof'].get('successful_frame_count', 0) == len(frames)
assert data['proof'].get('total_nonzero_pixels', 0) > 0
for frame in frames:
    assert int(frame['nonzero_pixels']) > 0, frame
    mp=Path(frame['mask_path'])
    assert mp.is_file() and mp.stat().st_size > 0, mp
print('SAM3 tracker-video proof assertions OK')
PY`

Expected result:
- Existing SAM3 PCS and image tracker probes remain byte-for-byte untouched unless the worker reports a blocker before editing.
- Existing manifest/runtime commands still pass and the `sam3` runtime self-check still validates the existing PCS proof.
- New tracker-video self-check exits 0 only when `Sam3TrackerVideoModel`/`Sam3TrackerVideoProcessor`, dependencies, and local `facebook/sam3` model files are available.
- Real tracker-video proof exits 0 only when the official tracker-video API produces real non-empty per-frame masks/tracks from Nick’s test video or an explicit frame folder.
- If local API/model/video decoding is unsupported, the probe exits non-zero with a clear blocker and no fake success artifacts.

## Stop Conditions
Stop and report if:
- target symbol is missing
- required fix exceeds allowed files
- validation cannot run
- existing architecture contradicts the requested change
- task requires product/design judgment not in packet
- adding the proof requires editing existing PCS/image tracker probes or manifests
- `Sam3TrackerVideoModel` or `Sam3TrackerVideoProcessor` is unavailable in the installed runtime
- the official tracker-video session/call signature cannot be determined safely from installed Transformers/docs/runtime introspection
- `/home/npittas/Videos/For_Test/Video_For_Test.mov` is missing/unreadable and no frame folder is provided
- decoding/extracting a short frame sequence fails with available local dependencies
- producing success would require PCS/image tracker API fallback or fabricated masks
- propagated masks are empty for every frame

## Required Return Contract
Return only a task-focused summary. Do not include transcript, tool logs, raw file dumps, large code blocks, or broad unrelated issues. Include status, files inspected/changed, validation evidence, blockers, and task-specific risks.
