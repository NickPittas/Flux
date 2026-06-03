# Planner Report

## Status
ready

## Rationale
This is a narrow technical spike because the old Apply-to-layer-mask/Roto/Premult path is intentionally disabled and known wrong. The smallest safe unit is to prove or disprove Natron/OpenFX Shuffle multi-plane behavior in the existing Flux AI apply entry point, then only implement Add Mask / Replace Mask through visible Shuffle rows if the proof artifact shows Color/RGBA is preserved while a new `ai_maskN.RGBA` plane is written from the selected mask input.

# Task Packet

## User Goal
Recover AI mask application so Nick can test working **Add Mask** and **Replace Mask** behavior. Do not revive the old Apply-to-layer-mask/Roto/Premult path. First prove whether stock Shuffle plus `Node::addUserComponents` can preserve the layer Color/RGBA plane while adding/updating `ai_maskN.RGBA` from the SAM mask image on input B. If it cannot be proven, stop and report that Flux needs a small Flux-owned multi-plane copy node.

## Mode
general-coding

## Relevant Locations
- file: `Gui/FluxAiPanel.cpp`
  symbol: `FluxAiPanel::onApplyClicked()`
  approximate lines: 1247-1261
  stable anchor: `AI mask apply is disabled for layer masks. Result kept at`
  reason: Current Apply path is intentionally disabled; this is the entry point to replace with spike-gated Add/Replace wiring or a temporary spike command/report.
  confidence: high
- file: `Gui/FluxAiPanel.h`
  symbol: `FluxAiPanel::onApplyClicked`, `_applyButton`
  approximate lines: 49-50, 120-123
  stable anchor: `void onApplyClicked();`
  reason: Declares the current UI action and button state that may be renamed/split for Add Mask / Replace Mask during the proven path.
  confidence: high
- file: `Gui/Gui05.cpp`
  symbol: Flux timeline/node wiring setup and mask helpers
  approximate lines: 1558-1588, 1716-1803
  stable anchor: `maskSelected`, `ensureMaskSourceNodes`, `kMaskColumnOffsetX`
  reason: Existing Flux graph wiring patterns, mask model integration, and visible node layout conventions for adding visible Shuffle rows in the layer branch.
  confidence: medium
- file: `Gui/FluxTimeline.h`
  symbol: `FluxMask`, `FluxLayer`, `addLayerMask`, `masksChanged`
  approximate lines: 101-150, 245-296
  stable anchor: `struct FluxMask`, `QList<FluxMask> masks`, `bool addLayerMask(int layerIndex);`
  reason: Existing mask model is Roto-oriented; spike must avoid corrupting it unless the proven Shuffle path needs explicit ai-mask model fields.
  confidence: medium
- file: `Gui/FluxTimeline.cpp`
  symbol: mask add/remove and duplicate node collection
  approximate lines: 275-288, 309-320, 490-495, 640-644
  stable anchor: `addMaskToSelectedRow`, `maskApplyNode`, `classifyLayerBranches(layer)`
  reason: Shows existing lifecycle/deactivation/duplication expectations for mask-related nodes.
  confidence: medium
- file: `Engine/Node.h`
  symbol: `Node::addUserComponents`, `Node::getUserCreatedComponents`, `Node::getSelectedLayer`
  approximate lines: 1267-1275
  stable anchor: `bool addUserComponents(const ImagePlaneDesc& comps);`
  reason: Public API candidate for creating/selecting user planes such as `ai_mask1.RGBA`.
  confidence: high
- file: `Engine/Node.cpp`
  symbol: `Node::addUserComponents`
  approximate lines: 7224-7259
  stable anchor: `///The node has node channel selector, don't allow adding a custom plane.`
  reason: Confirms the API only works on nodes with output channel selectors and sets the output selector to the created plane.
  confidence: high
- file: `Engine/ImagePlaneDesc.h`
  symbol: `ImagePlaneDesc` constructors and OFX plane/component mapping helpers
  approximate lines: 81-90, 170-199
  stable anchor: `Or any plane encoded in the format specified by the Natron multi-plane extension.`
  reason: Defines how to construct/map custom multi-plane descriptors for `ai_maskN.RGBA`.
  confidence: high
- file: `Documentation/source/plugins/net.sf.openfx.ShufflePlugin.rst`
  symbol: Shuffle plugin parameters
  approximate lines: 23-105
  stable anchor: `Output Layer / ``outputLayer```, `R / ``outputR```, `Set GBA From R / ``setGBAFromR```
  reason: Stock Shuffle has A/B inputs and output channel mapping choices, but docs do not prove Color preservation when outputting a custom layer.
  confidence: high

## Allowed Edit Files
- `Gui/FluxAiPanel.cpp`
- `Gui/FluxAiPanel.h`
- `Gui/FluxTimeline.cpp`
- `Gui/FluxTimeline.h`
- `Gui/Gui05.cpp`
- `Gui/FluxTimelineSerialization.h`

## Read-Only Context Files
- `Engine/Node.h`
- `Engine/Node.cpp`
- `Engine/ImagePlaneDesc.h`
- `Engine/EffectInstance.cpp`
- `Engine/EffectInstance.h`
- `Engine/EffectInstanceRenderRoI.cpp`
- `Documentation/source/plugins/net.sf.openfx.ShufflePlugin.rst`
- `ARCHITECTURE.md`
- `plans/PHASES.md`
- `tasks/TASKS.md`

## Required Change
1. Do not re-enable or reuse the old Apply-to-layer-mask/Roto/Premult behavior. Treat it as a non-goal and leave it disabled unless replacing the UI action with Add/Replace after proof.
2. Add the smallest possible technical-spike path reachable from the current AI mask result state that creates a controlled test graph using the selected SAM mask as input B and the selected/source layer branch as input A.
3. In that spike path, create/configure a visible stock Shuffle node (`net.sf.openfx.ShufflePlugin`) and call `Node::addUserComponents` with an `ImagePlaneDesc` for `ai_mask1.RGBA` (or the next available `ai_maskN.RGBA`). Configure Shuffle rows so B's mask channel populates all RGBA channels of `ai_maskN` while A Color/RGBA should pass through unchanged if Shuffle supports multi-plane preservation.
4. Produce a concrete pass/fail artifact in the project/log output, not only UI: record at minimum the created Shuffle node script name/label, created plane id/channels, source Color plane identity before/after, available output planes/components after refresh/render/metadata query, and whether Color/RGBA remains available downstream while `ai_maskN.RGBA` exists. Prefer writing a small project-relative JSON/text proof next to the SAM manifest if there is already a safe project-relative SAM output folder; otherwise append an unambiguous `FLUX AI MASK SPIKE PASS/FAIL` block to existing logs/stderr.
5. If the artifact proves stock Shuffle preserves Color/RGBA and adds/updates `ai_maskN.RGBA`, implement the narrow Add Mask / Replace Mask behavior with visible Shuffle rows:
   - Add Mask creates the next `ai_maskN` Shuffle row for the selected layer and leaves existing masks/planes intact.
   - Replace Mask updates the currently selected/last AI mask plane's Shuffle input/mapping to the new selected mask without creating a new plane.
   - Keep nodes visible and labeled clearly (for example `AI Mask Add ai_mask1` / `AI Mask Replace ai_mask1`).
6. If the proof fails, is inconclusive, or requires editing Engine/OpenFX/plugin internals, stop. Do not implement a half-working UI. Report that the next plan must be a Flux-owned multi-plane copy node, because CopyRectangle is not viable and Shuffle is unproven/failed.
7. Keep changes within the allowed GUI files. Do not edit Engine files, plugin source, serialization schema broadly, build files, lockfiles, tasks status, or plans status during this spike unless separately approved.

## Non-Goals
- Do not restore old Apply-to-layer-mask, Roto mask source, Premult, or hidden alpha-only workflows.
- Do not make CopyRectangle the solution.
- Do not add a Flux-owned multi-plane node in this task; only stop and report if needed.
- Do not redesign the AI panel/product workflow beyond enough labels/actions to expose Add Mask / Replace Mask after proof.
- Do not broadly refactor timeline masks, layer effects, serialization, or graph rebuilding.
- Do not mark T083 complete or update phase/task status.
- Do not commit, stage broad changes, or alter git history.

## Validation
Commands:
- `cmake --build <existing-build-dir> --target Natron -j$(nproc)`
- Launch Flux/Natron from the existing build, run SAM/AI mask on a real imported image or footage layer, then trigger the spike/Add Mask path.
- In the same session, inspect the node graph and generated/logged proof artifact for `FLUX AI MASK SPIKE PASS` or `FLUX AI MASK SPIKE FAIL`.
- If PASS and Add/Replace are implemented: create Add Mask twice and Replace Mask once, then save/reopen the project and verify visible Shuffle rows still exist and layer Color output still displays in viewer.

Expected result:
- Build succeeds.
- The spike emits a concrete pass/fail artifact. PASS must show both Color/RGBA preservation and `ai_maskN.RGBA` availability downstream from the Shuffle. FAIL must include the exact missing condition and must not claim Add/Replace is implemented.
- On PASS implementation, Add Mask creates visible Shuffle rows for new `ai_maskN` planes; Replace Mask rewires/updates the selected existing ai mask plane; viewer Color remains visually unchanged.

## Stop Conditions
Stop and report if:
- target symbol is missing
- required fix exceeds allowed files
- validation cannot run
- existing architecture contradicts the requested change
- task requires product/design judgment not in packet
- `Node::addUserComponents` returns false for Shuffle or cannot create/select `ai_maskN.RGBA`
- Shuffle cannot expose both preserved Color/RGBA and new/updated `ai_maskN.RGBA` in a concrete runtime artifact
- the implementation requires Engine/OpenFX/plugin source edits, a new plugin/node, migrations, generated files, or broad serialization changes
- the only proof available is UI appearance without metadata/render/log evidence

## Planner Self-Check
- locator evidence sufficient: yes — user supplied locator run 4f22a519 constraints, and inspected authorized anchors confirm Apply is disabled, Shuffle is a stock candidate, and `Node::addUserComponents` behavior.
- allowed edit files minimal and explicit: yes — limited to AI panel, timeline model/wiring, Gui05 integration, and existing Flux serialization header only.
- read-only context minimal: yes — Engine/API/plugin docs only; no broad source reads required for worker beyond listed files.
- anchors/lines included: yes — each relevant location includes path, symbol/anchor, approximate lines, reason, and confidence.
- validation concrete: yes — build plus runtime proof artifact with explicit pass/fail criteria and Add/Replace manual graph checks after PASS.
- parallelization decision explicit and safe: yes — single task; spike and implementation are sequential because implementation depends on runtime proof and touches shared AI/timeline graph files.
- non-goals and stop conditions sufficient: yes — old Apply/Roto/Premult, CopyRectangle, Engine/plugin edits, and unproven UI-only success are explicitly blocked.
- reviewer findings addressed, if revision: not applicable — no previous reviewer findings supplied.

## Required Return Contract
Return only a task-focused summary. Do not include transcript, tool logs, raw file dumps, large code blocks, or broad unrelated issues. Include status, files inspected/changed, validation evidence, blockers, and task-specific risks. The worker must explicitly state whether the spike result was PASS, FAIL, or NOT RUN, and attach/reference the concrete proof artifact path or exact log marker.
