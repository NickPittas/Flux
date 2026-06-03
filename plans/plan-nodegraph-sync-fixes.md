# Planner Report

## Status
ready

## Rationale
The reviewer supplied high-confidence, file-specific implementation details and the inspected source confirms the affected Flux compositing bridge, timeline paint/release handlers, Gui private state, and Node `inputChanged(int)` signal anchors. This is a single cohesive change: track only the current Flux compositing tree, mark a cheap dirty flag on external nodegraph input edits, and lazily sync selected timeline-facing state when the user next clicks the timeline.

# Task Packet

## User Goal
Implement nodegraph-to-timeline sync fixes so only nodes in the active Flux compositing tree can mark the timeline dirty, dirty handling remains O(1), Flux timeline rebuilds do not self-mark dirty, and sync runs lazily only when the user clicks/releases on the timeline while dirty.

## Mode
general-coding

## Relevant Locations
- file: `Gui/GuiPrivate.h`
  symbol: `struct GuiPrivate`
  approximate lines: 300-323
  stable anchor: `_fluxTimeline`, `_fluxMergeNodes`, `_fluxBgReformatNode`, `_fluxFinalOutputNode`
  reason: Add Flux nodegraph-sync state next to existing Flux compositing graph/export state.
  confidence: high
- file: `Gui/Gui.h`
  symbol: `class Gui`
  approximate lines: 373-380 and 582-727
  stable anchor: `void rebuildCompositingGraph(class FluxTimeline* timeline);`, `public Q_SLOTS:`, `void deferredInitGizmoParams(class FluxTimeline* timeline);`
  reason: Declare public dirty/sync API and the private slot connected to `Node::inputChanged(int)`.
  confidence: high
- file: `Gui/Gui.cpp`
  symbol: `Gui` method implementations
  approximate lines: file-level existing includes include `Gui/FluxTimeline.h`
  stable anchor: `#include "Gui/FluxTimeline.h"`
  reason: Implement dirty accessors and the O(1) compositing-tree dirty slot per reviewer direction.
  confidence: medium
- file: `Gui/Gui05.cpp`
  symbol: `Gui::rebuildCompositingGraph(FluxTimeline*)`
  approximate lines: 2561-3492
  stable anchor: `Gui::rebuildCompositingGraph(FluxTimeline* timeline)`, `NodePtr lastOutput = _imp->_fluxBgReformatNode`, `_imp->_fluxFinalOutputNode = lastOutput`
  reason: Guard Flux-owned rebuilds, update compositing-tree signal connections by diffing current upstream tree nodes, and keep tree tracking tied to the final output.
  confidence: high
- file: `Gui/Gui05.cpp`
  symbol: `Gui::deferredInitGizmoParams(FluxTimeline*)`
  approximate lines: 3495-3698
  stable anchor: `layer.gizmoNode->disconnectInput(0);`, `layer.gizmoNode->connectInput(readNode, 0);`, retry `QTimer::singleShot(300, this, ...)`
  reason: Guard deferred Flux-owned reconnect/knob initialization so its signal emissions do not dirty the timeline.
  confidence: high
- file: `Gui/Gui05.cpp`
  symbol: blend/effect sync patterns
  approximate lines: 3156 and 3328-3348
  stable anchor: `effect.node->setNodeDisabled(!effect.enabled);`, `getKnobByName("blendingMode")`, `getKnobByName("operation")`
  reason: Existing Flux writes effect enabled state and gizmo blend mode into graph; sync should read those nodegraph states back into the timeline/gizmo model.
  confidence: high
- file: `Gui/FluxTimeline.cpp`
  symbol: `FluxTimeline::paintEvent(QPaintEvent*)`
  approximate lines: 1923-2010
  stable anchor: `QPainter painter(this);`, `drawTimeRuler(painter, rulerRect);`, `drawLayerBars(painter, barsRect);`
  reason: Draw an orange dirty banner when `Gui` reports the nodegraph is dirty.
  confidence: high
- file: `Gui/FluxTimeline.cpp`
  symbol: `FluxTimeline::mouseReleaseEvent(QMouseEvent*)`
  approximate lines: 4433-4550
  stable anchor: `clearSnapState();`, `_interactionMode = eModeNone;`, `update();`
  reason: Trigger lazy nodegraph-to-timeline sync after a timeline click/release only when the dirty flag is true.
  confidence: high
- file: `Engine/Node.h`
  symbol: `Node::inputChanged(int)` and `Node::getGuiInputs() const`
  approximate lines: 405-407 and 1443-1447
  stable anchor: `const std::vector<NodeWPtr> & getGuiInputs() const`, `void inputChanged(int);`
  reason: Read-only API evidence for upstream tree traversal and signal connection.
  confidence: high

## Allowed Edit Files
- `Gui/GuiPrivate.h`
- `Gui/Gui.h`
- `Gui/Gui.cpp`
- `Gui/Gui05.cpp`
- `Gui/FluxTimeline.cpp`

## Read-Only Context Files
- `Engine/Node.h`
- `Gui/FluxTimeline.h`
- `Gui/Gui40.cpp`
- `ARCHITECTURE.md`
- `plans/PHASES.md`
- `tasks/TASKS.md`

## Required Change
Implement exactly these scoped changes:

1. `Gui/GuiPrivate.h`
   - Add `#include <set>` with the existing STL includes if needed.
   - Add these members immediately after `_fluxFinalOutputNode`:
     - `bool _fluxNodeGraphDirty = false;`
     - `bool _fluxSyncInProgress = false;`
     - `std::set<NodeWPtr, std::owner_less<NodeWPtr>> _fluxCompositingTreeNodes;`
   - Do not move or rename existing Flux members.

2. `Gui/Gui.h`
   - In the public Flux API area near `rebuildCompositingGraph(class FluxTimeline* timeline);`, declare:
     - `bool isFluxNodeGraphDirty() const;`
     - `void clearFluxNodeGraphDirty();`
     - `void syncFluxTimelineFromNodeGraph();`
   - In the private slot/method area, declare:
     - `void onCompositingTreeNodeChanged(int inputNb = 0);`
   - Keep this as a Qt slot-compatible member so it can be connected with string `SIGNAL`/`SLOT` syntax.

3. `Gui/Gui.cpp`
   - Implement `isFluxNodeGraphDirty()` as a null-safe return of `_imp->_fluxNodeGraphDirty`.
   - Implement `clearFluxNodeGraphDirty()` as a null-safe assignment to `false`; if the Flux timeline exists, schedule repaint/update so the banner disappears after sync.
   - Implement `onCompositingTreeNodeChanged(int inputNb)` as an O(1) dirty handler:
     - `Q_UNUSED(inputNb);`
     - Return immediately if `_imp` is null.
     - Return immediately if `_imp->_fluxSyncInProgress` is true.
     - Return immediately if `_imp->_fluxNodeGraphDirty` is already true.
     - Otherwise set `_imp->_fluxNodeGraphDirty = true` and call `_imp->_fluxTimeline->update()` if non-null.
   - Do not inspect nodes, traverse the graph, rebuild rows, sync properties, or perform any other non-O(1) work in this slot.

4. `Gui/Gui05.cpp` guard support
   - Add a small file-local RAII guard near the existing helper area or immediately before `rebuildCompositingGraph()`:
     - It stores a reference to `_fluxSyncInProgress`, remembers the previous value, sets it to `true` in the constructor, and restores the previous value in the destructor.
   - Use this guard after the early null checks at the top of `rebuildCompositingGraph()` so all Flux-owned disconnect/connect/knob writes during rebuild are ignored by the dirty handler.
   - Use the same guard after early null checks in `deferredInitGizmoParams()` so deferred Read→FluxLayer reconnects and initialization knob writes also cannot mark the timeline dirty. This must cover the block anchored by `layer.gizmoNode->disconnectInput(0);` / `connectInput(readNode, 0)`.

5. `Gui/Gui05.cpp` compositing tree tracking and signal diff
   - At the end of `rebuildCompositingGraph()`, after `lastOutput` is known and `_imp->_fluxFinalOutputNode = lastOutput` is assigned, collect the active compositing tree by walking upstream from `lastOutput` through each node's `getGuiInputs()`.
   - Use a `std::set<NodeWPtr, std::owner_less<NodeWPtr>>` for the new tree. Insert a node before recursing into its inputs so cycles/shared branches cannot recurse forever.
   - Include only nodes reachable upstream from the final compositing output. This must include the Flux background/Reformat, Merge chain, layer gizmos/readers/effects/masks/adjustments that actually feed the current final output, and must exclude unrelated nodegraph nodes not connected to the compositing tree.
   - Diff `_imp->_fluxCompositingTreeNodes` against the newly collected set:
     - For nodes present in old but not new, if `lock()` succeeds, call `QObject::disconnect(node.get(), SIGNAL(inputChanged(int)), this, SLOT(onCompositingTreeNodeChanged(int)));`
     - For nodes present in new but not old, if `lock()` succeeds, call `QObject::connect(node.get(), SIGNAL(inputChanged(int)), this, SLOT(onCompositingTreeNodeChanged(int)));`
     - Swap/assign `_imp->_fluxCompositingTreeNodes` to the new set.
   - Use classic Qt `SIGNAL`/`SLOT` syntax exactly as above. Do not use lambdas with `Qt::UniqueConnection` for this tracking path.
   - Do not connect every node in the nodegraph; only connect nodes discovered from the active final output tree.

6. `Gui/Gui05.cpp` implement `syncFluxTimelineFromNodeGraph()`
   - Add a guarded public method implementation using the same RAII guard to prevent its own writes from re-dirtying.
   - Null-check `_imp`, `getApp()`, and `_imp->_fluxTimeline` / `getFluxTimeline()` and return safely if unavailable.
   - Iterate the timeline layers via the existing model (`timeline->getLayers()` with the same const-cast pattern already used by `rebuildCompositingGraph()` when mutation is required).
   - Blend mode sync:
     - For non-adjustment layers with both `layer.gizmoNode` and `layer.mergeNode`, read the Merge node `operation` knob and write the same integer value to the gizmo `blendingMode` knob when both cast to `KnobIntBase`.
     - Null-check every node and knob before use.
   - Effect enabled-state sync:
     - For every `FluxEffect` in every layer or adjustment row, if `effect.node` exists and is activated, set `effect.enabled = !effect.node->isNodeDisabled()`.
     - Do not create, delete, reconnect, or reorder effects/layers/masks during this sync.
   - After syncing, call `clearFluxNodeGraphDirty()`, `timeline->refreshVisibleRows()`, and `timeline->update()`.
   - Keep the method narrowly limited to the reviewer-specified state: blending mode and effect enabled state. If deeper structural reconstruction is required, stop and report instead of inventing a broader sync algorithm.

7. `Gui/FluxTimeline.cpp` dirty banner
   - In `paintEvent`, after the background/accent/ruler setup and before or just after the layer bars are drawn, query `Gui* gui = getGui();` and `gui->isFluxNodeGraphDirty()`.
   - If dirty, draw a non-invasive orange banner across the timeline width with clear text such as: `Node graph changed — click timeline to sync`.
   - Keep drawing local to `paintEvent`; do not add persistent banner state in `FluxTimeline.h` for this task because the reviewer’s required state lives in `GuiPrivate` and no header change is authorized.
   - Ensure the banner does not trigger rebuild/sync work while painting.

8. `Gui/FluxTimeline.cpp` lazy sync trigger
   - In `mouseReleaseEvent`, after existing interaction handling and before the final `update()` returns, query `Gui* gui = getGui();`.
   - If `gui && gui->isFluxNodeGraphDirty()`, call `gui->syncFluxTimelineFromNodeGraph();`.
   - Do not sync on every paint, mouse move, node signal, or timeline rebuild. Sync must run only from this dirty-check click/release path.

9. Preserve existing behavior
   - Do not alter layer creation, deletion, effect reorder, mask graph, export graph, AI work viewer, project serialization, PyPlug code, or task/status files.
   - Do not change the existing `blendingMode` live-sync lambda unless required for compilation; this task is about nodegraph-to-timeline dirty/sync and tree-scoped signal tracking.
   - Do not add shortcut/UI actions beyond the banner and click-to-sync behavior.

## Non-Goals
- No broad nodegraph scanning or syncing every node in the project.
- No timeline model reconstruction from arbitrary nodegraph topology.
- No layer/effect/mask add/delete/reorder inference from manual nodegraph edits.
- No serialization schema changes.
- No changes to `Gui/FluxTimeline.h` unless compilation proves an existing declaration is required; prefer keeping all new timeline behavior in `FluxTimeline.cpp` and `Gui` APIs.
- No generated files, lockfiles, task status updates, or git operations.

## Validation
Commands:
- `git diff --check -- Gui/GuiPrivate.h Gui/Gui.h Gui/Gui.cpp Gui/Gui05.cpp Gui/FluxTimeline.cpp`
- `cmake --build build --target NatronGui -j"$(nproc)"`
- `cmake --build build --target Natron -j"$(nproc)"`

Manual GUI checks:
- Launch Flux/Natron from the existing build/run workflow.
- Add footage/layers from the timeline and confirm no nodegraph-dirty banner appears from Flux-owned `rebuildCompositingGraph()` or `deferredInitGizmoParams()` operations.
- In the node graph, edit a node that is upstream of the Flux final compositing output (for example disconnect/reconnect a layer Merge input or disable an effect node). Confirm the orange banner appears once and remains stable without repeated UI work.
- Click/release on the timeline while dirty. Confirm sync runs, the banner disappears, effect enabled state in the timeline matches the node’s disabled state, and Merge `operation` changes propagate back to the layer gizmo `blendingMode` control.
- Create or modify an unrelated node not connected to the final Flux compositing output. Confirm it does not show the dirty banner.
- Add many unrelated nodes or repeatedly edit connected nodes while dirty and confirm there is no visible UI slowdown; repeated `inputChanged(int)` events should be cheap no-ops once the bool is already true.

Expected result:
- Build succeeds.
- Only active compositing-tree nodes trigger the dirty banner.
- Flux-owned rebuild/init/sync paths do not self-dirty.
- Dirty handler remains O(1).
- Sync runs only on timeline click/release while dirty and clears the banner after updating blend/effect enabled state.

## Stop Conditions
Stop and report if:
- target symbol is missing
- required fix exceeds allowed files
- validation cannot run
- existing architecture contradicts the requested change
- task requires product/design judgment not in packet
- `NodeWPtr` cannot be stored in `std::set<NodeWPtr, std::owner_less<NodeWPtr>>` as specified by the reviewer
- `Node::inputChanged(int)` cannot be connected with classic `SIGNAL`/`SLOT` syntax to `Gui::onCompositingTreeNodeChanged(int)`
- implementing sync requires reconstructing timeline layers/effects/masks from arbitrary nodegraph topology rather than the reviewer-specified blend/effect-enabled sync
- unrelated nodegraph nodes still trigger dirty after tree-scoped connection diffing
- Flux-owned `rebuildCompositingGraph()` or `deferredInitGizmoParams()` still triggers dirty after guard placement

## Planner Self-Check
- locator evidence sufficient: yes — reviewer supplied exact file changes and inspected anchors confirm the affected files/symbols and Node APIs.
- allowed edit files minimal and explicit: yes — five explicit files matching reviewer source of truth; no broad directories.
- read-only context minimal: yes — only Node signal/API evidence, related timeline header/getter context, and mandated project docs.
- anchors/lines included: yes — each relevant location includes path, symbol, approximate lines, stable anchor, reason, and confidence.
- validation concrete: yes — diff check, CMake GUI/app build commands, and task-specific manual GUI checks.
- parallelization decision explicit and safe: yes — single task; all edits are interdependent across shared Gui/timeline files and should be performed sequentially by one worker.
- non-goals and stop conditions sufficient: yes — they prevent global graph sync, model reconstruction, unrelated file edits, and product/design expansion.
- reviewer findings addressed, if revision: not applicable — no reviewer findings for this plan revision were supplied; the provided reviewer analysis is used as source of truth.

## Required Return Contract
Return only a task-focused summary. Do not include transcript, tool logs, raw file dumps, large code blocks, or broad unrelated issues. Include status, files inspected/changed, validation evidence, blockers, and task-specific risks.
