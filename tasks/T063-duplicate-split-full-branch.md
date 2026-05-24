# T063 — Duplicate/split full branch plan

## Goal

Restore Duplicate Layer and Split Layer for layers that contain masks and/or manual precomp branches.

Current guard from T062A blocks these cases because the old duplicate path only copied `Read/Gizmo/effects/Merge` and would shallow-copy `FluxMask`, `maskApplyNode`, and manual branch node refs.

## Non-goals

- No new mask semantics: V1 still uses the first enabled layer mask and first enabled mask per effect.
- No UI for precomp branch children; precomp branch remains represented by the layer-row icon.
- No recreation from plugin IDs. Use Natron clipboard copy/paste so knobs, curves, roto shapes, links, and branch internals survive.

## Implementation plan

### 0. Fix terminal Premult + layer mask rebuild idempotency first

Before enabling branch-aware duplicate/split, fix `Gui::rebuildCompositingGraph()` so repeated rebuilds do not corrupt terminal Premult mask chains.

Current hazard:

- The normal effect loop may preserve `Premult input 0 = maskApplyNode` from the previous rebuild.
- The terminal-mask block then derives `prePremultOutput = lastEffect->getInput(0)`, which can already be `maskApplyNode`.
- That can make `maskApplyNode input 1` preserve against itself and corrupt `source → maskApply → Premult`.

Required fix in `Gui/Gui05.cpp`:

- When the last effect is Premult and `layer.maskApplyNode` exists, derive the source before Premult from model order, not blindly from `Premult input 0`:
  - if there is an effect before Premult, use `layer.effects[size - 2].node`
  - otherwise use `layer.gizmoNode`
- If `Premult input 0 == layer.maskApplyNode`, use `layer.maskApplyNode->getInput(1)` as the already-preserved pre-Premult source when it exists and traces back to the expected model source.
- Keep the final desired graph:

```text
prePremultSource/manual-inline-chain → maskApply input 1
mask Roto                         → maskApply input 0
maskApply                         → Premult input 0
Premult                           → Merge input 1
```

This idempotency fix must be built and preserved before relying on duplicate/split of copied mask/Premult branches.

### 1. Copy the whole layer-owned node subgraph

In `FluxTimeline::duplicateLayer(int index)`, replace the footage/solid `nodesToCopy` collection with a branch-aware collection:

1. Start with explicit Flux model refs:
   - `readerNode`
   - `gizmoNode`
   - every `FluxEffect::node`
   - `maskApplyNode`
   - every `FluxMask::reformatNode`
   - every `FluxMask::maskNode`
   - `mergeNode`
2. Add discovered manual graph nodes from `classifyLayerBranches(layer)`:
   - `mainPipeNodes` for inline manual nodes between Flux-owned nodes
   - `maskBranchNodes` for upstream mask side-branch internals not explicitly in `FluxMask`
   - `precompBranchNodes` for manual non-mask branches feeding the layer source pipe
3. De-duplicate by raw `Node*` before converting to `NodeGuiPtr`.
4. Exclude only graph outside this layer:
   - previous composite/background (`mergeNode` input 0 is already excluded by classifier)
   - viewer/final output/export nodes
   - other layers’ nodes unless they are genuinely inside this layer branch classification

### 2. Build an old-node → pasted-node map

Use `NodeGraph::pasteCliboard()` result:

```cpp
std::list<std::pair<std::string, NodeGuiPtr>> newNodes;
```

The first item is the original script name from the clipboard serialization. Build:

```cpp
QHash<QString, NodePtr> pastedByOldScriptName;
```

using each pasted node's paired old script name. Resolve originals with `node->getScriptName()` for clipboard mapping. Avoid plugin-ID-only matching except as a fallback/error diagnostic.

### 3. Rebuild the duplicated `FluxLayer` model from the map

After paste:

- `duplicate.readerNode = mapped(layer.readerNode)`
- `duplicate.gizmoNode = mapped(layer.gizmoNode)`
- `duplicate.mergeNode = mapped(layer.mergeNode)`
- `duplicate.maskApplyNode = mapped(layer.maskApplyNode)`
- `duplicate.effects[e].node = mapped(layer.effects[e].node)` preserving order, plugin id, label, enabled
- `duplicate.masks[m].reformatNode = mapped(layer.masks[m].reformatNode)`
- `duplicate.masks[m].maskNode = mapped(layer.masks[m].maskNode)` preserving name/type/plugin/enabled/inverted/effectIndex
- `duplicate.hasPrecompBranch = false`; rebuild/classifier recomputes it
- `duplicate.nodeInitialized = true`
- `duplicate.locked = false`; preserve mute/color/range/timeOffset like existing duplicate behavior

Keep the existing footage filename reset and `Read → Gizmo` reconnect for duplicated footage layers.

### 4. Let rebuild reconnect Flux-owned boundaries without destroying pasted internals

Do not manually reconnect every internal copied edge. Natron clipboard paste already restores connections among copied nodes.

After insertion, emit `compositingChanged()` as today. `Gui::rebuildCompositingGraph()` should:

- reconnect layer order / merge B input
- preserve inline manual chains with `preserveInlineChainInput()`
- reconnect mask apply and effect mask Flux-owned inputs
- reclassify `hasPrecompBranch`

If rebuild overwrites a copied edge, it must only overwrite Flux-owned boundaries and still preserve copied manual nodes through the existing preservation/classifier logic.

### 5. Adjustment rows with masks stay guarded unless branch-aware adjustment copy is implemented

Adjustment rows use a separate duplicate path (`isAdjustment`) that currently copies only adjustment effect nodes. Since effect masks can be attached to adjustment effects, do **not** remove the mask guard for adjustment rows unless that path is also upgraded to copy/remap adjustment masks.

T063 implementation should take the minimal safe path:

- Non-adjustment footage/solid layers: implement branch-aware duplicate/split and re-enable masks/precomp.
- Adjustment rows without masks: keep existing duplicate/split behavior.
- Adjustment rows with masks: keep duplicate/split disabled for now, unless the implementation explicitly copies/remaps their mask `reformatNode`/`maskNode` refs too.

Concrete `canDuplicateRow()` / `canSplitRow()` logic after T063:

```cpp
if (l.type == "adjustment") {
    if (!l.masks.isEmpty()) return false; // until adjustment mask branch copy is implemented
    return !l.effects.isEmpty();          // existing behavior
}
// footage/solid branch-aware path supports masks/precomp
```

### 6. Re-enable duplicate/split for supported masked/precomp layers

After branch-aware duplicate works:

- Remove the T062A guard from non-adjustment rows that rejects `!masks.isEmpty() || maskApplyNode || hasPrecompBranch`.
- Keep existing guards for locked/null/missing gizmo/merge.
- Keep adjustment rows with masks disabled unless adjustment mask branch copy is implemented in the same change.

`splitLayer()` can continue to call `duplicateLayer(index)` then trim original and duplicate. Because the duplicated model now has distinct mask/effect/precomp node refs, split no longer shares graph nodes.

### 7. Failure behavior

If required refs fail to map after paste:

- Deactivate any pasted nodes created by this duplicate attempt.
- Return `false` and leave `_layers` unchanged.
- Log a clear `FLUX ERROR` with the missing original node label/script name.

Required refs:

- non-adjustment: `gizmoNode`, `mergeNode`
- footage: `readerNode` if original had one
- each modeled effect node that existed pre-copy
- each modeled mask node/reformat node that existed pre-copy
- `maskApplyNode` if original had one

### 8. Validation

Build:

```bash
cd /home/npittas/Flux/build && cmake --build . -- -j$(nproc)
```

Manual validation in Natron/Flux:

1. Simple layer duplicate/split still works.
2. Layer with layer mask duplicates: original and duplicate have separate Roto/Reformat/maskApply nodes; editing duplicate mask does not affect original.
3. Layer with effect mask duplicates: duplicated effect mask drives duplicated effect only.
4. Layer with terminal Premult + layer mask duplicates and rebuild keeps `source → maskApply → Premult → Merge A`.
5. Layer with manual inline node duplicates and the inline node remains in duplicate source pipe.
6. Layer with manual precomp branch duplicates and duplicate gets its own copied precomp branch; precomp icon appears after rebuild.
7. Split masked/precomp layer creates two independent halves with distinct copied branch nodes and correct trim points.
8. Adjustment row without masks still duplicates/splits as before.
9. Adjustment row with effect masks remains disabled unless explicitly implemented.
10. Save/reopen after duplicate/split restores all refs via existing serialization.

## Files expected to change

- `Gui/FluxTimeline.cpp`
- possibly `Gui/FluxTimeline.h` if helper declarations are added
- `tasks/TASKS.md` status/docs
- `plans/PHASES.md` when complete
