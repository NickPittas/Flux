# Planner Report

## Status
ready

## Rationale
The reviewer blocker is localized to the external layer-mask Shuffle setup in `Gui/Gui05.cpp`: `ensureLayerExternalMaskShuffle()` currently prefers `B.Color.A` for `outputA`, which is unsafe for SAM3/external grayscale or RGB mask PNGs. A single-file edit can enforce the T083 mask contract by preferring mask luminance/red (`B.Color.R`) without expanding Packet F or touching unrelated AI/UI code.

# Task Packet

## User Goal
Fix the T083 Apply implementation blocker so SAM3/external AI mask layer alpha replacement does not default to the mask image alpha channel. External AI mask alpha replacement must use the mask luminance/red channel by contract, or otherwise inspect channel contract safely; minimal requested fix is to choose `B.Color.R` before `B.Color.A`.

## Mode
general-coding

## Relevant Locations
- file: `Gui/Gui05.cpp`
  symbol: `setChoiceByIdOrLabel(const NodePtr& node, const char* knobName, const QStringList& accepted)`
  approximate lines: 1705-1727
  stable anchor: `static bool setChoiceByIdOrLabel`
  reason: Helper picks the first matching Shuffle choice from the ordered accepted list, so caller order controls whether `B.Color.R` or `B.Color.A` is selected.
  confidence: high
- file: `Gui/Gui05.cpp`
  symbol: `ensureLayerExternalMaskShuffle(Gui* gui, FluxMask& mask, const NodeCollectionPtr& collection, double x, double y)`
  approximate lines: 1788-1813
  stable anchor: `kFluxLayerExternalMaskShuffleLabel` and `outputA`
  reason: Creates/configures the external layer-mask Shuffle and currently passes `"B.Color.A"` before `"B.Color.R"` for `outputA`, causing opaque/incorrect masks for grayscale/RGB PNGs when alpha exists.
  confidence: high
- file: `Gui/Gui05.cpp`
  symbol: external mask branch wiring inside compositing graph rebuild
  approximate lines: 2748-2770
  stable anchor: `externalMaskPathProjectRelative` and `ensureLayerExternalMaskShuffle`
  reason: Confirms this Shuffle is used only for external layer masks in the Apply path and wires the external mask branch into alpha replacement.
  confidence: high

## Allowed Edit Files
- `Gui/Gui05.cpp`

## Read-Only Context Files
- `/home/npittas/.pi/agent/COLLABORATION.md`
- `AGENTS.md`
- `ARCHITECTURE.md`
- `plans/PHASES.md`
- `tasks/TASKS.md`

## Required Change
In `Gui/Gui05.cpp`, update the external layer mask Shuffle configuration so `outputA` selects `B.Color.R` as the preferred source for the replacement alpha, with `B.Color.A` only as an optional fallback if the red/luminance choice is unavailable. The minimal expected change is to reorder the `QStringList` passed to `setChoiceByIdOrLabel(mask.externalShuffleNode, "outputA", ...)` from `B.Color.A`, `B.Color.R` to `B.Color.R`, `B.Color.A`.

Add a short nearby comment if useful to document the contract: SAM3/external AI masks are grayscale/RGB raster masks, so alpha replacement must consume mask luminance/red and must not prefer the mask file alpha channel.

Do not add channel-probing logic unless it is already locally available and can be implemented safely inside `Gui/Gui05.cpp` without broad scope. If implementing probing would require new APIs/files/product decisions, use the minimal ordered-choice fix instead.

## Non-Goals
- Do not expand Packet F or change broader T083 AI panel/model-manager/apply workflow.
- Do not edit serialization, timeline model, AI worker code, or mask data structures.
- Do not change internal Roto/RotoPaint mask behavior.
- Do not alter Shuffle output RGB mappings except as necessary for this alpha-source contract.
- Do not add new generated assets, migrations, lockfile changes, or broad refactors.

## Validation
Commands:
- `grep -n "outputA" Gui/Gui05.cpp`
- `cmake --build build --target Natron -j$(nproc)`

Expected result:
- The inspected `outputA` configuration for `ensureLayerExternalMaskShuffle()` lists `B.Color.R` before `B.Color.A`.
- The Natron/Flux GUI target builds successfully. If the local build directory or target name differs, stop and report the unavailable command rather than guessing a destructive setup step.

Manual check if GUI validation is available:
- Apply an external/SAM3 grayscale or RGB PNG mask to a layer and confirm the Shuffle node's `outputA` is set to `B.Color.R` and the resulting mask follows image luminance/red instead of opaque PNG alpha.

## Stop Conditions
Stop and report if:
- target symbol is missing
- required fix exceeds allowed files
- validation cannot run
- existing architecture contradicts the requested change
- task requires product/design judgment not in packet
- the local Shuffle plugin does not expose `B.Color.R` or equivalent red/luminance choice for `outputA`
- the fix appears to affect non-external masks or non-layer-mask workflows

## Planner Self-Check
- locator evidence sufficient: yes — user supplied the file and blocker; local anchors confirm the exact `outputA` order and external mask wiring.
- allowed edit files minimal and explicit: yes — one allowed source file, `Gui/Gui05.cpp`.
- read-only context minimal: yes — only mandated project guidance/status files plus anchored `Gui/Gui05.cpp` evidence were used.
- anchors/lines included: yes — relevant symbols, approximate lines, stable anchors, reasons, and confidence are listed.
- validation concrete: yes — source inspection plus build command, with a manual GUI check when available.
- parallelization decision explicit and safe: yes — single task; no parallelization because there is one shared edit file and one coherent blocker fix.
- non-goals and stop conditions sufficient: yes — scope excludes Packet F expansion and unrelated AI/mask architecture changes.
- reviewer findings addressed, if revision: yes — the reviewer blocker is directly addressed by preferring `B.Color.R` over `B.Color.A` for external AI mask alpha replacement.

## Required Return Contract
Return only a task-focused summary. Do not include transcript, tool logs, raw file dumps, large code blocks, or broad unrelated issues. Include status, files inspected/changed, validation evidence, blockers, and task-specific risks.
