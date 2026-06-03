# Planner Report

## Status
parallel-ready

## Why Split / Parallelize
T083 AI Paint live SAM3 workflow naturally decomposes into six phased implementation packets. Phase A is the required prompt-management foundation; Phase B can be built in parallel because it adds the persistent Python worker/protocol surface without touching prompt UI; Phase C depends on B's controller concepts and A's prompt store events; Phase D depends on A+B+C; Phase E depends on B and can follow D's result contract; Phase F is intentionally a later apply/mask integration phase that should not block live preview. Recommended transport: JSON-lines over a long-lived `QProcess` using the existing provider-runtime Python lookup. This keeps PyTorch/CUDA out of the Natron render/UI process, reuses VRAM across live/run requests, is restartable/debuggable, and avoids first-implementation risk from embedding Python/CUDA in an in-render-node path.

## Interference Check
- parallel safe: partial
- shared files or generated outputs: `Gui/FluxAiPanel.{h,cpp}` is shared by B/C/D/E; `Engine/AIPaint.{h,cpp}` is shared by A/C; generated outputs under `FluxGenerated/AI/...` are shared by D/E/F contracts.
- shared validation state: provider runtime/model install status, SAM3 VRAM residency, AI Work Viewer preview state, saved-project generated media directory.
- worktree isolation required: yes for parallel workers touching shared C++ files; no for sequential execution in one worktree.
- rationale: A and B can run in parallel if isolated because their allowed edit files are disjoint except integration naming conventions. C/D/E/F should be sequential after their dependencies to avoid conflicting state-machine and result-manifest changes.

## Proposed Task Sequence Or Parallel Batch
1. Task name: A — AI Paint prompt selection/delete/clear
   - purpose: make prompts selectable, deletable, clearable, visually selected, and persisted in AI Paint's prompt store.
   - allowed files: `Engine/AIPaintContext.h`, `Engine/AIPaintContext.cpp`, `Engine/AIPaint.h`, `Engine/AIPaint.cpp`
   - validation: build; add/select/delete/clear point and box prompts; save/reopen selected prompt persists.
   - can run in parallel with: B
2. Task name: B — Persistent SAM3 worker protocol and Python service
   - purpose: replace one-shot SAM3 probe launches with a reusable JSON-lines worker process capable of load/unload/infer/status.
   - allowed files: `Gui/FluxAiPanel.h`, `Gui/FluxAiPanel.cpp`, `tools/ai/sam3_transformers_worker.py`, `tools/ai/sam3_transformers_real_inference_probe.py`
   - validation: Python compile/self-check; worker load/infer/unload via provider runtime; VRAM/process remains resident between requests.
   - can run in parallel with: A only in worktree isolation
3. Task name: C — AI Paint Load/Unload/Live Preview controls + GUI state
   - purpose: AI Paint properties own SAM3 load/unload/live-preview/status controls while AI Panel may mirror/use state.
   - allowed files: `Engine/AIPaint.h`, `Engine/AIPaint.cpp`, `Gui/FluxAiPanel.h`, `Gui/FluxAiPanel.cpp`, `Gui/Gui05.cpp`
   - validation: build; controls appear on AI Paint properties; status changes; AI Panel does not own prompt capture.
   - can run in parallel with: none
4. Task name: D — Debounced live current-frame inference and AI Work Viewer preview
   - purpose: live-preview every current-frame prompt update through persistent SAM3 using all enabled prompts and preview only in Flux AI Work Viewer.
   - allowed files: `Gui/FluxAiPanel.h`, `Gui/FluxAiPanel.cpp`, `Gui/Gui.h`, `Gui/Gui05.cpp`, `tools/ai/sam3_transformers_worker.py`
   - validation: build; real video; latency evidence; multi-prompt live matte preview; main comp viewer unchanged.
   - can run in parallel with: none
5. Task name: E — AI Panel Run reuses worker + managed result history
   - purpose: make AI Panel Run/Generate reuse loaded SAM3 worker/model and record managed project-relative history/apply candidates.
   - allowed files: `Gui/FluxAiPanel.h`, `Gui/FluxAiPanel.cpp`, `Gui/ProjectGuiSerialization.h`, `Gui/Gui05.cpp`
   - validation: build; Run reuses loaded worker; history survives save/reopen; preview selected history item.
   - can run in parallel with: none
6. Task name: F — Apply workflow to layer/effect mask interfaces
   - purpose: implement or finalize the interface for applying managed AI results to layer/effect masks without auto-applying to comp.
   - allowed files: `Gui/FluxAiPanel.h`, `Gui/FluxAiPanel.cpp`, `Gui/FluxTimeline.h`, `Gui/FluxTimeline.cpp`, `Gui/FluxTimelineSerialization.h`, `Gui/Gui05.cpp`
   - validation: build; apply selected result to layer/effect mask; save/reopen; graph rebuild preserves mask.
   - can run in parallel with: none

## Task Packets

# Task Packet A — AI Paint prompt selection/delete/clear

## User Goal
Implement prompt management required by the approved AI Paint live SAM3 workflow: select prompts/boxes, delete selected prompt, clear all prompts, show selected prompt visually, and persist selected prompt state.

## Mode
general-coding

## Relevant Locations
- file: `Engine/AIPaintContext.h`
  symbol: `AIPaintPrompt`, `AIPaintContext`
  approximate lines: 52-90
  stable anchor: `bool selected;` and `int addPoint(...)`, `int addBox(...)`, `void clear()`
  reason: prompt data already has persisted selected flag but lacks select/delete APIs.
  confidence: high
- file: `Engine/AIPaintContext.cpp`
  symbol: `AIPaintContext::serialize`, `AIPaintContext::deserialize`, `addPoint`, `addBox`, `clear`
  approximate lines: 88-250
  stable anchor: `object.insert(QString::fromUtf8("selected"), prompt.selected);`
  reason: add select/delete methods and keep serialization compatibility.
  confidence: high
- file: `Engine/AIPaint.cpp`
  symbol: `AIPaintPrivate`, `initializeKnobs`, `drawOverlay`, `onOverlayPenDown`
  approximate lines: 41-520
  stable anchor: `kAIPaintParamSelectTool`, `kAIPaintParamPromptStore`, `if (tool == AIPaintTool::Select)`
  reason: AI Paint toolbar and overlay own prompt capture/visuals; select currently does no hit testing/removal.
  confidence: high

## Allowed Edit Files
- `Engine/AIPaintContext.h`
- `Engine/AIPaintContext.cpp`
- `Engine/AIPaint.h`
- `Engine/AIPaint.cpp`

## Read-Only Context Files
- `Gui/FluxAiPanel.cpp`
- `Gui/Gui05.cpp`
- `tasks/T083-ai-matte-depth.md`

## Required Change
Add minimal `AIPaintContext` APIs to select one prompt by id, clear selection, delete selected prompt, delete by id, and query selected id if useful. Update AI Paint overlay select mode to hit-test existing enabled point/box prompts in canonical viewer coordinates; selecting must make exactly one prompt selected, persist via `aiPaintPromptStore`, redraw overlay, and emit the existing prompt-store change path. Add toolbar/property buttons for Delete Selected and Clear All in AI Paint's own controls, not the AI Panel. Update overlay drawing so selected points/boxes have a distinct visual indicator (e.g. thicker/white outline/handle) while preserving include/exclude/neutral colors. New prompts may either auto-select or leave previous selection untouched, but the behavior must be explicit and consistent; prefer auto-select newest prompt because AI Panel already chooses selected over newest. Do not move prompt add/select/delete/clear controls into `FluxAiPanel`.

## Non-Goals
- No SAM3 worker changes.
- No AI Panel prompt-capture UI.
- No RotoPaint hijack or conversion to roto splines.
- No main comp viewer rewiring.
- No rendered RGBA output from AI Paint overlays.

## Validation
Commands:
- `cmake --build /home/npittas/Flux/build --target Natron -j$(nproc)`
- Manual GUI proof: apply AI Paint to footage, add multiple points and boxes in Flux AI Work Viewer, select each, delete selected, clear all, save/reopen project and verify selected prompt persists.

Expected result:
Build succeeds; selected prompt is visually indicated; delete/clear update overlays and AI Panel prompt summary through existing prompt-store `valueChanged` refresh.

## Stop Conditions
Stop and report if:
- target symbol is missing
- required fix exceeds allowed files
- validation cannot run
- existing architecture contradicts the requested change
- task requires product/design judgment not in packet
- prompt hit-testing cannot be made reliable in canonical overlay coordinates without broader viewer API changes

## Required Return Contract
Return only a task-focused summary. Include files changed, prompt-management behavior, validation evidence, blockers, and task-specific risks.

# Task Packet B — Persistent SAM3 worker protocol and Python service

## User Goal
Create the reusable SAM3 worker/model path so AI Paint live preview and AI Panel Run can share one loaded SAM3 model in VRAM instead of unloading/reloading per request.

## Mode
general-coding

## Relevant Locations
- file: `Gui/FluxAiPanel.h`
  symbol: `_sam3Process`, SAM3 slots, `startSam3StillMask(...)`
  approximate lines: 31-125
  stable anchor: `QProcess* _sam3Process;`
  reason: existing one-shot process state must evolve into persistent worker controller state.
  confidence: high
- file: `Gui/FluxAiPanel.cpp`
  symbol: `startSam3StillMask`, `onSam3ReadyReadStandardOutput`, `onSam3Finished`, `cancelSam3`
  approximate lines: 52-80, 862-1170
  stable anchor: `sam3_transformers_real_inference_probe.py` and `_sam3Process->start(providerPython, args)`
  reason: current run path launches one process per inference and parses whole stdout at finish.
  confidence: high
- file: `tools/ai/sam3_transformers_real_inference_probe.py`
  symbol: `load_flux_model`, `transformers_backend`, inference helpers, CLI result writer
  approximate lines: 1-440
  stable anchor: `RESULT_NAME = "sam3_transformers_real_inference_result.json"`
  reason: reusable code source for worker load/infer implementation.
  confidence: high
- file: `tools/ai/flux_provider_runtime.py`
  symbol: `cmd_python`, `cmd_status`, `cmd_self`
  approximate lines: 1-180
  stable anchor: `def cmd_python(args):`
  reason: worker must use the same provider-runtime Python lookup and status policy.
  confidence: high

## Allowed Edit Files
- `Gui/FluxAiPanel.h`
- `Gui/FluxAiPanel.cpp`
- `tools/ai/sam3_transformers_worker.py`
- `tools/ai/sam3_transformers_real_inference_probe.py`

## Read-Only Context Files
- `tools/ai/flux_provider_runtime.py`
- `tasks/T083-ai-matte-depth.md`

## Required Change
Add `tools/ai/sam3_transformers_worker.py` as a long-lived JSON-lines worker. Protocol must be line-delimited JSON request/response with request ids and at least: `status`, `load`, `infer_still`, `unload`, `shutdown`, and `cancel` if feasible. `load` initializes the SAM3 backend once and reports device/model/runtime metadata. `infer_still` accepts source image path, output directory, source dimensions, and an array of prompts (points and boxes with labels/roles) and writes mask PNG/result JSON with nonzero proof. `unload` releases model references and calls CUDA cache cleanup when available. Refactor shared inference helpers from the probe only as needed; keep the standalone probe working. In `FluxAiPanel`, add persistent process start/line parser/request tracking sufficient for later phases, but avoid wiring live preview controls in this packet. Preserve current one-shot Run behavior if full integration is not finished; do not regress existing SAM3 Run.

Transport decision: use JSON-lines persistent `QProcess` first. Do not embed Python/CUDA inside a render node or Natron render thread in this implementation.

## Non-Goals
- No AI Paint UI controls.
- No live preview scheduling.
- No result history/apply workflow.
- No render-thread SAM3 node.
- No main comp viewer rewiring.

## Validation
Commands:
- `python3 -m py_compile tools/ai/sam3_transformers_worker.py tools/ai/sam3_transformers_real_inference_probe.py`
- `python3 tools/ai/flux_provider_runtime.py status sam3 --json`
- Provider-runtime manual worker smoke: launch provider Python with `tools/ai/sam3_transformers_worker.py`, send `status`, `load`, `infer_still` against a real exported frame, then `unload` and `shutdown`.
- `cmake --build /home/npittas/Flux/build --target Natron -j$(nproc)`

Expected result:
Worker emits valid JSON-lines, keeps the process/model alive between `load` and multiple `infer_still` calls, writes non-empty mask/result artifacts, unloads on request, and existing AI Panel Run remains functional or explicitly uses the new worker without reload.

## Stop Conditions
Stop and report if:
- target symbol is missing
- required fix exceeds allowed files
- validation cannot run
- existing architecture contradicts the requested change
- task requires product/design judgment not in packet
- SAM3 Transformers API cannot support multi-prompt inference with available local model/runtime
- persistent worker cannot safely parse stdout because imported libraries emit non-JSON stdout that cannot be redirected/controlled

## Required Return Contract
Return only a task-focused summary. Include protocol fields implemented, files changed, worker validation, VRAM/process residency evidence, blockers, and risks.

# Task Packet C — AI Paint Load/Unload/Live Preview controls + GUI controller/state

## User Goal
AI Paint properties must own Load SAM3, Unload SAM3, Live Preview, and status controls. SAM3 is initially unloaded. AI Panel may show/use state but must not own prompt capture.

## Mode
general-coding

## Relevant Locations
- file: `Engine/AIPaint.cpp`
  symbol: `initializeKnobs`, `knobChanged`, `kAIPaintParamPage`
  approximate lines: 41-340
  stable anchor: `KnobPagePtr page = AppManager::createKnob<KnobPage>(this, tr("AI Paint"));`
  reason: AI Paint properties are the approved owner of SAM3 controls and status.
  confidence: high
- file: `Gui/FluxAiPanel.h`
  symbol: SAM3 process/controller fields and slots
  approximate lines: 31-125
  stable anchor: `_sam3Process`
  reason: GUI-level controller can own process mechanics while AI Paint owns user controls.
  confidence: high
- file: `Gui/FluxAiPanel.cpp`
  symbol: `updateUiState`, SAM3 lifecycle code
  approximate lines: 377-390, 862-1170
  stable anchor: `const bool sam3Running = _sam3Process && _sam3Process->state() != QProcess::NotRunning;`
  reason: expose/shared state to AI Paint controls and AI Panel summary.
  confidence: high
- file: `Gui/Gui05.cpp`
  symbol: `connectAIPaintPromptStoreRefresh`, source capture context setup
  approximate lines: 153-188, 1333-1489
  stable anchor: `aiPanel->setSourceCaptureContext(...)`
  reason: existing AI Paint node selection/prompt-store hooks are the connection point for control changes.
  confidence: high

## Allowed Edit Files
- `Engine/AIPaint.h`
- `Engine/AIPaint.cpp`
- `Gui/FluxAiPanel.h`
- `Gui/FluxAiPanel.cpp`
- `Gui/Gui05.cpp`

## Read-Only Context Files
- `Engine/AIPaintContext.h`
- `Engine/AIPaintContext.cpp`
- `tools/ai/sam3_transformers_worker.py`
- `tasks/T083-ai-matte-depth.md`

## Required Change
Add AI Paint property controls: `Load SAM3`, `Unload SAM3`, `Live Preview` (checkable), and a status label/string. SAM3 must start unloaded and Live Preview must not silently load the model unless Load SAM3 was requested or the approved packet explicitly reports clear blocked/fallback behavior. Connect AI Paint control changes to the GUI SAM3 controller for the currently selected AI Paint node. AI Panel may mirror loaded/loading/error/live-preview status and continue to own Run/Apply/log/history, but must not add point/box prompt capture controls. Define status states at minimum: unloaded, loading, loaded, live preview enabled, inferring, error, unloading. Keep controls per AI Paint effect/node so selected prompt state and live-preview preference survive project save if persistent knobs are used; if persistence is intentionally limited for loaded/unloaded runtime state, document it in code comments and return summary.

## Non-Goals
- No live inference scheduling beyond state hooks.
- No AI Panel prompt ownership.
- No automatic comp apply.
- No render-thread SAM3 node.
- No main comp viewer rewiring.

## Validation
Commands:
- `cmake --build /home/npittas/Flux/build --target Natron -j$(nproc)`
- Manual GUI proof: AI Paint properties show Load/Unload/Live Preview/status; initial status unloaded; Load starts persistent worker and status loaded; Unload releases worker; AI Panel reflects state without owning prompt capture.

Expected result:
AI Paint controls are visible and functional on the AI Paint properties page; model load state is clear; no point/box capture UI appears in the AI Panel.

## Stop Conditions
Stop and report if:
- target symbol is missing
- required fix exceeds allowed files
- validation cannot run
- existing architecture contradicts the requested change
- task requires product/design judgment not in packet
- Natron knob callbacks cannot safely call GUI-owned process controller without a queued/main-thread bridge

## Required Return Contract
Return only a task-focused summary. Include controls added, state behavior, validation evidence, blockers, and risks.

# Task Packet D — Debounced live current-frame inference and AI Work Viewer preview

## User Goal
When SAM3 is loaded and Live Preview is enabled, adding/selecting/editing/deleting prompts in the AI Work Viewer updates the current-frame matte live without pressing Run. Live preview is mandatory for the workflow and must use Flux AI Work Viewer, not the main comp viewer.

## Mode
general-coding

## Relevant Locations
- file: `Gui/Gui05.cpp`
  symbol: `connectAIPaintPromptStoreRefresh`, `previewFluxAiResultPngInWorkViewer`, source capture setup
  approximate lines: 153-188, 867-925, 1333-1489
  stable anchor: `aiPanel->refreshAIPaintPromptState();` and `Gui::previewFluxAiResultPngInWorkViewer(...)`
  reason: prompt changes already notify AI Panel; preview helper currently creates a freefloating Read preview in AI Work Viewer.
  confidence: high
- file: `Gui/FluxAiPanel.cpp`
  symbol: `refreshAIPaintPromptState`, `readAIPaintPrompts`, `buildAIPaintPromptForSam`, `refreshSourceFrameMetadata`, SAM3 process slots
  approximate lines: 360-540, 760-1170
  stable anchor: `chooseAIPaintPrompt(prompts, &chosen, &message)`
  reason: current path chooses one prompt; live preview must use all enabled prompts and debounce prompt/frame changes.
  confidence: high
- file: `Gui/Gui.h`
  symbol: `previewFluxAiResultPngInWorkViewer`
  approximate lines: locator-provided declaration
  stable anchor: `previewFluxAiResultPngInWorkViewer`
  reason: may need an update/reuse mode so live preview does not accumulate unmanaged Read nodes.
  confidence: medium
- file: `tools/ai/sam3_transformers_worker.py`
  symbol: `infer_still`
  approximate lines: new file from Packet B
  stable anchor: JSON-lines `infer_still` request
  reason: live preview calls the persistent worker with all enabled prompts.
  confidence: medium

## Allowed Edit Files
- `Gui/FluxAiPanel.h`
- `Gui/FluxAiPanel.cpp`
- `Gui/Gui.h`
- `Gui/Gui05.cpp`
- `tools/ai/sam3_transformers_worker.py`

## Read-Only Context Files
- `Engine/AIPaint.h`
- `Engine/AIPaint.cpp`
- `Engine/AIPaintContext.h`
- `Engine/AIPaintContext.cpp`
- `tools/ai/sam3_transformers_real_inference_probe.py`

## Required Change
Implement debounced live preview scheduling from AI Paint prompt-store changes and relevant current-frame/source changes. Preconditions: selected source context valid, SAM3 worker loaded, Live Preview enabled, at least one enabled point/box prompt. Build a multi-prompt request from all enabled prompts, not only the selected/newest prompt; selected prompt remains visually indicated and included. Use project-relative scratch/output directory under `FluxGenerated/AI/live/...` or an explicitly documented live-preview cache. Coalesce rapid prompt changes with a short debounce; cancel/ignore stale in-flight results using request ids/generation counters. Preview resulting current-frame mask in Flux AI Work Viewer only. Avoid creating unbounded freefloating Read nodes; either reuse a managed live-preview Read node or cleanly replace prior preview. Main comp viewer must remain untouched.

## Non-Goals
- No AI Panel prompt capture.
- No auto-apply to layer/effect mask.
- No video tracking/propagation beyond current-frame inference.
- No render-thread SAM3 node.
- No main viewer rewiring.

## Validation
Commands:
- `cmake --build /home/npittas/Flux/build --target Natron -j$(nproc)`
- Manual GUI proof with real video: load SAM3, enable Live Preview, add point, add box, select/delete prompt, clear prompts; record AI Work Viewer mask updates without pressing Run.
- Measure/report live prompt latency for at least three edits and show stale request suppression if edits are rapid.
- VRAM/process proof: show worker process/model remains loaded during multiple live previews and unload releases it.

Expected result:
Live preview updates current-frame mask using all enabled prompts whenever prompts change; AI Work Viewer displays source/live/result inspection; main comp viewer remains unchanged.

## Stop Conditions
Stop and report if:
- target symbol is missing
- required fix exceeds allowed files
- validation cannot run
- existing architecture contradicts the requested change
- task requires product/design judgment not in packet
- source-frame export cannot produce the current timeline/source frame reliably
- live preview creates unmanaged Read-node buildup or touches the main comp viewer
- SAM3 runtime does not support combined point/box prompt semantics and needs product decision for fallback

## Required Return Contract
Return only a task-focused summary. Include live-preview trigger behavior, debounce/stale-result handling, multi-prompt evidence, AI Work Viewer proof, validation, blockers, and risks.

# Task Packet E — AI Panel Run reuses loaded worker and creates managed project-relative result history

## User Goal
AI Panel Run/Generate must reuse the already-loaded SAM3 worker/model in VRAM if available and create managed outputs/history/apply candidates. Result must not just be a freefloating Read node; user must be able to preview again and apply to a layer/effect mask later.

## Mode
general-coding

## Relevant Locations
- file: `Gui/FluxAiPanel.cpp`
  symbol: `onRunClicked`, `startSam3StillMask`, `verifySam3ResultAndWriteManifest`, `onSam3Finished`, `onApplyClicked`
  approximate lines: 773-1170
  stable anchor: `_lastResultManifestProjectRelative` and `result_manifest.json`
  reason: current Run writes one manifest and previews result; Apply is placeholder.
  confidence: high
- file: `Gui/FluxAiPanel.h`
  symbol: serialization fields and SAM3 state fields
  approximate lines: 31-125
  stable anchor: `_lastResultManifestProjectRelative`
  reason: add result-history model/state and worker reuse API.
  confidence: high
- file: `Gui/ProjectGuiSerialization.h`
  symbol: `FluxAiPanelSerialization`
  approximate lines: 647-690
  stable anchor: `lastResultManifestProjectRelative`
  reason: current panel serialization stores only the last result; history requires schema extension.
  confidence: medium
- file: `Gui/Gui05.cpp`
  symbol: `previewFluxAiResultPngInWorkViewer`
  approximate lines: 867-925
  stable anchor: `Flux SAM3 Result Preview`
  reason: history preview should use managed AI Work Viewer preview, not unmanaged node buildup.
  confidence: high

## Allowed Edit Files
- `Gui/FluxAiPanel.h`
- `Gui/FluxAiPanel.cpp`
- `Gui/ProjectGuiSerialization.h`
- `Gui/Gui05.cpp`

## Read-Only Context Files
- `Engine/AIPaintContext.h`
- `Engine/AIPaintContext.cpp`
- `tools/ai/sam3_transformers_worker.py`
- `tasks/T083-ai-matte-depth.md`

## Required Change
Update AI Panel Run/Generate so it uses the persistent SAM3 worker if loaded; if not loaded, either load/reuse the same worker with clear status or block with explicit instruction according to the state behavior established in Packet C. Run should support all enabled prompts (same prompt contract as live preview) and write a managed project-relative result manifest under `FluxGenerated/AI/...`. Add a minimal result history UI/model to the AI Panel: list run id/time/source frame/model/prompt count/selected mask path, preview selected result in AI Work Viewer, and keep `_lastResultManifestProjectRelative` as compatibility pointer. Extend project GUI serialization to persist result history project-relative manifest paths. Ensure result preview does not leave only a freefloating Read node as the user's sole handle; history entry is the managed handle for preview/apply.

## Non-Goals
- No prompt capture in AI Panel.
- No automatic apply to comp.
- No layer/effect mask graph mutation yet except exposing apply candidates.
- No render-thread SAM3 node.
- No main viewer rewiring.

## Validation
Commands:
- `cmake --build /home/npittas/Flux/build --target Natron -j$(nproc)`
- Manual proof with real video: load SAM3 once, run AI Panel generation twice, verify no model reload between runs, history has both results, preview older/newer result, save/reopen preserves history.
- Inspect generated `result_manifest.json` files for project-relative paths, prompt array, model/runtime metadata, source metadata, and nonzero proof.

Expected result:
AI Panel Run reuses loaded SAM3 worker/model, managed history entries are available after generation and reopen, and previews are repeatable from history.

## Stop Conditions
Stop and report if:
- target symbol is missing
- required fix exceeds allowed files
- validation cannot run
- existing architecture contradicts the requested change
- task requires product/design judgment not in packet
- serialization versioning requires broader project-file migration beyond allowed files
- generated paths cannot be kept project-relative

## Required Return Contract
Return only a task-focused summary. Include worker reuse evidence, history/serialization behavior, validation, blockers, and risks.

# Task Packet F — Apply workflow to layer/effect mask interfaces

## User Goal
Define and implement the apply workflow so a managed AI result can be applied to a layer/effect mask later, rather than being only a preview/read node. This can be a later phase but must have explicit interfaces.

## Mode
general-coding

## Relevant Locations
- file: `Gui/FluxTimeline.h`
  symbol: `FluxMask`, `FluxLayer::masks`, mask apply fields
  approximate lines: P6 mask model area per task context
  stable anchor: `FluxMask`
  reason: existing P6 mask model is the intended layer/effect mask integration surface.
  confidence: medium
- file: `Gui/FluxTimelineSerialization.h`
  symbol: Flux mask serialization
  approximate lines: P6 serialization area per task context
  stable anchor: `masks`
  reason: applied AI mask must survive save/reopen and graph rebuild.
  confidence: medium
- file: `Gui/Gui05.cpp`
  symbol: Flux graph rebuild and preview helpers
  approximate lines: timeline graph/mask rebuild areas, `previewFluxAiResultPngInWorkViewer`
  stable anchor: `rebuildCompositingGraph` and `previewFluxAiResultPngInWorkViewer`
  reason: apply must connect generated raster mask into managed layer/effect mask graph without breaking P6 semantics.
  confidence: medium
- file: `Gui/FluxAiPanel.cpp`
  symbol: `onApplyClicked`, result history state
  approximate lines: 773-780 and Packet E history area
  stable anchor: `void FluxAiPanel::onApplyClicked()`
  reason: Apply button currently placeholder; should apply selected managed history result.
  confidence: high

## Allowed Edit Files
- `Gui/FluxAiPanel.h`
- `Gui/FluxAiPanel.cpp`
- `Gui/FluxTimeline.h`
- `Gui/FluxTimeline.cpp`
- `Gui/FluxTimelineSerialization.h`
- `Gui/Gui05.cpp`

## Read-Only Context Files
- `Gui/ProjectGuiSerialization.h`
- `Engine/AIPaintContext.h`
- `Engine/AIPaintContext.cpp`
- `tasks/T083-ai-matte-depth.md`

## Required Change
Implement the narrow apply interface for selected AI history result to selected layer/effect mask. If full graph application is too large, implement a documented interface boundary in these files only: selected result manifest -> project-relative mask media -> `FluxMask` entry with provenance/result manifest path -> graph rebuild hook consumes `FluxMask` as mask input. User action must be explicit via Apply; no auto-apply from live preview or Run. The applied mask must remain managed by Flux timeline/mask model, survive save/reopen, and be previewable again from history. If effect-mask application differs from layer-mask application, implement layer mask first and expose effect-mask as blocked/deferred with required exact file/symbol follow-up.

## Non-Goals
- No auto-apply to comp.
- No RotoPaint/spline conversion.
- No main comp viewer rewiring.
- No render-thread SAM3 node.
- No broad mask system refactor.

## Validation
Commands:
- `cmake --build /home/npittas/Flux/build --target Natron -j$(nproc)`
- Manual proof: generate SAM3 result, select history entry, Apply to selected layer/effect mask, verify viewport/graph reflects mask, save/reopen preserves applied mask and history.
- Negative proof: live preview and Run alone do not modify comp until Apply is clicked.

Expected result:
Managed SAM3 result can be explicitly applied to a Flux layer/effect mask and survives project reload; preview/history remains available.

## Stop Conditions
Stop and report if:
- target symbol is missing
- required fix exceeds allowed files
- validation cannot run
- existing architecture contradicts the requested change
- task requires product/design judgment not in packet
- graph rebuild would break existing P6 mask semantics, duplicate/split behavior, or save/reopen
- effect-mask semantics are ambiguous and require Nick's decision

## Required Return Contract
Return only a task-focused summary. Include apply interface implemented, files changed, save/reopen proof, validation, blockers, and risks.

## Planner Self-Check
- locator evidence sufficient: yes — high/medium evidence covers prompt context, AI Paint overlay/toolbar, one-shot SAM3 path, AI Work Viewer preview, and result manifest/history gaps.
- allowed edit files minimal and explicit: yes — each packet lists explicit files; shared files are called out in the interference check.
- read-only context minimal: yes — limited to authorized files needed for each phase.
- anchors/lines included: yes — relevant locations cite path, symbol, approximate lines, stable anchor, reason, confidence.
- validation concrete: yes — build plus GUI, real-video, VRAM, latency, multi-prompt, history, and apply proof per phase.
- parallelization decision explicit and safe: yes — A/B parallel only with isolation; C/D/E/F sequential due shared state and dependencies.
- non-goals and stop conditions sufficient: yes — explicitly block RotoPaint hijack, main viewer rewiring, render-thread SAM3 node, auto-apply, broader refactors, missing anchors, validation failure, architecture contradiction, and product/design ambiguity.
- reviewer findings addressed, if revision: not applicable — no previous reviewer findings supplied.
