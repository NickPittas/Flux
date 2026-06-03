# Planner Report

## Status
ready

## Rationale
This plan scopes the approved native AI Matte node work into the smallest safe first implementation: a new node-owned prompt model skeleton plus Natron/Flux viewer-toolbar and overlay prompt editing, without touching the existing AI Paint/SAM3/MatAnyone2 workflows. The runtime/model phases are intentionally deferred to later gates because native spike evidence is strong but still needs an approved integration harness before render-time matte generation.

# Task Packet

## User Goal
Create a nodegraph-usable native AI Matte node with viewer toolbar/overlay prompt tools for SAM and MatAnyone guidance. Phase 1+2 must deliver the node prompt model skeleton and viewer prompt editing only. Later phases will add SAM3 native preview, MatAnyone2 ONNX Runtime/native harnessing, and final matte generation. Existing AI Paint/SAM3/MatAnyone2 workflows must remain untouched.

## Mode
general-coding

## Relevant Locations
- file: `Engine/AIPaint.h`
  symbol: `class AIPaint : public EffectInstance`
  approximate lines: 38-111
  stable anchor: `virtual bool hasOverlay() const OVERRIDE FINAL { return true; }`
  reason: Existing read-only pattern for a native Natron node that owns a toolbar page, prompt context, overlay drawing, and overlay event handlers. Verify before coding; do not modify for this task.
  confidence: high
- file: `Engine/AIPaint.cpp`
  symbol: AI Paint toolbar/prompt/overlay implementation
  approximate lines: 68-82, 203-289, 365-486, 589-720
  stable anchor: `#define kAIPaintParamToolbar "aiPaintToolbar"`, `KnobPagePtr toolbar`, `AIPaint::drawOverlay`
  reason: Read-only reference for how Flux/Natron exposes node-owned viewer toolbar buttons and persistent overlay prompt indicators. Verify before coding; do not route the new node through AI Paint.
  confidence: high
- file: `Engine/AIPaintContext.h`
  symbol: `AIPaintPrompt`, `AIPaintContext`
  approximate lines: 32-83
  stable anchor: `enum class AIPaintPromptType`, `int addPoint(...)`, `int addBox(...)`
  reason: Read-only reference for point/box prompt model, selection state, and serialization shape. Verify before coding; copy/adapt concepts into a new native node model rather than sharing mutable AI Paint state.
  confidence: high
- file: `Engine/AIPaintContext.cpp`
  symbol: `AIPaintContext::appendPrompt`, `addPoint`, `addBox`, `selectPrompt`, `serialize`, `deserialize`
  approximate lines: 84-250
  stable anchor: `copy.selected`, `object.insert(QString::fromUtf8("selected"), prompt.selected);`
  reason: Read-only reference for single-selected prompt semantics and versionable JSON prompt store behavior. Verify before coding.
  confidence: high
- file: `Gui/NodeViewerContext.cpp`
  symbol: `NodeViewerContext::createGui`
  approximate lines: 190-242
  stable anchor: `if (toolbarPage)`, `_imp->toolbar = new QToolBar(_imp->viewer);`, `getIsToolBar()`
  reason: Existing GUI path that turns a node `KnobPage` marked `setAsToolBar(true)` into viewer toolbar buttons; the new node should use this pattern before any bespoke toolbar code.
  confidence: high
- file: `Engine/AppManager.cpp`
  symbol: built-in plugin registration
  approximate lines: 133-136, 1552-1556
  stable anchor: `registerBuiltInPlugin<FluxAIMaskCopy>(QString::fromUtf8(""), false, false);`
  reason: Registration anchor for a new built-in `FluxAIMatte`/`FluxAIMattePrompt` node if the implementation chooses an Engine-native node.
  confidence: high
- file: `Engine/EffectInstance.h`
  symbol: plugin id defines
  approximate lines: 95-100
  stable anchor: `#define PLUGINID_NATRON_AIPAINT`
  reason: Add a new stable plugin id for the native AI Matte node if no existing Flux AI Matte id is found. Verify exact location before coding.
  confidence: medium
- file: `Engine/Engine.pro`
  symbol: Engine source/header lists
  approximate lines: 68-70, 216-218
  stable anchor: `AIPaint.cpp`, `AIPaintContext.cpp`, `AIPaint.h`, `AIPaintContext.h`
  reason: qmake build list still enumerates Engine sources/headers; add new node files here if required by this build path.
  confidence: high
- file: `Engine/CMakeLists.txt`
  symbol: CMake source discovery
  approximate lines: 18-30
  stable anchor: `file(GLOB NatronEngine_HEADERS *.h)`, `file(GLOB NatronEngine_SOURCES *.cpp)`
  reason: CMake currently globs Engine files, so new `.h/.cpp` files should be picked up; verify no generated binding/type-system exclusions are needed.
  confidence: high
- file: `Gui/FluxAiPanel.cpp`
  symbol: existing SAM3/MatAnyone panel and worker integration
  approximate lines: 69-179, 313-345, 636-704, 1077-1110
  stable anchor: `_sam3WorkerProcess`, `_matAnyone2WorkerProcess`, `readAIPaintPrompts`
  reason: Protected existing workflow and later-phase integration reference; Phase 1+2 should not edit it except if a reviewer-approved later packet explicitly adds read-only summaries for the new node.
  confidence: high
- file: `tools/ai_native_spikes/results/sam3/export/sam3_export_report.json`
  symbol: SAM3 ONNX/ORT CUDA spike result
  approximate lines: 1-180
  stable anchor: `overall_status`, `onnx_export`, `ort_inference`, `baseline_parity`
  reason: Later-phase evidence: SAM3 point/box ONNX Runtime CUDA is viable with strong parity; verify exact current report before coding runtime harness.
  confidence: high
- file: `tools/ai_native_spikes/results/matanyone2/export/submodules/matanyone2_submodule_export_report.json`
  symbol: MatAnyone2 submodule export report
  approximate lines: 1-180
  stable anchor: `captured_targets`, `CUDAExecutionProvider`, `torch_parity`
  reason: Later-phase evidence: MatAnyone2 submodules export to ONNX and run through ORT CUDA; verify exact current report before coding runtime harness.
  confidence: high
- file: `tools/ai_native_spikes/results/matanyone2/ort_state_machine/matanyone2_ort_state_machine_report.json`
  symbol: MatAnyone2 ORT state-machine parity proof
  approximate lines: 1-150
  stable anchor: `schema`, `status`, `parity`, `iou`
  reason: Later-phase evidence: fixed-shape Python ORT state machine reproduces PyTorch output; runtime phase must preserve this parity gate.
  confidence: high

## Allowed Edit Files
Phase 1+2 implementation may edit only these explicit files unless a pre-coding locator pass proves a different required registration file:
- `Engine/FluxAIMattePromptContext.h` (new)
- `Engine/FluxAIMattePromptContext.cpp` (new)
- `Engine/FluxAIMatte.h` (new)
- `Engine/FluxAIMatte.cpp` (new)
- `Engine/AppManager.cpp`
- `Engine/EffectInstance.h`
- `Engine/Engine.pro`

## Read-Only Context Files
- `Engine/AIPaint.h`
- `Engine/AIPaint.cpp`
- `Engine/AIPaintContext.h`
- `Engine/AIPaintContext.cpp`
- `Gui/NodeViewerContext.cpp`
- `Gui/FluxAiPanel.cpp`
- `Gui/Gui05.cpp`
- `Gui/HostOverlay.cpp`
- `tasks/T083-ai-matte-depth.md`
- `tasks/T083-full-session-handoff.md`
- `tools/ai_native_spikes/README.md`
- `tools/ai_native_spikes/results/sam3/export/sam3_export_report.json`
- `tools/ai_native_spikes/results/sam3/point/sam3_transformers_real_inference_result.json`
- `tools/ai_native_spikes/results/sam3/box/sam3_transformers_real_inference_result.json`
- `tools/ai_native_spikes/results/matanyone2/export/submodules/matanyone2_submodule_export_report.json`
- `tools/ai_native_spikes/results/matanyone2/ort_state_machine/matanyone2_ort_state_machine_report.json`

## Required Change
Implement Phase 1+2 only.

### Phase 1 — Native node prompt model skeleton
1. Add a new native Engine node, tentatively named `FluxAIMatte`, with a stable plugin id such as `net.sf.openfx.FluxAIMatte` or a Natron/Flux built-in id consistent with `EffectInstance.h` conventions. Verify naming before coding; do not reuse `PLUGINID_NATRON_AIPAINT`.
2. Register the node as a normal nodegraph-usable built-in so it appears in node creation/search and can be connected by power users. It must not auto-attach to layers, masks, RotoPaint, or AI Paint.
3. Add a separate prompt context class, tentatively `FluxAIMattePromptContext`, containing at least:
   - prompt id
   - type: point or box for Phase 1+2; reserve enum values for text/current-mask/brush only as inert future placeholders if needed
   - guide target: `sam`, `matanyone`, or neutral/general guidance
   - role/label: include/positive, exclude/negative, neutral where supported
   - enabled flag
   - selected flag
   - frame/time metadata
   - canonical source/viewer coordinate metadata
   - point coordinate or box rect
   - optional backend tag/metadata map
4. Store prompt state in a hidden/string knob on the new node using versioned JSON. Preserve forward compatibility by ignoring unknown fields and keeping a schema/version field.
5. Provide minimal node knobs for model/task intent but no runtime execution yet:
   - task/mode display or choice: SAM preview, MatAnyone guidance, final matte pending
   - prompt summary/status string
   - hidden prompt store
   - optional read-only warning/status strings for later license/runtime gates
6. Implement identity/pass-through render for Phase 1+2. The node may pass input through unchanged or output transparent/no-op according to existing Natron node conventions, but it must not generate mattes yet and must not invoke Python, ORT, SAM3, MatAnyone2, or existing worker paths.
7. Keep all existing AI Paint prompt store, live preview, SAM3 worker, MatAnyone2 worker, AI result history, custom plane copy, Roto/RotoPaint, and layer-mask graph code unchanged.

### Phase 2 — Viewer toolbar/overlay prompt editing
1. Expose the new node's prompt editing tools through a node-owned toolbar page (`KnobPage::setAsToolBar(true)`) so `NodeViewerContext.cpp` can create toolbar controls using existing Natron/Flux GUI patterns.
2. Toolbar tools must include, at minimum:
   - Select/Move prompt
   - Add SAM positive point
   - Add SAM negative point
   - Add SAM box
   - Add MatAnyone guidance point or first-frame guide point
   - Add MatAnyone guidance box
   - Delete selected prompt
   - Clear prompts
3. Overlay behavior must be first-class and persistent:
   - draw all enabled points and boxes when the node is selected/active in the viewer
   - distinguish SAM vs MatAnyone guides visually without requiring the AI Panel
   - distinguish positive/negative/neutral roles
   - show selected prompt state clearly
   - support click-select, drag-move points, drag-move boxes, and box creation by drag
   - keep coordinates in source/viewer canonical space and avoid viewer zoom/proxy changing stored coordinates
4. Prompt mutation must update the hidden prompt store, redraw overlays, and update prompt summary/status knobs. It must survive save/reopen.
5. Use existing overlay event hooks and toolbar mechanics from AI Paint/EffectInstance as reference, but do not share mutable prompt state with AI Paint and do not edit AI Paint behavior.
6. Expected user-visible behavior after Phase 1+2:
   - user creates `Flux AI Matte` from the node graph
   - user connects a source and opens/selects the node in the viewer
   - viewer toolbar shows AI Matte prompt tools
   - user can add, remove, select, and move SAM and MatAnyone points/boxes directly in the viewer
   - prompt indicators remain visible while editing and persist after project save/reopen
   - render output is explicitly unchanged/no-op until later runtime phases

### Later phases overview — not part of this implementation packet
- Phase 3: SAM3 native preview harness. Use verified spike evidence from `tools/ai_native_spikes/results/sam3/export/sam3_export_report.json` showing point/box ONNX Runtime CUDA viability and parity. Add a preview-only controller/harness after a separate plan identifies final C++/ORT ownership, model path resolution, GPU provider setup, model license gating, and preview-cache lifecycle. Validation must include point and box prompt masks, parity thresholds, CUDA provider proof, and screenshots/recordings.
- Phase 4: MatAnyone2 ORT/native runtime harness. Use verified evidence from submodule export and `matanyone2_ort_state_machine_report.json` showing CUDA ORT submodules and fixed-shape state-machine parity against PyTorch. Add non-commercial warnings before install/run, fixed-shape/resolution constraints, state reset controls, and sequential GPU validation. Validation must include a short real clip, first-frame mask/guidance, parity metrics, alpha sequence proof, VRAM/timing notes, and warning screenshots.
- Phase 5: Final node matte generation. Convert Phase 1+2 prompts plus Phase 3/4 runtimes into generated project-relative raster media or node output according to an approved runtime/render architecture. It must remain nodegraph-usable, survive save/reopen, and not route through the existing AI Paint/Roto/Premult layer-mask path unless Nick explicitly approves. Validation must include nodegraph screenshots, viewport/matte proof on real media, generated media manifest, save/reopen, performance, and no token/plaintext leak audit.

## Non-Goals
- Do not modify existing `Engine/AIPaint.*`, `Engine/AIPaintContext.*`, `Gui/FluxAiPanel.*`, `tools/ai/sam3_transformers_worker.py`, `tools/ai/matanyone2_worker.py`, AI result history, or custom plane copy paths in Phase 1+2.
- Do not replace, hijack, or regress AI Paint/SAM3 live preview or MatAnyone2 panel workflows.
- Do not use native RotoPaint or Flux layer-mask/Roto/Premult as the prompt model.
- Do not implement SAM3 preview, MatAnyone2 inference, ORT C++ runtime, model manager changes, downloads, token handling, or final matte generation in Phase 1+2.
- Do not store generated media, model paths, access tokens, or absolute project-specific runtime paths in the prompt store.
- Do not introduce Python/CUDA/ORT execution inside the Natron render thread in this packet.
- Do not make high-level UX/product decisions beyond the approved toolbar tools for add/remove/select/move points and boxes.

## Validation
Commands:
- `cmake --build /home/npittas/Flux/build --target Natron -j$(nproc)`
- If qmake project files are still validated in this workspace, run the existing Engine/Natron qmake smoke command used by current maintainers after updating `Engine/Engine.pro`; otherwise report why CMake is the active build path.
- Manual GUI validation with screenshots/recording:
  1. Launch Flux/Natron from the built binary.
  2. Create a simple source/read node and create the new `Flux AI Matte` node from node search.
  3. Connect source to `Flux AI Matte` and view/select it.
  4. Capture a screenshot showing the new node in the node graph and its properties.
  5. Capture a screenshot showing the viewer toolbar with Select/Move, SAM point, SAM box, MatAnyone point, MatAnyone box, Delete Selected, and Clear controls.
  6. Record adding multiple SAM points, a SAM box, MatAnyone guidance point/box, selecting prompts, moving a point, moving/resizing or recreating a box, deleting selected, and clearing prompts.
  7. Save/reopen the project and capture proof that remaining prompts persist and overlay at the same source coordinates.
  8. Confirm existing AI Paint node toolbar/live preview and existing MatAnyone2 panel path still open/run as before if local models are installed; if models are not installed, confirm their UI states are unchanged and no new node code touches their files.

Expected result:
Build succeeds. The new node is nodegraph-usable and exposes native viewer toolbar/overlay prompt editing. Prompt state survives save/reopen. The rendered image is unchanged/no-op by design. Existing AI Paint/SAM3/MatAnyone2 workflows are untouched and not regressed. GUI-control validation includes screenshots/recordings; do not mark GUI behavior validated without visual proof of the controls and overlay response.

## Stop Conditions
Stop and report if:
- target symbol is missing
- required fix exceeds allowed files
- validation cannot run
- existing architecture contradicts the requested change
- task requires product/design judgment not in packet
- `NodeViewerContext.cpp` toolbar mechanics cannot expose the required tools from a node-owned toolbar page without broader GUI changes
- overlay coordinates cannot be made stable across viewer zoom/proxy/cache without changing viewer architecture
- implementing Phase 1+2 requires editing existing AI Paint/SAM3/MatAnyone2 workflow files
- the node cannot be registered/searchable without editing additional registration/build files not listed in Allowed Edit Files
- render no-op/pass-through behavior conflicts with Natron `EffectInstance` requirements and requires a product decision on output semantics
- MatAnyone2 non-commercial warning requirements would be bypassed or hidden by any prompt/runtime UI
- any work would store plaintext tokens, absolute generated-media paths, or model credentials

## Planner Self-Check
- locator evidence sufficient: yes + high-confidence anchors were verified for existing node toolbar/overlay patterns, built-in registration, prompt context serialization, active T083 source of truth, and native spike evidence; new file names are explicit Phase 1+2 implementation targets and must be verified before coding.
- allowed edit files minimal and explicit: yes + Phase 1+2 edits are limited to new node/context files plus built-in registration/plugin-id/qmake list; existing AI workflows are excluded.
- read-only context minimal: yes + context is restricted to AI Paint pattern files, toolbar/overlay references, T083 source-of-truth docs, and native spike reports needed for later-phase overview.
- anchors/lines included: yes + each relevant location has path, symbol/anchor, approximate lines, reason, and confidence.
- validation concrete: yes + build command and manual GUI screenshot/recording gates are specified, including save/reopen and protected-workflow checks.
- parallelization decision explicit and safe: yes + single task for Phase 1+2 because new node/context/registration files must be edited coherently; later runtime phases are sequential after this UI/model foundation.
- non-goals and stop conditions sufficient: yes + they explicitly protect AI Paint/SAM3/MatAnyone2, Roto/RotoPaint, token/model handling, render-thread runtime, and scope expansion.
- reviewer findings addressed, if revision: not applicable + no prior reviewer findings were supplied for this plan.

## Required Return Contract
Return only a task-focused summary. Do not include transcript, tool logs, raw file dumps, large code blocks, or broad unrelated issues. Include status, files inspected/changed, validation evidence, blockers, and task-specific risks.