# Planner Report

## Status
ready

## Rationale
This plan targets only the two regressions Nick reported in the already-approved T083 live workflow: box selection hit-testing and AI Panel Run using the one-shot/one-prompt path. Locator evidence is high-confidence, the persistent worker and multi-prompt conversion already exist, and the edit surface can stay limited to AI Paint overlay hit testing plus Flux AI Panel run/result handling.

# Task Packet

## User Goal
Fix T083 AI Paint/SAM3 workflow so AI Paint box selection is not junky/incorrect and AI Panel Run reuses the loaded persistent SAM3 worker with all enabled AI Paint prompts instead of launching the one-shot SAM3 probe for only the selected/newest prompt.

## Mode
general-coding

## Relevant Locations
- file: `Engine/AIPaint.cpp`
  symbol: `pointOnBox`
  approximate lines: 88-102
  stable anchor: `return std::abs(pos.x() - rect.left()) <= tolerance ||`
  reason: current hit-test accepts edge proximity OR `rect.normalized().contains(pos)`, making the whole box interior selectable and causing overlapping/interior clicks to feel wrong.
  confidence: high
- file: `Engine/AIPaint.cpp`
  symbol: `AIPaint::onOverlayPenDown`
  approximate lines: 715-767
  stable anchor: `it->type == AIPaintPromptType::Box && pointOnBox(...)`
  reason: select-mode prompt hit-testing uses canonical `pos` and ignores `viewportPos`; box hit-test behavior must be fixed here without changing prompt capture architecture.
  confidence: high
- file: `Engine/AIPaint.cpp`
  symbol: `AIPaint::drawOverlay`, `AIPaint::onOverlayPenMotion`, `AIPaint::onOverlayPenUp`
  approximate lines: 562-712, 789-828
  stable anchor: `const QPointF& /*viewportPos*/, const QPointF& pos`
  reason: overlay and prompt creation confirm canonical coordinate path; only validate/fix selection hit-testing unless a local coordinate bug is proven in these anchors.
  confidence: high
- file: `Gui/FluxAiPanel.cpp`
  symbol: `FluxAiPanel::buildAIPaintPromptsForSam`
  approximate lines: 663-685
  stable anchor: `QJsonArray FluxAiPanel::buildAIPaintPromptsForSam`
  reason: already converts all enabled include point/box prompts for SAM3; Run should use this instead of choosing one prompt.
  confidence: high
- file: `Gui/FluxAiPanel.cpp`
  symbol: `FluxAiPanel::runAIPaintLivePreview`
  approximate lines: 901-948
  stable anchor: `request.insert(QString::fromUtf8("prompts"), samPrompts);`
  reason: existing persistent-worker request pattern for all prompts should be reused/adapted for non-live Run.
  confidence: high
- file: `Gui/FluxAiPanel.cpp`
  symbol: `FluxAiPanel::onRunClicked`, `FluxAiPanel::startSam3StillMask`
  approximate lines: 1022-1185
  stable anchor: `_sam3Prompt = buildAIPaintPromptForSam(chosen, &message);` and `sam3_transformers_real_inference_probe.py`
  reason: current Run chooses selected/newest, stores one prompt, and starts one-shot provider process; replace this path for AI Paint Run with persistent worker all-prompts behavior.
  confidence: high
- file: `Gui/FluxAiPanel.cpp`
  symbol: `FluxAiPanel::processSam3WorkerLine`
  approximate lines: 1278-1346
  stable anchor: `} else if (command == QString::fromUtf8("infer_still")) {`
  reason: currently only live infer results update overlay; non-live worker `infer_still` must write/preview the Run manifest.
  confidence: high
- file: `Gui/FluxAiPanel.cpp`
  symbol: `FluxAiPanel::verifySam3ResultAndWriteManifest`
  approximate lines: 1348-1410
  stable anchor: `const QString key = _sam3Prompt.value(QString::fromUtf8("type")).toString();`
  reason: validates/writes manifest for a single selected prompt; must accept worker multi-prompt combined result and record all prompts.
  confidence: high
- file: `Gui/FluxAiPanel.h`
  symbol: `_sam3Prompt`, `_sam3WorkerPendingCommands`, `_livePreviewPendingRequestId`
  approximate lines: 60-145
  stable anchor: `QJsonObject _sam3Prompt;`
  reason: Run needs request/result state for an all-prompts worker inference without relying on a single selected prompt.
  confidence: high
- file: `tools/ai/sam3_transformers_worker.py`
  symbol: `Worker.infer_still`
  approximate lines: 104-173
  stable anchor: `prompts = request.get("prompts") or []`
  reason: read-only confirmation that persistent worker accepts prompt arrays and writes combined mask/result JSON.
  confidence: high

## Allowed Edit Files
- `Engine/AIPaint.cpp`
- `Gui/FluxAiPanel.h`
- `Gui/FluxAiPanel.cpp`

## Read-Only Context Files
- `Engine/AIPaint.h`
- `Engine/AIPaintContext.cpp`
- `Engine/AIPaintContext.h`
- `tools/ai/sam3_transformers_worker.py`
- `tools/ai/sam3_transformers_real_inference_probe.py`
- `tasks/T083-ai-paint-live-sam3-workflow-plan.md`

## Required Change
1. Fix box selection hit-testing in `Engine/AIPaint.cpp` narrowly:
   - Change `pointOnBox` so a box hit means near one of the four edges/corners within tolerance, not any point inside the normalized rectangle.
   - Keep reverse prompt iteration so top/newest prompt wins when edges overlap.
   - Keep using canonical overlay `pos`; do not switch to `viewportPos` unless local evidence in the listed anchors proves canonical conversion is wrong.
   - Preserve point selection, prompt creation, prompt store serialization, redraw behavior, and live preview overlay behavior.

2. Change AI Panel Run in `Gui/FluxAiPanel.cpp` to use all enabled AI Paint prompts through the persistent worker:
   - In `onRunClicked`, after saved-project/source/context validation, call `buildAIPaintPromptsForSam(prompts, &message)` and block with the existing error UI/log if it returns empty.
   - Store enough run state in `Gui/FluxAiPanel.h/.cpp` for a multi-prompt Run result (for example `_sam3Prompts` as `QJsonArray`, plus a non-live pending request id if needed). Do not rely on `_sam3Prompt` as the authoritative Run prompt for AI Paint multi-prompt runs.
   - If the persistent worker is already loaded, send an `infer_still` request matching the live-preview request shape but with the Run `absoluteRoot`, `relativeRoot`, `run_id`/task metadata if useful, `live_preview=false`, and the full prompt array.
   - If the persistent worker process is not started/loaded for Run, choose minimal-risk explicit behavior: start the persistent worker and send/load via the worker path (the worker `infer_still` already calls load if needed), then log/status that Run is loading SAM3 persistent worker before inference. Do not fall back to `sam3_transformers_real_inference_probe.py` for AI Paint point/box Run.
   - Avoid changing model manager policy, token handling, source capture, AI Panel prompt capture, native RotoPaint, or main comp viewer routing.

3. Make non-live persistent worker `infer_still` produce the same user-visible Run completion behavior as the old one-shot path:
   - Extend `processSam3WorkerLine` so an `infer_still` response that is not the current live preview is treated as a Run result when it matches the Run pending request/state.
   - Validate the worker result payload/result JSON for a combined or selected mask path, proof of nonzero pixels for at least the combined mask (or every returned successful prompt if the worker reports per-prompt proofs), and the expected output files under the Run output directory.
   - Refactor or add a narrow manifest writer so the Run manifest records all prompts (`prompts` array), source metadata/dimensions, combined selected mask path (prefer `mask_live_combined.png`/worker `selected_mask_path` if present), per-prompt masks/proofs from the worker result, output dir, result JSON path, and result manifest path.
   - Preview the selected/combined Run mask in the AI Work Viewer as the old Run path did; keep main comp viewer unchanged.
   - Leave the old one-shot `startSam3StillMask` path only if still needed for non-AI-Paint/text legacy paths; AI Paint point/box Run must not launch the one-shot probe.

## Non-Goals
- No native RotoPaint changes.
- No main comp viewer rewiring.
- No AI Panel prompt capture UI or prompt editing changes.
- No changes to live preview being overlay-based and worker-based.
- No broad SAM3 model manager/provider runtime redesign.
- No changing source frame export/capture semantics beyond what is necessary to validate existing Run context.
- No task status updates in `tasks/TASKS.md` or phase updates.

## Validation
Commands:
- `python3 -m py_compile tools/ai/sam3_transformers_worker.py tools/ai/sam3_transformers_real_inference_probe.py`
- `cmake --build /home/npittas/Flux/build --target Natron -j$(nproc)`

Manual GUI checks:
- In AI Work Viewer, create at least two overlapping AI Paint boxes and a point; in Select mode, clicking inside a box away from edges must not select the box, clicking/near-dragging the edge must select the intended newest/topmost edge, and point selection must still work.
- With SAM3 persistent worker loaded, click AI Panel Run with multiple enabled include prompts; logs must show persistent worker `infer_still`, not `sam3_transformers_real_inference_probe.py`, and generated manifest must contain a prompts array plus combined/selected mask path.
- With SAM3 persistent worker not loaded, click Run; behavior must explicitly start/load the persistent worker and run through worker `infer_still`, or fail with a clear load error. It must not silently fall back to one-shot probe for AI Paint point/box prompts.
- Confirm Run previews the combined/selected result in the AI Work Viewer and live preview overlay still updates from live requests.

Expected result:
Build and Python compile pass; AI Paint box selection no longer treats the whole interior as a hit; AI Panel Run uses all enabled prompts through the persistent worker and writes/previews a multi-prompt Run manifest.

## Stop Conditions
Stop and report if:
- target symbol is missing
- required fix exceeds allowed files
- validation cannot run
- existing architecture contradicts the requested change
- task requires product/design judgment not in packet
- canonical overlay coordinates are proven wrong and fixing them requires viewer/source transform API changes outside `Engine/AIPaint.cpp` and `Gui/FluxAiPanel.*`
- persistent worker cannot accept the existing prompt array/result contract without Python worker changes outside the allowed edit files
- manifest compatibility requires changing serialization/project history files outside the allowed edit files
- SAM3 local runtime/model is unavailable, after build/static validation is complete; report runtime validation as blocked by environment rather than adding fake success paths

## Planner Self-Check
- locator evidence sufficient: yes — user supplied exact high-confidence anchors and authorized snippets confirm the box hit-test and Run one-shot/one-prompt path.
- allowed edit files minimal and explicit: yes — only `Engine/AIPaint.cpp`, `Gui/FluxAiPanel.h`, and `Gui/FluxAiPanel.cpp` are needed for the narrow fix.
- read-only context minimal: yes — context is limited to AI Paint context/header, worker/probe contracts, and prior T083 plan.
- anchors/lines included: yes — all implementation locations include path, symbol, approximate lines, stable anchor, reason, and confidence.
- validation concrete: yes — build, Python compile, and specific GUI/runtime log/manifest checks are listed.
- parallelization decision explicit and safe: yes — single task; files/state overlap tightly, so parallelization would risk conflicting SAM3 run-state changes.
- non-goals and stop conditions sufficient: yes — prevent RotoPaint, main viewer, prompt capture, model-manager, and broad architecture creep.
- reviewer findings addressed, if revision: not applicable — no reviewer Blocker/Major findings were supplied for this revision.

## Required Return Contract
Return only a task-focused summary. Do not include transcript, tool logs, raw file dumps, large code blocks, or broad unrelated issues. Include status, files inspected/changed, validation evidence, blockers, and task-specific risks.
