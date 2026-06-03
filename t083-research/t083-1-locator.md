# Locator Report

## Summary
Implementation surface for T083-1 is mainly `Gui/Gui05.cpp`, new Flux AI panel files, `Gui/FluxTimeline*`, viewer mouse hooks in `Gui/ViewerGL.cpp`, project serialization, and a new/standalone AI worker controller/tool path.

## Confidence
medium

Codemap index was present but stale because of dirty/untracked work:
- `build-logs/`
- `lans/PHASES.md`
- `t083-research/`
- `tasks/T083-ai-matte-depth.md`
- `tools/ai/`
- others

Per instruction, I did not update the stale index and verified using source reads.

## Relevant Locations

1. `file:///home/npittas/Flux/Gui/Gui05.cpp`
   - symbol: `Gui::setupFluxUi()`
   - approximate lines: 532–1063
   - stable anchor: `Flux Layout:` and `Populate top-right pane: Properties + Export (tabs)`
   - why relevant: central Flux panel/tab registration. AI panel shell should likely be created beside `Export`, `Text`, and `Text Animators`.
   - evidence: verified top-right pane creates `FluxExportPanel`, `FluxTextPanel`, `FluxTextAnimatorPanel`, then `TabWidget::moveTab(...)`.

2. `file:///home/npittas/Flux/Gui/Gui05.cpp`
   - symbol: `Gui::rebuildCompositingGraph(FluxTimeline* timeline)`
   - approximate lines: 1436+
   - stable anchor: `Node Graph Layout — Per-Layer Vertical Branch Stack`
   - why relevant: generated media may need to become project/layer nodes after AI worker output; graph reconnect rules live here.
   - evidence: verified non-destructive Flux node creation/reconnection and background Reformat anchoring.

3. `file:///home/npittas/Flux/Gui/FluxTimeline.h`
   - symbols: `struct FluxLayer`, `class FluxTimeline`
   - approximate lines: 120–335
   - stable anchors: `struct FluxLayer {`, `Q_SIGNALS:`
   - why relevant: generated media metadata likely belongs in/near `FluxLayer`; timeline selection signals are already exposed.
   - evidence: verified existing fields: `filePath`, `type`, `readerNodeId`, `readerNode`, `gizmoNode`, `mergeNode`, `effects`, `masks`, and signals `layerSelected`, `effectSelected`, `maskSelected`.

4. `file:///home/npittas/Flux/Gui/FluxTimelineSerialization.h`
   - symbols: `FluxMaskSerialization`, `FluxLayerSerialization`, `FluxTimelineSerialization`
   - approximate lines: 60–280+
   - stable anchors: `struct FluxMaskSerialization`, `struct FluxLayerSerialization`
   - why relevant: generated media metadata/project-relative paths must persist in `.ntp`.
   - evidence: verified current serialization captures layer identity, file path, type, node script refs, masks, effects, animators.

5. `file:///home/npittas/Flux/Gui/FluxTimeline.cpp`
   - symbols: `FluxTimeline::serializeForProject()`, `FluxTimeline::restoreFromProjectSerialization(...)`
   - approximate lines: 4327–4583
   - stable anchor: `// Identity`, `// Node script names`, `// Masks`
   - why relevant: edit point for generated media metadata capture/restore.
   - evidence: verified `filePath`, timing/state, node refs, masks/effects, and text animator data are serialized/restored.

6. `file:///home/npittas/Flux/Gui/ProjectGuiSerialization.h`
   - symbol: `ProjectGuiSerialization`
   - approximate lines: 645–835
   - stable anchor: `FluxTimelineSerialization _fluxTimeline;`
   - why relevant: root GUI serialization version already includes Flux timeline payload.
   - evidence: verified `PROJECT_GUI_SERIALIZATION_INTRODUCES_FLUX` and `FluxTimeline` NVP save/load.

7. `file:///home/npittas/Flux/Gui/ProjectGuiSerialization.cpp`
   - symbol: `ProjectGuiSerialization::initialize`
   - approximate lines: 185–197
   - stable anchor: `// Serialize Flux timeline state`
   - why relevant: generated media metadata should flow through existing Flux timeline serialization.
   - evidence: verified `_fluxTimeline = timeline->serializeForProject()` and background Reformat script name save.

8. `file:///home/npittas/Flux/Gui/ProjectGui.cpp`
   - symbol: `ProjectGui::load`
   - approximate lines: 590–616
   - stable anchor: `// Restore Flux timeline state (after all engine nodes are loaded)`
   - why relevant: restore path for generated media metadata and rebuilt graph after project load.
   - evidence: verified `timeline->restoreFromProjectSerialization(...)` then `_gui->rebuildCompositingGraph(timeline)`.

9. `file:///home/npittas/Flux/Gui/ViewerGL.cpp`
   - symbol: `ViewerGL::mousePressEvent(QMouseEvent*)`
   - approximate lines: 1729–2026
   - stable anchors: `process plugin overlays`, `build selection rectangle`
   - why relevant: viewer prompt/click/region capture hooks should integrate before generic selection or through existing ROI/selection mechanics.
   - evidence: verified conversion to `zoomPos`, plugin overlay dispatch, picker rectangle, UserRoI drag, and generic selection rectangle emit.

10. `file:///home/npittas/Flux/Gui/HostOverlay.cpp`
   - symbol: `HostOverlay::penDown(...)`
   - approximate lines: 2781–2801
   - stable anchor: `for (DefaultInteractIPtrList::iterator it = _imp->interacts.begin()`
   - why relevant: existing overlay interaction path for node handles; likely read-only context unless AI prompt capture is implemented as overlay.
   - evidence: verified pen-down dispatch to registered interacts.

11. `file:///home/npittas/Flux/Engine/ProcessHandler.h`
   - symbol: `class ProcessHandler`
   - approximate lines: 83–193
   - stable anchor: `QProcess* _process`
   - why relevant: existing Natron background process controller pattern.
   - evidence: verified QProcess + local IPC + signals for output, cancel, finish.

12. `file:///home/npittas/Flux/Engine/ProcessHandler.cpp`
   - symbol: `ProcessHandler::ProcessHandler`, `startProcess`
   - approximate lines: 50–140
   - stable anchor: `_processArgs << "-b" << "-w"`
   - why relevant: useful pattern, but render-specific; AI worker likely needs separate `Gui/FluxAiWorkerController.*`.
   - evidence: verified background render starts current app with `-b -w`, IPC pipe, stdout/stderr hooks.

13. `file:///home/npittas/Flux/Gui/FluxProjectBin.cpp`
   - symbol: thumbnail QProcess use
   - approximate lines: 425–450
   - stable anchor: `Use QProcess to run: ffmpeg`
   - why relevant: lightweight local QProcess pattern for external tools.
   - evidence: verified synchronous `ffmpeg.start`, `waitForStarted`, `waitForFinished`, stdout capture.

14. `file:///home/npittas/Flux/Gui/Gui20.cpp`
   - symbols: `Gui::saveProject()`, `Gui::saveProjectAs()`
   - approximate lines: 1222–1290
   - stable anchor: `if ( project->hasProjectBeenSavedByUser() )`
   - why relevant: Force Save As for unsaved projects before AI generation.
   - evidence: verified unsaved `saveProject()` delegates to `saveProjectAs()` and project path/filename available through `Project`.

15. `file:///home/npittas/Flux/Gui/CMakeLists.txt`
   - symbol: GUI source registration
   - approximate lines: 19–45
   - stable anchor: `file(GLOB NatronGui_HEADERS *.h)` / `file(GLOB NatronGui_SOURCES *.cpp)`
   - why relevant: new `Gui/FluxAiPanel.*` and `Gui/FluxAiWorkerController.*` likely auto-included by glob, but explicit appends exist for recent Flux text files.
   - evidence: verified CMake glob plus explicit appends for `FluxTextPanel`, `FluxTextAnimatorModel`, `FluxTextAnimatorPanel`.

16. `file:///home/npittas/Flux/tools/linux/flux-linux-setup.sh`
   - symbols: `discover_plugin_payloads`, deploy/bootstrap helpers
   - approximate lines: 166–235+
   - stable anchor: `discover_plugin_payloads()`
   - why relevant: future deployment of Python AI tools/deps may need installer/checker integration.
   - evidence: verified payload discovery/deploy patterns for plugins and runtime setup.

## Allowed Edit Scope Recommendation

- Add:
  - `Gui/FluxAiPanel.h`
  - `Gui/FluxAiPanel.cpp`
  - `Gui/FluxAiWorkerController.h`
  - `Gui/FluxAiWorkerController.cpp`
  - `tools/ai/flux_ai_worker.py` or equivalent no-op worker skeleton
- Modify narrowly:
  - `Gui/Gui05.cpp` to instantiate/register AI panel and wire timeline/viewer/project context.
  - `Gui/FluxTimeline.h`
  - `Gui/FluxTimelineSerialization.h`
  - `Gui/FluxTimeline.cpp`
  - possibly `Gui/ProjectGuiSerialization.h` only if Flux serialization version bump is needed.
  - `Gui/ViewerGL.*` only if prompt/region capture needs direct viewer interaction.
  - `Gui/CMakeLists.txt` only if relying on glob is considered insufficient/project policy wants explicit append.
  - `tools/linux/flux-linux-setup.sh` only if T083-1 requires deploying/checking `tools/ai`.

## Read-Only Context Recommendation

- `file:///home/npittas/Flux/Engine/ProcessHandler.*`
- `file:///home/npittas/Flux/Gui/FluxProjectBin.cpp`
- `file:///home/npittas/Flux/Gui/HostOverlay.*`
- `file:///home/npittas/Flux/Gui/ProjectGui.*`
- `file:///home/npittas/Flux/Gui/Gui20.cpp`

## Validation Targets

- tests:
  - Save/reopen `.ntp` with generated media metadata serialized.
  - Unsaved project triggers Save As before AI generation.
  - No-op worker launch returns predictable result/cancel/error.
  - Viewer click/region capture updates AI panel state.
- commands:
  - `cmake --build "$BUILD_DIR" --target NatronGui Natron -j$(nproc)`
  - `python3 -m py_compile tools/ai/flux_ai_worker.py`
  - Run Flux GUI smoke test from existing launcher/build.
- manual checks:
  - AI panel appears in right pane.
  - Select layer/viewer prompt capture visibly populates panel.
  - Generated output path is project-relative under saved project directory.
  - Save/reopen preserves generated media metadata.

## Risks / Unknowns

- `ProcessHandler` is render-specific; reusing it directly for AI worker would couple AI to writer/render IPC. Prefer a new no-op worker controller using QProcess.
- Viewer capture could conflict with existing overlay, picker, ROI, and selection rectangle paths if inserted too broadly.
- Generated media metadata schema needs explicit versioning decision if changing `FluxLayerSerialization`.
- User requested writing this report to `t083-research/t083-1-locator.md`, but this role is read-only/no-edit; I did not write the file.

## Stop Recommendation

Implementation can proceed with **medium confidence** if scoped to the files above. Do not implement model download/security UX beyond the approved T083-1 foundation without a separate product decision.