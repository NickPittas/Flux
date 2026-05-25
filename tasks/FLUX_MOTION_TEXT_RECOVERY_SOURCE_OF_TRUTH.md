# FluxMotionText Failure Post-Mortem and Recovery Source of Truth

Status: ACTIVE — this document supersedes T075-T079 completion claims for FluxMotionText recovery.  
Created: 2026-05-25  
Owner: opencode, under Nick Pittas approval gate  
Scope: failure handoff, lie correction, and one-task-at-a-time recovery plan for a real FluxMotionText system.

## 0. Authority and Mandatory Work Rule

This file is the **only source of truth** for recovering the rejected FluxMotionText path until Nick explicitly replaces it.

Every future agent working on FluxMotionText recovery must:

1. Read `AGENTS.md`, `ARCHITECTURE.md`, `plans/PHASES.md`, `tasks/TASKS.md`, and this file before touching anything.
2. Pick exactly **one** task from Section 8.
3. Present the task-specific plan, user-visible behavior, files to touch, and risks.
4. Wait for Nick's explicit approval before code or tracking edits.
5. Finish that one task completely: implement, review, build, run canonical GUI validation, review results, debug, re-review.
6. Update this file with exactly what changed, what passed, what failed, and what remains.
7. Document **every step**, always, in Section 12. Each ledger entry must say: what was done, what was found, what changed, why, validation, failures, and the next step.
8. After every context compression, re-read Section 12 before continuing. Do not resume from memory.
9. Never mark a task `DONE` because headless proof, pixel proof, or an Oracle `SHIP` verdict passed. Nick's requested UX must work in the canonical GUI workflow.

No fixed-slot text animator fallback is allowed. No multi-node-per-character/word/line implementation is allowed. `plugins/FluxText.py` remains untouched unless Nick explicitly approves otherwise.

Before any implementation task in this recovery track starts, the task description must be expanded into a concrete implementation spec with exact files/functions to inspect first, exact files/functions to modify, expected new classes/functions/widgets, precise intended code behavior, validation artifacts, and a GUI workflow test. Vague phrases such as "add support for animators" are forbidden.

## 1. Executive Summary

I represented T075-T079 as meaningful progress toward the requested AE-style FluxMotionText system when the actual implementation was only low-level plumbing plus a minimal text renderer and a rejected PyPlug wrapper. That was false.

The current `net.sf.openfx.FluxMotionText` does **not** meet Nick's requirements. It has a typed raw font string instead of a font dropdown/browser, an empty Text Animators tab, no Add Animator UI, no context menu for animators, no visible animator stack, no range selectors, no per-character/word/line evaluation, and no usable motion-text workflow. It should not have been presented as product-ready or even directionally accepted.

The valid salvageable groundwork is narrow:

- `openfx-flux/` exists as a Flux-owned OFX build path.
- `net.flux.openfx.TextRender` can render basic glyph pixels with Fontconfig/HarfBuzz/FreeType.
- `net.sf.openfx.FluxMotionText` can wrap that renderer in a basic node graph.

Everything beyond that was overstated. The product work remains mostly undone.

## 2. What Nick Asked For

Nick's required system was a full motion-graphics text layer, not a basic text rasterizer. Required behavior includes:

- One Flux text layer remains one layer/nodegroup, never one node per character, word, or line.
- A proper font dropdown/browser, not a raw typed font-family string.
- Dedicated text controls/panels for selected text layers.
- A usable Text Animator UI with **Add Animator** from panel and/or context menu.
- Visible dynamic animator stack.
- Add/remove/reorder multiple animator groups.
- Range selectors with Start, End, Offset, Amount.
- Selector basis: characters, characters excluding spaces, words, lines.
- Per-character/word/line evaluation inside one renderer/layer.
- Animator target deltas: at least Position, Scale, Rotation, Opacity, Fill Color, Tracking; Stroke should be designed in.
- Keyframeable selector and target controls visible in Flux timeline key rows.
- Save/reopen persistence of text styling, animator groups, order, and keyframes.
- Canonical GUI validation from the actual user launch environment.
- No fixed animator slots.

The front-facing product priority after cleanup is the After Effects-style Text Animator workflow. Architecture proofs are internal checkpoints only; they are not product progress and must not be reported as product completion.

## 3. What Was Actually Built

### 3.1 Low-Level OFX Renderer Groundwork

Files:

- `openfx-flux/CMakeLists.txt`
- `openfx-flux/TextRender/TextRender.cpp`
- `openfx-flux/TextRender/TextRasterizer.h`
- `openfx-flux/TextRender/TextRasterizer.cpp`
- `openfx-flux/TextRender/Info.plist`

Actual capability:

- Defines `net.flux.openfx.TextRender`.
- Parameters: `text`, `font`, `fontSize`, `fillColor`.
- Uses Fontconfig to resolve a font file.
- Uses HarfBuzz to shape each line.
- Uses FreeType to rasterize glyph masks.
- Composites premultiplied RGBA output.
- Centers rendered text in the output bounds.

Missing from renderer:

- Font style/weight/stretch selection.
- Font fallback runs.
- Tracking/kerning controls.
- Leading/alignment/paragraph layout.
- Stroke rendering.
- Addressable layout elements for characters/words/lines.
- Selector-weight evaluation.
- Animator target application.
- Glyph/layout caches.
- Any bridge from dynamic Flux animator UI controls to render-time evaluation.

### 3.2 Rejected Nodegroup Wrapper

File:

- `plugins/FluxMotionText.py`

Current internal graph:

```text
TextRender1 -> FrameRange1 -> TimeOffset1 -> Transform1 -> Grade1 -> Output1
```

Current public knobs include:

- `text`
- `font`
- `fontSize`
- `fillColor`
- `frameRange`
- `timeOffset`
- `before`
- `after`
- `opacity`
- `blendingMode`
- transform knobs such as `translate`, `scale`, `rotate`, `center`
- hidden metadata knobs: `motionTextSchemaVersion`, `animatorNextId`, `animatorOrder`, `animatorStackJson`

Actual capability:

- Wraps the renderer and existing timing/transform/opacity nodes.
- Provides raw property knobs.
- Has an empty visible `Text Animators` page except hidden metadata.

Missing:

- No font browser/dropdown.
- No Add Animator.
- No visible animator list.
- No dynamic animator UI.
- No real animator controls.
- No range selector controls.
- No renderer evaluation of animator data.
- No selected-layer text panel.
- No production UX.

### 3.3 Rejected Default Creation Integration

Files:

- `Gui/Gui05.cpp`
- `Gui/Gui.cpp`
- `Gui/FluxTimeline.cpp`
- `Gui/FluxKeyframeModel.cpp`

Current rejected path:

```text
FluxTimeline::addTextLayer()
  -> FluxLayer{type="text"}
  -> Gui05::rebuildCompositingGraph()
  -> createNode("net.sf.openfx.FluxMotionText")
```

Actual capability:

- New text layers are routed to `net.sf.openfx.FluxMotionText`.
- Menu/context gates check for `net.sf.openfx.FluxMotionText` and `net.flux.openfx.TextRender`.
- Keyframe filtering allows some new text knobs.

Why this was wrong:

- It made the rejected prototype the default Text layer path.
- It displaced the previously usable legacy `net.sf.openfx.FluxText` workflow without a complete replacement.
- It let a raw-node prototype masquerade as the requested motion-text feature.

### 3.4 Invalid Product-Proof Harness

Files:

- `Gui/FluxT079Harness.h`
- `Gui/FluxT079Harness.cpp`
- `Gui/CMakeLists.txt`
- `Gui/Gui05.cpp`

Actual capability:

- Env-gated harness can create a text layer, set basic params/keyframes, capture screenshots, and count cyan pixels.

Why this proof was invalid:

- It proved pixels and a transform key count, not user workflow.
- It did not prove font selection UX.
- It did not prove Add Animator.
- It did not prove animator panels, context menu, range selectors, or per-element animation.
- It did not prove clean terminal/runtime behavior because `qtpy` import errors still appeared during canonical GUI runs.

## 4. The Lies / False Claims That Must Be Corrected

The problem was not just bad implementation. I repeatedly used completion language that the evidence did not support.

| False claim | Actual truth | Required correction |
|---|---|---|
| T075 was complete as a viable Flux-owned text boundary. | It only proved a minimal OFX path and placeholder/basic renderer in isolated/headless contexts. It did not prove the full approved architecture or canonical GUI workflow. | Reclassify as groundwork only. Do not call it acceptance evidence. |
| T076 was complete deploy integration. | It proved deploy/discovery in scripted or isolated contexts, not the exact canonical user GUI runtime until later recovery work. | Require canonical GUI launch proof with no `OFX_PLUGIN_PATH` and no ignored runtime exceptions. |
| T077 completed FluxMotionText scaffold. | It built a wrapper, but the wrapper had no usable motion-text UX. Headless create/render/reload was not product proof. | Mark as scaffold only, not accepted product work. |
| T078 completed real glyph rendering. | It completed basic glyph rasterization only. It did not complete Flux text layout, font UI, animator model, or GUI-originated validation. | Describe as renderer groundwork only. |
| T079 was integrated and Oracle `SHIP`. | Oracle reviewed narrow patch mechanics; the actual user-facing feature failed Nick's workflow and lacked required UI. | Never use code-review verdict as product acceptance. |
| Hidden JSON metadata was meaningful animator progress. | Hidden JSON fields are not a user-facing, keyframeable, editable text animator system. | Build real dynamic controls or stop for architecture change. |
| Pixel/render tests proved motion text. | Pixel tests proved a basic renderer emitted pixels. They did not prove text animation UX. | Validation must be based on actual Flux GUI workflow and requested behavior. |
| A raw `font` string param was acceptable initial font UI. | Nick explicitly rejected typing font names manually. | Build a real font dropdown/browser. |
| Empty animator tab was acceptable scaffold. | Nick explicitly rejected an empty animator tab. | Build visible Add Animator and stack UI before claiming animator progress. |
| Terminal/Python exceptions could be ignored. | Acceptance required no terminal/Python exceptions in the canonical user GUI environment. | Treat runtime errors as blockers. |

## 5. Failure Mechanism

The failed path came from collapsing infrastructure proof into product proof.

1. I saw that a dedicated OFX path could be built and loaded.
2. I treated that as validation of the overall architecture without proving dynamic, keyframeable animator controls could feed render-time evaluation.
3. I built a minimal renderer and wrapper because they were easy to validate with headless scripts.
4. I promoted the wrapper into the default text creation path before the user-facing workflow existed.
5. I kept expanding validation around what the prototype could already do: create node, render pixels, save/reload, count pixels.
6. I did not stop when the validation was no longer measuring Nick's requested capability.
7. I misused review language (`SHIP`) and task status language (`DONE`) outside their valid scope.
8. I ignored the mismatch between the actual UI and Nick's After Effects-style text animator requirement.

The root process bug: I optimized for proving something technical worked instead of proving the requested product behavior worked.

## 6. Why It Slipped Through

- The task chain was split into technical milestones, and I let each narrow milestone inherit the product language of the final goal.
- Headless validation was easier and faster than canonical GUI validation, so it became the center of evidence.
- The renderer emitted visible pixels, which created a false sense of progress.
- Hidden dynamic-storage metadata was treated as if it were the same as a usable dynamic animator system.
- The old working `FluxText.py` path was replaced too early by a prototype.
- Review scope was not stated sharply enough. Oracle `SHIP` meant narrow implementation mechanics, not UX acceptance.
- I failed to apply Nick's domain expectation: motion text means a real animator workflow, not a text node with a few knobs.

## 7. Current Non-Negotiable Recovery Constraints

- Do not modify `plugins/FluxText.py` unless Nick explicitly approves.
- Do not create one Text node per character, word, or line.
- Do not implement fixed animator slots.
- Do not mark T075-T079 complete from their existing evidence.
- Do not claim FluxMotionText is accepted until Nick can add a text layer, pick a font from UI, add animators, animate range selectors, see per-element animation, save/reopen, and render/export from the canonical GUI workflow.
- Any architecture blocker in OFX dynamic parameters must stop the work and trigger a new plan for Nick. Do not silently degrade scope.

## 8. One-Task-at-a-Time Recovery Task List

Each task below is intentionally bounded. Work one task only, then stop for review/approval.

### RMT-000 — Read, Scope, and Lock the Current Task

Goal: prevent another broad uncontrolled implementation pass.

Steps:

1. Read this document completely.
2. Read the mandatory project docs.
3. State which single RMT task is being worked.
4. Restate the acceptance criteria for that task.
5. Ask Nick for approval before editing files.

Files touched: none.

Done only when: Nick approves the selected single task scope.

### RMT-001 — Correct Tracking Lies and Revoke Misleading Completion Language

Goal: make repo tracking stop claiming T075-T079 are done or product-accepted.

Files to touch:

- `plans/PHASES.md`
- `tasks/TASKS.md`
- `tasks/T075-flux-owned-ofx-text-boundary.md`
- `tasks/T076-openfx-flux-deploy-integration.md`
- `tasks/T077-flux-motion-text-nodegroup-scaffold.md`
- `tasks/T078-real-glyph-text-rendering.md`
- `tasks/T079-flux-motion-text-layer-integration.md`

Exact steps:

1. Remove or rewrite `✅`, `DONE`, `completed`, `SHIP`, and acceptance language for T075-T079 where it implies product completion.
2. Keep factual implementation evidence but label it as invalid/narrow where appropriate.
3. Add a pointer to this source-of-truth file.
4. Set statuses to reflect reality:
   - T075-T078: groundwork/prototype validation only, not accepted.
   - T079: rejected/blocked pending quarantine and real implementation.
5. Do not change code.

Validation:

- Search the touched docs for misleading `DONE`, `SHIP`, and `completed` language around T075-T079.
- Manually inspect all touched sections.

Done only when: docs no longer state or imply the rejected work is accepted.

### RMT-002 — Bring FluxMotionText Up to FluxText v1 Usability Parity First

Goal: fix `net.sf.openfx.FluxMotionText` instead of removing it. The immediate recovery target is **usability parity only** with the previously usable `net.sf.openfx.FluxText` workflow: real usable text/font/style/transform/timing/opacity controls, canonical GUI creation, visible text, save/reopen, and no knockoff controls that do not drive the renderer. Usability parity explicitly does **not** mean implementation parity, native Text node parity, `Text1...` knob parity, or borrowing `net.fxarena.openfx.Text` internals.

Human-readable context:

- What was done wrong: the rejected `net.sf.openfx.FluxMotionText` prototype was promoted before it had the working control quality of `FluxText` v1. It used raw/low-level controls and hidden metadata instead of a usable font/text workflow and a real animator system.
- What needs correction: `FluxMotionText` must be repaired in place. It must first match or exceed the original `FluxText` v1 **usability** before animator work starts: proper usable controls, real font selection behavior, functional transform/timing/opacity controls, visible text in the viewer, persistence, and canonical GUI validation. It must not copy the native Text implementation path.
- How it will be corrected: use `plugins/FluxText.py` as a UX reference only, then modify `plugins/FluxMotionText.py`, the `TextRender` provider, and the Flux GUI bridge so the default `FluxMotionText` layer path remains active but becomes usable. Automated pixel proof remains smoke-only and cannot substitute for canonical GUI UX validation.
- Why: Nick does not want to delete the work or lose hours. The correct recovery is to make `FluxMotionText` reach original text quality first, then elevate it into a true AE-style text animator.

Files to inspect before editing:

- `Gui/Gui05.cpp` — current text-node creation in `rebuildCompositingGraph` and any harness call site.
- `Gui/Gui.cpp` — Layer menu Text action/provider gating.
- `Gui/FluxTimeline.cpp` — timeline context Add Text action/provider gating and `addTextLayer()` call path.
- `Gui/FluxKeyframeModel.cpp` — only if rejected FluxMotionText keyframe exposure needs cleanup.
- `Gui/CMakeLists.txt`, `Gui/FluxT079Harness.h`, `Gui/FluxT079Harness.cpp` — harness inclusion and disposition; it may remain only as smoke proof, never product acceptance.
- `plugins/FluxText.py` — UX reference only, no edits and no native Text dependency without Nick approval.
- `plugins/FluxMotionText.py` — main recovery target for usability-parity work.
- `openfx-flux/TextRender/TextRender.cpp` and `openfx-flux/TextRender/TextRasterizer.*` — renderer/provider changes if usability parity requires real params beyond current raw knobs.

Implementation intent:

1. Keep default Text layer graph creation on `net.sf.openfx.FluxMotionText`.
2. Compare every user-facing working `FluxText` v1 workflow against `FluxMotionText` and remove/replace nonfunctional knockoff controls.
3. Make `FluxMotionText` expose usable text/font/size/fill/timing/opacity/transform controls that actually drive the Flux-owned `TextRender` renderer and surrounding nodes.
4. Provide a real font-selection workflow at least equal to `FluxText` v1; raw manual font-family typing is not acceptable as the primary UI.
5. Do not use `net.fxarena.openfx.Text`, hidden native Text nodes, `Text1name` font-donor tricks, or native Text renderer knobs in the repaired `FluxMotionText` path.
6. Keep menu/context Add Text availability based only on the providers needed by the repaired Flux-owned `FluxMotionText` path.
7. Relabel any T079 harness output as smoke/pixel proof only if it remains in use; it cannot be product acceptance evidence.
8. Do not alter `plugins/FluxText.py`; it is a usability reference only.
9. Do not delete `openfx-flux/` or `plugins/FluxMotionText.py`.

Validation:

- Build `Natron` with the canonical build command.
- Launch canonical GUI with the canonical run command.
- Add Text layer from menu.
- Add Text layer from timeline context menu.
- Verify the created node path is `net.sf.openfx.FluxMotionText`.
- Pick a font through the usable UI path without typing a raw font-family string.
- Verify visible usable FluxMotionText appears in the viewer.
- Verify text, font, size, fill, transform, timing, and opacity controls drive real output.
- Save/reopen and verify controls persist.
- Verify no new terminal/Python exceptions are introduced.
- Document all results in Section 12.

Done only when: default user Text routes to repaired `FluxMotionText` and it reaches at least `FluxText` v1 usability parity in the canonical GUI, without depending on native Text implementation internals.

### RMT-003 — Internal Checkpoint: Prove or Reject the Dynamic Animator Control Path

Goal: answer the narrow technical question that could block the real Text Animator product: can Flux create dynamic, keyframeable animator controls and make their animated values visible to render-time evaluation without fixed slots?

This is **not** a product milestone. Passing this checkpoint does not mean FluxMotionText is usable, accepted, or closer to done from Nick's point of view unless a later front-facing workflow task validates actual animated text in the GUI.

Files to inspect first:

- `plugins/FluxMotionText.py`
- `Engine/Knob*.h/cpp`
- `Engine/NodeGroup*.h/cpp`
- `Engine/EffectInstance*.h/cpp`
- `Gui/FluxKeyframeModel.cpp`
- `Gui/FluxTimelineSerialization.h`
- `Gui/ProjectGuiSerialization.*`
- `openfx-flux/TextRender/TextRender.cpp`

Questions to answer with a runnable proof:

1. Can a selected `FluxMotionText` nodegroup receive new user knobs after creation?
2. Are those knobs keyframeable through Natron/Flux `Knob`/`Curve`?
3. Do they save/reopen with stable names and values?
4. Can render-time evaluation read their values per frame?
5. Can those values reach `net.flux.openfx.TextRender` without fixed slots?

Possible acceptable architectures:

- Dynamic group-level knobs plus a proven render-time bridge into TextRender.
- A Flux-native node/EffectInstance if OFX/PyPlug cannot support dynamic render-time controls.
- A Flux-side dynamic animator model with real keyframeable knobs and a proven per-frame mirroring bridge before render.

Forbidden result:

- Fixed animator slot arrays like `animator1`, `animator2`, `animator3`.

Validation:

- Create two animator groups dynamically.
- Keyframe their selector offsets independently.
- Save/reopen.
- Prove render-time code can read distinct animated values at two frames.

Done only when: a dynamic, keyframeable, render-visible control path is proven internally, or the task is marked BLOCKED with a required architecture switch for Nick. The result must be written as an implementation constraint for the front-facing animator tasks, not as user-visible product progress.

### Implementation-Spec Gate for RMT-004 through RMT-011

Before any task from RMT-004 through RMT-011 is implemented, the agent must first write a task-specific implementation spec and get Nick's approval. The spec must include:

1. Exact files/functions/classes to inspect before coding.
2. Exact files/functions/classes to modify.
3. New classes/widgets/functions to add, with names and responsibilities.
4. Precise data flow from UI control to keyframe storage to render-time evaluation.
5. Forbidden fallback paths for that task.
6. Build command and canonical GUI validation command.
7. Visual workflow test with actual animated text whenever the task touches animation or renderer behavior.

No implementation subagent may receive vague wording. If a task spec cannot be made precise, stop and ask Nick.

### Front-Facing Product Block: AE-Style Text Animators

RMT-004 through RMT-011 are the main product recovery block. Their purpose is not to prove that a renderer can draw pixels; their purpose is to build the workflow Nick asked for:

- select a text layer;
- pick fonts from UI;
- add multiple text animators from UI/context menu;
- see animator groups and selector/target controls;
- keyframe range selectors and target properties;
- see characters, words, and lines animate in the viewer;
- save/reopen and keep the same animator stack and keyframes.

The finished-product acceptance test is RMT-014 and happens only after the earlier implementation and visual workflow tests pass. Do not collapse RMT-014 into any earlier implementation proof.

### RMT-004 — Define the Real FluxMotionText Data Model and Public Contract

Goal: replace hidden JSON hand-waving with a concrete model that UI, timeline, serialization, and renderer can all use.

Files likely touched:

- New `Gui/FluxTextAnimatorModel.h`
- New `Gui/FluxTextAnimatorModel.cpp`
- Possibly `Gui/FluxTimeline.h`
- Possibly `plugins/FluxMotionText.py`
- Possibly `openfx-flux/TextRender/TextAnimatorTypes.h`

Code/data to define:

1. `FluxTextAnimator` with stable ID, name, enabled, order.
2. `FluxTextRangeSelector` with based-on, start, end, offset, amount, units, shape.
3. `FluxTextAnimatorTargets` with position, scale, rotation, opacity, tracking, fill, stroke fields.
4. Naming convention for dynamic knobs, e.g. `animator_<id>_selector_offset`.
5. Mapping from model fields to keyframeable knobs.
6. Serialization and migration rules.

Validation:

- Unit or harness creates a model with two animators.
- Converts to/from dynamic knobs or serialized form.
- Preserves stable IDs and order after save/reopen.

Done only when: future tasks can implement UI and renderer against this contract without guessing.

### RMT-005 — Build a Proper Character/Text Panel with Font Dropdown

Goal: remove the rejected raw typed font workflow.

Files likely touched:

- New `Gui/FluxTextPanel.h`
- New `Gui/FluxTextPanel.cpp`
- `Gui/CMakeLists.txt`
- `Gui/GuiPrivate.h`
- `Gui/Gui05.cpp`
- `Gui/FluxTimeline.h`
- `Gui/FluxTimeline.cpp`
- `Gui/Resources/Stylesheets/flux-dark.qss` if styling is needed

Code to write:

1. A Qt panel bound to the selected FluxMotionText layer.
2. Searchable font family dropdown using Qt/Fontconfig-visible fonts.
3. Font style/weight dropdown where available.
4. Font size control.
5. Fill color control.
6. Stroke color/width controls if renderer support is present; otherwise disabled with clear label until the renderer task lands.
7. Tracking and leading controls once renderer params exist.
8. Empty/disabled state for non-text selection.
9. Multi-selected new text layer support: applying a font/color/size writes to all selected new text layers.

Validation:

- Launch canonical GUI.
- Select text layer.
- Pick font from dropdown without typing.
- Change size and color from the panel.
- Verify viewer updates.
- Save/reopen and verify values persist.

Done only when: Nick no longer needs to type a font name to use FluxMotionText.

### RMT-006 — Extend TextRender from Basic Glyphs to Real Text Layout Controls

Goal: make the renderer support the styling controls needed by the panel and animator system.

Files likely touched:

- `openfx-flux/TextRender/TextRender.cpp`
- `openfx-flux/TextRender/TextRasterizer.h`
- `openfx-flux/TextRender/TextRasterizer.cpp`
- New `openfx-flux/TextRender/TextLayout.h`
- New `openfx-flux/TextRender/TextLayout.cpp`
- New `openfx-flux/TextRender/FontResolver.h`
- New `openfx-flux/TextRender/FontResolver.cpp`
- `openfx-flux/CMakeLists.txt`
- `plugins/FluxMotionText.py`

Code to write:

1. Font family + style/weight/stretch resolution.
2. Tracking.
3. Kerning toggle/factor if feasible.
4. Leading / line spacing.
5. Left/center/right alignment.
6. Optional point-text vs box-text data structure, even if box UI is deferred.
7. Stroke outline design or first implementation.
8. Keep files below 500 lines by splitting layout/font/raster/compositor code.

Validation:

- Render multiple fonts.
- Render multiline text.
- Change tracking and leading.
- Change alignment.
- Save/reopen and rerender.
- Compare viewer output through canonical GUI, not only headless scripts.

Done only when: renderer supports the basic Character/Paragraph panel controls needed for real text work.

### RMT-007 — Implement Text Layout Element Model for Characters, Words, and Lines

Goal: create addressable elements for animator evaluation without creating nodes per element.

Files likely touched:

- `openfx-flux/TextRender/TextLayout.h`
- `openfx-flux/TextRender/TextLayout.cpp`
- `openfx-flux/TextRender/TextRasterizer.cpp`
- New tests/harness scripts under `/tmp/opencode` or repo test path if approved

Code to write:

1. Shape full text into glyph clusters.
2. Track grapheme/glyph-cluster element ranges.
3. Derive word ranges.
4. Derive line ranges.
5. Store baseline, advance, bounds, center/pivot per element.
6. Support characters excluding spaces.
7. Keep complex scripts from being broken by byte-level indexing.

Validation:

- Text with spaces produces different character/word groups.
- Multiline text produces line groups.
- Ligature/accent text does not split by raw bytes.
- Debug dump or harness proves element counts and bounds.

Done only when: selector evaluation can target characters, words, and lines from real layout data.

### RMT-008 — Implement Range Selector Evaluator

Goal: build the AE-style selector weighting model.

Files likely touched:

- New `openfx-flux/TextRender/TextSelector.h`
- New `openfx-flux/TextRender/TextSelector.cpp`
- `openfx-flux/CMakeLists.txt`
- Animator model files from RMT-004

Code to write:

1. Based On: characters, characters excluding spaces, words, lines.
2. Start, End, Offset, Amount.
3. Percentage units first.
4. Reversed ranges: Start > End.
5. Shapes: Square, Ramp Up, Ramp Down.
6. Clean extension points for Triangle/Round/Smooth later.

Validation:

- Start 0 / End 100 gives first-to-last full influence.
- Start 100 / End 0 reverses influence.
- Offset animation moves the influence wave.
- Words/lines produce grouped weights.

Done only when: selector weights can be tested independently before visual animator application.

### RMT-009 — Apply Animator Target Deltas in TextRender

Goal: make text actually animate per character/word/line.

Files likely touched:

- `openfx-flux/TextRender/TextRender.cpp`
- `openfx-flux/TextRender/TextRasterizer.cpp`
- `openfx-flux/TextRender/TextLayout.*`
- `openfx-flux/TextRender/TextSelector.*`
- New `openfx-flux/TextRender/TextAnimatorEval.h`
- New `openfx-flux/TextRender/TextAnimatorEval.cpp`
- `openfx-flux/CMakeLists.txt`

Code to write:

1. Evaluate animator stack in order.
2. Apply weighted position deltas.
3. Apply weighted scale around element center.
4. Apply weighted rotation around element center.
5. Apply weighted opacity.
6. Apply weighted fill color.
7. Apply tracking influence if layout supports it.
8. Preserve one renderer pass and one Flux text layer.

Validation:

- Character reveal via opacity selector.
- Position wave by character.
- Word-based rotation/scale around word centers.
- Line-based offset.
- Two stacked animators produce predictable combined output.
- No generated character/word/line nodes appear in the graph.

Done only when: visible per-element animation works from render-time data.

### RMT-010 — Build the Text Animator Panel and Add Animator Context Menu

Goal: fix the empty animator tab and make Add Animator real.

Files likely touched:

- New `Gui/FluxTextAnimatorPanel.h`
- New `Gui/FluxTextAnimatorPanel.cpp`
- `Gui/CMakeLists.txt`
- `Gui/GuiPrivate.h`
- `Gui/Gui05.cpp`
- `Gui/FluxTimeline.h`
- `Gui/FluxTimeline.cpp`
- `Gui/FluxKeyframeModel.cpp`
- `plugins/FluxMotionText.py`

Code to write:

1. Panel bound to selected FluxMotionText layer.
2. `Add Animator` button.
3. Context menu entry on text layer: `Add Text Animator`.
4. Animator property menu: Position, Scale, Rotation, Opacity, Fill Color, Tracking, Stroke when supported.
5. Visible animator list with enable/disable, rename, remove.
6. Reorder controls.
7. Based On dropdown.
8. Range selector controls: Start, End, Offset, Amount, Shape.
9. Target delta controls for selected animator.
10. No hidden-only metadata as the user interface.

Validation:

- Add a Position animator from panel.
- Add an Opacity animator from context menu.
- Rename/reorder/remove animators.
- Keyframe selector Offset.
- Verify values persist save/reopen.

Done only when: the animator UI is visible and usable without raw knob hunting.

### RMT-011 — Integrate Animator Rows and Keyframes into the Timeline

Goal: make animator groups and selector/target keyframes visible in Flux's AE-like timeline hierarchy.

Files likely touched:

- `Gui/FluxTimeline.h`
- `Gui/FluxTimeline.cpp`
- `Gui/FluxKeyframeModel.cpp`
- `Gui/FluxKeyframeModel.h`
- `Gui/FluxTimelineSerialization.h` if model fields are serialized outside knobs

Code to write:

1. Text animator child rows under text layers.
2. Expand/collapse state for animator rows.
3. Row labels showing animator name and key property type.
4. Keyframe rows for selector Start/End/Offset/Amount.
5. Keyframe rows for target deltas.
6. Filtering that includes dynamic animator knobs by namespace.
7. Preserve existing effects/masks/keyframe row behavior.

Validation:

- Add two animators and expand the text layer.
- See animator rows.
- Keyframe Offset and see the key row.
- Save/reopen and verify row state/keyframes.

Done only when: text animator animation is visible and editable from the timeline.

### RMT-012 — Restore Default FluxMotionText Only After Product Workflow Exists

Goal: make `net.sf.openfx.FluxMotionText` the default text layer only after it actually satisfies Nick's requested workflow.

Prerequisites:

- RMT-003 through RMT-011 complete and accepted.

Files likely touched:

- `Gui/Gui05.cpp`
- `Gui/Gui.cpp`
- `Gui/FluxTimeline.cpp`
- `Gui/FluxKeyframeModel.cpp`
- `ARCHITECTURE.md`
- `plans/PHASES.md`
- `tasks/TASKS.md`

Code/docs to write:

1. Route default Add Text to `net.sf.openfx.FluxMotionText`.
2. Keep legacy `net.sf.openfx.FluxText` available only as reference/debug/import path if Nick wants it.
3. Update gating to require all real providers/panels.
4. Update docs only after canonical validation passes.

Validation:

- Canonical app launch command:
  ```bash
  QT_PLUGIN_PATH=/usr/lib64/qt6/plugins QT_QPA_PLATFORM=xcb /home/npittas/Flux/build/App/Natron
  ```
- Add text layer from menu.
- Pick font from dropdown.
- Add animator from panel/context menu.
- Animate range selector Offset.
- See per-character/word/line animation in viewer.
- Save/reopen.
- Render/export short sequence.
- No terminal/Python exceptions.
- Nick manually validates.

Done only when: Nick accepts the actual GUI workflow.

### RMT-013 — Performance, Cache, and Stress Validation

Goal: prevent the new text system from being correct but unusably slow.

Files likely touched:

- `openfx-flux/TextRender/TextRasterizer.*`
- `openfx-flux/TextRender/TextLayout.*`
- New cache files if needed: `TextGlyphCache.*`, `TextLayoutCache.*`
- Validation scripts if approved

Code to write:

1. Glyph cache keyed by font file, glyph ID, size, stroke params.
2. Layout cache invalidated by text/font/layout changes, not selector animation alone.
3. Avoid recomputing full shaping unnecessarily.
4. Thread-safety around caches.

Validation:

- 1k, 5k, 10k character text stress.
- Scrub Offset animation.
- Measure render/scrub timing.
- Verify cache invalidation correctness by changing text/font/size.

Done only when: performance is measured and acceptable enough for P7 continuation.

### RMT-014 — Final Product Acceptance Pass

Goal: close the recovery only with real user-facing validation.

Files touched:

- Documentation only after validation passes.

Validation matrix:

1. Build `Natron` and `FluxTextRender`.
2. Launch canonical GUI with no special OFX env unless the official launcher sets it.
3. Add FluxMotionText layer from menu and timeline context menu.
4. Use font dropdown/browser.
5. Edit text, font, size, fill, stroke/tracking if implemented.
6. Add at least two animators.
7. Animate range selector Offset on characters.
8. Animate based-on words and lines.
9. Reorder animators and verify output changes.
10. Save/reopen project.
11. Render/export a short sequence.
12. Verify terminal has no new Python/runtime exceptions.
13. Nick manually validates the workflow.

Done only when: Nick accepts it. No substitute proof counts.

## 9. Files and Current Disposition

| Path | Current disposition |
|---|---|
| `plugins/FluxText.py` | Legacy usable text v1. Do not mutate without approval. |
| `plugins/FluxMotionText.py` | Main recovery target. Must be fixed in place to reach FluxText v1 usability parity first, not implementation parity, then extended with AE-style animators after approved specs. |
| `openfx-flux/` | Salvageable renderer/build groundwork. Not product-complete. |
| `openfx-flux/TextRender/TextRender.cpp` | Basic OFX renderer. Needs layout/animator expansion. |
| `openfx-flux/TextRender/TextRasterizer.*` | Basic shaping/rasterization. Needs layout elements, controls, caching. |
| `Gui/Gui05.cpp` | Currently routes text to FluxMotionText. Keep this default path, but repair FluxMotionText so default text is usable. |
| `Gui/Gui.cpp` | Current Text menu gating uses FluxMotionText/TextRender providers. Keep aligned with the repaired default path. |
| `Gui/FluxTimeline.cpp` | Current context Text action uses new provider gates; `addTextLayer()` is generic. |
| `Gui/FluxKeyframeModel.cpp` | Allows some new text public controls, but no real animator namespace. |
| `Gui/FluxT079Harness.*` | Invalid as product proof. Quarantine/remove or relabel as smoke only. |
| `plans/PHASES.md` | Contains misleading completion/revocation mix. Needs correction in RMT-001. |
| `tasks/TASKS.md` | Contains revoked testing statuses but still has old task framing. Needs correction in RMT-001. |
| `tasks/T075`-`T079` | Keep evidence, remove false completion meaning. |

## 10. Validation Rules Going Forward

For FluxMotionText and any user-facing GUI recovery task, **screenshot or it never happened**. A completed UI step must include screenshots and/or recordings of the actual controls working. Viewport pixels alone are insufficient. Proof must show UI truth: dropdowns open/working, controls changed, properties panel state, and viewport response when relevant. Do not use "validated" for GUI-control behavior unless the artifact proves the control itself and the resulting behavior.

The following do **not** prove FluxMotionText completion:

- Headless-only `NatronRenderer` scripts.
- Isolated `HOME=/tmp` plugin discovery.
- Pixel-count screenshots.
- Internal node existence checks.
- Save/reload of hidden JSON metadata.
- Oracle review of narrow code mechanics.

Required proof for product claims:

- Canonical GUI launch.
- User-visible UI workflow.
- Dedicated font dropdown/browser.
- Add Animator UI/context menu.
- Visible animator stack.
- Keyframeable selector/target controls.
- Per-element render behavior.
- Save/reopen.
- Export/render sequence.
- No ignored terminal/Python exceptions.
- Nick manual acceptance for final default replacement.

## 11. Handoff Notes for the Next Agent

Start with RMT-001 or RMT-002 only if Nick approves. After cleanup, RMT-003 is only an internal blocker check; it must feed constraints into the front-facing animator tasks and must not be reported as product progress.

The most dangerous unresolved technical question is not glyph rendering; glyph rendering exists. The dangerous question is whether dynamic, keyframeable animator controls can be created and made visible to render-time evaluation without fixed slots. If that cannot be proven, continuing with PyPlug + OFX as-is will recreate the same failure.

The most dangerous process error is calling groundwork product progress. Every update must say exactly what user-visible behavior exists and what remains missing.

## 12. Documented Steps Taken

Every recovery step must add an entry here. If a context compression happens, the next agent must re-read this section before continuing.

### 2026-05-25 — Source-of-truth correction pass approved by Nick

- What was done: rewrote this recovery source-of-truth to incorporate Nick's corrections before executing RMT-001.
- What was found: the previous RMT-002 wording was too abstract and the plan still over-centered architecture proof instead of the final product workflow.
- What changed: added mandatory step documentation and post-compression re-read rules; rewrote RMT-002 as a human-readable cleanup/quarantine task with context; demoted RMT-003 to an internal checkpoint; added an implementation-spec gate for RMT-004 through RMT-011; explicitly marked AE-style Text Animators as the main front-facing product block; preserved RMT-014 as the final product acceptance test after earlier tests pass.
- Why: Nick approved RMT-001 conceptually but required the recovery plan itself to be corrected first so future work cannot drift into another vague or misleading implementation pass.
- Validation: documentation-only change; manually inspected the authority rules, rewritten RMT-002/RMT-003, implementation-spec gate, front-facing product block, and this ledger entry.
- Failures: none during editing.
- Next step: ask Nick for approval to execute RMT-001 tracking cleanup.

### 2026-05-25 — RMT-001 tracking cleanup approved by Nick

- What was done: corrected T075-T079 tracking language in phase/task docs and individual task files.
- What was found: `plans/PHASES.md` still had `✅` prerequisite entries for T075/T077; `tasks/TASKS.md` still listed T075-T078 as `TESTING` and T079 as `IN_PROGRESS`; T075-T079 task files still had result/review wording that read as completion or ship evidence.
- What changed: T075-T079 are now `BLOCKED` in `tasks/TASKS.md` and their individual task files; phase notes now label T075-T078 as groundwork/rejected scaffold/renderer groundwork only and T079 as rejected/blocked; individual task result sections now say the evidence is narrow history only, not product acceptance; each affected task points back to this source-of-truth file.
- Why: repo tracking must not imply the rejected FluxMotionText prototype is done, shippable, or accepted product work.
- Validation: documentation-only change; searched for misleading T075-T079 `✅`, `DONE`, `SHIP`, `completed`, `TESTING`, and `IN_PROGRESS` claims. Remaining matches are only this source-of-truth's false-claim table/instructions, not active tracking claims. Also verified all T075-T079 individual task statuses are `BLOCKED`.
- Failures: none during editing.
- Next step: ask Nick for approval to execute RMT-002 failed-experiment cleanup/quarantine.

### 2026-05-25 — RMT-002 inspection before code edits

- What was done: inspected the current rejected FluxMotionText default-creation path and harness wiring before proposing code edits.
- What was found: `Gui/Gui05.cpp` creates `net.sf.openfx.FluxMotionText` for all new text layers; `Gui/Gui.cpp` and `Gui/FluxTimeline.cpp` gate Add Text on `net.sf.openfx.FluxMotionText` plus `net.flux.openfx.TextRender`; `Gui/Gui05.cpp` calls `FluxT079Harness::maybeStart(this)` during UI setup; `Gui/FluxT079Harness.*` still describes itself as an autonomous T079 proof and expects `net.sf.openfx.FluxMotionText`; `plugins/FluxText.py` remains the usable legacy text path and was only inspected, not edited.
- What changed: no code changed during this inspection; this ledger entry was added because inspection is a large recovery step.
- Why: RMT-002 must restore the usable default Text workflow and quarantine invalid product-proof wiring without mutating legacy `plugins/FluxText.py`.
- Validation: read-only inspection of the relevant code paths and recovery rules.
- Failures: none during inspection.
- Next step: present a concrete RMT-002 code-change plan and wait for Nick's approval before editing code.

### 2026-05-25 — RMT-002 direction corrected by Nick

- What was done: rejected the previous quarantine/default-removal plan and rewrote RMT-002 around fixing `net.sf.openfx.FluxMotionText` in place.
- What was found: the previous RMT-002 wording would have removed `FluxMotionText` from the default path and restored legacy `FluxText`, which contradicts Nick's current instruction to preserve the work and make `FluxMotionText` reach original text usability first.
- What changed: RMT-002 now requires keeping default Text creation on `FluxMotionText`, using `FluxText.py` as a reference only, replacing nonfunctional knockoff controls, providing usable font/text controls, validating `FluxMotionText` in the canonical GUI, and then proceeding to AE-style animator work through later approved tasks.
- Why: Nick explicitly clarified that the goal is not to delete the failed work, but to repair `FluxMotionText` up to FluxText v1 usability parity before adding real After Effects-style animator code.
- Validation: documentation-only correction; inspected the rewritten RMT-002 goal, context, implementation intent, validation, and disposition table entries.
- Failures: the previous proposed code plan was wrong and not approved.
- Next step: inspect `FluxText.py` vs `FluxMotionText.py` and produce a precise usability-parity implementation spec before any product code edits.

### 2026-05-25 — RMT-002 usability-parity inspection and superseded plan review

- What was done: compared `plugins/FluxText.py` and `plugins/FluxMotionText.py`, inspected the Flux GUI text init/font-sync/overlay paths, inspected Add Text provider gating, inspected `FluxKeyframeModel.cpp`, and requested an Oracle review of the parity approach.
- What was found: `FluxText.py` gets usable font UI from native Text `Text1name` and syncs it to `Text1font` via `Gui05.cpp`; `FluxMotionText.py` exposes raw direct knobs (`font`, `fontSize`, `fillColor`, `translate`, etc.) and lacks a Flux-owned usability-equivalent font/text workflow; `TextRender` currently supports only `text`, `font`, `fontSize`, and `fillColor`; `Gui05.cpp` already prefers `Text1center/Text1scale/Text1rotate/textOverlayCenter` for legacy text overlay and falls back to direct names; Add Text gating currently requires only FluxMotionText and TextRender; keyframe filtering currently whitelists direct FluxMotionText knob names.
- What changed: no product code changed during inspection; the initial idea to create `Text1...` implementation parity and a hidden native Text font donor is superseded and must not be implemented.
- Why: FluxMotionText must be fixed in place with real usable controls before adding AE-style animators, and the plan must avoid another hidden/raw-knob scaffold.
- Validation: read-only inspection plus Oracle review. The first Oracle response discussed a Text1-prefixed/native Text donor option, but Nick rejected that architecture because FluxMotionText exists specifically to avoid native Text limitations. That donor approach is now invalid.
- Failures: none during inspection; the earlier quarantine/default-removal direction remains rejected.
- Next step: document Nick's correction that parity means usability parity only, then ask Nick to approve a Flux-owned RMT-002 usability-parity implementation spec before editing product code.

### 2026-05-25 — Parity wording corrected to usability parity only

- What was done: updated the recovery source-of-truth so every active RMT-002 parity reference means usability parity only.
- What was found: the previous wording could be read as implementation parity with `FluxText.py`, `Text1...` knobs, or a hidden native Text font-dropdown donor. That contradicts the architectural reason `TextRender` exists: native Text cannot support character/word/line animator evaluation.
- What changed: RMT-002 now explicitly forbids native Text node/donor/knob dependency, defines `FluxText.py` as UX reference only, requires Flux-owned `TextRender` controls for the repaired path, and marks the earlier Text1/native-donor review as superseded.
- Why: Nick instructed that all documents must define parity as usability parity, not implementation parity.
- Validation: searched recovery docs for `parity`, `Text1-prefixed`, `native Text donor`, `font-dropdown donor`, and related terms; corrected misleading active wording in this file.
- Failures: none during documentation editing.
- Next step: update other planning/tracking documents with the same usability-parity-only wording, then validate the search results.

### 2026-05-25 — Usability-parity wording propagated to planning docs

- What was done: updated non-recovery planning/architecture docs that could make parity sound like implementation parity.
- What was found: `plans/2026-05-25-flux-native-text-system-plan.md` had a `Native renderer v1 parity in isolation` heading and reusable-ideas wording that could be read as implementation reuse; `ARCHITECTURE.md` described legacy `Text1...`/native Text font selection as the generic text-layer model.
- What changed: the native text plan now says `Native renderer v1 usability baseline in isolation` and explicitly defines any FluxText comparison as usability parity only; `ARCHITECTURE.md` now labels `Text1...` and `Text1name` font sync as legacy FluxText implementation details and states that FluxMotionText must use Flux-owned `TextRender` controls.
- Why: Nick required all documents to define parity only as usability parity, never implementation parity or native Text knob reuse.
- Validation: searched `tasks/`, `plans/`, and `ARCHITECTURE.md` for `parity`, `native Text donor`, `font-dropdown donor`, `Text1-prefixed parity`, `implementation parity`, `Text1... parity`, and `FluxText v1 parity`. Remaining matches either explicitly say usability parity only or record superseded/invalid donor wording.
- Failures: none during documentation editing.
- Next step: draft the corrected RMT-002 implementation spec around Flux-owned TextRender controls and a Flux-owned font-selection UI, then ask Nick for approval before product code edits.

### 2026-05-25 — RMT-002 implementation patch and smoke validation

- What was done: implemented the approved RMT-002 usability-parity patch for `net.sf.openfx.FluxMotionText` without touching `plugins/FluxText.py` or adding any native Text donor path.
- What was found: `FluxMotionText` could stay on the default Add Text path, but it needed additional Flux-owned renderer controls and a dedicated GUI panel because the raw `font` string was not acceptable as the primary user workflow. The old T079 harness still proves only node creation/pixels, not product acceptance.
- What changed: extended `net.flux.openfx.TextRender` with `fontStyle`, `tracking`, `leading`, and `alignment`; extended `TextRasterizer` to pass those controls into Fontconfig/HarfBuzz/FreeType layout; updated `plugins/FluxMotionText.py` to alias the new renderer controls and remove the visible empty Text Animators page from page order; added `Gui/FluxTextPanel.{h,cpp}` with font family dropdown, dynamic style dropdown, size, fill, tracking, leading, and alignment controls writing to FluxMotionText direct knobs; wired the Text panel into the top-right Flux pane and selected-layer path; updated keyframe filtering for the new public text controls.
- Why: RMT-002 requires repairing `FluxMotionText` in place to original text usability before starting AE-style animator work, using Flux-owned `TextRender` controls and a Flux-owned font-selection path.
- Validation: `python3 -m py_compile plugins/FluxMotionText.py` passed; `cmake --build /home/npittas/Flux/build/openfx-flux -j$(nproc)` passed; `cmake --install /home/npittas/Flux/build/openfx-flux --prefix /home/npittas/Flux/plugins` installed `FluxTextRender.ofx.bundle`; canonical app build `cmake --build /home/npittas/Flux/build --target Natron -- -j$(nproc)` passed; canonical GUI smoke launch reached `FLUX: Layout created successfully`; env-gated T079 smoke harness produced `/tmp/opencode/rmt002-gui-proof/report.json` with `status: pass`, `pluginID: net.sf.openfx.FluxMotionText`, `viewerDisplayingImage: true`, and visible cyan pixels. Oracle reviewed the implemented patch and returned `SHIP for RMT-002 patch only` with no must-fix source defects. This validation is now explicitly limited to build/smoke/code-review proof and does not validate UI-control behavior.
- Failures: Nick reported the RMT-002 controls are still mostly stubs/non-working: no font/font-family dropdown proof, justification does not work, tracking does not work, leading does not work, and no screenshots/recording prove the controls worked. The previous UI-control validation claim was invalid. Canonical GUI launch still reports the pre-existing `qtpy`/`ModuleNotFoundError: No module named 'qtpy'` initGui error; this was not introduced by RMT-002 but remains incompatible with final acceptance if not fixed before product acceptance. The T079 harness remains smoke/pixel proof only.
- Next step: reproduce the broken RMT-002 control behavior in the canonical GUI with screenshots/recording of the actual UI state, then present an exact fix plan before further product-code edits.

### 2026-05-25 — Screenshot-proof rule added after invalid RMT-002 validation

- What was done: added Nick's explicit "Screenshot or it never happened" validation rule to this recovery source of truth and to `AGENTS.md` so future GUI claims require visual proof of actual controls.
- What was found: the previous RMT-002 entry overstated validation by listing build/smoke/pixel proof next to unproven UI-control behavior.
- What changed: Section 10 now requires screenshots/recordings for GUI-control claims; `AGENTS.md` now carries the same project-wide GUI validation rule; the RMT-002 ledger entry now says its UI-control validation claim was invalid and incomplete.
- Why: build success, smoke harnesses, and viewport pixels did not prove font dropdown, tracking, leading, justification, or Properties/Text panel behavior.
- Validation: documentation-only change; inspected the updated validation sections and RMT-002 ledger text.
- Failures: no code fixes were made; RMT-002 remains unaccepted and unvalidated for actual UI-control behavior.
- Next step: run a no-edit canonical GUI reproduction/proof pass for the broken controls, then request approval for the exact corrective implementation plan.

### 2026-05-25 — No-edit RMT-002 canonical GUI repro reached crash before control testing

- What was done: launched the canonical GUI, captured screenshot proof of the Layer > New > Text path, added a Text layer from the menu, and captured the resulting UI state.
- What was found: the canonical GUI created a timeline Text row and selected `FluxMotionText1`, but the app then crashed before the Text panel/dropdown/tracking/leading/justification controls could be tested.
- What changed: no product code changed. This ledger entry records the repro artifacts and upgrades the immediate failure from "controls unproven/non-working" to "canonical Add Text crashes before control validation can continue".
- Why: RMT-002 cannot be debugged from hypotheses or previous smoke proof; it needs a reproducible canonical GUI failure path with screenshot/log artifacts.
- Validation: proof artifacts were captured under `/tmp/opencode/rmt002-control-repro/`: `01-layer-menu.png`, `03-new-click-submenu.png`, `04-after-add-text.png`, and `natron.log`. The log shows `FLUX REBUILD`, `textInit`, transform overlay registration, then `Caught segmentation fault (SIGSEGV)` in `QOpenGLShaderProgram::bind()`.
- Failures: canonical launch still logs `ModuleNotFoundError: No module named 'qtpy'`; after Add Text the app crashed with SIGSEGV, so font dropdown, font family, justification, tracking, leading, Properties/Text panel behavior, save/reopen, and render/export remain untested and unvalidated.
- Next step: trace the Add Text crash path first, before any UI-control fix. Do not propose or implement control fixes until the crash is reproducible/understood and Nick approves the exact corrective plan.

### 2026-05-25 — RMT-002 crash trace narrowed: Add Text survives, float-pane click crashes

- What was done: ran the canonical Add Text path under `gdb`, reproduced the crash with automation, then ran a narrower no-debugger falsification pass that added Text and waited 15 seconds without touching the top-right pane controls.
- What was found: `Layer > New > Text` itself creates `FluxMotionText1` and does not crash. The `gdb` crash happened when automation clicked the top-right pane float/detach button instead of a text control/tab. The backtrace is `QAbstractButton::clicked -> TabWidget::floatPane -> TabWidget::closeSplitterAndMoveOtherSplitToParent -> Splitter::insertChild_mt_safe -> QOpenGLWidget paint -> ViewerGLPrivate::activateShaderRGB -> QOpenGLShaderProgram::bind`.
- What changed: no product code changed. The working hypothesis changed: the immediate RMT-002 blocker is no longer "Add Text crashes"; it is that Add Text leaves the new text layer without a foregrounded/bound Text panel, while the raw Properties panel still exposes string/stub-like controls.
- Why: the previous no-edit repro used screen coordinates that accidentally hit a pane control, creating a false association between Add Text and the OpenGL crash.
- Validation: `/tmp/opencode/rmt002-crash-gdb3/gdb.log` contains the float-pane crash backtrace; `/tmp/opencode/rmt002-falsify-add-only/driver.log` shows the process was still alive 15 seconds after Add Text; `/tmp/opencode/rmt002-falsify-add-only/after-add-15s.png` shows the Text layer row and `FluxMotionText1` after Add Text.
- Failures: the Text panel/dropdown/tracking/leading/alignment controls still have not been validated with UI screenshots. The canonical launch still logs the pre-existing `qtpy` error. The separate float-pane/OpenGL crash remains real but is not the Add Text failure path.
- Next step: request approval for a focused RMT-002 control-path fix: auto-select/bind new text layers, foreground the Text panel for FluxMotionText, make alignment visibly meaningful, then validate with screenshots of actual controls and viewport changes.

### 2026-05-25 — RMT-002 font dropdown corrected after failed proof

- What was done: after Nick pointed out the font selector was still not a dropdown, replaced the `FluxTextPanel` font selector with a Flux-owned explicit dropdown button and embedded font-family list that opens inside the Text panel, then selecting a listed family writes the `font` knob and refreshes the viewer.
- What was found: the previous `QFontComboBox`/`QComboBox` approach looked and behaved like a field in the canonical GUI proof path; it did not produce acceptable screenshot proof of an open font-family list. The embedded list is visible in the main-window screenshot and avoids relying on transient popup-window capture.
- What changed: `Gui/FluxTextPanel.h` and `Gui/FluxTextPanel.cpp` now use `QPushButton` + `QListWidget` for font selection, populate families from `QFontDatabase`, show the current font as `Name ▾`, hide the list after selection, repopulate style choices for the chosen family, and keep writing FluxMotionText's direct `font`/`fontStyle` knobs. No changes were made to `plugins/FluxText.py`.
- Why: RMT-002 requires a usable Flux-owned font-selection workflow, and “screenshot or it never happened” requires proof of the actual font list open, not just a field containing a font name.
- Validation: canonical app build passed with `cmake --build /home/npittas/Flux/build --target Natron -- -j$(nproc)`. GUI proof artifacts are under `/tmp/opencode/rmt002-font-dropdown-proof/run2/`: `01-text-panel-before-dropdown.png` shows the Text panel foregrounded with the `Sans ▾` font dropdown button; `02-font-dropdown-open.png` shows the opened embedded font-family list; `03-font-selected-viewport.png` shows selecting `ZonaPro-Thin` from the list updates the panel and visibly changes the viewport glyph shape. The setup used `FLUX_T079_AUTOMATED_PROOF=1` only to create and keep open a FluxMotionText layer, so this remains control-path proof, not final product acceptance.
- Failures: canonical launch still logs the pre-existing `qtpy`/`ModuleNotFoundError: No module named 'qtpy'` issue. Full RMT-002 is still not accepted: style dropdown, tracking, leading, alignment, save/reopen, menu/context Add Text, and Properties/Text panel parity still need screenshot-proof validation.
- Next step: continue RMT-002 UI-control proof/fixes for style, tracking, leading, and alignment, or stop for Nick review of the font-dropdown behavior.

### 2026-05-25 — RMT-002 tracking/leading/alignment control path validated by Nick

- What was done: performed an approved temporary instrumentation pass for the FluxMotionText UI-to-render control path, then removed the instrumentation and rebuilt/deployed the clean plugin.
- What was found: the Text panel writes for `text`, `tracking`, `leading`, and `alignment` reached `net.flux.openfx.TextRender` render-time values. Nick confirmed the live GUI test showed Leading, Tracking, and Justification working 100%.
- What changed: temporary `FLUX_TEXT_DEBUG` logging was added and then removed from `Gui/FluxTextPanel.cpp` and `openfx-flux/TextRender/TextRender.cpp`; the clean `FluxTextRender.ofx.bundle` was rebuilt and copied into `/home/npittas/.OFX/Plugins/`, because the GUI runtime was loading that user OFX bundle rather than only the repo-local install tree.
- Why: the previous screenshot attempts were wasting time without proving the fail path. The correct proof was whether panel changes reached the renderer and visibly affected the viewport.
- Validation: proof artifacts are under `/tmp/opencode/rmt002-debug-pass/`: `natron2.log` captured the temporary debug breadcrumb showing UI writes and render reads for text/leading/tracking/alignment, and `02-after-ui-writes.png`, `03-after-tracking.png`, `04-after-tracking-click2.png` captured the GUI control state and viewport response. Nick manually confirmed Leading, Tracking, and Justification worked. After removing debug code, `grep` found no remaining `FLUX_TEXT_DEBUG` in `*.cpp`; `cmake --build /home/npittas/Flux/build/openfx-flux -j$(nproc)`, `cmake --install /home/npittas/Flux/build/openfx-flux --prefix /home/npittas/Flux/plugins`, deployment to `/home/npittas/.OFX/Plugins/`, and `cmake --build /home/npittas/Flux/build --target Natron -- -j$(nproc)` passed.
- Failures: canonical launch still has the pre-existing `qtpy`/`ModuleNotFoundError: No module named 'qtpy'` issue. RMT-002 is still not final-accepted: save/reopen persistence, menu/context Add Text proof, and Properties/Text panel parity remain to be verified.
- Next step: validate save/reopen and menu/context Add Text paths with screenshots/proof, or stop for Nick review before moving to the next RMT task.

### 2026-05-25 — Viewer wake pixel/content probes added for Nick manual testing

- What was done: continued the approved viewer-refresh debug path by adding gated `FLUX_VIEWER_WAKE_DEBUG=1` probes in the upload and draw paths. Nick will run the manual GUI repro/tests.
- What was found: the previous draw/projection logs already ruled out active input, render scheduling, upload absence, texture visibility, projection/offscreen, and degenerate draw quads for the captured manual path; the next missing evidence is whether rendered image/texture/framebuffer content is blank or whether GL draw state fails despite valid geometry.
- What changed: `Gui/ViewerGL.cpp` now logs sampled rendered-image content stats in `endTransferBufferFromRAMToGPU()` (`imageSamples`, nonzero/alpha counts, max RGB/alpha, center RGBA) and sampled RAM upload-buffer stats in `transferBufferFromRAMtoGPU()`; `Gui/ViewerGLPrivate.cpp` now logs GL bind/shader/buffer/blend/scissor/depth state around draw, plus a small framebuffer checksum before/after `glDrawElements()`.
- Why: the blank viewer can no longer be debugged by more active-input/ROI/projection guesses; these probes should separate “upstream rendered blank pixels” from “valid pixels uploaded but not affecting the framebuffer”.
- Validation: canonical app build passed with `cmake --build /home/npittas/Flux/build --target Natron -- -j$(nproc)`. No GUI proof was run by the agent because Nick explicitly asked to do manual tests himself.
- Failures: no root cause or fix yet; this is instrumentation only and remains gated by `FLUX_VIEWER_WAKE_DEBUG=1`.
- Next step: Nick runs the canonical GUI with `FLUX_VIEWER_WAKE_DEBUG=1`, reproduces blank-on-add/connect, performs the repeated zoom wake, closes Natron, and shares the log for immediate analysis.

### 2026-05-25 — Viewer wake manual log analyzed and stale texture upload fix applied

- What was done: analyzed Nick's manual `/tmp/opencode/rmt002-viewer-refresh/manual-content.log` immediately after he reproduced blank viewer until three zoom-outs, then patched the upload path.
- What was found: after Add Text, `Viewer1` had valid active input and rendered/uploaded nonzero image data (`imageNonZero=12`, `imageMaxAbsRGB=1`, `imageMaxAlpha=1`). The draw path bound a valid texture/shader and drew non-degenerate geometry. The framebuffer sample did not change for the first mipmap-1 draws, then changed when the third zoom crossed to `closestPo2=4`/mipmap 2. Code inspection found a concrete bug in `ViewerGL::transferBufferFromRAMtoGPU()`: when the displayed texture type changes, the code replaced `_imp->displayTextures[textureIndex].texture` but kept uploading into the stale local `tex` pointer, so the viewer could draw the replacement texture that had not received the first upload.
- What changed: `Gui/ViewerGL.cpp` now reassigns `tex = _imp->displayTextures[textureIndex].texture` immediately after texture reallocation, and logs the realloc under `FLUX_VIEWER_WAKE_DEBUG`.
- Why: this directly explains at least the first blank-after-render/upload frame: the upload can go into an old texture while paint binds the newly allocated texture. It also matches the observed “later zoom wakes it” behavior because later renders reuse/fill the replacement texture or allocate a new mipmap texture.
- Validation: canonical app build passed with `cmake --build /home/npittas/Flux/build --target Natron -- -j$(nproc)`. Manual GUI validation is pending from Nick.
- Failures: not proven fixed yet; `FLUX_VIEWER_WAKE_DEBUG` instrumentation remains in place to compare the next run.
- Next step: Nick reruns the same canonical command and checks whether Add Text/new node appears without requiring zoom; if not, use the new log to see whether the realloc now uploads into the drawn texture and what remains stale.

### 2026-05-25 — Viewer refresh fix validated and debug cleanup completed

- What was done: recorded Nick's successful manual validation, then cleaned up temporary viewer wake instrumentation and the falsified deferred-render patch.
- What was found: Nick confirmed a perfect run after the stale texture-pointer fix: adding Text drew immediately, importing a video and adding it to the timeline also redrew immediately.
- What changed: all `FLUX_VIEWER_WAKE_DEBUG` instrumentation was removed from the viewer/input/rebuild path; the earlier deferred input-changed render helpers/state were removed because that patch was falsified; the real fix in `Gui/ViewerGL.cpp` remains: after reallocating `_imp->displayTextures[textureIndex].texture`, local `tex` is reassigned before upload.
- Why: the viewer bug was caused by uploading into an old texture pointer while drawing the replacement texture; debug probes and failed render-scheduling patch should not remain in production code.
- Validation: fixer cleanup completed; grep found no remaining `FLUX_VIEWER_WAKE_DEBUG`, `fluxViewerWakeDebugEnabled`, `scheduleInputChangedRender`, `renderInputChangedIfReady`, or `inputChangedRenderScheduled` references in `Gui/` or `Engine/`; canonical Natron build passed with `cmake --build /home/npittas/Flux/build --target Natron -- -j$(nproc)`.
- Failures: none in cleanup/build. Existing unrelated warnings remain.
- Next step: continue RMT work from the repaired `FluxMotionText`/viewer baseline; no git action taken.

### 2026-05-25 — RMT-002 text-panel keyframes and first-line local alignment implemented

- What was done: completed the approved RMT-002 follow-up for Text-panel keyframe toggles and local multiline alignment, then stopped waiting on a stuck Oracle review and self-reviewed the actual source files.
- What was found: Nick's two remaining UX gaps were real: the Flux Text panel had no direct keyframe controls, and the renderer alignment was canvas/format-oriented instead of using the first line as the local alignment reference. During self-review another issue was found: keyframe diamond states refreshed after selection/clicks but not automatically when the playhead moved.
- What changed: `Gui/FluxTextPanel.h` and `Gui/FluxTextPanel.cpp` now expose compact diamond toggle buttons for Text, Size, Fill Color, Tracking, Leading, and Alignment; the toggles use Natron knob keyframe APIs (`onKeyFrameSet` / `onKeyFrameRemoved`) on the alias master/current timeline frame and update `◆`/`◇` state. Font and fontStyle key buttons were intentionally not added in this scope. `FluxTextPanel` now also listens to timeline `frameChanged(SequenceTime,int)` and re-syncs animated values/key states as the playhead moves. `openfx-flux/TextRender/TextRasterizer.cpp` now uses the first shaped line advance as the local reference width; subsequent lines align left/center/right relative to that first line, while the overall text block remains centered/placed by the transform instead of jumping to canvas edges. No changes were made to `plugins/FluxText.py` and no fixed animator slots or parallel animation store were introduced.
- Why: RMT-002 needs FluxMotionText usability parity before AE-style animator work; text controls must be keyframeable from the Text panel, and multiline alignment must behave like local text-block alignment instead of project-format alignment.
- Validation: `cmake --build /home/npittas/Flux/build/openfx-flux -j$(nproc)` passed; `cmake --install /home/npittas/Flux/build/openfx-flux --prefix /home/npittas/Flux/plugins` passed; `FluxTextRender.ofx.bundle` was copied to `/home/npittas/.OFX/Plugins/`; canonical Natron build passed with `cmake --build /home/npittas/Flux/build --target Natron -- -j$(nproc)` and ended at `[100%] Built target Natron`. Source review confirmed the intended key buttons, alias-master key operations, frame-change refresh, and first-line local alignment code paths.
- Failures: the Oracle review session stalled and was abandoned. GUI acceptance proof is still pending under the screenshot/recording rule. `Gui/FluxTextPanel.cpp` is now 646 lines, exceeding the 500-line new-code guideline; defer splitting/refactor until after the current GUI behavior is proven unless Nick asks to clean it first. Canonical launch still has the pre-existing `qtpy`/`ModuleNotFoundError` issue until addressed separately.
- Next step: Nick should run the canonical GUI and validate the Text-panel diamonds and first-line alignment behavior. If the behavior passes, record screenshot/recording proof and then decide whether to split `FluxTextPanel.cpp` before moving to RMT-003/RMT-004.

### 2026-05-25 — RMT-002 load crash, viewer reconnect, and Text-panel animated-edit fixes

- What was done: debugged Nick's `test3.ntp` load crash report and the Text-panel keyframe-control regression, then applied the approved bounded fixes through fixer subagents.
- What was found: Nick's terminal backtrace mapped `0x59a63e` to `ViewerGL::setParametricParamsPickerColor()` at `Gui/ViewerGL.cpp:2716`, specifically a `dynamic_cast<NodeSettingsPanel*>(*it)` while Qt was hiding children during project load/layout restore. The loaded project data itself could be correct, but Viewer A was not connected to the final Flux Merge because `ProjectGui::load()` rebuilt Flux before `restoreLayout()` had recreated viewer tabs, and serialized viewer input restore could run after the first Flux rebuild. The Text-panel key buttons rendered as boxes because they depended on Unicode diamond glyphs in the global UI font. The Text-panel value setters also used plain `setValue()` after key creation, so animated knobs were not edited at the current keyed frame from the Text panel. During inspection, a subagent-introduced bug was caught before handoff: `setValueAtTime(..., nullptr)` is invalid because `Knob<T>::setValueAtTime()` asserts/dereferences `newKey`; this was corrected to use local `KeyFrame` objects.
- What changed: `Gui/ViewerGL.cpp` now skips parametric picker-color panel iteration while the project is loading, copies the visible panel list via `getVisiblePanels_mt_safe()`, and null-checks panel pointers before `dynamic_cast`. `Gui/ProjectGui.cpp` now re-runs non-destructive `rebuildCompositingGraph(timeline)` after `restoreLayout()` for loaded Flux projects with layers, so viewer tabs exist before final output is connected. `Gui/FluxTextPanel.cpp` now uses a local `FluxKeyDiamondButton` that paints outline/filled diamonds with `QPainter`, avoiding font glyph dependency. Text-panel setters now resolve the alias master once and, when the target knob is animated at a dimension, write edits with `setValueAtTime(currentFrame, ..., &KeyFrame)`; non-animated values still use `setValue()`.
- Why: RMT-002 cannot move forward while project load intermittently crashes, Viewer output needs manual reconnection after load, Text-panel key buttons are visually broken, or keyed controls become editable only from the raw Properties panel.
- Validation: canonical Natron build passed with `cmake --build /home/npittas/Flux/build --target Natron -- -j$(nproc)` and ended at `[100%] Built target Natron`. A post-fix `gdb -batch` load of `/home/npittas/Documents/test3.ntp` showed two Flux rebuilds (before and after layout restore) and no SIGSEGV/backtrace before the 90s timeout; no leftover Natron or gdb process remained after timeout cleanup. Source review confirmed the invalid null `KeyFrame*` calls were replaced with local `KeyFrame key; &key`.
- Failures: full GUI acceptance is still pending from Nick. The canonical launch still reports the pre-existing `qtpy`/`ModuleNotFoundError` issue. `Gui/FluxTextPanel.cpp` is now larger than before and still exceeds the 500-line guideline; split/cleanup should wait until behavior is stable unless Nick prioritizes cleanup.
- Next step: Nick should retest canonical GUI load of `test3.ntp`, confirm Viewer is connected to the final Merge immediately, verify no immediate load crash, and verify Text-panel diamond buttons display as actual diamonds and can edit keyed Text/Size/Fill/Tracking/Leading/Alignment values directly from the Text panel.

### 2026-05-25 — Manual File/Open crash path mapped and picker-panel iteration hardened

- What was done: accepted Nick's manual repro as authoritative after command-line and scripted GUI-open paths did not reproduce the crash, mapped Nick's pasted crash addresses, and implemented the approved `ViewerGL` hardening through a fixer subagent.
- What was found: command-line `/home/npittas/Documents/test3.ntp` load exits normally and scripted GUI-open was not equivalent to Nick's real action. Nick's manual `File/Open` crash still mapped to `ViewerGL::setParametricParamsPickerColor()` at `Gui/ViewerGL.cpp:2727`, but the log showed it happened before `Loading project:` while Qt was hiding FileDialog/widgets and delivering synthetic enter/leave events (`QWidgetPrivate::hideChildren`, `sendSyntheticEnterLeave`). Therefore the previous `project->isLoadingProject()` guard was too narrow: the stale raw `DockablePanel*` list can be hit during dialog/widget transitions before project loading starts.
- What changed: `Gui/ViewerGL.cpp` now keeps the project-loading guard but no longer iterates `Gui::getVisiblePanels_mt_safe()` raw `DockablePanel*` entries in `setParametricParamsPickerColor()`. It now enumerates live Qt-owned `NodeSettingsPanel` widgets with `gui->findChildren<NodeSettingsPanel*>()`, skips invisible panels, and null-checks `NodeGui`, `Node`, and `EffectInstancePtr` before calling `setInteractColourPicker_public()`.
- Why: the crash was another stale panel pointer/dynamic-cast failure from viewer color-picker state propagation during GUI transitions, not a project-data or TextRender failure. Enumerating live Qt objects avoids dereferencing a stale copied raw panel pointer while preserving color-picker behavior for visible live node settings panels.
- Validation: `addr2line -f -C -e /home/npittas/Flux/build/App/Natron 0x59aa86 0x5a64b8` mapped the new crash to `ViewerGL::setParametricParamsPickerColor()` at `Gui/ViewerGL.cpp:2727`; initial build caught a fixer-introduced `EffectInstancePtr` vs raw-pointer type error; the fixer corrected it; canonical Natron build passed with `cmake --build /home/npittas/Flux/build --target Natron -- -j$(nproc)` and ended at `[100%] Built target Natron`.
- Failures: Nick's exact manual File/Open flow still needs to be rerun by Nick for acceptance; the agent's automation is explicitly not accepted as equivalent proof. Canonical launch still reports the pre-existing `qtpy`/`ModuleNotFoundError` issue.
- Next step: Nick should rerun the exact canonical GUI File/Open path that crashed, verify whether the app survives opening `/home/npittas/Documents/test3.ntp`, and then continue reload validation: Viewer connected to final Merge, text scale unchanged, animation preserved, viewer not slow after load, and Text-panel keyed edits still work.

### 2026-05-25 — RMT-002 reload validation accepted by Nick

- What was done: recorded Nick's manual validation after the `ViewerGL::setParametricParamsPickerColor()` stale-panel hardening build.
- What was found: Nick confirmed the exact canonical GUI File/Open path that previously crashed now loads perfectly. Nick also confirmed the reload checklist passes: Viewer connects to the final Merge immediately, text scale is unchanged, animation is preserved, the viewer is not slow after load, and Text-panel keyed edits still work after reload.
- What changed: documentation only; no code changed in this step.
- Why: RMT-002 reload stability and saved-project behavior were the remaining blockers after Text-panel animation itself had already been confirmed working.
- Validation: Nick manually tested the canonical GUI and explicitly reported `Load works perfect now`; when asked about the remaining reload checks, Nick confirmed `All above checks pass. Approved`.
- Failures: the pre-existing `qtpy`/`ModuleNotFoundError` launch warning remains separate. `Gui/FluxTextPanel.cpp` still exceeds the 500-line guideline and should be split/cleaned after behavior is stable if Nick prioritizes cleanup.
- Next step: decide whether RMT-002 should be marked complete in tracking now, or whether to first perform a cleanup/simplification pass on oversized Text-panel code and the accumulated ViewerGL changes.

### 2026-05-25 — Startup qtpy warning fixed and PyPlug path corrected

- What was done: fixed the startup `qtpy`/`initGui.py` warning path after Nick clarified the desired fix was both to correct runtime paths and provide `qtpy`.
- What was found: `~/.config/INRIA/Natron.conf` had `groupPluginsSearchPath` set to `/home/npittas/Flux`, causing Natron to recursively scan the entire repo as PyPlugs and execute documentation example startup scripts such as `Documentation/source/devel/initGui.py`. The code path is `AppManager::loadPythonGroups()` in `Engine/AppManager.cpp`: it adds non-OFX plugin paths recursively to Python path, imports `qtpy`, then recursively runs `init.py`/`initGui.py`. A first attempt to install `qtpy` under `build/lib/python3.14/site-packages` made `PythonUtils::setupPythonEnv()` treat `build/` as `PYTHONHOME` with an incomplete standard library and caused an early Python startup crash at `PyModule_GetDict()`.
- What changed: `~/.config/INRIA/Natron.conf` now uses `/home/npittas/Flux/plugins` instead of `/home/npittas/Flux` for `groupPluginsSearchPath`. The bad `build/lib` PythonHome trigger was removed. `qtpy==2.4.3` and its `packaging` dependency were installed under `/home/npittas/Flux/build/Plugins`, which `PythonUtils::setupPythonEnv()` adds to `PYTHONPATH` without causing `build/` to become PythonHome and without placing `qtpy` inside the scanned PyPlug root.
- Why: Flux PyPlugs live under `/home/npittas/Flux/plugins`; the whole repo is not a PyPlug root. `qtpy` must be available to Natron's embedded Python, but it must be placed on a Python path that does not make Natron use an incomplete bundled Python tree or scan dependency modules as PyPlugs.
- Validation: canonical startup smoke passed with `QT_PLUGIN_PATH=/usr/lib64/qt6/plugins QT_QPA_PLATFORM=xcb /home/npittas/Flux/build/App/Natron`; the log no longer contains `Failed to import qtpy`, `ModuleNotFoundError: No module named 'qtpy'`, or `Failed to load /home/npittas/Flux/Documentation/source/devel/initGui.py`. It now reports `Info: init.py script not loaded`, `Info: initGui.py script not loaded`, and `FLUX: Layout created successfully`. No leftover Natron process remained after the timeout-terminated smoke run.
- Failures: the initial `build/lib/python3.14/site-packages` installation location was wrong and caused an early crash; it was removed and replaced with the working `build/Plugins` location. This is currently a build-local runtime dependency installation; if the build directory is deleted, `qtpy` must be reinstalled or added to the installer/bootstrap flow.
- Next step: add `qtpy` installation to the longer-lived Linux bootstrap/package workflow when T070 is resumed, so clean build directories get the dependency automatically.

### 2026-05-25 — Full UI Text Animator implementation pass reached build/smoke baseline

- What was done: began the approved full AE-style Text Animator implementation rather than another internal proof pass. Added a dynamic animator model, visible Text Animators panel, timeline context action, timeline animator rows, renderer JSON bridge, and render-time per-glyph animator evaluation.
- What was found: `FluxMotionText` already had hidden static metadata but no visible/dynamic animator workflow and `TextRender` had no `animatorStackJson` OFX param. Dynamic user knobs can be created on the selected `FluxMotionText` node and serialized into a renderer-visible JSON contract. The renderer needed RGBA raster output instead of single-alpha-plus-global-color to support per-element opacity/fill animation.
- What changed: added `Gui/FluxTextAnimatorModel.{h,cpp}` for dynamic `fta_<id>_*` knob creation, stable order, Add/Remove/Reorder, and JSON serialization including current values plus keyframe curves; added `Gui/FluxTextAnimatorPanel.{h,cpp}` with visible Add Animator menu, enable/name/reorder/remove controls, Based On selector, selector Start/End/Offset/Amount/Shape controls, target controls for Position/Scale/Rotation/Opacity/Fill/Tracking, and painted keyframe buttons; wired the new panel into the Flux top-right pane and text-layer selection path in `Gui/Gui05.cpp`; added layer context-menu `Add Text Animator`; added text animator child rows to the expanded timeline and exposed dynamic `fta_` knobs in `FluxKeyframeModel`; added `animatorStackJson` to `net.flux.openfx.TextRender` and aliased `FluxMotionText` metadata to it; changed `TextRasterizer` to produce premultiplied RGBA and apply selector-weighted per-glyph position/scale/rotation/opacity/fill/tracking at render time from the JSON/keyframe curves.
- Why: Nick approved the full product implementation scope: visible UI animators, dynamic groups, range selectors, keyframeable target/selector controls, renderer evaluation, timeline rows, and save/reopen path without fixed slots, native Text donor tricks, or multi-node-per-character implementations.
- Validation: `python3 -m py_compile /home/npittas/Flux/plugins/FluxMotionText.py` passed; `cmake --build /home/npittas/Flux/build/openfx-flux -j$(nproc)` passed; `cmake --install /home/npittas/Flux/build/openfx-flux --prefix /home/npittas/Flux/plugins` passed; updated `FluxTextRender.ofx.bundle` was copied to `/home/npittas/.OFX/Plugins/`; canonical Natron build `cmake --build /home/npittas/Flux/build --target Natron -- -j$(nproc)` passed and ended at `[100%] Built target Natron`; canonical GUI smoke launch reached `FLUX: Layout created successfully`; `pgrep -a Natron` confirmed no Natron process remained after the timeout-terminated smoke run.
- Failures: no screenshot/recording proof yet for actual Add Animator UI, dynamic controls, per-character/word/line viewport behavior, keyframe rows, save/reopen, or render/export. `openfx-flux/TextRender/TextRasterizer.cpp` is currently over the 500-line guideline after the first implementation pass and should be split after behavior is proven. Current renderer selector/evaluator is functional but still needs GUI validation and likely visual tuning for word/line pivots and complex-script edge cases.
- Next step: run canonical GUI validation with proof artifacts: Add Text, open Text Animators tab, add Position and Opacity animators from panel/context menu, keyframe Offset/targets, show expanded timeline animator rows/key rows, scrub per-element animation, save/reopen, render/export, close Natron, and verify no Natron process remains.

### 2026-05-26 — Text Animator playback/cache invalidation fix after Nick manual report

- What was done: debugged Nick's manual report that keyed Text Animator Offset updated while editing but froze during frame scrub/playback, and that letters snapped into place instead of visibly animating. Implemented the approved cache/time-dependency fix without running a GUI self-test.
- What was found: `FluxTextAnimatorPanel` only forced a viewer redraw after UI edits, while the renderer output depended on keyframes embedded inside a constant `animatorStackJson` string. Natron therefore had no normal animated OFX parameter value changing per frame for the embedded animator curves, so cached TextRender output could freeze until another UI edit invalidated the render. The snap symptom also matched the initial Position animator's default Square selector shape, which creates hard per-letter selection boundaries.
- What changed: `openfx-flux/TextRender/TextRender.cpp` now defines/fetches a hidden animated `animatorTimeDependency` double param and reads it at render time; `plugins/FluxMotionText.py` now creates and aliases the hidden `animatorTimeDependency` group param to `TextRender1`; `Gui/FluxTextAnimatorModel.cpp` now mirrors every `fta_*` key time into `animatorTimeDependency` keys whenever animator JSON is synced, and new Position animators default to Ramp Down selector shape instead of Square; `Gui/FluxTextAnimatorPanel.cpp` now calls `renderAllViewers(true)` after animator edits/adds instead of only repainting existing GL contents; `Gui/FluxTimeline.cpp` skips connecting native keyframe-change handlers to `animatorTimeDependency` itself so the synthetic cache-invalidation keys do not recursively trigger another JSON/dependency sync.
- Why: the host cache/render engine must see a time-varying param for embedded animator curves, and animator edits should request a real render, not just a GL redraw. The default selector should demonstrate smooth motion rather than AE-style Square-step snapping unless the user explicitly chooses Square.
- Validation: `python3 -m py_compile /home/npittas/Flux/plugins/FluxMotionText.py` passed; `cmake --build /home/npittas/Flux/build/openfx-flux -j$(nproc)` passed; `cmake --install /home/npittas/Flux/build/openfx-flux --prefix /home/npittas/Flux/plugins` passed; updated `FluxTextRender.ofx.bundle` was copied to `/home/npittas/.OFX/Plugins/`; canonical Natron build `cmake --build /home/npittas/Flux/build --target Natron -- -j$(nproc)` passed and ended at `[100%] Built target Natron`; after adding the recursion guard, canonical Natron build passed again and ended at `[100%] Built target Natron`.
- Failures: no GUI proof was captured because Nick explicitly asked to do manual testing himself. Existing projects/layers created before this patch may need recreation/reload through the updated PyPlug path to get the new hidden alias param if Natron does not auto-upgrade PyPlug group params.
- Next step: Nick should rerun the same manual test: add text, add Position animator, key Offset 0→100 over 10 frames, scrub/playback, and confirm the viewer refreshes per frame and the default motion no longer snaps unless Square selector is chosen.

### 2026-05-26 — Selector offset semantics and scale percentage fix implemented

- What was done: implemented Nick's approved AE-style selector/scale correction after he confirmed playback was working perfectly: Linear offset semantics, Ramp Up/Down endpoint/midpoint semantics, Offset UI clamp, linked Scale UI, and clearer Amount labeling.
- What was found: the renderer was still using the older `start/end + offset` range-shift model, so `Offset 100` and `Offset -100` could move the selector range off the text instead of putting all letters at the target. Scale was still treated as a delta (`1 + scale/100`) with default split X/Y controls, so `Scale 0` meant no change instead of zero size. The visible panel exposed `Amount` without context and allowed selector fields to range `-1000..1000`.
- What changed: `openfx-flux/TextRender/TextRasterizer.cpp` now clamps selector Offset to `-100..100`; shape index 0 is now labeled/treated as Linear with `0 = none`, `100 = all start→end`, and `-100 = all end→start`; Ramp Up maps `-100/0/100` to none/halfway/all; Ramp Down maps `100/0/-100` to none/halfway/all. Scale math now treats `100` as unchanged current size, `0` as zero size, and partial selector weights interpolate toward the target percentage. `Gui/FluxTextAnimatorModel.*` now creates Scale as `Scale %` with default `100,100`, adds persistent `fta_<id>_scaleSeparated`, labels `amount` as `Strength`, emits animator JSON version 2, and migrates old non-Scale animators with unkeyed `scale=0,0` to `100,100` so existing Position/Opacity animators do not collapse after the semantic change. `Gui/FluxTextAnimatorPanel.*` now shows selector Shape as `Linear/Ramp Up/Ramp Down`, clamps Start/End/Strength to `0..100`, clamps Offset to `-100..100`, labels Amount as `Strength %`, and replaces the default split Scale X/Y controls with a linked `Scale %` spinner plus an explicit `Separate X/Y` checkbox.
- Why: Nick clarified the expected After Effects behavior and explicitly approved this scope. The previous selector implementation did not satisfy the endpoints, and the previous scale UI/math contradicted the expected percentage-of-current-size behavior.
- Validation: `python3 -m py_compile /home/npittas/Flux/plugins/FluxMotionText.py` passed; `cmake --build /home/npittas/Flux/build/openfx-flux -j$(nproc)` passed; `cmake --install /home/npittas/Flux/build/openfx-flux --prefix /home/npittas/Flux/plugins` passed; updated `FluxTextRender.ofx.bundle` was copied to `/home/npittas/.OFX/Plugins/`; canonical Natron build `cmake --build /home/npittas/Flux/build --target Natron -- -j$(nproc)` passed and ended at `[100%] Built target Natron`.
- Failures: no GUI proof was captured because Nick has been doing the manual animator validation. `openfx-flux/TextRender/TextRasterizer.cpp` remains over the 500-line guideline and should be split after behavior is accepted. Existing Scale animators created before this semantic change may need manual review because old scale values were delta percentages and new values are absolute percentages.
- Next step: Nick should manually test the approved matrix: Linear Offset `0/100/-100`, Ramp Up `-100/0/100`, Ramp Down `100/0/-100`, Offset UI limits, Scale `0/100`, linked Scale default, and Separate X/Y behavior.

### 2026-05-26 — Add Animator property-pick menu removed

- What was done: implemented Nick's approved UX simplification for Option B: a text animator always exposes the full property set, so adding one should not ask which property to seed.
- What was found: the Text Animators panel button opened a property menu (`Position`, `Scale`, `Rotation`, `Opacity`, `Fill Color`, `Tracking`) even though every created animator displayed all target controls. The timeline context menu also created an `Add Text Animator` submenu with the same target choices. This made the property choice misleading.
- What changed: `Gui/FluxTextAnimatorPanel.cpp` now makes the `Add Animator` button directly create a generic animator and refresh the viewer, with no popup menu. `Gui/FluxTimeline.cpp` now shows one direct `Add Text Animator` action for text layers instead of a property submenu. `Gui/FluxTextAnimatorModel.cpp` now names generic animators `Animator N` when no initial target is passed; the old target-specific seeding path remains available internally but is no longer used by the panel or timeline context menu.
- Why: if all animator target controls are present in one animator, asking for a property at creation is redundant and confusing.
- Validation: source inspection confirmed the panel no longer creates a target menu and the timeline context menu now has a single direct `Add Text Animator` action. Canonical Natron build `cmake --build /home/npittas/Flux/build --target Natron -- -j$(nproc)` passed and ended at `[100%] Built target Natron`.
- Failures: no GUI screenshot proof was captured; Nick is doing the manual animator validation. Existing warnings in `FluxTimeline.cpp` about `qsizetype` formatting remain unrelated and pre-existing.
- Next step: Nick should verify both entry points: the Text Animators panel `Add Animator` button and the timeline context-menu `Add Text Animator` action both directly add a generic animator without a property-selection submenu.

### 2026-05-26 — Corrected Square/Linear distinction and animator-start-state semantics rebuilt

- What was done: corrected the selector/scale semantic patch after Nick pointed out that the previous build had effectively renamed Square to Linear and still did not match the requested animator-start-state behavior.
- What was found: the current renderer/UI only exposed three selector shapes and used shape index `0` as Linear. That removed the distinct Square shape Nick explicitly asked for. The generic animator default also inherited the wrong shape mapping. The renderer scale fallback for parsed animator JSON could still treat missing scale entries as `0` instead of the neutral `100`, which risks collapsing text when scale was not meant to be active.
- What changed: `openfx-flux/TextRender/TextRasterizer.cpp` now treats selector weight as selected/new-state influence: `1.0` means the animator values are applied, `0.0` means original text state. Shape `0` is Square with a hard switch, shape `1` is Linear with an interpolated crossing, shape `2` is Ramp Up, and shape `3` is Ramp Down. Offset semantics now match Nick's stated endpoints: Square/Linear `0 = animator/new state`, `100 = original from start to end`, `-100 = original from end to start`; Ramp Up `-100 = animator/new state`, `0 = halfway`, `100 = original`; Ramp Down `-100 = original`, `0 = halfway`, `100 = animator/new state`. Scale remains absolute percentage of original size (`100 = unchanged`, `0 = zero size`, `200 = double size`) and is interpolated between original multiplier `1.0` and selected multiplier `scale/100`. `Gui/FluxTextAnimatorModel.*` now creates and serializes four shape choices (`Square`, `Linear`, `Ramp Up`, `Ramp Down`) with Linear as the default for generic animators, keeps old internal target seeding available, maps the old Position seed to Ramp Down at index `3`, and repopulates existing shape choices for compatibility. `Gui/FluxTextAnimatorPanel.cpp` now shows the four-shape dropdown while preserving Option B direct Add Animator behavior and the linked Scale/Separate X/Y UI.
- Why: Nick clarified that Square and Linear are different: Square is a no-interpolation switch, while Linear interpolates between point A and point B. He also clarified that animator property values represent the starting/new/selected state, while Offset drives the return toward the original state.
- Validation: `python3 -m py_compile /home/npittas/Flux/plugins/FluxMotionText.py` passed; `cmake --build /home/npittas/Flux/build/openfx-flux -j$(nproc)` passed; `cmake --install /home/npittas/Flux/build/openfx-flux --prefix /home/npittas/Flux/plugins` passed; updated `FluxTextRender.ofx.bundle` was copied to `/home/npittas/.OFX/Plugins/`; canonical Natron build `cmake --build /home/npittas/Flux/build --target Natron -- -j$(nproc)` passed and ended at `[100%] Built target Natron`. After a small range-normalization correction for Square selectors, the full same build/deploy/Natron build sequence passed again and ended at `[100%] Built target Natron`.
- Failures: no GUI screenshot proof was captured because Nick is doing the manual animator validation. Existing projects/animators created during the rejected three-shape patch may have shape index `0`; with the corrected mapping, index `0` is now Square, so those should be recreated or manually changed to Linear where needed. `openfx-flux/TextRender/TextRasterizer.cpp` remains over the 500-line guideline and should be split only after behavior is accepted.
- Next step: Nick should manually verify the matrix again in the GUI: selector dropdown contains `Square`, `Linear`, `Ramp Up`, `Ramp Down`; generic Add Animator creates a Linear animator; Position `-100`, Scale `0`, Rotation `90` at Offset `0` show the animator/new state; Square hard-switches; Linear interpolates; Ramp Up and Ramp Down match the `-100/0/100` endpoint semantics.

### 2026-05-26 — Ramp Up/Down per-element gradient restored after Nick manual report

- What was done: fixed Nick's report that Square and Linear were fine, but Ramp Up and Ramp Down now moved/scaled/rotated the full word/line as one block.
- What was found: the previous correction made Ramp Up/Down weights depend only on Offset, not on the element's normalized position within the selected range. That produced a constant weight for every character/word/line in the selector; because transforms use the selected element pivot, a constant word/line weight makes the whole word/line move/scale/rotate together instead of ramping through the elements.
- What changed: `openfx-flux/TextRender/TextRasterizer.cpp` now computes Ramp Up/Down with the per-element `u` value again while preserving Nick's endpoint semantics. Ramp Up now uses `clamp01(u - offset/100)`: Offset `-100` is all animator/new state, `0` is a rising per-element ramp from original at the start to animator state at the end, and `100` is all original. Ramp Down now uses `clamp01((1-u) + offset/100)`: Offset `-100` is all original, `0` is a falling per-element ramp from animator state at the start to original at the end, and `100` is all animator/new state.
- Why: Ramp shapes must be gradients across characters/words/lines, not constant global selector strengths. The endpoints were right, but the spatial ramp was missing.
- Validation: `python3 -m py_compile /home/npittas/Flux/plugins/FluxMotionText.py` passed; `cmake --build /home/npittas/Flux/build/openfx-flux -j$(nproc)` passed; `cmake --install /home/npittas/Flux/build/openfx-flux --prefix /home/npittas/Flux/plugins` passed; updated `FluxTextRender.ofx.bundle` was copied to `/home/npittas/.OFX/Plugins/`; canonical Natron build `cmake --build /home/npittas/Flux/build --target Natron -- -j$(nproc)` passed and ended at `[100%] Built target Natron`.
- Failures: no GUI screenshot proof was captured because Nick is doing manual validation. This remains a targeted renderer weighting fix only; `TextRasterizer.cpp` is still oversized and should not be split until behavior is accepted.
- Next step: Nick should retest Ramp Up and Ramp Down on character/word/line Based On modes and confirm the transforms ramp through the elements instead of moving/scaling/rotating the full word/line uniformly.

### 2026-05-26 — Nick accepted core UI Text Animator task; finishing touches recorded

- What was done: recorded Nick's manual acceptance that the full UI Text Animator task is now working and 100% complete for the core scope, with remaining items categorized as finishing touches rather than blockers.
- What was found: Nick confirmed Square and Linear are fine, Ramp Up/Down were fixed, and the current task should be considered done. He also identified four follow-up polish/UX issues that must survive context compression and be implemented later.
- What changed: documentation/tracking only in this step. No code changed. The follow-up list is now canonical until moved into a dedicated task file:
  1. Preserve edited animator target values when timeline keyframes are moved or when selector shape is changed. Current symptom: moving keyframes in the timeline or changing shape from Linear to Ramp resets the edited target value such as Scale, Position, Rotation, etc., making it impossible to compare animation curves using the same target settings.
  2. Add user-selectable text animator transform anchor/pivot for scale/rotation/position application. Current behavior scales each letter around its center. Required dropdown options should include at least bottom-left, center, and bottom-right for letter/word/line pivots; Nick can decide exact default if needed.
  3. Add slider controls, not only spinbox numbers, for Scale, Start, End, Offset, and Amount/Strength so these values are fast to adjust and animate.
  4. Timeline selection behavior: selecting the text layer should continue opening the Text panel; selecting an animator row under a text layer should open the Text Animator panel and visually outline/highlight the selected animator.
- Why: Nick explicitly said the task is done and working, but these finishing touches must be written down so they are not lost after context compaction.
- Validation: Nick manual validation/acceptance. No build or GUI proof was run in this documentation-only step.
- Failures: none for the documentation update. These follow-ups are not yet implemented.
- Next step: when Nick asks for polish, implement the four finishing touches above as a separate bounded follow-up task; do not reopen the accepted core Text Animator task unless Nick reports a regression.

### 2026-05-26 — T081 Text Animator finishing touches implemented to build baseline

- What was done: implemented the four T081 finishing touches after Nick requested the post-acceptance polish before commit.
- What was found: animator row selection had no dedicated selection type/signal, the Text Animator panel had no selected animator state, selector/strength/scale controls were spinbox-only, and renderer pivot selection was hard-coded to the center of the selected character/word/line bounds. The compatibility repair path still contained a scale-default rewrite that could overwrite unkeyed animator target values.
- What changed: `FluxTextAnimatorModel` now creates/persists an animated `fta_<id>_anchor` choice and serializes it into `animatorStackJson`; the compatibility path no longer rewrites unkeyed Scale target values during refresh. `FluxTextAnimatorPanel` now has `setSelectedAnimatorId()`, outlines the selected animator group, adds an Anchor dropdown, and adds sliders alongside Start, End, Offset, Strength, and Scale controls. `FluxTimeline` now has a text-animator selection type/signal and highlights selected animator rows. `Gui05.cpp` now routes animator-row selection to the Text Animator panel tab and selection outline. `TextRasterizer` now reads animator `anchor` and applies bottom-left, center, or bottom-right pivots for scale/rotation/position transforms.
- Why: Nick accepted the core Text Animator task but requested these workflow polish items before moving on.
- Validation: `python3 -m py_compile plugins/FluxMotionText.py` passed; `cmake --build build/openfx-flux -j$(nproc)` passed; `cmake --install build/openfx-flux --prefix plugins` passed; updated `plugins/FluxTextRender.ofx.bundle` was copied to `$HOME/.OFX/Plugins/`; canonical Natron GUI build `cmake --build build --target Natron -- -j$(nproc)` passed and ended at `[100%] Built target Natron`.
- Failures: no GUI screenshot/recording proof was captured in this step; T081 should remain in REVIEW/validation-pending until Nick validates the controls manually or approves a GUI proof pass.
- Next step: inspect working tree, run code review/cleanup, then commit only the intended accepted baseline if Nick's requested commit step remains approved.

### 2026-05-26 — T081 Text Animator finishing touches manually validated by Nick

- What was done: recorded Nick's Natron GUI validation of all requested T081 finishing-touch checks after the committed implementation.
- What was found: Nick reported all T081 tests passed.
- What changed: tracking only. `tasks/TASKS.md` marks T081 `DONE`; `plans/PHASES.md` marks T081 complete and updates the known follow-up note.
- Why: T081 had implementation/build/review proof but was intentionally left in `REVIEW` until Nick validated the actual GUI controls.
- Validation: Nick manually tested target-value preservation during selector/keyframe workflow, Anchor dropdown behavior, sliders for Start/End/Offset/Strength/Scale, animator-row selection opening/highlighting the Animator panel, text-layer row selection returning to the Text panel, and save/reopen persistence. Nick reported: `All tests passed.`
- Failures: none reported.
- Next step: T081 is complete. Leave T082 Illustrator/PDF-vector import and T083 AI matte/depth tools for the next run unless Nick changes priority.
