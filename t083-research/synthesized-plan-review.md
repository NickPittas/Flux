I did not write `/home/npittas/Flux/t083-research/synthesized-plan-review.md` because this request also says “Do not edit files,” and the no-edit instruction wins. Findings below are ready to paste into that file.

```md
# T083 Synthesized Plan Review — AI Matte/Mask Generation and Video Depth Tools

## Recommended Architecture

Use a **model-pluggable, out-of-process AI inference bridge**.

Flux should not link ML frameworks directly into the C++/Qt/Natron process. Instead:

- Flux C++/Qt UI launches or talks to an external Python inference worker.
- Flux exports selected source frames/proxy frames plus prompt metadata and frame range.
- The worker runs a selected backend model.
- The worker writes generated outputs as project/cache artifacts:
  - alpha/matte sequences: EXR or 16-bit PNG
  - depth sequences: EXR preferred
  - JSON metadata: source layer, model, prompts, frame range, normalization, confidence/quality fields when available
- Flux imports/attaches the generated sequence into existing timeline mask/depth workflows.

Initial backend policy:

- **Ship/enable first only with legally safer models/adapters**:
  - SAM 2.1 for promptable video segmentation/masks.
  - Video Depth Anything as the safer Apache-2.0 video-depth baseline.
  - Optional MIT/Apache fallback/refiner candidates after testing.
- Keep higher-risk or unclear-license models as **user-installed backend slots** until reviewed:
  - SAM 3/SAM 3.1
  - MatAnyone / MatAnyone2
  - DepthCrafter
  - Cutie if license remains unclear
  - RVM/XMem2 if GPL3 contamination is an issue.

Host-side integration should reuse existing Flux systems:

- `Gui/FluxTimeline.h`
  - Extend `FluxMask` or add adjacent AI generation metadata.
- `Gui/FluxTimeline.cpp`
  - Add UI actions such as “Generate AI Matte…” from layer/effect/mask context.
- `Gui/Gui05.cpp`
  - Reuse existing layer/effect mask wiring.
  - Do not reintroduce layer-mask `Merge(in)`.
- `Gui/FluxTimelineSerialization.h`
  - Persist generated AI artifact metadata and node references.
- Optional new files:
  - `Gui/FluxAIPanel.{h,cpp}` — model picker, prompts summary, progress/cancel.
  - `Gui/FluxAIJob.{h,cpp}` — job state, process invocation, progress parsing.
  - `scripts/flux-ai/` or similar — Python worker/adapters, only after approved.

Generated masks should initially be treated as **raster matte sources**, not converted to roto splines. Roto conversion can be a later enhancement.

## First Vertical Slice

Recommended first slice: **Generate AI layer mask from a footage layer using SAM 2.1 or a lightweight placeholder-compatible adapter, then attach the generated matte sequence to Flux’s existing layer-mask path.**

Concrete first-slice behavior:

1. User imports a real `.mov` or `.mp4` footage layer.
2. User right-clicks the layer or uses an AI panel action:
   - `Generate AI Matte…`
3. UI shows:
   - model picker
   - frame range
   - prompt type placeholder/initial input, e.g. box/point/mask seed
   - output location
   - run/cancel/progress
4. Flux invokes external Python worker.
5. Worker writes a matte sequence and metadata JSON.
6. Flux creates/updates a timeline mask row for the layer.
7. Flux wires the generated matte into the approved layer-mask graph path.
8. User can scrub playback and see the matte affect the layer.
9. Save/reopen preserves the generated matte reference and mask row.
10. Render/export includes the applied matte.

Acceptance for first slice:

- Real video clip, not still-only.
- GUI proof showing model picker/action, progress, generated mask row, and viewer result.
- Save/reopen proof.
- Short render/export proof.
- Inference time, cache size, and GPU/CPU memory noted.

Depth should be the **second vertical slice**, not the first, because the mask graph already exists and is lower integration risk.

## Exact Decisions Needing Nick Approval

Before implementation, Nick should approve:

1. **First backend**
   - SAM 2.1 first for mask generation?
   - Video Depth Anything first for depth?
   - Whether any unclear-license models may be exposed as user-installed only.

2. **Runtime policy**
   - Local-only inference by default?
   - Any cloud/online inference allowed?
   - Python virtualenv/embedded runtime strategy.
   - CUDA/NVIDIA-first acceptable for initial implementation?

3. **Dependency/install policy**
   - Are heavyweight ML dependencies allowed in the installer scope?
   - Should AI dependencies be optional and separately installed?
   - Should Flux provide a checker only, not install models automatically?

4. **Model/weights policy**
   - Bundle weights, download on demand, or require user-supplied paths?
   - Where model weights live.
   - How to handle license prompts/acceptance for restricted models.

5. **UX entry point**
   - Dedicated AI panel?
   - Timeline context menu?
   - Mask-row action?
   - Viewer prompt tools in first slice or deferred?

6. **Output representation**
   - Raster matte/depth sequences as first-class generated artifacts?
   - Native Roto/RotoPaint conversion deferred?
   - Depth as generated layer, effect input, or utility pass?

7. **Cache/storage policy**
   - Project-relative generated media folder vs user cache.
   - Cleanup behavior.
   - Portability expectations for copied projects.

8. **Model shortlist**
   - Approve the candidate matrix and first benchmark set:
     - SAM 2.1
     - Cutie
     - MatAnyone/MatAnyone2
     - Video Depth Anything
     - DepthCrafter
     - ChronoDepth
     - MODNet/RVM only as fallback/reference.

## Risks

- **Licensing**
  - SAM 3/SAM 3.1 custom license may be unsuitable.
  - MatAnyone/MatAnyone2 and DepthCrafter need license/weights review.
  - RVM/XMem2 GPL3 can contaminate GPL2 distribution if bundled.

- **Runtime complexity**
  - PyTorch/CUDA dependency footprint is large.
  - Fedora packaging may be fragile.
  - GPU memory/time can be high for 4K or long clips.

- **Product risk**
  - A headless model demo is not enough.
  - Must prove real Flux GUI workflow, persistence, and render/export.

- **Graph integration**
  - Must preserve P6 mask invariants:
    - no layer-mask `Merge(in)`
    - preserve manual branches
    - effect masks remain side branches
    - rebuild remains non-destructive

- **Temporal quality**
  - Hard SAM masks may need refinement for hair, motion blur, and transparent edges.
  - Depth models may flicker or normalize inconsistently across shots.

- **Storage/cache**
  - EXR/depth/matte sequences can become large quickly.
  - Cache invalidation and project portability must be explicit.

## Milestone Breakdown

### M1 — Approval Architecture Packet
- Finalize model shortlist, runtime strategy, UX entry point, output representation, and cache policy.
- No code until Nick approves.

### M2 — Local AI Job Skeleton
- Add optional AI job UI/process bridge.
- Implement model registry and backend metadata.
- Worker can run a trivial/test adapter and write a sequence + JSON.
- Validate progress/cancel/error reporting.

### M3 — First Matte Vertical Slice
- Implement SAM 2.1 or approved first mask backend.
- Generate matte sequence from real video.
- Attach output to a layer mask row.
- Save/reopen/render/export validation.

### M4 — Prompt and Correction Workflow
- Add viewer prompt capture if approved.
- Support point/box/mask seed metadata.
- Allow rerun over selected frame range.
- Preserve generated artifacts and user corrections.

### M5 — Second Model Path
- Add Cutie or MatAnyone-style backend if license allows.
- Otherwise use a safe fallback model.
- Compare quality/performance on same clip.
- Prove the model-pluggable architecture is real.

### M6 — Video Depth Vertical Slice
- Add Video Depth Anything first unless Nick approves another model.
- Generate EXR depth sequence.
- Import/attach as approved depth artifact.
- Validate temporal stability on moving-camera/moving-subject footage.

### M7 — Packaging and Validation
- Add optional dependency checker.
- Document model install paths with portable variables.
- Validate in clean VM/container/distrobox, not by mutating production host.
- Produce GUI screenshots/recordings and benchmark notes.

## Implementation-Ready File Targets

Primary likely modifications after approval:

- `Gui/FluxTimeline.h`
  - AI artifact metadata and mask/depth model extensions.

- `Gui/FluxTimeline.cpp`
  - Context menu actions and selection flow.

- `Gui/FluxTimelineSerialization.h`
  - Persistence for generated AI artifacts.

- `Gui/Gui05.cpp`
  - Graph attachment for generated matte/depth sources.

Likely new files:

- `Gui/FluxAIPanel.h`
- `Gui/FluxAIPanel.cpp`
- `Gui/FluxAIJob.h`
- `Gui/FluxAIJob.cpp`
- `Gui/FluxAIModelRegistry.h`
- `Gui/FluxAIModelRegistry.cpp`
- `scripts/flux-ai/worker.py`
- `scripts/flux-ai/backends/*.py`
```
