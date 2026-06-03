# Planner Report

## Status
ready

## Rationale
This plan is scoped to the approved Phase 3 behavior: a node-owned, same-frame SAM3 guide-mask preview for `Flux AI Matte` only, with output render remaining identity/pass-through. The implementation stays adjacent to `Engine/FluxAIMatte*`, adds a narrow ONNX preview worker instead of touching protected AI Paint/AI Panel/SAM3 Transformers/MatAnyone2 flows, and treats the preview as volatile diagnostic UI state rather than final generated media.

# Task Packet

## User Goal
Implement Phase 3 for `Flux AI Matte`: user selects the node, adds frame-specific SAM point/box prompts, clicks an explicit preview/update control, and gets an asynchronous/off-thread SAM3 single-frame guide-mask preview overlay for that same frame only. Node properties must show status/errors. Output render must remain pass-through/no-op.

## Mode
general-coding

## Relevant Locations
- file: `Engine/FluxAIMatte.cpp`
  symbol: `FluxAIMatte::initializeKnobs`, `FluxAIMatte::knobChanged`, `FluxAIMatte::isIdentity`, `FluxAIMatte::render`
  approximate lines: 229-443, 446-464
  stable anchor: `#define kFluxAIMatteParamPromptSummary`, `Flux AI Matte prompt editor. Phase 1+2`, `return true;` in `isIdentity`, `return eStatusOK;` in `render`
  reason: Add Phase 3 preview/update/cancel/status knobs and wire explicit button actions while preserving identity render.
  confidence: high
- file: `Engine/FluxAIMatte.h`
  symbol: `class FluxAIMatte`
  approximate lines: 41-112
  stable anchor: `hasOverlay() const`, `drawOverlay(...)`, `knobChanged(...)`, `render(...)`
  reason: Add minimal preview-controller methods/members declarations as needed; render contract must stay no-op.
  confidence: high
- file: `Engine/FluxAIMattePrivate.h`
  symbol: `struct FluxAIMattePrivate`
  approximate lines: 34-93
  stable anchor: `FluxAIMattePromptContext context;`, `KnobStringWPtr promptSummary;`, `activeTool() const`
  reason: Store preview knob weak pointers, transient preview state/controller handle, current request id, status, and frame metadata.
  confidence: high
- file: `Engine/FluxAIMatteOverlay.cpp`
  symbol: `FluxAIMatte::drawOverlay`, prompt hit/edit handlers
  approximate lines: 96-244, 252-440
  stable anchor: `Only draw prompts that belong to the current frame`, `Active box drag preview`
  reason: Draw the guide-mask preview only when its stored frame matches the rounded viewer frame; keep existing prompt overlay behavior intact.
  confidence: high
- file: `Engine/FluxAIMattePromptContext.h`
  symbol: `FluxAIMattePrompt`, `FluxAIMattePromptContext::prompts`
  approximate lines: 32-75
  stable anchor: `FluxAIMatteGuideTarget::SAM`, `FluxAIMattePromptRole::Positive`, `FluxAIMattePromptRole::Negative`
  reason: Existing prompt model provides target/type/role/time/point/rect data needed for SAM prompt contract. Do not change unless a compile-required helper is truly necessary.
  confidence: high
- file: `Engine/FluxAIMattePromptContext.cpp`
  symbol: `serialize`, `deserialize`
  approximate lines: 268-357
  stable anchor: `schema`, `FluxAIMattePromptContext`, `metadata`
  reason: Existing prompt persistence should remain untouched; use it as the read-only source for prompt state and do not persist preview pixels here.
  confidence: high
- file: `Engine/EffectInstance.h`
  symbol: `EffectInstance::getImage`, `RenderActionArgs`
  approximate lines: 956-984, 1065-1078
  stable anchor: `virtual StatusEnum render`, `ImagePtr getImage(int inputNb, const double time`
  reason: Candidate API for main/UI-thread-only source-frame extraction, or for an existing Natron render-scheduled extraction path if safely located; do not call `EffectInstance::getImage()` from a raw background/QtConcurrent worker and do not run SAM inference inside `render`.
  confidence: medium
- file: `Engine/FluxAIMaskCopy.cpp`
  symbol: `FluxAIMaskCopy::render`
  approximate lines: 129-169
  stable anchor: `getImage(0, args.time`, `eStorageModeRAM`, `pasteFrom`
  reason: Read-only pattern for RAM source image access and component handling when extracting a frame preview input.
  confidence: medium
- file: `Engine/CMakeLists.txt`
  symbol: `file(GLOB NatronEngine_HEADERS *.h)`, `file(GLOB NatronEngine_SOURCES *.cpp)`
  approximate lines: 20-73
  stable anchor: `add_library(NatronEngine STATIC ${NatronEngine_HEADERS} ${NatronEngine_SOURCES})`
  reason: New `Engine/*.h/.cpp` files should be picked up by existing glob; verify build rather than editing CMake unless the build proves otherwise.
  confidence: high
- file: `tools/ai_native_spikes/results/sam3/export/sam3_export_onnx_point_log.json`
  symbol: ONNX point export proof
  approximate lines: 1-200
  stable anchor: `forward_input_names`, `input_points`, `input_labels`, `providers_used`, `CUDAExecutionProvider`
  reason: Defines Phase 3 point ONNX proof contract: `pixel_values`, `input_points`, `input_labels`, CUDA ORT validated.
  confidence: high
- file: `tools/ai_native_spikes/results/sam3/export/sam3_export_onnx_box_log.json`
  symbol: ONNX box export proof
  approximate lines: 1-200
  stable anchor: `forward_input_names`, `input_boxes`, `providers_used`, `CUDAExecutionProvider`
  reason: Defines Phase 3 box ONNX proof contract: `pixel_values`, `input_boxes`, CUDA ORT validated.
  confidence: high
- file: `Gui/FluxAiPanel.{h,cpp}`, `Gui/Gui05.cpp`, `tools/ai/sam3_transformers_worker.py`, `tools/ai/matanyone2_worker.py`
  symbol: existing AI Paint/SAM3/MatAnyone2 workflows
  approximate lines: read-only patterns only; e.g. `FluxAiPanel::setupUi`, `sam3_transformers_worker.py::normalize_prompt`, `infer_still`, `infer_video`
  stable anchor: protected workflow files named in the task
  reason: Protected from edits; use only as read-only context for what not to regress or couple to.
  confidence: high

## Allowed Edit Files
- `Engine/FluxAIMatte.h`
- `Engine/FluxAIMatte.cpp`
- `Engine/FluxAIMattePrivate.h`
- `Engine/FluxAIMatteOverlay.cpp`
- `Engine/FluxAIMattePreviewController.h` (new)
- `Engine/FluxAIMattePreviewController.cpp` (new)
- `tools/ai/sam3_onnx_preview_worker.py` (new)

## Read-Only Context Files
- `Engine/FluxAIMattePromptContext.h`
- `Engine/FluxAIMattePromptContext.cpp`
- `Engine/EffectInstance.h`
- `Engine/FluxAIMaskCopy.cpp`
- `Engine/CMakeLists.txt`
- `Gui/FluxAiPanel.h`
- `Gui/FluxAiPanel.cpp`
- `Gui/Gui05.cpp`
- `tools/ai/sam3_transformers_worker.py`
- `tools/ai/matanyone2_worker.py`
- `tools/ai_native_spikes/results/sam3/export/sam3_export_report.json`
- `tools/ai_native_spikes/results/sam3/export/sam3_export_onnx_point_log.json`
- `tools/ai_native_spikes/results/sam3/export/sam3_export_onnx_box_log.json`

## Required Change
Implement a single coherent Phase 3 preview slice with the following constraints.

1. Preserve `Flux AI Matte` render identity.
   - `FluxAIMatte::isIdentity` must continue returning input 0 for the requested time/view.
   - `FluxAIMatte::render` must not invoke SAM, ONNX Runtime, Python, model loading, image export, or preview compositing. It remains no-op/pass-through.
   - The preview is overlay/UI state only, never final matte output and never an output plane.

2. Add explicit node-property controls and status.
   - In `FluxAIMatte::initializeKnobs`, add a small Phase 3 property section on the existing `Flux AI Matte` page:
     - `Update SAM3 Preview` button, non-persistent, evaluate-on-change false.
     - `Cancel SAM3 Preview` button, non-persistent, evaluate-on-change false.
     - `SAM3 Preview Status` label, non-persistent.
     - `SAM3 Preview Frame` label or string, non-persistent.
     - `Show SAM3 Preview Overlay` checkable button/boolean, persistent if appropriate; default enabled. If persistence is architecturally awkward, keep non-persistent and document in return summary.
   - Existing prompt toolbar controls and persistence must not change except for status text updates.
   - `knobChanged` starts/cancels preview only for those new controls.

3. Add a node-owned transient preview controller adjacent to `FluxAIMatte`.
   - Add `FluxAIMattePreviewController` in new `Engine/FluxAIMattePreviewController.{h,cpp}`.
   - It must own no global state and must be instantiated/owned from `FluxAIMattePrivate` or lazily by `FluxAIMatte`.
   - It must use Qt/C++ facilities already available to `NatronEngine` (`Qt::Core`, `QProcess`, `QTemporaryDir`/cache paths, atomics/mutexes as needed; `Qt::Concurrent` is allowed only for non-Engine/non-`getImage()` CPU work). Do not add QtGui dependencies unless the build already links them; prefer simple RGB-only PPM / raw RGB source files and 8-bit grayscale PGM / raw mask files with manual parsing over `QImage`.
   - It must coordinate source-frame extraction as main/UI-thread-only Engine access unless an existing Natron render scheduler is located that safely provides the same source frame. Heavy SAM3/ONNX inference must run in the separate Python worker process or other non-Engine off-thread work only after source extraction has completed. Completion may return via queued Qt calls or a thread-safe poll followed by `redrawOverlayInteract()`.
   - It must cancel stale/in-flight jobs when the user clicks Cancel, starts a new preview, edits prompts, or destroys the node. Cancellation must terminate the child process if it is still running and ignore late results by request id/generation.
   - It must surface states: `idle`, `queued/exporting source`, `running SAM3`, `ready`, `cancelled`, `error`. Put concise status/errors in the status label.

4. Source frame extraction contract.
   - On `Update SAM3 Preview`, capture the rounded current frame passed by the knob change/context if available; if `knobChanged` cannot reliably provide current viewer time, use the node/app timeline current frame through existing Engine APIs only after locating a stable accessor. Stop if no stable current-frame source exists.
   - Filter prompts to enabled `FluxAIMatteGuideTarget::SAM` prompts whose `std::lround(prompt.time)` equals the requested preview frame. MatAnyone prompts must be ignored for Phase 3.
   - If no same-frame SAM prompts exist, do not launch inference; set status to an actionable error: `No SAM prompts on frame N`.
   - Extract/render the node input at render scale 1 into a temporary source image for the same frame. Use the safest available Engine image path; read-only examples are `EffectInstance::getImage` and `FluxAIMaskCopy::render` RAM image handling. This extraction must happen outside `FluxAIMatte::render` and on the main/UI thread only, unless the implementer first locates an existing Natron render scheduling API that safely performs source-frame extraction without invoking SAM/ONNX from a render thread. Do not call `EffectInstance::getImage()` or equivalent Engine frame access from a raw background thread or `QtConcurrent` worker. After the source file is written, hand it to the separate worker process for heavy inference.
   - Preferred source interchange: write RGB-only PPM (`P6`, three channels) or raw RGB plus JSON metadata containing width, height, RoD/bounds, frame, view, and coordinate offset. Do not write RGBA PPM; source PPM is always RGB-only. Mask result interchange must be 8-bit grayscale PGM (`P5`) or raw 8-bit alpha/mask bytes plus JSON metadata. Avoid introducing QtGui just to encode/decode PNG.
   - Stop and report if input-frame extraction requires modifying render architecture, touching protected AI Panel/source-export code, calling Engine image APIs from a raw background/QtConcurrent worker, or running SAM/ONNX inference on the render thread.

5. Prompt-to-SAM ONNX input contract.
   - Convert existing overlay/project-space prompt coordinates to source-image pixel coordinates using the exported frame bounds/RoD:
     - point: `x_px = round(prompt.point.x() - bounds.left)`, `y_px = round(prompt.point.y() - bounds.top)`, clamp to `[0,width-1]` / `[0,height-1]` only after detecting/reporting out-of-bounds in debug/status.
     - box: normalize rect, convert to `[x1,y1,x2,y2]` in exported source-pixel space using the same offset; clamp to image bounds; reject boxes with width/height < 1 after conversion.
   - Prompt JSON sent to the worker must include:
     - `id`, `type` (`point` or `box`), `role` (`positive` or `negative`), `frame`, `source_xy` or `source_xyxy`, `label` (`1` for positive, `0` for negative point labels), and original prompt coordinates for diagnostics.
   - Because the verified ONNX artifacts are single-prompt point/box exports, implement Phase 3 combination as preview-only:
     - Run each positive SAM point and SAM box independently, OR/max-composite their alpha masks.
     - Run negative SAM points independently if supported by the point ONNX label input and subtract/max-suppress them from the positive composite.
     - If negative-point ONNX output is unusable or unsupported, show a clear status warning and ignore negatives for preview rather than inventing final matte semantics.
   - Do not add text/current-mask/brush prompt handling in Phase 3.

6. Add a new isolated ONNX preview worker.
   - Create `tools/ai/sam3_onnx_preview_worker.py`; do not edit `tools/ai/sam3_transformers_worker.py`.
   - Worker accepts a single JSON request via stdin or `--request <json>` and emits JSON status/result to stdout. Include `--self-check` for validation.
   - Runtime dependencies are `onnxruntime`/`onnxruntime-gpu`, `numpy`, and simple image IO. Prefer stdlib/manual RGB-only PPM or raw RGB source read and 8-bit grayscale PGM or raw mask write; Pillow is acceptable only if already present in the AI environment. Do not require PyTorch for Phase 3 preview.
   - Model/artifact resolution order:
     1. explicit request paths for point/box ONNX,
     2. environment variables such as `FLUX_SAM3_POINT_ONNX` and `FLUX_SAM3_BOX_ONNX`,
     3. development proof artifact paths under `tools/ai_native_spikes/results/sam3/export/onnx/point/sam3_tracker_point.onnx` and `tools/ai_native_spikes/results/sam3/export/onnx/box/sam3_tracker_box.onnx` if present.
   - If artifacts are missing, return a structured blocker/error; do not download models or invoke the model manager in this phase.
   - Request contract: source image path + metadata, prompt list, output mask path, preferred providers `["CUDAExecutionProvider", "CPUExecutionProvider"]`.
   - Result contract: status, output 8-bit grayscale PGM or raw mask path, width/height, providers available/used, prompt count, per-prompt warnings/errors, elapsed milliseconds.
   - Worker must verify provider use and include CUDA evidence if CUDA is available. If CUDA provider is unavailable but CPU fallback succeeds, status must make that explicit. The known ~1.8GB ONNX session/model load latency is an accepted Phase 3 limitation; persistent worker processes, session caching, preloading, or warm session pools are future optimizations unless they can be implemented safely inside the allowed files without broader lifecycle/product scope.

7. Preview storage/display strategy.
   - Use a volatile node-owned preview cache, not project generated media and not prompt serialization.
   - Preferred storage: temporary/cache directory under Qt temp/cache, e.g. `Flux/sam3_node_preview/<node-or-script-name>/<request-id>/`, containing source RGB-only PPM or raw RGB, request JSON, result JSON, and result 8-bit grayscale PGM or raw alpha.
   - On successful result, load the small 8-bit alpha mask into `FluxAIMattePrivate` as an in-memory struct: `frame`, `width`, `height`, `bounds/RoD`, `std::vector<unsigned char> alpha`, `requestId`, `providersUsed`, `status`.
   - `drawOverlay` must draw this alpha as a semi-transparent colored guide overlay only when:
     - preview overlay is enabled,
     - preview status is ready,
     - `std::lround(time) == preview.frame`, and
     - dimensions/bounds are valid.
   - If the viewer is on any other frame, do not draw the preview and show prompts only. The status may say `Preview ready for frame N`.
   - Reason: volatile in-memory + temp/cache files avoid product decisions about generated media, save/reopen semantics, final matte ownership, and project-relative outputs. Phase 3 is explicitly preview-only.

8. Overlay rendering requirements.
   - Preserve existing point/box prompt drawing and hit testing.
   - Draw the mask before prompt markers so prompts remain visible on top.
   - Use OpenGL state protection consistent with existing `GLProtectAttrib` usage.
   - If efficient texture upload is too large for this slice, a conservative first implementation may draw a downsampled alpha grid/tiles, but it must still visually prove same-frame guide preview and must not freeze the UI. Report any performance limitation.

9. Protected workflows/files.
   - Do not edit: `Engine/AIPaint.*`, `Engine/AIPaintContext.*`, `Gui/FluxAiPanel.*`, `tools/ai/sam3_transformers_worker.py`, `tools/ai/matanyone2_worker.py`.
   - Do not route through existing AI Paint live preview or AI Panel run/history/apply paths.
   - Do not touch MatAnyone2 runtime, video inference, final matte generation, add/replace mask workflows, or Phase 1+2 prompt toolbar/overlay/persistence behavior except using existing SAM prompts as read-only input.

## Non-Goals
- No temporal propagation, tracking, or video mask sequence generation.
- No final matte/alpha output, no generated-media manifest, no Add Mask/Replace Mask, no project result history.
- No inference, ONNX session load, or process launch from `render()` or render threads.
- No model downloads, token handling, model-manager changes, installer changes, or license-flow changes.
- No changes to AI Paint, Flux AI Panel, SAM3 Transformers worker, MatAnyone2 worker, or their existing workflows.
- No text/current-mask/brush prompts.
- No architectural rewrite of Natron rendering or viewer source capture.

## Validation
Commands:
- `python3 -m py_compile tools/ai/sam3_onnx_preview_worker.py`
- `python3 tools/ai/sam3_onnx_preview_worker.py --self-check`
- If proof ONNX artifacts are present and runtime is invoked: `python3 tools/ai/sam3_onnx_preview_worker.py --self-check --require-cuda` or the implemented equivalent that prints available/used ORT providers.
- Build gate, using the local established build directory: `cmake --build "$BUILD_DIR" --target NatronEngine Natron -j$(nproc)`
- Protected-file regression gate: `git diff -- Engine/AIPaint.cpp Engine/AIPaint.h Engine/AIPaintContext.cpp Engine/AIPaintContext.h Gui/FluxAiPanel.cpp Gui/FluxAiPanel.h tools/ai/sam3_transformers_worker.py tools/ai/matanyone2_worker.py`

Expected result:
- Python worker compiles and self-check reports schema, dependency status, model path status, and ORT providers. If CUDA/ORT runtime is used, evidence must include `CUDAExecutionProvider` in available/used providers or a clear fallback/blocker.
- Build succeeds without adding CMake edits unless proven necessary by the build.
- Protected-file diff command is empty.
- GUI/manual proof must include screenshots/recording showing:
  1. `Flux AI Matte` node selected with Phase 3 preview controls/status visible.
  2. Same-frame SAM point/box prompts visible in the viewer.
  3. Clicking `Update SAM3 Preview` changes status to running without freezing UI.
  4. Resulting guide mask overlay appears on the prompt frame.
  5. Scrubbing to another frame hides the guide mask while prompt frame gating remains correct.
  6. Cancel/error path is visible and non-crashing.
  7. Output render remains visually/source pass-through with preview overlay not baked into render.

Regression checklist:
- Existing Phase 1+2 `Flux AI Matte` prompt toolbar still adds/selects/moves/deletes/clears point and box prompts.
- Prompt persistence save/reopen remains unchanged for prompts; preview pixels are not serialized.
- MatAnyone prompt tools still draw existing guide markers but are ignored by SAM3 preview.
- Existing AI Paint/Flux AI Panel live preview/history/run/apply behavior is untouched.
- Existing SAM3 Transformers and MatAnyone2 workers are untouched.
- No new blocking work occurs on UI thread during model inference.
- No SAM work occurs from render threads.

## Stop Conditions
Stop and report if:
- target symbol is missing
- required fix exceeds allowed files
- validation cannot run
- existing architecture contradicts the requested change
- task requires product/design judgment not in packet
- current viewer/timeline frame cannot be obtained safely from `FluxAIMatte` without touching protected GUI workflows
- source-frame extraction from the node input cannot be done on the main/UI thread or via an existing safe Natron render scheduler without raw background Engine calls, render-thread inference, or a broad render architecture change
- ONNX proof artifacts are missing and no explicit/env model paths are available
- ORT worker cannot load point/box ONNX models with a clear provider status
- multiple-prompt combination would require unapproved final-matte semantics beyond preview-only max/subtract compositing
- implementation would require editing `Engine/AIPaint.*`, `Engine/AIPaintContext.*`, `Gui/FluxAiPanel.*`, `tools/ai/sam3_transformers_worker.py`, or `tools/ai/matanyone2_worker.py`

## Planner Self-Check
- locator evidence sufficient: yes — high-confidence current node files are `Engine/FluxAIMatte*`; protected read-only patterns and ONNX proof artifacts were verified.
- allowed edit files minimal and explicit: yes — only `FluxAIMatte` node files, new adjacent preview controller files, and one isolated new ONNX preview worker.
- read-only context minimal: yes — limited to prompt context, Engine image-access examples, build glob, protected workflow files, and SAM3 proof logs.
- anchors/lines included: yes — relevant paths include symbols, approximate lines, stable anchors, reasons, and confidence.
- validation concrete: yes — includes Python compile/self-check, ORT/CUDA provider proof when runtime is invoked, build gate, protected-file diff gate, and GUI proof steps.
- parallelization decision explicit and safe: yes — single task; not parallelized because `Engine/FluxAIMatte.*` files are shared across controls, controller wiring, overlay drawing, and prompt/status behavior.
- non-goals and stop conditions sufficient: yes — scope creep to temporal propagation, final output, protected workflows, downloads, raw background Engine frame extraction, persistent session caching, and render-thread inference is explicitly blocked.
- reviewer findings addressed, if revision: yes — run e099ca23 Major is addressed by requiring main/UI-thread-only source extraction (or an existing safe Natron render scheduler), banning raw background/QtConcurrent `getImage()` access, correcting source/mask formats to RGB-only PPM/raw RGB and 8-bit grayscale PGM/raw mask, and documenting ~1.8GB ONNX load latency as an accepted Phase 3 limitation.

## Required Return Contract
Return only a task-focused summary. Do not include transcript, tool logs, raw file dumps, large code blocks, or broad unrelated issues. Include status, files inspected/changed, validation evidence, blockers, and task-specific risks.
