# Planner Report

## Status
ready

## Rationale
The implementation surface is already isolated to the existing built-in `FluxAIMaskCopy` node and its registration. The plan keeps scope to manual nodegraph testing only: expose the node, give it Natron's normal layer/channel selector knobs, and preserve the custom `ai_maskN` plane model without touching AI Panel, RotoPaint, Roto/layer-mask/Premult workflows, or timeline wiring.

# Task Packet

## User Goal
Create a standalone custom Flux shuffle/copy node that Nick can manually test in isolation to verify it creates custom `ai_maskN` RGBA channels accessible by downstream nodes.

## Mode
general-coding

## Relevant Locations
- file: `Engine/FluxAIMaskCopy.h`
  symbol: `class FluxAIMaskCopy`
  approximate lines: 17-42
  stable anchor: `virtual std::string getPluginID() const OVERRIDE FINAL WARN_UNUSED_RETURN { return PLUGINID_FLUX_AI_MASK_COPY; }`
  reason: Built-in node declaration; add only the minimal override(s) needed for node-owned output/input layer selectors and pass-through behavior.
  confidence: high
- file: `Engine/FluxAIMaskCopy.cpp`
  symbol: `FluxAIMaskCopy::initializeKnobs`, `FluxAIMaskCopy::addAcceptedComponents`, `FluxAIMaskCopy::render`
  approximate lines: 46-150
  stable anchor: `target->setName("targetPlane");`
  reason: Current node creates `targetPlane`, registers `ai_mask1`/target user components, and copies AI Mask RGBA into target plane while passing other requested planes from input 0.
  confidence: high
- file: `Engine/AppManager.cpp`
  symbol: `AppManager::loadBuiltinNodePlugins` built-in registrations
  approximate lines: 1549-1554
  stable anchor: `registerBuiltInPlugin<FluxAIMaskCopy>(QString::fromUtf8(""), false, true);`
  reason: Node is registered as internal-use-only; isolated manual node search/testing requires changing only this registration visibility.
  confidence: high
- file: `Engine/Node.cpp`
  symbol: `Node::initializeDefaultKnobs`, `Node::createChannelSelectors`, `Node::Implementation::createChannelSelector`
  approximate lines: 2221-2301 and 2449-2502
  stable anchor: `bool requiresLayerShuffle = _imp->effect->getCreateChannelSelectorKnob();`
  reason: Natron creates input/output layer selector knobs, including host-addable `outputLayer`, only when the effect reports `getCreateChannelSelectorKnob()` true.
  confidence: high
- file: `Engine/EffectInstance.h`
  symbol: `PLUGINID_FLUX_AI_MASK_COPY`, `getCreateChannelSelectorKnob`, `isPassThroughForNonRenderedPlanes`
  approximate lines: 100-103, 451-453, 901-904
  stable anchor: `#define PLUGINID_FLUX_AI_MASK_COPY`
  reason: Confirms plugin ID and the virtual hooks available for enabling selectors/pass-through semantics.
  confidence: high
- file: `Engine/CMakeLists.txt`
  symbol: `NatronEngine_SOURCES`
  approximate lines: 22-75
  stable anchor: `file(GLOB NatronEngine_SOURCES *.cpp)`
  reason: `FluxAIMaskCopy.cpp` is discovered by configure-time glob; validation must account for CMake reconfigure if this file was added after the current build tree was configured.
  confidence: high
- file: `Documentation/source/plugins/net.sf.openfx.ShufflePlugin.rst`
  symbol: Shuffle node controls
  approximate lines: 18-32
  stable anchor: `Output Layer / ``outputLayer```
  reason: Reference for expected downstream/manual behavior: an output layer selector exposes selectable planes for channel operations.
  confidence: medium

## Allowed Edit Files
- `Engine/FluxAIMaskCopy.h`
- `Engine/FluxAIMaskCopy.cpp`
- `Engine/AppManager.cpp`

## Read-Only Context Files
- `Engine/EffectInstance.h`
- `Engine/Node.cpp`
- `Engine/Node.h`
- `Engine/CMakeLists.txt`
- `Documentation/source/devel/PythonReference/NatronEngine/Effect.rst`
- `Documentation/source/plugins/net.sf.openfx.ShufflePlugin.rst`

## Required Change
1. In `Engine/FluxAIMaskCopy.h`, add the minimal virtual override(s) required for this custom node to receive Natron's normal channel/layer selector UI:
   - Override `getCreateChannelSelectorKnob()` to return `true`.
   - If needed for correct non-target plane forwarding, override `isPassThroughForNonRenderedPlanes()` to an existing pass-through mode consistent with `render()` handling requested planes; do not change global Node behavior.
2. In `Engine/FluxAIMaskCopy.cpp`, keep the existing `targetPlane` knob and render semantics, but make the custom plane registration robust for isolated manual tests:
   - Continue accepting/pass-throughing standard Color RGBA/RGB/Alpha and custom `ai_maskN` RGBA planes.
   - Ensure `ai_mask1` is always registered with `node->addUserComponents(...)` during initialization/accepted component discovery.
   - Ensure the currently valid `targetPlane` value is registered as a user component when it differs from `ai_mask1`.
   - Do not introduce native Roto, RotoPaint, layer-mask, Premult, timeline, or AI Panel dependencies.
   - Do not rename the `targetPlane` script name or plugin ID.
3. In `Engine/AppManager.cpp`, change only the `FluxAIMaskCopy` built-in registration so the node is visible/searchable for manual nodegraph testing. Keep it in the Flux grouping and do not alter nearby Roto/RotoPaint registrations.
4. Do not edit `Engine/CMakeLists.txt` unless implementation/build evidence proves the existing configure-time glob cannot see `Engine/FluxAIMaskCopy.cpp` after reconfigure. If a build tree predates this file, re-run CMake configuration instead of adding broad source-list churn.

## Non-Goals
- No AI Panel Add Mask/Replace Mask workflow wiring.
- No timeline/layer stack integration.
- No native RotoPaint changes or hijacking.
- No Roto, layer-mask, mask input, or Premult workflow changes.
- No global changes to `Node`, `EffectInstance`, CMake policy, plugin discovery, or downstream node behavior.
- No git restore/reset/checkout/revert, commits, or history/state changes.
- No ad-hoc scripts to rewrite source.

## Validation
Commands:
- `cmake --build <existing-build-dir> --target NatronEngine -j$(nproc)`
- If the build tree was configured before `Engine/FluxAIMaskCopy.cpp` existed and the target cannot find/link the node, run the project's existing CMake configure command for that build directory, then rerun: `cmake --build <existing-build-dir> --target NatronEngine -j$(nproc)`
- `cmake --build <existing-build-dir> --target Natron -j$(nproc)`

Expected result:
- Build validation: `NatronEngine` and `Natron` compile/link without errors after the minimal node/header/registration edits.
- Manual GUI/nodegraph validation for Nick, separate from build validation:
  1. Launch Flux/Natron from the validated build.
  2. Open the node search dialog and confirm `FluxAIMaskCopy` is visible under/searchable as Flux, not hidden as internal-use-only.
  3. Create a normal source/read node for Layer input 0 and an RGBA mask source/read node for AI Mask input 1.
  4. Create `FluxAIMaskCopy`, set `targetPlane` to `ai_mask1`, and connect Layer to input 0 and mask source to input 1.
  5. In the node's settings, confirm an Output Layer/channel selector exists and can expose/select the custom `ai_mask1.RGBA` plane.
  6. Add a downstream Shuffle/Shuffle-like node and verify `ai_mask1.r`, `ai_mask1.g`, `ai_mask1.b`, and `ai_mask1.a` are available as selectable channels/planes downstream.
  7. Change `targetPlane` to another valid value such as `ai_mask2`, force refresh/reopen settings if needed, and verify the downstream selector can see `ai_mask2.RGBA` without breaking `ai_mask1` availability.
  8. Confirm the non-target Layer/Color stream still passes through visually when output/viewing Color, and the target custom plane contains the AI Mask RGBA data when selected downstream.

Adjacent regression checklist:
- Confirm native `RotoPaint`, `Roto`, `RotoSmear`, and `RotoReplaceChannels` nodes still appear/register as before.
- Confirm no AI Panel Add/Replace Mask UI behavior changed in this packet.
- Confirm no layer-mask/Roto/Premult nodes are added to the test graph by the implementation.
- Confirm invalid `targetPlane` strings still fall back safely to `ai_mask1` rather than creating arbitrary plane names.
- Confirm existing projects without `FluxAIMaskCopy` still load to the node graph without plugin registration warnings introduced by this change.

## Stop Conditions
Stop and report if:
- target symbol is missing
- required fix exceeds allowed files
- validation cannot run
- existing architecture contradicts the requested change
- task requires product/design judgment not in packet
- enabling `getCreateChannelSelectorKnob()` requires broad edits to `Node.cpp`, global channel selector behavior, or downstream node internals
- exposing the node for manual testing requires changing plugin IDs, categories outside Flux, or AI Panel workflow code
- the CMake/build issue cannot be solved by reconfigure or the allowed files without broad build-system changes

## Planner Self-Check
- locator evidence sufficient: yes — high-confidence anchors identify the node implementation, plugin registration, channel selector creation path, plugin ID, and CMake glob behavior.
- allowed edit files minimal and explicit: yes — only the existing custom node header/source and its registration are editable.
- read-only context minimal: yes — Node/Effect/CMake/docs are context only and separated from editable files.
- anchors/lines included: yes — relevant locations include paths, symbols, approximate lines, stable anchors, reasons, and confidence.
- validation concrete: yes — build commands are explicit with CMake reconfigure caveat, and Nick's manual GUI/nodegraph validation is separated from build validation.
- parallelization decision explicit and safe: yes — single task; edits are interdependent in one node and one registration line, so parallelization would add conflict risk without benefit.
- non-goals and stop conditions sufficient: yes — they explicitly block AI Panel workflow broadening, RotoPaint hijack, layer-mask/Roto/Premult misuse, global Node changes, and git/source-rewrite side effects.
- reviewer findings addressed, if revision: not applicable — no previous plan/reviewer findings were supplied.

## Required Return Contract
Return only a task-focused summary. Do not include transcript, tool logs, raw file dumps, large code blocks, or broad unrelated issues. Include status, files inspected/changed, validation evidence, blockers, and task-specific risks.
