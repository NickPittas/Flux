# Implementation Plan

## Goal
Implement T083 as a CUDA-first, model-pluggable AI matte/mask/depth system with viewer selection, AI panel control, nodegraph-usable AI nodes, secure model acquisition, and project-relative generated media storage.

## Corrected Architecture Summary

T083 should not be a SAM2-first feature. The corrected architecture is a Flux AI subsystem built around:

- **CUDA-first Python worker process** for inference, isolated from the Qt/Natron process via QProcess/IPC patterns already present in `Engine/ProcessHandler.*`.
- **AI panel + nodegraph nodes**: timeline users control AI work from a dedicated panel, while power users can create normal nodegraph nodes such as base matte, matte refine, segment/track, and depth nodes.
- **Viewer selection as a first-class input**: point, box, text prompt, and current-frame mask prompts are captured in the viewer and passed to the base matte model.
- **Shared base matte pipeline**: a base matte from SAM3.1/BiRefNet/viewer selection is reused by ViTMatte or MatAnyone2 for edge/video refinement.
- **Generated media output**: all generated masks, alpha sequences, foregrounds, and depth EXRs are written under a project-relative generated-media directory, then referenced by Flux timeline masks/effects/nodegraph nodes.
- **Secure model manager**: installer/model manager installs tested models by default where legally possible, requests HF tokens only when needed, stores tokens only in encrypted desktop secret stores, and never writes plaintext tokens.

## Model Roles and Install/Legal Status

| Model | Role in Flux | Priority | Install Status | Legal/Bundling Status | Notes |
|---|---:|---:|---|---|---|
| **SAM 3.1 / SAM3+ line** | Viewer-selected prompt segmentation, base matte, video object masks/tracks | Highest | Gated HF download via secure token | Meta SAM License, non-OSI; GPL2 compatibility/redistribution needs legal review | Not SAM 2.1. Use point/box/text/mask prompts from viewer. |
| **MatAnyone2** | Best video human matte refinement from first-frame mask/base matte | Highest | User/download gated or external until legal clear | NTU S-Lab non-commercial; commercial/default bundling blocked without permission | Must be a real implementation option, not hand-waving. |
| **BiRefNet** | Safe bundled base matte/background removal, high-res still/image sequence masks | Highest | Install/bundle by default after tested | MIT; safe default candidate | First permissive CUDA worker validation target. |
| **ViTMatte** | Edge refinement from base mask-derived trimap | High | Install/bundle by default after tested | MIT; safe default candidate | Required matte-refine path using same base matte. |
| **Video Depth Anything** | Accepted depth baseline | High | Install by default if license/weights allow | Needs final manifest/license check | Baseline for depth node; keep because Nick accepted it. |
| **DepthCrafter** | Strong video-stable depth option | High | Real option, but not default commercial bundle until legal clear | Restrictive academic/research/education; no commercial/production | Heavy VRAM; expose resolution/stride controls. |
| **RVM** | Fast recurrent human video matting helper/baseline | Medium | External/helper only unless legal approves | GPL3 conflicts with GPL2-only if integrated/bundled | Consider subprocess aggregation only after legal decision. |
| **XMem2** | Semi-supervised video object mask propagation fallback | Medium | External/helper only unless legal approves | GPL3 conflicts with GPL2-only if integrated/bundled | Useful if SAM3.1 video tracking unavailable/too heavy. |
| **MatAnyone original** | Fallback/comparison for MatAnyone2 | Lower | Optional external research option | Restrictive/non-commercial | Keep behind same legal gate as MatAnyone2. |

## Proposed Nodegraph Node Set

1. **`FluxAIBaseMatte`**
   - Providers: `SAM3.1`, `BiRefNet`.
   - Inputs: source image/video sequence; optional prompt data asset from viewer.
   - Outputs: alpha/mask sequence and optional preview matte.
   - Use: base matte extraction for timeline layer masks and nodegraph workflows.

2. **`FluxAIMatteRefine`**
   - Providers: `ViTMatte`, `MatAnyone2`, optional `MatAnyone`.
   - Inputs: source image/video plus base matte/trimap/first-frame mask.
   - Outputs: refined alpha sequence, optional foreground sequence/video.
   - Required behavior: consume the same base matte produced by `FluxAIBaseMatte` or viewer prompt workflow.

3. **`FluxAISegmentTrack`**
   - Providers: `SAM3.1 Video`, optional `XMem2` if legal/external workflow approved.
   - Inputs: source video plus initial prompts/masks.
   - Outputs: propagated object masks with stable IDs where provider supports them.

4. **`FluxAIDepth`**
   - Providers: `Video Depth Anything`, `DepthCrafter`.
   - Inputs: source video/image sequence.
   - Outputs: EXR depth/disparity sequence and normalized preview output.

5. **`FluxAIGeneratedMediaRead` or reuse existing Read nodes**
   - Purpose: load project-relative generated masks/depth outputs into masks/effects/nodegraph.
   - Prefer reusing existing Read node creation where possible; add only if normal Read nodes cannot represent AI artifact metadata cleanly.

## Viewer Selection UX and AI Panel UX

### Viewer Selection UX

- Add AI prompt modes to the viewer:
  - **Point include/exclude** clicks.
  - **Box selection** drag rectangle.
  - **Text prompt** attached through AI panel.
  - **Use current alpha/mask** as prompt/refinement input.
- Hook locations:
  - `Gui/ViewerGL.cpp`: `ViewerGL::mousePressEvent` for click/box start/end dispatch.
  - `Gui/HostOverlay.cpp`: follow `HostOverlay::penDown/penUp` and `TransformInteract` overlay patterns for prompt overlay drawing.
- Prompt overlays should show selected points/boxes/object IDs before inference.
- Viewer prompt state must serialize as artifact metadata when it leads to generated media.

### AI Panel UX

- Add an AI panel to the Flux layout with:
  - Task selector: Base Matte, Refine Matte, Segment/Track, Depth.
  - Model selector filtered by installed/legal/available models.
  - Prompt controls: point/box/text/current-alpha.
  - Frame range controls: current frame, work area, full clip, custom range.
  - Output location preview: project-relative generated media path.
  - CUDA device, precision, resolution/stride, and VRAM-safe presets.
  - Run/cancel/progress/log summary.
  - Install/manage model button for missing models.
- AI panel actions create/control underlying timeline nodes; nodegraph users can create the AI nodes directly through `Gui/NodeCreationDialog.cpp`.

## Secure Installer / Model Manager Strategy

- Extend `tools/linux/flux-linux-setup.sh` to install the AI Python runtime/model manager after implementation is tested, not as a permanently optional prototype.
- Add `flux-model-manager` Python helper for `list`, `install`, `verify`, `remove`, `login`, `logout`, and offline bundle import.
- Use XDG paths:
  - Hub cache: `${XDG_CACHE_HOME:-$HOME/.cache}/Flux/huggingface`.
  - Download staging: `${XDG_CACHE_HOME:-$HOME/.cache}/Flux/model-downloads`.
  - Installed models: `${XDG_DATA_HOME:-$HOME/.local/share}/Flux/models/<provider>/<model>/<revision>`.
  - Non-secret manifest/config: `${XDG_CONFIG_HOME:-$HOME/.config}/Flux/models.json`.
- Never call `huggingface_hub.login()` or `hf auth login` in Flux automation.
- Use `snapshot_download(..., token=token, revision=<pinned>, cache_dir=..., local_dir=...)`.
- Store HF token only via QtKeychain/libsecret/KWallet or Python `keyring` with accepted secure backends.
- If no encrypted keyring is available, allow one-shot token use only and discard it; no plaintext fallback.
- Bundle permissive/public models where legally possible after hash/manifest validation. Gated/restrictive models require explicit license acceptance and/or legal approval.

## Tasks

1. **Define AI artifact and prompt data model**
   - File: `Gui/FluxTimeline.h`
   - Changes: Add lightweight structs for AI artifact metadata, prompt type, prompt points/boxes/text, provider/model id, output path, frame range, and source layer/node references.
   - Acceptance: Data model can represent base matte, refined matte, segmentation track, and depth outputs without model-specific hacks.

2. **Add AI artifact serialization**
   - File: `Gui/FluxTimelineSerialization.h`
   - Changes: Extend `FluxMaskSerialization` / `FluxLayerSerialization` with AI artifact metadata and project-relative generated media references.
   - Acceptance: Save/reopen preserves AI artifact metadata without absolute-path-only references.

3. **Add project-relative generated media path policy**
   - File: `Gui/FluxTimeline.cpp`
   - Changes: Add helper/path policy for generated AI media under the current project directory, e.g. `FluxGenerated/AI/<task>/<layer-or-node>/<run-id>/`.
   - Acceptance: New AI runs resolve to project-relative paths; unsaved projects require save-location prompt or safe temporary staging before commit.

4. **Create AI panel shell**
   - File: `Gui/Gui05.cpp`
   - Changes: Add AI panel dock/tab to Flux UI layout and connect it to timeline selection/current viewer frame.
   - Acceptance: Panel appears in Flux UI and can display selected layer/source and installed-model status.

5. **Implement AI panel widget**
   - New File: `Gui/FluxAIPanel.h`
   - New File: `Gui/FluxAIPanel.cpp`
   - Changes: Build task/model selectors, prompt controls, frame-range controls, output path display, Run/Cancel buttons, and status/progress area.
   - Acceptance: Screenshot proof shows AI panel with model/task controls and disabled/enabled states based on selection/model availability.

6. **Add viewer prompt overlay capture**
   - File: `Gui/ViewerGL.cpp`
   - File: `Gui/HostOverlay.cpp`
   - Changes: Add AI prompt mode hooks for point and box selection using existing viewer interaction patterns; emit prompt data to AI panel/timeline controller.
   - Acceptance: Recording shows viewer click/box prompt overlay and prompt data arriving in AI panel.

7. **Add AI job bridge using QProcess/IPC**
   - File: `Engine/ProcessHandler.h`
   - File: `Engine/ProcessHandler.cpp`
   - New File: `Gui/FluxAIJobController.h`
   - New File: `Gui/FluxAIJobController.cpp`
   - Changes: Launch Python worker with JSON job spec over stdin/stdout or temp job files; support progress, cancel, error propagation, and output manifest ingestion.
   - Acceptance: Dummy worker job writes a manifest and panel reports progress/cancel/error correctly.

8. **Create Python AI worker skeleton**
   - New File: `tools/ai/flux_ai_worker.py`
   - New File: `tools/ai/flux_ai_common.py`
   - Changes: Parse job JSON, validate CUDA availability, resolve local model paths, read image/video inputs, write output manifest, and return structured errors.
   - Acceptance: Worker can run a no-op/dummy CUDA capability job from Flux and from CLI.

9. **Implement model manifest and manager**
   - New File: `tools/ai/flux_model_manager.py`
   - Changes: Implement model listing/install/verify with pinned revisions, hashes, license metadata, local model store, and no plaintext secrets.
   - Acceptance: `list` and `verify` work offline; gated models report missing token/license state cleanly.

10. **Integrate secure token handling into installer/model manager**
   - File: `tools/linux/flux-linux-setup.sh`
   - File: `tools/ai/flux_model_manager.py`
   - Changes: Add Python deps for `huggingface_hub` and secure `keyring`; support token stdin/env/interactive; reject plaintext persistence; detect secure backends.
   - Acceptance: Installer can install public models without token; gated SAM3.1 path requests token and does not create HF plaintext token files.

11. **Implement permissive first model: BiRefNet base matte**
   - File: `tools/ai/flux_ai_worker.py`
   - Changes: Add BiRefNet provider for image/current-frame and image-sequence inference; output alpha PNG/EXR sequence plus manifest.
   - Acceptance: CUDA inference produces a mask sequence from real footage/images and creates project-relative generated media.

12. **Implement ViTMatte refinement path**
   - File: `tools/ai/flux_ai_worker.py`
   - Changes: Generate trimap from base mask via erode/dilate controls; run ViTMatte and output refined alpha.
   - Acceptance: Same base matte from BiRefNet/SAM path can be refined; visual comparison shows improved edges on test footage.

13. **Implement SAM3.1 gated viewer-selection path**
   - File: `tools/ai/flux_ai_worker.py`
   - File: `Gui/FluxAIPanel.cpp`
   - Changes: Add SAM3.1 provider using viewer point/box/text prompts and secure HF-installed local model path.
   - Acceptance: Viewer point/box selection generates base matte; gated model errors guide user to HF acceptance/token flow.

14. **Add AI node entries to node creation UI**
   - File: `Gui/NodeCreationDialog.cpp`
   - File: `Engine/CreateNodeArgs.h`
   - Changes: Register/create Flux AI node wrappers or PyPlug/plugin nodes so nodegraph users can create BaseMatte, MatteRefine, SegmentTrack, and Depth nodes.
   - Acceptance: Nodegraph creation test can create each AI node and run a simple pass image/video -> inference -> generated result workflow.

15. **Wire AI outputs into existing mask graph workflow**
   - File: `Gui/Gui05.cpp`
   - File: `Gui/FluxMaskUtils.cpp`
   - Changes: Connect generated matte Read/output nodes into existing layer/effect mask rows without breaking inline mask graph and branch preservation rules.
   - Acceptance: Layer/effect mask graph preservation survives rebuild, duplicate/split where supported, and save/reopen.

16. **Implement Video Depth Anything baseline depth node**
   - File: `tools/ai/flux_ai_worker.py`
   - Changes: Add depth provider producing EXR depth/disparity sequence and preview normalization.
   - Acceptance: Real video produces project-relative EXR depth sequence readable in Flux.

17. **Add DepthCrafter as legal-gated depth option**
   - File: `tools/ai/flux_model_manager.py`
   - File: `tools/ai/flux_ai_worker.py`
   - Changes: Add manifest/provider behind non-commercial/restrictive license gate; expose resolution/VRAM controls.
   - Acceptance: If user explicitly enables and model is installed, DepthCrafter runs on a short test clip; otherwise UI reports legal/install gate.

18. **Add MatAnyone2 as legal-gated matte refinement option**
   - File: `tools/ai/flux_model_manager.py`
   - File: `tools/ai/flux_ai_worker.py`
   - Changes: Add MatAnyone2 provider accepting source video plus first-frame/base mask; output alpha/foreground sequence.
   - Acceptance: If legally enabled and installed, MatAnyone2 refines the same base matte; otherwise UI reports non-commercial/license gate.

19. **Evaluate RVM/XMem2 external-helper integration**
   - File: `tools/ai/flux_model_manager.py`
   - Changes: Add manifest stubs and legal-gated external helper hooks only if Nick/legal approves GPL3 subprocess aggregation.
   - Acceptance: Decision recorded; no GPL3 code/weights bundled by accident.

20. **Validation pass**
   - Files: `tests/assets/` and validation notes under `t083-research/` or task detail file.
   - Changes: Validate with real images/videos: nodegraph creation, AI panel screenshots, viewer prompt recording, save/reopen, layer/effect mask graph preservation, installer token handling, CUDA/perf/VRAM notes.
   - Acceptance: GUI proof artifacts show controls and outputs; generated media remains project-relative; no plaintext token is found in config/cache/logs.

## Files to Modify

- `Gui/Gui05.cpp` - add AI panel to layout; wire AI outputs into timeline/layer/mask graph; preserve rebuild semantics.
- `Gui/FluxTimeline.h` - add AI artifact/prompt metadata to layer/mask/effect model.
- `Gui/FluxTimeline.cpp` - add generated media path policy, AI actions/context menus, persistence integration, save/reopen behavior.
- `Gui/FluxTimelineSerialization.h` - serialize AI artifact metadata and project-relative generated media references.
- `Gui/FluxMaskUtils.cpp` - recognize generated AI matte/depth branches where relevant without misclassifying manual/precomp branches.
- `Gui/ViewerGL.cpp` - hook viewer point/box prompt events.
- `Gui/HostOverlay.cpp` - draw AI prompt overlays using existing interaction patterns.
- `Gui/NodeCreationDialog.cpp` - expose nodegraph-usable AI nodes.
- `Engine/CreateNodeArgs.h` - support Flux-managed AI node creation flags if needed.
- `Engine/ProcessHandler.h` - AI worker process bridge support if current API needs extension.
- `Engine/ProcessHandler.cpp` - QProcess/IPC bridge implementation details if current API needs extension.
- `tools/linux/flux-linux-setup.sh` - install AI runtime/model manager, secure token flow, default permissive model install after tests.

## New Files

- `Gui/FluxAIPanel.h` - AI panel widget declaration.
- `Gui/FluxAIPanel.cpp` - AI panel UI and state handling.
- `Gui/FluxAIJobController.h` - C++ controller for AI job launch/progress/cancel/result ingestion.
- `Gui/FluxAIJobController.cpp` - QProcess/IPC job implementation.
- `tools/ai/flux_ai_worker.py` - CUDA Python inference worker.
- `tools/ai/flux_ai_common.py` - shared job spec, manifest, path, and error helpers.
- `tools/ai/flux_model_manager.py` - model install/verify/login/offline-bundle manager.
- `tools/ai/model_manifest.json` - pinned model definitions, revisions, hashes, license gates, install profiles.
- `tasks/T083-ai-matte-depth.md` - task detail/validation checklist if Nick wants task-file tracking before implementation.

## Milestone Breakdown

1. **Milestone 0: Legal/model manifest decisions**
   - Create manifest with BiRefNet, ViTMatte, SAM3.1, MatAnyone2, Video Depth Anything, DepthCrafter, RVM, XMem2.
   - Mark each as bundled, user-download, gated, non-commercial, or external-only.

2. **Milestone 1: First implementation slice — viewer-selected base matte + refine scaffold**
   - Build AI panel shell, viewer point/box overlay, job controller, worker skeleton, generated media storage, and BiRefNet + ViTMatte providers.
   - Include SAM3.1 as the target provider in the UI/model manager with secure gated install flow, but implement graceful “requires access/token/model install” state until token/model are available.
   - This respects Nick’s SAM3+/viewer-selection/matte-refine direction while honestly handling SAM3.1 gating/legal reality.

3. **Milestone 2: SAM3.1 real integration**
   - Add promptable SAM3.1 image/video path from viewer points/boxes/text to base matte/track outputs.
   - Validate secure token flow and no plaintext persistence.

4. **Milestone 3: Nodegraph AI nodes**
   - Expose BaseMatte, MatteRefine, SegmentTrack, Depth nodes in `NodeCreationDialog` and confirm nodegraph-only workflows.

5. **Milestone 4: Video matte refinement**
   - Add MatAnyone2 behind legal/non-commercial gate and prove source video + base mask -> refined alpha workflow.

6. **Milestone 5: Depth**
   - Add Video Depth Anything baseline and DepthCrafter legal-gated option producing EXR depth sequences.

7. **Milestone 6: Installer/default install**
   - After tested implementation, make permissive models install by default; gated/restrictive models are installed only after explicit license/token flow.

## Dependencies

- Tasks 1-3 must precede save/reopen and graph wiring work.
- Tasks 4-8 are the first implementation foundation and should precede any real model provider.
- Task 10 depends on Task 9 and must precede SAM3.1 gated install.
- Task 11 should precede Task 12 because ViTMatte needs a base matte/trimap source.
- Task 13 depends on viewer prompt capture, secure model manager, and worker skeleton.
- Tasks 14-15 depend on generated media outputs and AI node/job controller foundations.
- DepthCrafter/MatAnyone2/RVM/XMem2 tasks depend on Nick/legal decisions.

## Risks

- **Legal**: SAM3.1 Meta SAM License compatibility with GPL2 and redistribution is unresolved; MatAnyone2 and DepthCrafter are non-commercial/restrictive; RVM/XMem2 are GPL3.
- **Security**: HF token must never be persisted through Hugging Face default login/cache flows; verify no plaintext token file is created.
- **Performance/VRAM**: DepthCrafter and SAM3.1 video may be heavy; RTX 4090 local benchmarks are required.
- **Project state**: Unsaved projects need a clear generated-media policy before writing project-relative outputs.
- **Graph safety**: AI-generated branches must not break existing P6 mask graph preservation or misclassify manual/precomp branches.
- **UX proof**: GUI work needs screenshots/recordings showing AI panel controls, viewer prompt overlay, and resulting viewport/nodegraph behavior.
- **Roto spline conversion**: Defer unless an easy, reliable path is discovered; raster mask/alpha sequence workflow is the primary implementation.

## Exact Open Decisions for Nick/Legal

1. Can Flux redistribute SAM3.1 code/weights under Meta SAM License in a GPL2 application, or must SAM3.1 always be user-downloaded after HF/license acceptance?
2. Should Flux request commercial permission for MatAnyone2, and can it be used internally before permission as a non-commercial research option?
3. Should Flux request commercial permission for DepthCrafter, or keep it as an explicit non-commercial/research-gated option only?
4. Are GPL3 RVM/XMem2 acceptable as external subprocess helpers, or should they be excluded until Flux licensing strategy changes?
5. Which Video Depth Anything implementation/checkpoint is approved for the default depth baseline after license/quality review?
6. For unsaved projects, should AI generated media force “Save Project As” before running, or allow temporary cache outputs that are moved once the project is saved?

## Task Packet Candidate for First Coding Step

**Task:** T083-AI Foundation: AI panel shell, viewer prompt capture, generated media metadata, and CUDA worker skeleton.

**Scope:**
- Add `FluxAIPanel` UI shell with task/model selectors and prompt/run controls.
- Add viewer point/box prompt mode overlay and connect prompt data to the AI panel.
- Add AI artifact metadata and serialization fields.
- Add project-relative generated media path policy.
- Add `FluxAIJobController` launching a dummy `flux_ai_worker.py` CUDA capability/no-op job.
- Add model manager manifest entries with legal/install status only; no real model inference yet.

**Files:**
- `Gui/FluxAIPanel.{h,cpp}`
- `Gui/FluxAIJobController.{h,cpp}`
- `Gui/Gui05.cpp`
- `Gui/FluxTimeline.{h,cpp}`
- `Gui/FluxTimelineSerialization.h`
- `Gui/ViewerGL.cpp`
- `Gui/HostOverlay.cpp`
- `tools/ai/flux_ai_worker.py`
- `tools/ai/flux_ai_common.py`
- `tools/ai/flux_model_manager.py`
- `tools/ai/model_manifest.json`

**Acceptance:**
- Build passes.
- AI panel appears and reflects selected layer/source.
- Viewer point and box prompts are visible and recorded.
- Dummy worker runs through QProcess, reports CUDA availability/progress, and writes an output manifest under the project-relative generated-media policy.
- Save/reopen preserves AI prompt/artifact metadata.
- No HF token or plaintext secret handling is introduced in this first slice.
