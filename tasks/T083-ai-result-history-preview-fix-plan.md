# Planner Report

## Status
ready

## Rationale
The reviewer findings are narrow regressions in the already-implemented Packet E AI result history path: unsafe project-relative path acceptance and incomplete persistent-worker busy-state handling. The fix can stay within `FluxAiPanel` state/logic with no serialization schema change required, because `ProjectGuiSerialization.h` already contains the Packet E history field/versioning and the requested fixes only need to filter values before storing, resolving, or previewing them.

# Task Packet

## User Goal
Fix the Packet E implementation review findings only: reject unsafe restored/legacy AI result manifest/history paths before they are stored or previewed, and keep Preview Again/Run/Apply disabled while a persistent SAM3 inference request is active.

## Mode
general-coding

## Relevant Locations
- file: `Gui/FluxAiPanel.cpp`
  symbol: `FluxAiPanel::serializeForProject`
  approximate lines: 133-151
  stable anchor: `serialization.lastResultManifestProjectRelative = _lastResultManifestProjectRelative.toStdString();`
  reason: currently serializes `_lastResultManifestProjectRelative` and history entries after only trimming/non-empty checks; must serialize only safe project-relative manifest paths.
  confidence: high
- file: `Gui/FluxAiPanel.cpp`
  symbol: `FluxAiPanel::restoreFromProjectSerialization`
  approximate lines: 153-200
  stable anchor: `_resultManifestHistoryProjectRelative.clear();`
  reason: currently accepts any non-empty restored history/legacy `lastResultManifestProjectRelative`; must filter through the same project-relative path guard before storing/seeding/selecting.
  confidence: high
- file: `Gui/FluxAiPanel.cpp`
  symbol: `FluxAiPanel::addOrPromoteResultManifest`
  approximate lines: 568-582
  stable anchor: `const QString path = projectRelativeManifest.trimmed();`
  reason: current entry point accepts any worker-provided path; must reject absolute paths and `..` traversal before history insertion, `_lastResultManifestProjectRelative` update, and UI refresh.
  confidence: high
- file: `Gui/FluxAiPanel.cpp`
  symbol: `FluxAiPanel::resultHistoryDisplayLabel`, `FluxAiPanel::selectedResultMaskProjectRelative`, `FluxAiPanel::previewSam3RunResult`
  approximate lines: 584-678 and 1652-1663
  stable anchor: `QDir(_gui->getApp()->getProject()->getProjectPath()).filePath(...)`
  reason: preview/display paths are resolved against the project path; must not resolve or preview unsafe manifest or mask paths loaded from serialization or manifest JSON.
  confidence: high
- file: `Gui/FluxAiPanel.cpp`
  symbol: `FluxAiPanel::updateUiState`
  approximate lines: 681-702
  stable anchor: `const bool running = (_worker && _worker->isRunning()) || sam3ControllerBusy;`
  reason: current busy calculation omits `_sam3RunPendingRequestId` and pending persistent worker `infer_still` commands, allowing Run/Apply/Preview Again while persistent SAM3 inference is active.
  confidence: high
- file: `Gui/FluxAiPanel.cpp`
  symbol: `FluxAiPanel::sendSam3WorkerRequest`, `FluxAiPanel::processSam3WorkerLine`, `FluxAiPanel::cancelSam3`
  approximate lines: 1446-1478 and 1806-1813
  stable anchor: `_sam3WorkerPendingCommands.insert(id, command);`
  reason: `_sam3WorkerPendingCommands` records command names by request id and can be used to detect pending `infer_still` requests; any helper must avoid treating load/unload/cancel as run inference unless intended.
  confidence: high
- file: `Gui/FluxAiPanel.h`
  symbol: `FluxAiPanel` private helpers/member declarations
  approximate lines: 60-151
  stable anchor: `void addOrPromoteResultManifest(const QString& projectRelativeManifest);`
  reason: declare any small private helper(s), e.g. project-relative path guard and persistent inference busy check, if implemented as class methods.
  confidence: high
- file: `Gui/ProjectGuiSerialization.h`
  symbol: `FluxAiPanelSerialization`
  approximate lines: 649-688 and 901
  stable anchor: `BOOST_CLASS_VERSION(NATRON_NAMESPACE::FluxAiPanelSerialization, 1)`
  reason: read-only confirmation that Packet E history serialization already exists; do not edit unless a compile failure proves the guard needs a declaration here, which is not expected.
  confidence: high

## Allowed Edit Files
- `Gui/FluxAiPanel.h`
- `Gui/FluxAiPanel.cpp`

## Read-Only Context Files
- `Gui/ProjectGuiSerialization.h`
- `tasks/T083-ai-result-history-preview-plan.md`
- `ARCHITECTURE.md`
- `plans/PHASES.md`
- `tasks/TASKS.md`

## Required Change
Implement only the two reviewer fixes:

1. Add a shared project-relative path guard in `FluxAiPanel` or an anonymous namespace in `Gui/FluxAiPanel.cpp` and use it everywhere Packet E stores or previews serialized/history paths.
   - The guard must reject empty-after-trim values, `QDir::isAbsolutePath(path)`, Windows-style absolute paths if Qt reports them absolute, and raw path traversal components before or during normalization. Specifically, split the trimmed input on both `/` and `\` (or otherwise inspect raw path components before `QDir::cleanPath()` can collapse them) and reject any component exactly equal to `..` before accepting the path.
   - After raw traversal rejection, return `QDir::cleanPath(path)` as the normalized relative path for stable deduplication. Do not rely on checking for `..` only after `cleanPath()`, because `cleanPath()` can normalize traversal segments away; do not accept paths that normalize outside the project root.
   - Apply the guard before assigning/serializing `_lastResultManifestProjectRelative`, before pushing history entries into `FluxAiPanelSerialization`, before restoring history entries, before seeding history from legacy `lastResultManifestProjectRelative`, and at the start of `addOrPromoteResultManifest()`.
   - Apply the guard before resolving a selected manifest in `resultHistoryDisplayLabel()` and `selectedResultMaskProjectRelative()`.
   - In `selectedResultMaskProjectRelative()`, also guard the manifest JSON's `selected_mask_path_project_relative` before resolving or previewing it.
   - In `previewSam3RunResult(const QString& relativeMask)`, guard `relativeMask` before resolving it against the project path; log/skip if unsafe.
   - For rejected restored/worker paths, do not crash and do not store them. A concise `appendLog()` message is acceptable for worker/interactive paths; avoid noisy logs for every serialization entry if that would spam project open.

2. Include persistent SAM3 inference in UI busy-state.
   - Add a helper or local calculation for pending persistent SAM3 inference: true when `_sam3RunPendingRequestId` is non-empty, or when `_sam3WorkerPendingCommands` contains an `infer_still` request representing an active non-live inference.
   - At minimum, `_sam3RunPendingRequestId` must be included in the `running`/busy condition used by `updateUiState()` so Run, Apply, and Preview Again remain disabled after the persistent worker request is sent and until its matching response clears `_sam3RunPendingRequestId`.
   - If using `_sam3WorkerPendingCommands`, avoid disabling these controls for live-preview-only inference unless the code cannot distinguish it safely; the reviewer specifically requires persistent run-pending state to disable Preview Again during active Run inference.
   - Keep Cancel enabled while persistent inference is active.
   - Do not alter live preview scheduling, prompt capture, worker protocol, or Packet F Apply behavior.

3. Keep scope constrained.
   - Do not modify `ProjectGuiSerialization.h` unless the build fails for a reason directly caused by the new helper declarations; the schema/versioning already exists.
   - Do not touch `Gui05.cpp`, Packet F mask apply, AI prompt capture, node graph mutation, or generated media layout.

## Non-Goals
- No Packet F Apply-to-mask implementation or mask graph changes.
- No Gui05 / Work Viewer routing changes.
- No AI prompt capture or viewer toolbar workflow changes.
- No serialization schema redesign or migration beyond filtering unsafe values at FluxAiPanel boundaries.
- No cleanup/deletion of generated history files.
- No git operations.

## Validation
Commands:
- `cmake --build /home/npittas/Flux/build --target Natron -j$(nproc)`

Manual checks:
- Create or reopen a project with AI result history; verify normal project-relative history entries still display and Preview Again still previews the selected mask.
- Inject/restore history values such as `/tmp/result_manifest.json`, `../result_manifest.json`, and `FluxGenerated/AI/../evil/result_manifest.json`; verify they are not stored, not serialized back, and cannot be previewed.
- Start a persistent SAM3 Run and observe while inference is active that Run, Apply, and Preview Again are disabled and Cancel is enabled; after completion, controls return to the expected enabled state.

Expected result:
Build passes; only safe project-relative manifest/mask paths are stored/resolved/previewed; absolute and traversal paths are rejected at restore, serialization, worker-promotion, and preview boundaries; persistent SAM3 inference keeps Run/Apply/Preview Again disabled until completion.

## Stop Conditions
Stop and report if:
- target symbol is missing
- required fix exceeds allowed files
- validation cannot run
- existing architecture contradicts the requested change
- task requires product/design judgment not in packet
- enforcing safe project-relative paths requires changing project serialization schema or generated media layout
- disabling Preview Again during active persistent inference requires changing the SAM3 worker protocol
- the work starts becoming Packet F Apply/mask graph mutation or Gui05 Work Viewer routing

## Planner Self-Check
- locator evidence sufficient: yes — review findings map directly to current `FluxAiPanel` symbols and the existing serialization struct was inspected as read-only context.
- allowed edit files minimal and explicit: yes — only `Gui/FluxAiPanel.h` and `Gui/FluxAiPanel.cpp`; `ProjectGuiSerialization.h` is read-only unless an unexpected compile issue proves otherwise.
- read-only context minimal: yes — limited to serialization confirmation, the source Packet E plan, and mandated project context docs.
- anchors/lines included: yes — relevant locations include path, symbol, approximate lines, stable anchor, reason, and confidence.
- validation concrete: yes — build command plus targeted manual checks for unsafe paths and inference-active UI state.
- parallelization decision explicit and safe: yes — single task; both fixes share `FluxAiPanel` state/UI and should not be split because concurrent edits would interfere in `updateUiState()` and history helpers.
- non-goals and stop conditions sufficient: yes — excludes Packet F, Gui05, prompt capture, schema redesign, generated media changes, and git operations.
- reviewer findings addressed, if revision: yes — Major 1 addressed by shared path guard before store/serialize/restore/preview, with raw `..` component rejection before/while normalizing; Major 2 addressed by including persistent run-pending SAM3 inference in UI busy state.

## Required Return Contract
Return only a task-focused summary. Do not include transcript, tool logs, raw file dumps, large code blocks, or broad unrelated issues. Include status, files inspected/changed, validation evidence, blockers, and task-specific risks.
