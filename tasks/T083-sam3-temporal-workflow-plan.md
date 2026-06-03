# Planner Report

## Status
ready

## Rationale
This plan scopes T083 temporal work to the first safe implementation unit only: prove whether the local Transformers SAM3 installation exposes an official temporal/video tracking API, or produce a structured blocker if it does not. The allowed edit surface is limited to the existing SAM3 Python worker/probe files; GUI, comp viewer, render-thread, generated-media integration, and fake propagation are explicitly out of scope.

# Task Packet

## User Goal
Create the first T083 SAM3 temporal workflow implementation step: a temporal API proof only. The worker must prove or structured-block the existence and usability of official `Sam3TrackerVideoProcessor` / `Sam3TrackerVideoModel` Transformers APIs for temporal/video propagation. Do not implement GUI workflow, fake propagation, render-thread Python/CUDA, or main comp viewer integration.

## Mode
general-coding

## Relevant Locations
- file: `tasks/T083-ai-matte-depth.md`
  symbol: `Architecture Summary`, `T083-5 — SAM3.1 viewer-selection base matte/video mask node`, `Restart Protocol`
  approximate lines: 12-35, 100-111, 180-186
  stable anchor: `CUDA-first external Python worker launched out-of-process from Flux`
  reason: T083 source of truth: external Python worker, viewer prompt ownership, model/security constraints, and SAM3 video mask milestone.
  confidence: high
- file: `tasks/TASKS.md`
  symbol: `T083 | AI matte/mask generation and video depth tools`
  approximate lines: 152-153
  stable anchor: `Approved direction: CUDA-first external Python worker`
  reason: Confirms T083 is in progress and records approved high-level direction.
  confidence: high
- file: `tools/ai/sam3_transformers_real_inference_probe.py`
  symbol: `transformers_backend`
  approximate lines: 197-248
  stable anchor: `from transformers import Sam3Model, Sam3Processor, Sam3TrackerModel, Sam3TrackerProcessor, pipeline`
  reason: Existing SAM3 backend discovery already proves still-image concept/tracker APIs and records blockers/capabilities; temporal proof belongs next to this backend discovery.
  confidence: high
- file: `tools/ai/sam3_transformers_real_inference_probe.py`
  symbol: `run_prompt`
  approximate lines: 252-323
  stable anchor: `def run_prompt(backend: dict[str, Any], prompt_kind: str, image: Any, prompt: Any, out_path: Path, source_size: tuple[int, int], np: Any, Image: Any)`
  reason: Existing prompt execution is still-image only; temporal proof must not masquerade as propagation through repeated still calls.
  confidence: high
- file: `tools/ai/sam3_transformers_real_inference_probe.py`
  symbol: `run_real` / unsupported video reporting
  approximate lines: 325-376
  stable anchor: `result["unsupported"]["video_propagation_or_later_frame_correction"]`
  reason: Current probe explicitly marks video propagation as not proven; the task must replace/extend this with an honest temporal API proof or blocker.
  confidence: high
- file: `tools/ai/sam3_transformers_worker.py`
  symbol: `Worker.infer_still`
  approximate lines: 129-182
  stable anchor: `def infer_still(self, request: dict[str, Any]) -> dict[str, Any]:`
  reason: Existing long-lived worker supports still inference only; any worker temporal command must be a proof endpoint, not production propagation.
  confidence: high
- file: `tools/ai/sam3_transformers_worker.py`
  symbol: `main` command dispatch
  approximate lines: 203-218
  stable anchor: `elif command == "infer_still":`
  reason: If adding a JSON-lines temporal proof command, command dispatch belongs here.
  confidence: high

## Allowed Edit Files
- `tools/ai/sam3_transformers_real_inference_probe.py`
- `tools/ai/sam3_transformers_worker.py`

## Read-Only Context Files
- `tasks/T083-ai-matte-depth.md`
- `tasks/TASKS.md`
- `plans/PHASES.md`
- `ARCHITECTURE.md`
- `/home/npittas/Flux/AGENTS.md`
- `/home/npittas/.pi/agent/COLLABORATION.md`

## Required Change
Implement a narrow temporal API proof in the existing SAM3 Transformers probe/worker:

1. In `tools/ai/sam3_transformers_real_inference_probe.py`, add detection for official Transformers temporal/video classes named `Sam3TrackerVideoProcessor` and `Sam3TrackerVideoModel` without inventing substitute behavior.
   - Import/detect these classes defensively so environments lacking them return structured blockers instead of crashing.
   - Report a clear backend capability such as `video_tracker_api`, `video_propagation_or_later_frame_correction`, or equivalent in existing JSON result structures.
   - If the classes load from the local manifest-resolved SAM3 model path, inspect their callable signatures and record supported temporal inputs/prompts in JSON proof metadata.
   - If an actual minimal video/frame-folder invocation can be made safely through the detected official API, run only a tiny proof and write real result metadata. If the official call shape is unclear or unsupported, do not guess; return `status: blocked` / `unsupported_by_detected_api: true` with the class names, signatures inspected, and exact runtime blocker.
   - Preserve the existing still-image prompt behavior and JSON schemas as much as possible; add new fields compatibly rather than breaking existing callers.

2. Add a CLI proof path to the probe for temporal/video API checking.
   - Reuse existing `--video` if practical, or add a clearly named option such as `--temporal-api-proof`.
   - The command must succeed only when the official temporal API is actually proven usable; otherwise it must exit with a non-zero blocked/unsupported result while still writing structured JSON.
   - Do not implement frame-by-frame still-mask loops, optical-flow hacks, XMem/RVM helpers, dummy masks, or fabricated propagation.

3. In `tools/ai/sam3_transformers_worker.py`, expose the same temporal proof through the long-lived JSON-lines worker only as a proof/status command.
   - Accept a command name such as `temporal_api_proof` or `infer_temporal_proof`.
   - Return structured JSON with `status`, `backend`, `capabilities`, `blockers`, and any proof metadata from the probe.
   - Do not add production video generation, generated-media writes, GUI callbacks, render-thread work, or main comp viewer integration.

4. Keep Python/CUDA isolation intact.
   - All model loading/proof code remains in the external Python scripts.
   - Do not edit C++/Qt files.
   - Do not move PyTorch/CUDA into Flux/Natron render or GUI threads.

5. Be honest in result wording.
   - If `Sam3TrackerVideoProcessor` / `Sam3TrackerVideoModel` are unavailable in the installed Transformers version, the output must name that exact blocker.
   - If classes import but model loading or invocation fails, the output must include the exception type/message and mark temporal propagation unproven.
   - Existing still tracker classes `Sam3TrackerProcessor` / `Sam3TrackerModel` do not count as temporal/video propagation proof by themselves.

## Non-Goals
- No GUI, AI panel, viewer toolbar, overlay, prompt UX, or main comp viewer work.
- No production generated-media workflow, project save/reopen, nodegraph node, mask application, or timeline integration.
- No fake temporal propagation by looping still-image inference across frames.
- No render-thread, in-process Natron/Qt, or C++ PyTorch/CUDA integration.
- No model-manager, installer, token, license, or download-flow changes.
- No MatAnyone2, DepthCrafter, BiRefNet, ViTMatte, RVM, XMem2, or depth work.

## Validation
Commands:
- `python3 -m py_compile tools/ai/sam3_transformers_real_inference_probe.py tools/ai/sam3_transformers_worker.py`
- `python3 tools/ai/sam3_transformers_real_inference_probe.py --self-check --json`
- `python3 tools/ai/sam3_transformers_real_inference_probe.py --temporal-api-proof --json --output-dir /tmp/flux-sam3-temporal-proof` (or the exact implemented equivalent; if a video/frame-folder is required, use a tiny local test clip/frame folder and document the path)
- `printf '{"id":"status-1","command":"status"}\n{"id":"proof-1","command":"temporal_api_proof","output_dir":"/tmp/flux-sam3-temporal-worker-proof"}\n{"id":"shutdown-1","command":"shutdown"}\n' | python3 tools/ai/sam3_transformers_worker.py`

Expected result:
- Compile passes.
- Self-check remains valid for the current environment.
- Temporal proof command returns either a real official `Sam3TrackerVideoProcessor` / `Sam3TrackerVideoModel` proof, or a structured blocked/unsupported JSON result naming the missing/unusable official temporal API.
- Worker command returns the same honest temporal capability/blocker metadata without crashing.
- Existing still-image CLI/worker behavior is not regressed.

## Stop Conditions
Stop and report if:
- target symbol is missing: existing probe/worker anchors listed above cannot be found
- required fix exceeds allowed files: temporal proof requires C++/Qt, GUI, model manager, installer, generated media, or new helper modules
- validation cannot run: Python execution/compilation is unavailable, or a required local model/video artifact is missing and no structured-block path is possible
- existing architecture contradicts the requested change: proof would require in-process render-thread Python/CUDA or fake propagation
- task requires product/design judgment not in packet: naming/UX/workflow decisions beyond a proof endpoint are needed
- official temporal API is undocumented/ambiguous enough that invoking it would require guessing; return signatures and a structured blocker instead

## Planner Self-Check
- locator evidence sufficient: yes — implementation files and anchors are from existing SAM3 worker/probe plus T083 source-of-truth docs.
- allowed edit files minimal and explicit: yes — only the two Python SAM3 files are editable.
- read-only context minimal: yes — project/task source-of-truth files plus required agent instructions only.
- anchors/lines included: yes — every relevant implementation/context location includes file, symbol/anchor, approximate lines, reason, and confidence.
- validation concrete: yes — compile, self-check, CLI temporal proof, and worker JSON-lines proof commands are specified.
- parallelization decision explicit and safe: yes — single task; both allowed edit files are coupled by probe/worker API and should be edited by one worker to avoid command/schema drift.
- non-goals and stop conditions sufficient: yes — they block GUI scope creep, fake propagation, render-thread CUDA, and unapproved model/workflow expansion.
- reviewer findings addressed, if revision: not applicable — no reviewer findings were supplied for this new plan file.

## Required Return Contract
Return only a task-focused summary. Do not include transcript, tool logs, raw file dumps, large code blocks, or broad unrelated issues. Include status, files inspected/changed, validation evidence, blockers, and task-specific risks. If blocked, include the structured temporal API blocker and exact classes/signatures/errors inspected.
