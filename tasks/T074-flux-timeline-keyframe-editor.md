# T074 — FluxTimeline animated property rows + grouped keyframes v1

## Status

`DONE`

Implementation complete and validated. Scrutinize fixes applied (undo batching via beginChanges/endChanges, paint-time polling removed, expanded state persisted, disableNode filtering scoped to adjustment rows, native knob/Bezier/RotoContext signal invalidation). Build passed; autonomous DopeSheet proof passed; Nick manually validated the final behavior.

## Goal

Add an After Effects-like keyframe view directly inside `FluxTimeline`, mixing a simple dopesheet-style keyframe view with inline curve previews for animated layer, effect, and mask properties.

The UI must be backed by Natron's native `Knob`/`Curve`/Roto animation data. Flux must not create a duplicate animation system.

## Approved Direction

- Show keyframes in the Flux layer timeline hierarchy, per layer/effect/mask.
- Show **only animated properties** for v1.
- Provide a timeline-wide toggle between:
  - simple keyframe/dopesheet mode, and
  - simple inline curve-preview mode.
- Use simple inline curve previews for v1; full CurveEditor/tangent editing can come later.
- Keep multidimensional knobs grouped only when grouping reflects actual animation state.
- Hide adjustment-row system disable keyframes.
- For text layers, expose only promoted `Text1...` knobs, not duplicated internal Text node knobs.
- For masks, include top-level mask/shape animation, not per-vertex animation.

## Existing Code Evidence

- FluxTimeline currently has layer/effect/mask rows only: `Gui/FluxTimeline.h:46`.
- Visible rows are rebuilt from layers, masks, effects, and effect masks: `Gui/FluxTimeline.cpp:1818`.
- Keyframes are native `Curve`/`KeyFrame` data: `Engine/Curve.h:50`.
- Knobs expose native keyframe/curve APIs: `Engine/Knob.h:708`, `Engine/Knob.h:771`.
- Native knob serialization saves animation curves: `Engine/KnobSerialization.h:297`.
- Natron DopeSheet moves keys through `knob->moveValueAtTime(...)`: `Gui/DopeSheetEditorUndoRedo.cpp:247`.
- Natron already exposes all-dimension key actions for multidim knobs: `Gui/KnobGui.cpp:445`.
- Roto nodes expose `RotoContext`: `Engine/Node.h:314`.
- `RotoContext::getBeziersKeyframeTimes()` gathers Bezier shape keyframes: `Engine/RotoContext.h:347`, `Engine/RotoContext.cpp:2047`.
- Bezier shape-level key APIs exist: `Engine/Bezier.h:335`, `Engine/Bezier.h:631`.
- Per-vertex curves exist separately and are out of scope for v1: `Engine/BezierCP.h:79`.
- Text layer promoted knobs are the approved public text animation surface: `Gui/Gui05.cpp:1708`, `plugins/FluxText.py:154`.

## Scope

### 1. Animated property row model

Add a keyframe/property row model under existing timeline layer/effect/mask rows.

Each keyframe property row should map to native animation ownership:

- owner kind: layer, effect, mask, or roto shape
- backing `NodePtr` where applicable
- backing `KnobIPtr` and dimension set where applicable
- backing `CurvePtr`/curves where applicable
- display label
- editable/read-only/system flags

Do not store keyframe values in Flux data structures. Flux may store UI state only.

Implementation should live in helper files such as:

- `Gui/FluxKeyframeModel.{h,cpp}`
- `Gui/FluxKeyframePainting.{h,cpp}` or equivalent

Do not keep growing `FluxTimeline.cpp` with all keyframe logic.

### 2. Dopesheet mode

In keyframe/dopesheet mode:

- draw keyframe diamonds from native curves/keyframe times.
- allow selecting keyframes.
- allow adding a key at the playhead.
- allow deleting selected keyframes.
- allow dragging selected keyframes horizontally.
- use native keyframe APIs (`onKeyFrameSet`, `setKeyFrame`, `moveValueAtTime`, Bezier shape key APIs) so normal Natron undo/evaluation/serialization behavior remains intact.

### 3. Inline curve-preview mode

In inline curve-preview mode:

- draw simple sampled previews from native `Curve::getValueAt()` data for knob curves.
- draw the same key handles/ticks as dopesheet mode.
- support the same select/add/delete/drag operations as dopesheet mode where practical.
- do **not** implement full tangent editing in T074.

### 4. Multidimensional knob grouping rules

Multidimensional knobs must avoid creating synthetic animation.

Rules:

- If **only one dimension** of a multidimensional knob is animated, show/treat it as a separate dimension row.
- Do not create keyframes in other dimensions just because the knob is multidimensional.
- If two or more dimensions are animated, show a grouped property row by default.
- Ungrouped mode shows one row per animated dimension.
- Grouping/ungrouping is Flux UI state only; native keyframes remain in their original per-dimension curves.
- Grouping state should persist by stable node script name + knob name, not by copied keyframe data.

Grouped-row key behavior:

- A full diamond means all animated dimensions represented by the grouped row have a key at that frame.
- A partial/hollow diamond means only some represented dimensions have a key at that frame.
- Adding a key on a grouped row adds keys only to the dimensions represented by that grouped row, using current per-dimension values.
- Deleting a grouped key removes existing keys for represented dimensions at that frame.
- Dragging a grouped key moves only existing dimension keys at that frame.
- Dragging or selecting a partial grouped key must not create missing dimension keys.
- If completing missing dimensions is desired later, it must be an explicit action, not implicit drag behavior.

### 5. Layer/effect/text knob filtering

Layer/effect rows should expose animated user-facing knobs only.

Filtering requirements:

- Hide adjustment-row trim/range keyframes that Flux uses on effect `disable` knobs.
- Hide internal/system/secret/implementation knobs.
- For `FluxText`, expose only promoted `Text1...` knobs on the FluxText gizmo.
- Do not also expose internal native Text node knobs for the same text controls.
- Avoid duplicate property rows for aliases/master-slave links.

### 6. Mask and Roto shape animation

Mask rows must show top-level Roto/RotoPaint animation, without per-vertex detail.

Required v1 behavior:

- Show aggregate mask keyframe ticks from `RotoContext::getBeziersKeyframeTimes()`.
- Treat Bezier shape keyframes as shape-level keys, not individual control-point keys.
- Do not expose `BezierCP` per-vertex curves/keyframes in T074.

Per-shape rows are desirable but must be proven before full UI commitment:

- enumerate shape items in stable render/order or layer/name order;
- prove stable identity for save/reopen and duplicate/split cases;
- prove safe add/move/delete through `Bezier::setKeyframe`, `removeKeyframe`, and `moveKeyframe`;
- document the code path before implementing persistent per-shape row UI.

If stable per-shape identity is not proven inside T074, keep v1 to aggregate mask ticks and record the blocker.

### 7. Persistence

Native keyframes must remain serialized by Natron's existing project serialization.

Flux may persist only UI state:

- keyframe vs inline-curve mode;
- expanded/collapsed keyframe property rows;
- multidim grouped/ungrouped override;
- optional animated-only/filter UI flags.

Do not serialize duplicate keyframe values in `FluxTimelineSerialization`.

## Acceptance Criteria

- Animated layer properties appear under expanded layer rows after keyframes exist.
- Animated effect properties appear under their effect rows after keyframes exist.
- Animated mask properties and aggregate Roto shape key times appear under mask rows.
- Only animated properties are shown in v1.
- Keyframe mode draws editable keyframe diamonds.
- Inline curve-preview mode draws sampled curve previews and key ticks.
- User can add, select, drag, and delete supported native keyframes from FluxTimeline.
- A multidim knob with only one animated dimension is shown/edited as that separate dimension only.
- A multidim knob with two or more animated dimensions is grouped by default.
- Grouped multidim drag/delete moves/deletes only existing keys; it never creates missing dimension keys.
- Ungrouping shows separate animated dimension rows and persists as Flux UI state.
- Save/reopen preserves native keyframes and restores Flux keyframe UI state.
- Adjustment-row `disable` range keyframes are hidden and cannot be edited through this UI.
- Text layers show promoted `Text1...` keyframes only.
- Per-vertex Roto/Bezier control point animation is not shown in v1.
- Implementation is reviewed by oracle and scrutinize before completion.

## Validation Plan

1. Build Flux/Natron.
2. Text layer validation:
   - animate promoted `Text1center`, `Text1scale`, `Text1size`, text color/opacity where available;
   - verify only promoted `Text1...` rows appear;
   - save/reopen and confirm keyframes and UI state remain.
3. Footage/solid layer validation:
   - animate transform/opacity;
   - move/delete keys in FluxTimeline;
   - verify viewer/render behavior changes accordingly.
4. Effect validation:
   - add Transform/Grade/Blur or equivalent OFX effects;
   - animate effect knobs;
   - verify property panel, existing DopeSheet, and FluxTimeline remain in sync.
5. Multidim validation:
   - animate only one dimension of a multidim knob and verify it remains a separate dimension row;
   - animate two or more dimensions and verify default grouping;
   - drag partial grouped keys and confirm missing dimension keys are not created.
6. Adjustment row validation:
   - trim/move an adjustment row;
   - verify internal `disable` range keys still work but are hidden in the keyframe UI.
7. Mask validation:
   - add a Roto/RotoPaint mask;
   - create/move/delete shape-level keys;
   - verify aggregate key ticks appear;
   - verify no per-vertex rows appear.
8. Compare the same project against Natron's existing DopeSheet/CurveEditor for keyframe agreement.

## Risks / Guardrails

- Do not duplicate animation state.
- Do not create missing dimension keys implicitly.
- Do not expose adjustment-row system keys.
- Do not expose internal Text node duplicates.
- Do not implement per-vertex Roto key editing in v1.
- Do not embed the full Natron CurveWidget/tangent editor in v1 unless Nick explicitly approves a separate plan.
- Stop and ask Nick if per-shape Roto identity cannot be made stable without new model policy.
