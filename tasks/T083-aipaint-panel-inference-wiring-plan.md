# Planner Report

## Status
ready

## Rationale
The locator evidence and authorized context identify a narrow, coherent implementation unit: expose native AI Paint prompt data to the AI Panel, select/validate a source frame, and launch the existing SAM3 still-mask path without changing prompt ownership or RotoPaint. The plan limits edits to the native AI Paint/panel/viewer wiring surfaces already evidenced, and requires stopping if still-frame export/source metadata cannot be made real within those files.

# Task Packet

## User Goal
Wire native AI Paint viewer-owned point/box prompts into the Flux AI Panel so the panel can summarize prompts and run inference from the selected AI Paint/source.

## Mode
general-coding

## Relevant Locations
- file: `Engine/AIPaint.h`
  symbol: `class AIPaint`
  approximate lines: 35-95
  stable anchor: `std::unique_ptr<AIPaintPrivate> _imp;`
  reason: Native AI Paint currently owns prompt capture privately; add a minimal read-only accessor/API if needed so the panel can consume persistent prompts without moving controls.
  confidence: high
- file: `Engine/AIPaint.cpp`
  symbol: `AIPaint::initializeKnobs`, `AIPaint::onKnobsLoaded`, `AIPaint::knobChanged`, overlay pen handlers
  approximate lines: 202-210, 265-297, 400-454
  stable anchor: `kAIPaintParamPromptStore`, `_imp->context.addPoint`, `_imp->context.addBox`
  reason: Confirms AI Paint persists prompts in `aiPaintPromptStore` and updates `AIPaintContext`; use this store/context as source of truth.
  confidence: high
- file: `Engine/AIPaintContext.h`
  symbol: `AIPaintContext`, `AIPaintPrompt`
  approximate lines: 31-68
  stable anchor: `std::vector<AIPaintPrompt> prompts() const;`
  reason: Defines supported prompt fields/types and already exposes a read-only prompt vector.
  confidence: high
- file: `Engine/AIPaintContext.cpp`
  symbol: `AIPaintContext::serialize`, `AIPaintContext::deserialize`
  approximate lines: 143-240
  stable anchor: `root.insert(QString::fromUtf8("prompts"), promptArray);`
  reason: Defines persistent JSON contract for id/type/role/time/coordinateSpace/x/y/rect/displaySize/metadata.
  confidence: high
- file: `Gui/FluxAiPanel.h`
  symbol: `FluxAiPanel`
  approximate lines: 24-97
  stable anchor: `setSourceCaptureContext`, `updatePromptSummary`, `startSam3StillMask`, `buildSourcePointPrompt`, `buildSourceBoxPrompt`
  reason: Panel already has source context, prompt conversion helpers, and SAM3 launch surface; add AI Paint prompt/source state here.
  confidence: high
- file: `Gui/FluxAiPanel.cpp`
  symbol: `updateUiState`, `refreshSourceFrameMetadata`, `updatePromptSummary`, `onRunClicked`, `startSam3StillMask`
  approximate lines: 350-645
  stable anchor: `_runButton->setEnabled(false);`, `refreshSourceFrameMetadata` returning `false`, RotoPaint prompt text
  reason: Replace disabled/stub UI with native AI Paint prompt summary, validation, and run path.
  confidence: high
- file: `Gui/Gui05.cpp`
  symbol: AI panel creation/source viewer wiring lambda
  approximate lines: 858-867, 1080-1120
  stable anchor: `aiPanel->setViewerForCapture`, no active `setSourceCaptureContext` caller
  reason: Wire the selected source/AI Paint context into the panel when the source viewer is armed.
  confidence: high
- file: `Gui/FluxAiWorkerController.cpp`
  symbol: `FluxAiWorkerController::startNoopJob`
  approximate lines: 58-95
  stable anchor: `output_dir_project_relative`, `output_path_policy`
  reason: Existing generated-media/job contract uses project-relative output roots; mirror this policy for the SAM3 still path.
  confidence: high
- file: `tasks/T083-ai-matte-depth.md`
  symbol: T083 architecture summary / approved decisions
  approximate lines: 5-45
  stable anchor: `Unsaved projects must force Save As before AI generation`
  reason: Task-level constraints: viewer toolbar owns prompts; AI Panel summarizes/runs; project-relative outputs; force Save As.
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
- `Gui/FluxAiWorkerController.h`
- `Gui/FluxAiWorkerController.cpp`
- `tools/ai/flux_ai_worker.py`
- `tasks/T083-ai-matte-depth.md`

## Required Change
1. Keep native AI Paint as prompt source of truth. Do not add point/box capture controls to `FluxAiPanel` and do not touch/hijack RotoPaint.
2. Add the smallest read-only AI Paint prompt access surface needed by the panel:
   - Prefer an `AIPaint` method returning `std::vector<AIPaintPrompt>` or serialized prompt JSON from `_imp->context`/`aiPaintPromptStore`.
   - Do not change prompt persistence format except for compatible/extensible additions already supported by `AIPaintContext`.
3. Update `FluxAiPanel` state/API to accept the selected AI Paint/source context from `Gui05.cpp`.
   - The panel must know the active viewer/source and the native AI Paint prompt provider for that source.
   - If no selected AI Paint/source can be identified from the current `Gui05.cpp` source-viewer wiring without broad timeline/nodegraph changes, stop and report the missing connector instead of guessing.
4. Replace RotoPaint wording in `FluxAiPanel` with AI Paint wording.
   - `updatePromptSummary()` should summarize enabled AI Paint point/box prompts: counts, selected/usable prompt if applicable, source/time mismatch if applicable, and a clear message when no AI Paint prompts exist.
   - Do not render prompts in the panel; viewer overlay remains viewer-only.
5. Make `updateUiState()` enable Run only when all are true:
   - not currently running;
   - a supported model is selected/available enough for the existing SAM3 path;
   - a valid selected source/AI Paint prompt exists;
   - source frame metadata is fresh;
   - project/save/output preflight can succeed.
6. Implement `onRunClicked()` using the existing SAM3 still-mask path:
   - Re-read AI Paint prompts at click time.
   - Choose a deterministic v1 prompt: selected enabled point/box if selection exists; otherwise the newest/enabled point or box for the current source/frame. If multiple valid prompts exist and no selection can be represented, summarize and use the deterministic newest prompt; do not invent multi-prompt backend behavior.
   - Convert native prompt coordinates through existing `buildSourcePointPrompt()` / `buildSourceBoxPrompt()` compatible JSON fields, then call `startSam3StillMask()`.
   - Preserve point label semantics: include = positive label; exclude may be negative only if the existing SAM3 path supports it; otherwise block exclude prompts with a clear message.
7. Replace the `refreshSourceFrameMetadata()` stub with a real minimal still-frame handoff or stop before enabling Run.
   - Required outcome: an actual source PNG path and metadata dimensions that match `validateSourceCaptureContext()` and `startSam3StillMask()` expectations.
   - Generated/intermediate paths must be under the project-relative generated-media root used by existing AI output policy.
   - Unsaved projects must force Save As before any frame export or generated media directory write. Verify/add this preflight in `onRunClicked()` or the metadata/export helper before `QDir().mkpath()`/process launch.
   - If exporting the selected source frame cannot be implemented safely within allowed files, leave Run disabled and report that source-frame export needs a separate locator/task.
8. Ensure SAM3 result manifests remain project-relative and include the selected AI Paint prompt JSON/provenance in the existing `selected_prompt`/`prompts` fields.

## Non-Goals
- Do not modify or hijack native RotoPaint.
- Do not move point/box prompt controls into the AI Panel.
- Do not change viewer overlay prompts into renderable RGBA content.
- Do not redesign the SAM backend, add multi-prompt SAM batching, create nodegraph AI suites, or convert prompts to Roto splines.
- Do not broaden into model-manager/download work beyond honoring existing missing-model status.
- Do not edit task status files in this implementation packet.

## Validation
Commands:
- `cmake --build /home/npittas/Flux/build --target Natron -j$(nproc)`
- If the build directory/target name differs, run the project’s existing equivalent GUI target build and report the exact command used.
- Manual GUI validation: open a saved project, select a source through the existing source-viewer flow, add native AI Paint point and box prompts in the viewer toolbar, confirm the AI Panel summarizes AI Paint prompts without RotoPaint wording, confirm Run is disabled before source/prompt/model/save preconditions and enabled only after they are met.
- Manual generation validation: with SAM3 installed/available, run one point or box prompt and verify output/manifest paths are project-relative; with SAM3 missing, verify the panel blocks with a clear missing-model/provider message and writes no generated media.
- Manual unsaved-project validation: start from an unsaved project and attempt Run; Save As must be forced before any output directory, source PNG, or generated manifest is written.

Expected result:
- Build succeeds.
- AI Panel summarizes native AI Paint point/box prompts from the selected AI Paint/source.
- Run launches the existing SAM3 still-mask path only when preconditions are valid.
- Generated media/manifests are project-relative.
- Unsaved projects cannot write generated media before Save As.

## Stop Conditions
Stop and report if:
- target symbol is missing
- required fix exceeds allowed files
- validation cannot run
- existing architecture contradicts the requested change
- task requires product/design judgment not in packet
- selected AI Paint/source discovery requires editing timeline/nodegraph files not allowed here
- source frame metadata/export cannot be made real within allowed files
- implementation would add AI Panel prompt capture controls or modify RotoPaint
- implementation would write generated media before Save As/project-relative output is established

## Required Return Contract
Return only a task-focused summary. Do not include transcript, tool logs, raw file dumps, large code blocks, or broad unrelated issues. Include status, files inspected/changed, validation evidence, blockers, and task-specific risks.
