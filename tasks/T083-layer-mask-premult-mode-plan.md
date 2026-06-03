# Planner Report

## Status
ready

## Rationale
Locator evidence and authorized context identify one narrow edit surface: the existing Flux layer-mask model/serialization, timeline context menu, and Gui05 layer-mask graph wiring. The plan preserves current projects and default behavior by making premultiply the default, then adds a per-mask opt-out that lets layer alpha remain in the flow by skipping only Flux's final layer-mask Premult node for native Roto/RotoPaint and AI raster masks.

# Task Packet

## User Goal
Implement approved Packet B: add a per-layer-mask premult/preserve-alpha option. Default behavior remains current premult. Nick's approved direction: "We might not want to premultiply and just have the alpha in the flow." The option must apply to AI raster layer masks and native Roto/RotoPaint layer masks, must not hijack RotoPaint/user nodes, and must save/reopen.

## Mode
general-coding

## Relevant Locations
- file: `Gui/FluxTimeline.h`
  symbol: `struct FluxMask`
  approximate lines: 101-122
  stable anchor: `struct FluxMask {` with fields `enabled`, `inverted`, `effectIndex`, `externalShuffleNode`
  reason: Add a per-mask boolean state such as `premultiply` or `preserveAlphaInFlow`; default must preserve current Premult behavior.
  confidence: high
- file: `Gui/FluxTimeline.h`
  symbol: `FluxTimeline` public methods/signals
  approximate lines: 248-302
  stable anchor: `bool removeMaskFromLayer(int layerIndex, int maskIndex);`
  reason: Add a narrow setter/toggler API for the mask premult option if needed by the context menu; emit existing `masksChanged`/`compositingChanged`.
  confidence: medium
- file: `Gui/FluxTimeline.cpp`
  symbol: mask duplication/add/remove/apply helpers
  approximate lines: 656-706, 1073-1142, 1146-1235, 1270-1335
  stable anchor: comments `// Map masks`, `FluxTimeline::addLayerMask`, `FluxTimeline::addEffectMask`, `FluxTimeline::applyExternalMaskToSelectedRow`
  reason: Copy the new field on duplicate, initialize defaults, and ensure AI-mask application does not change the selected mode.
  confidence: high
- file: `Gui/FluxTimeline.cpp`
  symbol: `FluxTimeline::contextMenuEvent`
  approximate lines: 3619-3655 and 3966-3973
  stable anchor: `// Mask row context menu` and action text `Add Layer Mask`
  reason: Cheap UI/control surface: on mask rows add a checkable action named `Premultiply Mask Alpha` (checked by default). Optional layer-row submenu is non-goal unless trivial; minimum accepted surface is mask-row context menu.
  confidence: high
- file: `Gui/FluxTimelineSerialization.h`
  symbol: `FluxMaskSerialization` and version macros
  approximate lines: 60-101 and 425-427
  stable anchor: `BOOST_CLASS_VERSION(NATRON_NAMESPACE::FluxMaskSerialization, 2)`
  reason: Persist new mode with a version bump; older projects must load as premultiply enabled.
  confidence: high
- file: `Gui/Gui05.cpp`
  symbol: layer-mask graph labels/helpers
  approximate lines: 1696-1703, 1909-1944
  stable anchor: `kFluxLayerMaskPremultLabel`, `ensureInlineLayerMaskPremultNode`
  reason: Existing code always creates/stores final Premult in `layer.maskApplyNode`; worker must conditionally skip/deactivate only the Flux-owned final Premult when the mask mode says preserve alpha.
  confidence: high
- file: `Gui/Gui05.cpp`
  symbol: layer mask wiring in `Gui::rebuildCompositingGraph`
  approximate lines: 2724-2828
  stable anchor: comment `// Chain: source -> [Unpremult] -> Roto -> Premult -> Merge A`
  reason: Change native and external layer-mask branches so output is `premultNode` when enabled and the mask output (`Roto` or `Shuffle`) when disabled; keep Unpremult logic unchanged.
  confidence: high
- file: `Gui/Gui05.cpp`
  symbol: stale cleanup helpers
  approximate lines: 1982-2108 and 2914-2921
  stable anchor: `cleanupStaleInlineLayerMaskChain`
  reason: Ensure toggling from premult to preserve-alpha deactivates/disconnects stale Flux-owned Premult without removing native Roto/RotoPaint/user nodes.
  confidence: medium

## Allowed Edit Files
- `Gui/FluxTimeline.h`
- `Gui/FluxTimeline.cpp`
- `Gui/FluxTimelineSerialization.h`
- `Gui/Gui05.cpp`

## Read-Only Context Files
- `ARCHITECTURE.md`
- `plans/PHASES.md`
- `tasks/TASKS.md`
- `/home/npittas/.pi/agent/COLLABORATION.md`
- `/home/npittas/Flux/AGENTS.md`

## Required Change
1. Add a per-`FluxMask` boolean for final-premult behavior. Recommended polarity: `bool premultiplyAlpha;` default `true` in constructors/deserialization defaults, because existing behavior is always Premult.
2. Persist the field in `FluxMaskSerialization`:
   - Add the boolean with default `true`.
   - Serialize it under a clear NVP such as `PremultiplyAlpha` only for the bumped mask serialization version.
   - Bump `BOOST_CLASS_VERSION(NATRON_NAMESPACE::FluxMaskSerialization, 2)` to `3`.
   - Older versions must load as `true`.
3. Wire model copy paths:
   - Duplicate/copy masks must copy the new flag.
   - New layer/effect masks must default true. The graph behavior requirement applies only to layer masks (`effectIndex < 0`); do not alter effect-mask wiring unless the field is simply serialized/copied harmlessly.
4. Add a minimal timeline UI/control surface:
   - In the mask-row context menu, add a checkable action `Premultiply Mask Alpha`.
   - It must be enabled only when the parent layer is not locked.
   - Checked means current/default final Premult behavior; unchecked means preserve alpha in flow / skip final Premult.
   - Triggering it updates that mask field, keeps the same mask selected, refreshes visible rows if necessary, emits `masksChanged(layerIndex)` and `compositingChanged()`.
   - If there is already a nearby naming convention for menu state labels, follow it; otherwise keep the exact label above.
5. Change only layer-mask graph wiring in `Gui05.cpp`:
   - Native Roto/RotoPaint layer masks: current chain is `source -> [Unpremult] -> Roto -> Premult -> Merge A`. If `premultiplyAlpha == false`, make the layer output be the mask node output (`Roto`/existing mask node) instead of creating/using the final Premult. Do not disconnect or replace the Roto/RotoPaint node except normal Flux wiring of its input 0.
   - External/AI raster layer masks: current chain is `source -> [Unpremult] + external Read/Reformat -> Shuffle -> Premult -> Merge A`. If `premultiplyAlpha == false`, make layer output be the `Shuffle` output and skip the final Premult.
   - When switching to preserve-alpha, deactivate/reset only the Flux-owned final Premult stored in `layer.maskApplyNode` if it has label `kFluxLayerMaskPremultLabel`; do not deactivate Roto/RotoPaint, external Read/Reformat/Shuffle, Unpremult, user/manual inserted nodes, or effect-mask nodes.
   - When switching back to premult, recreate/reuse `ensureInlineLayerMaskPremultNode` and restore current behavior.
   - Update nearby comments so they describe `[Premult if enabled]`, not an unconditional Premult.
6. Do not hijack RotoPaint: if a mask node is already a user/native RotoPaint or existing mask node, preserve it. Any cleanup must match exact Flux-owned labels before deactivation.

## Non-Goals
- Do not add new mask blending/compositing modes beyond the final Premult on/off flag.
- Do not change effect-mask semantics or effect mask inputs.
- Do not change Roto/RotoPaint drawing tools, plugin IDs, or properties panels.
- Do not alter project/task status files, phase plans, build config, lockfiles, generated files, or unrelated UI panels.
- Do not make preserve-alpha the default.

## Validation
Commands:
- `cmake --build <existing-build-dir> --target NatronGui -j$(nproc)`
- If no known build dir exists, run the repository's established build command for the GUI target and report the exact command used.

Manual checks:
- Create/open a project with a footage/solid/text layer and add a native layer mask. Confirm the mask-row context menu has `Premultiply Mask Alpha` checked by default.
- With checked/default mode, rebuild/inspect node graph: layer-mask branch still includes `Flux Layer Mask Premult` after Roto/Shuffle.
- Uncheck the action; confirm node graph routes Merge A from Roto/Shuffle output without the final `Flux Layer Mask Premult`, and existing Roto/RotoPaint drawing remains editable.
- Apply an AI raster mask to a layer mask; repeat checked and unchecked inspections for `Shuffle -> Premult` versus `Shuffle` output.
- Save, close/reopen, and confirm the per-mask checked/unchecked state and graph wiring persist.

Expected result:
- Build succeeds. Existing projects and new masks default to premultiplied behavior. Users can opt out per layer mask from the mask-row context menu, and save/reopen preserves the choice.

## Stop Conditions
Stop and report if:
- target symbol is missing
- required fix exceeds allowed files
- validation cannot run
- existing architecture contradicts the requested change
- task requires product/design judgment not in packet
- graph wiring requires changing effect-mask behavior or hijacking/replacing RotoPaint nodes
- serialization cannot be versioned backward-compatibly with old mask data defaulting to premult
- stale Premult cleanup cannot be limited to exact Flux-owned labels

## Planner Self-Check
- locator evidence sufficient: yes; run ebc984c1 plus authorized reads identify FluxMask, serialization v2, context menu, and unconditional Gui05 Premult wiring.
- allowed edit files minimal and explicit: yes; four requested files cover model, UI, serialization, and graph wiring.
- read-only context minimal: yes; only mandated project docs and authorized planning files were used.
- anchors/lines included: yes; each relevant location has path, symbol/anchor, approximate lines, reason, and confidence.
- validation concrete: yes; build target plus specific manual graph/save-reopen checks are listed.
- parallelization decision explicit and safe: yes; single task because model/serialization/UI/graph wiring share `FluxMask` state and must be validated together.
- non-goals and stop conditions sufficient: yes; they prevent default inversion, effect-mask expansion, RotoPaint hijack, and broad repo edits.
- reviewer findings addressed, if revision: not applicable; no prior reviewer findings supplied for this plan.

## Required Return Contract
Return only a task-focused summary. Do not include transcript, tool logs, raw file dumps, large code blocks, or broad unrelated issues. Include status, files inspected/changed, validation evidence, blockers, and task-specific risks.
