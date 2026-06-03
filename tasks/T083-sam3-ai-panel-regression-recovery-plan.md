# Planner Report

## Status
ready

## Rationale
The regression is concentrated in the AI Panel SAM3 runtime path: the UI/workflow state already expects a persistent SAM3 worker, but the current helper implementations are stubs, preview is log-only, and source-frame repair must preserve timeline-to-source mapping rather than mask errors. This plan avoids git restoration and ad-hoc rewrite scripts, keeps native Roto/RotoPaint and custom AI mask copy workflows protected, and scopes recovery to the minimal C++ files needed for worker startup/protocol, preview restoration, source-frame context validation, and Qt6 signal hygiene.

# Task Packet

## User Goal
Recover T083 SAM3/AI Paint after the wrong SAM3 path regression: AI Paint Load/Unload SAM3 and Live Preview must work, AI Panel Run must actually invoke SAM3 and produce managed project-relative results/history, Preview Again must show results in the AI Work Viewer, Add/Replace must continue using visible custom-channel AI Mask Copy rows, and the main comp viewer/native RotoPaint path must remain untouched.

## Mode
general-coding

## Relevant Locations
- file: `Gui/FluxAiPanel.cpp`
  symbol: `FluxAiPanel::ensureSam3WorkerStarted`, `sendSam3WorkerRequest`, `processSam3WorkerLine`, `previewSam3RunResult`
  approximate lines: 1772-1794
  stable anchor: `SAM3 worker unavailable in this build path.`
  reason: These helpers are currently stubs/log-only; they are the direct cause of Load SAM3, Live Preview, Run, and Preview Again failures.
  confidence: high
- file: `Gui/FluxAiPanel.cpp`
  symbol: `FluxAiPanel::onRunClicked`
  approximate lines: 1495-1605
  stable anchor: `request.insert(QString::fromUtf8("command"), QString::fromUtf8("infer_still"));`
  reason: Current approved workflow already targets persistent worker `infer_still`; repair should implement the helpers it calls, not switch to one-shot unless the worker protocol proves unusable.
  confidence: high
- file: `Gui/FluxAiPanel.cpp`
  symbol: `FluxAiPanel::requestAIPaintSam3Load`, `requestAIPaintSam3Unload`, live preview sender
  approximate lines: 558-607 and 1198-1204
  stable anchor: `request.insert(QString::fromUtf8("command"), QString::fromUtf8("load"));`
  reason: AI Paint knobs route through these methods; they must use the same persistent worker path as Run.
  confidence: high
- file: `Gui/FluxAiPanel.cpp`
  symbol: `FluxAiPanel::onSam3ReadyReadStandardOutput`, `onSam3Finished`, `onSam3ErrorOccurred`, `cancelSam3`
  approximate lines: 1358-1468
  stable anchor: `processSam3WorkerLine(line);`
  reason: Existing process callbacks provide the integration points for JSON-line worker responses, request completion, error logging, and cancellation.
  confidence: high
- file: `Gui/FluxAiPanel.h`
  symbol: SAM3 worker state fields and helper declarations
  approximate lines: 74-80 and 132-153
  stable anchor: `_sam3WorkerPendingCommands`
  reason: Existing state fields are sufficient for request IDs, pending command tracking, loaded/loading state, and run pending ID; only add declarations/state if strictly needed.
  confidence: high
- file: `tools/ai/sam3_transformers_worker.py`
  symbol: `main`, `Worker.load`, `Worker.unload`, `Worker.infer_still`
  approximate lines: 61-180 and 221-255
  stable anchor: `elif command == "infer_still":`
  reason: Defines the JSON-line protocol to implement from C++: stdin requests with `id`/`command`, stdout responses containing `id` and payload.
  confidence: high
- file: `Gui/Gui05.cpp`
  symbol: `Gui::exportFluxSam3SourceFrameForSelectedLayer`, `Gui::exportFluxSam3SourceFrameForSourceContext`, AI Paint source context setters
  approximate lines: 747-752, 760-903, 1381-1540
  stable anchor: `FLUX-SAM3-A1 source-frame context clamped`
  reason: Current direct edit clamps stale frame 0; worker must refine this so context is refreshed from selected layer/current timeline frame before export and mismatches are logged/metadata-correct, not silently hidden.
  confidence: high
- file: `Gui/Gui.h`
  symbol: `Gui::previewFluxAiResultPngInWorkViewer`
  approximate lines: 90-93
  stable anchor: `previewFluxAiResultPngInWorkViewer`
  reason: `previewSam3RunResult()` must resolve project-relative masks under the project directory and call this method to restore Preview Again/final result preview while preserving main comp viewer isolation.
  confidence: high
- file: `Gui/FluxAiWorkerController.cpp`
  symbol: constructor QProcess connects
  approximate lines: 22-25
  stable anchor: `SIGNAL(errorOccurred(QProcess::ProcessError))`
  reason: Qt6 warning requires typed `QProcess::errorOccurred` connection if editing this file; worker errors must still be logged.
  confidence: medium
- file: `Gui/FluxTimeline.cpp`
  symbol: `addAIMaskCopyToSelectedLayer`, `replaceSelectedAIMaskCopy`
  approximate lines: 1139-1304
  stable anchor: `PLUGINID_FLUX_AI_MASK_COPY`
  reason: Protected adjacent workflow; do not replace it with Roto/layer-mask/Premult, and validate it remains visible custom-channel AI Mask Copy rows.
  confidence: high

## Allowed Edit Files
- `Gui/FluxAiPanel.cpp`
- `Gui/FluxAiPanel.h`
- `Gui/Gui05.cpp`
- `Gui/FluxAiWorkerController.cpp`

## Read-Only Context Files
- `Gui/Gui.h`
- `Gui/FluxAiWorkerController.h`
- `Gui/FluxTimeline.cpp`
- `Gui/FluxTimeline.h`
- `Engine/AIPaint.cpp`
- `Engine/AIPaint.h`
- `tools/ai/flux_provider_runtime.py`
- `tools/ai/sam3_transformers_worker.py`
- `tools/ai/sam3_transformers_real_inference_probe.py`
- `tasks/T083-orchestrator-recovery-source-of-truth.md`
- `tasks/T083-ai-paint-live-sam3-workflow-plan.md`
- `tasks/T083-aipaint-panel-inference-wiring-plan-v2.md`
- `tasks/T083-ai-result-history-preview-plan.md`
- `tasks/T083-ai-result-history-preview-fix-plan.md`
- `tasks/T083-ai-apply-mask-workflow-plan.md`

## Required Change
Implement as one sequential recovery unit because all runtime repairs converge on `Gui/FluxAiPanel.cpp` state and UI behavior.

1. **Do not use restoration shortcuts.** Do not run or propose `git restore/reset/checkout/revert`, and do not use Python/shell scripts to rewrite source files. Make exact source edits only in the allowed files.
2. **Keep AI Panel Run on the persistent worker path.** The current approved workflow and locator evidence show `onRunClicked()` already constructs an `infer_still` request, while AI Paint Load/Unload and Live Preview also need long-lived loaded state. Implement the existing persistent-worker helpers rather than reverting to the one-shot probe. One-shot probe code may remain as legacy fallback only if already present, but do not route approved Run/Load/Live Preview through it unless the Python worker protocol is contradicted by source evidence.
3. **Implement `ensureSam3WorkerStarted()`.** It must locate and start `tools/ai/sam3_transformers_worker.py` from the repo root/build runtime context, using the same Python/provider environment resolution patterns already present in `FluxAiPanel`/`FluxAiWorkerController`/`flux_provider_runtime.py`. Connect stdout, stderr, finished, and `QProcess::errorOccurred` with Qt6-safe typed connects. If the worker is already running, return true. On failure, return a specific message/log including attempted executable/script path.
4. **Implement `sendSam3WorkerRequest()`.** Assign a unique request id, add it to `_sam3WorkerPendingCommands`, serialize compact JSON plus newline to worker stdin, and return the id only after the write is accepted. Preserve `command` values `load`, `unload`, `infer_still`, `cancel`, `shutdown`, and Live Preview fields. If the process is missing or write fails, remove pending state and return empty with log/error.
5. **Implement `processSam3WorkerLine()`.** Parse JSON-line responses from `sam3_transformers_worker.py`. Match response ids to `_sam3WorkerPendingCommands`; update loading/unloading/loaded flags and AI Paint SAM3 status knob for `load`/`unload`; for `infer_still`, call `verifySam3ResultAndWriteManifest()`, set `_lastResultManifestProjectRelative`, refresh result history, preview the verified selected mask, clear `_sam3RunPendingRequestId`, and show success/failure status. For live preview requests, preview/update overlay through the existing AI Paint live-preview path and do not create fake masks. Log stderr/non-JSON lines without treating them as success.
6. **Restore result preview.** Replace log-only `previewSam3RunResult()` with safe project-relative resolution: sanitize `relativeMask`, join it under `project->getProjectPath()`, confirm the file exists and is non-empty, then call `_gui->previewFluxAiResultPngInWorkViewer(absolutePath, &diagnostics)`. Log diagnostics and do not touch the main comp viewer.
7. **Fix source-frame 0 correctly.** Before Run/Live Preview/source export, refresh the source context from the selected timeline layer and current timeline frame when possible. Compute `sourceFrame = timelineFrame - layer.timeOffset`, clamp only to the layer's original media range, update both timeline/source metadata when clamped, and log the correction (`requested`, `clamped`, `originalRange`). Do not simply hide invalid stored context; if the selected layer/reader no longer matches, block with a clear message. Preserve correct mapping for media range `[1,81]` so frame 0 is never exported for that source.
8. **Fix Qt6 QProcess warning.** In `Gui/FluxAiWorkerController.cpp`, replace old string-based QProcess signal connects with typed connects, especially `&QProcess::errorOccurred`; keep `onErrorOccurred(QProcess::ProcessError)` behavior and progress/error logging. Also use typed connects for any new SAM3 worker process connections in `FluxAiPanel.cpp`.
9. **Protect adjacent workflows.** Do not edit `FluxTimeline` Add/Replace code. Ensure AI Panel Add/Replace continues to call `addAIMaskCopyToSelectedLayer()` / `replaceSelectedAIMaskCopy()` with project-relative masks and creates/updates visible `FluxAIMaskCopy` effect rows with `ai_maskN` custom channels. Do not add Roto, layer-mask, or Premult nodes to the AI result path.
10. **Preserve AI Paint controls.** Prompt capture/select/delete/clear, Load/Unload SAM3 knobs, Live Preview overlay, and source/current-frame context must keep using AI Paint-owned state. Native Roto/RotoPaint masks are protected and must not be modified.

## Non-Goals
- No git restoration/reset/revert/checkout or broad working-tree cleanup.
- No ad-hoc Python/shell rewrite scripts for source edits.
- No changes to native Roto/RotoPaint mask behavior or protected Natron RotoPaint internals.
- No Roto/layer-mask/Premult AI mask application path.
- No redesign of the AI Panel UI beyond restoring broken runtime behavior.
- No changes to Python worker protocol unless C++ integration reveals a direct mismatch; if so, stop and report before editing Python files because they are read-only for this packet.
- No updates to task status files or phase documents in this recovery implementation packet.

## Validation
Commands:
- `cmake --build /home/npittas/Flux/build --target Natron -j$(nproc)`
- If the exact target name differs in this build tree, run the narrow equivalent existing GUI app build target and report the target used.
- `QT_QPA_PLATFORM=xcb /home/npittas/Flux/build/App/Natron`

Manual runtime GUI checks with real behavior and required proof artifacts:
- Before GUI validation, create `/tmp/flux-t083-sam3-recovery-proof/` and save screenshots/recordings plus relevant logs there. Do not mark GUI behavior validated unless the proof shows the actual UI control and resulting behavior, not logs alone.
- Load Nick's T083 project/media or a saved project with footage range starting at 1; create/select an AI Paint node on a footage layer. Save launch/runtime logs to `/tmp/flux-t083-sam3-recovery-proof/flux-runtime.log`.
- Verify prompt capture/select/delete/clear still works in AI Paint; capture UI proof if prompt controls are exercised during this validation.
- Click AI Paint `Load SAM3`; confirm no `SAM3 worker unavailable in this build path` and no Qt warning `QProcess::error(QProcess::ProcessError)`; status knob/UI shows loaded or an honest runtime/model blocker from the worker. Save screenshot/recording proof showing the Load SAM3 control and status under `/tmp/flux-t083-sam3-recovery-proof/`.
- Click AI Paint `Unload SAM3`; confirm status/UI changes through the persistent worker and no stale loaded state remains. Save screenshot/recording proof showing the Unload SAM3 control and status.
- Enable Live Preview and create/use a prompt; confirm the current-frame overlay path runs through the persistent worker and does not export source frame 0 for `[1,81]` footage. Save screenshot/recording proof showing the Live Preview control, overlay/result in the viewer/work area, and source-frame log lines with requested/clamped/originalRange.
- Click AI Panel `Run`; confirm SAM3 `infer_still` runs, writes project-relative `FluxGenerated/AI/.../result_manifest.json`, updates result history, and previews the selected mask in the AI Work Viewer. Save screenshot/recording proof showing the Run button, result history entry, and AI Work Viewer preview.
- Use result history `Preview Again`; confirm it displays the saved mask via `Gui::previewFluxAiResultPngInWorkViewer()` and does not alter the main comp viewer. Save screenshot/recording proof showing the Preview Again control, AI Work Viewer result, and unchanged main comp viewer.
- Use `Add Mask`; confirm a visible timeline AI Mask Copy effect row appears with `ai_maskN` custom channel target and no Roto/layer-mask/Premult node is created for this AI path. Save screenshot/recording proof showing the Add Mask action result, visible row, and custom-channel target.
- Select that AI Mask Copy row and use `Replace Mask`; confirm only its source read/mask changes and visible row remains selected/valid. Save screenshot/recording proof showing the selected row before/after Replace Mask.
- Confirm native Roto/RotoPaint masks still create/edit independently and were not repurposed for AI Panel Add/Replace; save proof if this is verified in the same GUI session.
- Confirm main comp viewer isolation explicitly: AI result preview and Preview Again must show in the AI Work Viewer while the main comp viewer remains on the comp output. Save side-by-side screenshot/recording proof under `/tmp/flux-t083-sam3-recovery-proof/`.

Expected result:
- Build succeeds.
- AI Paint Load/Unload SAM3 and AI Panel Run are no longer no-ops/stubs.
- Source-frame context uses valid media-range frames and records/logs corrected timeline/source metadata.
- Result history and Preview Again work through the AI Work Viewer.
- Add/Replace custom-channel path remains intact and main comp viewer isolation is preserved.

## Stop Conditions
Stop and report if:
- target symbol/anchor is missing or the current file no longer matches the described workflow.
- required fix exceeds the allowed edit files, especially if Python worker files or `FluxTimeline` must be changed.
- the Python worker protocol contradicts the expected `id`/`command` JSON-line contract.
- provider/Python environment resolution cannot be determined from allowed context without guessing.
- validation cannot run or the GUI cannot launch.
- source-frame correction would require product/design judgment about timeline mapping beyond `timelineFrame - timeOffset` clamped to original media range.
- existing architecture contradicts persistent worker use for Load/Unload/Live Preview/Run.
- preserving native Roto/RotoPaint or custom-channel Add/Replace conflicts with the runtime repair.

## Reviewer Checklist
Reviewer must not perform code-only review. Check actual application workflow and adjacent regressions against Nick's hierarchy:
- Confirm no git restoration or ad-hoc rewrite scripts were used/proposed by the worker.
- Inspect actual runtime path from AI Paint knob forwarding to `FluxAiPanel` worker helpers and from AI Panel Run to result manifest/history/preview.
- Require proof artifacts under `/tmp/flux-t083-sam3-recovery-proof/`; GUI checks are not validated without screenshots/recordings showing UI controls and resulting behavior, plus relevant logs where applicable.
- Verify Load SAM3 and Unload SAM3 with proof showing the controls/status and absence of the old unavailable-stub message/Qt warning.
- Verify Live Preview with proof showing the control, overlay/result behavior, and source-frame requested/clamped/originalRange log evidence.
- Verify Run/result history and Preview Again with proof showing the Run button, created history entry/manifest path, Preview Again control, AI Work Viewer preview, and main comp viewer unchanged.
- Verify Add Mask with proof showing the visible AI Mask Copy row and `ai_maskN` custom-channel target.
- Verify Replace Mask with proof showing the selected custom-channel row before/after replacement, with only its source read/mask updated.
- Verify prompt operations and source-frame mapping in code and, where possible, live GUI behavior.
- Confirm native Roto/RotoPaint masks are untouched.
- Confirm AI Add/Replace uses visible custom-channel `FluxAIMaskCopy`/`ai_maskN` rows only, with no Roto/layer-mask/Premult path.
- Confirm main comp viewer is not used for AI result preview, with explicit proof of main-comp-viewer isolation.
- Confirm Qt6 QProcess warning is fixed with typed `errorOccurred` connects.

## Planner Self-Check
- locator evidence sufficient: yes — high-confidence anchors identify the stubbed helpers, Run path, AI Paint forwarding, source-frame export, preview API, worker protocol, and protected Add/Replace path.
- allowed edit files minimal and explicit: yes — edits are limited to AI Panel runtime, source-frame context, and QProcess signal hygiene files.
- read-only context minimal: yes — context files are only the protected adjacent workflows, Python protocol/runtime sources, and T083 source-of-truth plans.
- anchors/lines included: yes — each relevant location includes path, symbol, approximate lines, stable anchor, reason, and confidence.
- validation concrete: yes — build command plus explicit GUI/runtime checks with expected behavior and required screenshots/recordings/log proof under `/tmp/flux-t083-sam3-recovery-proof/` are specified.
- parallelization decision explicit and safe: yes — single sequential task because `FluxAiPanel.cpp` state and workflow validation are shared; parallel edits would interfere.
- non-goals and stop conditions sufficient: yes — restoration shortcuts, Python rewrites, Roto/Premult drift, broad redesign, and scope expansion are blocked.
- reviewer findings addressed, if revision: yes — added screenshot/recording/log proof requirements for Load/Unload SAM3, Live Preview overlay, Run/result history, Preview Again, Add Mask, Replace Mask, and main-comp-viewer isolation without broadening implementation scope.

## Required Return Contract
Return only a task-focused summary. Do not include transcript, tool logs, raw file dumps, large code blocks, or broad unrelated issues. Include status, files inspected/changed, validation evidence, blockers, and task-specific risks.