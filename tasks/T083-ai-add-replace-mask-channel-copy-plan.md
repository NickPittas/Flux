# Planner Report

## Status
ready

## Rationale
Nick's source-of-truth and reviewer feedback are explicit: AI Panel Add/Replace must create and maintain visible AI Copy/Shuffle effect rows that write generated mask RGBA into custom `ai_maskN` planes while preserving the selected layer's original visible Color/RGBA output. The plan is scoped to the AI Panel, Flux timeline effect model/serialization, and graph rebuild/wiring files already identified by locator evidence, with Engine channel-plane APIs treated as read-only evidence and with hard stops for any Shuffle/pass-through limitation.

# Task Packet

## User Goal
Replace the AI Panel's rejected `Apply` workflow with `Add Mask` and `Replace Mask`. `Add Mask` creates a visible Copy/Shuffle effect row in the selected layer flow using the next custom plane slot (`ai_mask1`, `ai_mask2`, ...). `Replace Mask` updates only the selected visible AI Copy/Shuffle effect row. AI Read results are RGBA and must copy R/G/B/A into `ai_maskN.r/g/b/a`. The effect must preserve/pass through the original layer Color/RGBA exactly while adding/writing the custom `ai_maskN` plane. Do not create Roto layer masks and do not add Premult.

## Mode
general-coding

## Relevant Locations
- file: `Gui/FluxAiPanel.h`
  symbol: `FluxAiPanel` button/slot declarations
  approximate lines: 31-145
  stable anchor: `_applyButton`, `onApplyClicked()`
  reason: replace Apply controls with Add Mask / Replace Mask controls and slots.
  confidence: high
- file: `Gui/FluxAiPanel.cpp`
  symbol: `FluxAiPanel::setupUi`, `FluxAiPanel::onApplyClicked`
  approximate lines: 340-374, 1247-1277
  stable anchor: `_applyButton = new QPushButton(tr("Apply"));`, `timeline->applyExternalMaskToSelectedRow(relativeMask, &message)`
  reason: current AI result application entry point; change to two explicit workflows and clear success/failure status messages.
  confidence: high
- file: `Gui/FluxTimeline.h`
  symbol: `FluxEffect`, `FluxLayer`, timeline selection/model APIs
  approximate lines: 90-154 and related public methods later in file
  stable anchor: `struct FluxEffect`, `QList<FluxEffect> effects`
  reason: visible AI Copy/Shuffle must be represented as a normal effect row with metadata for AI ownership, target `ai_maskN` plane, and source mask path.
  confidence: high
- file: `Gui/FluxTimeline.cpp`
  symbol: effect row creation/selection/actions; external mask apply path
  approximate lines: 326-779, 933-1020, 1185-1190
  stable anchor: `layer.effects`, `mask.externalShuffleNode`, `applyExternalMaskToSelectedRow`
  reason: add APIs for Add AI mask Copy/Shuffle and Replace selected AI mask Copy/Shuffle; do not reuse the hidden external-mask apply workflow.
  confidence: high
- file: `Gui/FluxTimelineSerialization.h`
  symbol: `FluxEffectSerialization`, `FluxMaskSerialization`
  approximate lines: 35-103, 268-353
  stable anchor: `struct FluxEffectSerialization`, `ExternalShuffleNode`
  reason: persist AI Copy/Shuffle effect metadata on visible effect rows; do not depend on rejected hidden mask serialization.
  confidence: high
- file: `Gui/Gui05.cpp`
  symbol: `Gui::rebuildCompositingGraph`, Flux external mask helper area, effect wiring
  approximate lines: 1696-1785, 2277-3198
  stable anchor: `kFluxExternalMaskReadLabel`, `Gui::rebuildCompositingGraph(FluxTimeline* timeline)`
  reason: create/reconnect AI Copy/Shuffle nodes as ordinary visible layer-flow effects and ensure Add/Replace does not create FluxMask/Roto/Premult graph branches.
  confidence: high
- file: `Engine/Node.h`
  symbol: `Node::addUserComponents`
  approximate lines: 1268-1273
  stable anchor: `bool addUserComponents(const ImagePlaneDesc& comps);`
  reason: read-only evidence for registering custom output image planes such as `ai_maskN`; reviewer notes this is public but only valid on output/channel selector nodes.
  confidence: high
- file: `Engine/Node.cpp`
  symbol: `Node::addUserComponents`
  approximate lines: 7225-7265
  stable anchor: `Node::addUserComponents(const ImagePlaneDesc& comps)`
  reason: confirms API depends on channel selector/output support, rejects duplicate plane IDs, refreshes metadata, and selects the created output layer.
  confidence: high
- file: `Engine/ImagePlaneDesc.h`
  symbol: `ImagePlaneDesc` constructors and plane/channel helpers
  approximate lines: 64-151
  stable anchor: `ImagePlaneDesc(const std::string& planeID`, `getPlaneID()`, `getChannels()`
  reason: read-only evidence for constructing stable custom RGBA plane descriptors named `ai_maskN` with channels `r`, `g`, `b`, `a`.
  confidence: high
- file: `Documentation/source/plugins/net.sf.openfx.ShufflePlugin.rst`
  symbol: Shuffle parameters
  approximate lines: 1-112
  stable anchor: `Output Layer / ``outputLayer```; `Output Premult / ``outputPremult```; `R / ``outputR```
  reason: locator evidence for `net.sf.openfx.ShufflePlugin` and stable channel/output knobs used by the Copy/Shuffle row.
  confidence: medium

## Allowed Edit Files
- `Gui/FluxAiPanel.h`
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
- `Engine/Node.h`
- `Engine/Node.cpp`
- `Engine/ImagePlaneDesc.h`
- `Documentation/source/plugins/net.sf.openfx.ShufflePlugin.rst`

## Required Change
1. Before product edits, run a small compile/runtime accessibility spike from the allowed GUI/timeline code path, then remove any temporary spike code. Verify all of the following together on the intended visible Copy/Shuffle node class:
   - the worker can construct an `ImagePlaneDesc` for exact plane IDs `ai_mask1`, `ai_mask2`, etc. with RGBA channels `r`, `g`, `b`, `a`;
   - `Node::addUserComponents(...)` is called only on a node that actually supports output/channel selection, not on arbitrary nodes;
   - the new custom plane can be selected/configured by stable knobs; and
   - the node can pass through the input Color/RGBA unchanged while adding or writing the `ai_maskN` output plane.
   If any item fails, stop and report. Do not edit `Engine/Node.*`, do not invent fallback plane names, and do not fall back to Roto masks, hidden masks, Color/Alpha replacement, or Premult.
2. Replace the AI Panel `Apply` button with two user-visible buttons: `Add Mask` and `Replace Mask`. Both operate on the selected/last valid generated AI mask result using existing result validation helpers (for example `selectedResultMaskProjectRelative`) and must report clear status/log messages.
3. Implement `Add Mask` so it requires a selected normal layer/effect context, creates a visible timeline effect row backed by `net.sf.openfx.ShufflePlugin` or an existing equivalent Copy/Shuffle node, registers/selects the next free target plane `ai_maskN`, and copies AI Read RGBA source channels into `ai_maskN.r`, `.g`, `.b`, `.a`.
4. Configure the visible Copy/Shuffle row so the original image remains the visible layer output: Color/RGBA from the upstream layer input must be passed through unmodified, and only the custom `ai_maskN` plane is added/written from the AI Read RGBA. The worker must identify and set the plugin's layer/channel knobs to preserve input Color/RGBA rather than replacing it with the mask. If the selected plugin cannot simultaneously preserve/pass through original Color/RGBA and add/write `ai_maskN`, stop and report the exact limitation.
5. Label the row clearly, e.g. `AI Mask ai_mask1`, and ensure it appears/selects like other expanded layer effect rows.
6. Implement next-slot detection per selected layer by scanning existing active AI Copy/Shuffle effect metadata/nodes for `ai_maskN`; choose the lowest positive unused N. `Add Mask` must not overwrite existing AI mask planes.
7. Implement `Replace Mask` so it only updates the currently selected visible AI Copy/Shuffle effect row's source/read/path while preserving its existing `ai_maskN` target and preserving original Color/RGBA pass-through behavior. If the selected row is not an AI-created Copy/Shuffle effect, fail clearly with a message equivalent to: `Replace Mask failed: select an AI Mask Copy/Shuffle effect row in the timeline.` Do not guess from selected layer, newest result, or most recently created mask.
8. Store enough metadata on `FluxEffect` and in `FluxEffectSerialization` to identify AI mask Copy/Shuffle rows after save/reopen: at minimum AI-mask flag/type, target plane name (`ai_maskN`), source mask project-relative/manifest path, and associated Read/Shuffle node script names needed to reconnect/update. Preserve generic effect behavior.
9. Update graph creation/rebuild so AI mask Copy/Shuffle nodes are Flux-owned visible effects in the selected layer flow and reconnect in effect order like ordinary layer effects. They must not create `FluxMask`, Roto/RotoPaint, maskApply, terminal Premult, hidden external-mask Shuffle, or any layer alpha replacement path.
10. Configure Shuffle/Copy knobs by stable lookup with guarded errors. Required custom-plane mapping: AI Read RGBA source channels to destination `ai_maskN.r/g/b/a`. Required visible-image behavior: upstream Color/RGBA remains bitwise/visually unchanged through the effect row. Set `outputPremult` only to neutral/unpremultiplied metadata if required by the plugin, but do not add a Premult node.
11. Preserve duplicate/split/save/reopen semantics for the visible effect row. If existing duplicate/split code copies effect nodes generically, ensure new AI metadata maps to the copied node and does not alias the original update target.
12. Keep legacy Roto/Premult mask functionality unchanged for non-AI workflows. The AI Panel Add/Replace workflow must not call `applyExternalMaskToSelectedRow` or create/update timeline mask rows.

## Non-Goals
- No Roto/RotoPaint mask creation for AI Panel Add/Replace.
- No layer Color/RGBA or alpha replacement; the visible image must remain unchanged by Add Mask except for the added custom plane metadata/data.
- No terminal Premult insertion and no new Premult node.
- No edits to `Engine/Node.h`, `Engine/Node.cpp`, or `Engine/ImagePlaneDesc.h`; use them only as read-only API evidence.
- No guessing a Replace target from selected layer, newest result, or last created mask.
- No broad redesign of AI generation, model manager, worker protocols, or result manifests except fields strictly needed for this workflow.
- No OpenFX plugin binary or third-party plugin changes.

## Validation
Commands:
- `cmake --build "$BUILD_DIR" --target Natron -j$(nproc)`
- `cmake --build "$BUILD_DIR" --target NatronRenderer -j$(nproc)`
- Manual GUI validation with a saved project and real AI-generated mask result: run/generate an AI mask, select a layer, click `Add Mask`, verify a visible `AI Mask ai_mask1` Copy/Shuffle effect row appears in that layer flow and the AI path creates no Roto/Premult/FluxMask row.
- Manual image-preservation validation before save: compare the viewer/rendered layer Color/RGB and alpha immediately before Add Mask vs immediately after Add Mask; expected visible RGB and alpha are unchanged while the node output exposes an `ai_mask1` plane containing the AI Read RGBA data.
- Manual GUI validation: add a second result to the same layer and verify it targets `ai_mask2.*` without overwriting `ai_mask1.*`, with visible Color/RGBA still unchanged.
- Manual GUI validation: select an `AI Mask ai_mask1` effect row, click `Replace Mask`, verify the row/node source updates while retaining `ai_mask1.*` and preserving original visible Color/RGBA.
- Manual GUI validation: select a normal layer or non-AI effect row, click `Replace Mask`, verify clear failure and no graph/model mutation.
- Manual save/reopen validation: after Add Mask, save/reopen and verify visible AI Copy/Shuffle rows, target `ai_maskN`, source path, graph wiring, and `ai_maskN` plane persist.
- Manual save/reopen image-preservation validation: after reopening, compare the visible layer Color/RGB and alpha against the pre-Add reference; expected visible RGB/alpha unchanged while `ai_maskN` still exists and contains the generated mask channels.

Expected result:
- Build succeeds. AI Panel exposes `Add Mask` and `Replace Mask` instead of `Apply`. Add creates visible Copy/Shuffle effect rows targeting sequential `ai_maskN` RGBA planes. Replace only updates selected AI Copy/Shuffle rows. Original visible layer Color/RGBA is preserved before/after Add Mask and after save/reopen while the custom `ai_maskN` plane exists. No AI Panel operation creates Roto layer masks or Premult nodes.

## Stop Conditions
Stop and report if:
- target symbol is missing
- required fix exceeds allowed files
- validation cannot run
- existing architecture contradicts the requested change
- task requires product/design judgment not in packet
- `Node::addUserComponents` or `ImagePlaneDesc` cannot create/select stable arbitrary `ai_maskN` RGBA planes from the allowed edit surface
- the available node for `Node::addUserComponents` is not an output/channel selector node, or only arbitrary/non-selector nodes are reachable
- arbitrary channel layers are not accessible downstream through Shuffle/Copy choices after registration; fallback is to stop and report exact API/plugin limitation, not to use Color/Alpha, Roto masks, hidden masks, or Premult
- Shuffle/Copy channel knobs cannot be identified by stable names after inspecting the plugin instance
- Shuffle/Copy cannot preserve/pass through the original layer Color/RGBA unchanged while adding/writing the new `ai_maskN` plane
- Replace Mask cannot reliably identify the currently selected effect row without broad selection-model changes

## Planner Self-Check
- locator evidence sufficient: yes — anchors are present for AI Panel controls, `FluxEffect`, graph rebuild, serialization, Shuffle docs, `Node::addUserComponents`, and `ImagePlaneDesc` construction helpers.
- allowed edit files minimal and explicit: yes — six explicit GUI/timeline files cover UI entry points, visible effect model, serialization, and graph wiring; Engine files are read-only.
- read-only context minimal: yes — mandatory project docs plus Node/ImagePlaneDesc API and Shuffle plugin docs only.
- anchors/lines included: yes — each relevant location includes path, symbol, approximate lines, stable anchor, reason, and confidence.
- validation concrete: yes — build commands and focused manual checks cover Add, second channel, Replace, negative Replace, no Roto/Premult, Color/RGBA preservation before and after save/reopen, and `ai_maskN` persistence.
- parallelization decision explicit and safe: yes — single task; changes share AI Panel, timeline model/serialization, and graph rebuild state, so parallel edits would risk conflicting schema/wiring changes.
- non-goals and stop conditions sufficient: yes — explicitly prevent Roto/Premult, Engine edits, guessed Replace targets, broad AI redesign, unstable channel fallbacks, and any Shuffle path that cannot preserve original Color/RGBA while adding `ai_maskN`.
- reviewer findings addressed, if revision: yes — the plan adds `Engine/ImagePlaneDesc.h` as read-only context, states `Node::addUserComponents` is only valid on output/channel selector nodes, explicitly requires original Color/RGBA pass-through while writing `ai_maskN`, adds hard stops for non-preserving Shuffle/Copy behavior, and adds validation that visible RGB/alpha are unchanged after Add Mask and after save/reopen while `ai_maskN` exists.

## Required Return Contract
Return only a task-focused summary. Do not include transcript, tool logs, raw file dumps, large code blocks, or broad unrelated issues. Include status, files inspected/changed, validation evidence, blockers, and task-specific risks.
