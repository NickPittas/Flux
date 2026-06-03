# Planner Report

## Status
ready

## Rationale
This blocker-fix plan is limited to correcting the two reviewer blockers in the existing T083 live-preview overlay implementation: remove or explicitly contain the unauthorized `Gui/FluxAiPanel.h` change, and make AI Paint live-mask texture ownership safe. The behavior goal is unchanged: live SAM3 preview remains an AI Paint viewer overlay, does not rewire the AI Work Viewer, and does not affect final/manual Run preview or render output.

# Task Packet

## User Goal
Make the T083 live preview AI Paint overlay implementation ready for manual proof by fixing the reviewer blockers while preserving achieved behavior: live preview does not rewire the viewer; final/manual Run preview remains separate; live mask overlay draws before prompts; render output is unchanged.

## Mode
general-coding

## Relevant Locations
- file: `Gui/FluxAiPanel.h`
  symbol: `class FluxAiPanel`
  approximate lines: 21-26, 39-44, 81-87, 142-145
  stable anchor: `#include "Engine/AIPaintContext.h"` and `_livePreviewDebounceTimer`
  reason: reviewer flagged this header as modified outside the previous allowed edit set. The narrow fix is to avoid header changes unless required by already-existing declarations; if live-overlay-only declarations/includes were added here, remove them. If current declarations genuinely require this header, keep only the minimal necessary declarations and document why.
  confidence: high
- file: `Gui/FluxAiPanel.cpp`
  symbol: `aipaintEffectFromNode`, `clearAIPaintLivePreviewMask`, live `infer_still` handling
  approximate lines: 438-455, 488-524, 1315-1326
  stable anchor: `static AIPaint* aipaintEffectFromNode(const NodePtr& aiPaintNode)` and `aiPaint->setLivePreviewMaskPath(maskPath);`
  reason: live result routing can remain entirely in the `.cpp` through static helpers; no FluxAiPanel public/header API should be needed for the overlay routing.
  confidence: high
- file: `Engine/AIPaint.h`
  symbol: `class AIPaint`
  approximate lines: 77-84
  stable anchor: `void setLivePreviewMaskPath(const QString& absolutePngPath);` and `void clearLivePreviewMask();`
  reason: public AI Paint instance API is the appropriate narrow boundary for FluxAiPanel to set/clear a live overlay mask.
  confidence: high
- file: `Engine/AIPaint.cpp`
  symbol: `AIPaintPrivate`, `AIPaint::~AIPaint`, `AIPaint::setLivePreviewMaskPath`, `AIPaint::clearLivePreviewMask`, `AIPaint::knobChanged`, `AIPaint::drawOverlay`
  approximate lines: 145-173, 198-200, 265-289, 553-564, 595-666
  stable anchor: `GLuint liveMaskTexture;`, empty `AIPaint::~AIPaint()`, and `releaseLiveMaskTexture(&_imp->liveMaskTexture);`
  reason: reviewer blocker is unsafe persistent GL texture lifecycle: texture is deleted on dirty redraw but not on destruction, and deletion requires a current GL context.
  confidence: high

## Allowed Edit Files
- `Engine/AIPaint.h`
- `Engine/AIPaint.cpp`
- `Gui/FluxAiPanel.cpp`
- `Gui/FluxAiPanel.h` — allowed only to remove/avoid the previously unauthorized live-overlay header change, or to keep the minimal declarations that are demonstrably required by existing `FluxAiPanel.cpp`/class state; do not add new live-overlay API here.

## Read-Only Context Files
- `tasks/T083-live-preview-aipaint-overlay-fix-plan.md`

## Required Change
1. Resolve the `Gui/FluxAiPanel.h` scope blocker narrowly:
   - Prefer no header change for the live-overlay fix. Keep live-overlay helper functions as `static`/anonymous-scope implementation details in `Gui/FluxAiPanel.cpp`.
   - Remove any header include/declaration/member added solely for the overlay routing if it is not required to compile.
   - If `Gui/FluxAiPanel.h` must retain changes because existing declarations use `AIPaintPrompt` or live-preview debounce state, keep only those necessary declarations/includes and document the necessity in the worker return summary. Do not add a new public FluxAiPanel overlay API.
2. Fix `AIPaint` live-mask texture lifecycle safely. Do not call `glDeleteTextures` from `AIPaint::~AIPaint()` unless the code can prove a current GL context is available there. The preferred narrow safe fix is to avoid owning a persistent GL texture across overlay frames:
   - Replace `GLuint liveMaskTexture` persistent ownership with CPU-side loaded mask state (`QImage` or Cairo-loaded pixel copy/path metadata) plus a per-`drawOverlay()` transient texture, or equivalent non-persistent upload/draw/delete pattern.
   - In `drawOverlay()`, when a valid live mask exists, create/upload the GL texture inside the active overlay GL context, draw the tinted quad before prompt primitives, then delete the texture before returning from `drawOverlay()` while the context is still current.
   - If keeping a persistent texture instead, implement a documented context-current deletion path for replacement and destruction; if that cannot be guaranteed from the allowed files, do not keep a persistent texture.
3. Preserve the overlay behavior already implemented:
   - `setLivePreviewMaskPath()` stores/replaces the current mask path or CPU image state and calls `redrawOverlayInteract()`.
   - `clearLivePreviewMask()` clears the live mask state and calls `redrawOverlayInteract()`.
   - `drawOverlay()` draws the live mask as a semi-transparent tinted overlay before point/box prompts, so prompts remain visible/editable above it.
   - `knobChanged()` clear-all prompt handling may clear the live overlay if already implemented; keep that behavior if present.
4. Preserve live/final separation in `Gui/FluxAiPanel.cpp`:
   - Live `infer_still` success should resolve `_sourceAIPaintNode` to `AIPaint` and call `setLivePreviewMaskPath(maskPath)`.
   - Live preview must not call `_gui->previewFluxAiResultPngInWorkViewer(...)`, must not create/update `FluxSAM3LiveResultPreview`, and must not disconnect/connect viewer inputs.
   - Final/manual Run preview behavior must remain separate and unchanged.
5. Preserve clear points:
   - Clear the live overlay when live preview is disabled, when SAM3 unload is requested/completed, and when the selected/source AI Paint node changes.
6. Do not change RotoPaint, main viewer wiring, render output, SAM3 worker protocol, or final/manual Run result preview.

## Non-Goals
- No RotoPaint changes.
- No main comp viewer changes.
- No render-output changes; live mask remains overlay-only.
- No reintroduction of live preview Read-node rewiring.
- No redesign of FluxAiPanel public API.
- No broad cleanup of AI Paint prompts, SAM3 worker protocol, or final/manual Run preview.

## Validation
Commands:
- `cmake --build /home/npittas/Flux/build --target Natron -j$(nproc)`

Manual GUI proof:
- With footage and an AI Paint effect selected, enable Live Preview, add/select/remove point and box prompts, and verify the AI Work Viewer stays on the source/AI Paint node rather than switching to `FluxSAM3LiveResultPreview`.
- Verify the SAM3 live matte appears as a semi-transparent overlay and prompt handles draw above it and remain editable.
- Trigger at least two live results and verify the overlay updates without Read-node buildup or viewer input rewiring.
- Disable Live Preview and unload SAM3; verify the overlay clears and the viewer input remains unchanged.
- Run the existing final/manual SAM3 Run path and verify final result preview remains separate/unchanged.
- Capture screenshot or recording evidence showing the live overlay, visible/editable prompts, and unchanged viewer source.

Expected result:
Build succeeds. `Gui/FluxAiPanel.h` is either unchanged for live-overlay purposes or its retained change is explicitly justified. AI Paint no longer owns a GL texture that can leak or require unsafe destruction without a current context. Live preview remains overlay-only; final/manual Run preview and render output are unchanged.

## Stop Conditions
Stop and report if:
- target symbol is missing
- required fix exceeds allowed files
- validation cannot run
- existing architecture contradicts the requested change
- task requires product/design judgment not in packet
- safe texture cleanup requires GL context APIs outside the allowed files
- fixing `Gui/FluxAiPanel.h` would require removing declarations needed by existing non-overlay behavior
- live/final preview separation cannot be preserved without editing viewer/RotoPaint/render-output code

## Planner Self-Check
- locator evidence sufficient: yes — reviewer blockers map directly to located header scope, live routing helpers, and AI Paint texture ownership/drawOverlay code.
- allowed edit files minimal and explicit: yes — four explicit files; `Gui/FluxAiPanel.h` is included only to resolve the unauthorized-header blocker narrowly.
- read-only context minimal: yes — only the previous T083 plan is needed beyond allowed edit files.
- anchors/lines included: yes — each relevant location includes path, symbol/anchor, approximate lines, reason, and confidence.
- validation concrete: yes — one build command plus task-specific GUI proof checks and screenshot/recording requirement.
- parallelization decision explicit and safe: yes — single task; header scope, live routing, and AI Paint texture lifecycle are coupled and should not be parallelized.
- non-goals and stop conditions sufficient: yes — explicitly forbids RotoPaint/main viewer/render-output/rewiring scope creep and stops on unsafe GL context needs.
- reviewer findings addressed, if revision: yes — blocker 1 is resolved by including `Gui/FluxAiPanel.h` only for removal/minimal justification; blocker 2 is resolved by requiring transient-context texture deletion or proven context-current deletion.

## Required Return Contract
Return only a task-focused summary. Do not include transcript, tool logs, raw file dumps, large code blocks, or broad unrelated issues. Include status, files inspected/changed, validation evidence, blockers, and task-specific risks, including whether `Gui/FluxAiPanel.h` was reverted/justified and how GL texture lifecycle was made safe.
