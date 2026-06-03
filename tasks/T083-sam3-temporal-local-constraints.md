# T083 SAM3 Temporal / Video Workflow — Local Code Constraints Brief

## Status
constraints-only

## Purpose
Local-code constraints for planning SAM3 video/temporal workflow work. This is not a final implementation plan. It records verified current behavior, likely edit surfaces, source-frame export options, manifest/history implications, validation targets, and stop conditions for the next planner/implementation pass.

## Verified Current State
- `tools/ai/sam3_transformers_worker.py` is a long-lived JSON-lines worker, but its command surface is still-only: `status`, `load`, `infer_still`, `unload`, `cancel`, `shutdown`. Unknown commands raise `ValueError`.
- `Worker.infer_still(...)` accepts one exported still image plus point/box/text prompts, writes `sam3_transformers_real_inference_result.json`, per-prompt masks, and `mask_live_combined.png` when any prompt succeeds.
- `tools/ai/sam3_transformers_real_inference_probe.py` exposes still-image real inference helpers and CLI args for `--image`, `--output-dir`, `--prompt-kind`, `--source-width`, and `--source-height`; no temporal/video CLI contract is present in the inspected anchors.
- `Gui/FluxAiPanel.cpp` uses the worker command `infer_still` for both persistent Run and live preview. Live preview writes to `FluxGenerated/AI/live/sam3_transformers/current/`; Run writes to `FluxGenerated/AI/<task>/<model>/<runId>/`.
- `Gui/Gui05.cpp` exports only a single source frame as a temporary PNG under `QDir::temp()/Flux/sam3_preview`. Metadata records selected layer, timeline frame, source frame, original frame range, time offset, render mode, dimensions, and notes that the temporary source PNG path is intentionally not persisted.
- Result history is still-run oriented: `FluxAiPanel` stores project-relative manifest paths in `_resultManifestHistoryProjectRelative`, promotes one last manifest, caps history at 50, displays labels from `run_id`, created time, `source_metadata.source_frame`, model, prompt count, and selected mask basename.

## Likely Edit Surfaces for Temporal Planning
- `tools/ai/sam3_transformers_worker.py`
  - Add a new explicit temporal command only after an approved contract exists, e.g. `infer_video` or `infer_temporal`.
  - Preserve existing `infer_still` request/result compatibility for live current-frame preview and existing history.
  - Define cancellation semantics up front; current cancel is cooperative bookkeeping and cannot interrupt active synchronous inference safely.
- `tools/ai/sam3_transformers_real_inference_probe.py`
  - Reuse model/backend discovery where possible, but do not assume the detected local SAM3 API supports temporal propagation without probing a real API path.
  - Any temporal helper must report honest blockers like the still probe; no fake propagation or fabricated masks.
- `Gui/FluxAiPanel.h` / `Gui/FluxAiPanel.cpp`
  - Add request/result state for temporal runs separately from still `_sam3SourcePng`, `_sam3SourceMetadata`, and `_sourceFramePng` fields.
  - Extend manifest/history display without breaking existing selected still-mask preview/apply paths.
  - Keep prompt ownership in AI Paint/viewer toolbar; AI Panel may summarize prompts and run/apply/history only.
- `Gui/Gui05.cpp` / `Gui/Gui.h`
  - Source export is the main local constraint. Existing `exportFluxSam3SourceFrameForSourceContext(...)` is one-frame-only and ffmpeg-based.
  - A temporal workflow needs a new export/query path or an approved reuse of original media path plus source frame range metadata.
- `Gui/FluxTimeline.h` / `Gui/FluxTimeline.cpp`
  - Timeline layer fields already separate source frames (`inPoint`, `outPoint`, `originalInPoint`, `originalOutPoint`) from `timeOffset`; temporal planning must preserve this separation.
  - Any video range selection should derive source-frame ranges from layer trim/current timeline context without changing trim/move semantics.

## Source-Frame / Source-Range Export Options
1. **Original-media path + metadata contract**
   - For video footage, pass original source file path plus source frame range, original first/last frame, time offset, and prompt frame.
   - Lowest local export cost and preserves temporal context, but worker must own media decode and frame indexing correctly.
   - Requires explicit handling for still images, image sequences, variable frame-rate media, and ffmpeg frame indexing.
2. **Project-relative extracted frame sequence**
   - Export selected source range to `FluxGenerated/AI/.../source_frames/` and pass an ordered manifest to the worker.
   - More deterministic for SAM3 temporal inference and persistence; larger disk footprint and more export time.
   - Must avoid temp-only source paths for persistent Run results.
3. **Hybrid**
   - Keep current temp PNG for live current-frame preview; use original-media or extracted sequence only for persistent Segment/Track temporal Run.
   - Best preserves current UX while limiting temporal complexity to explicit run mode.

## Manifest / History Implications
- Existing still manifest shape must remain readable: selected mask path, source metadata, prompts, model, run id, and created time.
- Temporal manifests likely need a new schema/version and fields for:
  - source media path policy: project-relative source reference where possible, or explicit external absolute source with reload warnings;
  - source frame range, timeline frame range, prompt/reference frame, original first/last frame, time offset;
  - output kind: single mask, mask sequence, track sequence, alpha sequence, foreground/depth if applicable;
  - output sequence pattern and frame numbering contract;
  - per-frame proof/errors and temporal backend capability/blocker metadata;
  - prompts with frame/time provenance.
- Result history labels should distinguish still vs temporal runs and show range/sequence summary, not just one `source_frame`.
- Preview/apply paths currently expect a selected single mask file. Temporal results need separate preview/apply contracts for sequences; do not overload `selected_mask_path_project_relative` without compatibility rules.
- Generated media for persistent temporal Run must be project-relative under `FluxGenerated/AI/...`; temp PNG source exports must not be persisted as source of truth.

## Validation Targets for a Future Temporal Plan
- Python:
  - `python3 -m py_compile tools/ai/sam3_transformers_worker.py tools/ai/sam3_transformers_real_inference_probe.py`
  - Worker JSON-lines smoke test: `status`, `load`, temporal command with honest blocked/ok result, `cancel`, `unload`, `shutdown`.
  - Real short video or extracted sequence test with non-empty mask sequence if local SAM3 temporal API supports it; otherwise proof of blocker with no fabricated outputs.
- C++/GUI:
  - `cmake --build /home/npittas/Flux/build --target Natron -j$(nproc)`
  - Saved-project preflight still forces Save As before persistent generated media.
  - Current still live preview and still Run continue to work.
  - Temporal Run writes project-relative generated media and manifest; save/reopen preserves history.
  - GUI proof: source layer selected, prompt controls visible in viewer/AI Paint, temporal run result/history visible, and resulting sequence preview/apply behavior shown.
- Media correctness:
  - Validate frame indexing against a clip with visible frame numbers or motion.
  - Verify prompt/reference frame maps to correct source frame after layer trim and timeOffset.
  - Verify output sequence dimensions match source export dimensions.

## Stop Conditions
Stop and request a new locator/planning pass if:
- The local SAM3/Transformers API cannot expose a real temporal/video propagation path; do not simulate temporal tracking with repeated still masks unless Nick explicitly approves that fallback.
- The task requires deciding whether temporal input should be original media, extracted sequence, or hybrid without Nick/orchestrator approval.
- Applying temporal outputs would require new mask/effect graph semantics beyond the approved raster generated-media approach.
- Source frame/range mapping contradicts existing FluxTimeline trim/timeOffset architecture.
- Persistent generated media would require absolute temp source paths or unsaved-project writes.
- Validation cannot include real media/frame-index proof for any claimed temporal behavior.

## Locator Evidence Used
- `tools/ai/sam3_transformers_worker.py`: `Worker.status`, `load`, `infer_still`, `unload`, `cancel`, command dispatch around lines 39-220.
- `tools/ai/sam3_transformers_real_inference_probe.py`: still-image probe helpers and CLI around lines 252-429.
- `Gui/FluxAiPanel.cpp`: source context, source-frame metadata refresh, `infer_still` live preview/run requests, result history management around inspected ranges 261-360, 520-1320.
- `Gui/FluxAiPanel.h`: SAM3 worker/process state and still/source metadata fields around lines 45-150.
- `Gui/Gui05.cpp`: source frame export and timeline/source frame mapping around lines 745-894 and source context setup anchors around 1329-1530.
- `Gui/FluxTimeline.h`: layer source-frame/trim/timeOffset fields and source-frame capture signal around lines 120-266.
- `tasks/T083-ai-paint-live-sam3-workflow-plan.md` and `tasks/T083-ai-matte-depth.md`: authorized T083 workflow constraints and Nick-approved AI/prompt/generated-media decisions.
