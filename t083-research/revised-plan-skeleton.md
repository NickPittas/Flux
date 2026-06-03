# Implementation Plan

## Goal
Deliver a revised T083 plan skeleton for AI matte/mask generation and video depth that reflects Nick's corrections: SAM 3+ first, CUDA-first local inference, bundled/default install if legally allowed, viewer-driven selection, AI panel plus nodegraph-usable nodes, raster matte/depth artifacts stored project-relatively, refinement via VitMatte or MatAnyone2, and Video Depth Anything accepted for depth.

## Tasks
1. **Lock corrected product direction before implementation**
   - File: `tasks/TASKS.md` / future `tasks/T083-ai-matte-mask-depth.md`
   - Changes: Update T083 wording after Nick approval to remove the older "SAM2 first / optional AI deps" bias and state the corrected assumptions.
   - Acceptance: T083 explicitly says SAM 3+ is the first segmentation backend to research/target, CUDA/NVIDIA is first-class, working implementation should be default/bundled where legal, and Video Depth Anything remains acceptable for depth.

2. **Deep legal research gate for default/bundled model install**
   - File: research output under `t083-research/`
   - Changes: Deep research agents must answer whether Flux may legally bundle/install by default: SAM 3/SAM 3.1 code + weights, VitMatte, MatAnyone2, Video Depth Anything, and any required dependencies.
   - Acceptance: Each candidate has a clear verdict: `bundle allowed`, `download-on-first-use with acceptance`, `user-token gated`, `user-supplied only`, or `do not ship`, with cited license/weights/source terms.

3. **Design AI runtime as CUDA-first, local, product path**
   - File: planned new `Gui/FluxAIJob.{h,cpp}`, `scripts/flux-ai/worker.py`, installer/checker files after approval
   - Changes: Plan external Python worker/service using CUDA first on Nick's NVIDIA path; CPU fallback can exist later but must not define the initial product quality target.
   - Acceptance: Plan includes CUDA dependency check, GPU/VRAM reporting, failure messages, cancel/progress, and no headless-only completion claim.

4. **Define model registry with SAM 3+ as first segmentation backend**
   - File: planned new `Gui/FluxAIModelRegistry.{h,cpp}` and `scripts/flux-ai/backends/`
   - Changes: Model metadata must include license state, install mode, HF/token need, task type, prompt types, output type, CUDA requirement, model path, and default enablement.
   - Acceptance: Registry can expose SAM 3+ first for base mask generation, VitMatte/MatAnyone2 for matte refinement, and Video Depth Anything for depth without hardcoding one model path.

5. **Add secure HuggingFace/token handling if required**
   - File: planned new token settings/service integration; exact file TBD after Qt credential-storage research
   - Changes: If any backend needs gated HuggingFace access, request token in UI, store encrypted via platform-appropriate secret storage if available, and never write raw tokens to project files/logs/scripts.
   - Acceptance: Deep research identifies Fedora/KDE-compatible encrypted storage option; implementation plan includes token redaction, validation, and revoke/change UI.

6. **Design user-facing AI panel**
   - File: planned new `Gui/FluxAIPanel.{h,cpp}`, integrate from `Gui/Gui05.cpp`
   - Changes: Dedicated panel with model picker, task tabs/actions (Segment/Matte/Depth), selected layer/source, frame range, prompt/selection summary, output folder preview, install/status, run/cancel/progress, and apply/update controls.
   - Acceptance: GUI proof requirement defined: screenshots/recording must show panel controls, model dropdown, progress, applied result, and viewer response.

7. **Design nodegraph-usable AI nodes**
   - File: likely new PyPlug/OFX/node wrappers under `plugins/` or generated node groups; exact node technology TBD
   - Changes: Provide nodegraph-accessible nodes for generated AI matte/depth workflows, not AI panel only. Nodes should read project-relative generated sequences/metadata and be usable by power users in Natron node graph.
   - Acceptance: Plan names node types and expected knobs, e.g. source layer/clip, artifact path, model metadata, refresh/regenerate action where feasible, and generated alpha/depth output.

8. **Implement viewer-driven selection/prompt design, not text-only prompting**
   - File: likely `Gui/Viewer*`, `Gui/HostOverlay.cpp`, `Gui/FluxAIPanel.{h,cpp}` after source inspection
   - Changes: First workflow must let user make a viewer selection/prompt: points, boxes, scribbles, or mask seed on the actual viewer image. Text prompting may be additional for SAM 3+, not the only interaction.
   - Acceptance: User can select/mark the target in the viewer, generate a base matte, and see selection metadata recorded in the AI job JSON.

9. **Generate base raster matte from selection using SAM 3+**
   - File: `scripts/flux-ai/backends/sam3*.py` plus Flux attachment code later
   - Changes: Export selected frame range/proxy frames, run SAM 3+ from viewer prompts, write per-frame matte sequence and metadata.
   - Acceptance: Real `.mov`/`.mp4` clip produces a project-relative matte sequence that can be scrubbed in Flux.

10. **Refine base matte through VitMatte or MatAnyone2**
   - File: `scripts/flux-ai/backends/vitmatte*.py` / `matanyone2*.py`
   - Changes: Treat SAM 3+ result as base/trimap/target guidance, then refine to higher-quality alpha with VitMatte or MatAnyone2, depending on legal and technical research.
   - Acceptance: Same clip shows improved soft edges/hair/motion-blur handling versus base segmentation; benchmark records inference time and VRAM.

11. **Attach generated matte to existing Flux mask graph as raster artifact**
   - File: `Gui/FluxTimeline.h`, `Gui/FluxTimeline.cpp`, `Gui/FluxTimelineSerialization.h`, `Gui/Gui05.cpp`
   - Changes: Extend mask/depth metadata to reference generated project-relative sequences; wire generated alpha into approved layer/effect mask paths without reintroducing layer-mask `Merge(in)`.
   - Acceptance: Timeline mask row appears, save/reopen preserves it, render/export includes the matte, and manual nodegraph branches are preserved.

12. **Defer raster-to-roto conversion explicitly**
   - File: future task note, not T083 first implementation
   - Changes: State that AI outputs are raster alpha/depth sequences for T083; conversion to editable Roto splines is a later research/quality task.
   - Acceptance: No T083 milestone depends on vectorizing masks into roto shapes.

13. **Add Video Depth Anything depth vertical slice**
   - File: `scripts/flux-ai/backends/video_depth_anything.py`, generated depth attachment code TBD
   - Changes: Generate project-relative EXR depth sequences using Video Depth Anything as an accepted video-depth backend; compare only if other depth candidates are legally/technically viable.
   - Acceptance: Real moving video produces temporally usable depth sequence, visible in Flux as approved depth node/layer/pass, with save/reopen/render proof.

14. **Store generated artifacts project-relatively**
   - File: serialization and project/job metadata files TBD
   - Changes: Generated mattes/depth/JSON go under an approved project-relative folder, e.g. `<project-dir>/FluxGenerated/ai/<job-id>/`, with relative paths in project serialization.
   - Acceptance: Copying the project folder keeps generated AI artifacts linked; missing-artifact errors are explicit and recoverable.

15. **Validation and packaging milestone**
   - File: installer/checker docs and validation notes after approval
   - Changes: Once legal gates pass, AI dependencies/models should be default/bundled install path, not indefinitely optional, with clean VM/distrobox validation.
   - Acceptance: Build/installer check verifies CUDA, model files, worker import, token state if needed, GUI workflow screenshots, save/reopen, export, performance/VRAM/cache metrics.

## Files to Modify
- `tasks/TASKS.md` - update T083 wording after approval to reflect corrected direction.
- `plans/PHASES.md` - update P7/T083 status once implementation starts/completes.
- `Gui/Gui05.cpp` - integrate AI panel and attach generated matte/depth artifacts to existing graph paths.
- `Gui/FluxTimeline.h` - extend mask/depth data model with AI artifact metadata.
- `Gui/FluxTimeline.cpp` - add AI actions, mask-row integration, selection/apply flow.
- `Gui/FluxTimelineSerialization.h` - persist generated AI artifact metadata and relative paths.
- Installer/checker files TBD - add CUDA/model/token checks once legal/install decisions are known.

## New Files
- `Gui/FluxAIPanel.h` - AI panel UI declaration.
- `Gui/FluxAIPanel.cpp` - AI panel UI implementation.
- `Gui/FluxAIJob.h` - AI job/process state and progress API.
- `Gui/FluxAIJob.cpp` - external worker launch/cancel/progress/error handling.
- `Gui/FluxAIModelRegistry.h` - model metadata API.
- `Gui/FluxAIModelRegistry.cpp` - model registry implementation.
- `scripts/flux-ai/worker.py` - out-of-process Python worker entry point.
- `scripts/flux-ai/backends/sam3.py` - SAM 3+ base segmentation backend, exact name/version TBD.
- `scripts/flux-ai/backends/vitmatte.py` - matte refinement backend if legal/technical research approves.
- `scripts/flux-ai/backends/matanyone2.py` - video matte refinement backend if legal/technical research approves.
- `scripts/flux-ai/backends/video_depth_anything.py` - accepted video-depth backend.
- `t083-research/legal-model-matrix.md` - required deep legal findings.
- `t083-research/technical-benchmark-matrix.md` - required deep technical findings.

## Dependencies
- Task 2 blocks bundling/default-install decisions for SAM 3+, VitMatte, MatAnyone2, and Video Depth Anything.
- Task 3 depends on approving CUDA-first local runtime and Python worker strategy.
- Task 5 depends on legal/technical research confirming whether HuggingFace gated models or tokens are required.
- Task 8 depends on locating/approving the safest viewer overlay/prompt integration points.
- Task 10 depends on Task 9 producing a base matte and Task 2 approving VitMatte or MatAnyone2 usage.
- Task 11 depends on project-relative artifact policy from Task 14 and existing P6 mask graph invariants.
- Task 15 depends on legal clearance and at least one tested vertical slice.

## Open Legal Questions for Deep Research Agents
- What exactly are the license and commercial-use terms for SAM 3/SAM 3.1 code, weights, and any gated HuggingFace distribution?
- Can Flux GPL2 legally bundle SAM 3+ code/weights, or must it download on first use / require user-supplied weights / show license acceptance?
- Do VitMatte and MatAnyone2 permit commercial use, redistribution, bundled install, and derivative integration in GPL2 software?
- Does Video Depth Anything permit bundled/default install of code and weights under Apache-2.0 terms, including any third-party pretrained components?
- Which dependencies introduce incompatible licenses or redistribution restrictions: PyTorch/CUDA wheels, xFormers, transformers, HuggingFace hub, model-specific custom ops?
- If a HuggingFace token is required, what are the legal/ToS constraints around asking for and storing a user token locally?
- Are there model export restrictions, dataset-use restrictions, research-only clauses, field-of-use limits, or attribution requirements for any candidate?

## Open Technical Questions for Deep Research Agents
- Current best SAM 3+ API for video: supported prompt types, video predictor stability, CUDA requirements, model sizes, and how to run on Fedora/Python 3.14 environment.
- Whether SAM 3+ can produce temporally propagated masks directly or requires separate propagation/refinement for long clips.
- Best bridge from viewer selection to SAM 3+ prompts: point, box, scribble, mask seed, text concept, or combined prompts.
- Whether VitMatte accepts SAM-generated masks/trimaps cleanly for video; what preprocessing creates a good trimap from base matte.
- Whether MatAnyone2 can use SAM 3+ mask/selection as target guidance and whether it is practical on RTX 4090 for 1080p/4K clips.
- Exact project-relative artifact format: EXR alpha/depth vs PNG alpha, bit depth, colorspace, frame numbering, metadata schema.
- Best nodegraph representation for generated AI matte/depth nodes within Natron/Flux without destabilizing existing P6 graph rebuild logic.
- Encrypted token storage options on Fedora KDE/Qt6: KWallet via QtKeychain/libsecret/Secret Service, dependency availability, fallback behavior.
- CUDA installer strategy: system CUDA vs PyTorch bundled CUDA wheels vs distro packages; compatibility with NVIDIA driver 595.71.05.
- Expected VRAM/time/cache sizes for 100/500/1000-frame clips at 1080p and 4K for SAM 3+, VitMatte, MatAnyone2, and Video Depth Anything.

## Risks
- SAM 3+ may have legal restrictions that prevent bundling/default install despite being the desired first backend.
- VitMatte/MatAnyone2 may be technically ideal but legally blocked or too heavy for default distribution.
- Python/PyTorch/CUDA dependency footprint may complicate the currently clean Flux installer.
- Viewer prompt tools require careful integration with Natron viewer overlays and must not break existing transform/text/roto interactions.
- Generated EXR sequences can become very large; project-relative portability must include cleanup/relink UX.
- AI output is raster in T083; users expecting editable roto splines need a clearly deferred follow-up.
- Product completion requires actual Flux GUI proof, save/reopen, render/export, and Nick acceptance; model demos alone are not enough.
