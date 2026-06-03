# Planner Report

## Status
ready

## Rationale
This plan is sufficient and scoped because Nick supplied the official HF API split and the existing probe is a single-file standalone script whose current failure mode is localized to model/processor selection, prompt execution, postprocessing, and output artifact writing. The task can be completed by editing only the probe while preserving its honest non-fabricating behavior and existing CLI/result contract.

# Task Packet

## User Goal
Update `tools/ai/sam3_transformers_real_inference_probe.py` so the SAM3 Transformers real-inference probe follows the official Hugging Face `facebook/sam3` usage supplied by Nick: text proof through `Sam3Model`/`Sam3Processor`, box proof through the official PCS API unless code evidence shows tracker is required, point proof through `Sam3TrackerModel`/`Sam3TrackerProcessor`, and real mask plus overlay outputs for text, box, and point prompts.

## Mode
general-coding

## Relevant Locations
- file: `tools/ai/sam3_transformers_real_inference_probe.py`
  symbol: module imports / `transformers_backend`
  approximate lines: 1-218
  stable anchor: `def transformers_backend(model_path: Path, device: str) -> dict[str, Any]:`
  reason: currently loads only `Sam3Model`/`Sam3Processor`, infers point support from that processor, and has no tracker backend.
  confidence: high
- file: `tools/ai/sam3_transformers_real_inference_probe.py`
  symbol: `postprocess_mask`, `save_mask`, `run_prompt`
  approximate lines: 134-258
  stable anchor: `def postprocess_mask(processor: Any, output: Any, source_size: tuple[int, int]) -> Any:`
  reason: current postprocessing is generic and only tries `post_process_instance_segmentation`; official docs require PCS `post_process_instance_segmentation` for text/box and tracker `post_process_masks` for point/PVS.
  confidence: high
- file: `tools/ai/sam3_transformers_real_inference_probe.py`
  symbol: `run_real`
  approximate lines: 261-325
  stable anchor: `result["proofs"]["text"] = run_prompt(backend, "text", image, args.text, output_dir / "mask_text.png", image.size, np, Image)`
  reason: currently runs all three proof types through the same backend and only writes `mask_*.png`, not `overlay_*.png`.
  confidence: high
- file: `tools/ai/sam3_transformers_real_inference_probe.py`
  symbol: `build_parser`
  approximate lines: 328-341
  stable anchor: `p.add_argument("--point", type=parse_point, default=parse_point("200,220,1"), help="point prompt x,y,label")`
  reason: CLI should remain compatible; only adjust help/defaults if required by official tracker API shape.
  confidence: high

## Allowed Edit Files
- `tools/ai/sam3_transformers_real_inference_probe.py`

## Read-Only Context Files
- `/home/npittas/.pi/agent/COLLABORATION.md`
- `AGENTS.md`
- `ARCHITECTURE.md`
- `plans/PHASES.md`
- `tasks/TASKS.md`

## Required Change
1. Replace the single-backend design with explicit official API paths inside the same probe file:
   - PCS image backend: import/load `Sam3Model` and `Sam3Processor` from `transformers`, using the existing manifest-resolved local model path and `local_files_only=True`; move model to selected device and set eval mode.
   - PVS image tracker backend: import/load `Sam3TrackerModel` and `Sam3TrackerProcessor` from `transformers`, using the same local path and `local_files_only=True`; move model to selected device and set eval mode.
   - If tracker classes are unavailable in the installed Transformers build, report point proof as blocked with a precise runtime blocker; do not claim point unsupported by SAM3 generally.
2. Implement prompt execution according to the official HF split Nick supplied:
   - Text proof: use `Sam3Processor`/`Sam3Model` with text prompt and postprocess via `Sam3Processor.post_process_instance_segmentation(...)`.
   - Box proof: use `Sam3Processor`/`Sam3Model` with box prompt if the processor call signature/docs-supported kwargs accept it; postprocess via `post_process_instance_segmentation(...)`. Only use tracker for box if direct source/API inspection during implementation shows the official PCS processor path cannot accept box prompts in the installed HF package; if switching, record that backend choice in result JSON.
   - Point proof: use `Sam3TrackerProcessor`/`Sam3TrackerModel` with point coordinates/labels and postprocess via `Sam3TrackerProcessor.post_process_masks(...)`.
3. Add overlay output support following Nick's official-doc anchor:
   - Prefer importing and using the official `overlay_masks(image, results["masks"])` helper if it is exposed by the installed package/docs path.
   - If the helper is not importable, stop and report the exact missing import/API as a blocker unless implementation can identify an official local equivalent in the installed package without broad refactoring.
   - Save `overlay_text.png`, `overlay_box.png`, and `overlay_point.png` beside existing `mask_text.png`, `mask_box.png`, and `mask_point.png`.
4. Keep mask saving honest and verifiable:
   - Continue to save binary mask PNGs for each proof.
   - Ensure `save_mask` can handle the official postprocessed mask structures returned by both PCS and tracker postprocessors.
   - Preserve nonzero pixel accounting and fail the individual proof if no mask array is returned or the saved mask/overlay is missing or empty.
5. Update the JSON result payload minimally:
   - Record separate backend metadata for PCS and tracker/PVS, including class names and capability/status.
   - For each proof, include `mask_path`, `overlay_path`, `nonzero_pixels`, and a precise `runtime_blocker` on failure.
   - Preserve the top-level schema string unless a schema bump is necessary; if bumped, keep old fields compatible where practical.
6. Preserve existing CLI behavior and exit semantics:
   - `--self-check` remains dependency/model readiness only, but may add availability of `Sam3TrackerModel`, `Sam3TrackerProcessor`, and official overlay helper to diagnostics.
   - Real run returns `0` only if text, box, and point proofs all succeed; otherwise non-zero as currently.
   - Do not add video, mask-paint, download, GUI, or Flux integration work.

## Non-Goals
- Do not implement `Sam3VideoModel`, `Sam3TrackerVideoModel`, video propagation, later-frame correction, or mask-paint prompts.
- Do not edit model manager, manifests, task trackers, GUI files, C++ files, or docs.
- Do not fabricate fallback masks or treat pipeline mask generation as proof of prompted SAM3 behavior.
- Do not change model installation paths, download behavior, or authentication/token handling.

## Validation
Commands:
- `python -m py_compile tools/ai/sam3_transformers_real_inference_probe.py`
- `python tools/ai/sam3_transformers_real_inference_probe.py --self-check --json`
- `python tools/ai/sam3_transformers_real_inference_probe.py --image <real_test_image_path> --output-dir <empty_output_dir> --text "<object text prompt>" --box "x1,y1,x2,y2" --point "x,y,1" --device auto --json`

Expected result:
- Compile command exits `0`.
- Self-check JSON reports installed dependencies/model status and explicitly reports whether PCS classes, tracker classes, and official overlay helper are available.
- Real run exits `0` only when text, box, and point proofs succeed.
- Output directory contains non-empty `mask_text.png`, `mask_box.png`, `mask_point.png`, `overlay_text.png`, `overlay_box.png`, `overlay_point.png`, and `sam3_transformers_real_inference_result.json`.
- Result JSON records successful proof statuses and file paths for all three proof types, or precise blockers if the local HF package lacks required official APIs.

## Stop Conditions
Stop and report if:
- target symbol is missing
- required fix exceeds allowed files
- validation cannot run
- existing architecture contradicts the requested change
- task requires product/design judgment not in packet
- installed Transformers package does not expose `Sam3TrackerModel`/`Sam3TrackerProcessor` and no official local equivalent is identifiable from the target file/imported package API
- official `overlay_masks(image, results["masks"])` helper or an official local equivalent cannot be imported/used
- point proof would require using `Sam3Model`/`Sam3Processor` instead of the tracker API
- any proof would require fabricated masks, placeholder overlays, or non-official API guesses

## Required Return Contract
Return only a task-focused summary. Do not include transcript, tool logs, raw file dumps, large code blocks, or broad unrelated issues. Include status, files inspected/changed, validation evidence, blockers, and task-specific risks.
