# Planner Report

## Status
ready

## Rationale
This plan is scoped to unwiring the AI-generated external layer-mask path that contradicts Nick's source-of-truth, while preserving native Roto/RotoPaint mask behavior and its existing Premultiply checkbox. The locator evidence is specific and sufficient: the implementation touches only the AI Apply call path, the Flux timeline mask model/menu/serialization, and the graph rebuild helpers that create or clean Flux-owned external layer-mask nodes.

# Task Packet

## User Goal
Recover from the incorrect AI layer-mask integration. Nick's source-of-truth: layer masks are for native Roto/RotoPaint application only; AI Apply must not create or use a FluxMask/Roto/Premult layer-mask graph. Remove timeline Premultiply Mask Alpha context/menu/serialization introduced for this external AI layer-mask path. Preserve native Roto/RotoPaint and its existing Premultiply checkbox. Add/Replace Mask will later be visible copy/shuffle effect rows with `ai_maskN` channels, not this task.

## Mode
general-coding

## Relevant Locations
- file: `Gui/FluxAiPanel.cpp`
  symbol: `FluxAiPanel::onApplyClicked`
  approximate lines: 1247-1274
  stable anchor: `timeline->applyExternalMaskToSelectedRow(relativeMask, &message)`
  reason: AI Apply currently calls into timeline external mask application; this must stop creating/using FluxMask layer-mask graph.
  confidence: high
- file: `Gui/FluxTimeline.h`
  symbol: `FluxMask`, `FluxTimeline::applyExternalMaskToSelectedRow`
  approximate lines: 103-121, 258-260
  stable anchor: `externalMaskPathProjectRelative`, `externalReadNode`, `externalShuffleNode`, `premultiplyAlpha`, `applyExternalMaskToSelectedRow`
  reason: External AI mask state/API and timeline premultiply state live here and should be removed or made non-operative as specified.
  confidence: high
- file: `Gui/FluxTimeline.cpp`
  symbol: `FluxTimeline::applyExternalMaskToSelectedRow`, mask context menu, timeline serialization/restore
  approximate lines: 1249-1321, 3677-3693, 4590-4607, 4730-4755
  stable anchor: `Premultiply Mask Alpha`, `maskSer.premultiplyAlpha`, `maskSer.externalMaskPathProjectRelative`
  reason: Implements AI external mask application, Premultiply Mask Alpha timeline context action, and persistence/restore of the unwanted state.
  confidence: high
- file: `Gui/FluxTimelineSerialization.h`
  symbol: `FluxMaskSerialization::serialize`
  approximate lines: 63-103
  stable anchor: `PremultiplyAlpha`, `ExternalMaskPathProjectRelative`, `ExternalReadNode`, `ExternalShuffleNode`
  reason: Serializes timeline premultiply/external AI mask fields introduced for this path; writes must be removed and legacy load must be ignored safely.
  confidence: high
- file: `Gui/Gui05.cpp`
  symbol: `ensureExternalMaskRead`, `ensureExternalMaskReformat`, `ensureLayerExternalMaskShuffle`, `cleanupStaleInlineLayerMaskChain`, rebuild branches keyed by external mask path
  approximate lines: 1753-1825, 2027-2160 and nearby rebuild layer-mask branch sites
  stable anchor: `mask.externalMaskPathProjectRelative`, `kFluxLayerExternalMaskShuffleLabel`, `cleanupStaleInlineLayerMaskChain`
  reason: Creates external Read/Reformat/Shuffle layer-mask graph and contains existing narrow stale cleanup logic that must be retained/adapted for recovery.
  confidence: high

## Allowed Edit Files
- `Gui/FluxAiPanel.cpp`
- `Gui/FluxTimeline.h`
- `Gui/FluxTimeline.cpp`
- `Gui/FluxTimelineSerialization.h`
- `Gui/Gui05.cpp`

## Read-Only Context Files
- `/home/npittas/.pi/agent/COLLABORATION.md`
- `AGENTS.md`
- `ARCHITECTURE.md`
- `plans/PHASES.md`
- `tasks/TASKS.md`

## Required Change
1. In `Gui/FluxAiPanel.cpp`, change `FluxAiPanel::onApplyClicked` so it no longer calls `FluxTimeline::applyExternalMaskToSelectedRow` and no longer reports success for layer-mask application. It should keep result selection/path validation as needed, then return a clear non-success message/log that AI mask application is disabled/deferred until the future `ai_maskN` visible copy/shuffle effect-row workflow exists. Do not create timeline masks, Roto nodes, Premult nodes, Read/Reformat/Shuffle nodes, or serialized layer-mask state from AI Apply.
2. In `Gui/FluxTimeline.h/.cpp`, remove the external AI mask API and state from the active model:
   - remove `FluxTimeline::applyExternalMaskToSelectedRow` declaration/definition and any now-unused path safety helper used only by it;
   - remove `FluxMask::externalMaskPathProjectRelative`, `externalReadNode`, and `externalShuffleNode` from active model state;
   - remove timeline-owned `premultiplyAlpha` from `FluxMask` only if it is the timeline layer-mask Premultiply Mask Alpha state introduced for this path. Do not touch native Roto/RotoPaint plugin parameters or their existing Premultiply checkbox.
3. In `Gui/FluxTimeline.cpp`, remove the timeline context menu action labelled `Premultiply Mask Alpha` and the associated writes to `mask.premultiplyAlpha`. Leave native Roto/RotoPaint node settings and UI untouched.
4. In `Gui/FluxTimeline.cpp` and `Gui/FluxTimelineSerialization.h`, stop writing/restoring the unwanted timeline serialization fields (`PremultiplyAlpha`, `ExternalMaskPathProjectRelative`, `ExternalReadNode`, `ExternalShuffleNode`) into the live Flux mask model. If Boost/XML compatibility requires keeping deprecated fields in `FluxMaskSerialization` to consume old projects, keep them clearly marked as legacy-load-only and ignore them on restore; do not populate live `FluxMask` state or emit them for newly saved projects if avoidable under the existing serializer constraints.
5. In `Gui/Gui05.cpp`, remove graph creation/rebuild branches keyed by `mask.externalMaskPathProjectRelative` and delete now-unused helpers `ensureExternalMaskRead`, `ensureExternalMaskReformat` if only used for external AI layer masks, and `ensureLayerExternalMaskShuffle` if only used for external AI layer masks. Preserve native Roto/RotoPaint mask graph creation/application.
6. Load-ignore/stale cleanup strategy:
   - On project load, ignore legacy external AI mask fields; do not restore external Read/Reformat/Shuffle node refs and do not create mask rows solely because those fields existed.
   - During rebuild, keep or adapt a narrow cleanup pass that detects only Flux-owned stale inline/external layer-mask chains by exact Flux labels (`kFluxLayerExternalMaskShuffleLabel`, `kFluxExternalMaskReformatLabel`, `kFluxExternalMaskReadLabel`, and the Flux-owned inline mask Premult/Unpremult/Roto labels already used by cleanup), verifies the chain reaches the expected layer source before touching it, reconnects Merge input 1 back to the expected source, deactivates only those Flux-owned stale nodes, and clears only stale model refs that still exist.
   - Do not deactivate user-created Roto/RotoPaint nodes, native masks, or arbitrary Read/Reformat/Shuffle nodes. If cleanup cannot prove Flux ownership and expected-source reachability, leave the graph alone and report the risk.
7. Remove or resolve any compile errors caused by deleted members/functions. Do not implement the future Add/Replace Mask `ai_maskN` copy/shuffle effect-row workflow in this task.

## Non-Goals
- Do not remove native Roto/RotoPaint functionality or its existing Premultiply checkbox/parameter.
- Do not implement future Add/Replace Mask UI, copy/shuffle rows, `ai_maskN` channels, or effect-row masking.
- Do not alter unrelated timeline effects, layer ordering, project bin, viewer, export, or general serialization behavior.
- Do not perform git commits, staging, resets, or history changes.

## Validation
Commands:
- `cmake --build build --target NatronGui -j$(nproc)`
- `grep -R "applyExternalMaskToSelectedRow\|Premultiply Mask Alpha\|externalMaskPathProjectRelative\|ExternalMaskPathProjectRelative\|externalReadNode\|externalShuffleNode" Gui/FluxAiPanel.cpp Gui/FluxTimeline.h Gui/FluxTimeline.cpp Gui/FluxTimelineSerialization.h Gui/Gui05.cpp`

Expected result:
- Build passes.
- Grep has no active references to the removed AI external layer-mask API/state/menu/serialization, except explicitly documented legacy-load-only compatibility fields if they are required and ignored on restore.
- Manual smoke check if GUI can be launched: AI Apply does not add a Flux mask row or create Flux-owned Roto/Premult/Read/Reformat/Shuffle layer-mask nodes; native Roto/RotoPaint mask behavior and its own Premultiply checkbox remain available.

## Stop Conditions
Stop and report if:
- target symbol is missing
- required fix exceeds allowed files
- validation cannot run
- existing architecture contradicts the requested change
- task requires product/design judgment not in packet
- removing serialization fields would break old-project loading and no safe legacy-ignore strategy is possible within the allowed files
- distinguishing native Roto/RotoPaint premultiply from timeline-owned `premultiplyAlpha` is ambiguous in code
- stale cleanup would need to touch non-Flux-owned/user nodes to work

## Planner Self-Check
- locator evidence sufficient: yes — user supplied exact locator evidence and spot reads confirmed all named anchors.
- allowed edit files minimal and explicit: yes — five explicit files cover the AI Apply call, timeline model/menu/serialization, and graph helpers/rebuild cleanup.
- read-only context minimal: yes — only required Pi/project rules and task/architecture status files were read.
- anchors/lines included: yes — each relevant location has path, symbol, approximate lines, stable anchor, reason, and confidence.
- validation concrete: yes — build target, grep check, and focused GUI/manual behavior check are specified.
- parallelization decision explicit and safe: yes — single task; the changes are tightly coupled across API, model, serialization, and rebuild cleanup, so parallel edits would risk conflicting state changes.
- non-goals and stop conditions sufficient: yes — they preserve native Roto/RotoPaint and block future `ai_maskN` implementation/scope creep.
- reviewer findings addressed, if revision: not applicable — no reviewer findings supplied for this planning pass.

## Required Return Contract
Return only a task-focused summary. Do not include transcript, tool logs, raw file dumps, large code blocks, or broad unrelated issues. Include status, files inspected/changed, validation evidence, blockers, and task-specific risks.
