# Planner Report

## Status
ready

## Rationale
This fix plan is limited to the two implementation-review blockers in the already-implemented T083 AI Paint → AI Panel wiring: add a retained-source/export-metadata mismatch guard and restore the pre-existing project-relative `FluxGenerated/AI/<task>/<model>/<run>/` SAM output root policy. The work is confined to `FluxAiPanel` state/logic and does not alter prompt ownership, RotoPaint, provider runtime scripts, or task/model UX.

# Task Packet

## User Goal
Fix implementation reviewer blockers for T083 AI Paint → AI Panel inference wiring before ship: reject stale/mismatched source-frame exports and restore the approved generated-media output root policy for SAM3 still-mask runs.

## Mode
general-coding

## Relevant Locations
- file: `Gui/FluxAiPanel.h`
  symbol: `FluxAiPanel` selected-source and source-frame state
  approximate lines: 65-119
  stable anchor: `_sourceFrameMetadata`, `_sourceLayerIndex`, `_sourceLayerName`, `_sourceReaderLabel`, `_sourceSourceFrame`
  reason: Existing retained source context fields and exported metadata state that must be compared after source-frame export.
  confidence: high
- file: `Gui/FluxAiPanel.cpp`
  symbol: `FluxAiPanel::refreshSourceFrameMetadata`
  approximate lines: 512-545
  stable anchor: `source frame exported for AI Paint prompt inference`
  reason: Current export accepts any metadata after basic file/dimension checks; add explicit comparison against retained selected-source context here or immediately after this function returns.
  confidence: high
- file: `Gui/FluxAiPanel.cpp`
  symbol: `FluxAiPanel::validateSourceCaptureContext`
  approximate lines: 547-584
  stable anchor: `exported source metadata dimensions are stale`
  reason: Current validation checks viewer identity/input and dimensions only; extend with selected layer/reader/source-frame metadata checks or call a new narrow helper from this path.
  confidence: high
- file: `Gui/FluxAiPanel.cpp`
  symbol: `FluxAiPanel::onRunClicked`
  approximate lines: 742-773
  stable anchor: `const QString relativeRoot = QString::fromUtf8("generated/ai/%1/").arg(runId);`
  reason: Current run root violates approved/pre-existing generated-media policy; replace with existing `FluxGenerated/AI/<task>/<model>/<run>/` project-relative root construction and pass unchanged to `startSam3StillMask()`.
  confidence: high
- file: `Gui/FluxAiPanel.cpp`
  symbol: `FluxAiPanel::startSam3StillMask`
  approximate lines: 776-889
  stable anchor: `QDir().mkpath(absoluteRoot);` and `_sam3RelativeRoot = relativeRoot;`
  reason: Should receive the restored absolute/project-relative root unchanged; do not redesign runtime invocation.
  confidence: high
- file: `Gui/FluxAiWorkerController.cpp`
  symbol: `FluxAiWorkerController::startNoopJob`
  approximate lines: 84-93
  stable anchor: `output_path_policy`, `project_relative_fluxgenerated_ai`
  reason: Read-only evidence of the existing generated-media policy contract that SAM3 should preserve.
  confidence: high
- file: `tasks/T083-ai-matte-depth.md`
  symbol: Nick-approved T083 decisions
  approximate lines: 5-23
  stable anchor: `Unsaved projects must force Save As before AI generation`
  reason: Confirms generated output must be project-relative and unsaved projects must not write AI generation output.
  confidence: high
- file: `tasks/T083-aipaint-panel-inference-wiring-plan-v2.md`
  symbol: original approved plan validation/output requirements
  approximate lines: full task packet
  stable anchor: `Preserve project-relative generated output and Save As preflight`
  reason: Source-of-truth plan being fixed; constraints remain in force except as narrowed by this reviewer-blocker fix.
  confidence: high

## Allowed Edit Files
- `Gui/FluxAiPanel.h`
- `Gui/FluxAiPanel.cpp`

## Read-Only Context Files
- `tasks/T083-aipaint-panel-inference-wiring-plan-v2.md`
- `Gui/FluxAiWorkerController.h`
- `Gui/FluxAiWorkerController.cpp`
- `Gui/Gui.h`
- `tasks/T083-ai-matte-depth.md`

## Required Change
1. Add explicit source export metadata/context comparison.
   - After `_gui->exportFluxSam3SourceFrameForSelectedLayer(&metadata)` succeeds and before `_sourceFrameMetadataFresh` can become true, compare the returned metadata against the retained selected-source fields already stored on the panel.
   - Required comparisons, when the metadata key exists or is expected from the existing exporter: `selected_layer_index` equals `_sourceLayerIndex`, `selected_layer_name` equals `_sourceLayerName`, `reader_label` equals `_sourceReaderLabel`, and `source_frame` equals `_sourceSourceFrame`.
   - Keep existing file existence/nonzero-size and dimension consistency checks.
   - On mismatch, clear `_sourceFramePng`, `_sourceFrameMetadata`, `_sourceFrameWidth`, `_sourceFrameHeight`, keep `_sourceFrameMetadataFresh = false`, set/report a clear reselect-source message such as `source-frame export no longer matches selected source; reselect Source Viewer for this layer`, and return false.
   - If the exporter metadata uses a nearby stable key name for any of these fields, use the existing key; do not invent a parallel metadata schema. If any required metadata field is absent and the worker cannot prove the exporter does not provide it, treat absence as mismatch/stale and block with the same reselect-source message.
   - Prefer a small private helper in `FluxAiPanel.cpp` (and declaration in `FluxAiPanel.h` only if needed) such as `exportedSourceMetadataMatchesSelection(const QJsonObject&, QString*) const` to keep `refreshSourceFrameMetadata()` readable.
2. Ensure `validateSourceCaptureContext()` cannot pass stale metadata.
   - Either call the same metadata/context comparison helper from `validateSourceCaptureContext()` after the existing dimension checks, or otherwise duplicate the same explicit comparisons there.
   - Preserve the existing viewer identity, viewer input 0, reader activation, freshness, and dimension checks.
   - Do not compare against current timeline/global selection by scanning other UI state; compare the retained selected-source context against the just-exported metadata.
3. Restore the existing generated-media root policy in `onRunClicked()`.
   - Replace `relativeRoot = "generated/ai/%1/"` with the pre-existing project-relative `FluxGenerated/AI/<task>/<model>/<run>/` policy.
   - Build the path from the current task text, model id, and run id; sanitize path components minimally for filesystem safety if surrounding code already has a local helper/pattern, otherwise use simple stable tokens without changing user-facing labels.
   - Keep the unsaved-project preflight before source export or directory creation.
   - Compute `absoluteRoot` from the saved project directory plus the restored project-relative root.
   - Pass `modelId`, `runId`, `absoluteRoot`, `relativeRoot`, `_sourceFramePng`, and `_sourceFrameMetadata` unchanged into `startSam3StillMask()`.
4. Preserve existing SAM3/runtime behavior.
   - Do not change provider runtime scripts, prompt conversion, AI Paint prompt ownership, model availability behavior, manifest schema, or `startSam3StillMask()` arguments except as needed to accept the restored paths.
   - Do not touch RotoPaint or add AI Panel prompt capture controls.

## Non-Goals
- Do not touch RotoPaint.
- Do not add AI Panel point/box prompt capture controls.
- Do not redesign SAM/provider runtime or Python scripts.
- Do not broaden AI Paint prompt selection/conversion behavior beyond what is already implemented.
- Do not edit task status files, project plans, or generated artifacts.
- Do not edit files outside `Gui/FluxAiPanel.h` and `Gui/FluxAiPanel.cpp` unless a hard compile blocker proves absolutely necessary; stop and report first.

## Validation
Commands:
- `cmake --build /home/npittas/Flux/build --target Natron -j$(nproc)`
- `grep -R "generated/ai/%1" -n /home/npittas/Flux/Gui/FluxAiPanel.cpp && false || true`
- `grep -R "FluxGenerated/AI" -n /home/npittas/Flux/Gui/FluxAiPanel.cpp /home/npittas/Flux/Gui/FluxAiWorkerController.cpp`
- `grep -n "selected_layer_index\|selected_layer_name\|reader_label\|source_frame" /home/npittas/Flux/Gui/FluxAiPanel.cpp`

Expected result:
- Build succeeds.
- No remaining `generated/ai/%1` SAM3 run-root construction remains in `FluxAiPanel.cpp`.
- Static grep shows `FluxGenerated/AI` policy evidence in the panel path construction and/or worker-controller policy context.
- Static grep shows `FluxAiPanel.cpp` explicitly checks `selected_layer_index`, `selected_layer_name`, `reader_label`, and `source_frame` metadata against retained selected-source context.
- Manual GUI validation may be left for orchestrator/Nick: stale source/layer/frame after arming Source Viewer should block Run with a reselect-source message; valid saved project runs should write under `FluxGenerated/AI/<task>/<model>/<run>/`.

## Stop Conditions
Stop and report if:
- target symbol is missing
- required fix exceeds allowed files
- validation cannot run
- existing architecture contradicts the requested change
- task requires product/design judgment not in packet
- exported source metadata does not expose enough selected source fields to compare without editing the exporter
- restoring `FluxGenerated/AI/<task>/<model>/<run>/` requires changing provider runtime scripts or worker-controller API
- implementing the fix would write generated media before Save As/project-relative output is established
- implementing the fix would alter viewer-toolbar prompt ownership, add AI Panel prompt controls, or touch RotoPaint

## Required Return Contract
Return only a task-focused summary. Do not include transcript, tool logs, raw file dumps, large code blocks, or broad unrelated issues. Include status, files inspected/changed, validation evidence, blockers, and task-specific risks.
