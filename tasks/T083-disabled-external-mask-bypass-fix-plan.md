# Planner Report

## Status
ready

## Rationale
The blocker is localized to the layer-mask graph cleanup/preservation path in `Gui/Gui05.cpp`: external raster masks can terminate at `Flux Layer External Mask Shuffle` when `premultiplyAlpha=false`, while stale cleanup and preservation guards only recognize the inline Roto/Premult/Unpremult labels. A narrow fix can extend the existing Flux-owned stale-chain detection to external mask labels and ensure disabled layer masks reconnect Merge A to the current layer output without changing UX or mask creation behavior.

# Task Packet

## User Goal
Fix the combined Packet A/B blocker: disabled external/AI raster layer masks with `premultiplyAlpha=false` must not remain preserved in the layer Merge input. They must bypass/clean the external Read/Reformat/Shuffle chain and restore the layer output regardless of the premultiply toggle.

## Mode
general-coding

## Relevant Locations
- file: `Gui/Gui05.cpp`
  symbol: mask graph label constants (`kFluxExternalMaskReadLabel`, `kFluxExternalMaskReformatLabel`, `kFluxLayerExternalMaskShuffleLabel`)
  approximate lines: 1696-1703
  stable anchor: `static const char* kFluxLayerExternalMaskShuffleLabel = "Flux Layer External Mask Shuffle";`
  reason: External-mask Flux-owned labels already exist and should be included in stale cleanup/preservation detection.
  confidence: high
- file: `Gui/Gui05.cpp`
  symbol: `cleanupStaleInlineLayerMaskChain`
  approximate lines: 2021-2097
  stable anchor: `cleanupStaleInlineLayerMaskChain(FluxLayer& layer, const NodePtr& mergeNode, const NodePtr& expectedSource)`
  reason: Existing disabled/no-enabled-layer-mask cleanup is topology-based from Merge A but currently only collects Premult/Roto/Unpremult; extend it to recognize and deactivate external mask Shuffle/Reformat/Read when the chain traces back to the expected layer source.
  confidence: high
- file: `Gui/Gui05.cpp`
  symbol: `preserveInlineChainInput`
  approximate lines: 2195-2216
  stable anchor: `if (upstreamContainsNode(currentInput, expectedSource))`
  reason: Current preservation guard prevents stale Premult chains from being treated as manual inline chains; add the same Flux-owned mask-chain guard for external Shuffle/Reformat/Read labels so disabled external mask chains cannot be preserved.
  confidence: high
- file: `Gui/Gui05.cpp`
  symbol: external layer mask wiring inside `Gui::rebuildCompositingGraph`
  approximate lines: 2799-2838
  stable anchor: `ensureLayerExternalMaskShuffle(this, *lmask, collection,`
  reason: Confirms the `premultiplyAlpha=false` external path sets `layerOutput` directly to the Shuffle node and skips final Premult, causing the stale-chain signature missed by current cleanup.
  confidence: high
- file: `Gui/Gui05.cpp`
  symbol: stale cleanup and Merge A reconnect inside `Gui::rebuildCompositingGraph`
  approximate lines: 2974-3018
  stable anchor: `cleanupStaleInlineLayerMaskChain(layer, layer.mergeNode, layerOutput);`
  reason: The fix must ensure this no-enabled-mask cleanup runs before `preserveInlineChainInput(layer.mergeNode, 1, layerOutput)` can retain a stale external mask chain.
  confidence: high

## Allowed Edit Files
- `Gui/Gui05.cpp`

## Read-Only Context Files
- `/home/npittas/.pi/agent/COLLABORATION.md`
- `AGENTS.md`
- `ARCHITECTURE.md`
- `plans/PHASES.md`
- `tasks/TASKS.md`
- `/tmp/T083-toggle-premult-combined-review.md`

## Required Change
Implement a narrow graph-cleanup fix in `Gui/Gui05.cpp` only:

1. Extend stale Flux-owned layer-mask chain detection to include external-mask nodes with exact labels:
   - `Flux Layer External Mask Shuffle`
   - `Flux External Mask Reformat`
   - `Flux External Mask Read`
   Keep matching exact-label only; do not use broad substring detection.
2. In `cleanupStaleInlineLayerMaskChain`, treat an external Shuffle as sufficient evidence of a stale Flux-owned layer-mask chain, equivalent to current Premult/Roto evidence, but only clean it when the discovered chain still reaches `expectedSource` upstream.
3. When such a stale external chain is found, disconnect Merge input 1 and reconnect `expectedSource`, then deactivate the Flux-owned external Shuffle/Reformat/Read nodes discovered in that stale chain. Preserve the existing narrow behavior for inline Roto/Premult/Unpremult chains.
4. Reset any model references for deactivated external mask nodes on disabled layer-mask rows (`externalShuffleNode`, `reformatNode`, `externalReadNode`) when they point to inactive nodes. Do not delete or alter user/manual nodes.
5. Update `preserveInlineChainInput` so a current input chain that reaches `expectedSource` is not preserved if it contains any Flux-owned layer-mask node label, including external Shuffle/Reformat/Read as well as existing Premult/Roto/Unpremult. This prevents Merge A from preserving stale external mask chains when cleanup catches only part of the chain.
6. Do not change mask UI, serialization, premultiply toggle behavior, enabled-mask graph construction, project/task status files, or broader graph layout.

## Non-Goals
- Do not add new UX, menu actions, preferences, or serialization fields.
- Do not alter enabled mask behavior except where needed to keep existing enabled external masks wired.
- Do not refactor unrelated graph rebuild code or manual inline-chain preservation beyond Flux-owned layer-mask labels.
- Do not edit `FluxMaskUtils` unless implementation proves an already-existing label helper in that file is required; current plan expects `Gui/Gui05.cpp` only.
- Do not update task status or git state.

## Validation
Commands:
- `cmake --build /home/npittas/Flux/build --target Natron -j$(nproc)`

Targeted code review/manual inspection:
- Inspect the disabled external mask path in `Gui/Gui05.cpp` and verify: when no enabled layer mask exists, Merge input 1 is reconnected to `layerOutput`/`expectedSource`, and `preserveInlineChainInput` cannot preserve a chain containing `Flux Layer External Mask Shuffle`, `Flux External Mask Reformat`, or `Flux External Mask Read`.
- Inspect that enabled external masks still wire `Read -> Reformat -> Shuffle -> [Premult if premultiplyAlpha=true] -> Merge A`, and that `premultiplyAlpha=false` still omits Premult while enabled.

Expected result:
- Build passes.
- Targeted review confirms disabled external/AI raster masks bypass/clean stale external Read/Reformat/Shuffle chains regardless of `premultiplyAlpha`, while manual non-Flux inline chains remain preserved.

## Stop Conditions
Stop and report if:
- target symbol is missing
- required fix exceeds allowed files
- validation cannot run
- existing architecture contradicts the requested change
- task requires product/design judgment not in packet
- fixing the blocker requires changing mask UI/serialization semantics or broad graph rebuild behavior
- label-based cleanup would risk deactivating nodes not exact-labeled as Flux-owned mask nodes

## Planner Self-Check
- locator evidence sufficient: yes — blocker report plus anchored reads in `Gui/Gui05.cpp` identify exact labels, cleanup, preservation, and wiring sites.
- allowed edit files minimal and explicit: yes — only `Gui/Gui05.cpp` is needed for the localized graph cleanup/preservation fix.
- read-only context minimal: yes — only required project instructions/status files, the review report, and targeted `Gui/Gui05.cpp` windows were used.
- anchors/lines included: yes — each relevant location includes approximate lines and stable anchors.
- validation concrete: yes — build command plus targeted code-review checks for disabled/enabled external mask paths.
- parallelization decision explicit and safe: yes — single task; no parallel split because all changes touch the same cleanup/preservation logic in one file.
- non-goals and stop conditions sufficient: yes — explicitly prevents UX, serialization, task-status, git, and broad graph refactors.
- reviewer findings addressed, if revision: not applicable — this is a new narrow fix plan based on the combined Packet A/B blocker report.

## Required Return Contract
Return only a task-focused summary. Do not include transcript, tool logs, raw file dumps, large code blocks, or broad unrelated issues. Include status, files inspected/changed, validation evidence, blockers, and task-specific risks.
