# Locator Report

## Summary
Revised T083 should be implemented as new Flux-owned AI node/panel/job/cache surfaces that plug into existing node creation, viewer overlay/event, installer, project serialization, and P6 mask graph paths.

## Confidence
medium

## Relevant Locations

1. `file:///home/npittas/Flux/Gui/Gui05.cpp`
   - symbol: `Gui::rebuildCompositingGraph`, `ensureMaskSourceNodes`, `ensureInlineLayerMaskPremultNode`
   - approximate lines: 1111–1235, 1436–2110
   - stable anchor: `Gui::rebuildCompositingGraph(FluxTimeline* timeline)`
   - why relevant: Central Flux graph builder; creates FluxLayer/FluxMotionText nodes, Read nodes, Merge chain, effect-mask branches, inline layer masks.
   - evidence: Uses `CreateNodeArgs`, disables `AutoConnect/AddUndoRedoCommand/SettingsOpened`, creates `FluxLayer`, `FluxSolid`, `FluxMotionText`, `Read`, `Roto`, `Premult`, `Unpremult`, and wires mask graph.

2. `file:///home/npittas/Flux/Gui/FluxTimeline.h`
   - symbol: `FluxLayer`, `FluxMask`, `FluxEffect`
   - approximate lines: 64–151
   - stable anchor: `struct FluxMask`
   - why relevant: Current in-memory layer/effect/mask model; likely place to reference AI-generated raster matte/depth artifacts or node-backed AI effects.
   - evidence: `FluxMask` stores `maskNode`, `reformatNode`, `effectIndex`; `FluxLayer` stores `masks`, `maskApplyNode`, `hasPrecompBranch`.

3. `file:///home/npittas/Flux/Gui/FluxTimeline.cpp`
   - symbol: `FluxTimeline::contextMenuEvent`, `serializeForProject`, `restoreFromProjectSerialization`
   - approximate lines: 3423–3860, 4328–4585
   - stable anchor: `void FluxTimeline::contextMenuEvent(QContextMenuEvent* event)`
   - why relevant: Timeline context menu already adds masks/effects and opens properties; serialization already persists node script names for effects/masks.
   - evidence: Context menu has `Add Effect Mask`, `Open Mask Properties`, `Add Effect to Layer...`; serialization stores node fully-qualified names and restores via `Project::getNodeByFullySpecifiedName`.

4. `file:///home/npittas/Flux/Gui/FluxTimelineSerialization.h`
   - symbol: `FluxMaskSerialization`, `FluxLayerSerialization`
   - approximate lines: 31–260
   - stable anchor: `struct FluxMaskSerialization`
   - why relevant: Minimal serialization extension point for AI model node refs, generated raster/depth sequence metadata, prompt/tool state.
   - evidence: Existing structs persist `pluginId`, `nodeScriptName`, `maskNodeScriptName`, `reformatNodeScriptName`, `maskApplyNodeScriptName`, effects, masks, animators.

5. `file:///home/npittas/Flux/Gui/FluxMaskUtils.cpp`
   - symbol: `discoverMaskInput`, `isPremultNode`, `isUnpremultNode`, `isRotoMaskNode`
   - approximate lines: 60–220+
   - stable anchor: `int discoverMaskInput(const NodePtr& node)`
   - why relevant: Existing mask-input discovery and branch classification should be reused for AI matte/depth node attachment.
   - evidence: Finds mask inputs via `EffectInstance::isInputMask()` or labels containing mask/matte/alpha.

6. `file:///home/npittas/Flux/Gui/ViewerGL.cpp`
   - symbol: `ViewerGL::mousePressEvent`
   - approximate lines: 1729–2026
   - stable anchor: `ViewerGL::mousePressEvent(QMouseEvent* e)`
   - why relevant: Viewer click/box prompt tools should hook before/alongside overlay/picker handling.
   - evidence: Converts Qt mouse position to zoom/canonical coords, dispatches overlay `notifyOverlaysPenDown`, picker point/rectangle selection.

7. `file:///home/npittas/Flux/Gui/HostOverlay.cpp`
   - symbol: `HostOverlay::penDown`, `HostOverlay::penUp`, `TransformInteract::penDown`
   - approximate lines: 2503–2805
   - stable anchor: `HostOverlay::penDown(double time,`
   - why relevant: AI prompt overlays can follow existing interact plumbing for viewer handles/selections.
   - evidence: Iterates overlay interact list and consumes pen events; existing transform interact demonstrates hit-testing in viewer coordinates.

8. `file:///home/npittas/Flux/Gui/NodeCreationDialog.cpp`
   - symbol: `NodeCreationDialog::NodeCreationDialog`, `getNodeName`
   - approximate lines: 360–520
   - stable anchor: `NodeCreationDialog::NodeCreationDialog(const QString& initialFilter,`
   - why relevant: Nodegraph-usable AI nodes must be user-creatable/discoverable in the normal plugin creation dialog.
   - evidence: Builds plugin list from registered `Plugin` objects where `getIsUserCreatable()` is true and returns plugin ID/major version.

9. `file:///home/npittas/Flux/Engine/CreateNodeArgs.h`
   - symbol: `CreateNodeArgs`
   - approximate lines: use as read-only context
   - stable anchor: `kCreateNodeArgsPropPluginID`
   - why relevant: Required C++ node creation API for AI nodes.
   - evidence: Existing graph code in `Gui05.cpp`/`FluxTimeline.cpp` relies on it for all Flux-managed node creation.

10. `file:///home/npittas/Flux/tools/linux/flux-linux-setup.sh`
   - symbol: `discover_plugin_payloads`, `bootstrap_python_runtime`
   - approximate lines: 167–246, 703–742, 1124–1129
   - stable anchor: `bootstrap_python_runtime()`
   - why relevant: Installer token/dependency/model Python flow belongs here.
   - evidence: Installs Python deps into `${FLUX_INSTALL_PREFIX}/Plugins/python`, discovers PyPlug/OFX payloads, validates `TextRender` OFX creation.

11. `file:///home/npittas/Flux/Engine/ProcessHandler.h`
   - symbol: `ProcessHandler`
   - approximate lines: 83–170
   - stable anchor: `class ProcessHandler`
   - why relevant: Existing QProcess + IPC pattern for background render jobs; useful read-only model for AI process/job bridge.
   - evidence: Owns `QProcess`, `QLocalServer`, sockets, cancellation slots, process log.

12. `file:///home/npittas/Flux/Engine/ProcessHandler.cpp`
   - symbol: `ProcessHandler::ProcessHandler`, `startProcess`
   - approximate lines: 52–150
   - stable anchor: `ProcessHandler::startProcess()`
   - why relevant: Shows existing process launch and IPC wiring.
   - evidence: Starts current app with render args and `--IPCpipe`; captures stdout/stderr/error/finished.

## Allowed Edit Scope Recommendation
- New files preferred:
  - `Gui/FluxAiPanel.{h,cpp}`
  - `Gui/FluxAiJobBridge.{h,cpp}` or similar
  - `plugins/FluxAIMatte.py` / `plugins/FluxAIDepth.py` if PyPlug nodegroup route is chosen
  - optional OFX/native plugin payload under existing Flux OFX plugin structure if nodegraph-usable AI nodes require C++/OFX.
- Existing minimal edit files:
  - `Gui/Gui05.cpp` for panel docking and graph attachment.
  - `Gui/FluxTimeline.{h,cpp}` for model/context menu/persistence hooks.
  - `Gui/FluxTimelineSerialization.h` for AI artifact metadata.
  - `tools/linux/flux-linux-setup.sh` and `INSTALL_FLUX_LINUX.md` for token/dependency setup.

## Read-Only Context Recommendation
- `file:///home/npittas/Flux/Gui/ViewerGL.cpp`
- `file:///home/npittas/Flux/Gui/HostOverlay.cpp`
- `file:///home/npittas/Flux/Gui/NodeCreationDialog.cpp`
- `file:///home/npittas/Flux/Engine/ProcessHandler.*`
- `file:///home/npittas/Flux/Gui/FluxMaskUtils.*`

## Validation Targets
- tests:
  - Save/reopen project with AI node, prompt metadata, generated raster matte/depth sequence reference.
  - Add AI matte as layer mask and effect mask; verify P6 inline mask graph remains valid.
  - Nodegraph creation: AI nodes appear in Tab/node dialog and can be manually connected.
- commands:
  - `cmake --build "$BUILD_DIR" --target Natron`
  - `bash tools/linux/flux-linux-setup.sh --check`
  - `bash tools/linux/flux-linux-setup.sh --bootstrap-python` after dependency edits.
- manual checks:
  - Screenshot: AI panel controls visible.
  - Screenshot/recording: viewer click/box prompt creates/updates matte/depth.
  - Nodegraph screenshot: generated raster matte/depth sequence attached to mask/depth path.
  - Save/reopen screenshot: AI artifacts reconnect.

## Risks / Unknowns
- No existing AI panel/job bridge found; this is new infrastructure.
- Existing `ProcessHandler` is render-specific; reuse as pattern, not direct fit.
- Raster matte/depth attachment needs product decision: generated Read sequence, PyPlug/OFX output node, or sidecar cache node.
- Installer token handling must avoid storing secrets in project files or scripts.
- User requested writing to `file:///home/npittas/Flux/t083-research/revised-local-surface.md`, but this locator role is explicitly read-only/no-edit, so I did not write the file.

## Stop Recommendation
Implementation can proceed after architecture packet approval, but only with narrow first slice: nodegraph-usable placeholder AI node + panel/job bridge skeleton + generated raster sequence attachment path, without changing existing P6 mask semantics.