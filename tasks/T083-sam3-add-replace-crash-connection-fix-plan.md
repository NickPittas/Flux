# Planner Report

## Status
split-required

## Why Split / Parallelize
This emergency fix has two related but sequentially safer units: first make project-load serialization tolerant of pre-AI projects, current unversioned T083 projects that already contain AI fields, and legacy FluxMask external/premultiply fields; then verify/rebuild the Add/Replace AI mask graph connections using the now-safe restored model. They must not run in parallel because Task B depends on restored `FluxEffect::isAIMaskCopy` and AI mask read-node metadata being preserved by Task A.

## Interference Check
- parallel safe: no
- shared files or generated outputs: both tasks affect save/load runtime state; Task B reads restored AI mask metadata from Task A
- shared validation state: same Flux project files and GUI/manual validation session
- worktree isolation required: no, but execute sequentially in one worktree
- rationale: Task A is prerequisite archive compatibility; Task B then edits graph creation/rebuild paths and validates Add/Replace against a stable serialization model.

## Proposed Task Sequence Or Parallel Batch
1. Task name: Serialization compatibility emergency guard
   - purpose: prevent previous Flux projects from crashing while preserving current unversioned T083 projects that already saved AI effect fields
   - allowed files:
     - `Gui/FluxTimelineSerialization.h`
   - validation: build, open a pre-T083/legacy mask project, save/reopen a current unversioned T083 project containing an AI Mask Copy effect
   - can run in parallel with: none
2. Task name: Add/Replace AI Mask graph connection verification
   - purpose: ensure SAM3 Add Mask and Replace Mask create/retain native `FluxAIMaskCopy` nodes with connected input0/source and input1/mask read nodes, without old Roto/layer-mask/Premult behavior
   - allowed files:
     - `Gui/FluxTimeline.cpp`
     - `Gui/Gui05.cpp`
     - `Gui/FluxAiPanel.cpp`
   - validation: build, run Add Mask and Replace Mask, verify node graph `FluxAIMaskCopy` input A/input0 and B/input1 are connected, no Roto/Premult nodes are created
   - can run in parallel with: none

## Task Packets

# Task Packet A — Serialization compatibility emergency guard

## User Goal
Fix Nick-reported blocker where loading previous Flux projects can crash after T083 SAM3/AI-mask changes. Source of truth: old Apply-to-layer-mask workflow is gone; no Roto/layer mask/Premult should be resurrected. Old projects must not crash, and current unversioned T083 project files that already contain AI fields must not lose or misalign their AI-mask metadata.

## Mode
general-coding

## Relevant Locations
- file: `Gui/FluxTimelineSerialization.h`
  symbol: `struct FluxEffectSerialization::serialize`
  approximate lines: 35-68
  stable anchor: `ar & ::boost::serialization::make_nvp("IsAIMaskCopy", isAIMaskCopy);`
  reason: T083 AI mask fields are currently serialized unconditionally in an unversioned class; this preserves current unversioned T083 archives but can throw on pre-AI archives if fields are absent.
  confidence: high
- file: `Gui/FluxTimelineSerialization.h`
  symbol: `struct FluxMaskSerialization::serialize`
  approximate lines: 71-96
  stable anchor: `void serialize(Archive & ar, const unsigned int version)` followed by current fields `Name`, `Type`, `PluginId`, `Enabled`, `Inverted`, `EffectIndex`, `MaskNode`, `ReformatNode`
  reason: compatibility point for legacy FluxMask fields; retired external/premultiply fields must be consumed/ignored in the historical class-version order without rebuilding old Roto/Premult workflows.
  confidence: high
- file: `Gui/FluxTimelineSerialization.h`
  symbol: `BOOST_CLASS_VERSION(NATRON_NAMESPACE::FluxTimelineSerialization, 2)`
  approximate lines: 428-430
  stable anchor: exact line currently present near EOF
  reason: still-present class-version anchor for the top-level timeline; do not disturb unless needed for a proven archive migration.
  confidence: high
- file: `Gui/FluxTimelineSerialization.h`
  symbol: `BOOST_CLASS_VERSION(NATRON_NAMESPACE::FluxLayerSerialization, 3)`
  approximate lines: 428-430
  stable anchor: exact line currently present near EOF
  reason: still-present layer class-version anchor/order; do not infer FluxEffect versioning from the layer version.
  confidence: high
- file: `Gui/FluxTimelineSerialization.h`
  symbol: `BOOST_CLASS_VERSION(NATRON_NAMESPACE::FluxMaskSerialization, 3)`
  approximate lines: 428-430
  stable anchor: exact line currently present near EOF, after FluxTimeline and FluxLayer version macros
  reason: confirms current FluxMask class version is 3 and supplies the version gates for consuming retired legacy NVPs.
  confidence: high
- file: `Gui/ProjectGuiSerialization.h`
  symbol: `ProjectGuiSerialization::load`
  approximate lines: 786-796
  stable anchor: `ar & ::boost::serialization::make_nvp("FluxTimeline", _fluxTimeline);`
  reason: read-only context showing Flux timeline data is loaded through Boost XML as one nested object; if exact dual-schema handling cannot be implemented in the nested serializer, the worker must stop for representative XML samples instead of guessing.
  confidence: medium

## Allowed Edit Files
- `Gui/FluxTimelineSerialization.h`

## Read-Only Context Files
- `Gui/ProjectGuiSerialization.h` — load/save path for `FluxTimeline`, approximate lines 720-796.
- `Gui/FluxTimeline.cpp` — restore/save call sites around `toSerialization()`/`fromSerialization()` and AI mask field mapping, approximate lines 4574-4707.

## Required Change
Implement a narrow Boost-compatible serialization migration in `Gui/FluxTimelineSerialization.h` only:
1. Do **not** implement the previously proposed naive `BOOST_CLASS_VERSION(FluxEffectSerialization, 1)` plus `if (version >= 1)` guard by itself. Existing current T083 archives were saved while `FluxEffectSerialization` was unversioned, so they may load as version 0 while still containing `IsAIMaskCopy`, `AIMaskTargetPlane`, `AIMaskSourceRelativePath`, `AIMaskManifestRelativePath`, and `AIMaskReadNode`. Guarding those fields out for version 0 would drop/misalign AI metadata.
2. Preserve current unversioned T083 AI-field archives. Acceptable strategy: keep the AI effect fields in the unversioned/default load path and rely on safe constructor defaults (`isAIMaskCopy=false`, strings empty) for saves that have those NVPs; only add class versioning if the implementation demonstrably still reads both existing unversioned-with-AI and future versioned files.
3. Also support older pre-AI projects that do not contain the AI effect NVPs. If Boost XML cannot safely make these fields optional inside `FluxEffectSerialization` without corrupting archive state, stop and report that exact old/current XML samples are required. Do not ship a guess that only supports one schema.
4. For pre-AI compatibility, prefer a proven Boost-compatible optional-field technique: split save/load if needed; on save always emit the current AI fields; on load either read current AI NVPs when present or leave defaults when absent. The worker must verify this against actual XML/project samples or a minimal reproducible archive test before claiming success.
5. In `FluxMaskSerialization::serialize`, consume/ignore retired legacy `FluxMask` fields using local dummy variables and the current class version gates/order. Keep the currently present fields first in their current order: `Name`, `Type`, `PluginId`, `Enabled`, `Inverted`, `EffectIndex`, `MaskNode`, `ReformatNode`. Then for `version >= 2`, consume/ignore exactly these legacy NVPs in this historical order: `ExternalMaskPathProjectRelative`, `ExternalReadNode`, `ExternalShuffleNode`. Then for `version >= 3`, consume/ignore exactly this legacy NVP: `PremultiplyAlpha`. Do not add those fields back to the live data model and do not create Roto/layer-mask/Premult behavior.
6. Stop instead of guessing if a representative legacy FluxMask archive contradicts the NVP names or order in item 5. The worker may adjust only if the actual archive evidence proves a different order/name and must report that evidence in the return contract.
7. Preserve existing current-project save/load field names for all non-legacy fields.
8. Do not edit project task status files or perform git operations.

## Non-Goals
- Do not restore old Apply-to-layer-mask UI or graph behavior.
- Do not create Roto, Premult, Unpremult, or layer mask nodes.
- Do not change AI mask graph wiring; that is Task B.
- Do not broaden serialization changes outside the listed file unless reporting a blocker for orchestrator approval.

## Validation
Commands:
- `cmake --build build --target Natron -j$(nproc)`
- Manual GUI or minimal Boost archive test: load a pre-T083 project/archive whose `FluxEffectSerialization` entries do not contain AI fields; confirm no crash and defaults are applied.
- Manual GUI or minimal Boost archive test: load a current unversioned T083 project/archive whose `FluxEffectSerialization` entries do contain AI fields; confirm AI fields are preserved.
- Manual GUI: load a legacy FluxMask project/archive containing retired NVPs `ExternalMaskPathProjectRelative`, `ExternalReadNode`, `ExternalShuffleNode`, and `PremultiplyAlpha`; confirm those fields are consumed/ignored and no Roto/Premult/layer-mask behavior is restored.

Expected result:
Build passes. Old pre-AI projects load without archive exceptions/crash. Current unversioned T083 projects with AI mask copy effects preserve AI fields. Legacy FluxMask external/premult fields do not break load and are ignored. No Roto/Premult/layer-mask behavior is reintroduced.

## Stop Conditions
Stop and report if:
- target serialization structs or class-version anchors are missing
- required fix exceeds `Gui/FluxTimelineSerialization.h`
- validation cannot run
- actual old/current project XML samples are unavailable and Boost optional-field handling cannot be proven safely
- a representative legacy archive contradicts retired FluxMask NVP names/order (`ExternalMaskPathProjectRelative`, `ExternalReadNode`, `ExternalShuffleNode`, then `PremultiplyAlpha`)
- existing Boost archive behavior contradicts dual support for pre-AI missing fields and unversioned T083 present fields
- task requires product/design judgment not in packet

## Planner Self-Check
- locator evidence sufficient: yes — exact serialization structs, current AI NVP anchor, FluxMask serializer, and class-version/order anchors were inspected; reviewer-supplied legacy NVP names/order are included.
- allowed edit files minimal and explicit: yes — one header for Task A.
- read-only context minimal: yes — only project GUI load path and timeline save/restore mapping are needed as context.
- anchors/lines included: yes — structs, current AI-field NVP anchor, FluxMask serializer, and exact version macro anchors listed.
- validation concrete: yes — build plus old pre-AI, current unversioned T083, and legacy FluxMask archive/project load checks.
- parallelization decision explicit and safe: yes — sequential; Task B depends on Task A.
- non-goals and stop conditions sufficient: yes — explicitly forbids old Roto/layer-mask/Premult resurrection and requires stopping for samples if exact dual-schema support cannot be proven or legacy archive order contradicts locator evidence.
- reviewer findings addressed, if revision: yes — Task A now names and orders retired FluxMask NVPs, includes exact still-present class-version anchors, forbids naive AI version gating, preserves current unversioned T083 AI archives, and keeps Task B sequential.

## Required Return Contract
Return only a task-focused summary. Do not include transcript, tool logs, raw file dumps, large code blocks, or broad unrelated issues. Include status, files inspected/changed, validation evidence, blockers, and task-specific risks.

---

# Task Packet B — Add/Replace AI Mask graph connection verification

## User Goal
Fix Nick-reported blocker where SAM3 Add Mask / Replace Mask creates unconnected nodes. Source of truth: old Apply-to-layer-mask is gone; no Roto/layer mask/Premult. Add/Replace must work so Nick can test, while keeping the native `FluxAIMaskCopy` node.

## Mode
general-coding

## Relevant Locations
- file: `Gui/FluxTimeline.cpp`
  symbol: `FluxTimeline::addAIMaskCopyToSelectedLayer`
  approximate lines: 1155-1234
  stable anchor: `Q_EMIT compositingChanged();`
  reason: creates AI mask Read and native `FluxAIMaskCopy`, appends effect metadata, then relies on async graph rebuild; currently no synchronous postcondition verifies input wiring.
  confidence: high
- file: `Gui/FluxTimeline.cpp`
  symbol: `FluxTimeline::replaceSelectedAIMaskCopy`
  approximate lines: 1237-1284
  stable anchor: `if (effect.aiMaskReadNode) effect.aiMaskReadNode->deactivate(std::list<NodePtr>(), false, true);`
  reason: swaps mask Read source and emits graph rebuild, but currently does not synchronously verify the copy node input1 is reconnected.
  confidence: high
- file: `Gui/Gui05.cpp`
  symbol: `Gui::rebuildCompositingGraph` AI-mask effect handling
  approximate lines: 2634-2671
  stable anchor: `if (effect.isAIMaskCopy && effect.node->getNInputs() > 1)`
  reason: graph rebuild creates/restores mask Read node, adds user components/target knob, connects input0 from layer source/effect chain and input1 from AI mask Read.
  confidence: high
- file: `Gui/FluxAiPanel.cpp`
  symbol: `FluxAiPanel::onAddMaskClicked` and `FluxAiPanel::onReplaceMaskClicked`
  approximate lines: 1267-1305
  stable anchor: `appendLog(message.isEmpty() ? QString::fromUtf8("Add Mask failed.") : message);`
  reason: panel is the user-facing Add/Replace path; should surface new connection/rebuild verification logs/failures.
  confidence: high
- file: `Engine/FluxAIMaskCopy.cpp`
  symbol: `FluxAIMaskCopy::render`
  approximate lines: 95-140
  stable anchor: `args.inputImages.find(1)`
  reason: confirms native node expects input0 as pass-through source and input1 as mask source for target AI mask plane.
  confidence: high

## Allowed Edit Files
- `Gui/FluxTimeline.cpp`
- `Gui/Gui05.cpp`
- `Gui/FluxAiPanel.cpp`

## Read-Only Context Files
- `Engine/FluxAIMaskCopy.cpp` — render input contract only; do not edit.
- `Gui/FluxTimelineSerialization.h` — AI mask fields/restore context from Task A; do not edit in Task B.

## Required Change
Implement a narrow connection-verification fix for Add/Replace:
1. Preserve the native `PLUGINID_FLUX_AI_MASK_COPY` / `FluxAIMaskCopy` node path. Do not switch back to Roto/Premult/layer-mask graph creation.
2. After Add Mask and Replace Mask create/update `effect.aiMaskReadNode`, ensure `FluxAIMaskCopy` is connected to the layer/effect chain input0 and AI mask Read input1. Prefer calling the existing compositing rebuild synchronously or adding a small existing-pattern helper that rebuilds/verifies immediately, rather than relying only on queued signal delivery.
3. In `Gui::rebuildCompositingGraph`, make AI mask reconnection robust: disconnect/reconnect input0 and input1 as needed, verify `effect.node->getInput(0) == sourceInput` and `effect.node->getInput(1) == effect.aiMaskReadNode` when required, and log clear `FLUX ERROR`/`FLUX INFO` messages with layer/effect indices and node labels.
4. If Add/Replace cannot verify connections, return failure to the AI panel message path and leave the user with an actionable log. Avoid deactivating the previous replacement Read until the replacement path can be connected or recover cleanly.
5. Keep graph output semantics unchanged: `FluxAIMaskCopy` remains a main-pipe effect node that copies source input0 to outputs and copies mask input1 into the target `ai_maskN` plane.
6. Do not edit `Engine/FluxAIMaskCopy.cpp` unless a reviewer/orchestrator explicitly authorizes it; its input contract is read-only context for this emergency fix.

## Non-Goals
- Do not restore Apply-to-layer-mask.
- Do not create Roto, RotoPaint, Premult, Unpremult, or layer-mask nodes for Add/Replace.
- Do not alter SAM3 model execution, generated file formats, or prompt UX.
- Do not change task tracking docs or git state.

## Validation
Commands:
- `cmake --build build --target Natron -j$(nproc)`
- Manual GUI: open a project with one normal footage/text/solid layer, run SAM3 result path, click Add Mask.
- Manual GUI/node graph: verify the created native `FluxAIMaskCopy` node has input A/input0 connected to the layer/effect-chain source and input B/input1 connected to the AI mask Read node.
- Manual GUI: select the AI Mask Copy effect row, run/select another SAM3 result, click Replace Mask.
- Manual GUI/node graph: verify the same/native AI Mask Copy node remains, input0 remains connected to source, input1 points to the replacement AI mask Read, and no Roto/Premult/layer-mask nodes are created.
- Manual GUI: save/reopen after Add/Replace and verify connections survive rebuild/load.

Expected result:
Build passes. Add Mask and Replace Mask produce a connected native `FluxAIMaskCopy` effect in the layer chain. The AI panel reports success only after connection verification. Node graph contains no resurrected Roto/Premult/layer-mask Apply-to-layer-mask artifacts.

## Stop Conditions
Stop and report if:
- target Add/Replace/rebuild anchors are missing
- required fix needs files outside allowed edits
- validation cannot run
- existing architecture contradicts native `FluxAIMaskCopy` input0/input1 semantics
- task requires product/design judgment not in packet

## Planner Self-Check
- locator evidence sufficient: yes — provided and existing plan anchors cover Add/Replace, rebuild, and render input contract locations.
- allowed edit files minimal and explicit: yes — three GUI files for creation/rebuild/logging; engine file is read-only.
- read-only context minimal: yes — only native render contract and Task A serialization context.
- anchors/lines included: yes — exact functions and approximate line ranges listed.
- validation concrete: yes — build plus GUI node graph input A/B checks and save/reopen check.
- parallelization decision explicit and safe: yes — sequential after Task A due shared serialization/restore state.
- non-goals and stop conditions sufficient: yes — explicitly blocks old Roto/layer-mask/Premult and scope creep.
- reviewer findings addressed, if revision: yes — Task B remains sequential after Task A and depends on corrected serialization compatibility.

## Required Return Contract
Return only a task-focused summary. Do not include transcript, tool logs, raw file dumps, large code blocks, or broad unrelated issues. Include status, files inspected/changed, validation evidence, blockers, and task-specific risks.
