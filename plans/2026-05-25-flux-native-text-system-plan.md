# Flux Native Text System Plan — Approval Draft

Date: 2026-05-25
Status: Approved direction; T075 boundary spike complete
Related grounding note: `plans/2026-05-25-flux-text-node-grounding.md`

## Executive Summary

Flux needs a first-class text system, not just a wrapped Natron/openfx-arena Text node. The current `FluxText.py` layer is good enough for Text v1, but it cannot support AE-style per-character/per-word/per-line text animation without exploding into many nodes.

The proposed direction is:

1. Keep the user-facing Flux text layer as one timeline layer.
2. Add a separate Flux-owned native text nodegroup with its own renderer; do not upgrade or mutate the existing `FluxText.py` layer.
3. Build the renderer around internal text layout, glyph clusters, words, lines, and per-element animator weights.
4. Add dedicated Character, Paragraph, and Text Animator panels that operate on the selected text layer from the timeline.
5. Reuse Natron/Flux `Knob` + `Curve` animation wherever practical so keyframes, Dope Sheet, and project serialization remain native.

The architectural rule: **one text layer equals one render node/gizmo, never one node per character/word/line.**

## Current Verified State

Current text path:

```text
FluxTimeline::addTextLayer()
  -> FluxLayer{type="text"}
  -> Gui05::rebuildCompositingGraph()
  -> createNode("net.sf.openfx.FluxText")      # PyPlug gizmo
  -> internal net.fxarena.openfx.Text node      # external Text.ofx
  -> FrameRange -> TimeOffset -> Grade -> Output
```

Important existing contracts:

- `plugins/FluxText.py` is currently the text layer gizmo ID: `net.sf.openfx.FluxText`.
- Text menu/context actions are currently gated on exact provider ID `net.fxarena.openfx.Text`.
- `Gui/Gui05.cpp` resolves promoted knobs such as `Text1center`, `Text1scale`, `Text1rotate`, `Text1name`, `Text1font`, `textOverlayCenter`, `frameRange`, `timeOffset`, and `opacity`.
- `Gui/FluxKeyframeModel.cpp` has text-specific filtering: text layer keyframe rows currently expose promoted `Text1*` knobs plus whitelisted public controls like `opacity`.
- Timeline serialization stores no text-specific fields; text state persists through Natron node/knob serialization.
- There is no in-tree Flux-native OFX build target today. External OFX bundles are currently deployed from `plugins/ofx-extras/`.

## Recommended Architecture

### Recommendation: new Flux text nodegroup, separate from current FluxText

Use a new Flux-native text layer nodegroup, tentatively:

```text
net.sf.openfx.FluxMotionText
```

The name is provisional, but the architectural point is fixed: this is a **new nodegroup**, not an upgrade to the current `net.sf.openfx.FluxText` PyPlug.

The new nodegroup owns or wraps a Flux native render provider, tentatively:

```text
net.flux.openfx.TextRender
```

This provider should live in a **Flux-owned OpenFX source path**, not in `openfx-misc` and not as a mutation of `plugins/FluxText.py`. The intended path is a new in-tree standalone OpenFX project, tentatively:

```text
openfx-flux/
  CMakeLists.txt
  TextRender/
    ...
```

`openfx-misc` and existing OFX bundles can be used as implementation/build references, but Flux text should have its own OFX build/deploy path so we fully control its source, dependencies, parameters, packaging, and future renderer modules.

The new layer path is separate:

```text
FluxMotionText nodegroup                 # new text layer type
  -> Flux TextRender node       # net.flux.openfx.TextRender
  -> FrameRange
  -> TimeOffset
  -> Grade / opacity
  -> Output
```

This gives us:

- A clean text system without dragging old `FluxText.py` constraints into the new renderer.
- Low-risk integration with the current trim/move/opacity/merge-chain model.
- A native render implementation we control.
- The old `FluxText.py` can remain as a reference/fallback/legacy layer, but it should not be involved in the new text implementation.
- Reusable UX expectations from old `FluxText.py` are allowed: usable font selection, visible editable text controls, frameRange/timeOffset/opacity workflow, overlay behavior, deployment paths, and familiar naming where it does not couple the new system to native Text. This is **usability parity only**, not implementation parity, native Text node parity, or `Text1...` knob parity.

### Compatibility rule

Current Text v1 must remain usable as a legacy/reference path while the new nodegroup is built:

- Add Text from menu/context menu.
- Render text in viewer.
- Trim/move text layer.
- Layer opacity.
- Viewer overlay transform/keyframing.
- Timeline key rows.
- Duplicate/split where currently supported.
- Save/reopen.

The new nodegroup must not temporarily replace working Text v1 with a prototype or hardcoded renderer. The menu/context UI can later make the new text layer the default, but the old PyPlug remains untouched unless Nick explicitly approves a cleanup.

### Public control contract for the new nodegroup

The new nodegroup should preserve the same functional contracts that Flux timeline code already understands:

- `frameRange`
- `timeOffset`
- `before`
- `after`
- `opacity`
- text/layer center
- scale
- rotation
- uniform scale
- skew controls if supported
- interactive overlay toggle if supported
- font family/style controls
- `textOverlayCenter`

Exact old `Text1*` names may be reused where they reduce C++ churn, but they are not mandatory for the new nodegroup. If the new names differ, these areas must be updated together:

- `Gui/Gui05.cpp` text layer init and overlay resolution.
- `Gui/FluxKeyframeModel.cpp` text filtering.
- Text panel binding code.
- Serialization/reference restoration if new Flux-layer fields are added.

New animator knobs must be discoverable in timeline keyframe rows. Either give them an accepted prefix or update `Gui/FluxKeyframeModel.cpp` to explicitly include the new text animator namespace.

### Why not one native Engine node immediately?

A direct Natron Engine built-in node may become attractive later, but it is a larger engine integration risk. A Flux-owned OFX renderer matches Natron's existing plugin model and can be built/deployed/tested separately. The existing app already expects layer sources to be plugin/gizmo nodes.

If the dedicated Flux OFX path cannot fully support the approved plan, the architecture must change before implementation proceeds. This is a hard stop, not a place to silently degrade scope. Blockers include inability to support dynamic/layered animator groups, keyframeable selector/target controls, project persistence, timeline row visibility, selected-layer panel binding, or required render performance. Possible fallback architectures are a Flux-native node/EffectInstance that can own dynamic knobs, or a Flux-side dynamic animator model with real keyframeable controls mirrored into the renderer. Fixed animator slots are not an approved fallback.

## Text Rendering Stack

Recommended render stack:

```text
Fontconfig -> HarfBuzz -> FreeType -> custom FP32 RGBA compositor
```

Roles:

- **Fontconfig**: resolve font family/style to actual font files and fallback fonts.
- **HarfBuzz**: shape Unicode text into glyph runs with glyph IDs, clusters, advances, kerning, ligatures, and script handling.
- **FreeType**: rasterize glyphs to alpha masks or outlines.
- **Flux compositor code**: composite glyph masks into Natron/OpenFX 32-bit float RGBA output, premultiplied alpha.

Qt remains the correct stack for UI font pickers and panel widgets, not the render engine.

## Data Model

### Text layer source data

Minimum v1 text source parameters:

- Text string.
- Font family.
- Font style/weight/stretch where available.
- Font size.
- Fill color.
- Stroke/outline color.
- Stroke/outline width.
- Tracking.
- Kerning enabled/disabled or kerning factor.
- Point text position / center.
- Paragraph alignment: left, center, right.
- Leading / line spacing.
- Box text dimensions can be deferred, but the renderer should not be designed in a way that blocks it.

### Internal layout model

The renderer computes this each render or from cache:

```text
TextLayout
  lines[]
    words[]
      clusters[]
        glyphs[]
```

Each addressable element needs:

- Element index.
- Source text range/cluster range.
- Glyph range.
- Baseline position.
- Advance.
- Bounds.
- Center/pivot.
- Word/line parent index.

The first implementation can be Latin-first, but it must use glyph clusters instead of byte/char loops so complex text support is not painted into a corner.

### Render/evaluation order

The renderer must keep layer-level transforms separate from per-element text animation:

```text
text source/style parameters
  -> shape/layout text
  -> evaluate text animator selector weights
  -> apply per-element animator deltas around element centers
  -> composite glyphs into transparent FP32 premultiplied RGBA
  -> layer/global text transform / overlay transform
  -> FrameRange / TimeOffset timing
  -> Grade opacity
  -> Merge chain
```

This avoids mixing the layer transform with per-character/word/line transforms.

## AE-Style Animator Model

Text animator groups are weighted delta applicators:

```text
final_element_value = base_value + animator_delta * selector_weight
```

Selector weight is evaluated per addressable element: character/glyph cluster, word, or line.

### Animator group v1

Each animator group has:

- Enabled.
- Name.
- Based On:
  - Characters.
  - Characters excluding spaces.
  - Words.
  - Lines.
- Range Selector:
  - Start.
  - End.
  - Offset.
  - Amount.
  - Units: Percentage initially; Index can follow.
  - Shape: Square, Ramp Up, Ramp Down initially.
- Target deltas:
  - Position.
  - Scale.
  - Rotation.
  - Opacity.
  - Tracking.
  - Fill color.
  - Stroke color.
  - Stroke width.

### Dynamic animator stack

Text animators must be dynamically addable/removable/layerable. Fixed animator slots are rejected because they would artificially limit real motion-graphics workflows.

Required behavior:

- Text layer context menu and/or Text Animator panel exposes **Add Text Animator**.
- The user chooses an animator/property type from a menu, e.g. Position, Scale, Rotation, Opacity, Tracking, Fill, Stroke.
- Flux creates a new animator group with a stable internal ID and its own selector/property controls.
- Multiple animator groups can stack on the same text layer.
- The user can add another animator later to move from B back to A, add a second reveal, add a color pass, etc.
- Re-click/remove deletes or disables that animator group according to a safe undo/serialization model.

Implementation requirement:

- Each dynamic animator group still needs real keyframeable controls for selector and target properties.
- If Natron supports dynamic user knobs on nodegroups after creation, use those for animator groups.
- If OFX/static parameter declaration blocks true dynamic knobs, T075/T079 must prove an alternative before implementation proceeds: either a Flux-native node/EffectInstance that can own dynamic knobs, or a Flux-side animator model with keyframeable dynamic knobs mirrored into the renderer.
- Do not fall back to fixed slots without Nick's explicit approval.

## Selector Semantics

Required v1 behavior:

- `Start = 0`, `End = 100`: first-to-last influence.
- `Start = 100`, `End = 0`: reversed last-to-first influence.
- `Offset = -100..100`: shifts the influence range across the element list.
- `Amount = 0..100`: scales selector influence.
- Range can be animated with standard keyframes.
- Selector shape controls weight across the selected span.

Initial shapes:

- Square.
- Ramp Up.
- Ramp Down.

Follow-up shapes:

- Triangle.
- Round.
- Smooth.
- Ease High / Ease Low.

Deferred selectors:

- Randomize Order / Seed.
- Wiggly selector.
- Expression selector.
- Selector blend modes: Add, Subtract, Intersect, Difference.

## UI Panels

Text should be editable from purpose-built panels bound to the selected timeline text layer. The generic properties panel remains available but is not the primary workflow.

### Selected-layer targeting rule

- If one text layer is selected: panels edit it live.
- If a non-text layer is selected: panels disable and show a short empty state.
- If nothing is selected: panels show a short empty state.
- If multiple new text layers are selected: panel edits apply to all selected new text layers.
- If the selected text layers have mixed values, the panel should show a mixed/blank state for that control; changing the control writes the new value to all selected text layers.

Panel controls write directly to the selected text layer nodegroup settings. No Apply button. They are shortcuts to the underlying node settings, not a separate state store.

### Character panel v1

Controls:

- Font family: searchable Qt font list.
- Font style/weight/stretch.
- Font size.
- Fill color.
- Stroke/outline color.
- Stroke/outline width.
- Tracking.
- Kerning toggle/factor if renderer supports it in v1.

Purpose: fast type styling without digging through raw layer properties.

### Paragraph panel v1

Controls:

- Left / center / right alignment.
- Leading / line spacing.
- Vertical align can be included only if box text exists; otherwise defer.
- Justify modes should be deferred unless the renderer has solid paragraph layout.

Purpose: common paragraph formatting for selected text layer.

### Text Animator panel v1

Controls:

- Add Animator.
- Add Animator from a typed list/context menu: Position, Scale, Rotation, Opacity, Tracking, Fill, Stroke, etc.
- Enable/disable animator.
- Rename animator.
- Remove animator.
- Add multiple animator groups layered on top of existing ones.
- Choose Based On.
- Enable target properties: Position, Scale, Rotation, Opacity, Tracking, Fill, Stroke.
- Edit target deltas.
- Edit Range Selector Start/End/Offset/Amount.
- Shape selector.

Preferred interaction:

- Text Animator panel shows a dynamic animator list for the selected text layer(s).
- Selecting an animator focuses its properties.
- Range selector gets a small mini-graph/bar later; spinboxes/sliders are enough for first validation.

## Timeline Integration

Text animator groups should appear as children of the text layer, not as external effects and not as generated character nodes.

Desired hierarchy:

```text
▼ Text "Title"
  ▶ Text Animator 1: Position
  ▶ Text Animator 2: Opacity
  ▼ Properties with Keys
    Text1center / Position
    Animator 1 > Selector Offset
```

Implementation notes:

- Reuse the existing visible-row/child-row model from effects, masks, and T074 keyframe rows.
- Text animator rows can initially be synthetic UI rows backed by real knobs on the text gizmo/render node.
- Animated selector/target knobs should appear in the keyframe model.
- Static text styling knobs do not need to clutter the timeline unless animated.
- Dynamic animator rows must preserve order, because stacking order affects the final result.

## Implementation Phases / Proposed Tasks

These task IDs are approved direction. T075 is now tracked in `tasks/TASKS.md`; later tasks should be added only as their scope becomes current.

### T075 — Text architecture spike and plugin boundary

Goal: prove the selected new-nodegroup/plugin boundary before large implementation.

Deliverables:

- Confirm whether `net.flux.openfx.TextRender` is built as a standalone in-tree OFX bundle or as a Natron built-in provider.
- Create/prove a dedicated Flux-owned OFX source path, tentatively `openfx-flux/`, using existing OFX projects only as references.
- Prove CMake builds the Flux OFX bundle without relying on prebuilt binary extras.
- Prove deploy/install/discovery path for the new Flux OFX bundle, matching the way other OFX plugins are discovered by Natron.
- Confirm the new text layer nodegroup identity, tentatively `net.sf.openfx.FluxMotionText`, separate from `net.sf.openfx.FluxText`.
- Add a tiny render prototype that outputs transparent image + one hardcoded glyph/string.
- Add CMake/build/deploy path for the prototype.
- Verify Flux can create the provider from the running app.
- Verify a new text layer can be represented in `FluxLayer` without touching old Text v1 behavior.
- Verify whether dynamic animator knobs can be added to a nodegroup after creation and remain keyframeable/serializable.
- Verify OFX bundle layout, install/deploy path, Natron discovery, describe/instance creation, and render callback execution.
- Verify generator basics: Region of Definition, project-format coordinates, render-window clipping, premultiplied alpha, FP32 output, pixel aspect handling, and thread-safety expectations.

Validation:

- Build passes.
- App launches.
- Node can be created manually or via harness.
- Node renders a valid premultiplied RGBA image in the viewer/export path.
- No existing Text v1 path regresses.
- Dynamic animator storage path is proven or implementation stops for architecture reassessment before animator work begins.

### T076 — Native renderer v1 usability baseline in isolation

Goal: make the new native renderer useful inside the new nodegroup without replacing the current `FluxText.py` path. Any comparison to Text v1 is a **usability baseline only**: the renderer must provide enough real controls to be usable, but it must not borrow native Text nodes, native Text knobs, or `FluxText.py` implementation internals.

Deliverables:

- Fontconfig family/style resolution.
- HarfBuzz shaping.
- FreeType glyph rasterization.
- Custom FP32 premultiplied compositing.
- Basic multiline layout with explicit newline handling.
- Fill color, font size, tracking, simple left/center/right alignment.
- New-nodegroup test wrapper or manual node path that does not break current Text v1.

Validation:

- Render white text, colored text, and multiline text.
- Test several system fonts.
- Compare alpha edges visually against current Text node.
- Save/reopen native-renderer parameter state where applicable.
- Current Text v1 still adds/renders exactly as before.

### T077 — New text layer integration

Goal: integrate the new text nodegroup as its own Flux text layer path while leaving old `FluxText.py` untouched.

Deliverables:

- Add a new nodegroup/plugin file for the new text layer, tentatively `plugins/FluxMotionText.py` or equivalent native wrapper.
- Add a new layer type or version marker so Flux can distinguish legacy text from new text.
- Preserve `frameRange`, `timeOffset`, `before`, `after`, `opacity`, and overlay contracts in the new nodegroup.
- Add or map public text controls for font/style/size/color/tracking/alignment.
- Update text creation UI to create the new text nodegroup when approved, while keeping legacy text available only if needed/reference:
  - `Gui/Gui.cpp` menu text action.
  - `Gui/FluxTimeline.cpp` context-menu text action.
- Do not edit old `plugins/FluxText.py` except for comments/reference docs unless Nick explicitly approves.

Validation:

- Add new Text layer from menu/context menu.
- Text renders in viewer.
- Trim/move/opacity still work.
- Viewer transform overlay still works and writes keyframes.
- Timeline key rows still show expected text controls.
- Save/reopen preserves text.
- Legacy Text v1 remains untouched and usable if invoked through any retained legacy/debug path.

### T078 — Character and Paragraph panels

Goal: make common text formatting fast from dedicated UI.

Deliverables:

- New Qt dock/panel: Character/Paragraph tabs or equivalent.
- Live binding to selected new text layer(s).
- Font family/style, size, fill, stroke, tracking.
- Alignment and leading controls where renderer supports them.
- Empty/disabled state for non-text selection.
- Multi-selected new text layers receive the same panel edits simultaneously.

Validation:

- Select one new text layer, change font/size/color/tracking without opening raw properties.
- Select multiple new text layers and apply the same font/color/tracking change to all.
- Select non-text layer, panel disables safely.
- Save/reopen preserves panel-driven changes.

### T079 — Dynamic text animator data model

Goal: expose AE-style dynamically addable animator groups on the new text layer/nodegroup.

Deliverables:

- Add/remove/reorder text animator groups from context menu and Text Animator panel.
- Each animator group has selector knobs and target delta knobs.
- Each animator group has a stable ID and user-visible name.
- Animator group controls are real keyframeable controls or backed by a proven keyframeable dynamic-knob model.
- Knobs/controls are visible to the Flux keyframe model when animated.
- Text animator evaluation order defined.
- `Gui/FluxKeyframeModel.cpp` text filtering updated if animator knobs do not use the existing `Text1` prefix.

Validation:

- Keyframe Start/End/Offset and see key rows.
- Add two Position animators in sequence and keyframe them independently.
- Reorder animators and verify stack order affects output predictably.
- Remove/disable an animator and verify it no longer affects render.
- Save/reopen animator groups and keyframes.

### T080 — Range selector evaluator

Goal: implement selector weights correctly.

Deliverables:

- Based On: Characters, Characters excluding spaces, Words, Lines.
- Start/End/Offset/Amount.
- Reversed ranges: Start > End.
- Shapes: Square, Ramp Up, Ramp Down.

Validation:

- Start 0 / End 100 affects first-to-last.
- Start 100 / End 0 affects last-to-first.
- Offset animation moves the influence wave.
- Characters/Words/Lines produce different expected grouping.

### T081 — Animator property application

Goal: apply weighted target deltas during render.

Deliverables:

- Position.
- Scale around element center.
- Rotation around element center.
- Opacity.
- Tracking influence.
- Fill color / stroke color / stroke width if the render pipeline supports stroke by this point.

Validation:

- AE-like type-on/off patterns.
- Per-word and per-line transforms pivot around word/line centers.
- Multiple animators stack predictably.
- Hundreds/thousands of characters remain one node/layer.

### T082 — Text Animator panel and timeline rows

Goal: provide a usable motion-graphics UI for animator groups.

Deliverables:

- Dedicated Text Animator panel.
- Add/remove/enable/rename/reorder dynamic animator groups.
- Property target controls.
- Range selector controls.
- Timeline child rows for text animator groups.
- Keyframe rows for animated selector/target knobs.

Validation:

- User can create a basic text reveal without touching raw properties.
- Offset keyframes appear under the text layer.
- Deleting/disabling animator updates render immediately.

### T083 — Performance, persistence, and compatibility hardening

Goal: make the new text system durable enough to become the default Flux text layer while keeping legacy Text v1 untouched unless explicitly retired later.

Deliverables:

- Glyph cache keyed by font file, glyph ID, size, outline/stroke parameters.
- Layout cache invalidated only by text/font/layout changes, not by selector animation every frame.
- Compatibility path for old `FluxText.py` projects if needed.
- De-emphasize `Text.ofx.bundle` for new text work after the new nodegroup is validated; do not remove legacy support without approval.

Validation:

- Stress test long text: 1k, 5k, 10k characters.
- Scrub Offset animation and measure responsiveness.
- Save/reopen old and new text layers.
- Render sequence through the normal Flux export path.

## User-Visible Behavior After Completion

After the approved scope is implemented, users should be able to:

1. Add one new Flux text layer from the timeline/menu.
2. Select one or more new text layers and immediately edit font, size, fill, stroke, tracking, alignment, and leading from dedicated panels.
3. Add a Text Animator from a purpose-built panel.
4. Animate characters, words, or lines with Start/End/Offset without creating multiple nodes.
5. See animator/keyframe rows under the text layer in the timeline.
6. Save/reopen projects with text styling and text animator keyframes intact.

## Validation Plan

Every task must follow the project lifecycle: implement, review, test with real artifacts, review results, debug if needed, then update task/phase status.

Minimum validation matrix:

| Area | Test |
|---|---|
| Build | `cmake --build "$BUILD_DIR" --target Natron -- -j$(nproc)` plus any new OFX target |
| Launch | `QT_QPA_PLATFORM=xcb "$BUILD_DIR/App/Natron"` with `QT_PLUGIN_PATH` set only if the distro needs an explicit Qt plugin path |
| Text render | Add new text layer, render in viewer, change font/size/color |
| Timeline | Trim/move new text layer, ensure current model still holds |
| Keyframes | Keyframe selector Offset and target property, verify rows/Dope Sheet |
| Save/reopen | Save `.ntp`, reopen, verify text and animator state |
| Export | Render a short sequence with animated text |
| Performance | Measure 1k/5k/10k character layout and Offset scrubbing |
| Compatibility | Open a project with old Text v1 layer if available |

## Risks and Mitigations

| Risk | Impact | Mitigation |
|---|---|---|
| OFX dynamic parameter limits make arbitrary animator groups awkward | High | T075/T079 must prove dynamic keyframeable animator storage; if OFX cannot support it, switch to a Flux-native node/EffectInstance or Flux-side dynamic knob model. Do not use fixed slots without approval |
| Font fallback / shaping complexity | Medium | Use Fontconfig + HarfBuzz from day one, even if v1 UI is Latin-first |
| Glyph rasterization performance | Medium | Add glyph/layout caches before declaring replacement done |
| New public knob names break overlays/keyframes | High | Update `Gui05.cpp`, text panel binding, and `FluxKeyframeModel.cpp` together; reuse old naming patterns only where useful |
| Accidentally mutating legacy FluxText regresses working Text v1 | High | Keep `plugins/FluxText.py` untouched; implement a new nodegroup path |
| Text creation menu remains gated on old `net.fxarena.openfx.Text` | Medium | Update menu/context gates to create the new nodegroup while preserving any retained legacy/debug path |
| Native Text alignment issue distracts from new renderer | Low | Treat T072 as legacy Text issue; new renderer owns its own alignment implementation |
| Build/deploy for new Flux OFX plugin adds installer complexity | Medium | First task creates/proves dedicated `openfx-flux/` CMake/deploy path; update Linux setup only after prototype works |
| Dedicated OFX path cannot support full approved text plan | High | Stop and switch architecture with Nick's approval; do not ship a reduced/fixed-slot implementation |
| Stroke rendering quality | Medium | Ship fill-first if needed; implement stroke via FreeType outline/path once layout is proven |
| Large font menus are slow | Low/Medium | Use Qt searchable font picker panel, not huge OFX choice lists |
| Old projects using current FluxText need migration | Low/Medium | No migration needed for initial work because legacy FluxText remains separate; add migration only if/when Nick asks to retire it |

## Approved Direction

Nick approved these choices, with corrections incorporated:

1. Use a Flux-owned native text renderer instead of extending openfx-arena Text.
2. Create a dedicated Flux-owned OFX source/build path, tentatively `openfx-flux/`, similar in spirit to other OFX plugins but fully controlled by Flux.
3. Build a **separate new text nodegroup**, not an upgrade of the current `FluxText.py`.
4. Keep old `FluxText.py` only as legacy/reference; do not involve it in the new implementation except for reusable patterns.
5. Preserve the functional text-layer contracts Flux needs: trim/move/opacity, overlay/keyframes, panel binding, timeline visibility, and save/reopen.
6. Build dedicated Character, Paragraph, and Text Animator panels as first-class UI shortcuts to the selected new text nodegroup settings.
7. Support multi-selected text layers in the panels: changing a font/style/control applies to all selected new text layers.
8. Use a dynamic/layered animator stack. Add/remove/reorder animator groups from panel/context menus. Fixed slots are not approved.

Implementation may begin by turning this plan into tasks, but if T075 discovers that the dedicated Flux OFX path cannot fully implement the approved plan, stop and ask Nick before changing architecture.
