# Agent Summary

## Status
Done

## Task Completed
Evaluated MatAnyone2 under VRAM limits. Recommendation for Flux: **do not implement “32-frame chunks” as the primary path**. MatAnyone2 already has an internal recurrent/memory system; use a **streaming worker around `InferenceCore.step()`** and configure/patch its memory budget, instead of repeatedly calling `process_video()` on 32-frame chunks. If upstream API cannot be safely patched, use **chunked sequential windows with overlap and last/overlap mask re-seeding** only as a fallback, because it resets model memory and can create boundary flicker/drift.

## Files Inspected
- `tasks/T083-ai-matte-depth.md`: confirmed MatAnyone2 is planned as a legal-gated/non-commercial video matte refinement provider in the external Python worker architecture.
- `t083-research/deep-required-models.md`: confirmed MatAnyone2 IO contract: source video + first-frame/base mask → alpha/foreground sequence, with non-commercial warning.
- `t083-research/revised-synthesized-plan.md`: confirmed Flux’s approved generated-media, CUDA worker, model-pluggable design.
- `tools/ai/model_manifest.json`: confirmed MatAnyone2 manifest entry is user-download/non-commercial and not bundled by default.
- Upstream `pq-yang/MatAnyone2/matanyone2/inference/inference_core.py`: found `process_video()` loads the full video into memory and accumulates outputs; public API does not expose a 32-frame rolling-window option. Internal `step()` supports frame-by-frame inference and memory updates.
- Upstream `pq-yang/MatAnyone2/matanyone2/inference/memory_manager.py`: confirmed FIFO-style memory limiting exists through `max_mem_frames` when long-term memory is disabled.
- Upstream `pq-yang/MatAnyone2/matanyone2/config/eval_matanyone_config.yaml`: confirmed defaults: `use_long_term: False`, `mem_every: 5`, `max_mem_frames: 5`, `chunk_size: -1`, `max_internal_size: -1`.

## Files Changed
- None.

## Validation
- command: `not run`
- result: not run
- evidence: Research-only task; no code edits or executable validation requested. Upstream source inspection was sufficient to answer the strategy question.

## Blockers / Decisions Needed
- None for the recommendation.
- Product/legal reminder: MatAnyone2 remains non-commercial/user-download only per T083 policy.

## Risks / Follow-up
- **Best path:** implement Flux MatAnyone2 worker using streamed frame decode → `InferenceCore.step()` → immediate alpha/foreground frame write, with controls for `max_internal_size`, `mem_every`, `max_mem_frames`, precision, and optional long-term memory.
- **Interpretation caveat:** `max_mem_frames=32` limits model memory frames/tokens, not necessarily “exactly 32 raw frames in a temporal batch.”
- **Fallback:** if upstream streaming patch is too risky, process 32-frame chunks with 4–8 frame overlap, seed each chunk from the last accepted/overlap alpha, discard overlap outputs, and warn that seams/flicker are possible.
- **Avoid:** naive non-overlapped 32-frame `process_video()` calls; this resets memory every chunk and is the most likely to produce temporal discontinuities.