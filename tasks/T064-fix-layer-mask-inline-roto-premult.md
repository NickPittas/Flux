# T064 — Fix layer masks: inline Roto/Premult, no Merge(in)

## Problem

Current layer-mask graph is wrong for compositing practice and violates Nick's requested design.

Current implementation creates a Merge node with operation `in` and feeds the Roto mask into its A/B input path. This was introduced during T060/P6 implementation without Nick's approval.

Files/evidence:

- `Gui/Gui05.cpp`: `ensureLayerMaskApplyNode()` creates a Merge node for layer masks.
- `Gui/Gui05.cpp`: `setMergeOperationIn()` forces operation `in`.
- `Gui/Gui05.cpp`: layer-mask rebuild section wires `source → Merge(in)`, `Roto → Merge(in)`.

## Required behavior

Layer masks must be inline, readable, and vertical:

```text
source pipe
  → [Unpremult if the pipe already has alpha]
  → Roto
  → Premult
  → downstream layer Merge A
```

No Merge node for a layer mask. No split side branch for layer masks.

Effect masks are separate and may still use side mask inputs; do not rewrite effect-mask behavior in this task except where shared utilities must stop assuming layer masks are Merge(in).

## Implementation plan

### 1. Change the data meaning carefully

`FluxLayer::maskApplyNode` currently points to the wrong Merge(in). For compatibility/minimal schema churn:

- Reuse `maskApplyNode` as the inline **Premult** node reference for layer masks.
- Update comments/serialization wording later, but do not bump project serialization version unless absolutely required.
- Existing projects with old Merge(in) `maskApplyNode` should not reuse that node. On rebuild, if `maskApplyNode` exists and is a Merge/in node, deactivate it/reset the ref and create a Premult instead.
- Old artifact deactivation must be narrow: only deactivate Flux-owned old nodes by label/name, e.g. old `Flux Layer Mask Apply` Merge and old `Flux Mask Reformat`. Never deactivate arbitrary user Merge/Reformat nodes by plugin type alone.

### 2. Layer-mask node creation

In `Gui/Gui05.cpp`:

- Remove/stop using `ensureLayerMaskApplyNode()` for layer masks.
- Add helpers:
  - `ensureInlineLayerMaskPremultNode(Gui*, FluxLayer&, collection, x, y)` creates/reuses `net.sf.openfx.Premult`, stores in `layer.maskApplyNode`, label e.g. `Flux Layer Mask Premult`.
  - `ensureInlineLayerMaskUnpremultNode(...)` creates/reuses `net.sf.openfx.Unpremult` when needed.
- Add `isUnpremultNode()` utility alongside `isPremultNode()`.
- Existing `FluxMask::maskNode` is the Roto node and is now inline in the layer pipe.
- Do **not** use `FluxMask::reformatNode` for layer masks. It may remain for effect masks. For layer masks, deactivate/reset `lmask->reformatNode` if it was created by the old implementation.

Unpremult storage policy:

- Do not add serialization fields unless unavoidable.
- If no model ref is added, Unpremult must be deterministically discovered from graph topology:
  - expected source/effect output → optional Unpremult → layer-mask Roto → Premult
  - discover an existing Flux-owned Unpremult from `Roto input 0` when it is between expected source and Roto.
  - reuse it on rebuild/reopen instead of creating duplicates.
  - deactivate it when removing/disabling the layer mask only if it is Flux-owned.

### 3. Alpha detection / Unpremult rule

Nick's rule: if the pipe already has alpha, put an Unpremult before the Roto.

Implementation should use a conservative V1 detector:

- If the source is a footage layer, assume alpha may exist and insert Unpremult.
- If the layer has effects before the mask, assume alpha may exist and insert Unpremult.
- If the source is a solid/FluxSolid and no prior effect, Unpremult can be skipped.

If a reliable metadata premult/components API is easy and already available, use it; otherwise keep the conservative rule and document it in code. Conservative extra Unpremult is preferable to missing required unpremult on alpha media.

### 4. Rebuild graph wiring

Replace current layer-mask wiring in `Gui::rebuildCompositingGraph()`.

Desired V1 single enabled layer mask:

```text
layerOutputBeforeMask
  → optionalUnpremult input 0
  → Roto input 0
  → Premult input 0
  → layer.mergeNode input 1
```

If Unpremult is skipped:

```text
layerOutputBeforeMask → Roto → Premult → layer.mergeNode input 1
```

Preserve manual inline nodes with `preserveInlineChainInput()` around each boundary:

- `Unpremult input 0` expected source = `layerOutputBeforeMask`
- `Roto input 0` expected source = `unpremultNode` or `layerOutputBeforeMask`
- `Premult input 0` expected source = `Roto`
- merge A expected source = `Premult`

Do not special-case terminal Premult as the mask apply node anymore. If the user already has a terminal Premult effect before the mask, it remains part of `layerOutputBeforeMask`; the layer mask then adds its own Roto/Premult inline after it.

Stale chain rule:

- When there is **no enabled layer mask**, rebuild must not preserve a stale Flux-owned layer-mask chain on `layer.mergeNode input 1`.
- If current Merge A input is a Flux-owned inline layer-mask chain (`Unpremult/Roto/Premult` labelled as Flux layer mask nodes) that reaches `layerOutputBeforeMask`, force Merge A back to `layerOutputBeforeMask` and disconnect/deactivate old Flux-owned layer-mask nodes as appropriate.
- This avoids `preserveInlineChainInput()` keeping a disabled/removed mask alive simply because it still traces back to the expected source.

### 5. Branch classifier update

In `Gui/FluxMaskUtils.cpp`:

- Stop modeling layer masks as `maskApplyNode` with mask alpha input 0 and source image input 1.
- Treat layer `FluxMask::maskNode` as a main-pipe node for layer masks (`effectIndex < 0`).
- Treat `layer.maskApplyNode` as the inline Premult node when it is Premult.
- If an inline Unpremult helper is added to the model, include it in main pipe. If not stored in model, discover it as an inline node between expected source and Roto.
- Effect mask `FluxMask::maskNode` / `reformatNode` remain mask branch nodes when `effectIndex >= 0`.
- Precomp detection must continue to ignore effect-mask branches, but layer mask Roto/Premult must not be classified as precomp.

Explicit expected segment model for layer masks:

```text
source/effects → optional Unpremult input 0
optional Unpremult → layer Roto input 0
Roto → Premult input 0
Premult → layer Merge input 1
```

Layer-mask `maskNode` must be main pipe, not `knownMaskNodes`. Only effect masks remain known mask-branch nodes.

### 6. Duplicate/split and serialization impact

T063 branch-aware duplicate copies:

- explicit `maskApplyNode`
- every mask's `maskNode` and `reformatNode`
- classifier `mainPipeNodes`, `maskBranchNodes`, `precompBranchNodes`

After classifier update, inline layer-mask Roto/Premult/Unpremult should be copied as main-pipe nodes and refs remapped. If Unpremult is not stored directly, it must still be discovered/copied by classifier so duplicate/split does not share it.

Serialization can continue storing `maskApplyNodeScriptName`, but it now points to inline Premult for new projects.

### 7. Remove old Merge(in) artifacts

On rebuild or when creating a layer mask:

- If old `layer.maskApplyNode` is a Merge/in node labelled `Flux Layer Mask Apply`, deactivate it and clear the ref.
- If old layer-mask `reformatNode` labelled `Flux Mask Reformat` exists and is only for a layer mask, deactivate it/reset the ref.
- If an old/new Flux-owned inline Unpremult exists and the layer mask is removed/disabled/no longer needs Unpremult, deactivate it via topology discovery from `Roto input 0` only if labelled Flux-owned.

Do not delete user/manual nodes unless they are Flux-owned old layer-mask artifacts.

### 7b. Save/reopen migration

On restore/rebuild of old projects:

- If restored `maskApplyNode` is the old Flux-owned Merge(in), clear/deactivate it before graph rebuild creates inline Premult.
- If restored layer-mask `reformatNode` is old Flux-owned `Flux Mask Reformat`, clear/deactivate it.
- Rediscover existing Flux-owned inline Unpremult from graph before creating a new one so reopen does not duplicate Unpremult nodes.
- Continue serializing `maskApplyNodeScriptName`; for new projects it points to inline Premult.

### 8. Validation

Build:

```bash
cd /home/npittas/Flux/build && cmake --build . -- -j$(nproc)
```

Manual validation after Nick restarts:

1. Add a layer mask to a simple solid layer: graph is `FluxSolid/FluxLayer → Roto → Premult → Merge A`, no Merge(in), no Reformat side branch.
2. Add a layer mask to footage/effect layer: graph includes `Unpremult → Roto → Premult` inline.
3. Node graph remains vertical/readable; no extra compositing layer/merge for masks.
4. Effect masks still connect as side mask inputs.
5. Duplicate/split masked layer creates independent inline mask nodes.
6. Save/reopen restores inline layer-mask nodes.

## Status

Done. Oracle plan-reviewed, implemented, build passed, scrutinize found stale-chain/Unpremult cleanup blockers, blockers fixed, re-scrutinize verdict: ship.
