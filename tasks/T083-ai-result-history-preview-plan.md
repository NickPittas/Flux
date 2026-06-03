# Planner Report

## Status
ready

## Rationale
Locator evidence and authorized context identify a narrow, coherent edit surface for Packet E's remaining work: persist and display managed AI result manifest history in the AI Panel, add preview-again from selected history, and preserve the existing `_lastResultManifestProjectRelative` compatibility pointer. Packet E's worker-reuse portion is already present in the current SAM3 persistent-worker path, so this plan avoids Packet F apply/mask graph work and does not require modifying the existing AI Work Viewer preview helper.

# Task Packet

## User Goal
Complete T083 Packet E's remaining managed AI Panel result history / preview-again behavior after live preview, multiprompt Run, and box prompt fixes. AI generated results must be managed project-relative history entries in the AI Panel, selectable for preview again, and preserved across save/reopen. Apply-to-mask remains Packet F and must not be implemented here.

## Mode
general-coding

## Relevant Locations
- file: `Gui/FluxAiPanel.cpp`
  symbol: `FluxAiPanel::serializeForProject`, `FluxAiPanel::restoreFromProjectSerialization`
  approximate lines: 128-176
  stable anchor: `_lastResultManifestProjectRelative`
  reason: current project serialization stores/restores only the last manifest pointer; extend this to persist a small history of project-relative manifest paths while preserving the last-result compatibility field.
  confidence: high
- file: `Gui/ProjectGuiSerialization.h`
  symbol: `FluxAiPanelSerialization`
  approximate lines: 647-683
  stable anchor: `lastResultManifestProjectRelative`
  reason: serialization struct currently has no history collection and is currently serialized unversioned; add a project-relative manifest-path list with a default-safe Boost class version guard so existing v14 project files that contain a FluxAiPanel block without the new field still load.
  confidence: high
- file: `Gui/FluxAiPanel.cpp`
  symbol: `FluxAiPanel::setupUi`
  approximate lines: 251-299
  stable anchor: `QHBoxLayout* buttons`, `_runButton`, `_applyButton`, `_cancelButton`
  reason: add minimal history UI using simple Qt widgets already used in this file, including a selectable result list and Preview Again button.
  confidence: high
- file: `Gui/FluxAiPanel.cpp`
  symbol: `FluxAiPanel::updateUiState`
  approximate lines: 534-544
  stable anchor: `_applyButton->setEnabled(!_lastResultManifestProjectRelative.isEmpty()`
  reason: enable Preview Again based on selected/available history and keep Apply disabled/enabled behavior tied to the current compatibility pointer until Packet F changes it.
  confidence: high
- file: `Gui/FluxAiPanel.cpp`
  symbol: `FluxAiPanel::verifySam3ResultAndWriteManifest`
  approximate lines: 1401-1491
  stable anchor: `result_manifest.json`, `selected_mask_path_project_relative`, `_lastResultManifestProjectRelative =`
  reason: after each verified SAM3 Run manifest, append or promote the manifest into managed history and refresh the UI selection.
  confidence: high
- file: `Gui/FluxAiPanel.cpp`
  symbol: `FluxAiPanel::previewSam3RunResult`
  approximate lines: 1494-1503
  stable anchor: `previewFluxAiResultPngInWorkViewer`
  reason: reuse the current project-relative mask preview path for Preview Again; if needed, add a helper that reads the selected manifest and passes `selected_mask_path_project_relative` to this function.
  confidence: high
- file: `Gui/FluxAiPanel.cpp`
  symbol: `FluxAiPanel::onWorkerProgress`, `FluxAiPanel::onWorkerFinished`
  approximate lines: 1511-1544
  stable anchor: `result_manifest_path_project_relative`
  reason: legacy worker messages still update `_lastResultManifestProjectRelative`; keep compatibility by also feeding valid manifest paths into the new history model if these paths appear.
  confidence: high
- file: `Gui/FluxAiPanel.h`
  symbol: `FluxAiPanel` member declarations
  approximate lines: 8-149
  stable anchor: `_lastResultManifestProjectRelative`
  reason: declare the result-history container, minimal widgets, and private slots/helpers required by the AI Panel implementation.
  confidence: high
- file: `Gui/Gui05.cpp`
  symbol: `Gui::previewFluxAiResultPngInWorkViewer`
  approximate lines: 897-951
  stable anchor: `Flux SAM3 Result Preview`
  reason: existing helper already previews result PNGs in Flux AI Work Viewer and should be reused as-is unless a compile-time signature issue is discovered.
  confidence: high

## Allowed Edit Files
- `Gui/FluxAiPanel.h`
- `Gui/FluxAiPanel.cpp`
- `Gui/ProjectGuiSerialization.h`

## Read-Only Context Files
- `Gui/Gui05.cpp`
- `tasks/T083-ai-paint-live-sam3-workflow-plan.md`
- `tasks/T083-ai-matte-depth.md`

## Required Change
Implement the smallest managed result-history layer inside `FluxAiPanel`:

1. Add state and UI declarations in `Gui/FluxAiPanel.h`:
   - Include the Qt types needed for a simple list/button UI, e.g. `QListWidget` if chosen.
   - Add a `QStringList` or equivalent member storing project-relative result manifest paths.
   - Add widget members for a result history list and a `Preview Again` button.
   - Add private slot/helper declarations for history selection changes, preview-again, adding/promoting a manifest path, refreshing the list, deriving a display label from a manifest, and resolving the selected manifest/mask.

2. Extend `FluxAiPanelSerialization` in `Gui/ProjectGuiSerialization.h` with a default-safe serialization version guard:
   - Add a collection of project-relative manifest paths, e.g. `std::vector<std::string> resultManifestHistoryProjectRelative`.
   - If using `std::vector<std::string>`, include `boost/serialization/vector.hpp` in this header if it is not already available via an existing include.
   - Add `BOOST_CLASS_VERSION(FluxAiPanelSerialization, 1)` at namespace/global scope in the same style as existing Boost class-version declarations.
   - Serialize the new collection with a new NVP such as `ResultManifestHistoryProjectRelative` only when `version >= 1`.
   - For `version < 1`, leave `resultManifestHistoryProjectRelative` default-empty so existing projects that already contain the v14 `FluxAiPanel` block but not the new field continue to load.
   - Keep `lastResultManifestProjectRelative` unchanged for compatibility and do not add broad project-file migration logic.

3. Update `serializeForProject()` / `restoreFromProjectSerialization()` in `Gui/FluxAiPanel.cpp`:
   - Save the history collection as project-relative manifest paths.
   - Restore the collection after filtering empty/duplicate paths.
   - Preserve compatibility: if restored history is empty but `lastResultManifestProjectRelative` is non-empty, seed history with that last manifest; always keep `_lastResultManifestProjectRelative` populated from the serialized last pointer or selected/latest history entry.
   - Refresh the history UI and selected row after restore.

4. Add minimal history UI in `setupUi()`:
   - Place a small label such as `Result History`, a list widget, and a `Preview Again` button below status/prompt or near the Run/Apply controls.
   - Use simple Qt widget patterns already present in the file; do not introduce custom models/delegates unless required.
   - Connect list selection to update `_lastResultManifestProjectRelative` and `updateUiState()`.
   - Connect Preview Again to preview the selected history item.

5. Add result-history behavior:
   - When `verifySam3ResultAndWriteManifest()` successfully writes `result_manifest.json`, call the new helper with `_lastResultManifestProjectRelative` so the new run appears at the top or selected position in the history.
   - When legacy `onWorkerProgress()` / `onWorkerFinished()` see `result_manifest_path_project_relative`, also add/promote that path to history to avoid regressing older worker messages.
   - Deduplicate by manifest path and keep a sane in-memory cap if desired (for example 25-50 entries) without deleting generated files.
   - The history entry display should be useful but robust: read the manifest JSON if available and show run id/time if present, source frame from `source_metadata`, model id, prompt count, and selected mask basename; fall back to the manifest path if any field/file is missing.
   - All stored paths must remain project-relative. Do not store absolute preview PNG paths in serialization.

6. Implement Preview Again:
   - From the selected manifest path, resolve against `Project::getProjectPath()` only at preview time.
   - Read `selected_mask_path_project_relative` from the manifest and call existing `previewSam3RunResult(relativeMask)`.
   - If the manifest or selected mask is missing/invalid, show a status/log message and do not crash.
   - Reuse `Gui::previewFluxAiResultPngInWorkViewer` through `previewSam3RunResult`; do not modify `Gui/Gui05.cpp` unless compilation proves an interface mismatch.

7. UI state:
   - Enable Preview Again only when not running and a history item with a non-empty manifest path is selected.
   - Keep Apply behavior as compatibility-only for now: it may remain enabled based on `_lastResultManifestProjectRelative`, but `onApplyClicked()` must not implement mask graph application in this packet.
   - Do not add AI prompt capture controls to the AI Panel.

## Non-Goals
- No AI prompt capture in the AI Panel; viewer toolbar remains the prompt owner.
- No auto-apply to comp.
- No Packet F mask apply workflow or layer/effect mask graph mutation.
- No main comp viewer rewiring.
- No changes to generated media layout beyond recording existing project-relative manifest paths.
- No deletion/cleanup UI for generated history files.
- No git operations.

## Validation
Commands:
- `cmake --build /home/npittas/Flux/build --target Natron -j$(nproc)`

Manual checks required because this is user-facing GUI behavior:
- Launch Flux, select a valid source and existing viewer-owned AI Paint prompts, run SAM3 twice, and capture screenshot/recording showing two result history entries in the AI Panel.
- Select the older history entry and click `Preview Again`; verify the Flux AI Work Viewer updates to that selected mask, then repeat for the newer entry.
- Save the project, reopen it, and verify history entries reappear and Preview Again still works from project-relative paths.
- Open an existing project file containing a v14 `FluxAiPanel` block without `ResultManifestHistoryProjectRelative`; verify it loads without serialization failure and history defaults safely, seeded from `lastResultManifestProjectRelative` if present.
- Inspect the saved project serialization and generated `result_manifest.json` files enough to verify history stores project-relative manifest paths and each preview resolves `selected_mask_path_project_relative`.

Expected result:
Build passes; result history is a managed AI Panel handle for generated manifests; Preview Again works for selected past runs; save/reopen preserves the history; existing projects whose `FluxAiPanel` serialization lacks the new history field still load with empty/default history; `_lastResultManifestProjectRelative` remains compatible for existing Apply enablement and future Packet F.

## Stop Conditions
Stop and report if:
- target symbol is missing
- required fix exceeds allowed files
- validation cannot run
- existing architecture contradicts the requested change
- task requires product/design judgment not in packet
- existing projects with a v14 `FluxAiPanel` block cannot be loaded with a default-empty history via `BOOST_CLASS_VERSION(FluxAiPanelSerialization, 1)` and `version >= 1` guarded serialization
- generated or serialized history paths would need to be absolute instead of project-relative
- implementing Preview Again requires changing main comp viewer routing or `Gui05.cpp` preview helper behavior
- the work starts becoming Packet F apply-to-mask graph mutation

## Planner Self-Check
- locator evidence sufficient: yes — all implementation files and symbols come from supplied locator evidence plus authorized context reads; confidence is high for the narrow history/preview work.
- allowed edit files minimal and explicit: yes — only `Gui/FluxAiPanel.h`, `Gui/FluxAiPanel.cpp`, and `Gui/ProjectGuiSerialization.h`; `Gui/Gui05.cpp` is read-only because the existing preview helper should be reused.
- read-only context minimal: yes — limited to the existing preview helper and T083 source-of-truth/task packet context.
- anchors/lines included: yes — relevant locations include paths, symbols, approximate lines, anchors, reasons, and confidence.
- validation concrete: yes — build command plus specific GUI/manual proof for history, preview-again, and save/reopen.
- parallelization decision explicit and safe: yes — single task; files are shared between UI state and serialization, so splitting would risk interference in `FluxAiPanel` state and save/restore behavior.
- non-goals and stop conditions sufficient: yes — explicitly excludes prompt capture, Packet F apply, main viewer rewiring, broad migration, and absolute-path history.
- reviewer findings addressed, if revision: yes — Major serialization finding is addressed by requiring `BOOST_CLASS_VERSION(FluxAiPanelSerialization, 1)`, `version >= 1` guarded history serialization, default-empty old-load behavior, and `boost/serialization/vector.hpp` when using `std::vector<std::string>`.

## Required Return Contract
Return only a task-focused summary. Do not include transcript, tool logs, raw file dumps, large code blocks, or broad unrelated issues. Include status, files inspected/changed, validation evidence, blockers, and task-specific risks.
