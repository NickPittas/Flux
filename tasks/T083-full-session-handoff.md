# T083 Full Session Handoff — SAM3 / AI Paint / Custom Plane Mask Workflow

**Date:** 2026-05-28
**Workspace:** `/home/npittas/Flux`
**Dev binary:** `/home/npittas/Flux/build/App/Natron`
**Current task:** T083 — AI matte/mask generation and video depth tools
**Current status:** IN_PROGRESS / recovery and workflow-correction phase

This is the single-file handoff for a new Pi session. Read this file after the mandatory project docs listed below. It captures what was done, what is currently true, what must not regress, and what to do next.

---

## 0. Mandatory first reads for any new session

Before taking action, every new session must read:

1. `/home/npittas/.pi/agent/COLLABORATION.md`
2. `/home/npittas/.pi/agent/ORCHESTRATOR_OPERATING_RULES.md`
3. `/home/npittas/Flux/AGENTS.md`
4. `/home/npittas/Flux/ARCHITECTURE.md`
5. `/home/npittas/Flux/plans/PHASES.md`
6. `/home/npittas/Flux/tasks/TASKS.md`
7. This handoff: `/home/npittas/Flux/tasks/T083-full-session-handoff.md`
8. T083 source-of-truth docs listed in section 6.

**Important:** The Flux project rules require reading `AGENTS.md`, `ARCHITECTURE.md`, `plans/PHASES.md`, and `tasks/TASKS.md` on every user request. Do not rely on memory.

---

## 1. Operating rules that matter most right now

Nick's direct instructions are authoritative.

Do not use these recovery shortcuts unless Nick explicitly asks:

- no `git restore`
- no `git checkout -- file`
- no `git reset`
- no revert-as-recovery
- no ad-hoc Python/shell scripts to rewrite source files
- no broad git staging or commits without explicit approval

For non-trivial coding work in this session, use the codemap orchestrator flow:

1. Locate narrowly.
2. Plan narrowly.
3. Review the plan.
4. Delegate to scoped worker or make only trivial exact edits if explicitly safe.
5. Review implementation.
6. Validate with build/runtime evidence.

Reviews must verify user workflow and adjacent regressions, not just code/build.

For user-facing GUI behavior, **screenshot/recording or it is unvalidated**.

---

## 2. Project phase context

Current phase from `plans/PHASES.md` / `tasks/TASKS.md`:

- P0–P6 are DONE.
- P7 Shapes + Text is IN_PROGRESS.
- T083 AI matte/mask generation and video depth tools is IN_PROGRESS.
- T082 Adobe Illustrator import is still PENDING.
- T080/T081 FluxMotionText animator workflow was manually accepted by Nick.

T083 is being developed during P7 but touches AI, viewer overlays, worker processes, timeline effect rows, serialization, and generated project-relative media.

---

## 3. Current T083 product direction

Approved direction:

- CUDA/PyTorch AI runs out-of-process through external Python workers.
- No Python/CUDA in the Natron render thread.
- SAM3/SAM3.1 is user-downloadable/gated; not bundled by default.
- AI outputs are generated raster media under project-relative paths.
- Unsaved projects must Force Save As before AI generation.
- AI prompt ownership is viewer/AI Paint-owned, not arbitrary AI Panel capture.
- AI previews go to the dedicated AI Work Viewer / AI Paint overlay paths, not the main comp viewer.
- Native RotoPaint must remain native and protected.
- AI masks must not use the Flux layer-mask/Roto/Premult path.
- Layer masks are for Roto/RotoPaint application only.
- If a user wants Premult, they add Premult manually.

Current key workflow split:

1. **AI Paint / SAM3 prompt and preview workflow**
   - AI Paint owns point/box prompt tools and live overlay.
   - AI Panel can run SAM3 and manage results/history.
   - Persistent SAM3 worker is the runtime path.

2. **AI result application workflow**
   - Old Apply-to-layer-mask path was wrong and has been unwired.
   - Correct workflow is Add Mask / Replace Mask.
   - Add Mask creates visible timeline effect rows using custom channels.
   - Replace Mask updates only the selected AI mask copy row.
   - No Roto/layer-mask/Premult nodes for AI result application.

3. **Standalone nodegraph workflow**
   - `Flux Custom Plane Copy` exists as a normal visible node.
   - User can manually create arbitrary safe custom planes like `hair`, `holdout_01`, `_matte`.
   - Flux AI workflow can still use `ai_mask1`, `ai_mask2`, etc. to find managed masks.

---

## 4. What was completed before this handoff

### 4.1 Native RotoPaint restored/protected

Native RotoPaint is not the AI prompt node. It must not be hijacked.

AI Paint is separate:

- `Engine/AIPaint.h`
- `Engine/AIPaint.cpp`
- `Engine/AIPaintContext.h`
- `Engine/AIPaintContext.cpp`

Important plugin IDs:

- `PLUGINID_NATRON_ROTOPAINT`
- `PLUGINID_NATRON_AIPAINT`

### 4.2 AI Paint node and tools

Implemented/working areas:

- AI Paint node exists separately from native RotoPaint.
- AI Select / AI Point / AI Box tools.
- AI prompt select/delete/clear.
- Live overlay drawn via `AIPaint::drawOverlay`.
- Main comp viewer is not rewired for AI preview.

Relevant files:

- `Engine/AIPaint.*`
- `Engine/AIPaintContext.*`
- `Gui/ViewerGL.*`
- `Gui/ViewerTab.*`
- `Gui/NodeViewerContext.cpp`

### 4.3 Persistent SAM3 worker and runtime path repaired

Persistent JSON-lines worker path is in use:

- `tools/ai/sam3_transformers_worker.py`
- `tools/ai/sam3_transformers_real_inference_probe.py`
- `tools/ai/flux_provider_runtime.py`

C++ integration points:

- `FluxAiPanel::ensureSam3WorkerStarted`
- `FluxAiPanel::sendSam3WorkerRequest`
- `FluxAiPanel::processSam3WorkerLine`
- `FluxAiPanel::previewSam3RunResult`
- `FluxAiPanel::refreshSourceFrameMetadata`

Manual validation already confirmed by Nick before the current custom-plane work:

- SAM3 worker starts:
  `/home/npittas/.local/share/Flux/ai-envs/sam3/venv/bin/python /home/npittas/Flux/tools/ai/sam3_transformers_worker.py`
- Worker ready/loaded.
- Live preview updates.
- AI Panel Run generates manifest, example:
  `FluxGenerated/AI/base-matte/sam3_transformers/run-20260528-231348/result_manifest.json`
- AI result previews in AI Work Viewer.

Builds passed after recovery:

- `cmake --build /home/npittas/Flux/build --target Natron -j$(nproc)`
- Python smoke:
  - `python3 -m py_compile tools/ai/sam3_transformers_worker.py tools/ai/sam3_transformers_real_inference_probe.py tools/ai/flux_provider_runtime.py`
  - `python3 tools/ai/flux_provider_runtime.py status sam3 --json`
  - JSON-lines worker ready/status/shutdown smoke.

### 4.4 Source-frame 0 regression fixed

Reported runtime failure was:

```text
FLUX-SAM3-A1 capture blocked: source-frame export unavailable: stored source frame 0 is outside original range [1,81]
```

Fix implemented:

- `FluxAiPanel::refreshSourceFrameMetadata()` now reacquires current `FluxTimeline`, verifies selected/stored source identity, recomputes `sourceFrame = currentFrame - layer.timeOffset`, clamps to original media range, and updates state/label before export.

Relevant files:

- `Gui/FluxAiPanel.cpp`
- `Gui/Gui05.cpp`

### 4.5 Qt6 QProcess signal warning fixed in edited paths

Reported warning:

```text
QObject::connect: No such signal QProcess::error(QProcess::ProcessError)
```

Fixed via typed Qt6 signal connections using `&QProcess::errorOccurred` in edited paths.

Relevant files:

- `Gui/FluxAiPanel.cpp`
- `Gui/FluxAiWorkerController.cpp`

### 4.6 AI Panel result history and preview

Implemented/verified areas:

- Result history.
- Preview Again.
- Remove Entry button.
- Metadata tooltips.
- Do not delete generated files when removing history entries.
- Generated AI outputs/history remain project-relative.

Relevant files:

- `Gui/FluxAiPanel.h`
- `Gui/FluxAiPanel.cpp`
- `Gui/Gui05.cpp`

### 4.7 AI Panel layout

AI Panel was made scrollable/non-compacted using `QScrollArea`.

Relevant files:

- `Gui/FluxAiPanel.cpp`
- `Gui/FluxAiPanel.h`

### 4.8 Wrong AI Apply path unwired

Old incorrect behavior was removed/unwired:

- No AI Apply-to-layer-mask.
- Removed external mask fields from unversioned effect serialization path.
- Removed Premultiply Mask Alpha UI/serialization for this AI path.
- Native Roto/RotoPaint layer mask path preserved.

Relevant files:

- `Gui/FluxTimeline.cpp`
- `Gui/FluxTimelineSerialization.h`
- `Gui/Gui05.cpp`

### 4.9 Serialization emergency compatibility

Implemented in `Gui/FluxTimelineSerialization.h`:

- Removed AI fields from unversioned `FluxEffectSerialization::serialize`.
- Added versioned `FluxEffectAIMaskSerialization`.
- Moved AI mask metadata into `FluxLayerSerialization` version 4.
- Restored consume-only legacy fields in `FluxMaskSerialization`:
  - `ExternalMaskPathProjectRelative`
  - `ExternalReadNode`
  - `ExternalShuffleNode`
  - `PremultiplyAlpha`

Purpose: old project load should not crash, and old wrong AI-mask fields are consumed safely.

Runtime old-project save/reopen still needs explicit validation before claiming done.

---

## 5. Current focused work: custom channel/plane node

### 5.1 Why this node exists

Nick asked to determine whether Natron can create/write custom channels, not just read them.

Findings:

- Natron supports image layers/channels and user/custom planes.
- Python API docs: `Effect.addUserPlane(planeName, channels)` adds a plane to a node Channels selector.
- Source: `Node::addUserComponents(...)` only works when the node has a channel/output selector.
- Stock Shuffle has output layer controls, but it was uncertain whether stock nodes preserve all planes and create arbitrary custom planes in the way needed.

Decision:

- Create a dedicated native Flux custom plane copy/shuffle node.
- Use it both for managed AI masks and as a standalone user node.

### 5.2 Node name and plugin identity

Current visible label:

- `Flux Custom Plane Copy`

Current plugin ID remains unchanged for compatibility:

- `PLUGINID_FLUX_AI_MASK_COPY`
- Defined in `Engine/EffectInstance.h`

Current source files:

- `Engine/FluxAIMaskCopy.h`
- `Engine/FluxAIMaskCopy.cpp`

Registration:

- `Engine/AppManager.cpp`
- `registerBuiltInPlugin<FluxAIMaskCopy>(QString::fromUtf8(""), false, false);`
- It was changed from internal-use-only to visible/searchable so Nick can test it manually.

CMake:

- `Engine/CMakeLists.txt` uses configure-time `file(GLOB NatronEngine_SOURCES *.cpp)`.
- Current build includes the node and builds successfully.
- If a future build tree predates this cpp, reconfigure CMake rather than doing broad source-list churn.

### 5.3 Node behavior

Inputs:

- Input 0: `Layer` — normal layer stream / pass-through source.
- Input 1: `AI Mask` — RGBA source to copy into target custom plane.

Knob:

- `targetPlane`
- Default: `ai_mask1`

Render behavior:

- For requested output plane equal to `targetPlane`, copy/convert input 1 RGBA into that target plane.
- For every non-target requested plane, fetch/pass through the same plane from input 0.
- This preserves Color/RGBA and prior custom planes while adding/writing the target custom plane.

Channel selector support:

- `getCreateChannelSelectorKnob()` explicitly returns `true`.
- `node->addUserComponents(...)` registers `ai_mask1` and the current valid `targetPlane` when different.

### 5.4 Plane-name policy

Originally restricted to `ai_maskN` only.

Nick correctly pointed out that managed AI masks can still use `ai_maskN`, but a standalone node should allow general names.

Current policy:

- `ai_mask1` remains the default/fallback.
- Flux-managed AI workflow should still use `ai_mask1`, `ai_mask2`, etc. for easy discovery.
- Standalone user node can use safe names such as:
  - `hair`
  - `holdout_01`
  - `_matte`
  - `face_matte`

Validator in `Engine/FluxAIMaskCopy.cpp`:

- first char must be `[A-Za-z_]`
- remaining chars must be `[A-Za-z0-9_]`
- rejects empty strings
- rejects punctuation, spaces, dots, slashes, colons by identifier rule
- rejects reserved/confusing built-ins case-insensitively:
  - `Color`
  - `RGBA`
  - `RGB`
  - `Alpha`
  - `Backward`
  - `Forward`
  - `DisparityLeft`
  - `DisparityRight`
  - `Motion`
  - `None`

### 5.5 Current validation evidence for custom node

Build passed after exposing the node and after broadening custom names:

```bash
cmake --build /home/npittas/Flux/build --target NatronEngine Natron -j$(nproc)
```

Result:

- `Built target NatronEngine`
- `Built target NatronGui`
- `Built target Natron`

Nick manually tested that custom channels appear downstream:

- `Flux Custom Plane Copy` appears downstream.
- The custom plane/channels are accessible by other downstream nodes.

Manual validation still recommended for the broadened names:

1. Launch `/home/npittas/Flux/build/App/Natron`.
2. Create `Flux Custom Plane Copy`.
3. Connect normal source to input 0.
4. Connect RGBA mask/matte source to input 1.
5. Set `targetPlane` to `hair` or `holdout_01`.
6. Add downstream Shuffle/channel-aware node.
7. Confirm downstream sees `hair.r/g/b/a` or `holdout_01.r/g/b/a`.
8. Confirm invalid names fall back safely to `ai_mask1`.

---

## 6. T083 source-of-truth and key markdown references

A new session should read these, in this order, before continuing T083 implementation:

### Core T083 source of truth

- `tasks/T083-ai-matte-depth.md`
- `tasks/T083-orchestrator-recovery-source-of-truth.md`
- `tasks/T083-sam3-complete-workflow-plan.md`
- `tasks/T083-sam3-ai-panel-regression-recovery-plan.md`
- `tasks/T083-custom-shuffle-node-isolation-plan.md`

### AI Paint / SAM3 prompt and panel plans

- `tasks/T083-ai-paint-live-sam3-workflow-plan.md`
- `tasks/T083-aipaint-panel-inference-wiring-plan-v2.md`
- `tasks/T083-aipaint-select-autorefresh-plan-v3.md`
- `tasks/T083-aipaint-box-selection-and-run-multiprompt-fix-plan.md`
- `tasks/T083-sam3-box-prompt-tracker-fix-plan.md`
- `tasks/T083-sam3-object-bound-mask-plan.md`
- `tasks/T083-sam3-object-bound-mask-fix-plan.md`
- `tasks/T083-sam3-temporal-workflow-plan.md`
- `tasks/T083-sam3-temporal-real-proof-plan.md`
- `tasks/T083-sam3-temporal-local-constraints.md`

### AI result/history/preview plans

- `tasks/T083-ai-result-history-preview-plan.md`
- `tasks/T083-ai-result-history-preview-fix-plan.md`
- `tasks/T083-ai-result-history-polish-plan.md`
- `tasks/T083-ai-result-history-stuck-busy-fix-plan.md`
- `tasks/T083-ai-work-viewer-implementation-plan.md`
- `tasks/T083-ai-work-viewer-review-fix-plan.md`

### AI Add/Replace/custom-channel plans

- `tasks/T083-ai-add-replace-mask-channel-copy-plan.md`
- `tasks/T083-flux-ai-mask-copy-node-plan.md`
- `tasks/T083-flux-ai-mask-copy-preserve-planes-fix-plan.md`
- `tasks/T083-ai-mask-channel-copy-viability-spike-plan.md`
- `tasks/T083-ai-apply-mask-workflow-plan.md` — historical/obsolete in parts; do not reintroduce layer-mask/Roto/Premult AI Apply path.
- `tasks/T083-unwire-ai-layer-mask-recovery-plan.md`
- `tasks/T083-disabled-external-mask-bypass-fix-plan.md`

### Viewer toolbar and orchestration correction docs

- `tasks/T083-sam3-authoritative-drift-audit.md`
- `tasks/T083-sam3-viewer-toolbar-multiprompt-recovery-plan.md`
- `tasks/T083-sam3-viewer-toolbar-multiprompt-implementation-packet.md`
- `tasks/T083-subagent-source-of-truth-protocol.md`

### Other phase/project docs needed for broader continuation

- `plans/PHASES.md`
- `tasks/TASKS.md`
- `tasks/FLUX_MOTION_TEXT_RECOVERY_SOURCE_OF_TRUTH.md`
- `tasks/T070-linux-workstation-installer.md`
- `tasks/T084-plugin-payload-discovery.md`
- `tasks/T085-build-warning-audit.md`

---

## 7. Key source files for current T083 work

### Engine / native nodes

- `Engine/AIPaint.h`
- `Engine/AIPaint.cpp`
- `Engine/AIPaintContext.h`
- `Engine/AIPaintContext.cpp`
- `Engine/FluxAIMaskCopy.h`
- `Engine/FluxAIMaskCopy.cpp`
- `Engine/AppManager.cpp`
- `Engine/EffectInstance.h`
- `Engine/Engine.pro`
- `Engine/CMakeLists.txt`
- `Engine/Node.cpp`
- `Engine/ImagePlaneDesc.h`

### GUI AI Panel / worker / viewer

- `Gui/FluxAiPanel.h`
- `Gui/FluxAiPanel.cpp`
- `Gui/FluxAiWorkerController.cpp`
- `Gui/FluxAiWorkerController.h`
- `Gui/Gui.h`
- `Gui/Gui05.cpp`
- `Gui/ViewerGL.h`
- `Gui/ViewerGL.cpp`
- `Gui/ViewerTab.h`
- `Gui/ViewerTab.cpp`
- `Gui/NodeViewerContext.cpp`

### Timeline / serialization / graph

- `Gui/FluxTimeline.h`
- `Gui/FluxTimeline.cpp`
- `Gui/FluxTimelineSerialization.h`
- `Gui/ProjectGuiSerialization.h`
- `Gui/ProjectGuiSerialization.cpp`
- `Gui/FluxMaskUtils.h`
- `Gui/FluxMaskUtils.cpp`

### AI Python runtime

- `tools/ai/flux_provider_runtime.py`
- `tools/ai/model_manifest.json`
- `tools/ai/provider_runtime_manifest.json`
- `tools/ai/sam3_transformers_worker.py`
- `tools/ai/sam3_transformers_real_inference_probe.py`

### Documentation/source references for custom planes

- `Documentation/source/devel/PythonReference/NatronEngine/Effect.rst`
  - `Effect.addUserPlane(planeName, channels)`.
- `Documentation/source/devel/PythonReference/NatronEngine/ImageLayer.rst`
  - Image layer/channel model.
- `Documentation/source/guide/getstarted-about-mainconcepts.rst`
  - Image Layers and Channels.
- `Documentation/source/plugins/net.sf.openfx.ShufflePlugin.rst`
  - Shuffle output layer and channel mapping behavior.

---

## 8. Current known blockers / unvalidated items

### 8.1 Add Mask / Replace Mask workflow still needs retest/fix

Earlier runtime failure:

```text
Add Mask failed: could not create AI Read or FluxAIMaskCopy node.
Replace Mask failed: select an AI Mask Copy effect row in the timeline.
```

Likely cause at the time:

- `FluxAIMaskCopy` existed in source but may not have been visible/compiled/registered in the configured build.
- It is now visible/searchable and builds successfully.

Next step is to retest Add Mask from the AI Panel after current custom node changes.

If Add Mask still fails, do not guess. Split diagnostics in `FluxTimeline::addAIMaskCopyToSelectedLayer`:

- distinguish AI Read creation failure from `FluxAIMaskCopy` node creation failure.
- log/report exact plugin ID attempted.
- verify `PLUGINID_FLUX_AI_MASK_COPY` can be created from the same app runtime path.

Relevant symbols:

- `FluxAiPanel::onAddMaskClicked`
- `FluxAiPanel::onReplaceMaskClicked`
- `FluxTimeline::addAIMaskCopyToSelectedLayer`
- `FluxTimeline::replaceSelectedAIMaskCopy`
- `Gui::rebuildCompositingGraph`

### 8.2 Save/reopen validation still needed

Need GUI/runtime proof that:

- project loads without emergency-serialization crash,
- AI result history persists project-relative paths,
- AI mask effect rows persist,
- generated AI Read nodes and custom plane copy nodes reconnect,
- no absolute generated paths leak into project-facing metadata.

### 8.3 Old Apply docs can mislead new agents

Several markdowns contain old/obsolete Apply-to-layer-mask direction. Current source-of-truth supersedes them:

- Do not use AI masks as Flux layer masks.
- Do not route AI result PNGs through Roto/RotoPaint/Premult.
- Use visible AI mask copy effect rows with custom channels.

### 8.4 Reviewer behavior risk

Previous reviewers incorrectly treated build/source-only evidence as enough for GUI workflows.

For any future T083 user-facing step, reviewer prompts must explicitly require:

- actual application workflow intent,
- adjacent regressions,
- runtime evidence classification,
- GUI proof if applicable,
- `unvalidated` verdict if runtime proof is absent.

---

## 9. Immediate next work order

### Step 1 — Quick manual/runtime retest by Nick or agent-assisted GUI proof

Use current built app:

```bash
/home/npittas/Flux/build/App/Natron
```

Test sequence:

1. Open the existing SAM3 test project/media.
2. Confirm AI Paint SAM3 Load still works.
3. Confirm AI Panel Run still generates a manifest and AI Work Viewer preview.
4. Click Add Mask.
5. Expected:
   - visible timeline effect row appears,
   - row uses `Flux Custom Plane Copy` / `PLUGINID_FLUX_AI_MASK_COPY`,
   - target plane is `ai_mask1`,
   - node graph has no Roto/layer-mask/Premult for AI result path.
6. Run Add Mask again.
7. Expected second row/channel: `ai_mask2`.
8. Select the first AI mask copy row and run Replace Mask.
9. Expected:
   - only selected row/source read updates,
   - no guessing / no unrelated row mutation.
10. Save/reopen and verify rows/paths/wiring.

If Nick prefers to do GUI validation himself, ask for observed error text/logs/screenshots and do not claim GUI validation without them.

### Step 2 — If Add Mask still fails, implement narrow diagnostics/fix

Use codemap workflow.

Likely allowed edit files for a diagnostic/fix packet:

- `Gui/FluxTimeline.cpp`
- maybe `Gui/FluxTimeline.h`
- maybe `Gui/FluxAiPanel.cpp` only if button message handling needs clarity

Required diagnostics:

- separate read-node failure from copy-node failure,
- include attempted plugin IDs,
- include project-relative mask path validation result,
- confirm node creation uses `PLUGINID_FLUX_AI_MASK_COPY`, not label text.

Do not change custom node plugin ID.

### Step 3 — Stabilize Add/Replace final workflow

Final expected user-visible behavior:

- AI Panel buttons are Add Mask / Replace Mask.
- Add Mask creates next `ai_maskN`.
- Replace Mask requires selected AI mask copy effect row.
- Visible timeline rows appear under the selected layer.
- Generated AI result media remains project-relative.
- Downstream node users can shuffle/access `ai_maskN.r/g/b/a`.
- Main comp viewer remains untouched by AI Work Viewer preview.
- No Roto/layer-mask/Premult AI path.

### Step 4 — Save/reopen and old-project validation

Use actual project:

- `/home/npittas/Videos/For_Test/Main_AI_SAM_Test.ntp`
- or Nick's current SAM3 project.

Check:

- old project load no crash,
- AI history metadata loads,
- custom plane copy rows load,
- graph rebuild reconnects,
- generated mask files are found via project-relative paths.

### Step 5 — Update task/phase docs only after validated

After real validation, update:

- `tasks/TASKS.md` T083 row/status details if needed.
- `tasks/T083-ai-matte-depth.md` current stage/evidence.
- `plans/PHASES.md` only if phase progress changes.

Do not mark T083 done; T083 has many pending milestones.

---

## 10. Full T083 milestone planning after Add/Replace stabilization

From `tasks/T083-ai-matte-depth.md`, remaining high-level milestones include:

- T083-2 — Secure model manager / installer download flow: pending full real model download validation.
- T083-3 — BiRefNet base matte node.
- T083-4 — ViTMatte refinement node.
- T083-5 — SAM3.1 viewer-selection base matte/video mask node.
- T083-6 — MatAnyone2 video matte refinement node with non-commercial warning.
- T083-7 — Nodegraph AI nodes integration.
- T083-8 — Video Depth Anything depth node.
- T083-9 — DepthCrafter depth option with restrictive/non-commercial warning.
- T083-10 — RVM/XMem2 external helper hooks.
- T083-11 — validation/packaging/performance pass.

Do not jump to these until current SAM3/Add/Replace custom-channel workflow is stable or explicitly blocked.

---

## 11. Wider project continuation after T083

When ready to continue the full project phases/tasks:

1. Read `tasks/TASKS.md` and `plans/PHASES.md` fresh.
2. Current active phase is P7.
3. P7 has T082 pending and T083 in progress.
4. Text animator work T080/T081 is manually accepted.
5. Several older TextRender tasks T075–T079 are marked BLOCKED/rejected as prototype evidence only; do not revive without reading `tasks/FLUX_MOTION_TEXT_RECOVERY_SOURCE_OF_TRUTH.md`.
6. P8 Polish + Cache is pending after P7.

For any new feature:

- present a concrete plan,
- state user-visible behavior and risks,
- wait for Nick's approval,
- implement only the approved scope,
- validate with real artifacts/screenshots for GUI.

---

## 12. Current working tree snapshot at handoff time

`git status --short` showed many modified/untracked files from the broad T083 work. Do not treat this as a clean repo. Do not revert anything without Nick.

Key modified tracked files include:

- `AGENTS.md`
- `Engine/AppManager.cpp`
- `Engine/EffectInstance.h`
- `Engine/Engine.pro`
- `Gui/FluxAiPanel.cpp`
- `Gui/FluxAiPanel.h`
- `Gui/FluxAiWorkerController.cpp`
- `Gui/FluxTimeline.cpp`
- `Gui/FluxTimeline.h`
- `Gui/FluxTimelineSerialization.h`
- `Gui/Gui.h`
- `Gui/Gui05.cpp`
- `Gui/NodeViewerContext.cpp`
- `Gui/ProjectGuiSerialization.h`
- `Gui/ViewerGL.cpp`
- `Gui/ViewerGL.h`
- many `tasks/T083-*.md`
- `tools/ai/sam3_transformers_real_inference_probe.py`

Key untracked source files include:

- `Engine/AIPaint.cpp`
- `Engine/AIPaint.h`
- `Engine/AIPaintContext.cpp`
- `Engine/AIPaintContext.h`
- `Engine/FluxAIMaskCopy.cpp`
- `Engine/FluxAIMaskCopy.h`
- `tools/ai/sam3_transformers_worker.py`
- many `tasks/T083-*.md`

Graphify artifacts also exist:

- `.graphify_detect.json`
- `.graphify_incremental.json`
- `.graphify_python`
- `graphify-out/`

Because files are untracked/modified, build success is more important than assuming git baseline correctness. Any cleanup/commit strategy requires Nick's explicit approval.

---

## 13. Validation commands used recently

Most recent build after custom plane changes:

```bash
cmake --build /home/npittas/Flux/build --target NatronEngine Natron -j$(nproc)
```

Result:

- passed
- `NatronEngine`, `NatronGui`, and `Natron` built

Earlier SAM3/runtime validation commands:

```bash
python3 -m py_compile \
  tools/ai/sam3_transformers_worker.py \
  tools/ai/sam3_transformers_real_inference_probe.py \
  tools/ai/flux_provider_runtime.py

python3 tools/ai/flux_provider_runtime.py status sam3 --json
```

GUI launch command:

```bash
QT_QPA_PLATFORM=xcb /home/npittas/Flux/build/App/Natron
```

Proof/checklist path used previously:

- `/tmp/flux-t083-sam3-recovery-proof/manual-checklist.txt`

---

## 14. Key manual test assets

Known useful assets/projects:

- `/home/npittas/Videos/For_Test/Video_For_Test.mov`
- `/home/npittas/Videos/For_Test/Main_AI_SAM_Test.ntp`
- Nick runtime media mentioned in session:
  - `lightx2v_lora_rank_comparison.mp4`

SAM3 generated manifest example:

- `FluxGenerated/AI/base-matte/sam3_transformers/run-20260528-231348/result_manifest.json`

---

## 15. Protected workflows checklist for every future T083 fix

Every future T083 implementation/review should explicitly classify these as verified, unverified, broken/blocker, or not applicable:

- Native RotoPaint node creation and native Roto/RotoPaint layer masks.
- AI Paint node creation and selection.
- AI Paint prompt capture/edit/delete/clear.
- AI Paint Load SAM3 / Unload SAM3.
- AI Paint Live Preview current-frame overlay.
- AI Panel Run.
- AI Panel result history and Preview Again.
- AI Panel Add Mask / Replace Mask.
- Source-frame export and timeline/source-frame mapping.
- Old project load/save/reopen.
- Timeline effect enable/disable.
- Main comp viewer isolation.
- Custom `Flux Custom Plane Copy` standalone node usage.
- Downstream Shuffle/channel access to custom planes.

---

## 16. Recommended next prompt to a new orchestrator session

If continuing directly, the next session can start with:

> Read `tasks/T083-full-session-handoff.md`, then retest or diagnose AI Panel Add Mask / Replace Mask now that `Flux Custom Plane Copy` is visible and downstream custom planes work. Preserve native RotoPaint, no layer-mask/Roto/Premult AI path, no git restoration. If Add Mask still fails, split diagnostics between AI Read creation and Flux Custom Plane Copy creation before fixing.

---

## 17. Summary in one paragraph

T083 is mid-recovery but much closer: SAM3 persistent worker, AI Paint live preview, AI Panel Run/history/Preview Again, project-relative outputs, and source-frame refresh have been repaired and manually validated by Nick. The wrong AI Apply-to-layer-mask/Roto/Premult path was unwired. A native `Flux Custom Plane Copy` node now exists, is visible/searchable, builds, supports Natron channel selectors, preserves input planes, copies input-B RGBA into a custom target plane, and accepts safe arbitrary plane names while defaulting to `ai_mask1`; Nick confirmed downstream custom channels appear. The immediate next task is to retest/fix AI Panel Add Mask / Replace Mask so it creates visible timeline AI mask copy effect rows using `ai_maskN`, without touching native RotoPaint, layer masks, Premult, or the main comp viewer, then validate save/reopen and update task docs.
