# Planner Report

## Status
ready

## Rationale
The locator evidence and authorized context identify one narrow GUI-only failure path: the AI Panel stores the intended AI Work Viewer/source context, but Run re-exports through a GUI helper that reads the current timeline selection. The same success path already records a project-relative mask, and the AI Work Viewer helper already provides the correct non-main viewer surface, so this can be fixed in the AI Panel/Gui bridge without RotoPaint, backend Python, or main comp viewer changes.

# Task Packet

## User Goal
Fix Flux AI Panel manual Run so source-frame export uses the AI Panel's stored AI Work Viewer/source context instead of current timeline selection drift, provide better export diagnostics and PNG dimension metadata, and after SAM3 succeeds show the generated single-frame mask/result in the Flux AI Work Viewer. Do not add auto-run on prompt placement.

## Mode
general-coding

## Relevant Locations
- file: `Gui/FluxAiPanel.cpp`
  symbol: `FluxAiPanel::refreshSourceFrameMetadata(QString*)`
  approximate lines: 518-555
  stable anchor: `const QString png = _gui->exportFluxSam3SourceFrameForSelectedLayer(&metadata);`
  reason: Run source export currently calls the selection-based exporter and collapses all failures to a generic empty-PNG message; this must use stored panel context and report detailed diagnostics.
  confidence: high
- file: `Gui/FluxAiPanel.cpp`
  symbol: `FluxAiPanel::exportedSourceMetadataMatchesSelection(const QJsonObject&, QString*) const` and `validateSourceCaptureContext(QString*) const`
  approximate lines: 557-605
  stable anchor: `metadata.value(sourceFrameKey).toInt(INT_MIN) == _sourceSourceFrame`
  reason: Metadata validation already enforces stored layer/source frame identity and exported width/height; preserve or update this validation to match the new exporter metadata.
  confidence: high
- file: `Gui/FluxAiPanel.cpp`
  symbol: `FluxAiPanel::onRunClicked()`
  approximate lines: 747-815
  stable anchor: `if (!refreshSourceFrameMetadata(&message) || !validateSourceCaptureContext(&message))`
  reason: Manual Run preflight should keep unsaved-project and prompt checks, then use the refreshed stored source-frame metadata for SAM3.
  confidence: high
- file: `Gui/FluxAiPanel.cpp`
  symbol: `FluxAiPanel::verifySam3ResultAndWriteManifest(...)` and `FluxAiPanel::onSam3Finished(...)`
  approximate lines: 935-1005 and 1063-1098
  stable anchor: `manifest.insert(QString::fromUtf8("selected_mask_path_project_relative"), _sam3RelativeRoot + fileName);`
  reason: Success path records the generated single-frame mask project-relative path; hook preview only after verification succeeds.
  confidence: high
- file: `Gui/FluxAiPanel.h`
  symbol: `class FluxAiPanel`
  approximate lines: 23-93
  stable anchor: `bool refreshSourceFrameMetadata(QString* message);`
  reason: Add only private helper declaration(s) if needed for export diagnostics or result-preview handoff.
  confidence: high
- file: `Gui/Gui05.cpp`
  symbol: `Gui::exportFluxSam3SourceFrameForSelectedLayer(QJsonObject*)`
  approximate lines: 699-866
  stable anchor: `const int layerIndex = timeline->getSelectedLayerIndex();`
  reason: Current exporter derives layer/source from current selection; refactor/add an overload that accepts the stored source context and exports from that exact reader/file/frame. Also populate `exported_png_width` and `exported_png_height` after export.
  confidence: high
- file: `Gui/Gui05.cpp`
  symbol: `Gui::ensureFluxAiWorkViewerTab(NodePtr*)` and source-viewer callers
  approximate lines: 302-359 and 1228-1288
  stable anchor: `viewerNode->disconnectInput(0); viewerNode->connectInput(layer.readerNode, 0);`
  reason: Existing pattern for safely routing Flux AI Work Viewer input 0 without touching the main comp viewer; reuse it for the manual Run result preview.
  confidence: high
- file: `Gui/Gui05.cpp`
  symbol: Read node creation patterns
  approximate lines: 2154-2163
  stable anchor: `NodePtr readNode = getApp()->createReader(filePath, readArgs);`
  reason: Use the existing reader creation pattern for a temporary Read node pointing at the generated mask PNG, then connect that reader to AI Work Viewer input 0.
  confidence: medium
- file: `Gui/Gui.h`
  symbol: `Gui` public/private declarations
  approximate lines: 90-92 and 376-379
  stable anchor: `QString exportFluxSam3SourceFrameForSelectedLayer(QJsonObject* sourceMetadata = 0);`
  reason: Declare the new/changed stored-context export API and any narrow AI Work Viewer result-preview helper used by `FluxAiPanel`.
  confidence: high

## Allowed Edit Files
- `Gui/FluxAiPanel.cpp`
- `Gui/FluxAiPanel.h`
- `Gui/Gui05.cpp`
- `Gui/Gui.h`

## Read-Only Context Files
- `Gui/FluxTimeline.h`
- `Engine/EffectInstance.h`
- `tasks/T083-ai-matte-depth.md`

## Required Change
1. Preserve the existing public behavior of `exportFluxSam3SourceFrameForSelectedLayer()` for timeline capture actions, but move the export body into a narrow context-based helper/overload that can accept the AI Panel's stored values: layer index/name, source file path, reader node/label, timeline frame, source frame, and original frame range if needed.
2. Change `FluxAiPanel::refreshSourceFrameMetadata()` to call the context-based exporter using `_sourceLayerIndex`, `_sourceLayerName`, `_sourceFilePath`, `_sourceReaderNode`, `_sourceReaderLabel`, `_sourceTimelineFrame`, and `_sourceSourceFrame` instead of the current selected timeline layer. Do not change prompt selection or unsaved-project preflight.
3. Improve export diagnostics returned to the panel log: include the specific blocking reason for missing GUI/project, invalid stored context, missing/deactivated reader, missing source file, out-of-range source frame, temp directory creation failure, missing ffmpeg, ffmpeg failure/no output, PNG copy failure, invalid output dimensions, and metadata mismatch. Avoid logging only `source-frame export failed or produced an empty PNG` when a specific reason is known.
4. Populate source metadata with positive `width`, `height`, `exported_png_width`, and `exported_png_height` for copied or ffmpeg-exported PNGs. Use a Qt image metadata/probe path available in the existing dependencies (for example `QImageReader`/`QImage`) after the file exists; if dimensions cannot be read, return failure with a specific diagnostic rather than passing stale/zero values.
5. Keep metadata identity fields compatible with `exportedSourceMetadataMatchesSelection()`: `selected_layer_index`, `selected_layer_name`, `reader_label`, `timeline_frame`, `source_frame`, original range/time offset/source basename/suffix/render mode, and temporary PNG basename/note.
6. Add a narrow GUI helper, declared in `Gui/Gui.h` and implemented in `Gui/Gui05.cpp`, to preview a generated AI result PNG in Flux AI Work Viewer only. It should create a temporary/non-autoconnect Read node for the absolute generated mask PNG (using the same `createReader` pattern as footage reads), set a Flux/SAM3 preview label/script name if safe, disconnect AI Work Viewer input 0, connect the Read node to input 0, show the AI Work Viewer tab, and call viewer redraw. It must not connect or alter the main comp viewer.
7. In `FluxAiPanel::onSam3Finished()`, after `verifySam3ResultAndWriteManifest()` returns true, use the manifest's selected mask path (`selected_mask_path_project_relative`, or the known `_sam3RelativeRoot + mask_<kind>.png`) to build the absolute path under the project directory and call the new AI Work Viewer preview helper. Log a concise success/failure diagnostic for the preview, but do not turn a successful SAM3 generation into overall failure solely because preview wiring failed.
8. Keep generated output references project-relative in manifests and panel state. The temporary source PNG may remain temporary and intentionally not persisted.

## Non-Goals
- No RotoPaint, mask-application, or layer graph changes.
- No SAM/backend/provider Python changes unless implementation proves the GUI path is not at fault; if so, stop and report.
- No auto-run on prompt placement or viewer prompt UX changes.
- No broad AI Panel refactor and no changes to model manager/install flow.
- Do not touch the main comp viewer or final compositing viewer routing.
- Do not convert generated masks into applied Flux masks in this task.

## Validation
Commands:
- `cmake --build /home/npittas/Flux/build --target Natron -j$(nproc)`
- Manual GUI validation: launch Flux from the existing build/run path, open/save a project, add a footage layer, arm Source Viewer/AI Work Viewer for that layer, create/use an existing SAM3 prompt, deliberately change timeline selection/current row, press AI Panel Run, and verify the log shows a stored-context source export with nonzero `exported_png_width`/`exported_png_height` metadata and no empty-PNG failure.
- Manual GUI validation: after SAM3 success, verify the generated `mask_<kind>.png` appears in the Flux AI Work Viewer and capture a screenshot/recording showing the AI Work Viewer tab and result.
- Manual GUI validation: verify the main comp viewer remains connected to the normal comp output and is not switched to the generated mask/read preview.

Expected result:
- Build succeeds.
- Manual Run exports the originally armed AI Panel source frame even if timeline selection drifts before Run.
- Export failure logs identify the concrete failing condition.
- Result manifest keeps project-relative generated mask paths and includes valid source dimension metadata.
- SAM3 success previews the generated single-frame mask/result in Flux AI Work Viewer only.

## Stop Conditions
Stop and report if:
- target symbol is missing
- required fix exceeds allowed files
- validation cannot run
- existing architecture contradicts the requested change
- task requires product/design judgment not in packet
- the stored AI Panel context lacks enough data to reconstruct the intended source frame without broad timeline/model changes
- Qt/Natron cannot read exported PNG dimensions with available dependencies inside the allowed files
- generated mask preview requires changing main comp viewer routing or applying masks to timeline layers
- backend/SAM Python behavior is the proven cause of the empty-PNG failure

## Planner Self-Check
- locator evidence sufficient: yes — high-confidence anchors cover Run export, metadata validation, SAM3 success manifest, AI Work Viewer routing, and Read creation patterns.
- allowed edit files minimal and explicit: yes — only `Gui/FluxAiPanel.cpp`, `Gui/FluxAiPanel.h`, `Gui/Gui05.cpp`, and `Gui/Gui.h` are needed for GUI export/preview bridge changes.
- read-only context minimal: yes — timeline header, EffectInstance header, and T083 task source of truth are enough for API/context constraints.
- anchors/lines included: yes — each relevant location includes path, symbol, approximate lines, stable anchor, reason, and confidence.
- validation concrete: yes — build plus specific GUI reproduction, metadata, AI Work Viewer preview, and main viewer non-regression checks.
- parallelization decision explicit and safe: yes — single task; export fix and preview hook share `FluxAiPanel.cpp`, `Gui05.cpp`, and AI Work Viewer state, so parallel edits would interfere.
- non-goals and stop conditions sufficient: yes — they exclude RotoPaint/backend/auto-run/main-viewer/application scope creep and require stopping on missing context or architecture contradictions.
- reviewer findings addressed, if revision: not applicable — no reviewer findings were supplied for this plan.

## Required Return Contract
Return only a task-focused summary. Do not include transcript, tool logs, raw file dumps, large code blocks, or broad unrelated issues. Include status, files inspected/changed, validation evidence, blockers, and task-specific risks.
