# Planner Report

## Status
ready

## Rationale
The reviewer blocker is localized to `FluxAIMaskCopy`: the node currently advertises only standard RGBA/RGB/Alpha components and, during render, reuses the first input-A image for every non-target output plane. A narrow Engine-only fix can preserve upstream custom planes by matching each requested output plane to the same plane from input A, while writing only input-B RGBA into the selected `ai_maskN` target plane.

# Task Packet

## User Goal
Fix T083 `FluxAIMaskCopy` so adding a second AI mask plane (for example `ai_mask2`) leaves an existing upstream plane (for example `ai_mask1`) unchanged. The node must pass through every non-target upstream plane from input A and only write/copy input B RGBA into its selected target `ai_maskN.RGBA` plane.

## Mode
general-coding

## Relevant Locations
- file: `Engine/FluxAIMaskCopy.cpp`
  symbol: `FluxAIMaskCopy::addAcceptedComponents`
  approximate lines: 45-50
  stable anchor: `void FluxAIMaskCopy::addAcceptedComponents`
  reason: currently advertises only RGBA/RGB/Alpha, which can prevent custom upstream `ai_maskN` planes from being accepted/requested through chained `FluxAIMaskCopy` nodes.
  confidence: high
- file: `Engine/FluxAIMaskCopy.cpp`
  symbol: `FluxAIMaskCopy::render`
  approximate lines: 71-111
  stable anchor: `for (std::list<std::pair<ImagePlaneDesc, ImagePtr> >::const_iterator it = args.outputPlanes.begin()`
  reason: currently uses `args.inputImages[0].front()` as the source for all non-target planes, so non-target custom planes can be overwritten with the first input-A plane instead of copied plane-for-plane.
  confidence: high
- file: `Engine/FluxAIMaskCopy.h`
  symbol: `class FluxAIMaskCopy`
  approximate lines: 17-41
  stable anchor: `virtual void addAcceptedComponents(int inputNb, std::list<ImagePlaneDesc>* comps) OVERRIDE FINAL;`
  reason: allowed only if the implementation needs a helper/declaration adjustment; keep unchanged if the fix fits entirely in the `.cpp`.
  confidence: medium
- file: `Engine/DiskCacheNode.cpp`
  symbol: `DiskCacheNode::render`
  approximate lines: 213-235
  stable anchor: `getImage(0, args.time, args.originalScale, args.view, NULL, &it->first`
  reason: existing Engine pattern for pass-through rendering by fetching the matching requested output plane from input 0 instead of reusing an arbitrary first input image.
  confidence: high

## Allowed Edit Files
- `Engine/FluxAIMaskCopy.cpp`
- `Engine/FluxAIMaskCopy.h`

## Read-Only Context Files
- `/home/npittas/.pi/agent/COLLABORATION.md`
- `/home/npittas/Flux/AGENTS.md`
- `ARCHITECTURE.md`
- `plans/PHASES.md`
- `tasks/TASKS.md`
- `Engine/DiskCacheNode.cpp`

## Required Change
1. In `Engine/FluxAIMaskCopy.cpp`, change non-target pass-through logic so each output plane is sourced from the matching plane on input A, not from the first input-A image.
   - Prefer the existing Engine pattern used by `DiskCacheNode::render`: for each non-target `outComps`, fetch `getImage(0, args.time, args.originalScale, args.view, NULL, &outComps, false, true, eStorageModeRAM, 0, &roiPixel)` or the exact local signature required by this checkout.
   - If using `args.inputImages` instead, explicitly find an input-A image whose `getComponents().getPlaneID()` matches `outComps.getPlaneID()`; do not use `.front()` except as a same-plane match.
   - Copy/convert that matching input-A plane into the same output plane. If missing, fail or zero only that plane according to surrounding Engine pass-through conventions; do not substitute RGBA for custom planes.
2. Keep target-plane behavior narrow: only when `outComps.getPlaneID() == target`, copy/convert input B into the target `ai_maskN.RGBA` output.
   - Input B should remain the AI mask source; do not read input A for the target plane unless B is missing and existing behavior requires zero/failure.
   - Do not modify any non-target plane while rendering the target.
3. Update component advertisement so chained AI mask planes can flow through.
   - At minimum, ensure input/output accepted components include the selected target `ai_maskN` plane and do not block upstream custom `ai_maskN` planes already present on input A.
   - If Natron’s API requires user-created components for custom planes, use the existing `makeAIMaskPlane()`/`Node::addUserComponents()` pathway narrowly; do not introduce GUI metadata changes unless the target plane selector is proven to be the blocker.
4. Preserve current validation of target names via `isValidAIMaskPlane()` and the existing fallback to `ai_mask1`.
5. Keep the change limited to `FluxAIMaskCopy`; no broad refactor, no AI-panel workflow changes, no timeline/mask model changes unless the implementation hits a stop condition.

## Non-Goals
- Do not redesign AI mask UI, result history, AI worker, SAM/MatAnyone/DepthCrafter flows, or Flux mask rows.
- Do not change Roto/Premult/mask-apply graph behavior.
- Do not change serialization or project file schema.
- Do not alter CMake/build files unless the existing source file is not compiled, which should be reported as a stop condition.
- Do not add broad custom-plane infrastructure beyond what `FluxAIMaskCopy` needs to preserve non-target planes.

## Validation
Commands:
- `cmake --build /home/npittas/Flux/build --target NatronEngine -j$(nproc)`
- `cmake --build /home/npittas/Flux/build --target Natron -j$(nproc)`
- If the local build directory/targets differ, run the established equivalent Engine and GUI build targets and report the exact commands used.

Manual/render checks:
- In Flux/Natron, create or open a saved project with a footage layer and an existing `FluxAIMaskCopy` writing B.RGBA into `ai_mask1.RGBA`; verify `ai_mask1` is present and non-empty.
- Chain a second `FluxAIMaskCopy` after the first, set its target to `ai_mask2`, feed a different B.RGBA mask, render/view/export a frame, and verify:
  - `ai_mask1.RGBA` after the second node is pixel-identical to `ai_mask1.RGBA` before the second node.
  - `ai_mask2.RGBA` contains the second B.RGBA mask.
  - the normal color/RGBA layer stream from input A remains unchanged.
- If a command-line render/export path exists for this project, render the same frame before and after inserting the second node and inspect the custom planes in EXR or the available plane viewer. If no automated plane-diff tool is available, document the manual viewer/EXR inspection and why automated comparison was unavailable.

Expected result:
- Builds pass.
- Chained `FluxAIMaskCopy` nodes preserve prior non-target `ai_maskN` planes while only writing the selected target plane.

## Stop Conditions
Stop and report if:
- `FluxAIMaskCopy::addAcceptedComponents` or `FluxAIMaskCopy::render` anchors are missing or substantially different.
- Preserving custom planes requires changes outside `Engine/FluxAIMaskCopy.{h,cpp}`.
- Natron’s API does not allow fetching/copying a matching input-A custom plane from within this node without broader metadata/channel-selector work.
- The target plane selection UI/metadata is proven to require GUI changes.
- Build or manual validation cannot run in the local environment.
- Existing Engine architecture contradicts the requested pass-through behavior.
- The task requires product/design judgment beyond preserving non-target upstream planes and writing only the selected AI mask target.

## Planner Self-Check
- locator evidence sufficient: yes — reviewer identified exact blocker and local inspection confirmed the relevant `addAcceptedComponents` and `render` anchors.
- allowed edit files minimal and explicit: yes — limited to `Engine/FluxAIMaskCopy.cpp` with `Engine/FluxAIMaskCopy.h` allowed only for helper/declaration needs.
- read-only context minimal: yes — only required project instructions/task status plus one Engine pass-through pattern file.
- anchors/lines included: yes — relevant files, symbols, approximate lines, reasons, and confidence are listed.
- validation concrete: yes — Engine/GUI build commands plus chained `ai_mask1`/`ai_mask2` render/manual plane-preservation checks.
- parallelization decision explicit and safe: yes — single task; both fixes affect the same node/render path and must be implemented together to avoid partial behavior.
- non-goals and stop conditions sufficient: yes — prevents AI workflow, GUI, serialization, graph, and broad custom-plane scope creep.
- reviewer findings addressed, if revision: yes — plan directly addresses both reviewer blockers: accepted components and first-input-image non-target pass-through.

## Required Return Contract
Return only a task-focused summary. Do not include transcript, tool logs, raw file dumps, large code blocks, or broad unrelated issues. Include status, files inspected/changed, validation evidence, blockers, and task-specific risks.
