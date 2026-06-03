# Planner Report

## Status
ready

## Rationale
This revision keeps the implementation scoped to the Flux timeline model/painting and the existing Gui05 viewer connection integration point, while explicitly addressing the reviewed gaps: timeline switching cannot reuse nodegraph `ConnectCommand` because it has no `NodeGui`/`Edge` selection, adjustment rows resolve to their final enabled effect node rather than a merge node, timeline number-key handling must use Natron's native-key normalization, and transient badges must be cleared before structural timeline mutations.

# Task Packet

## User Goal
From the Flux timeline, selecting a layer or effect and pressing number keys 1-9 should connect the corresponding main viewer input to the selected timeline node, mirroring Natron nodegraph viewer switching behavior from the timeline surface. Effect rows connect directly to their effect node; normal layer rows connect to the full layer output immediately before that layer's Merge; adjustment layer rows connect to the last enabled adjustment effect node; key `1` always reconnects viewer input 1 to the full comp output. Timeline rows should show a small transient badge for viewer inputs connected through this timeline action.

## Mode
general-coding

## Relevant Locations
- file: `Gui/FluxTimeline.h`
  symbol: `struct FluxEffect`, `struct FluxLayer`, `class FluxTimeline`
  approximate lines: 90-180, 220-360
  stable anchor: `struct FluxEffect {`, `struct FluxLayer {`, `virtual void keyPressEvent(QKeyEvent* event) OVERRIDE;`
  reason: Add transient viewer-input badge state, expose a focused signal/helper API for timeline viewer switching, and keep selection resolution inside the timeline model that already tracks selected layer/effect.
  confidence: high
- file: `Gui/FluxTimeline.cpp`
  symbol: `FluxTimeline::keyPressEvent`
  approximate lines: 4384-4440
  stable anchor: `void FluxTimeline::keyPressEvent(QKeyEvent* event)`
  reason: Add unmodified number-key handling for `Qt::Key_1` through `Qt::Key_9` using `Gui::handleNativeKeys(event->key(), event->nativeScanCode(), event->nativeVirtualKey())`; preserve existing Delete/Ctrl+D/F/Tab/Escape behavior.
  confidence: high
- file: `Gui/FluxTimeline.cpp`
  symbol: `FluxTimeline::removeLayer`
  approximate lines: 332-379
  stable anchor: `FluxTimeline::removeLayer(int index)`
  reason: Structural layer removal deactivates/removes nodes and shifts indices; clear all viewer-input badges before the structural mutation begins.
  confidence: high
- file: `Gui/FluxTimeline.cpp`
  symbol: `FluxTimeline::duplicateLayer`
  approximate lines: 382-780
  stable anchor: `bool FluxTimeline::duplicateLayer(int index)`
  reason: Structural duplication copies/pastes nodes and inserts a new row; clear all viewer-input badges before the structural mutation begins.
  confidence: high
- file: `Gui/FluxTimeline.cpp`
  symbol: `FluxTimeline::removeEffectFromLayer`
  approximate lines: 940-979
  stable anchor: `bool FluxTimeline::removeEffectFromLayer(int layerIndex, int effectIndex)`
  reason: Structural effect removal deactivates effect-owned nodes, shifts effect indices, and may invalidate badge targets; clear all viewer-input badges before the structural mutation begins.
  confidence: high
- file: `Gui/FluxTimeline.cpp`
  symbol: `FluxTimeline::drawLayerBars`
  approximate lines: 1910-2230
  stable anchor: `if (visibleRow.type == eFluxVisibleRowLayer)` and `} else if (visibleRow.type == eFluxVisibleRowEffect) {`
  reason: Draw small viewer-input badges beside layer/effect labels without changing row layout or timeline bar behavior.
  confidence: high
- file: `Gui/FluxTimeline.cpp`
  symbol: `FluxTimeline::rebuildVisibleRows`
  approximate lines: 2580-2725
  stable anchor: `FluxVisibleRow row;` and `FluxVisibleRow effectRow;`
  reason: Relevant to row identity and effect/layer indexing used by badge repainting; no broad model rebuild is needed.
  confidence: high
- file: `Gui/NodeGraph30.cpp`
  symbol: `NodeGraph::connectCurrentViewerToSelection`
  approximate lines: 60-138
  stable anchor: `pushUndoCommand( new ConnectCommand(this, foundInput, foundInput->getSource(), selected) );`
  reason: Nodegraph viewer switching uses selected `NodeGui`, viewer input `Edge`, and `ConnectCommand`; FluxTimeline does not have `NodeGui`/`Edge` selection, so this mechanism is not reusable for timeline row selection.
  confidence: high
- file: `Gui/Gui05.cpp`
  symbol: existing Flux viewer wiring
  approximate lines: 1090-1110, 1505-1515, 1595-1605, 1688-1698
  stable anchor: `viewerNode->disconnectInput(0);` and `viewerNode->connectInput(..., 0);`
  reason: Existing Flux viewer paths wire viewers by raw `disconnectInput`/`connectInput`, which is the mechanism timeline viewer switching should follow.
  confidence: high
- file: `Gui/Gui05.cpp`
  symbol: `Gui::getFluxMainCompositingViewerTab`, `Gui::showFluxViewerTab`
  approximate lines: 298-333
  stable anchor: `ViewerTab* Gui::getFluxMainCompositingViewerTab() const`
  reason: Existing main viewer lookup excludes the Flux AI Work Viewer and can be reused for timeline viewer switching.
  confidence: high
- file: `Gui/Gui05.cpp`
  symbol: `setupFluxUi` timeline signal wiring area
  approximate lines: 1390-1710
  stable anchor: `QObject::connect(timeline, &FluxTimeline::layerSelected, this,` and `QObject::connect(timeline, &FluxTimeline::effectSelected, this,`
  reason: Add one focused connection from the timeline's new viewer-input request signal to Gui-owned viewer connection code.
  confidence: high
- file: `Gui/Gui05.cpp`
  symbol: adjustment layer branch in `Gui::rebuildCompositingGraph`
  approximate lines: 2605-2637
  stable anchor: `if (layer.type == QString::fromUtf8("adjustment")) {` and `if (effect.enabled) { adjustmentOutput = effect.node; }`
  reason: Adjustment layers do not have a merge node; their output is the last enabled effect node in the adjustment effect chain, or null if no enabled active effects exist.
  confidence: high
- file: `Gui/Gui05.cpp`
  symbol: `Gui::rebuildCompositingGraph`
  approximate lines: 2471-3388
  stable anchor: `// Connect the final output to the main composition viewer only.` and `// Store final output node for export panel`
  reason: Confirms full comp output is `lastOutput` / `_imp->_fluxFinalOutputNode`, viewer input 0 is connected through `viewerNode->disconnectInput(0); viewerNode->connectInput(lastOutput, 0);`, and normal layer Merge input 1 is the full layer output before Merge.
  confidence: high
- file: `Gui/Gui50.cpp`
  symbol: `Gui::handleNativeKeys`, `Gui::keyPressEvent`
  approximate lines: 354-512
  stable anchor: `Gui::handleNativeKeys(int key, quint32 nativeScanCode, quint32 nativeVirtualKey)` and `Qt::Key key = (Qt::Key)Gui::handleNativeKeys(...)`
  reason: Natron normalizes number-key handling across keyboard layouts using native scan/virtual key data; FluxTimeline must use the same public static helper before checking `Qt::Key_1`...`Qt::Key_9`.
  confidence: high
- file: `Gui/Gui.h`
  symbol: `Gui::handleNativeKeys`
  approximate lines: 539-542
  stable anchor: `static int handleNativeKeys(int key, quint32 nativeScanCode, quint32 nativeVirtualKey) WARN_UNUSED_RETURN;`
  reason: Confirms the native-key helper is public static and callable from `FluxTimeline::keyPressEvent`.
  confidence: high
- file: `Engine/ViewerInstance.h`
  symbol: `ViewerInstance::getNInputs`, `ViewerInstance::setInputA`, `ViewerInstance::setInputB`, `ViewerInstance::getInputLabel`
  approximate lines: 275-281, 373-405
  stable anchor: `void setInputA(int inputNb);` and `return QString::number(inputNb + 1).toStdString();`
  reason: Confirms viewer supports indexed inputs labelled 1-based.
  confidence: high
- file: `Engine/Node.h`
  symbol: `Node::getInput`, `Node::canConnectInput`, `Node::connectInput`, `Node::disconnectInput`
  approximate lines: 361-370, 552-572
  stable anchor: `NodePtr getInput(int index) const;`, `canConnectInput`, and `virtual bool connectInput(const NodePtr& input, int inputNumber);`
  reason: Confirms the connection API, the validation API requested by review, and the requirement to disconnect an occupied input before reconnecting.
  confidence: high

## Allowed Edit Files
- `Gui/FluxTimeline.h`
- `Gui/FluxTimeline.cpp`
- `Gui/Gui05.cpp`

## Read-Only Context Files
- `Gui/NodeGraph30.cpp`
- `Gui/Gui50.cpp`
- `Gui/Gui.h`
- `Engine/ViewerInstance.h`
- `Engine/Node.h`
- `Gui/Gui.cpp`
- `ARCHITECTURE.md`
- `plans/PHASES.md`
- `tasks/TASKS.md`

## Required Change

### Overview
Implement timeline-local number-key viewer switching without changing nodegraph behavior. The timeline should emit a request when it has focus and the user presses normalized `1`-`9`; Gui05 should resolve the main compositing viewer, validate that the target can connect to the requested input, connect the input to the correct node using Flux's existing raw viewer wiring mechanism, redraw the viewer, and tell the timeline to update transient badges.

### Affected Files
1. `Gui/FluxTimeline.h`
   - Add transient badge storage to `FluxLayer` and `FluxEffect`, preferably `QList<int> viewerInputBadges;` or equivalent small Qt5/Qt6-compatible container.
   - Add a signal such as `void viewerInputSwitchRequested(int viewerInputIndex);` where the index is zero-based (`0` for key `1`, `1` for key `2`, etc.). Keep the signal primitive-only to avoid Qt metatype issues with `NodePtr`.
   - Add public helper methods for Gui05 to call, for example:
     - `NodePtr selectedViewerSwitchTargetNode(int viewerInputIndex) const;`
     - `void setViewerInputBadgeForNode(int viewerInputIndex, const NodePtr& node);`
     - `void clearViewerInputBadge(int viewerInputIndex);`
     - `void clearAllViewerInputBadges();`
   - Keep these helpers narrow; do not expose mutable layer lists or broad selection internals.

2. `Gui/FluxTimeline.cpp`
   - Include `Gui/Gui.h` if not already available in this translation unit, so `Gui::handleNativeKeys` can be called.
   - In `FluxTimeline::keyPressEvent`, before falling through to `QWidget::keyPressEvent(event)`, handle only unmodified normalized number keys `Qt::Key_1` through `Qt::Key_9`:
     - Compute `Qt::Key key = (Qt::Key)Gui::handleNativeKeys(event->key(), event->nativeScanCode(), event->nativeVirtualKey());`.
     - Use the returned `key`, not raw `event->key()`, for the `Qt::Key_1`...`Qt::Key_9` check.
     - Require `event->modifiers() == Qt::NoModifier` so shortcuts such as Ctrl+number or Alt+number are not stolen.
     - Map `Qt::Key_1` to `viewerInputIndex = 0`, ..., `Qt::Key_9` to `viewerInputIndex = 8`.
     - Emit `viewerInputSwitchRequested(viewerInputIndex)`, accept the event, and return.
     - Preserve all existing Delete/Ctrl+Shift+D/Ctrl+D/F/Tab/Escape behavior.
   - Implement `selectedViewerSwitchTargetNode(int viewerInputIndex) const`:
     - If `viewerInputIndex == 0`, return an empty `NodePtr`; Gui05 owns full-comp resolution through `_imp->_fluxFinalOutputNode`.
     - If `_selectedType == eFluxSelectionEffect`, validate `_selectedLayer`, `_selectedEffectIndex`, `effect.node`, and `effect.node->isActivated()`, then return `effect.node`.
     - If `_selectedType == eFluxSelectionLayer`, validate `_selectedLayer` and branch by layer type:
       - For `layer.type == QString::fromUtf8("adjustment")`, return the last enabled, active effect node in `layer.effects` order, or an empty `NodePtr` if there are no enabled active effects. Document in code that adjustment layers do not have a `mergeNode`; this mirrors `Gui05.cpp` where `adjustmentOutput` advances to each enabled adjustment effect and the last enabled effect becomes the row output.
       - For non-adjustment layers, validate `layer.mergeNode` and `layer.mergeNode->isActivated()`, then return `layer.mergeNode->getInput(1)`. This is the authoritative full-layer output immediately before Merge A, matching `rebuildCompositingGraph` where Merge input 1 is this layer's output after effects/masks.
     - For masks, properties, text animator rows, no selection, missing nodes, or inactive nodes, return empty `NodePtr` for inputs 2-9.
   - Implement badge update helpers:
     - For a requested input index, remove that input number from every layer/effect badge list first, so one viewer input displays on at most one timeline target.
     - `clearAllViewerInputBadges()` must remove every viewer badge from every layer/effect and `update()` if anything changed.
     - If the connected node is empty or is the full comp target for input 1, leave no layer/effect badge.
     - If the connected node matches an effect node, add the one-based badge number (`viewerInputIndex + 1`) to that `FluxEffect`.
     - Else if the connected node matches a non-adjustment layer's current `mergeNode->getInput(1)`, add the badge to that `FluxLayer`.
     - Else if the connected node matches an adjustment layer's last enabled active effect node, add the badge to that adjustment `FluxLayer` rather than duplicating the badge on the child effect row, because this represents the adjustment row as the selected layer output.
     - Call `update()` after badge changes.
   - Add badge cleanup before structural mutations:
     - In `duplicateLayer(int index)`, after validating the index/row can duplicate and before any clipboard copy/paste or row insertion, call `clearAllViewerInputBadges()`.
     - In `removeLayer(int index)`, after validating the layer can be removed and before deactivating nodes or removing the row, call `clearAllViewerInputBadges()`.
     - In `removeEffectFromLayer(int layerIndex, int effectIndex)`, after validating indices/lock state and before `takeAt()`/node deactivation/mask reindexing, call `clearAllViewerInputBadges()`.
     - Do not serialize badges and do not attempt to remap badges across these structural changes.
   - Draw badges in `drawLayerBars`:
     - Layer row: draw a compact rounded rectangle in the name column near the right edge of the available label area, before/eliding around it so the layer name does not overpaint it. Badge text is the one-based input number, e.g. `2`.
     - Effect row: draw the same badge style near the right edge of `effectLabelRect`; reduce `effectAvailW` by the badge width before eliding the label.
     - Use existing palette-compatible colors: small dark/blue or amber background, light text, no new resources, no stylesheet dependency.
     - If multiple badges exist on one row, draw them left-to-right or right-aligned as small adjacent chips; keep total width bounded and elide text first.

3. `Gui/Gui05.cpp`
   - In the existing Flux timeline setup/wiring block near other `QObject::connect(timeline, ...)` connections, add a connection for `viewerInputSwitchRequested`.
   - In the lambda:
     1. Validate `getApp()`, `timeline`, `viewerInputIndex >= 0`, and `viewerInputIndex < 9`.
     2. Get the main composition viewer with `getFluxMainCompositingViewerTab()`; do not use or create the Flux AI Work Viewer.
     3. Resolve `viewerNode` via `ViewerTab::getInternalNode()->getNode()` and validate active, non-AI viewer.
     4. Resolve target node:
        - For `viewerInputIndex == 0`, use `_imp->_fluxFinalOutputNode` as full comp. If null, fall back to `viewerNode->getInput(0)` only if active; otherwise do nothing and report a scoped warning to stderr.
        - For inputs 2-9, call `timeline->selectedViewerSwitchTargetNode(viewerInputIndex)` and require a non-null active node.
     5. Connect using the existing evidenced Flux mechanism, with validation:
        - Save the current input node for possible restore: `NodePtr previousInput = viewerNode->getInput(viewerInputIndex);`.
        - If `previousInput == targetNode`, treat the connection as already successful and only update badges/show/redraw.
        - Otherwise call `viewerNode->disconnectInput(viewerInputIndex);` because `canConnectInput` may reject an occupied socket as already connected.
        - Before connecting, call `viewerNode->canConnectInput(targetNode, viewerInputIndex)` and require the OK return value used by Natron's `Node::canConnectInput` API.
        - If `canConnectInput` fails, attempt to restore `previousInput` if it is still active and connectable, clear that input's badge, print a scoped warning, and return without showing a misleading badge.
        - If validation succeeds, call `bool connected = viewerNode->connectInput(targetNode, viewerInputIndex);`.
        - Verify `connected` and/or `viewerNode->getInput(viewerInputIndex) == targetNode`; on failure, attempt to restore `previousInput` if safe, clear that input's badge, and print a scoped warning.
     6. If successful, call `timeline->setViewerInputBadgeForNode(viewerInputIndex, targetNode);`, `showFluxViewerTab(viewerTab);`, and `redrawAllViewers();`.
   - Do not modify `Gui::rebuildCompositingGraph` except if necessary to keep input 1's badge cleared after automatic full-comp reconnects. Avoid reconnecting viewer inputs 2-9 during graph rebuild.

### Viewer Connection Mechanism
Nodegraph viewer switching in `Gui/NodeGraph30.cpp` works by resolving a selected `NodeGui`, locating the viewer input `Edge`, and pushing `ConnectCommand(this, foundInput, foundInput->getSource(), selected)` for undoable nodegraph graph edits. Timeline lacks NodeGui/Edge selection, so we cannot reuse `ConnectCommand`. For timeline viewer switching, use raw `disconnectInput`/`connectInput` directly, matching existing Flux viewer wiring in `Gui05.cpp`, and skip undo for this operation. Add `canConnectInput` validation before connecting as described above. Viewer input labels are one-based in `ViewerInstance::getInputLabel`, while `Node::connectInput` is zero-based, so the UI key/badge number must always be converted by subtracting one.

### Adjustment Layer Mapping
Adjustment layers do not have a `mergeNode`. In `selectedViewerSwitchTargetNode`, when the selected layer type is adjustment, return the last enabled active effect node in that adjustment row, or null if there are no enabled active effects. This mirrors the adjustment branch in `Gui05.cpp`: `adjustmentOutput` starts as the upstream comp, each enabled adjustment effect advances `adjustmentOutput`, and the final enabled effect becomes the adjustment row output. Do not attempt to use `layer.mergeNode` for adjustment rows.

### Visual Indicator Design
Badges are transient UI indicators stored in the timeline model, not serialized project data. A badge is a small rounded chip with the one-based viewer input number (`2`, `3`, etc.) drawn on the layer/effect row that was last connected through the timeline for that input. Pressing `1` connects full comp and clears badge `1` from all layer/effect rows because full comp is not a layer/effect row. Pressing `2` on a new target removes badge `2` from the previous target and shows it on the new layer/effect. If a node cannot be resolved, `canConnectInput` fails, or `connectInput` fails, do not show a badge. Any structural mutation through `duplicateLayer`, `removeLayer`, or `removeEffectFromLayer` clears all badges before changing model/node structure.

## Non-Goals
- Do not change nodegraph keyboard handling or nodegraph selection behavior.
- Do not create, delete, or reorder compositing nodes except for existing duplicate/remove behavior already in the touched functions.
- Do not add undo/redo for timeline viewer input switching; this intentionally skips undo because it cannot use nodegraph `ConnectCommand` and matches existing Flux raw viewer wiring.
- Do not serialize viewer badges or change project file format.
- Do not preserve/remap badges across duplicate/remove structural mutations.
- Do not alter Flux AI Work Viewer behavior or source-capture viewer routing.
- Do not implement mask/property/text-animator row viewer switching unless Nick explicitly approves it.
- Do not change Viewer A/B active input UI semantics beyond connecting the requested viewer input socket.

## Validation
Commands:
- `cmake --build "${BUILD_DIR:-build}" --target Natron --parallel`
- Launch Flux/Natron from the built target using the project's normal run command, create/import a simple comp with at least two layers, one normal layer effect, and one adjustment row with at least two effects, and manually verify the GUI behavior below.

Expected result:
- Build succeeds with no new compile errors.
- Timeline focused + selected effect + `2`: main viewer node input index 1 is connected to that effect's node; the effect row shows badge `2`; viewer refreshes.
- Timeline focused + selected normal layer + `2`: main viewer node input index 1 is connected to `layer.mergeNode->getInput(1)`, i.e. the layer output before Merge with effects/masks included; the layer row shows badge `2`.
- Timeline focused + selected adjustment layer + `2`: main viewer node input index 1 is connected to the last enabled active adjustment effect node; the adjustment layer row shows badge `2`; if all adjustment effects are disabled/missing, no connection occurs and no badge is shown.
- Timeline focused + `1`: main viewer input index 0 is connected to `_imp->_fluxFinalOutputNode`/full comp; badge `1` is not shown on layer/effect rows.
- Timeline number keys work through `Gui::handleNativeKeys`, including native digit key handling on the current keyboard layout.
- Existing nodegraph viewer switching still works when the nodegraph has focus.
- Flux AI Work Viewer source/AI preview paths still use their own viewer and are not affected.
- Duplicating a layer, removing a layer, or removing an effect clears all existing viewer-input badges before the model structure changes.
- Capture a screenshot or short recording showing the timeline focus, selected layer/effect/adjustment badge, and visible viewer response for at least effect→input 2, normal layer→input 2, and adjustment layer→input 2.

## Stop Conditions
Stop and report if:
- target symbol is missing
- required fix exceeds allowed files
- validation cannot run
- existing architecture contradicts the requested change
- task requires product/design judgment not in packet
- `Gui::handleNativeKeys` cannot be called from `FluxTimeline.cpp` without introducing a circular dependency or broader include refactor
- `Node::canConnectInput` has a signature/return contract that cannot be checked locally in `Gui05.cpp` without broad API changes
- the main viewer cannot be resolved without creating a new viewer or touching the Flux AI Work Viewer
- normal layer full-output resolution via `mergeNode->getInput(1)` is null or contradicts `rebuildCompositingGraph`'s Merge A wiring
- adjustment layer output cannot be resolved as the last enabled active effect node without broader graph traversal
- implementing persistent badges, saved viewer-input state, or undo/redo becomes necessary

## Planner Self-Check
- locator evidence sufficient: yes — implementation files and APIs are anchored in `FluxTimeline.h/cpp`, `Gui05.cpp`, `NodeGraph30.cpp`, `Gui50.cpp`, `Gui.h`, `ViewerInstance.h`, and `Node.h`; reviewer-requested mechanism, adjustment mapping, native-key handling, and badge cleanup locations are all covered.
- allowed edit files minimal and explicit: yes — three explicit source files, all directly needed for timeline input/model/paint state and Gui-owned viewer connection.
- read-only context minimal: yes — limited to the nodegraph mechanism trace, Flux viewer wiring/adjustment branch, native-key helper, viewer/node API files, and mandatory project planning/context files.
- anchors/lines included: yes — relevant locations include path, symbol/anchor, approximate lines, reason, and confidence.
- validation concrete: yes — build command plus focused GUI manual checks and screenshot/recording requirement for user-facing behavior.
- parallelization decision explicit and safe: yes — single task; changes share timeline model/paint, structural mutation helpers, and one Gui05 signal hookup, so splitting would create unnecessary integration risk.
- non-goals and stop conditions sufficient: yes — scope excludes nodegraph changes, AI viewer routing, serialization, badge remapping, undo/redo, and unapproved row types.
- reviewer findings addressed, if revision: yes — nodegraph `ConnectCommand` non-reuse/raw wiring/skip-undo plus `canConnectInput`, adjustment-layer last-enabled-effect mapping, native-key handling through `Gui::handleNativeKeys`, and structural badge cleanup for `duplicateLayer`, `removeLayer`, and `removeEffectFromLayer` are all explicitly included.

## Required Return Contract
Return only a task-focused summary. Do not include transcript, tool logs, raw file dumps, large code blocks, or broad unrelated issues. Include status, files inspected/changed, validation evidence, blockers, and task-specific risks.
