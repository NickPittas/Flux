# Locator Report

## Summary
T083 should integrate through existing Flux timeline mask graph/model, node creation, project import/bin, export/render, Python/PyPlug conventions, and Natron cache/render infrastructure rather than touching core roto/render internals first.

## Confidence
medium

## Codemap Status
- Index was present but stale.
- Stale reason: git commit changed; working tree had dirty/untracked files.
- Updated index for safe read-only locating.

## Relevant Locations

1. `file:///home/npittas/Flux/Gui/FluxTimeline.h`
   - symbol: `FluxMask`, `FluxLayer::masks`
   - approximate lines: 101–149
   - stable anchor: `struct FluxMask {`
   - why relevant: Existing mask data model where AI matte/depth-generated masks should be represented or extended.
   - evidence: `FluxMask` stores `name`, `type`, `pluginId`, `enabled`, `inverted`, `effectIndex`, `maskNode`, `reformatNode`.

2. `file:///home/npittas/Flux/Gui/FluxTimeline.cpp`
   - symbol: `addLayerMask`, `addEffectMask`, `canAddEffectMask`, `removeMaskFromLayer`
   - approximate lines: 1018–1160
   - stable anchor: `FluxTimeline::addLayerMask(int layerIndex)`
   - why relevant: Existing UI/model actions for adding masks; likely insertion point for “Generate AI Matte/Depth Mask” actions.
   - evidence: `addLayerMask()` appends a `FluxMask`, selects it, emits `masksChanged`, `compositingChanged`, `maskSelected`.

3. `file:///home/npittas/Flux/Gui/Gui05.cpp`
   - symbol: `ensureMaskSourceNodes`, layer/effect mask wiring in `Gui::rebuildCompositingGraph`
   - approximate lines: 1066–1240, 1880–2020
   - stable anchors:
     - `// Mask graph helpers (T060/T061)`
     - `// -- Effect mask wiring (T061) --`
     - `// -- Layer mask wiring (T064) --`
   - why relevant: Existing graph creation/wiring path for Roto-based masks. AI-generated matte outputs can be connected here as mask source nodes or new generated-media nodes.
   - evidence: Creates `Reformat`, `Roto`, `Premult`, `Unpremult` using `CreateNodeArgs` and `getApp()->createNode`.

4. `file:///home/npittas/Flux/Gui/FluxTimelineSerialization.h`
   - symbol: `FluxMaskSerialization`
   - approximate lines: 50–72
   - stable anchor: `struct FluxMaskSerialization`
   - why relevant: Generated masks/depth maps need save/reopen persistence here or adjacent structs.
   - evidence: Serializes `Name`, `Type`, `PluginId`, `Enabled`, `Inverted`, `EffectIndex`, `MaskNode`, `ReformatNode`.

5. `file:///home/npittas/Flux/Engine/RotoContext.h`
   - symbol: `RotoContext`
   - approximate lines: 60–120
   - stable anchor: `class RotoContext`
   - why relevant: Native roto shape/matte backend; useful if AI output is converted to roto splines rather than raster matte.
   - evidence: `RotoContext` is MT-safe, owns spline data structures, has `createBaseLayer()`, `getOrCreateBaseLayer()`, `isEmpty()`.

6. `file:///home/npittas/Flux/Gui/FluxProjectBin.h`
   - symbol: `FluxProjectBin`, signals `fileRequested`, `filesDropped`
   - approximate lines: 70–120
   - stable anchor: `class FluxProjectBin`
   - why relevant: Existing media import surface; AI-generated matte/depth image sequences could be imported into bin or attached to source media.
   - evidence: Public `addFile()`, `getFiles()`, signals for file-request/drop flow.

7. `file:///home/npittas/Flux/Gui/FluxProjectBin.cpp`
   - symbol: `FluxProjectBin::onImportButtonClicked`, `addFile`
   - approximate lines: 160–250
   - stable anchor: `FluxProjectBin::onImportButtonClicked()`
   - why relevant: Import filter and asset registration path.
   - evidence: Import dialog accepts video/images; `addFile()` stores paths and thumbnails.

8. `file:///home/npittas/Flux/Engine/CreateNodeArgs.h`
   - symbol: `CreateNodeArgs`
   - approximate lines: 30–120
   - stable anchor: `#define kCreateNodeArgsPropPluginID`
   - why relevant: Canonical OpenFX/plugin node creation API.
   - evidence: Defines plugin ID/version, initial params, GUI/autoconnect/out-of-project behavior.

9. `file:///home/npittas/Flux/Gui/FluxExportPanel.cpp`
   - symbol: `FluxExportPanel`, `syncFrameRangeFromProject`, render/output controls
   - approximate lines: 20–120, 300–370
   - stable anchors:
     - `FluxExportPanel::FluxExportPanel`
     - `FluxExportPanel::syncFrameRangeFromProject`
   - why relevant: Render/export integration if T083 outputs depth/matte passes or generated sidecar sequences.
   - evidence: Owns Write/Reformat nodes, output path, render button, frame range sync.

10. `file:///home/npittas/Flux/Engine/Cache.h`
   - symbol: `Cache`
   - approximate lines: 382–460+
   - stable anchor: `class Cache : public CacheAPI`
   - why relevant: Existing cache infrastructure for rendered images; likely read-only reference for invalidation/performance patterns, not first edit target.
   - evidence: Generic LRU cache template with entry/key/data abstractions.

## Allowed Edit Scope Recommendation
- Primary:
  - `Gui/FluxTimeline.h`
  - `Gui/FluxTimeline.cpp`
  - `Gui/FluxTimelineSerialization.h`
  - `Gui/Gui05.cpp`
- Likely new files:
  - `Gui/FluxAIMattePanel.{h,cpp}` or equivalent
  - `Gui/FluxAIMatteModel.{h,cpp}` for model choice/job state
  - optional plugin/PyPlug or helper script under `plugins/` only after architecture decision
- Secondary:
  - `Gui/FluxProjectBin.{h,cpp}` if generated outputs become bin assets
  - `Gui/FluxExportPanel.{h,cpp}` only if adding export of matte/depth passes

## Read-Only Context Recommendation
- `file:///home/npittas/Flux/Engine/RotoContext.h`
- `file:///home/npittas/Flux/Engine/CreateNodeArgs.h`
- `file:///home/npittas/Flux/Engine/Cache.h`
- `file:///home/npittas/Flux/plans/2026-05-20-engine-rendering-cache-1.0.md`
- Existing PyPlug examples in `file:///home/npittas/Flux/Gui/Resources/PyPlugs/`

## Validation Targets
- tests:
  - Existing build target for GUI/Natron app.
  - Save/reopen project containing generated mask/depth node refs.
- commands:
  - CMake build command used for Flux GUI target.
  - Launch Flux and verify Project Bin → Timeline → mask generation workflow.
- manual checks:
  - Add AI matte to footage layer.
  - Add AI matte to effect mask input where `discoverMaskInput()` succeeds.
  - Verify generated mask appears as timeline mask row.
  - Verify Roto/matte node properties open from mask row.
  - Save/reopen preserves generated mask/depth configuration.
  - Export render includes applied matte/depth result.

## Risks / Unknowns
- Current evidence locates local integration only; model/runtime choice still requires online research and architecture approval.
- No existing `BackgroundTask` symbol found; async inference/job handling needs deeper inspection or new Qt worker pattern.
- AI model outputs may be raster masks/depth maps, not roto splines; decide whether to store as generated media, plugin node output, or converted Roto shapes.
- Python scripting hooks exist broadly, but exact safest C++→Python job invocation surface needs a deeper pass.

## Stop Recommendation
Implementation should not proceed until model/runtime architecture is approved. Local edit surface is sufficiently identified for a task packet after that decision.

Note: I did not write `/home/npittas/Flux/t083-research/local-integration-surface.md` because this assigned role has a hard no-edit/no-write rule.