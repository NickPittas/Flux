# Planner Report

## Status
ready

## Rationale
Reviewer evidence identifies two narrow regressions in the existing AI Panel implementation: generated output/preview paths treat `Project::getProjectPath()` as a file path instead of the project directory, and manual Run no longer forces a Save As for never-saved projects before writing. Both fixes are localized to `FluxAiPanel::onRunClicked()` / `FluxAiPanel::onSam3Finished()` and can preserve the already-implemented stored-context export, diagnostics, PNG dimensions, and AI Work Viewer preview helper.

# Task Packet

## User Goal
Fix Flux AI Panel manual Run so SAM3 exports and previews generated AI output under the saved project directory, blocks/forces Save As for unsaved projects before any writes, shows the SAM3 manual Run preview in AI Work Viewer, and leaves the main comp viewer untouched.

## Mode
general-coding

## Relevant Locations
- file: `Gui/FluxAiPanel.cpp`
  symbol: `FluxAiPanel::onRunClicked()`
  approximate lines: 747-813
  stable anchor: `const QString projectPath = _gui->getApp()->getProject()->getProjectPath();`
  reason: Current Run preflight only checks `projectPath.isEmpty()` and then builds `absoluteRoot` with `QFileInfo(projectPath).absolutePath()`, which skips the saved-project Force Save As behavior and writes relative output beside the project directory instead of inside it.
  confidence: high
- file: `Gui/FluxAiPanel.cpp`
  symbol: `FluxAiPanel::onSam3Finished(...)`
  approximate lines: 1098-1104
  stable anchor: `const QString projectDir = QFileInfo(projectPath).absolutePath();`
  reason: Current preview path is resolved from the parent of `Project::getProjectPath()`; it must resolve the project-relative mask under the project directory before calling `previewFluxAiResultPngInWorkViewer()`.
  confidence: high
- file: `Gui/Gui.cpp`
  symbol: `Gui::reloadProject()`
  approximate lines: 187-198
  stable anchor: `QString projectPath = proj->getProjectPath(); StrUtils::ensureLastPathSeparator(projectPath); projectPath.append(filename);`
  reason: Existing Natron usage treats `Project::getProjectPath()` as the project directory, not the full project file path.
  confidence: high
- file: `Gui/Gui20.cpp`
  symbol: `Gui::saveProject()` / `Gui::saveProjectAs()`
  approximate lines: 1224-1286
  stable anchor: `if ( project->hasProjectBeenSavedByUser() ) { ... } else { return saveProjectAs(); }`
  reason: Existing save pattern forces Save As for projects not saved by the user; manual Run should use the same project API/pattern before creating generated output.
  confidence: high
- file: `Engine/Project.h`
  symbol: `Project::getProjectPath()`, `Project::hasProjectBeenSavedByUser()`, `Project::isSaveUpToDate()`
  approximate lines: 131-137
  stable anchor: `bool hasProjectBeenSavedByUser() const WARN_UNUSED_RETURN;`
  reason: Confirms the project APIs available for saved-by-user preflight and project directory resolution.
  confidence: high
- file: `Gui/Gui05.cpp`
  symbol: `Gui::exportFluxSam3SourceFrameForSourceContext(...)` and `Gui::previewFluxAiResultPngInWorkViewer(...)`
  approximate lines: 723-944
  stable anchor: `source frame exported for stored AI Work Viewer context` and `SAM3 result preview loaded in Flux AI Work Viewer`
  reason: Existing good implementation for stored-context export, diagnostics, dimensions, and AI Work Viewer preview should be preserved; do not refactor this path for the blocker fix.
  confidence: high
- file: `Gui/Gui.h`
  symbol: `Gui` AI helper declarations
  approximate lines: 90-93
  stable anchor: `QString exportFluxSam3SourceFrameForSourceContext(...)`
  reason: Confirms helper APIs already exist; no header change is expected for this blocker fix unless the implementation discovers a signature mismatch.
  confidence: high

## Allowed Edit Files
- `Gui/FluxAiPanel.cpp`

## Read-Only Context Files
- `tasks/T083-ai-run-export-preview-fix-plan.md`
- `Gui/FluxAiPanel.h`
- `Gui/Gui.cpp`
- `Gui/Gui.h`
- `Gui/Gui05.cpp`
- `Gui/Gui20.cpp`
- `Engine/Project.h`

## Required Change
1. In `FluxAiPanel::onRunClicked()`, replace the current unsaved-project check that only tests `projectPath.isEmpty()` with a saved-by-user preflight using the existing `Project`/`Gui` pattern:
   - Obtain the current `ProjectPtr` once after `_gui`, app, and project availability checks.
   - If `!project->hasProjectBeenSavedByUser()`, log a concise message that SAM3 generation requires saving the project and call `_gui->saveProjectAs()` before any prompt reads, source export, directory creation, or SAM3 process start.
   - If Save As is cancelled or fails, set failure status, append a clear blocked/cancelled message, update UI state, and return without writing generated output.
   - After Save As succeeds, re-read `project->getProjectPath()` because Save As may have changed it.
   - If the refreshed project directory is still empty, fail with the existing save-before-running style message.
2. In `FluxAiPanel::onRunClicked()`, build the generated run directory from the project directory itself:
   - Keep `relativeRoot = "FluxGenerated/AI/<task>/<model>/<runId>/"` unchanged.
   - Change `absoluteRoot` from `QDir(QFileInfo(projectPath).absolutePath()).filePath(relativeRoot)` to `QDir(projectPath).filePath(relativeRoot)`.
   - Do not change manifest relative paths or `_sam3RelativeRoot`; outputs must remain project-relative.
3. In `FluxAiPanel::onSam3Finished()`, resolve the generated mask preview path from the project directory itself:
   - Remove the `QFileInfo(projectPath).absolutePath()` parent-directory derivation.
   - Use `QDir(projectPath).filePath(relativeMask)` when calling `_gui->previewFluxAiResultPngInWorkViewer(...)`.
   - Preserve the existing behavior where preview failure is logged as skipped and does not turn a verified SAM3 result into overall failure.
4. Preserve the existing good implementation for:
   - stored-context source export via `exportFluxSam3SourceFrameForSourceContext(...)`,
   - detailed source export diagnostics,
   - positive `width`/`height` and `exported_png_width`/`exported_png_height` metadata,
   - manifest project-relative mask paths,
   - AI Work Viewer-only preview helper,
   - main comp viewer routing.

## Non-Goals
- No RotoPaint, provider, backend Python, or model manager changes.
- No auto-run on prompt placement.
- No broad refactor of AI Panel, Gui helpers, timeline, project serialization, or viewer infrastructure.
- Do not alter stored-context export behavior except as needed to compile after this narrow fix.
- Do not connect the generated mask to the main comp viewer or apply it to the timeline comp.
- Do not change generated relative path schema (`FluxGenerated/AI/...`) or manifest field names.

## Validation
Commands:
- `cmake --build /home/npittas/Flux/build --target Natron -j$(nproc)`
- Manual GUI proof: create a new unsaved project, configure a valid AI Work Viewer/SAM3 prompt, press Run, verify Save As is forced before writes; cancel Save As and confirm no `FluxGenerated/AI/...` output is created and the panel reports a blocked/cancelled Run.
- Manual GUI proof: repeat with Save As accepted, complete Run, and verify generated output exists inside the saved project directory at `<projectPath>/FluxGenerated/AI/<task>/<model>/<runId>/...`, not in the parent directory.
- Manual GUI proof: after SAM3 success, verify `mask_<kind>.png` is displayed in Flux AI Work Viewer and capture screenshot/recording showing the AI Work Viewer tab/result.
- Manual GUI proof: verify the main comp viewer remains on the normal comp output and is not switched to the generated mask preview.

Expected result:
- Build succeeds.
- Unsaved projects cannot run SAM3 writes without successful Save As.
- Generated SAM3 output and preview path resolution are rooted under `Project::getProjectPath()` as the project directory.
- Existing stored-context source export, diagnostics, dimensions, manifest relative paths, and AI Work Viewer preview behavior still work.

## Stop Conditions
Stop and report if:
- target symbol is missing
- required fix exceeds allowed files
- validation cannot run
- existing architecture contradicts the requested change
- task requires product/design judgment not in packet
- `Project::getProjectPath()` is proven not to be a project directory in this build despite `Gui.cpp` usage
- forcing Save As before Run requires changing `Gui::saveProjectAs()` behavior or project save policy
- generated output path correction requires changing manifest schema or backend Python arguments
- AI Work Viewer preview requires touching main comp viewer routing

## Planner Self-Check
- locator evidence sufficient: yes — reviewer evidence plus authorized code confirms both faulty anchors and the existing project-directory/save APIs.
- allowed edit files minimal and explicit: yes — the blocker fix is localized to `Gui/FluxAiPanel.cpp`; all other files are read-only context.
- read-only context minimal: yes — context is limited to original plan, AI panel/header, existing Gui save/path usage, AI helper declarations/implementations, and Project API.
- anchors/lines included: yes — each relevant location includes path, symbol, approximate lines, stable anchor, reason, and confidence.
- validation concrete: yes — build plus manual Save As preflight, project-relative output, AI Work Viewer preview, and main viewer non-regression proof.
- parallelization decision explicit and safe: yes — single task; both blockers share `FluxAiPanel.cpp` and one Run/finish flow, so parallel edits would interfere.
- non-goals and stop conditions sufficient: yes — they prevent backend/RotoPaint/auto-run/refactor/main-viewer/schema scope creep and require stopping on architecture or validation contradictions.
- reviewer findings addressed, if revision: yes — blocker 1 is addressed by using `QDir(projectPath).filePath(relativeRoot/relativeMask)`, and blocker 2 is addressed by restoring saved-by-user Save As preflight before writes.

## Required Return Contract
Return only a task-focused summary. Do not include transcript, tool logs, raw file dumps, large code blocks, or broad unrelated issues. Include status, files inspected/changed, validation evidence, blockers, and task-specific risks.
