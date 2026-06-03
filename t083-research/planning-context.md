# T083 Planning Context Brief — AI Matte/Mask Generation and Video Depth Tools

Scope: repository-docs-only brief for planning T083. Sources read: `AGENTS.md`, `ARCHITECTURE.md`, `plans/PHASES.md`, `tasks/TASKS.md`, and relevant task docs for masks/roto/text/plugin deployment (`T064`, `T065`, `T069`, `T070`, `T084`, `FLUX_MOTION_TEXT_RECOVERY_SOURCE_OF_TRUTH`). No source-code inspection or web research was performed for this brief.

## Approved product goal

T083 is a pending P7 task: add AI-assisted matte generation/extraction and video depth tools to Flux. The goal is explicitly **multi-model** and **video-oriented**, not a one-model shortcut.

Approved direction from repository docs:

- Flux is a Linux-first, Qt6/Natron-based 2D motion graphics compositor with an After Effects-style layer timeline; node graph remains available for power users.
- T083 must research and integrate AI matte/mask generation with multiple model choices.
- Initial candidate families named in the docs: SAM 3.1, MatAnyone/MatAnything-style video matting, BFRNet or similar helpers where useful, plus other strong current video segmentation/matting models.
- T083 must also research and integrate **video-stable depth estimation**.
- Depth Anything must **not** be assumed sufficient; Nick explicitly flagged it as weak for video.
- Implementation should be model-pluggable and compare current online model options plus local integration constraints before coding.

## Existing Flux context T083 must fit

Flux already has the required host-side infrastructure for AI-assisted matte/depth tools to attach to:

- Layer-based timeline, effects, masks, keyframes, import/export, and UI shell are already implemented.
- Native masks are timeline tree children under layers/effects.
- Layer masks use the approved inline graph pattern:
  - `source/effects → [Unpremult] → Roto → Premult → downstream layer Merge A`
  - no Merge(in) layer-mask implementation.
- Effect masks are side branches:
  - `Reformat → Roto` into the effect mask input.
- Roto/RotoPaint now includes `Zero selected input channels` / `replaceSelectedChannels`, required for clean alpha replacement before drawing/compositing mask shapes.
- Manual nodegraph edits and precomp branches must be preserved; Flux rebuilds must not destroy user-added nodes.
- OpenFX provider/plugin deployment is now handled by repository-source-of-truth installer logic and installed under Flux-specific prefixes.

## Constraints and invariants

### Product / approval constraints

- Nick owns high-level product, UX, architecture, workflow, and repo-policy decisions.
- Before non-trivial coding, present a concrete plan, user-visible behavior, and risks; wait for Nick approval.
- Any model choice, UX flow, local/remote inference architecture, dependency footprint, GPU/runtime policy, cache/storage policy, or licensing strategy is a Nick-approval decision before implementation.
- Do not silently reduce T083 to a single model, image-only workflow, or Depth Anything-only prototype.

### Architecture constraints

- Flux remains Qt widgets + Natron engine, not Electron/web UI.
- Natron engine/core is kept; Flux adds UI/bridge layers.
- New code should follow C++17 / CMake / Qt6 patterns and existing Natron style.
- New files should stay under the 500-line guideline.
- OpenFX plugins are built/deployed separately from the main app where appropriate.
- User-facing setup docs must use portable variables (`$FLUX_ROOT`, `$BUILD_DIR`, `$PLUGIN_PREFIX`, `$OFX_USER_PLUGIN_DIR`, `$HOME`, `$XDG_CACHE_HOME`) and avoid developer-machine absolute paths.

### Mask/roto graph constraints

- Do not reintroduce layer-mask Merge(in). Layer masks must remain inline Roto/Premult.
- Effect masks remain mask-input side branches and must not be classified as precomp/manual branches.
- Rebuilds must preserve existing nodes, manual branches, and user edits; no destructive graph cleanup except narrow Flux-owned artifact migration/removal.
- If AI generates or updates mask data, it should integrate with native Roto/RotoPaint/mask rows or an approved new mask/depth representation without breaking existing P6 semantics.

### Plugin/deployment constraints

- Installer treats repository plugin payloads as source of truth: top-level `plugins/*.py`, `.ofx.bundle` under `plugins/` and `plugins/ofx-extras/`, plus community PyPlug trees.
- Required plugin sets remain explicit; missing core payloads should fail deploy/check instead of silently producing a partial install.
- OFX binaries must be `ldd`-clean and runtime-discoverable via scoped cache validation.
- Avoid sudo unless explicitly approved.
- Use clean VM/container/distrobox validation for installer/end-to-end deploy changes, not production-host mutation.

## Non-goals / forbidden shortcuts

- No `.aep` compatibility is a project note; T083 should not pivot into After Effects project import.
- Do not replace the Qt/Natron architecture with a Python/web ML application shell.
- Do not assume Depth Anything is acceptable for video depth.
- Do not ship a headless-only or pixel-only proof as product completion; GUI workflow proof is required for user-facing features.
- Do not call a model smoke test a finished Flux feature unless the actual Flux UI workflow, persistence, render/export path, and validation artifacts pass.
- Do not hide or skip broken plugins as a fix; build/install/validate missing providers.
- Do not break or modify FluxMotionText recovery paths; text recovery docs stress that product claims require real GUI validation and Nick acceptance.

## Likely milestones for T083

1. **Research and architecture proposal**
   - Online/current model research for video matting, segmentation, mask propagation, alpha matting, restoration helpers, and video-stable depth.
   - Local feasibility research: Python/C++/OFX integration, GPU requirements, model sizes, licenses, offline operation, packaging burden, cache/storage needs.
   - Produce a Nick-approval plan before coding.

2. **Model-choice abstraction**
   - Define a pluggable model interface and metadata: task type (matte, segmentation, depth), video support, prompts/inputs, output channels, temporal consistency, GPU/runtime needs.
   - Decide with Nick whether inference lives in-process, out-of-process service, Python helper, OFX plugin, or external command bridge.

3. **UX design**
   - Decide approved user-visible workflow: layer/effect context action, dedicated AI panel, mask-row action, viewer prompt tools, model picker, progress/cancel UI, preview/apply behavior.
   - Define how generated mattes/depth maps become Flux artifacts: Roto/RotoPaint shapes, alpha masks, generated image/depth layers, effect mask branches, or cached sequences.

4. **Prototype one approved vertical slice**
   - Use real video footage, not still-only test data.
   - Generate a matte/mask or depth sequence, attach it to the approved Flux graph path, scrub playback, save/reopen, and render/export.

5. **Multi-model expansion**
   - Add at least a second model path to prove the model-choice architecture is real.
   - Compare quality/stability/performance across models on the same real clip.

6. **Polish and validation**
   - Progress/cancel, error handling, cache invalidation, dependency checks, installer/deploy updates, and documentation.

## Required user-visible behavior

Minimum behavior should be approved by Nick before coding, but repository docs imply T083 should eventually let a user:

- Select a layer, effect, or mask context in the timeline.
- Choose an AI matte/mask/depth action from a visible UI control/panel/context menu.
- Pick among supported models rather than being forced into one hardcoded model.
- Run inference on real video footage with progress and cancellation.
- Preview the resulting matte/depth over time in the viewer by scrubbing playback.
- Apply generated output to Flux-native structures:
  - layer masks via inline Roto/Premult path, or
  - effect masks via side mask input, or
  - generated depth/matte layers/nodes if approved.
- Save/reopen the project with generated artifacts still connected and usable.
- Render/export a short sequence with the generated matte/depth affecting output.

## Validation expectations

General project validation rules apply:

- Use real-world artifacts. For T083 this means actual video clips, not only single still images.
- Validate import, rendering, performance, color/alpha correctness where relevant.
- For GUI behavior: screenshot or recording, or it did not happen. Proof must show the actual UI controls/panels/dropdowns/progress and the resulting viewport behavior.
- Viewport pixels alone, headless scripts, model output files, or narrow review verdicts are not enough for product completion.

Suggested T083 validation matrix:

- Real `.mov` and `.mp4` clips imported as Flux footage layers.
- At least one foreground subject matte/mask test across multiple frames with motion.
- At least one difficult edge case: hair/soft edges/motion blur/transparent edge if available.
- At least one depth test on moving camera or moving subjects to assess temporal stability.
- Scrub/playback before and after applying generated result.
- Save/reopen project and verify generated artifacts persist.
- Render/export a short sequence and visually compare output.
- Measure inference time, preview/render performance, cache size, GPU/CPU memory use.
- Screenshots/recordings showing model picker, controls, progress, applied mask/depth, and viewer response.

## Decisions requiring Nick approval before coding

Ask Nick before implementing any of the following:

- Final model shortlist and first vertical-slice model(s).
- Whether models are bundled, downloaded on demand, user-supplied, or system-installed.
- Online/cloud inference vs local-only inference. Default should not assume cloud.
- Python runtime strategy, virtualenv/embedded dependency approach, CUDA/ROCm/CPU policy, and whether large ML dependencies are acceptable in installer scope.
- UI workflow: AI panel vs timeline context actions vs mask-row actions vs viewer prompt tools.
- Output representation: native Roto shapes, raster alpha/depth sequences, OFX-generated images, sidecar cache, or new node/layer types.
- Cache location, invalidation, project-relative storage, portability, and cleanup policy.
- Licensing risk for any model/code/weights.
- Any installer changes that add heavyweight dependencies, sudo requirements, or non-Fedora portability assumptions.

## Repository docs / evidence map

- `AGENTS.md`: approval gate, git restrictions, mandatory validation, screenshot requirement for GUI features, Qt/Natron architecture.
- `ARCHITECTURE.md`: approved product direction; AI-assisted matte/depth is a planned core feature; layer-to-node mapping and mask/effect graph semantics; OpenFX plugin/deploy notes; real-world validation requirements.
- `plans/PHASES.md`: P7 active; T083 pending; T083 must research multiple models and video-stable depth, with Depth Anything not assumed sufficient.
- `tasks/TASKS.md`: T083 exact pending task statement and current status.
- `tasks/T064-fix-layer-mask-inline-roto-premult.md`: approved inline layer-mask graph and no Merge(in).
- `tasks/T065-roto-replace-selected-channels.md`: native Roto/RotoPaint replace-selected-channel behavior needed for clean masks.
- `tasks/T069-ofx-plugin-restoration.md`: plugin restoration policy and runtime registry/cache validation.
- `tasks/T070-linux-workstation-installer.md`: Fedora-first installer/deploy constraints and validation patterns.
- `tasks/T084-plugin-payload-discovery.md`: repository plugin payload discovery/deploy source of truth.
- `tasks/FLUX_MOTION_TEXT_RECOVERY_SOURCE_OF_TRUTH.md`: strong process lesson: distinguish groundwork from product completion; GUI workflow and Nick acceptance are required for user-facing claims.
