# Planner Report

## Status
ready

## Rationale
Packet E result-history infrastructure is already present and locator evidence identifies a narrow AI Panel edit surface. This plan keeps the polish limited to history-list management and richer display inside `FluxAiPanel`, avoids generated-file deletion and Apply graph mutation, and deliberately defers rename/tag persistence because it would require new serialization/schema decisions beyond the safe polish scope.

# Task Packet

## User Goal
Polish T083 AI result history after Packet E: allow removing/deleting a history entry from the AI Panel list only, improve history entries with optional thumbnail/metadata display, and defer rename/tag persistence unless it can be implemented without serialization/schema risk. Do not delete generated result files and do not implement Apply-to-comp/mask graph changes.

## Mode
general-coding

## Relevant Locations
- file: `Gui/FluxAiPanel.cpp`
  symbol: `FluxAiPanel::setupUi`
  approximate lines: 340-368
  stable anchor: `layout->addWidget(new QLabel(tr("Result History")));`
  reason: existing Result History `QListWidget` and Preview Again button are created here; add a narrow remove/list-management control and any list icon/tooltip setup here.
  confidence: high
- file: `Gui/FluxAiPanel.cpp`
  symbol: `FluxAiPanel::refreshResultHistoryList`
  approximate lines: 619-639
  stable anchor: `QListWidgetItem* item = new QListWidgetItem(resultHistoryDisplayLabel(manifestPath), _resultHistoryList);`
  reason: central list rebuild path; set item text, tooltip, icon/thumbnail metadata, and preserve selection after removing entries.
  confidence: high
- file: `Gui/FluxAiPanel.cpp`
  symbol: `FluxAiPanel::resultHistoryDisplayLabel`
  approximate lines: 641-674
  stable anchor: `selected_mask_path_project_relative`
  reason: already reads safe project-relative manifest JSON and builds a robust metadata label; extend or share this parsing for tooltip/thumbnail display without broad search.
  confidence: high
- file: `Gui/FluxAiPanel.cpp`
  symbol: `FluxAiPanel::selectedResultManifestProjectRelative`
  approximate lines: 676-682
  stable anchor: `_resultHistoryList->currentItem()->data(Qt::UserRole)`
  reason: selected manifest path is the safe handle for Preview Again and list-only removal.
  confidence: high
- file: `Gui/FluxAiPanel.cpp`
  symbol: `FluxAiPanel::selectedResultMaskProjectRelative`, `FluxAiPanel::onPreviewAgainClicked`
  approximate lines: 684-725 and 1760-1771
  stable anchor: `previewSam3RunResult(relativeMask);`
  reason: ensure polish does not regress selected-history preview and uses existing project-relative path guard.
  confidence: high
- file: `Gui/FluxAiPanel.cpp`
  symbol: `FluxAiPanel::serializeForProject`, `FluxAiPanel::restoreFromProjectSerialization`, `FluxAiPanel::addOrPromoteResultManifest`
  approximate lines: 160-232 and 604-617
  stable anchor: `_resultManifestHistoryProjectRelative`
  reason: list-only removal must update in-memory serialized history and `_lastResultManifestProjectRelative` compatibility pointer; keep existing project-relative sanitization and dedupe behavior.
  confidence: high
- file: `Gui/FluxAiPanel.cpp`
  symbol: `FluxAiPanel::updateUiState`
  approximate lines: 727-746
  stable anchor: `_previewAgainButton->setEnabled(!selectedResultManifestProjectRelative().isEmpty()`
  reason: add/enable any remove-history control only when safe: item selected and no worker/persistent SAM3 run is active.
  confidence: high
- file: `Gui/FluxAiPanel.h`
  symbol: `FluxAiPanel` slots/helpers/members
  approximate lines: 50-150
  stable anchor: `void onPreviewAgainClicked();`, `QListWidget* _resultHistoryList;`, `QStringList _resultManifestHistoryProjectRelative;`
  reason: declare a remove-history slot/button and any small private helper needed for item metadata/thumbnail display.
  confidence: high
- file: `Gui/ProjectGuiSerialization.h`
  symbol: `FluxAiPanelSerialization`
  approximate lines: 649-688 and 901
  stable anchor: `resultManifestHistoryProjectRelative`, `BOOST_CLASS_VERSION(NATRON_NAMESPACE::FluxAiPanelSerialization, 1)`
  reason: read-only context for this packet unless a non-persistent UI-only rename/tag is implemented; do not add persistent rename/tag fields in this polish task.
  confidence: high

## Allowed Edit Files
- `Gui/FluxAiPanel.h`
- `Gui/FluxAiPanel.cpp`

## Read-Only Context Files
- `Gui/ProjectGuiSerialization.h`
- `tasks/T083-ai-result-history-preview-plan.md`
- `tasks/T083-ai-result-history-preview-fix-plan.md`

## Required Change
Implement a narrow Result History polish pass in `FluxAiPanel` only:

1. Add list-only removal for selected history entries.
   - Add a small `Remove from History` or `Remove Entry` button near `Preview Again`, or an equivalent context-menu action on `_resultHistoryList` if that is less invasive.
   - Removing an entry must only remove the selected manifest path from `_resultManifestHistoryProjectRelative` and refresh the list.
   - It must not delete `result_manifest.json`, masks, thumbnails, source frames, run directories, or any generated media on disk.
   - After removal, update `_lastResultManifestProjectRelative`: prefer the newly selected/current history item; if history is empty, clear it.
   - Keep all existing `sanitizeProjectRelativePath` guards; never store or act on an unsafe manifest path.
   - Keep `Preview Again` behavior unchanged for remaining entries.

2. Improve history display with tooltip and/or thumbnail metadata without new persistence.
   - Reuse the existing manifest read in `resultHistoryDisplayLabel()` or factor a tiny helper if needed.
   - At minimum, set a useful tooltip for each item in `refreshResultHistoryList()` containing the safe project-relative manifest path plus available manifest metadata: run id/time, model id, source frame, prompt count, and selected mask path/basename.
   - If low-risk with current Qt includes, set a small icon thumbnail from the selected mask or generated preview path found in the manifest, but only after guarding project-relative paths and checking the file exists. If thumbnail lookup is uncertain, skip icons and implement tooltip/metadata only.
   - Do not change main comp viewer routing or the Work Viewer preview helper.

3. Defer rename/tag persistence.
   - Do not edit `Gui/ProjectGuiSerialization.h` or add persistent rename/tag fields in this packet.
   - If the worker sees an obvious, UI-only label edit that does not persist and does not confuse save/reopen behavior, it may be proposed in the return as future work rather than implemented.
   - Persistent rename/tag support requires a separate plan because it touches project serialization and product semantics for labels/tags.

4. UI state and behavior.
   - Disable the remove-history control while any current `updateUiState()` busy condition disables Run/Preview Again, and when no item is selected.
   - Ensure selection changes still update `_lastResultManifestProjectRelative` and `updateUiState()`.
   - Ensure save/reopen naturally reflects removed entries through the existing `resultManifestHistoryProjectRelative` serialization, with no serialization version bump.

## Non-Goals
- No generated file deletion or cleanup of AI run directories.
- No Apply graph mutation, mask application, layer/effect mask wiring, or Packet F behavior.
- No main comp viewer changes and no `Gui05.cpp` preview-helper changes.
- No broad serialization bump, schema redesign, or persistent rename/tag fields.
- No AI prompt capture workflow changes.
- No git operations.

## Validation
Commands:
- `cmake --build /home/npittas/Flux/build --target Natron -j$(nproc)`

Manual checks:
- Launch Flux with a project containing at least two AI result history entries; capture screenshot/recording showing richer history labels/tooltips and the remove-history control.
- Select an older entry, click `Preview Again`, and verify the Flux AI Work Viewer previews that selected mask as before.
- Remove one selected history entry and verify it disappears from the AI Panel list, the next/remaining selection is sensible, and `Preview Again` still works for remaining entries.
- Verify the removed entry's generated files/run directory still exist on disk.
- Save and reopen the project; verify the removed entry remains absent from history and remaining entries still preview via project-relative paths.
- If thumbnail icons are implemented, verify missing thumbnail/mask files do not crash and fall back to text/tooltip display.

Expected result:
Build passes; the AI Panel can remove selected history entries from the managed list without deleting generated files; history entries show more useful metadata through labels/tooltips and optionally thumbnails; save/reopen persists the edited list through existing history serialization; Preview Again and project-relative path safety remain intact.

## Stop Conditions
Stop and report if:
- target symbol is missing
- required fix exceeds allowed files
- validation cannot run
- existing architecture contradicts the requested change
- task requires product/design judgment not in packet
- removing a history entry would require deleting or modifying generated files on disk
- tooltip/thumbnail display requires unsafe absolute paths or traversal-prone path resolution
- implementing rename/tag requires editing `ProjectGuiSerialization.h`, changing project serialization versioning, or deciding persistent label/tag semantics
- the work starts becoming Packet F Apply/mask graph mutation or main comp viewer routing

## Planner Self-Check
- locator evidence sufficient: yes — implementation files and anchors come from locator evidence and authorized context; confidence is high for `FluxAiPanel` history/UI helpers.
- allowed edit files minimal and explicit: yes — only `Gui/FluxAiPanel.h` and `Gui/FluxAiPanel.cpp`; serialization is read-only because rename/tag persistence is deferred.
- read-only context minimal: yes — only existing serialization and prior T083 Packet E/fix plans.
- anchors/lines included: yes — relevant locations include path, symbol, approximate lines, stable anchor, reason, and confidence.
- validation concrete: yes — build command plus focused GUI/manual checks for removal, preview-again, persistence, and no generated-file deletion.
- parallelization decision explicit and safe: yes — single task; UI controls, list state, and `_lastResultManifestProjectRelative` share `FluxAiPanel` state, so splitting risks interference.
- non-goals and stop conditions sufficient: yes — explicitly excludes generated deletion, Apply graph mutation, main viewer changes, serialization bump, and persistent rename/tag.
- reviewer findings addressed, if revision: not applicable — no reviewer findings were supplied for this polish plan.

## Required Return Contract
Return only a task-focused summary. Do not include transcript, tool logs, raw file dumps, large code blocks, or broad unrelated issues. Include status, files inspected/changed, validation evidence, blockers, and task-specific risks.
