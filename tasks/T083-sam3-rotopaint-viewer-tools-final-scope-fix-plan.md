# Planner Report

## Status
ready

## Rationale
This plan is sufficient and scoped because the remaining reviewed blockers are confined to active SAM3 source-export/context wiring in `Gui/Gui05.cpp` and backend/preview/apply-facing controls and handlers in `Gui/FluxAiPanel.*`; the already-passing RotoPaint SAM point/box tool path is treated as read-only behavior to preserve, not reworked.

# Task Packet

## User Goal
Fix the remaining T083 SAM RotoPaint viewer tools review blockers while preserving the passing RotoPaint/RotoBrush drawing path: SAM tools belong in the left vertical viewer toolbar, not the top bar; no custom ViewerGL prompt capture/draw/storage; no backend, preview, apply, persistence, source export, or source-context plumbing in this slice.

## Mode
general-coding

## Relevant Locations
- file: `Gui/Gui05.cpp`
  symbol: source viewer arming / AI panel context setup in the selected-layer SAM/RotoPaint path
  approximate lines: 1083-1128
  stable anchor: `exportFluxSam3SourceFrameForSelectedLayer(&sourceMetadata)`
  reason: reviewer found backend/source-export creep still active here; remove source PNG export, metadata probing, and `setSourceCaptureContext(...)` from this viewer-tool blocker-fix path while preserving safe viewer activation and RotoPaint tool behavior.
  confidence: high
- file: `Gui/Gui05.cpp`
  symbol: timeline source frame capture connection
  approximate lines: 1144-1147
  stable anchor: `QObject::connect(timeline, &FluxTimeline::sourceFrameCaptureRequested, this,`
  reason: connection still invokes source-frame export; disconnect/remove for this scope unless it is proven unrelated to SAM viewer tools, in which case stop and report.
  confidence: high
- file: `Gui/FluxAiPanel.cpp`
  symbol: `FluxAiPanel::setupUi`
  approximate lines: 236-245
  stable anchor: `_runButton = new QPushButton(tr("Preview / Run"));`
  reason: reviewer found visible Preview/Run, Apply, Cancel controls that expose backend/preview/apply workflow; hide/remove these controls for this scope or make them explicitly inactive future UI only if no backend path remains reachable.
  confidence: high
- file: `Gui/FluxAiPanel.cpp`
  symbol: constructor signal wiring for run/apply/cancel
  approximate lines: 68-73
  stable anchor: `QObject::connect(_runButton, SIGNAL(clicked(bool)), this, SLOT(onRunClicked()));`
  reason: remove or guard signal wiring for any controls removed/hidden so no backend/apply path is reachable through the panel.
  confidence: high
- file: `Gui/FluxAiPanel.cpp`
  symbol: `FluxAiPanel::updateUiState`
  approximate lines: 367-376
  stable anchor: `_runButton->setEnabled(false);`
  reason: ensure hidden/removed backend-facing controls are not enabled or surfaced by runtime state updates.
  confidence: high
- file: `Gui/FluxAiPanel.cpp`
  symbol: `FluxAiPanel::onApplyClicked`
  approximate lines: 547-551
  stable anchor: `Apply is handled by a later packet`
  reason: reviewer found apply handler messaging still exposes future apply workflow; remove the active slot path or ensure it is unreachable with no visible/apply control in this scope.
  confidence: high
- file: `Gui/FluxAiPanel.cpp`
  symbol: `FluxAiPanel::onRunClicked` and SAM3 runner code
  approximate lines: 558-617 and following backend helpers as needed
  stable anchor: `SAM3 run is disabled until the viewer-owned multi-prompt backend contract is implemented.`
  reason: runtime disables run but leaves preview/run UI and SAM3 backend path present; remove the visible/run entry point and avoid exposing backend preview/run in this slice. Do not implement backend behavior.
  confidence: high
- file: `Gui/FluxAiPanel.h`
  symbol: run/apply/cancel slots, backend helper declaration, and button members
  approximate lines: 39-60, 89-91
  stable anchor: `void onRunClicked();`
  reason: update declarations/members only as required by the `FluxAiPanel.cpp` cleanup so removed/hidden controls and unreachable slots do not leave build errors or active UI paths.
  confidence: high
- file: `Engine/RotoPaint.cpp`
  symbol: SAM point/box RotoPaint tool behavior
  approximate lines: 869-898, 2731-2748
  stable anchor: `case eRotoToolSamPoint:`
  reason: read-only preservation target; SAM point/box must remain in RotoPaint path and not be reimplemented in GUI/backend code.
  confidence: high
- file: `Engine/RotoPaintInteract.cpp`
  symbol: SAM action-to-tool mapping and style handling
  approximate lines: 627-633, 1071-1073, 1166-1167
  stable anchor: `samPointAction.lock()`
  reason: read-only preservation target confirming the passed RotoPaint SAM path remains present.
  confidence: high
- file: `Engine/RotoPaintInteract.h`
  symbol: SAM tool enum/action members
  approximate lines: 487-491, 565-569
  stable anchor: `eRotoToolSamPoint`
  reason: read-only preservation target confirming SAM point/box state belongs to RotoPaint tools.
  confidence: high

## Allowed Edit Files
- `Gui/Gui05.cpp`
- `Gui/FluxAiPanel.cpp`
- `Gui/FluxAiPanel.h`

## Read-Only Context Files
- `Engine/RotoPaint.cpp`
- `Engine/RotoPaintInteract.cpp`
- `Engine/RotoPaintInteract.h`
- `tasks/T083-sam3-rotopaint-viewer-tools-blocker-fix-plan.md`
- `tasks/T083-sam3-rotopaint-viewer-tools-plan.md`

## Required Change
1. Do not use git revert, reset, restore, checkout, or broad cleanup.
2. In `Gui/Gui05.cpp`, remove the active source-frame export/context plumbing from the selected-layer SAM/RotoPaint viewer path:
   - remove the `QJsonObject sourceMetadata` / `exportFluxSam3SourceFrameForSelectedLayer(&sourceMetadata)` call used while arming source viewer context;
   - remove exported width/height gating and the `aiPanel->setSourceCaptureContext(...)` call;
   - keep only non-backend viewer/tool behavior, such as selecting/arming the viewer and, if still needed, `aiPanel->setViewerForCapture(...)` without source export, source dimensions, reader metadata, or backend context.
3. In `Gui/Gui05.cpp`, remove the `sourceFrameCaptureRequested` connection that directly calls `exportFluxSam3SourceFrameForSelectedLayer()` for this slice. If the signal is required by unrelated existing product behavior outside SAM viewer tools, stop and report exact evidence instead of expanding scope.
4. In `Gui/FluxAiPanel.cpp/.h`, remove or hide the Preview/Run, Apply, and Cancel workflow from the visible AI panel for this scope:
   - no visible `Preview / Run`, `Apply`, or backend-facing `Cancel` buttons for T083 viewer tools;
   - no clickable signal path to `onRunClicked`, `onApplyClicked`, `_worker->cancel()`, or `cancelSam3()` from visible controls;
   - remove corresponding members/slots only if that is the smallest build-safe cleanup. It is acceptable to leave private unreachable backend helper code if removing it would expand risk, but it must not be visible, enabled, connected from UI, or presented as part of this slice.
5. Update any status/prompt text touched by the cleanup to state that SAM prompts are managed via the RotoPaint SAM Point/Box tools in the left viewer toolbar. Do not mention preview/run/apply as available workflow.
6. Preserve the passed checks: do not add ViewerGL prompt API/storage/draw references, do not restore top-bar SAM prompt controls, do not add AI panel dependency on ViewerGL prompt capture, and do not change the RotoPaint SAM point/box drawing path in `Engine/RotoPaint*`.
7. Do not implement backend, preview generation, apply-to-mask/nodegraph integration, persistence, source export, source-frame metadata refresh, or prompt JSON/source-pixel conversion in this task.

## Non-Goals
- No edits to `Engine/RotoPaint.cpp`, `Engine/RotoPaintInteract.cpp`, or `Engine/RotoPaintInteract.h` unless a stop condition is reached and a new plan is approved.
- No ViewerGL prompt capture/draw/storage work.
- No top viewer bar SAM prompt UX.
- No SAM3 backend execution, preview, apply, manifest, persistence, source-frame export, or source-export API cleanup beyond disconnecting this active viewer-tool path.
- No broad AI panel redesign beyond hiding/removing the backend-facing controls and entry points identified by the reviewer.
- No cleanup of unrelated dirty docs/tasks/plans/graphify files.

## Validation
Commands:
- `cmake --build /home/npittas/Flux/build --target Natron -j$(nproc)`
- `grep -R "exportFluxSam3SourceFrameForSelectedLayer(&sourceMetadata)\|setSourceCaptureContext\|sourceFrameCaptureRequested" Gui/Gui05.cpp Gui/FluxAiPanel.cpp Gui/FluxAiPanel.h`
- `grep -R "Preview / Run\|Apply is handled\|SAM3 run is disabled\|buttons->addWidget(_runButton)\|buttons->addWidget(_applyButton)\|buttons->addWidget(_cancelButton)" Gui/FluxAiPanel.cpp Gui/FluxAiPanel.h`
- `grep -R "setAiViewerPromptSourceContext\|appendAiViewer.*Prompt\|drawAiViewerPromptIndicators\|getAiViewerPromptCount\|removeSelectedAiViewerPrompt\|clearAiViewerPrompts\|AiViewerPrompt" Gui/Gui05.cpp Gui/FluxAiPanel.cpp Gui/FluxAiPanel.h Gui/ViewerGL.cpp Gui/ViewerGL.h Gui/ViewerTab.cpp Gui/ViewerTab.h`
- `grep -R "eRotoToolSamPoint\|makeStroke(false, RotoPoint\|eRotoToolSamBox" Engine/RotoPaint.cpp Engine/RotoPaintInteract.cpp Engine/RotoPaintInteract.h`

Expected result:
- Build succeeds.
- First grep has no `Gui05.cpp` active source-export/context path for this slice; `setSourceCaptureContext` may remain only as an uncalled declaration/definition in `FluxAiPanel.*` if not safely removable in this narrow cleanup.
- Second grep shows no visible Preview/Run/Apply/Cancel UI construction or stale apply/run messaging exposed by the panel.
- Third grep confirms no custom ViewerGL prompt API/storage/draw references have been reintroduced in the scoped files; if pre-existing references remain in files not edited by this task, report them with anchors rather than broadening scope.
- Fourth grep confirms SAM point/box still route through RotoPaint/RotoBrush code.
- Manual GUI smoke check if GUI is available: SAM point/box controls appear only in the left RotoPaint viewer toolbar; AI panel does not present Preview/Run/Apply backend workflow; existing RotoPaint SAM point and box drawing still work.

## Stop Conditions
Stop and report if:
- target symbol is missing
- required fix exceeds allowed files
- validation cannot run
- existing architecture contradicts the requested change
- task requires product/design judgment not in packet
- removing/hiding the AI panel workflow requires deciding a future product UX beyond “not visible/active in this slice”
- source-frame export removal breaks non-SAM viewer behavior outside the authorized scope
- preserving RotoPaint SAM tools requires changes to Engine files
- unrelated dirty files would need to be edited or reverted to make validation pass

## Required Return Contract
Return only a task-focused summary. Do not include transcript, tool logs, raw file dumps, large code blocks, or broad unrelated issues. Include status, files inspected/changed, validation evidence, blockers, and task-specific risks. Explicitly note that no git revert/reset/restore was used.
