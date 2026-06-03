# Planner Report

## Status
ready

## Rationale
The reported mis-segmentation is isolated to the Python SAM3 inference path: Flux already exports the correct source-frame box coordinates and the persistent worker delegates all prompts to the shared `run_prompt()` helper. The smallest safe fix is to route `prompt_kind == "box"` through the already-loaded official tracker backend, mirroring the point path, while leaving GUI coordinate mapping, live overlay, persistent worker lifecycle, native RotoPaint, and main comp viewer code unchanged.

# Task Packet

## User Goal
Fix T083 SAM3 box prompt inference so an AI Paint box prompt produces an object-bound matte inside the user-drawn source-frame box instead of using concept/visual-prompt matching that may segment a similar object elsewhere.

## Mode
general-coding

## Relevant Locations
- file: `tools/ai/sam3_transformers_real_inference_probe.py`
  symbol: `transformers_backend()`
  approximate lines: 197-249
  stable anchor: `backend["capabilities"].update({"tracker_point_prompt": True, "point_prompt": True})`
  reason: Loads both concept and tracker SAM3 Transformers backends and advertises capabilities; box capability must reflect tracker-box availability, not concept-box routing only.
  confidence: high
- file: `tools/ai/sam3_transformers_real_inference_probe.py`
  symbol: `run_prompt()`
  approximate lines: 252-306
  stable anchor: `if prompt_kind == "point":`
  reason: Current point prompts use `Sam3TrackerProcessor/Sam3TrackerModel`, but box prompts fall through to `Sam3Processor/Sam3Model` with `input_boxes`, causing concept matching on a different similar object.
  confidence: high
- file: `tools/ai/sam3_transformers_worker.py`
  symbol: `Worker.normalize_prompt()` and `Worker.infer_still()`
  approximate lines: 103-178
  stable anchor: `proof = run_prompt(self.backend, kind, image, value, out_path, source_size, self.np, self.Image)`
  reason: Persistent GUI worker already normalizes box prompts to `[x1,y1,x2,y2]` and calls shared `run_prompt()`; likely no edit needed, but worker validation must prove the shared fix applies to persistent mode.
  confidence: high
- file: `Gui/FluxAiPanel.cpp`
  symbol: `FluxAiPanel::buildSourceBoxPrompt()`
  approximate lines: 946-980
  stable anchor: `prompt.insert(QString::fromUtf8("source_xyxy"), QJsonArray{ix1, iy1, ix2, iy2});`
  reason: Read-only evidence that GUI source coordinates are already canonicalized correctly; do not change this unless validation contradicts the provided evidence.
  confidence: high
- file: `Gui/FluxAiPanel.cpp`
  symbol: live/persistent infer request handling
  approximate lines: 930-941, 1113-1123, 1345-1360
  stable anchor: `request.insert(QString::fromUtf8("command"), QString::fromUtf8("infer_still"));`
  reason: Confirms live overlay and main run both use the persistent worker; fix must preserve this path.
  confidence: high
- file: `Gui/FluxAiPanel.h`
  symbol: SAM3 worker/process state members
  approximate lines: 58-143
  stable anchor: `QProcess* _sam3WorkerProcess;`
  reason: Read-only context for persistent worker state; no header/API change is expected.
  confidence: high
- file: `Engine/AIPaint.cpp`
  symbol: AI Paint prompt/tool definitions
  approximate lines: 49-69, 182-185
  stable anchor: `AIPaint::getPrompts() const`
  reason: Read-only confirmation that AI Paint prompt storage/tooling is separate from SAM3 backend inference; no native RotoPaint/AI Paint edit is in scope.
  confidence: medium

## Allowed Edit Files
- `tools/ai/sam3_transformers_real_inference_probe.py`

## Read-Only Context Files
- `tools/ai/sam3_transformers_worker.py`
- `Gui/FluxAiPanel.cpp`
- `Gui/FluxAiPanel.h`
- `Engine/AIPaint.cpp`
- `/home/npittas/Videos/For_Test/FluxGenerated/AI/live/sam3_transformers/current/sam3_transformers_real_inference_result.json`

## Required Change
Update `tools/ai/sam3_transformers_real_inference_probe.py` only:
1. In `transformers_backend()`, advertise tracker box support when `Sam3TrackerProcessor/Sam3TrackerModel` load successfully, e.g. add tracker-specific and public capability keys for boxes (`tracker_box_prompt`, `box_prompt`) alongside existing point capability. Preserve concept text capability for text prompts.
2. In `run_prompt()`, route `prompt_kind == "box"` through `backend["tracker_processor"]` / `backend["tracker_model"]`, not through `backend["processor"]` / `backend["model"]`.
3. Use the official tracker input shape for boxes consistent with the verified local experiment and the point path. Start from the existing point code and replace prompt args with the tracker processor's box arguments (expected pattern: `images=image`, `input_boxes=[[[prompt]]]`, `input_boxes_labels=[[1]]`, `return_tensors="pt"`; if the installed tracker API rejects `input_boxes_labels`, use signature-filtering or the API-supported tracker label kwarg, but do not fall back to concept `Sam3Processor/Sam3Model` for boxes).
4. Post-process tracker box masks with `processor.post_process_masks(outputs.pred_masks.cpu(), inputs["original_sizes"])[0]`, save with existing `save_mask()`, and raise a clear error such as `official tracker returned an empty box mask` for empty masks.
5. Keep text prompts on the concept backend. Keep point prompts behavior unchanged except for any small helper refactor needed to share tracker mask execution between points and boxes.
6. If tracker backend is unavailable for a box prompt, return `status="unsupported"`, `unsupported_by_detected_api=True`, and a tracker-specific blocker. Do not silently use concept box inference as fallback for AI Paint box prompts.
7. Preserve output schema fields, result JSON naming, mask paths, persistent worker behavior, live overlay behavior, and CLI arguments.

## Non-Goals
- Do not edit `Gui/FluxAiPanel.cpp`, `Gui/FluxAiPanel.h`, `Engine/AIPaint.cpp`, native RotoPaint, AI Paint prompt drawing/storage, main comp viewer, project serialization, or generated result assets.
- Do not change source coordinate transforms; provided evidence proves `source_xyxy [251,105,808,568]` is correct.
- Do not remove persistent worker or live preview/overlay behavior.
- Do not change text prompt concept inference semantics in this task.
- Do not add new model downloads, tokens, model manager behavior, or broad SAM3 architecture changes.

## Validation
Commands:
- `python3 -m py_compile tools/ai/sam3_transformers_real_inference_probe.py tools/ai/sam3_transformers_worker.py`
- `python3 tools/ai/sam3_transformers_real_inference_probe.py --json --image /tmp/Flux/sam3_preview/source_lightx2v_lora_rank_comparison.mp4_31_c65b377ad3a0.png --output-dir /tmp/Flux/t083_probe_box_tracker --prompt-kind box --source-width 2496 --source-height 1056 --box 251,105,808,568 --box-source-float-min 251.37350012371775,105.0623763697115 --box-source-float-max 807.8488624425584,567.4855647755087`
- `printf '%s\n' '{"id":"load","command":"load"}' '{"id":"infer","command":"infer_still","image":"/tmp/Flux/sam3_preview/source_lightx2v_lora_rank_comparison.mp4_31_c65b377ad3a0.png","output_dir":"/tmp/Flux/t083_worker_box_tracker","source_width":2496,"source_height":1056,"prompts":[{"id":"1","type":"box","source_xyxy":[251,105,808,568],"source_float_min":{"x":251.37350012371775,"y":105.0623763697115},"source_float_max":{"x":807.8488624425584,"y":567.4855647755087}}]}' '{"id":"shutdown","command":"shutdown"}' | python3 tools/ai/sam3_transformers_worker.py`
- If the source PNG above is missing, first inspect the current worker result JSON `source_image.path` for an available replacement. If no source PNG is available, stop and report that local image validation cannot run; do not substitute a different image unless Nick provides/approves it.
- Optional bbox check after either inference command: run a short local Python/PIL snippet against `/tmp/Flux/t083_probe_box_tracker/mask_*.png` or `/tmp/Flux/t083_worker_box_tracker/mask_1.png` to print the nonzero bbox; expected bbox must overlap the drawn top-left prompt region and must not be in the prior wrong top-right range `(1968,128)-(2495,424)`.

Expected result:
- Compile command passes.
- Probe result JSON reports box prompt `status: succeeded`, backend capabilities include tracker box support, and the saved mask bbox is in/near the top-left panda region (verified direct experiment was approximately `(315,129)-(826,527)`).
- Worker JSON-lines inference returns `ok: true`, writes `sam3_transformers_real_inference_result.json`, and `selected_mask_path` points to a mask whose bbox overlaps `source_xyxy [251,105,808,568]` rather than the top-right tile.

## Stop Conditions
Stop and report if:
- target symbol is missing
- required fix exceeds allowed files
- validation cannot run
- existing architecture contradicts the requested change
- task requires product/design judgment not in packet
- installed `Sam3TrackerProcessor` has no supported box-prompt API after checking its callable signature/docs locally
- box validation only works by falling back to concept `Sam3Processor/Sam3Model`
- the fixed mask bbox still lands in the top-right tile or outside the user box region

## Planner Self-Check
- locator evidence sufficient: yes — high-confidence anchors show the incorrect box fallthrough in shared `run_prompt()` and the worker/GUI paths that consume it.
- allowed edit files minimal and explicit: yes — only `tools/ai/sam3_transformers_real_inference_probe.py` is needed because persistent worker imports `run_prompt()`.
- read-only context minimal: yes — limited to worker call path, GUI coordinate/request path, AI Paint prompt context, and provided runtime result JSON.
- anchors/lines included: yes — each relevant location includes path, symbol/anchor, approximate lines, reason, and confidence.
- validation concrete: yes — includes py_compile, standalone probe, persistent worker JSON-lines inference, and bbox check against the existing source PNG when available.
- parallelization decision explicit and safe: yes — single task; no parallel split because one shared Python helper controls both probe and persistent worker behavior.
- non-goals and stop conditions sufficient: yes — explicitly excludes native RotoPaint, main viewer, GUI coordinate mapping, persistent worker removal, and concept text changes.
- reviewer findings addressed, if revision: not applicable — no reviewer findings supplied.

## Required Return Contract
Return only a task-focused summary. Do not include transcript, tool logs, raw file dumps, large code blocks, or broad unrelated issues. Include status, files inspected/changed, validation evidence, blockers, and task-specific risks.
