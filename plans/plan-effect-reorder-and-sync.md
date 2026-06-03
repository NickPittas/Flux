# Planner Report

## Status
ready

## Rationale
The requested features share the timeline model and compositing-graph bridge, so this plan keeps them in one scoped implementation unit while moving the new graph-to-timeline parser into new small files to avoid further bloating the already-large Flux UI sources. Locator confidence is sufficient from the current timeline/effect model, rebuild wiring, serialization model, Node APIs, and node collection traversal APIs.

# Task Packet

## User Goal
Implement two related Flux timeline/nodegraph features:

1. Drag effects up/down inside an expanded timeline effect stack; the nodegraph reconnects to match the new order.
2. Detect unsynced nodegraph edits, show a timeline sync banner on timeline click, and provide manual sync controls that map the nodegraph back into timeline layers/effects/adjustment rows using Nick's sync rules.

## Mode
general-coding

## Relevant Locations
- file: `Gui/FluxTimeline.h`
  symbol: `FluxEffect`, `FluxLayer`, `FluxTimeline` signals/slots/private state
  approximate lines: 90-189, 191-307, 339-518
  stable anchor: `struct FluxEffect`, `struct FluxLayer`, `void compositingChanged();`, `enum InteractionMode`
  reason: Owns timeline model, selection state, visible rows, effect order, and custom-painted interaction state.
  confidence: high
- file: `Gui/FluxTimeline.cpp`
  symbol: `FluxTimeline::moveEffectInLayer`
  approximate lines: 986-1037
  stable anchor: `bool FluxTimeline::moveEffectInLayer(int layerIndex, int fromEffectIndex, int toEffectIndex)`
  reason: Existing safe model mutation for effect reorder, including effect-mask index remapping and compositing rebuild signal.
  confidence: high
- file: `Gui/FluxTimeline.cpp`
  symbol: `FluxTimeline::rebuildVisibleRows`
  approximate lines: 2574-2665
  stable anchor: `void FluxTimeline::rebuildVisibleRows()` and `effectRow.type = eFluxVisibleRowEffect`
  reason: Defines expanded effect rows and row metadata needed for drag hit-testing and visual insertion feedback.
  confidence: high
- file: `Gui/FluxTimeline.cpp`
  symbol: `FluxTimeline::mousePressEvent`, `mouseMoveEvent`, `mouseReleaseEvent`, `paintEvent`
  approximate lines: 1739-1760, 3300-3470, 3626-3845
  stable anchor: `if (clickRow->type == eFluxVisibleRowEffect)`, `case eModeReorderLayer`, `if (_interactionMode == eModeReorderLayer)`
  reason: Add click-triggered sync banner behavior, effect drag state, cursor feedback, and release-time reorder.
  confidence: high
- file: `Gui/Gui05.cpp`
  symbol: `Gui::setupFluxUi`
  approximate lines: 1321-1776
  stable anchor: `QObject::connect(timeline, &FluxTimeline::compositingChanged, this,` and `// 7. Timeline: effectsChanged`
  reason: Wires timeline signals to graph rebuild and panels; add sync action wiring and suppress dirty marking during Flux-owned rebuilds.
  confidence: high
- file: `Gui/Gui05.cpp`
  symbol: `Gui::rebuildCompositingGraph`
  approximate lines: 2470-3395
  stable anchor: `NodePtr lastOutput = _imp->_fluxBgReformatNode`, `for (int e = 0; e < layer.effects.size(); ++e)`, `input 0 (B) = previous output`, `_imp->_fluxFinalOutputNode = lastOutput`
  reason: Existing timeline-to-nodegraph source of truth for merge B/A wiring, adjustment rows, layer inline effects, masks, and final output.
  confidence: high
- file: `Gui/Gui.cpp`
  symbol: `Gui::createNodeGUI`
  approximate lines: 329-346
  stable anchor: `NodeGuiPtr nodeGui = graph->createNodeGUI(node, args);`
  reason: Central GUI node creation hook where node-added dirty marking and per-node signal connections can be installed without Engine changes.
  confidence: medium
- file: `Gui/GuiPrivate.h`
  symbol: `GuiPrivate` Flux members
  approximate lines: 301-322
  stable anchor: `_fluxTimeline`, `_fluxMergeNodes`, `_fluxBgReformatNode`, `_fluxFinalOutputNode`
  reason: Store dirty-suppression and graph-sync guard state near existing Flux graph bridge members.
  confidence: high
- file: `Gui/Gui.h`
  symbol: Flux accessors/bridge declarations
  approximate lines: 371-381
  stable anchor: `void rebuildCompositingGraph(class FluxTimeline* timeline);`
  reason: Add small public/private declarations for dirty marking and graph-to-timeline sync entry points if needed by Timeline/Gui wiring.
  confidence: high
- file: `Gui/FluxTimelineSerialization.h`
  symbol: `FluxEffectSerialization`, `FluxLayerSerialization`, `FluxTimelineSerialization`
  approximate lines: 35-72, 285-338, 486-535
  stable anchor: `std::vector<FluxEffectSerialization> effects;`
  reason: Confirms sync can reuse existing layer/effect fields without a project-file schema bump unless new persisted state is deliberately added.
  confidence: high
- file: `Gui/FluxMaskUtils.h` / `Gui/FluxMaskUtils.cpp`
  symbol: `discoverMaskInput`, `classifyLayerBranches`
  approximate lines: header 37-62; cpp 60-120, 243-470
  stable anchor: `FluxLayerBranchClassification classifyLayerBranches(const FluxLayer& layer)`
  reason: Existing branch classification and mask-input discovery should be reused by sync logic to avoid treating mask/precomp branches as layer effects.
  confidence: high
- file: `Engine/Node.h`
  symbol: `Node::getInput`, `getOutputs`, `connectInput`, `disconnectInput`, signals
  approximate lines: 369-405, 520-530, 563-582, 676-798, 1109-1111, 1414-1448
  stable anchor: `void inputChanged(int);`, `void outputsChanged();`, `std::string getPluginID() const;`
  reason: Required read-only API for graph traversal and dirty detection.
  confidence: high
- file: `Engine/NodeGroup.h`
  symbol: `NodeCollection`
  approximate lines: 53-180
  stable anchor: `NodesList getNodes() const;`, `void getActiveNodes(NodesList* nodes) const;`, `NodePtr getNodeByFullySpecifiedName(...) const;`
  reason: Required read-only API for enumerating active project nodes during sync.
  confidence: high
- file: `Gui/CMakeLists.txt`
  symbol: source discovery
  approximate lines: 19-28
  stable anchor: `file(GLOB NatronGui_SOURCES *.cpp)`
  reason: New `Gui/*.h/.cpp` sync helper files are automatically included; no CMake edit should be necessary.
  confidence: high

## Allowed Edit Files
- `Gui/FluxTimeline.h`
- `Gui/FluxTimeline.cpp`
- `Gui/Gui05.cpp`
- `Gui/Gui.cpp`
- `Gui/Gui.h`
- `Gui/GuiPrivate.h`
- `Gui/FluxNodegraphTimelineSync.h` (new)
- `Gui/FluxNodegraphTimelineSync.cpp` (new)

## Read-Only Context Files
- `Gui/FluxTimelineSerialization.h`
- `Gui/FluxMaskUtils.h`
- `Gui/FluxMaskUtils.cpp`
- `Gui/FluxAiPanel.cpp`
- `Gui/CMakeLists.txt`
- `Engine/Node.h`
- `Engine/EffectInstance.h`
- `Engine/NodeGroup.h`
- `ARCHITECTURE.md`
- `plans/PHASES.md`
- `tasks/TASKS.md`

## Required Change

### Overview / affected files
Implement this as one coherent change with two layers:

1. Timeline UI/model additions in `FluxTimeline.{h,cpp}`:
   - effect-row drag state and insertion feedback;
   - dirty/banner/manual-sync state;
   - a signal/slot boundary so `Gui` can run nodegraph-to-timeline sync and clear dirty state only on success.
2. Graph bridge additions in `Gui05.cpp`, `Gui.cpp`, `Gui.h`, `GuiPrivate.h`, and new `FluxNodegraphTimelineSync.{h,cpp}`:
   - dirty flagging from node creation/removal/connection changes;
   - suppression while Flux itself rebuilds the graph;
   - graph traversal that returns a new `QList<FluxLayer>` from the actual nodegraph, preserving existing node pointers and metadata where possible.

Do not edit Engine files for this feature unless an existing public signal/API is proven unusable; if Engine edits seem necessary, stop and report.

### Reorder algorithm
1. Reuse existing `FluxTimeline::moveEffectInLayer()` as the final model mutation; it already moves `FluxEffect`, remaps `FluxMask::effectIndex`, refreshes visible rows, emits `effectsChanged`, selects the moved effect, and emits `compositingChanged`.
2. Add a separate effect drag interaction rather than overloading layer bar movement:
   - extend `InteractionMode` with `eModeReorderEffect`;
   - add private drag fields such as `_dragEffectLayerIndex`, `_dragEffectFromIndex`, `_dragEffectTargetIndex`, and `_dragEffectInsertionY`;
   - initialize them in the constructor and reset them on release/cancel.
3. In `mousePressEvent`, when a left click lands on `eFluxVisibleRowEffect` and the owner layer is not locked:
   - select the effect row as today;
   - start `eModeReorderEffect` only when the click is on the effect row body/label, not on the enable checkbox or child property/mask rows;
   - keep existing double-click selection/properties behavior.
4. In `mouseMoveEvent` for `eModeReorderEffect`:
   - find the target effect row in the same owning layer from `_visibleRows`;
   - ignore layer rows, mask rows, property rows, and effect rows from other layers;
   - update only `_dragEffectTargetIndex` and draw insertion feedback; do not mutate `_layers` continuously.
5. In `paintEvent`/a helper called from it, draw a clear insertion line/highlight at the target effect row while dragging.
6. In `mouseReleaseEvent`, if target is valid and differs from source, call `moveEffectInLayer(layer, from, target)` once. The existing `compositingChanged` connection will call `Gui::rebuildCompositingGraph`, whose effect loop wires `gizmo/source -> effect[0] -> effect[1] -> ... -> Merge A`, so the nodegraph order changes to match the timeline.
7. Preserve constraints:
   - locked layers cannot reorder effects;
   - adjustment rows can reorder their own effects if the row is unlocked;
   - effect masks must continue following the moved effect through `moveEffectInLayer`;
   - do not implement cross-layer effect moves in this task.

### Dirty flag and sync trigger data flow
1. Add timeline state/API:
   - `bool _nodegraphDirty`, `bool _syncBannerVisible`, banner/button hit rects;
   - `void setNodegraphDirty(bool dirty, const QString& reason = QString())`;
   - `bool isNodegraphDirty() const`;
   - `void clearNodegraphDirty()`;
   - signal `nodegraphSyncRequested()`.
2. Dirty trigger behavior:
   - `Gui::createNodeGUI` connects every created node with `Qt::UniqueConnection` to a small dirty-marking path for `Node::inputChanged(int)`, `Node::outputsChanged()`, and `Node::activated(bool)`; also mark dirty for node creation itself.
   - In `Gui::rebuildCompositingGraph`, use a scoped suppression flag in `GuiPrivate` so Flux-owned `disconnectInput/connectInput/createNode` calls do not mark the nodegraph dirty.
   - If the node is Flux AI Work Viewer or other non-compositing auxiliary viewer, do not mark timeline dirty for that viewer-only node. If classification is unclear, err on marking dirty; sync can no-op if graph is unchanged.
3. Timeline click trigger:
   - In `FluxTimeline::mousePressEvent`, make the dirty-banner check a top-level early check before row-specific handling, selection, drag setup, context-menu logic, or other returns. This avoids bypassing the banner when the click lands on a specialized row/control.
   - On any mouse press inside the timeline widget (not hover), if `_nodegraphDirty` is true and the banner is not visible, set `_syncBannerVisible = true`, repaint, and continue the click unless the click was on the banner button.
   - Render banner text exactly: `Nodegraph changes detected — click to sync`.
   - Clicking the banner/button emits `nodegraphSyncRequested()`.
4. Manual sync controls:
   - Add an always-present small painted sync button in the timeline ruler/header area. It should be clickable even when no banner is visible; when dirty, use the banner text/dirty styling; when clean, label it `Sync Nodegraph` or `Sync` and keep it visually secondary.
   - Add a `QShortcut` on the timeline for manual sync. Use `Ctrl+Shift+Y` only after the pre-implementation shortcut audit confirms it does not conflict; if it conflicts, stop and report instead of inventing a different shortcut.
5. Sync execution:
   - `Gui::setupFluxUi` connects `timeline->nodegraphSyncRequested` to `Gui::syncFluxTimelineFromNodegraph()` or an equivalent method.
   - On success: replace the timeline model, rebuild visible rows, call `rebuildCompositingGraph(timeline)` to normalize wiring/layout, clear dirty/banner state, update panels/selection.
   - On failure: leave the existing timeline model untouched, keep dirty/banner visible, and report a concise warning to stderr/status/log.

### Nodegraph-to-timeline sync algorithm
Implement the algorithm in new `Gui/FluxNodegraphTimelineSync.{h,cpp}` with a narrow API, for example:

- input: `Gui*`, current `QList<FluxLayer>`, `NodePtr bgReformat`, `NodePtr finalOutput`, main viewer node if useful;
- output: success flag, diagnostic message, and rebuilt `QList<FluxLayer>`.

Required traversal semantics, expressed as implementation pseudocode:

```text
syncFromNodegraph(currentLayers, bgReformat, finalOutput):
  root = validated bgReformat anchor; output = finalOutput or main viewer input 0, excluding Flux AI Work Viewer
  preserve = index currentLayers by node pointer and script name for timing/colors/expanded/locked/mute/solo/masks/AI/text metadata

  mainPipe = choose downstream path root -> output via consumers whose input 0 is the current pipe node
  if no path or multiple equally plausible paths: fail with diagnostic, do not mutate timeline

  rowsBottomToTop = []
  for each segment of mainPipe separated by main-pipe Merge nodes:
    mainPipeEffects = non-Merge effect nodes in graph order
    if mainPipeEffects not empty:
      append/reuse an adjustment row for this segment; if segment is after the last Merge, place it as Nick's bottom adjustment rule requires

    if segment ends at a main-pipe Merge:
      aPipe = walk that Merge input 1 exactly one layer/precomp level
      layer = classify/reuse footage, solid, text, or conservative source-backed row from the A-pipe source
      layer.mergeNode = segment Merge
      layer.effects = non-mask, non-precomp nodes between source/gizmo and Merge A input in source-to-merge order
      preserve/rebuild masks and derive hasPrecompBranch with `discoverMaskInput()` / `classifyLayerBranches()`; do not recurse nested Merges into extra timeline layers
      append layer

  newLayers = convert rowsBottomToTop to Flux top-to-bottom timeline order, matching `rebuildCompositingGraph` pixel-layer ordering
  replaceLayersFromNodegraph(newLayers) without dirtying the graph; preserve selected backing node when possible; do not serialize dirty/banner state
```

Notes the worker must preserve while implementing the pseudocode:
- Treat `net.sf.openfx.MergePlugin` / `PLUGINID_OFX_MERGE` nodes on the chosen main pipe as layer merge boundaries; input 0 is B/background/main pipe, input 1 is A/foreground/layer source.
- Rule 1: effect on main pipe between two Merges maps to an adjustment row for that segment.
- Rule 2: every main-pipe Merge A input maps to one timeline layer; do not recurse nested Merge/precomp structures into extra rows.
- Rule 3: layer A-flow nodes between source/gizmo and Merge A input map to that layer's effects.
- Rule 4: effect on the main pipe after a Merge with no other Merge below maps to the bottom adjustment layer per Nick's rule.
- Rule 5: Merge inside a layer A flow is a precomp branch marker, not a new timeline layer.
- Preserve existing `FluxEffect` metadata by matching node pointer/script name; otherwise set `pluginId = node->getPluginID()`, `label = node->getLabel()`, `enabled = !node->isNodeDisabled()` if an enabled API exists, else default true.

### Implementation steps
1. Before code changes, audit existing shortcuts for `Ctrl+Shift+Y` in the allowed edit files and read-only shortcut context already listed; if any existing Flux/Natron action uses it, stop and report the conflict instead of choosing an alternate shortcut.
2. Add `FluxNodegraphTimelineSync.{h,cpp}` with small structs for result/diagnostics and helper functions. Keep each new file under 500 lines; split further only if necessary and report before expanding scope.
3. Add timeline dirty/banner/manual-sync UI state, painting, hit-testing, and signal in `FluxTimeline.{h,cpp}`. In `mousePressEvent`, the dirty-banner check must be a top-level early check before row-specific handling so specialized row/control branches cannot bypass it.
4. Add effect drag reorder state and interaction in `FluxTimeline.{h,cpp}`, using `moveEffectInLayer()` only on release.
5. Add `Gui` dirty wiring:
   - `GuiPrivate` suppression/in-progress flags;
   - `Gui::createNodeGUI` node signal connections and node-created dirty mark;
   - scoped suppression in `Gui::rebuildCompositingGraph` and graph-to-timeline sync.
6. Add `Gui` sync entry point and connect it from `setupFluxUi` to the timeline signal/shortcut.
7. Implement the graph parser and model replacement path.
8. Ensure timeline-to-nodegraph sync still emits/rebuilds exactly as before for layer add/delete/reorder/trim/move, effect add/remove/reorder, masks, solo/mute/lock, and export final output.
9. Keep all new logic Qt5/Qt6 compatible: avoid Qt6-only APIs, use existing signal/slot style where surrounding code does.

## Non-Goals
- No Engine API changes unless public Node/NodeCollection APIs are proven insufficient.
- No cross-layer effect drag/move.
- No automatic background sync without user action.
- No recursive precomp expansion; only one A-pipe level is mapped.
- No new project serialization schema for dirty/banner state.
- No redesign of timeline visuals beyond the sync banner/button and effect insertion feedback.
- No changes to existing save/reopen semantics except preserving existing timeline data after sync.
- No task status updates or git commits.

## Validation
Commands:
- `cmake --build "${BUILD_DIR:-build}" --target Natron -j"$(nproc)"`
- If the repo uses a different configured build directory on this machine, run the equivalent existing Natron GUI target build and report the exact command.

Manual GUI validation (screenshots/recording required for GUI-control proof):
- Create/import a layer, add at least two effects, expand the layer, drag an effect above/below another effect, and verify the nodegraph inline chain reconnects in the same order.
- Repeat effect drag reorder on an adjustment row with two main-pipe effects.
- Lock a layer and verify its effect rows cannot be reordered.
- Manually add/connect an inline effect in a layer flow in the nodegraph; click the timeline; verify the banner appears only on click; click sync; verify the effect appears under that layer and the graph remains valid.
- Manually add an effect on the main pipe between two Merges; sync; verify an adjustment row is created/reused with that effect.
- Manually add an effect on the main pipe after a Merge with no other Merge below; sync; verify it maps to the bottom adjustment layer per Nick's rule.
- Manually connect a Merge into a main-pipe Merge A input; sync; verify only one level of that A-pipe becomes timeline rows/layers.
- Manually place a Merge inside an existing layer flow; sync; verify no extra timeline layer is created for that nested/precomp Merge.
- Run a regression pass for existing timeline -> nodegraph operations: add footage/solid/text, reorder layers, add/remove effects, trim/move, mute/solo, and confirm viewer/export final output still uses the rebuilt final output.
- Save/reopen a project after sync and verify existing serialization restores layer/effect node references.

Expected result:
- Build succeeds.
- Effect drag reorder visibly works in the timeline and reconnects the nodegraph order.
- Dirty banner appears on timeline click after manual nodegraph structural edits, not on hover.
- Manual sync button and shortcut request sync even when the banner is hidden.
- Nodegraph-to-timeline sync follows all five Nick sync rules without regressing timeline-to-nodegraph rebuilds.

## Stop Conditions
Stop and report if:
- target symbol is missing
- required fix exceeds allowed files
- validation cannot run
- existing architecture contradicts the requested change
- task requires product/design judgment not in packet
- `Ctrl+Shift+Y` conflicts with an existing Flux shortcut and no approved alternate exists
- nodegraph traversal has multiple plausible Reformat-to-final-output main-pipe paths and cannot choose deterministically
- the sync helper would need to recursively expand precomps or edit Engine internals
- preserving current masks/AI effect metadata would require serialization schema changes
- new helper files cannot stay below 500 lines without broader refactoring

## Planner Self-Check
- locator evidence sufficient: yes — anchors verified in timeline structs, effect reorder function, visible rows, mouse events, graph rebuild, Gui node creation, Node APIs, and NodeCollection APIs.
- allowed edit files minimal and explicit: yes — edits are limited to timeline UI/model, Gui bridge/dirty state, and two new sync helper files.
- read-only context minimal: yes — serialization/mask utilities/CMake/Engine APIs are read-only context only.
- anchors/lines included: yes — Relevant Locations include path, symbol, approximate lines, stable anchor, reason, and confidence.
- validation concrete: yes — build command plus task-specific manual GUI proof cases including screenshots/recording.
- parallelization decision explicit and safe: yes — single task; not parallelized because both features edit `FluxTimeline.{h,cpp}`/`Gui05.cpp` and share dirty/sync/rebuild state.
- non-goals and stop conditions sufficient: yes — scope excludes Engine edits, recursive precomp expansion, cross-layer moves, serialization changes, and shortcut/design ambiguity.
- reviewer findings addressed, if revision: yes — plan path remains `plans/plan-effect-reorder-and-sync.md`; sync algorithm is now pseudocode; `Ctrl+Shift+Y` audit is a required pre-implementation step; dirty-banner handling is specified as a top-level early `mousePressEvent` check.

## Required Return Contract
Return only a task-focused summary. Do not include transcript, tool logs, raw file dumps, large code blocks, or broad unrelated issues. Include status, files inspected/changed, validation evidence, blockers, and task-specific risks.