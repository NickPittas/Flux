# Planner Report

## Status
ready

## Rationale
Locator evidence and authorized context show the model already has serialized `FluxEffect::enabled` and `FluxMask::enabled` fields, while `Gui/FluxTimeline.cpp` paints/selects effect and mask rows without toggle hit zones, and `Gui/Gui05.cpp::rebuildCompositingGraph()` already uses enabled state for layer/effect mask selection and per-layer effect node disabling. The narrow implementation is therefore limited to adding visible row toggles and ensuring graph bypass/disable semantics are applied for all effect rows, adjustment-effect rows, layer-mask rows, and effect-mask rows.

# Task Packet

## User Goal
Add timeline enable/disable controls for all effect rows and all mask rows: layer masks, effect masks, and all effects. Toggling must change the actual Natron graph behavior, persist across save/reopen via existing serialized enabled fields, and be proven with build plus GUI screenshot/recording evidence. Do not hijack native RotoPaint behavior and do not broaden into AI/mask redesign work.

## Mode
general-coding

## Relevant Locations
- file: `Gui/FluxTimeline.h`
  symbol: `struct FluxEffect`, `struct FluxMask`, `FluxTimeline` public/private methods
  approximate lines: 90-118, 220-390
  stable anchor: `bool enabled;` in both structs; `Q_SIGNALS:` and private helper declarations
  reason: enabled state already exists; add narrow toggle helper declarations only if needed.
  confidence: high
- file: `Gui/FluxTimeline.cpp`
  symbol: `FluxTimeline::drawLayerBars`
  approximate lines: 1854-1936
  stable anchor: `} else if (visibleRow.type == eFluxVisibleRowEffect)` and `} else if (visibleRow.type == eFluxVisibleRowMask)`
  reason: effect and mask rows are painted here; add a small enable/disable control in the fixed control column for these rows and dim disabled labels/rows.
  confidence: high
- file: `Gui/FluxTimeline.cpp`
  symbol: `FluxTimeline::mousePressEvent`
  approximate lines: 2999-3046
  stable anchor: `if (clickRow->type == eFluxVisibleRowEffect)` and `if (clickRow->type == eFluxVisibleRowMask)` in label-area handling
  reason: effect/mask row selection currently consumes clicks before any toggle; add hit testing for a row enable button before selection.
  confidence: high
- file: `Gui/Gui05.cpp`
  symbol: `Gui::rebuildCompositingGraph(FluxTimeline*)`
  approximate lines: 2288-2336, 2635-2674, 2672-2723, 2724-2965
  stable anchor: adjustment-row loop comment `Disable state is controlled by updateAdjustmentTrimKeyframes()`; per-layer effect line `effect.node->setNodeDisabled(!effect.enabled)`; `firstEnabledEffectMask(layer, e)`; `firstEnabledLayerMask(layer)`
  reason: ensure toggled state has graph consequences for all effect types and mask types; per-layer effects already disabled, masks already selected by enabled state, but adjustment effects must also honor `FluxEffect::enabled` without breaking existing trim keyframes.
  confidence: high
- file: `Gui/FluxTimelineSerialization.h`
  symbol: `FluxEffectSerialization`, `FluxMaskSerialization`
  approximate lines: 34-57, 61-90
  stable anchor: `make_nvp("Enabled", enabled)`
  reason: read-only confirmation that enabled fields already persist for save/reopen.
  confidence: high

## Allowed Edit Files
- `Gui/FluxTimeline.h`
- `Gui/FluxTimeline.cpp`
- `Gui/Gui05.cpp`

## Read-Only Context Files
- `Gui/FluxTimelineSerialization.h`

## Required Change
1. In `Gui/FluxTimeline.cpp`, add an enable/disable toggle for every `eFluxVisibleRowEffect` and `eFluxVisibleRowMask` row in the fixed control column. Reuse the existing layer-row L/V/S visual pattern where practical, but keep this narrow: one button is enough, preferably aligned at the existing visibility column x=20 and labeled `V` or an obvious enabled marker. Disabled rows must be visually distinguishable (dim text/row/control) without changing selection behavior outside the toggle hit zone.
2. In `FluxTimeline::mousePressEvent`, before selecting an effect or mask row in the label/control area, hit-test the new toggle rect for the clicked visible row. On hit:
   - For effect rows, flip `_layers[layerIndex].effects[effectIndex].enabled`.
   - For mask rows, flip `_layers[layerIndex].masks[maskIndex].enabled`.
   - Emit `effectsChanged(layerIndex)` for effect toggles and `masksChanged(layerIndex)` for mask toggles if existing UI panels depend on those signals.
   - Always emit `compositingChanged()` so graph wiring/disable state updates immediately, then `update()`.
   - Do not open/select the row on a toggle hit unless existing layer V behavior does so; keep behavior consistent with layer toggles.
3. In `Gui/Gui05.cpp::rebuildCompositingGraph`, make graph behavior match UI state:
   - Per-layer effects should remain wired in order but be node-disabled when `!effect.enabled` (existing line near 2670 should be preserved or tightened).
   - Adjustment-row effects must also honor `FluxEffect::enabled`. Because existing adjustment trim uses disable-knob keyframes and comments warn against blind `setNodeDisabled()`, implement the least invasive safe path: when disabled, bypass the adjustment effect in the main pipe by not advancing `adjustmentOutput` through that effect and, if necessary, disconnect its main input; when enabled, keep current wiring/trim-keyframe behavior. Do not overwrite adjustment trim keyframes.
   - Layer masks/effect masks should continue to use only enabled masks via existing `firstEnabledLayerMask()` and `firstEnabledEffectMask()` paths. Ensure toggling a mask off removes/disconnects its graph influence on rebuild (existing no-mask cleanup for layer masks and effect mask input disconnect should remain active).
4. Do not change serialization schema unless implementation proves the existing enabled fields are not restored. If save/reopen fails due to missing assignment outside `FluxTimelineSerialization.h`, stop and report because the authorized edit surface did not include the restore implementation file(s).
5. Do not add native RotoPaint capture/hijack behavior, AI mask workflow changes, new mask types, broad UI redesign, or unrelated refactors.

## Non-Goals
- No native RotoPaint hijack or replacement workflow.
- No broad AI/mask generation changes.
- No serialization schema redesign; existing `Enabled` fields are the intended persistence path.
- No changes to layer L/V/S behavior.
- No support for multiple simultaneously active masks beyond existing first-enabled V1 behavior.
- No commits, staging, or git history changes.

## Validation
Commands:
- `cmake --build build -j$(nproc) --target Natron`
- Launch Flux/Natron GUI from the current build using the project’s existing run command, create/open a project with a layer effect, an adjustment effect row, a layer mask, and an effect mask, then toggle each row enabled/disabled from the timeline.

Expected result:
- Build succeeds with no new compile errors.
- GUI proof artifact (screenshot or short recording) shows the timeline controls themselves for an effect row, adjustment effect row, layer mask row, and effect mask row in both enabled/disabled states where practical, plus visible viewport/node-graph response demonstrating disabled rows are actually bypassed/disabled.
- Save the project with at least one disabled effect and one disabled mask, reopen it, and screenshot/record that the disabled state and graph behavior persist.

## Stop Conditions
Stop and report if:
- target symbol is missing
- required fix exceeds allowed files
- validation cannot run
- existing architecture contradicts the requested change
- task requires product/design judgment not in packet
- adjustment-row disable cannot be implemented without overwriting trim keyframes or editing files outside the allowed set
- save/reopen persistence requires editing files other than `Gui/FluxTimeline.h`, `Gui/FluxTimeline.cpp`, or `Gui/Gui05.cpp`
- adding mask toggles would require native RotoPaint behavior changes or AI workflow changes

## Planner Self-Check
- locator evidence sufficient: yes — high-confidence evidence identifies existing enabled fields, row paint/select anchors, layer button pattern, graph rebuild anchors, and serialization anchors.
- allowed edit files minimal and explicit: yes — only timeline UI/header and graph rebuild file are editable.
- read-only context minimal: yes — serialization header only confirms existing persistence fields.
- anchors/lines included: yes — relevant locations include path, symbol, approximate lines, stable anchor, reason, and confidence.
- validation concrete: yes — includes build command plus required GUI screenshot/recording and save/reopen proof.
- parallelization decision explicit and safe: yes — single task; UI toggle state and graph behavior share `Gui/FluxTimeline.cpp`/`Gui05.cpp`, so splitting would create interference.
- non-goals and stop conditions sufficient: yes — blocks broad AI/RotoPaint/serialization redesign and out-of-scope file edits.
- reviewer findings addressed, if revision: not applicable — no prior reviewer findings supplied.

## Required Return Contract
Return only a task-focused summary. Do not include transcript, tool logs, raw file dumps, large code blocks, or broad unrelated issues. Include status, files inspected/changed, validation evidence, blockers, and task-specific risks.
