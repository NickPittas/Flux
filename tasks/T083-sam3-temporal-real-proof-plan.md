# Planner Report

## Status
ready

## Rationale
The current temporal blocker is narrowly isolated: the Transformers temporal proof detects official tracker-video classes but stops before attempting a real minimal invocation, while `tools/ai/sam3_tracker_video_real_inference_probe.py` already contains an honest tiny-video propagation path. This packet first proves or blocks that existing invocation under provider Python, then ports only the successful invocation shape into the existing temporal proof and JSON-lines worker path without changing GUI behavior or restoring any concept-box fallback that could break box-mask correctness.

# Task Packet

## User Goal
Create the next temporal SAM3 implementation step: use the existing real tracker-video probe on a tiny clip/frame range with provider Python; if it succeeds, port/reuse the exact invocation into `temporal_api_proof` and the worker command; if it fails, capture the concrete blocker. Preserve Nick-approved still box tracker correctness and do not restore concept-box fallback.

## Mode
general-coding

## Relevant Locations
- file: `tools/ai/sam3_tracker_video_real_inference_probe.py`
  symbol: `run_real`, `load_backend`, `decode_frames`, `mask_to_png`, `extract_masks`, `normalize_masks_for_postprocess`, `tracker_video_api_status`
  approximate lines: 55-258
  stable anchor: `processor.init_video_session`, `processor.add_inputs_to_inference_session`, `model.propagate_in_video_iterator`, `post_process_masks`, `RESULT_NAME = "sam3_tracker_video_real_inference_result.json"`
  reason: existing honest real-video propagation probe likely has the exact official tracker-video invocation shape needed for temporal proof.
  confidence: high
- file: `tools/ai/sam3_transformers_real_inference_probe.py`
  symbol: `temporal_api_proof`, `run_temporal_api_proof`, `transformers_backend`
  approximate lines: 205-249, 267-306, 370-392
  stable anchor: `"proof": {"invocation_attempted": False` and `official temporal classes loaded, but this proof did not find a documented safe minimal invocation shape`
  reason: current temporal proof loads official classes but explicitly does not invoke propagation; update this only after the real video probe succeeds.
  confidence: high
- file: `tools/ai/sam3_transformers_worker.py`
  symbol: `Worker.temporal_api_proof`, `main`
  approximate lines: 182-212, 245-248
  stable anchor: `proof = temporal_api_proof(self.local_path, str(request.get("device") or self.device or "auto"), output_dir, Path(str(request["video"])) if request.get("video") else None)`
  reason: worker already delegates temporal proof to the probe function; ensure it exposes the same real invocation result and blockers.
  confidence: high

## Allowed Edit Files
- `tools/ai/sam3_transformers_real_inference_probe.py`
- `tools/ai/sam3_transformers_worker.py`
- `tools/ai/sam3_tracker_video_real_inference_probe.py`

## Read-Only Context Files
- `tools/ai/flux_provider_runtime.py` (for provider Python/status commands only; do not edit)

## Required Change
1. Resolve provider Python with `python3 tools/ai/flux_provider_runtime.py python sam3` and run `tools/ai/sam3_tracker_video_real_inference_probe.py` against a tiny real video/frame range before editing the temporal proof. Use a small `--max-frames` value, an existing tiny/short clip if available, or `/home/npittas/Videos/For_Test/Video_For_Test.mov` with a short frame window. Keep prompts in-bounds; prefer the probe defaults unless the clip dimensions require different coordinates.
2. If the tracker-video probe succeeds with non-empty propagated masks, port/reuse its exact official invocation sequence into `tools/ai/sam3_transformers_real_inference_probe.py::temporal_api_proof`:
   - load `Sam3TrackerVideoProcessor` and `Sam3TrackerVideoModel` with local files and trusted remote code if the successful probe required it;
   - decode/use the provided video or tiny frames exactly as proven;
   - initialize a video session, add point/optional box inputs, call model forward if required by the proven sequence, iterate `propagate_in_video_iterator`, postprocess masks, save proof mask PNGs, and record nonzero pixel counts;
   - set `proof.invocation_attempted = True`, `status = "succeeded"` only when at least one real non-empty propagated mask is written, and keep `fake_masks_generated = False`;
   - on any real API/runtime failure, return `status = "blocked"` with the concrete exception/blocker and `invocation_attempted` reflecting whether the attempt reached the API.
3. Ensure `run_temporal_api_proof` writes `sam3_transformers_temporal_api_proof_result.json` with the updated proof fields, and ensure `tools/ai/sam3_transformers_worker.py::Worker.temporal_api_proof` returns the same proof result through JSON-lines without swallowing blockers.
4. If the existing tracker-video real probe fails, do not guess or invent a new API shape. Leave product source changes limited to any small blocker-reporting fix required in the probe, and report the exact blocker/result artifact.
5. Preserve still-image tracker prompt behavior and box-mask correctness. Do not restore or add concept-box fallback in `transformers_backend` / `run_prompt`; do not change GUI, model manager, prompt serialization, or provider runtime behavior.

## Non-Goals
- No GUI changes.
- No concept-box fallback restoration or broad SAM backend redesign.
- No fake masks, synthetic success, or status `succeeded` without non-empty real mask artifacts.
- No model download/install workflow changes.
- No task status, phase, git, or generated media policy changes.

## Validation
Commands:
- `python3 -m py_compile tools/ai/sam3_tracker_video_real_inference_probe.py tools/ai/sam3_transformers_real_inference_probe.py tools/ai/sam3_transformers_worker.py`
- `python3 tools/ai/flux_provider_runtime.py status sam3 --json`
- `PROVIDER_PY=$(python3 tools/ai/flux_provider_runtime.py python sam3); "$PROVIDER_PY" tools/ai/sam3_tracker_video_real_inference_probe.py --video /home/npittas/Videos/For_Test/Video_For_Test.mov --start-frame 0 --max-frames 3 --output-dir /tmp/flux-sam3-tracker-video-real-proof --json`
- `PROVIDER_PY=$(python3 tools/ai/flux_provider_runtime.py python sam3); "$PROVIDER_PY" tools/ai/sam3_transformers_real_inference_probe.py --temporal-api-proof --video /home/npittas/Videos/For_Test/Video_For_Test.mov --device auto --output-dir /tmp/flux-sam3-temporal-proof --json`
- `PROVIDER_PY=$(python3 tools/ai/flux_provider_runtime.py python sam3); printf '%s\n' '{"id":"temporal-proof","command":"temporal_api_proof","video":"/home/npittas/Videos/For_Test/Video_For_Test.mov","device":"auto","output_dir":"/tmp/flux-sam3-temporal-worker-proof"}' '{"id":"shutdown","command":"shutdown"}' | "$PROVIDER_PY" tools/ai/sam3_transformers_worker.py`

Expected result:
Python compile passes. Provider status resolves or gives a concrete environment blocker. The standalone tracker-video probe either succeeds with non-empty `mask_*.png` files and `total_nonzero_pixels > 0`, or records a concrete blocker. If it succeeds, the updated temporal API proof and worker temporal command also report `status: succeeded`, `capabilities.video_tracker_api: true`, `capabilities.video_propagation_or_later_frame_correction: true`, `proof.invocation_attempted: true`, `proof.fake_masks_generated: false`, and non-empty real mask artifacts. If it fails, the implementation reports `blocked` with the exact blocker and no fake success.

## Stop Conditions
Stop and report if:
- target symbol is missing
- required fix exceeds allowed files
- validation cannot run
- existing architecture contradicts the requested change
- task requires product/design judgment not in packet
- `sam3_tracker_video_real_inference_probe.py` fails and the exact API invocation cannot be proven from its result
- making temporal proof succeed would require fake masks, remote downloads, provider-runtime changes, GUI changes, or restoring concept-box fallback
- the tiny clip/path is unavailable and no real substitute video/frame directory can be identified without broad repo or filesystem search

## Planner Self-Check
- locator evidence sufficient: yes; provided and verified anchors show current temporal proof stops before invocation and existing tracker-video probe contains real propagation calls.
- allowed edit files minimal and explicit: yes; only the two temporal/probe Python files plus optional helper extraction/fix in the existing tracker-video probe.
- read-only context minimal: yes; provider runtime is referenced only for validation command resolution.
- anchors/lines included: yes; relevant paths include symbols, approximate lines, stable anchors, reasons, and confidence.
- validation concrete: yes; includes compile, provider status, existing real probe run, updated temporal proof run, and worker JSON-lines temporal command.
- parallelization decision explicit and safe: yes; single task because edits and validation share the same SAM3 provider runtime/model/VRAM state and temporal proof files.
- non-goals and stop conditions sufficient: yes; explicitly blocks GUI scope, fake success, concept-box fallback restoration, provider changes, and unproven API guessing.
- reviewer findings addressed, if revision: not applicable; no reviewer findings supplied for this new plan.

## Required Return Contract
Return only a task-focused summary. Do not include transcript, tool logs, raw file dumps, large code blocks, or broad unrelated issues. Include status, files inspected/changed, validation evidence, blockers, and task-specific risks.
