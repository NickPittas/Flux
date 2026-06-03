# Locator Report

## Summary
T073 edit surface is mostly `Gui/DopeSheetView.cpp` for native Dope Sheet keyframe/row readability, with `Gui/FluxTimeline.cpp` and `Gui/FluxKeyframeModel.*` as verified context for FluxTimeline animated property rows/key data.

## Confidence
high

## Relevant Locations
1. `file:///home/npittas/Flux/Gui/DopeSheetView.cpp`
   - symbol: `DopeSheetViewPrivate`, `generateKeyframeTextures()`, `drawRows()`, `drawNodeRow()`, `drawKnobRow()`, `drawKeyframes()`, `drawTexturedKeyframe()`
   - approximate lines: ~142-238, ~817-872, ~1033-1148, ~1310-1535
   - stable anchor: `void DopeSheetViewPrivate::drawKeyframes(const DSNodePtr &dsNode) const`
   - why relevant: native Dope Sheet painting surface: row backgrounds/separators, keyframe texture upload, individual/master key diamond drawing, selected-key time labels, fallback diamond rendering.
   - evidence: source reads settings colors for rows, draws keys from `dsKnob->getInternalKnob()->getCurve(ViewIdx(0), dim)` fallback to `knobGuiP->getCurve(...)`, draws dim-row keys and master keys for knob/node rows; fallback diamond exists if texture invalid.

2. `file:///home/npittas/Flux/Gui/DopeSheet.cpp`
   - symbol: `DopeSheet::addNode()`, `DopeSheet::findDSKnob()`, `DopeSheet::onKeyframeSetOrRemoved()`, `DSNode::DSNode()`, `DSKnob::getInternalKnob()`
   - approximate lines: ~312-370, ~483-505, ~1018-1030, ~1060-1135, ~1541-1600
   - stable anchor: `DSNode::DSNode(DopeSheet *model, DopeSheetItemType itemType, const NodeGuiPtr &nodeGui, QTreeWidgetItem *nameItem)`
   - why relevant: Dope Sheet data source and refresh wiring. Creates DSKnob rows from `nodeGui->getKnobs()`, stores authoritative knob in DSKnob, connects `KnobGui` keyframe signals to model refresh.
   - evidence: `addNode()` filters animatable nodes; `DSNode::DSNode()` creates root/dim items for animatable knobs and connects `keyFrameSet`, `keyFrameRemoved`, `refreshDopeSheet`; `onKeyframeSetOrRemoved()` emits hierarchy refresh plus `modelChanged()`.

3. `file:///home/npittas/Flux/Gui/DopeSheetHierarchyView.cpp`
   - symbol: `isFluxManagedDopeSheetNode()`, `HierarchyViewPrivate::checkNodeVisibleState()`, `checkKnobVisibleState()`, `HierarchyView::onKeyframeSetOrRemoved()`
   - approximate lines: ~57-66, ~364-420, ~1030-1040
   - stable anchor: `void HierarchyViewPrivate::checkKnobVisibleState(DSKnob *dsKnob)`
   - why relevant: row visibility/stability gates for native Dope Sheet. Flux-managed nodes bypass settings-panel visibility, knobs/dims are hidden based on real curve animation.
   - evidence: Flux nodes are identified by plugin ID prefix `net.sf.openfx.Flux`; common nodes are hidden unless animated; knob rows prefer `dsKnob->getInternalKnob()->getCurve()` before GUI curves.

4. `file:///home/npittas/Flux/Gui/DopeSheetEditor.cpp`
   - symbol: `DopeSheetEditor::DopeSheetEditor()`
   - approximate lines: ~95-140
   - stable anchor: `_imp->dopeSheetView = new DopeSheetView(...)`
   - why relevant: widget composition and signal chain. Confirms native editor is split hierarchy + GL view, and model signals connect to both.
   - evidence: constructs `DopeSheet`, `HierarchyView`, `DopeSheetView`; connects `keyframeSetOrRemoved` to hierarchy and model changes/selection to view redraw.

5. `file:///home/npittas/Flux/Gui/FluxTimeline.cpp`
   - symbol: `FluxTimeline::paintEvent()` property-row block, `FluxTimeline::rebuildVisibleRows()`
   - approximate lines: ~1831-2010, ~2165-2285, ~2297-2437, ~3060-3080, ~3365-3375
   - stable anchor: `// ── Draw keyframe diamonds in the timeline area ──`
   - why relevant: FluxTimeline animated property row UI/readability surface and comparison target. Keyframes are painted as QPainter diamonds; curve preview uses sampled native curves.
   - evidence: property labels use 7pt font and muted colors; key diamonds are 6px half-width, solid/hollow for full/partial grouped keys, selected keys yellow; rows are built under layers/masks/effects from model helpers.

6. `file:///home/npittas/Flux/Gui/FluxTimeline.h`
   - symbol: `FluxTimeline` layout/display state
   - approximate lines: ~389-466
   - stable anchor: `static const int kPropertyRowHeight = 20;`
   - why relevant: low-risk constants/state for property-row readability: row heights, keyframe curve toggle, grouping state, row dirty flag, selected key state.
   - evidence: `kPropertyRowHeight` is 20, `_showKeyframeCurves`, `_ungroupedKeyframeProperties`, `_propertyRows`, `_selectedKeyTime` are declared here.

7. `file:///home/npittas/Flux/Gui/FluxKeyframeModel.h`
   - symbol: `FluxKeyframeKey`, `FluxKeyframeProperty`, `buildLayerProperties()`, `buildEffectProperties()`, `buildMaskProperties()`, `keysForProperty()`
   - approximate lines: ~39-118
   - stable anchor: `struct FluxKeyframeProperty`
   - why relevant: FluxTimeline property-row/keyframe data contract.
   - evidence: key row properties include owner node, knob, dims, grouping key, roto aggregate flag; key handles expose `isPartial` and animated dim counts.

8. `file:///home/npittas/Flux/Gui/FluxKeyframeModel.cpp`
   - symbol: `collectKnobProperties()`, `buildLayerProperties()`, `buildEffectProperties()`, `buildMaskProperties()`, `keysForProperty()`
   - approximate lines: ~238-470
   - stable anchor: `QList<FluxKeyframeKey> keysForProperty(const FluxKeyframeProperty& property)`
   - why relevant: actual keyframe row data sources: layer gizmo nodes, effect nodes, mask roto aggregate keyframes.
   - evidence: collects animated dimensions from node knobs, groups multi-dim keys unless ungrouped, skips slave/hidden knobs, aggregates roto Bezier key times, computes partial grouped keys.

9. `file:///home/npittas/Flux/Gui/FluxKeyframeModelOps.cpp`
   - symbol: `nearestKeyTimeForProperty()`, `addKeyAtTime()`, `deleteKeysAtTime()`, `moveKeysAtTime()`
   - approximate lines: ~26-180, ~316-345
   - stable anchor: `moveKnobKeysAtTime(const FluxKeyframeProperty& property, double oldTime, double newTime)`
   - why relevant: stability risk context for keyframe editing; polish should avoid altering edit semantics unless explicitly scoped.
   - evidence: key edits batch knob changes, collision-check moves, and operate only on represented dims/roto shapes.

10. `file:///home/npittas/Flux/Gui/Resources/Stylesheets/flux-dark.qss`
   - symbol: Flux dark stylesheet
   - approximate lines: ~1-200 plus rest of file
   - stable anchor: `/* Flux Dark Theme — After Effects-inspired */`
   - why relevant: current global styling/readability reference. Native Dope Sheet GL painting does not appear QSS-driven, but scrollbars/widgets around it are.
   - evidence: stylesheet defines global colors, toolbars, scrollbars, etc.; DopeSheetView painting itself uses GL/settings colors, not QSS selectors.

11. `file:///home/npittas/Flux/tasks/T074-dopesheet-alias-keyframes-handoff.md`
   - symbol: T074 DopeSheet handoff notes
   - approximate lines: ~90-330
   - stable anchor: `### Native DopeSheet texture/key visibility fix already done`
   - why relevant: verified prior context and risks for T073. Confirms crash hardening and texture visibility were done; warns not to restart alias-curve debugging as polish.
   - evidence: notes list files touched, fixed RGBA texture upload/fallback diamond, and recommends runtime-path proof before further alias debugging.

## Allowed Edit Scope Recommendation
- Low-risk polish edits: `Gui/DopeSheetView.cpp` only for keyframe diamond contrast/size/outline, selected-key highlight, row background contrast, separators, project bounds/current-frame readability, and safer fallback drawing.
- If FluxTimeline property-row readability is explicitly included: small paint-only edits in `Gui/FluxTimeline.cpp` around the property-row block and possibly constants in `Gui/FluxTimeline.h` (`kPropertyRowHeight`, colors, diamond size). Avoid data/model changes.
- Avoid editing `Gui/FluxKeyframeModel*` or `Gui/DopeSheet.cpp` for readability unless validation proves a data/refresh stability bug, because those change key semantics and row contents.

## Read-Only Context Recommendation
- Use `Gui/DopeSheet.cpp`, `Gui/DopeSheetHierarchyView.cpp`, `Gui/DopeSheetEditor.cpp` to trace native Dope Sheet model/visibility/refresh.
- Use `Gui/FluxKeyframeModel.*` and `Gui/FluxTimeline.cpp` to compare FluxTimeline key visibility/data and avoid regressing T074.
- Use `tasks/T074-dopesheet-alias-keyframes-handoff.md` as history: prior DopeSheet crash/texture fixes are already accepted; do not re-debug alias invisibility unless T073 scope expands.

## Validation Targets
- tests: no dedicated automated test found for Dope Sheet UI; rely on build + GUI manual/screenshot validation.
- commands:
  - `cmake --build /home/npittas/Flux/build --target Natron -j$(nproc)`
  - launch per source of truth: `QT_PLUGIN_PATH=/usr/lib64/qt6/plugins QT_QPA_PLATFORM=xcb /home/npittas/Flux/build/App/Natron`
- manual checks:
  - Open Flux with Dope Sheet tab visible; create/import footage layer, add keys on Transform/Opacity/Text properties, and confirm native Dope Sheet rows/key diamonds remain visible at normal and zoomed views.
  - Select single and multiple keyframes; verify selected/unselected contrast, selected time label readability, and no flicker/disappear during selection/drag.
  - Expand/collapse Dope Sheet hierarchy rows; verify master keys in node/root rows and dim-row keys are distinguishable.
  - Compare FluxTimeline animated property rows: grouped full keys vs partial hollow keys, selected key color, inline-curve toggle, horizontal scroll/zoom.
  - Required proof for GUI polish: screenshots/recording showing Dope Sheet controls/rows/keyframes before/after, including selected keys and expanded hierarchy.

## Risks / Unknowns
- Codemap index is stale: generated at commit `580c3b266152d9c9b9d9193125cbbc57fb66c6e9`; current commit is `21babb906d30cadf7ceaf0657f52554e14244de1`; dirty/untracked `build-logs/`. I did not update because index exists and staleness appears due to commit drift plus build-log output; all cited locations were verified by direct source reads.
- Native DopeSheet uses OpenGL immediate-mode drawing and settings colors; QSS changes alone likely will not affect keyframe/row drawing.
- Dope Sheet alias/key visibility had prior failed debugging. T073 should stay paint/readability-focused unless Nick explicitly asks for deeper native DopeSheet data-path fixes.

## Stop Recommendation
Implementation can proceed now for paint-only Dope Sheet readability polish in `Gui/DopeSheetView.cpp`; include `Gui/FluxTimeline.cpp`/`.h` only if the task packet explicitly includes FluxTimeline property-row polish.
