# Planner Report

## Status
ready

## Rationale
This is a narrow blocker-fix plan for the reviewed T083 implementation: remove the remaining custom ViewerGL prompt path and stale top-bar/panel references, while preserving the already-passing RotoPaint toolbar/tool behavior. The plan explicitly requires Nick approval for the expanded edit set and does not authorize git revert or cleanup of unrelated dirty task/plan/graphify state.

# Task Packet

## User Goal
Fix the T083 SAM RotoPaint viewer tools review blockers without using git revert: only RotoPaint/RotoBrush drawing path, left vertical viewer toolbar only, no custom ViewerGL prompt draw/capture, no backend/preview/apply/persistence creep.

## Mode
general-coding

## Relevant Locations
- file: `Gui/ViewerGL.cpp`
  symbol: `ViewerGL::setAiViewerPromptSourceContext` through custom prompt append/select/draw helpers
  approximate lines: 599-857
  stable anchor: `ViewerGL::setAiViewerPromptSourceContext`
  reason: custom source-context/source-pixel prompt storage, prompt append/select/remove/clear/count, and draw path must be removed or made unreachable per source of truth.
  confidence: high
- file: `Gui/ViewerGL.h`
  symbol: `AiViewerPromptToolMode`, `AiViewerPrompt`, prompt method declarations, prompt member fields
  approximate lines: 458-476, 520, 604-635, 709-712
  stable anchor: `enum AiViewerPromptToolMode`
  reason: header declarations/members for the forbidden custom ViewerGL prompt API must be removed after cpp cleanup.
  confidence: high
- file: `Gui/Gui05.cpp`
  symbol: SAM source viewer capture setup
  approximate lines: 1121-1127
  stable anchor: `setAiViewerPromptSourceContext`
  reason: still calls the forbidden ViewerGL prompt source-context API.
  confidence: high
- file: `Gui/ViewerTab.cpp`
  symbol: `onAIPromptPointButtonClicked`, `onAIPromptBoxButtonClicked`, `onAIPromptRemoveButtonClicked`, `onAIPromptClearButtonClicked`
  approximate lines: 1112-1133
  stable anchor: `ViewerTab::onAIPromptRemoveButtonClicked`
  reason: point/box handlers are empty, but remove/clear still call forbidden ViewerGL prompt APIs.
  confidence: high
- file: `Gui/ViewerTab.h`
  symbol: AI prompt toolbar slot declarations and related includes/state, if present
  approximate lines: inspect around slot declarations for `onAIPrompt*`
  stable anchor: `onAIPromptRemoveButtonClicked`
  reason: remove stale declarations if cpp/top-bar cleanup leaves them unused.
  confidence: medium-high
- file: `Gui/FluxAiPanel.cpp`
  symbol: `FluxAiPanel::updatePromptSummary`, source-capture methods using ViewerGL prompt data
  approximate lines: 549-551 plus nearby prompt-building helpers as needed
  stable anchor: `Prompt summary: %1 viewer prompt%2`
  reason: panel still reports ViewerGL prompt counts and may depend on forbidden prompt collection.
  confidence: high
- file: `Gui/FluxAiPanel.h`
  symbol: `setViewerForCapture`, `setSourceCaptureContext`, prompt helper declarations/members
  approximate lines: 20-95
  stable anchor: `void setViewerForCapture(ViewerGL* viewer);`
  reason: remove stale active references if no longer needed after disabling custom prompt collection.
  confidence: medium-high

## Allowed Edit Files
Requires Nick approval before implementation because these files were outside/forbidden in the previous packet but are now necessary to remove stale active references:
- `Gui/ViewerGL.cpp`
- `Gui/ViewerGL.h`
- `Gui/Gui05.cpp`
- `Gui/ViewerTab.cpp`
- `Gui/ViewerTab.h`
- `Gui/FluxAiPanel.cpp`
- `Gui/FluxAiPanel.h`

## Read-Only Context Files
- `tasks/T083-sam3-rotopaint-viewer-tools-blocker-fix-plan.md`
- `tasks/TASKS.md`
- `plans/PHASES.md`
- `ARCHITECTURE.md`

## Required Change
1. Do not use git revert, reset, checkout, restore, or broad cleanup.
2. Remove the custom ViewerGL prompt capture/draw/storage path:
   - delete or fully disconnect ViewerGL prompt source-context, prompt count, append/select/remove/clear, source-pixel conversion, prompt drawing, prompt signal, prompt struct/members, and any mouse-event branches that use them.
   - ensure `drawAiViewerPromptIndicators()` is not called or no longer exists.
3. Remove stale calls from `Gui/Gui05.cpp` to `setAiViewerPromptSourceContext`; keep only RotoPaint/SAM tool activation and any safe AI panel selection behavior that does not collect prompts or export/apply/backend data.
4. Remove top-bar/custom prompt UX hooks from `ViewerTab.cpp/.h`: point/box/remove/clear must not call ViewerGL prompt APIs. Prefer removing/deactivating obsolete AI prompt top-bar controls rather than leaving dead active controls.
5. Update `FluxAiPanel.cpp/.h` so it no longer reads ViewerGL prompt counts or presents ViewerGL prompt collection as the prompt source. If the panel remains visible, its prompt summary should state that prompts are created via the RotoPaint SAM point/box tools in the left viewer toolbar, not via ViewerGL prompt storage.
6. Preserve the passing behavior: existing RotoPaint toolbar groups, `eRotoToolSamPoint`, one-point `makeStroke(false, RotoPoint(...))` on pen-down with no stroke-building state afterward, SAM box via existing rectangle path, and existing brush mapping.
7. Do not clean unrelated dirty docs/tasks/plans/graphify state in this task. Report it separately as pre-existing/out-of-scope working-tree contamination.

## Non-Goals
- No backend, preview, apply, manifest, persistence, source-frame export, or prompt JSON feature work.
- No git operations or reverts.
- No cleanup of unrelated dirty `tasks/`, `plans/`, graphify, or documentation files without Nick approval.
- No redesign of RotoPaint/RotoBrush internals beyond preserving the already-reviewed SAM point/box behavior.

## Validation
Commands:
- `cmake --build /home/npittas/Flux/build --target Natron -j$(nproc)`
- `grep -R "setAiViewerPromptSourceContext\|appendAiViewer.*Prompt\|drawAiViewerPromptIndicators\|getAiViewerPromptCount\|removeSelectedAiViewerPrompt\|clearAiViewerPrompts\|AiViewerPrompt" Gui/ViewerGL.cpp Gui/ViewerGL.h Gui/Gui05.cpp Gui/ViewerTab.cpp Gui/ViewerTab.h Gui/FluxAiPanel.cpp Gui/FluxAiPanel.h`
- `grep -R "eRotoToolSamPoint\|makeStroke(false, RotoPoint\|eRotoToolSamBox" Gui/Roto*.cpp Gui/Roto*.h Gui/ViewerTab.cpp Gui/ViewerTab.h Gui/ViewerGL.cpp Gui/ViewerGL.h`

Expected result:
- Build succeeds.
- First grep has no remaining custom ViewerGL prompt API/storage/draw references, except only if a harmless removed-comment is intentionally absent; no active references allowed.
- Second grep confirms SAM point/box still route through RotoPaint/RotoBrush, with brush mappings unchanged.
- Manual GUI smoke check, if a GUI session is available: SAM point/box appear only in the left RotoPaint viewer toolbar; no top-bar prompt capture/remove/clear workflow; point creates one Roto stroke point; box creates rectangle path; no custom prompt overlay is drawn.

## Stop Conditions
Stop and report if:
- Nick has not approved the expanded edit file set.
- target symbol is missing
- required fix exceeds allowed files
- validation cannot run
- existing architecture contradicts the requested change
- task requires product/design judgment not in packet
- removing ViewerGL prompt APIs would require changing backend/SAM apply/persistence code beyond disconnecting stale active references
- unrelated dirty docs/tasks/plans/graphify files would need to be edited or reverted to make validation pass

## Required Return Contract
Return only a task-focused summary. Do not include transcript, tool logs, raw file dumps, large code blocks, or broad unrelated issues. Include status, files inspected/changed, validation evidence, blockers, and task-specific risks. Explicitly separate blocker-fix source changes from pre-existing/out-of-scope dirty working-tree files.
