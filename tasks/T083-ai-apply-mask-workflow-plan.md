# Planner Report

## Status
ready

## Rationale
This revision addresses the reviewer blocker by making external raster layer-mask application an explicit alpha-replacement graph instead of feeding the PNG through the native Roto/RotoPaint chain. Locator evidence identifies the existing Apply surface, safe selected-result resolver, timeline mask model/serialization, mask-input discovery, current native mask rebuild paths, and Shuffle/Read plugin support, so the worker can implement a narrow explicit-Apply workflow while preserving native empty-path masks and avoiding main-viewer or auto-apply behavior.

# Task Packet

## User Goal
Implement T083 Packet F Apply workflow: when the user clicks AI Panel Apply, apply the selected managed AI result/history manifest mask PNG to the currently selected layer mask or unambiguous effect mask, using project-relative generated media paths. External layer masks must use an explicit raster alpha-replacement graph. Preserve native Roto/RotoPaint masks when no external raster path is set. Do not auto-apply after Run, Preview, or Live Preview.

## Mode
general-coding

## Relevant Locations
- file: `Gui/FluxAiPanel.cpp`
  symbol: `FluxAiPanel::onApplyClicked`
  approximate lines: 1215-1219
  stable anchor: `void FluxAiPanel::onApplyClicked()`
  reason: explicit Apply surface currently only logs prompt-tool guidance; replace with real explicit apply workflow.
  confidence: high
- file: `Gui/FluxAiPanel.cpp`
  symbol: `FluxAiPanel::selectedResultMaskProjectRelative`, `FluxAiPanel::previewSam3RunResult`, `FluxAiPanel::onPreviewAgainClicked`
  approximate lines: 684-721 and 1760-1770
  stable anchor: `selected_mask_path_project_relative`
  reason: already validates selected result manifest and returns a safe project-relative mask PNG; Apply must reuse this and remain separate from Preview Again.
  confidence: high
- file: `Gui/FluxAiPanel.h`
  symbol: `FluxAiPanel` private helpers/slots
  approximate lines: 31-145
  stable anchor: `void onApplyClicked();` and `bool selectedResultMaskProjectRelative(QString* relativeMask, QString* message) const;`
  reason: may need a private helper declaration for apply-to-mask orchestration.
  confidence: high
- file: `Gui/FluxTimeline.h`
  symbol: `FluxMask`, `FluxLayer::masks`, `addLayerMask`, `addEffectMask`, `addMaskToSelectedRow`, `canAddEffectMask`
  approximate lines: 101-116, 149-151, 245-250, 309, 390-391
  stable anchor: `struct FluxMask {`
  reason: mask model lacks external raster source fields; minimal apply/query API belongs with existing layer/effect mask semantics.
  confidence: high
- file: `Gui/FluxTimeline.cpp`
  symbol: `FluxTimeline::addMaskToSelectedRow`, `addLayerMask`, `addEffectMask`, `canAddEffectMask`, mask duplication/removal/remap paths
  approximate lines: 273-288, 655-733, 933-947, 1017-1123
  stable anchor: `FluxTimeline::addLayerMask(int layerIndex)` and `FluxTimeline::addEffectMask(int layerIndex, int effectIndex)`
  reason: Apply should create/update the selected target mask and keep external path metadata coherent through duplicate, remove, reorder, and effect remap operations.
  confidence: high
- file: `Gui/FluxTimelineSerialization.h`
  symbol: `FluxMaskSerialization`
  approximate lines: 60-88 and 417-419
  stable anchor: `std::string reformatNodeScriptName;`
  reason: add versioned persisted project-relative external mask source path and any external source/apply node script names needed for save/reopen.
  confidence: high
- file: `Gui/Gui05.cpp`
  symbol: `ensureMaskSourceNodes`, `ensureInlineLayerMaskPremultNode`, `ensureInlineLayerMaskUnpremultNode`, `Gui::rebuildCompositingGraph`
  approximate lines: 1695-1788, 1794-1863, 2557-2574, 2597-2678
  stable anchor: `// Mask graph helpers (T060/T061)`, `// -- Effect mask wiring (T061) --`, `// -- Layer mask wiring (T064) --`
  reason: current native graph is here; external raster layer masks must branch to explicit Read/Reformat/Shuffle/Premult alpha replacement while empty-path native masks keep the current Roto/RotoPaint graph.
  confidence: high
- file: `Gui/Gui05.cpp`
  symbol: `Gui::showFluxAiWorkViewerResultPreview`, footage Read creation/reload
  approximate lines: 926-943, 2259-2268, 2930-2936
  stable anchor: `getApp()->createReader(filePath, readArgs)`
  reason: existing safe PNG-backed reader creation/reload pattern for Flux-owned external mask source nodes.
  confidence: medium
- file: `Gui/FluxMaskUtils.cpp`
  symbol: `discoverMaskInput`, `isPremultNode`, `isUnpremultNode`
  approximate lines: 80-123, 126-160
  stable anchor: `int discoverMaskInput(const NodePtr& node)`
  reason: effect-mask Apply must use existing mask-input discovery and fail without mutation when no mask input exists; optional shared classification/constants may live here.
  confidence: high
- file: `Gui/FluxMaskUtils.h`
  symbol: mask utility declarations
  approximate lines: unknown
  stable anchor: existing declarations paired with `FluxMaskUtils.cpp`
  reason: optional narrow utility location for external mask node labels/classification if current code already centralizes mask graph helpers there.
  confidence: medium
- file: `Engine/EffectInstance.h`
  symbol: `PLUGINID_OFX_READOIIO`, `PLUGINID_OFX_SHUFFLE`, `PLUGINID_NATRON_ROTO`, `PLUGINID_NATRON_READ`
  approximate lines: 54-99
  stable anchor: `#define PLUGINID_OFX_SHUFFLE`
  reason: plugin IDs/macros available for Read/OIIO, Shuffle, and native Roto; use existing constants rather than string literals where available.
  confidence: medium
- file: `Documentation/source/plugins/net.sf.openfx.ShufflePlugin.rst`
  symbol: `outputR`, `outputG`, `outputB`, `outputA`, `outputComponents`, `outputPremult`
  approximate lines: 17, 40-53, 57-96
  stable anchor: `Rearrange channels from one or two inputs`
  reason: documents the safe two-input Shuffle graph and channel knobs for replacing layer alpha from external PNG alpha/red while preserving layer RGB.
  confidence: medium

## Allowed Edit Files
- `Gui/FluxAiPanel.h`
- `Gui/FluxAiPanel.cpp`
- `Gui/FluxTimeline.h`
- `Gui/FluxTimeline.cpp`
- `Gui/FluxTimelineSerialization.h`
- `Gui/Gui05.cpp`
- `Gui/FluxMaskUtils.h`
- `Gui/FluxMaskUtils.cpp`

## Read-Only Context Files
- `Documentation/source/plugins/net.sf.openfx.ShufflePlugin.rst`
- `Engine/EffectInstance.h`
- `Gui/ProjectGuiSerialization.h`
- `tasks/T083-ai-paint-live-sam3-workflow-plan.md`
- `tasks/T083-ai-matte-depth.md`

## Required Change
1. Extend `FluxMask` with minimal project-relative external raster source metadata, e.g. `QString externalMaskPathProjectRelative`, plus node refs/script names needed for Flux-owned external mask `Read/Reformat` and layer-mask `Shuffle` nodes. Keep all new fields empty by default so existing native Roto/RotoPaint masks are unchanged.
2. Extend `FluxMaskSerialization` compatibly with the project-relative external PNG path and any new external source/apply node script names needed for node reuse. Older projects with only native masks must load with empty external fields. Save/reopen must restore project-relative paths, never developer-machine absolute generated-media paths.
3. Add narrow timeline API for Apply to resolve the current target without broad UI judgment, preferably `applyExternalMaskToSelectedRow(const QString& projectRelativeMaskPath, QString* message)`. It must support:
   - selected layer row: update an existing enabled layer mask if one exists, otherwise create one with `addLayerMask` unless the layer is locked;
   - selected effect row: update an existing enabled mask for that effect if one exists, otherwise create one with `addEffectMask` only if `canAddEffectMask` and `discoverMaskInput` allow it;
   - selected mask row: update that exact mask;
   - ambiguous/no selection/locked layer/unsupported effect mask input: return false with a clear message and do not create nodes.
4. Implement `FluxAiPanel::onApplyClicked` to call `selectedResultMaskProjectRelative`, find the active Flux timeline through existing GUI wiring/accessors, call the timeline apply helper, update status/log, and trigger only the normal timeline `compositingChanged`/rebuild path. Apply must remain explicit; do not call this path from Run completion, Preview Again, or live preview.
5. Update graph rebuild in `Gui05.cpp` for external raster masks while preserving native empty-path behavior:
   - If `externalMaskPathProjectRelative` is empty, leave the existing native graph unchanged: effect masks use `Reformat -> Roto -> discovered mask input`; layer masks use `layerOutput -> [Unpremult] -> Roto/RotoPaint -> Premult -> Merge A`.
   - If an effect mask has an external path, instantiate/reuse a Flux-owned external PNG `Read` plus `Reformat` if needed, resolve the file from project directory + sanitized project-relative path, and connect the external mask source to `discoverMaskInput(effect.node)`. If no mask input exists, fail/recover without mutation beyond safe cleanup and report via Apply helper path.
   - If a layer mask has an external path, do **not** feed the external PNG through Roto/RotoPaint. Build/reuse the explicit alpha-replacement chain: `layerOutput -> [optional existing Unpremult under current conservative rule] -> Shuffle(A input)` and `external SAM3 PNG Read -> [Reformat to project format if needed] -> Shuffle(B input)`, set Shuffle output RGB from A (`outputR=A.Color.R`, `outputG=A.Color.G`, `outputB=A.Color.B`) and output alpha from B alpha (`outputA=B.Color.A`) or B red (`outputA=B.Color.R`) for grayscale/RGB mask PNGs by contract, then `Shuffle -> Premult -> Merge A`.
   - Reuse existing Flux-owned Unpremult/Premult helpers only where they are unambiguous; add/reuse Flux-owned labels for external source and Shuffle nodes such as `Flux Layer External Mask Read`, `Flux Layer External Mask Reformat`, and `Flux Layer External Mask Shuffle`.
6. Use Flux-owned node label/name conventions and/or script-name metadata for external AI mask nodes so rebuild, duplicate, delete, and cleanup can identify them without deleting or replacing user-created native RotoPaint/Roto nodes. Applying an external AI PNG must coexist with any native `maskNode`; it must not repurpose or destroy native Roto/RotoPaint mask nodes.
7. Preserve existing mask lifecycle behavior: delete/deactivate layer/effect masks should clean up Flux-owned external source/shuffle nodes; duplicate/split/move/remap should carry the external path and node refs appropriately; removing/reordering effects must still remap/remove effect masks as current code does.
8. Keep UI behavior minimal: existing Apply button/result selection behavior is enough, but failure messages must clearly explain no selected target, locked layer, ambiguous target, non-project-relative path, missing file, or effect with no mask input.
9. Path handling must be project-relative and constrained: sanitize the selected manifest path, resolve under the project/generated-media directory, reject absolute paths or traversal, and persist only the relative string in Flux metadata.

## Non-Goals
- No auto-apply after SAM3 Run, Preview Again, or Live Preview.
- No changes to main comp viewer preview semantics.
- No conversion of SAM3 PNGs into Roto/RotoPaint splines.
- No replacement/destruction of native RotoPaint/Roto mask nodes.
- No broad AI worker/model-manager changes.
- No new product UI beyond the existing Apply button and existing timeline mask selection behavior.
- No absolute generated-media paths in Flux timeline/mask serialization.
- No hardcoded Shuffle choice integer values unless surrounding code already proves them; prefer knob names/choice labels/helper lookup.

## Validation
Commands:
- `cmake --build /home/npittas/Flux/build --target Natron -j$(nproc)`

Manual GUI proof required:
- Open a real project, run or select an existing managed SAM3 result history item whose manifest contains `selected_mask_path_project_relative`, select a layer row, click Apply, and verify the layer mask affects compositing through the explicit external `Read/Reformat/Shuffle/Premult` chain before the layer Merge A.
- Verify the external layer mask Shuffle preserves layer RGB from A and derives alpha from B alpha, or from B red for grayscale/RGB masks by the selected contract.
- Select an effect row with an unambiguous/discoverable mask input, click Apply, and verify the effect mask input is connected to the applied external mask source; for an effect without a mask input, verify Apply fails with a clear message and no graph mutation.
- Save, close, reopen, and verify the applied AI mask path remains project-relative in Flux serialization and the graph reconnects without rerun/preview.
- Verify Preview Again still only previews the selected result in the AI work preview path and does not apply to layer/effect masks.
- Verify pre-existing native layer/effect Roto/RotoPaint masks with empty external path still rebuild and work with the unchanged native graph.

Expected result:
Build succeeds; Apply explicitly binds the selected managed AI result PNG to the selected layer/effect mask target; external layer masks use a safe alpha-replacement Shuffle graph; no automatic apply occurs from Run/Preview/Live; save/reopen preserves project-relative paths; native RotoPaint/Roto masks are preserved.

## Stop Conditions
Stop and report if:
- target symbol is missing
- required fix exceeds allowed files
- validation cannot run
- existing architecture contradicts the requested change
- task requires product/design judgment not in packet
- no safe way exists for `FluxAiPanel` to access the active `FluxTimeline` without adding a broad GUI API outside the allowed files
- effect mask target is ambiguous or `canAddEffectMask`/`discoverMaskInput` says the selected effect cannot accept a mask
- generated mask path cannot be sanitized as project-relative or resolved under the project directory
- implementing external PNG source would require replacing/deleting native RotoPaint/Roto mask nodes
- layer-mask implementation cannot build the explicit `Read/Reformat/Shuffle/Premult` alpha-replacement graph and would instead feed the external PNG through the native Roto chain
- save/reopen requires changing project serialization outside the allowed files

## Planner Self-Check
- locator evidence sufficient: yes — high/medium-confidence anchors cover Apply, selected result path resolution, mask model, timeline mask add/remap paths, serialization, current native graph rebuild, effect mask discovery, Read creation, and Shuffle channel behavior.
- allowed edit files minimal and explicit: yes — all files are explicit and limited to AI panel apply, timeline mask model/API/serialization, mask utilities, and graph rebuild.
- read-only context minimal: yes — only Shuffle docs, plugin ID constants, project serialization header, and T083 context docs are listed.
- anchors/lines included: yes — relevant locations include path, symbol/anchor, approximate lines, reason, and confidence.
- validation concrete: yes — build command plus targeted manual GUI, graph, save/reopen, no-auto-apply, and native-mask regression checks.
- parallelization decision explicit and safe: yes — single task; shared graph rebuild, timeline mask model, and serialization files make this unsafe to parallelize with other T083 Apply/timeline work in the same worktree.
- non-goals and stop conditions sufficient: yes — scope protects explicit Apply, main viewer semantics, native Roto/RotoPaint preservation, project-relative paths, and allowed-file boundaries.
- reviewer findings addressed, if revision: yes — the blocker is addressed by requiring external raster layer masks to use an explicit `Read/Reformat/Shuffle/Premult` alpha-replacement graph and forbidding the unsafe external-PNG-through-Roto path.

## Required Return Contract
Return only a task-focused summary. Do not include transcript, tool logs, raw file dumps, large code blocks, or broad unrelated issues. Include status, files inspected/changed, validation evidence, blockers, and task-specific risks.
