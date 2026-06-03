# Planner Report

## Status
ready

## Rationale
The user goal is explicit and supersedes the previous Apply/spike path: implement a small native two-input Flux AI mask copy effect and wire AI Panel Add/Replace to visible timeline effect rows. Locator/context evidence identifies the existing AI Panel Apply entry point, Flux timeline effect model/serialization/graph rebuild, the native `RotoReplaceChannels` pattern, `Node::addUserComponents`, and AppManager built-in registration. The task is a single coherent change because node registration, timeline metadata, and AI Panel actions must agree on the same plugin ID, target plane, and source Read node.

# Task Packet

## User Goal
Implement working AI Panel `Add Mask` and `Replace Mask` for generated AI masks. `Add Mask` creates a visible effect row using a new native `FluxAIMaskCopy` node/effect. `Replace Mask` updates only the selected AI mask copy effect row. The node takes input A as the layer Color/RGBA stream and input B as the AI mask Read RGBA, preserves/passes through input A Color/RGBA, and copies input B RGBA into a custom `ai_maskN.RGBA` plane registered via `Node::addUserComponents`/`ImagePlaneDesc`. Do not use Roto, layer masks, or Premult.

## Mode
general-coding

## Relevant Locations
- file: `Engine/RotoReplaceChannels.h`
  symbol: `RotoReplaceChannels`
  approximate lines: 37-132
  stable anchor: `static EffectInstance* BuildEffect(NodePtr n)` and `virtual StatusEnum render`
  reason: native built-in EffectInstance class pattern to mirror for `FluxAIMaskCopy`.
  confidence: high
- file: `Engine/RotoReplaceChannels.cpp`
  symbol: `RotoReplaceChannels::render`
  approximate lines: 28-211
  stable anchor: `#include "Engine/RotoReplaceChannels.h"`, `addAcceptedComponents`, `render(const RenderActionArgs& args)`
  reason: rendering/copying pattern for native nodes, input image lookup, plane iteration, and ROI-safe pixel writes.
  confidence: high
- file: `Engine/EffectInstance.h`
  symbol: built-in plugin ID defines
  approximate lines: 95-105
  stable anchor: `#define PLUGINID_NATRON_ROTO_REPLACE_CHANNELS`
  reason: add a plugin ID macro for `PLUGINID_FLUX_AI_MASK_COPY` or equivalent Flux built-in ID.
  confidence: high
- file: `Engine/AppManager.cpp`
  symbol: `AppManager::loadBuiltinNodePlugins`
  approximate lines: 1538-1556
  stable anchor: `registerBuiltInPlugin<RotoReplaceChannels>(QString::fromUtf8(""), false, true);`
  reason: register the new native `FluxAIMaskCopy` built-in using the same pattern.
  confidence: high
- file: `Engine/CMakeLists.txt`
  symbol: `file(GLOB NatronEngine_HEADERS *.h)` / `file(GLOB NatronEngine_SOURCES *.cpp)`
  approximate lines: 20-24
  stable anchor: `file(GLOB NatronEngine_SOURCES *.cpp)`
  reason: confirms new `Engine/FluxAIMaskCopy.{h,cpp}` files are picked up automatically; no CMake edit expected unless build proves otherwise.
  confidence: high
- file: `Engine/Node.h`
  symbol: `Node::addUserComponents`
  approximate lines: 1268-1273
  stable anchor: `bool addUserComponents(const ImagePlaneDesc& comps);`
  reason: public API for registering custom `ai_maskN` RGBA output components on the native node.
  confidence: high
- file: `Engine/Node.cpp`
  symbol: `Node::addUserComponents`
  approximate lines: 7225-7265
  stable anchor: `Node::addUserComponents(const ImagePlaneDesc& comps)`
  reason: validates duplicate rejection and metadata refresh behavior for custom planes.
  confidence: high
- file: `Engine/ImagePlaneDesc.h`
  symbol: `ImagePlaneDesc`
  approximate lines: 64-151
  stable anchor: `ImagePlaneDesc(const std::string& planeID` and `getChannels()`
  reason: construct exact `ai_maskN` plane descriptors with `r/g/b/a` channels.
  confidence: high
- file: `Gui/FluxAiPanel.h`
  symbol: `FluxAiPanel` slots/buttons
  approximate lines: 31-100
  stable anchor: `void onApplyClicked();`
  reason: replace Apply slot/control declarations with Add Mask and Replace Mask slots/buttons.
  confidence: high
- file: `Gui/FluxAiPanel.cpp`
  symbol: `FluxAiPanel::setupUi`, `FluxAiPanel::onApplyClicked`
  approximate lines: 360-376 and 1250-1500
  stable anchor: `_applyButton = new QPushButton(tr("Apply"));`, `void FluxAiPanel::onApplyClicked()`
  reason: current old Apply/spike path does nothing useful for Nick; replace with Add/Replace calls into timeline.
  confidence: high
- file: `Gui/FluxTimeline.h`
  symbol: `FluxEffect`, `FluxLayer`, selected row APIs
  approximate lines: 90-160 and related public methods
  stable anchor: `struct FluxEffect`, `QList<FluxEffect> effects`
  reason: store visible AI mask copy effect metadata and expose Add/Replace APIs for AI Panel.
  confidence: high
- file: `Gui/FluxTimeline.cpp`
  symbol: effect model actions, duplicate/delete/serialization restore helpers, selected row handling
  approximate lines: 300-780, 930-1020, 1170-1200, 4290-4590
  stable anchor: `layer.effects`, `applyExternalMaskToSelectedRow`, `FluxTimeline::restore`
  reason: add effect-row creation/replacement logic and avoid the old external-mask/Roto/Premult path.
  confidence: high
- file: `Gui/FluxTimelineSerialization.h`
  symbol: `FluxEffectSerialization`
  approximate lines: 35-103 and 268-353
  stable anchor: `struct FluxEffectSerialization`
  reason: persist AI mask copy effect flag, target plane, source mask path, and node script names.
  confidence: high
- file: `Gui/Gui05.cpp`
  symbol: `Gui::rebuildCompositingGraph`
  approximate lines: 1696-1785 and 2277-3198
  stable anchor: `Gui::rebuildCompositingGraph(FluxTimeline* timeline)`
  reason: create/connect AI mask Read as input B and native copy node as a visible per-layer effect in effect order.
  confidence: high

## Allowed Edit Files
- `Engine/FluxAIMaskCopy.h`
- `Engine/FluxAIMaskCopy.cpp`
- `Engine/EffectInstance.h`
- `Engine/AppManager.cpp`
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
- `Engine/RotoReplaceChannels.h`
- `Engine/RotoReplaceChannels.cpp`
- `Engine/CMakeLists.txt`
- `Engine/Node.h`
- `Engine/Node.cpp`
- `Engine/ImagePlaneDesc.h`

## Required Change
1. Add a native built-in effect `FluxAIMaskCopy` in `Engine/FluxAIMaskCopy.{h,cpp}` using the `RotoReplaceChannels` `EffectInstance` pattern. Register it in `EffectInstance.h` with a stable Flux/Natron built-in plugin ID and in `AppManager::loadBuiltinNodePlugins`.
2. `FluxAIMaskCopy` must have two inputs: input 0 label `Layer`/source Color-RGBA and input 1 label `AI Mask`/mask RGBA. Input 0 is required for normal use; input 1 may be optional only if the render safely passes through input 0 when missing.
3. Add non-animated knobs/metadata needed to select the target custom plane name, e.g. `targetPlane` containing `ai_mask1`, `ai_mask2`, etc. Guard invalid names so only exact `ai_mask` plus positive integer is accepted from Flux UI.
4. During node creation/configuration, register the target RGBA plane using `Node::addUserComponents(ImagePlaneDesc(...))`. The plane ID must be exactly `ai_maskN` with channels `r`, `g`, `b`, `a`. Duplicate-plane registration should be treated as already-existing success only when the existing plane matches the requested target; otherwise fail clearly.
5. Implement render behavior: for Color/RGBA output planes, copy/pass through input A unchanged. For the target `ai_maskN` output plane, copy input B RGBA into output RGBA over the ROI. For non-target custom planes, preserve input A when available or zero-fill only when no matching input exists. Do not modify Color/RGBA alpha and do not premultiply.
6. Replace the AI Panel `Apply` button and `onApplyClicked()` path with `Add Mask` and `Replace Mask` buttons/slots. Reuse existing selected-result validation (`selectedResultMaskProjectRelative`) and clear status/log messages.
7. Add timeline APIs used by the panel: `addAIMaskCopyToSelectedLayer(relativeMask, manifestRelative, message)` and `replaceSelectedAIMaskCopy(relativeMask, manifestRelative, message)` or equivalent names. Keep logic in `FluxTimeline`; AI Panel should not directly mutate graph internals.
8. `Add Mask` must require a selected normal layer or selected effect within a layer, choose the lowest unused `ai_maskN` on that layer by scanning active AI mask copy effect metadata, create an AI mask Read node for the result, create the native `FluxAIMaskCopy` node as a visible effect row in the selected layer flow, connect upstream layer/effect stream to input A and AI Read to input B, label it `AI Mask ai_maskN`, select/show it, and never overwrite existing AI mask planes.
9. `Replace Mask` must operate only when the currently selected timeline row is an AI-created `FluxAIMaskCopy` effect row. It updates only that row's source AI Read/path/manifest while preserving the existing `ai_maskN` target, node identity where possible, and Color/RGBA pass-through behavior. If the selected row is not an AI mask copy effect, fail with a clear message equivalent to: `Replace Mask failed: select an AI Mask Copy effect row in the timeline.`
10. Extend `FluxEffect` and `FluxEffectSerialization` with minimal metadata: AI mask copy flag/type, target plane name, source mask project-relative path, source manifest project-relative path if present, AI Read node script name, and native copy node script name if not already represented by generic effect node fields.
11. Update `Gui::rebuildCompositingGraph` so AI mask copy rows rebuild like ordinary visible per-layer effects in effect order, with input A from the current layer stream and input B from the AI Read node. Do not call `applyExternalMaskToSelectedRow` for this workflow.
12. Preserve generic effect behavior, duplicate/split/save/reopen semantics, and legacy non-AI mask functionality. If duplicate/split copies visible effects, ensure AI metadata maps to copied nodes and replacement of a duplicate does not alter the original row.

## Non-Goals
- No Roto/RotoPaint creation or layer-mask workflow for AI Panel Add/Replace.
- No Premult node and no alpha replacement of the layer Color/RGBA stream.
- No OpenFX Shuffle/Copy fallback for this task; the requested solution is the new native `FluxAIMaskCopy` node.
- No broad redesign of AI generation, SAM worker, result history, model management, or prompt capture.
- No broad project/task status updates, commits, staging, or git history changes.
- No edits to `Engine/CMakeLists.txt` unless the build proves the existing glob does not pick up the new files.

## Validation
Commands:
- `cmake --build "$BUILD_DIR" --target NatronEngine -j$(nproc)`
- `cmake --build "$BUILD_DIR" --target Natron -j$(nproc)`
- `cmake --build "$BUILD_DIR" --target NatronRenderer -j$(nproc)`

Manual checks:
- Launch Flux, generate or select a real AI mask result, select a footage/normal layer, click `Add Mask`, and verify a visible `AI Mask ai_mask1` effect row appears under the layer.
- Inspect the graph/node properties enough to confirm a native `FluxAIMaskCopy` node is used, input A is the layer stream, input B is the AI Read RGBA, and no Roto/RotoPaint/FluxMask/Premult node is created by Add/Replace.
- Compare viewer/rendered Color/RGB and alpha immediately before and after Add Mask; expected visible output is unchanged while the node output exposes `ai_mask1.RGBA` populated from the AI Read.
- Add a second AI mask to the same layer; expected row/plane is `AI Mask ai_mask2` and `ai_mask1` remains unchanged.
- Select `AI Mask ai_mask1`, choose a different AI result, click `Replace Mask`; expected source Read/path updates, target remains `ai_mask1`, visible Color/RGBA remains unchanged.
- Select a normal layer or non-AI effect row and click `Replace Mask`; expected clear failure and no graph/model mutation.
- Save/reopen the project and verify AI mask copy rows, target `ai_maskN`, source path, graph wiring, and Color/RGBA pass-through persist.

Expected result:
- Build succeeds. AI Panel has working `Add Mask` and `Replace Mask`. Add creates visible native `FluxAIMaskCopy` effect rows targeting sequential custom `ai_maskN.RGBA` planes. Replace updates only the selected AI mask copy effect row. Original layer Color/RGBA remains unchanged. No Roto/layer-mask/Premult path is used.

## Stop Conditions
Stop and report if:
- target symbol is missing
- required fix exceeds allowed files
- validation cannot run
- existing architecture contradicts the requested change
- task requires product/design judgment not in packet
- `Node::addUserComponents` cannot be made to register stable `ai_maskN` RGBA planes on the native node from the allowed edit surface
- render APIs do not expose both input A and input B images for the requested output plane in the native effect
- preserving Color/RGBA while adding/writing `ai_maskN` is not possible without broader engine changes
- Replace Mask cannot reliably identify the currently selected visible AI mask copy effect row
- implementing this requires falling back to Roto/RotoPaint/layer masks/Premult/Shuffle instead of the native node

## Planner Self-Check
- locator evidence sufficient: yes — user supplied source-of-truth plus verified anchors for AI Panel Apply, timeline effects/serialization/rebuild, native `RotoReplaceChannels`, built-in registration, `Node::addUserComponents`, and `ImagePlaneDesc`.
- allowed edit files minimal and explicit: yes — ten explicit files cover the new native node, plugin ID/registration, AI Panel controls, timeline metadata/actions/serialization, and graph wiring.
- read-only context minimal: yes — mandatory docs plus native-node/API/build context only.
- anchors/lines included: yes — each relevant location includes path, symbol, approximate lines, stable anchor, reason, and confidence.
- validation concrete: yes — build targets and manual GUI/graph/image-preservation/save-reopen checks are specified.
- parallelization decision explicit and safe: yes — single task; native plugin ID, timeline schema, AI Panel buttons, and graph rebuild share state, so parallel edits would risk conflicts.
- non-goals and stop conditions sufficient: yes — explicitly forbids Roto/layer-mask/Premult/Shuffle fallback, broad AI redesign, broad git/task updates, and unsafe scope expansion.
- reviewer findings addressed, if revision: not applicable — no reviewer findings were supplied for this requested plan; prior stale plan is superseded by the new native-node source-of-truth.

## Required Return Contract
Return only a task-focused summary. Do not include transcript, tool logs, raw file dumps, large code blocks, or broad unrelated issues. Include status, files inspected/changed, validation evidence, blockers, and task-specific risks.
