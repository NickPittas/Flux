# Planner Report

## Status
ready

## Rationale
The timeline already owns layer trim state, keyframe row discovery, keyframe drag preview, and mouse interaction in `Gui/FluxTimeline.{h,cpp}`; existing keyframe query/move helpers in `Gui/FluxKeyframeModel.*` are sufficient read-only support. The smallest safe implementation is to add timeline-local snap target collection, snap state, and visual feedback without changing Natron engine curves, nodegraph wiring, serialization semantics, or plugin data models.

# Task Packet

## User Goal
Keyframes and layer trim in/out edges should snap to other keyframes from any effect on any layer and to other layer trim edges while dragging. Snapping must update continuously during drag, use a configurable pixel tolerance, provide visual feedback, work for keyframe diamonds and trim handles, and avoid nodegraph changes.

## Mode
general-coding

## Relevant Locations
- file: `Gui/FluxTimeline.h`
  symbol: `class FluxTimeline`, `InteractionMode`, keyframe drag members
  approximate lines: 408-510
  stable anchor: `enum HitZone`, `enum InteractionMode`, `// Keyframe drag state`
  reason: Add small timeline-local snap data structures/state, tolerance accessors, and helper declarations.
  confidence: high
- file: `Gui/FluxTimeline.cpp`
  symbol: `FluxTimeline::FluxTimeline`
  approximate lines: 110-140
  stable anchor: `_dragPropertyIndex(-1)` initializer block
  reason: Initialize snap tolerance and active snap feedback state.
  confidence: high
- file: `Gui/FluxTimeline.cpp`
  symbol: `FluxTimeline::drawLayerBars`
  approximate lines: 2042-2123 and 2304-2445
  stable anchor: `// ── Layer bar (right area) ──`, `// ── Draw keyframe diamonds in the timeline area ──`, `// Draw the dragged key at its current target position`
  reason: Existing paint path draws layer bars, trim handles, keyframes, and dragged key preview; add snap line/highlight overlay here or via a new `drawSnapIndicator()` called from `paintEvent`.
  confidence: high
- file: `Gui/FluxTimeline.cpp`
  symbol: `FluxTimeline::paintEvent`
  approximate lines: 1738-1793
  stable anchor: `drawLayerBars(painter, barsRect);`, `drawPlayhead(painter, totalRect);`
  reason: Best call site for a global snap guide line after layer/keyframe drawing and before/after playhead, depending desired z-order.
  confidence: high
- file: `Gui/FluxTimeline.cpp`
  symbol: `FluxTimeline::frameToX`, `FluxTimeline::xToFrame`
  approximate lines: 2560-2571
  stable anchor: `return _layerLabelWidth + (int)( (frame - _firstFrame) * _zoom ) - _scrollOffsetX;`
  reason: Convert pixel tolerance into timeline-frame proximity and draw feedback at snapped frame.
  confidence: high
- file: `Gui/FluxTimeline.cpp`
  symbol: `FluxTimeline::hitTest`
  approximate lines: 3216-3270
  stable anchor: `// Check trim handles`
  reason: Confirms trim handles are layer-row only and are the interaction start point for trim snapping.
  confidence: high
- file: `Gui/FluxTimeline.cpp`
  symbol: `FluxTimeline::mousePressEvent`
  approximate lines: 3537-3567 and 3587-3618
  stable anchor: `nearestKeyTimeForProperty(prop, xToFrame(x), toleranceFrames, &nearestTime)`, `// Store interaction start state`
  reason: Existing keyframe hit-test uses hard-coded 8px tolerance; drag state for keyframes and trim handles is established here. Also clear stale snap state when starting pan, resize, playhead drag, or empty-space interactions.
  confidence: high
- file: `Gui/FluxTimeline.cpp`
  symbol: `FluxTimeline::mouseMoveEvent`
  approximate lines: 3645-3724 and 3775-3785
  stable anchor: `case eModeTrimLeft`, `case eModeTrimRight`, `case eModeDragKeyframe`
  reason: Apply continuous snap on every mouse move for trim handles and keyframe drag preview.
  confidence: high
- file: `Gui/FluxTimeline.cpp`
  symbol: `FluxTimeline::mouseReleaseEvent`
  approximate lines: 3798-3855
  stable anchor: `if (_interactionMode == eModeDragKeyframe)`, `moveKeysAtTime(prop, roundedOld, roundedNew)`
  reason: Commit the snapped preview frame on release and clear snap feedback state.
  confidence: high
- file: `Gui/FluxTimeline.cpp`
  symbol: `FluxTimeline::rebuildVisibleRows`, `refreshKeyframeSignalConnections`
  approximate lines: 2608-2719 and 2734-2930
  stable anchor: `buildLayerProperties`, `buildEffectProperties`, `buildMaskProperties`, `keysForProperty`
  reason: Shows the existing all-property keyframe discovery APIs that snap target collection should reuse without relying on rows being expanded/visible.
  confidence: high
- file: `Gui/FluxKeyframeModel.h`
  symbol: `FluxKeyframeProperty`, `FluxKeyframeKey`, `keysForProperty`, `nearestKeyTimeForProperty`, `moveKeysAtTime`
  approximate lines: 38-149
  stable anchor: `Return keyframe times for a property row`, `Move keyframes from oldTime to newTime`
  reason: Read-only API contract for collecting keyframe snap targets and committing keyframe moves.
  confidence: high
- file: `Gui/FluxKeyframeModel.cpp`
  symbol: `buildLayerProperties`, `buildEffectProperties`, `buildMaskProperties`, `keysForProperty`
  approximate lines: 1-505
  stable anchor: `collectKnobProperties`, `shouldShowKnob`, `getAnimatedDimensions`
  reason: Confirms property builders enumerate layer/effect/mask animated keys from existing Flux-owned node models, including text animator properties exposed through `buildLayerProperties` for text layers.
  confidence: high
- file: `Gui/FluxKeyframeModelOps.cpp`
  symbol: `nearestKeyTimeForProperty`, `moveKeysAtTime`
  approximate lines: 18-72 and 315-334
  stable anchor: `KeyFrameSet keyFrames = curve->getKeyFrames_mt_safe();`, `return moveKnobKeysAtTime(property, oldTime, newTime);`
  reason: Existing key hit-test and move behavior should remain the commit path; do not duplicate curve mutation.
  confidence: high
- file: `Gui/FluxTimelineSerialization.h`
  symbol: `FluxLayerSerialization`, `FluxTimelineSerialization`
  approximate lines: 296-306 and 489-525
  stable anchor: `// Timing`, `showKeyframeCurves`
  reason: Read-only confirmation that layer edge timing is serialized already; snap tolerance/feedback should not alter project serialization unless explicitly approved.
  confidence: high
- file: `Engine/Curve.h`
  symbol: `KeyFrame`, `KeyFrameSet`, `Curve::getKeyFrames_mt_safe`
  approximate lines: 39-110 and 180-220
  stable anchor: `typedef std::set<KeyFrame, KeyFrame_compare_time> KeyFrameSet;`
  reason: Read-only engine evidence for keyframe time model; implementation must not modify Engine.
  confidence: medium

## Allowed Edit Files
- `Gui/FluxTimeline.h`
- `Gui/FluxTimeline.cpp`

## Read-Only Context Files
- `Gui/FluxTimelineSerialization.h`
- `Gui/FluxKeyframeModel.h`
- `Gui/FluxKeyframeModel.cpp`
- `Gui/FluxKeyframeModelOps.cpp`
- `Engine/Curve.h`
- `ARCHITECTURE.md`
- `plans/PHASES.md`
- `tasks/TASKS.md`

## Required Change
Implement timeline-local snapping only in `FluxTimeline`.

Overview:
- Add snap target collection for two target kinds: keyframe times and layer edge times.
- Add continuous snap application during `eModeDragKeyframe`, `eModeTrimLeft`, and `eModeTrimRight` mouse movement.
- Add visible feedback when snapping is active: a vertical guide line at the snapped frame plus a small label/marker; optionally accent the dragged key diamond/trim handle using existing paint paths.
- Make snap tolerance configurable in pixels through a `FluxTimeline` member with default value, not a hard-coded local literal.
- Do not modify nodegraph rebuild logic, Engine classes, keyframe storage, project serialization, or the semantics of `inPoint/outPoint/timeOffset`.

Data structures to add in `Gui/FluxTimeline.h`:
- A small private enum, e.g. `SnapTargetKind { eSnapNone, eSnapKeyframe, eSnapLayerEdge }`.
- A small private struct, e.g. `SnapTarget { int frame; SnapTargetKind kind; int layerIndex; int propertyIndex; }`.
- Private snap state:
  - `_snapTolerancePixels`, default 8 or 10 pixels.
  - `_snapActive`.
  - `_snapFrame`.
  - `_snapKind`.
- Optional public methods if consistent with surrounding style:
  - `int snapTolerancePixels() const;`
  - `void setSnapTolerancePixels(int pixels);`
  These satisfy configurability without adding UI or serialization. Clamp to a sane range such as 1-50 pixels. If adding public methods is considered API surface creep, keep a private member plus private setter; do not add project persistence.
- Private helper declarations:
  - `QList<SnapTarget> collectSnapTargets(InteractionMode mode, int sourceLayerIndex, int sourcePropertyIndex, double sourceKeyTime) const;`
  - `bool resolveSnapFrame(int candidateFrame, const QList<SnapTarget>& targets, int* snappedFrame, SnapTargetKind* snappedKind) const;`
  - `void clearSnapState();`
  - `void setSnapState(int snappedFrame, SnapTargetKind kind);`
  - `void drawSnapIndicator(QPainter& painter, const QRect& rect);`

Snap target algorithm:
1. Convert the pixel tolerance to frames for comparison using the current zoom:
   - `toleranceFrames = qMax(0.0, (double)_snapTolerancePixels / qMax(_zoom, 0.0001));`
   - Compare pixel distance as a tie-breaker using `qAbs(frameToX(targetFrame) - frameToX(candidateFrame))` so the configured tolerance remains pixel-based.
2. Collect layer edge targets from all `_layers`, regardless visibility/expanded state:
   - start edge = `layer.inPoint + layer.timeOffset`
   - end edge = `layer.outPoint + layer.timeOffset`
   - Include footage, solid, text, adjustment, and null rows if they have meaningful edges; layer edge targets are allowed even if the target layer is locked/muted.
   - Exclude the exact edge currently being dragged from the source layer to avoid self-snapping. For left trim, exclude source layer start edge; for right trim, exclude source layer end edge. The opposite edge may remain a target only if it cannot violate the one-frame minimum duration clamp.
3. Collect keyframe targets from all modeled Flux properties, not just expanded visible property rows:
   - For each layer: call `buildLayerProperties(layer, _ungroupedKeyframeProperties)`, then `keysForProperty` on each property.
   - Text animator keyframes are covered by this layer-property pass: existing text layer/animator controls are exposed through `buildLayerProperties`, so do not add a parallel text-specific traversal unless implementation inspection proves the local property builder no longer exposes them.
   - For each effect on each layer: call `buildEffectProperties(effect, isAdjustmentRow(layerIndex), _ungroupedKeyframeProperties)`, then `keysForProperty`.
   - For layer/effect masks: call `buildMaskProperties(mask)`, then `keysForProperty`.
   - Round key times to integer frame targets using the same convention as existing paint/commit code: `static_cast<int>(time + 0.5)` / `std::round`.
   - For keyframe dragging, exclude the dragged property key at `_dragOrigKeyTime` so the key can move away from its original frame. Do not exclude other properties or other layers at the same frame.
   - Hidden adjustment-row disable keys do not need to become keyframe targets because adjustment row trim edges are already layer edge targets.
4. Pick the nearest target within `_snapTolerancePixels` in screen pixels. If distances tie, prefer keyframes over layer edges only if that gives clearer feedback; otherwise keep deterministic order: smaller pixel distance, then lower frame number, then target kind enum order.
5. Return the unsnapped candidate if no target is within tolerance and call `clearSnapState()` so stale guide lines never remain visible.

Interaction-specific behavior:
- Keyframe drag (`mouseMoveEvent`, `eModeDragKeyframe`):
  - Candidate = `qBound(_firstFrame, xToFrame(x), _lastFrame)`.
  - Resolve snap targets excluding the dragged key.
  - Set `_dragCurrentKeyTime` to the snapped frame when active, otherwise candidate.
  - Update `_selectedKeyTime` only on successful release/move if existing behavior requires it; avoid changing native curves during mouse move.
  - On release, existing `moveKeysAtTime(prop, roundedOld, roundedNew)` commits the snapped preview frame.
- Left trim (`mouseMoveEvent`, `eModeTrimLeft`):
  - Candidate timeline edge = `_interactionOrigInPoint + _interactionOrigTimeOffset + frameDelta`.
  - Resolve snap targets excluding this layer's start edge.
  - Convert snapped timeline edge back to source in-point: `newIn = snappedEdgeFrame - _layers[layerIndex].timeOffset`.
  - Clamp after snapping so `newIn <= outPoint - 1`.
  - Preserve trim model: `inPoint` changes; `timeOffset` does not change. Existing code currently does not update `trimStart` on left trim during mouse move; do not invent a broad model rewrite unless inspection shows a local bug. If changing `trimStart` is necessary for correctness, keep it scoped and explain in return.
- Right trim (`mouseMoveEvent`, `eModeTrimRight`):
  - Candidate timeline edge = `_interactionOrigOutPoint + _interactionOrigTimeOffset + frameDelta`.
  - Resolve snap targets excluding this layer's end edge.
  - Convert snapped timeline edge to source out-point: `newOut = snappedEdgeFrame - _layers[layerIndex].timeOffset`.
  - Clamp after snapping so `newOut >= inPoint + 1`.
  - Preserve existing `trimEnd` update logic, but compute it from the final snapped/clamped `newOut`.
- Mouse press / mode switches:
  - Add explicit `clearSnapState()` calls before entering non-snap modes so stale visual feedback cannot survive a branch change:
    - pan branch, e.g. Alt/middle-drag that sets `eModePan`;
    - layer-label resize branch that sets `eModeResizeLabel` or equivalent;
    - playhead/ruler drag branch that sets playhead-drag interaction mode;
    - empty-space click/deselect branch that clears selection or starts no timeline drag.
  - When starting a snap-capable trim/keyframe drag, initialize state by calling `clearSnapState()` first, then let the first `mouseMoveEvent` set active snap feedback.
- Mouse release:
  - Keep the existing commit paths: `updateLayerTrimKnobs`, `updateAdjustmentTrimKeyframes`, and `moveKeysAtTime`.
  - Clear snap state after committing/canceling and repaint.
- No snapping is required for full bar horizontal moves unless Nick explicitly extends the scope. Do not alter `eModeMoveBar` behavior.

Visual feedback:
- Add `drawSnapIndicator(QPainter&, QRect)` and call it from `paintEvent` after `drawLayerBars` and before or after `drawPlayhead`.
- When `_snapActive` is true, draw a vertical guide at `frameToX(_snapFrame)` from the ruler through the visible timeline area, clipped to the timeline area (`x >= _layerLabelWidth`). Use a distinct color that does not conflict with the red playhead, e.g. cyan/blue.
- Draw a small label near the ruler such as `Snap: 42` or a short icon; keep it unobtrusive.
- Existing dragged key drawing already uses a bright yellow diamond; when snapping is active, optionally use the snap guide color or a slightly thicker outline. Trim handles can rely on the global snap guide unless a small handle accent can be added without large paint refactoring.

Implementation steps:
1. In `FluxTimeline.h`, add the snap enum/struct/state/helper declarations and tolerance accessor/setter or private member. Keep additions small and private where possible. The target collector declaration must be exactly `QList<SnapTarget> collectSnapTargets(InteractionMode mode, int sourceLayerIndex, int sourcePropertyIndex, double sourceKeyTime) const;` unless local naming conventions require only whitespace/style changes.
2. Initialize snap state in `FluxTimeline::FluxTimeline` near existing interaction/keyframe drag state.
3. Replace the keyframe hit-test hard-coded `8.0 / _zoom` in `mousePressEvent` and property context-menu areas with `_snapTolerancePixels / _zoom` or a small helper, so tolerance is consistently configurable. Do not change key selection semantics beyond using the member tolerance.
4. Implement `collectSnapTargets` in `FluxTimeline.cpp` near interaction helpers or near `hitTest`:
   - Build layer edge targets from `_layers`.
   - Build keyframe targets from `buildLayerProperties`, `buildEffectProperties`, `buildMaskProperties`, `keysForProperty`.
   - Rely on `buildLayerProperties` to cover layer-owned text animator keyframes; do not introduce text-specific duplicate logic unless the local builder does not expose those properties.
   - Exclude the active source target as described above.
5. Implement `resolveSnapFrame` to choose the nearest candidate by pixel distance and update `out` values only on success; call `clearSnapState()` on no-match paths.
6. Update `mouseMoveEvent` for `eModeTrimLeft`, `eModeTrimRight`, and `eModeDragKeyframe` to call snap helpers continuously.
7. Update `mousePressEvent` mode-switch branches to explicitly call `clearSnapState()` for pan, label/header resize, playhead drag, and empty-space click/no-drag branches. Also clear at the start of snap-capable drag setup to reset stale state before movement.
8. Update `mouseReleaseEvent` to clear snap state in all exit paths.
9. Add `drawSnapIndicator` and call it from `paintEvent`.
10. Build and manually validate with GUI proof.

## Shift-Toggle Snap Behavior

Snapping is governed by the Shift key — it is **OFF by default** and only enabled while Shift is held. This applies to all snap-capable interactions (keyframe drag, trim-left, trim-right).

### Rules

1. **Snapping is OFF by default.** No snap targets are collected and no snap guide is drawn unless Shift is held.
2. **Holding Shift during a drag enables snapping.** When Shift is first detected during a snap-capable drag, call `collectSnapTargets()` and begin snapping the dragged element to the nearest target.
3. **Releasing Shift mid-drag disables snapping immediately.** Call `clearSnapState()`. The dragged keyframe or trim handle follows the raw mouse position freely from that point onward.
4. **Shift is the sole toggle.** There is no permanent snap-on/snap-off preference or toolbar button. Pressing/releasing Shift is the only way to toggle snap state.

### Implementation details

- In `mouseMoveEvent`, for each of the snap-capable `InteractionMode` cases (`eModeDragKeyframe`, `eModeTrimLeft`, `eModeTrimRight`), check `QApplication::keyboardModifiers() & Qt::ShiftModifier` at the top of the move handler.
- **Shift just pressed (transition from not-held to held):**
  - Set a `_snapShiftHeld` flag to `true`.
  - Call `collectSnapTargets()` to populate the snap target list for the current drag context.
  - Proceed with snap resolution as described in the main snap algorithm.
- **Shift just released (transition from held to not-held):**
  - Set `_snapShiftHeld` to `false`.
  - Call `clearSnapState()` to remove the snap guide and reset snap state.
  - Continue the drag using the raw (unsnapped) candidate frame.
- **Shift unchanged:** apply or skip snapping based on the current `_snapShiftHeld` flag.
- Add `bool _snapShiftHeld = false;` to the snap state members in `FluxTimeline.h`.
- In `mousePressEvent`, initialize `_snapShiftHeld` based on whether Shift is already held at press time. If Shift is held when the drag starts, collect targets immediately.
- In `mouseReleaseEvent`, clear `_snapShiftHeld` along with the rest of snap state.

### Pseudocode (mouseMoveEvent, snap-capable branch)

```
bool shiftNow = (QApplication::keyboardModifiers() & Qt::ShiftModifier) != 0;

if (shiftNow && !_snapShiftHeld) {
    // Shift just pressed — begin snapping
    _snapShiftHeld = true;
    auto targets = collectSnapTargets(mode, sourceLayer, sourceProp, sourceTime);
    if (resolveSnapFrame(candidateFrame, targets, &snappedFrame, &snappedKind)) {
        setSnapState(snappedFrame, snappedKind);
        useFrame = snappedFrame;
    }
} else if (!shiftNow && _snapShiftHeld) {
    // Shift just released — stop snapping
    _snapShiftHeld = false;
    clearSnapState();
    useFrame = candidateFrame;
} else if (_snapShiftHeld) {
    // Shift held throughout — keep snapping
    auto targets = collectSnapTargets(mode, sourceLayer, sourceProp, sourceTime);
    if (resolveSnapFrame(candidateFrame, targets, &snappedFrame, &snappedKind)) {
        setSnapState(snappedFrame, snappedKind);
        useFrame = snappedFrame;
    } else {
        clearSnapState();
        useFrame = candidateFrame;
    }
} else {
    // Shift not held — no snapping
    useFrame = candidateFrame;
}
```

### Interaction with existing snap plan

- The `collectSnapTargets()`, `resolveSnapFrame()`, `setSnapState()`, and `clearSnapState()` helpers are unchanged from the main plan.
- The snap guide (`drawSnapIndicator`) is only drawn when `_snapActive && _snapShiftHeld`.
- The existing tolerance algorithm, target exclusion rules, and visual feedback all apply when Shift enables snapping.
- `clearSnapState()` calls added for pan, label resize, playhead drag, and empty-space click branches in `mousePressEvent` remain as specified — those are mode-exit cleanups independent of Shift state.
- `mouseReleaseEvent` clears both `_snapActive` and `_snapShiftHeld` on all exit paths.

### Validation additions

In addition to the existing validation steps:

11. Start a keyframe drag without Shift — confirm no snap guide appears and the keyframe follows the mouse freely.
12. While dragging, press Shift — confirm snapping activates mid-drag and the keyframe snaps to the nearest target with a visible guide.
13. While dragging with Shift held, release Shift — confirm snapping deactivates immediately, the guide disappears, and the keyframe follows the mouse.
14. Press Shift again mid-drag — confirm snapping re-activates.
15. Confirm the above also works for left-trim and right-trim handle drags.
16. Confirm that releasing the mouse while Shift is held commits the snapped position correctly, and releasing without Shift commits the raw position.

## Non-Goals
- Do not modify `Engine/`, Natron `Curve`, `Knob`, `Bezier`, or nodegraph rebuild behavior.
- Do not add project serialization for snap tolerance unless Nick explicitly asks for per-project persistence.
- Do not add a preferences UI or menu item unless already-existing local timeline settings UI is found inside the allowed files.
- Do not snap full layer bar moves/reorders; scope is keyframe drags and trim handles only.
- Do not change keyframe interpolation, grouped-key collision rules, or `moveKeysAtTime` behavior.
- Do not make hidden adjustment disable keys visible in keyframe rows.
- Do not add a separate text animator keyframe traversal unless `buildLayerProperties` inspection proves text animator controls are not represented there.
- Do not refactor the large timeline file beyond the minimal helpers required for this feature.

## Validation
Commands:
- `cmake --build "${BUILD_DIR:-build}" --target Natron --parallel "$(nproc)"`
- Manual GUI validation in Flux/Natron after build:
  1. Create at least two layers with animated properties/effects at distinct frames; expand enough rows to drag a visible keyframe.
  2. Include a text layer with text animator keyframes and confirm those text animator keyframes are available as snap targets through the normal layer-property path.
  3. Drag a keyframe near another layer/effect/text-animator keyframe and confirm it snaps during movement, shows a snap guide, and lands on the snapped frame after release.
  4. Drag a keyframe near another layer's in/out edge and confirm continuous snap/guide and correct final frame.
  5. Drag a layer left trim handle near keyframes and other layer edges; confirm the trim edge snaps continuously, guide appears, and `FrameRange`/timeline bar final state matches release position.
  6. Drag a layer right trim handle near keyframes and other layer edges; confirm the same behavior and that one-frame minimum duration is preserved.
  7. Start pan, label/header resize, playhead drag, and empty-space click interactions immediately after a snap; confirm the snap guide clears every time.
  8. Confirm full layer bar move/reorder behavior is unchanged.
  9. Confirm no nodegraph topology changes are created by keyframe or trim snapping beyond existing trim/keyframe commit side effects.
  10. Capture screenshot or short recording showing snap guide while dragging and final snapped state.

Expected result:
- Build succeeds.
- During keyframe and trim-handle drags, the previewed key/edge continuously jumps to snap targets within the configured pixel tolerance and displays a visible snap guide.
- On release, existing keyframe/trim commit paths persist the snapped frame.
- Text animator keyframes participate as snap targets via `buildLayerProperties`.
- Snap visual state clears on pan, resize, playhead drag, empty-space click, no-target movement, release, and cancel paths.
- Node graph remains unchanged except for pre-existing trim/keyframe knob updates.

## Stop Conditions
Stop and report if:
- target symbol is missing
- required fix exceeds allowed files
- validation cannot run
- existing architecture contradicts the requested change
- task requires product/design judgment not in packet
- collecting keyframe targets requires Engine/API changes instead of existing `FluxKeyframeModel` APIs
- continuous snapping would require mutating native keyframes during mouse move rather than previewing and committing on release
- snap tolerance persistence or UI is required to satisfy Nick but cannot be done inside the allowed files
- text animator keyframes are not exposed through `buildLayerProperties` and adding support would require files outside `Gui/FluxTimeline.{h,cpp}`
- left-trim correctness requires changing established `inPoint/outPoint/timeOffset/trimStart/trimEnd` semantics beyond a narrow local fix

## Planner Self-Check
- locator evidence sufficient: yes — previous approved plan supplied exact files and high-confidence anchors in `FluxTimeline.{h,cpp}` plus keyframe helper APIs; this revision only clarifies reviewer-requested details.
- allowed edit files minimal and explicit: yes — only `Gui/FluxTimeline.h` and `Gui/FluxTimeline.cpp` are needed for timeline-local snapping.
- read-only context minimal: yes — keyframe model, serialization, and `Engine/Curve.h` are cited only to verify existing data/query contracts.
- anchors/lines included: yes — each relevant location includes path, symbol, approximate lines, stable anchor, reason, and confidence.
- validation concrete: yes — build command plus manual GUI checks, text animator coverage check, snap-clear checks, and screenshot/recording requirement for user-facing behavior.
- parallelization decision explicit and safe: yes — single task; all required edits share the same widget state and mouse/paint paths, so parallelization would risk conflicts in `FluxTimeline.{h,cpp}`.
- non-goals and stop conditions sufficient: yes — they prevent nodegraph, Engine, serialization, preferences UI, duplicate text traversal, and unrelated movement/refactor scope creep.
- reviewer findings addressed, if revision: yes — revised plan adds the exact `collectSnapTargets` signature, explicit `clearSnapState()` requirements for pan/resize/playhead/empty-space `mousePressEvent` branches, and confirmation that text animator keyframes are covered by `buildLayerProperties`.

## Required Return Contract
Return only a task-focused summary. Do not include transcript, tool logs, raw file dumps, large code blocks, or broad unrelated issues. Include status, files inspected/changed, validation evidence, blockers, and task-specific risks.
