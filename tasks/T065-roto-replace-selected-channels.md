# T065 — Roto/RotoPaint: Replace selected channels before drawing

## Goal

Add a native checkbox to Natron's original Roto and RotoPaint nodes, near the R/G/B/A channel controls:

```text
Replace selected channels
```

When enabled, the node must zero the selected/process channels from the incoming image **before** any Roto/RotoPaint shape/stroke compositing happens. Then the normal Roto/RotoPaint behavior runs.

Example:

- Roto processes Alpha only.
- Replace selected channels is enabled.
- Input alpha is zeroed first.
- Roto shapes are then injected/composited into alpha according to their normal operator/blending behavior.
- RGB passes through unchanged.

This is required for Flux inline layer masks: `source → Roto(replace alpha) → Premult`.

## Current code path

- Channel knobs are created in `Engine/RotoPaint.cpp` around `RotoPaint::initializeKnobs()`:
  - `kNatronOfxParamProcessR/G/B/A`
  - stored in `_imp->enabledKnobs[4]`.
- Roto default channel selection is RGB off, Alpha on in `RotoNode::isHostChannelSelectorSupported()`.
- Render selected channels are read into `copyChannels` in `RotoPaint::render()`.
- RotoPaint internal tree connects the Roto/RotoPaint node input as the global Merge B/background input in `RotoContext::refreshRotoPaintTree()`.
- Final render uses `copyUnProcessedChannels()` to restore unselected channels from the original input.

## Non-negotiable behavior

The zeroing must happen before the node does what it normally does. It is not enough to zero the output after rendering because many blend modes use the previous input channel during compositing.

## Implementation plan

### 1. Add the knob

Files:

- `Engine/RotoPaintInteract.h`
- `Engine/RotoPaint.cpp`

Add `KnobBoolWPtr replaceSelectedChannelsKnob;` to `RotoPaintPrivate`.

In `RotoPaint::initializeKnobs()`, after the R/G/B/A channel bools and before/near `Premultiply`, add:

- label: `Replace`
- name: `replaceSelectedChannels`
- default: `false`
- animation disabled
- tooltip: zero selected R/G/B/A channels from input before drawing/compositing shapes/strokes.

Place visually near the channel checkboxes; use `setAddNewLine(...)` so it appears next to/under the channel controls without disrupting the rest of the page.

### 2. Provide a zero-selected-channels helper

Preferred minimal implementation: add a local helper in `RotoPaint.cpp` that mutates an `Image` over `args.roi` and sets selected components to 0.

Constraints:

- Must handle float images; RotoPaint supports float only (`addSupportedBitDepth()` returns float).
- Must handle RGBA and Alpha images; be safe for RGB/XY by only zeroing channels that exist in the target plane.
- Channel mapping follows Natron convention already used by `copyUnProcessedChannels()`:
  - RGBA: R=0, G=1, B=2, A=3
  - Alpha single-component plane maps to A.
- Do not touch unselected channels.

If no existing public Image API exists for per-channel fill, implement it carefully in `RotoPaint.cpp` using Image pixel access patterns already present in Engine code, or add a small `Image` method if that is cleaner and reviewed.

### 3. Feed the internal Roto tree with a zeroed-background image

This is the critical part.

The current internal tree uses the Roto/RotoPaint node input as the background/global Merge B input (`RotoContext::refreshRotoPaintTree()`). The replacement must ensure that background has selected channels zeroed **before** internal merges composite the shapes.

Implementation must use a persistent internal preprocessing node, not render-time graph mutation.

#### Required approach — internal background preprocessing node

Add an internal/out-of-project, no-GUI preprocessing node owned by `RotoContext`, analogous to the existing hidden global merge nodes.

Internal tree wiring becomes:

```text
RotoPaint/Roto input 0
  → ReplaceSelectedChannels internal node
  → globalMerge B input
```

The internal node behavior:

- Replace off: identity/pass-through.
- Replace on: copy input and zero the selected/process channels before output.
- It must preserve unselected channels.
- It must be keyed by the parent Roto/RotoPaint replace checkbox and selected channel knobs for cache/hash correctness.
- It must not appear in the user nodegraph.

Do **not** rewire global merge inputs during `RotoPaint::render()`; `RotoContext::refreshRotoPaintTree()` mutates global graph state and doing that during render would race concurrent renders.

Do **not** use an external OFX Shuffle/Channel node unless its knob schema/hash behavior is fully proven. Prefer a native internal EffectInstance implementation for this hidden Roto infrastructure.

Implementation shape:

- Add a small native internal effect class, e.g. `RotoReplaceChannels`, following Natron's built-in plugin pattern.
- Add a new internal plugin ID beside the other built-ins in `Engine/EffectInstance.h`, e.g. `PLUGINID_NATRON_ROTO_REPLACE_CHANNELS`.
- Implement a static `BuildEffect(NodePtr)` factory like other native effects.
- Register it in `Engine/AppManager.cpp` with `AppManager::registerBuiltInPlugin<...>()` and `internalUseOnly=true`, following the `RotoSmear` internal-only pattern.
- It takes one input.
- It owns/receives render-affecting knobs/state for:
  - replace enabled
  - process R/G/B/A channel booleans
- It renders identity when replace is off.
- It renders input copy with selected channels zeroed when replace is on.
- It supports float/RGBA/Alpha safely.
- It is created in `RotoContext::getOrCreate...()` with:
  - `kCreateNodeArgsPropOutOfProject = true`
  - `kCreateNodeArgsPropNoNodeGUI = true`
  - fixed internal name based on parent Roto node script name
- `RotoContext::refreshRotoPaintTree()` should connect parent input 0 to this internal node, then connect the internal node to globalMerge input 0.
- If replace is off, the internal node can remain connected as identity so topology is stable.

Internal effect contract:

- `getNInputs() == 1`
- accepted components: RGBA/RGB/Alpha/XY-safe, matching what Roto/RotoPaint accepts where practical
- supported bit depth: float at minimum
- `renderThreadSafety() = eRenderSafetyFullySafe`
- supports tiles and multiresolution like RotoPaint
- `isIdentity()` returns input 0 when replace is off
- `render()`:
  - obtains input 0 for the requested ROI/time/view
  - copies/converts it to output plane(s)
  - if replace is on, zeros selected existing channels in the output image over ROI
  - leaves unselected channels untouched

RotoContext ownership/storage:

- Add one internal replace-channel node ref to `RotoContextPrivate` (`NodePtr` or `NodeWPtr`, consistent with existing internals).
- Add `RotoContext::getOrCreateReplaceChannelsNode()`.
- Create it using `CreateNodeArgs` like `getOrCreateGlobalMergeNode()` creates hidden global merge nodes.
- Include the preprocessing node in `RotoContext::getRotoPaintTreeNodes()`, otherwise thread-local render setup/cache traversal can miss it.

Render-only override is not the preferred path. If a fixer discovers the internal node is impossible within this task, stop and report rather than implementing a thread-unsafe workaround.

### 4. Final output composition

The existing final output behavior should remain:

- Unselected channels are restored from the original input via `copyUnProcessedChannels()`.
- Selected channels come from the Roto/RotoPaint result.

When replace is off, existing behavior must be unchanged.

When items are empty:

- Replace off: identity behavior unchanged.
- Replace on: selected channels should be zeroed even if there are no shapes/strokes, because the checkbox means “zero selected channels before doing anything.” If there is nothing to draw afterward, the output selected channels remain zero.

This empty-items path is handled directly in `RotoPaint::render()` because the internal RotoPaint tree is bypassed when there are no items.

### 5. Hash/cache invalidation

The new parent Roto/RotoPaint knob must affect render hash/cache. Standard knob creation should do this if it is a normal non-secret render-affecting knob. Do not mark it metadata-slave/secret/non-evaluating.

The internal preprocessing node must also be hash-safe:

- Prefer normal internal render-affecting knobs on the preprocessing node:
  - `replaceSelectedChannels`
  - process R/G/B/A booleans
- Sync these internal knobs from the parent Roto/RotoPaint replace/channel knobs when the parent knobs change and before refresh/render graph use.
- Do not read mutable parent hidden state directly from the internal node render unless the internal node hash explicitly includes that state.
- Avoid any hidden state that changes render output without changing hash.

### 6. Validation

Build:

```bash
cd /home/npittas/Flux/build && cmake --build . -- -j$(nproc)
```

Manual tests:

1. Roto alpha-only, Replace off: existing alpha is preserved/combined as before.
2. Roto alpha-only, Replace on: input alpha becomes zero before shapes; drawn shape defines alpha.
3. Roto RGB selected, Replace on: selected RGB channels zero before drawing, alpha untouched unless A selected.
4. RotoPaint strokes with Replace on obey normal paint/blend behavior over zeroed selected channels.
5. Empty Roto with Replace on zeros selected channels and passes unselected channels.
6. Save/reopen preserves checkbox.

## Open implementation question for oracle

The hard part is adding the native internal preprocessing EffectInstance and registering/creating it cleanly as an out-of-project hidden node. Oracle rejected render-time rewiring; this plan now requires stable topology through the internal preprocessing node.

## Result

DONE — implemented and reviewed.

- Added visible `Zero selected input channels` checkbox (`replaceSelectedChannels`) to Roto/RotoPaint near the process-channel controls.
- Added hidden internal `RotoReplaceChannels` node (`PLUGINID_NATRON_ROTO_REPLACE_CHANNELS`) and registered it internal-only.
- `RotoContext` owns/syncs the hidden node and feeds per-item Roto/RotoPaint trees through `getRotoPaintInputForInternalTree()`.
- Empty-Roto render path directly zeros selected channels when replacement is enabled.
- Save/reopen preserves the checkbox and does not serialize the hidden preprocessing node as a project node.
- Fixed related save/load stale-panel crash, Shadertoy GL context currentness, Qt slot warnings, and CImg OFX discovery from `~/.OFX/Plugins`.

Validation:

- Build passed: `cmake --build /home/npittas/Flux/build --target Natron -- -j$(nproc)`.
- Save/load smoke passed for `/home/npittas/Flux/rotoDemo.ntp`; Nick also confirmed interactive save/load.
- Shadertoy GL render confirmed working by Nick after xcb/reattach fix.
- CImg OFX bundle installed to `~/.OFX/Plugins/CImg.ofx.bundle`; cache contains `net.sf.cimg.CImgBlur`, `net.sf.cimg.CImgBloom`, and `net.sf.cimg.CImgDilate`.
- Final oracle review verdict: ship.
