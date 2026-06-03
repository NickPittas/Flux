# Planner Report

## Status
ready

## Rationale
Reviewer stop-condition sentinel is resolved by expanding the packet with Nick-approved `Gui/Gui05.cpp` edit access only for the existing source-viewer/SAM3 source handoff path that already computes selected source context. The implementation remains scoped to viewer toolbar ownership, ViewerGL prompt model/input/drawing, exact source-frame pixel prompt coordinates with source/layer/frame identity, and AI-panel demotion; backend, preview/apply, and project persistence stay deferred.

# Task Packet

## User Goal
Recover T083 SAM3 point/box prompting so selection and add/remove/clear controls live in the viewer toolbar, not the AI panel; support multiple points and boxes per source image/video; show persistent visual indicators for every point and box in the viewer; and store prompts in exact source-frame pixel coordinates with source dimensions and source/layer/frame identity. Backend/preview/apply/persistence remain deferred.

## Mode
general-coding

## Relevant Locations
- file: `Gui/ViewerTab.cpp`
  symbol: `ViewerTab::ViewerTab(...)` constructor
  approximate lines: 126-210, 603
  stable anchor: `_imp->mainLayout = new QVBoxLayout(this);`, `/*1st row of buttons*/`, `_imp->firstSettingsRow`, `_imp->firstRowLayout->addWidget(...)`, `_imp->mainLayout->addWidget(_imp->viewerContainer);`
  reason: Existing permanent viewer toolbar/settings row; add SAM3 point mode, box mode, remove selected prompt, and clear all prompts here or in an immediately adjacent viewer-toolbar surface.
  confidence: high
- file: `Gui/ViewerTab.h`
  symbol: `class ViewerTab`
  approximate lines: inspect around toolbar/action public/private declarations
  stable anchor: `class ViewerTab`
  reason: Declare viewer-toolbar action slots/state only if needed for the new SAM3 toolbar controls.
  confidence: medium
- file: `Gui/ViewerTab30.cpp`
  symbol: `ViewerTab::setTrackerInterface`, `ViewerTab::setPluginViewerInterface`, `ViewerTab::setCurrentNodeViewerInterface`
  approximate lines: 410-433, 592-640, 705-710
  stable anchor: `_imp->mainLayout->insertWidget(index, buttonsBar);`, `index = _imp->mainLayout->indexOf(_imp->viewerContainer);`, `getCurrentButtonsBar()`
  reason: Read-only pattern for Natron-style viewer toolbars above the viewer; do not edit unless a compile-only declaration forces it, and stop first if so.
  confidence: high
- file: `Gui/NodeViewerContext.cpp`
  symbol: `NodeViewerContextPrivate::addToolBarTool`, `NodeViewerContext::getToolBar`
  approximate lines: 111-127, 328-424, 613-700
  stable anchor: `QAction* addToolBarTool(...)`, `toolButton = new ViewerToolButton(toolbar);`, `action->setData(data);`, `toolButton->addAction(action);`
  reason: Read-only implementation pattern for grouped `ViewerToolButton`/`QAction` viewer tools like Roto/Tracker.
  confidence: high
- file: `Gui/ViewerGL.h`
  symbol: `ViewerGL`, existing SAM3 prompt capture API/signals/state
  approximate lines: 454-503, 656-665
  stable anchor: `void armAiViewerBoxCapture();`, `void armAiViewerPointCapture();`, `Q_EMIT aiViewerPointCaptured(prompt)` signal declarations, `_aiViewerBoxCaptureArmed`, `_aiViewerPointCaptureArmed`, `_sam3PreviewMaskImage`
  reason: Replace/extend one-shot AI-named prompt capture with viewer-owned prompt tool mode, stable source-pixel prompt collection API, selected prompt, source-context setter, and prompt collection change signals.
  confidence: high
- file: `Gui/ViewerGL.cpp`
  symbol: `ViewerGL::paintGL`
  approximate lines: 503-522
  stable anchor: `drawSam3PreviewMaskOverlay();` followed by `drawOverlay(getCurrentMipmapLevel());`
  reason: Insert persistent prompt indicator drawing after/beside SAM3 preview mask overlay and before/around normal overlays so indicators are visible and distinct.
  confidence: high
- file: `Gui/ViewerGL.cpp`
  symbol: `ViewerGL::drawOverlay(unsigned int mipmapLevel)` and `ViewerGL::drawSam3PreviewMaskOverlay()`
  approximate lines: 636-685, 688-827
  stable anchor: `const RectD canonicalFormat = getCanonicalFormat(0);`, `_imp->viewerTab->drawOverlays( time, RenderScale::fromMipmapLevel(mipmapLevel) );`
  reason: Existing overlay drawing examples. Use only as drawing reference; stored prompt data must remain exact source-frame pixel coordinates and be transformed to viewer/canonical space only at draw time.
  confidence: high
- file: `Gui/ViewerGL.cpp`
  symbol: `ViewerGL::mousePressEvent(QMouseEvent*)`, `ViewerGL::mouseReleaseEvent(QMouseEvent*)`, `ViewerGL::penMotionInternal(...)`
  approximate lines: 1833-2164, 2167-2254, 2330-2710
  stable anchor: `_aiViewerPointCaptureArmed`, `_aiViewerBoxCaptureDragging`, `if (_imp->ms == eMouseStateSelecting)`, `case eMouseStateSelecting:`
  reason: Convert one-shot point/box capture to multi-prompt add/select/remove/clear behavior while preserving pan/zoom/wipe/plugin overlay/default selection.
  confidence: high
- file: `Gui/ViewerGL.cpp`
  symbol: `ViewerGL::armAiViewerBoxCapture`, `ViewerGL::armAiViewerPointCapture`, `ViewerGL::cancelAiViewerPromptCapture`
  approximate lines: 2735-2755
  stable anchor: `ViewerGL::armAiViewerPointCapture()`
  reason: Rename/adapt arming into viewer prompt tool mode setters; ensure point and box modes are mutually exclusive and cancel in-progress box drag safely.
  confidence: high
- file: `Gui/FluxAiPanel.cpp`
  symbol: `FluxAiPanel::setupUi`, constructor signal wiring, `updateUiState`, `onCaptureViewerPointClicked`, `onCaptureViewerBoxClicked`, `onClearPromptClicked`, `onViewerPointCaptured`, `onViewerBoxCaptured`, `startSam3StillMask`
  approximate lines: 76-89, 278-286, 421-432, 641-759, 833-883
  stable anchor: `_captureViewerPointButton = new QPushButton(tr("Point"));`, `_viewer->armAiViewerPointCapture();`, `_lastPrompt = sourcePrompt; _hasPrompt = true;`, `promptArgs << "--prompt-kind" << kind`
  reason: Remove/demote AI-panel point/box/clear controls and single-prompt ownership; keep task/model/status/run/apply/log and summary-only display. Do not expand backend flow beyond compile-required adaptation.
  confidence: high
- file: `Gui/FluxAiPanel.h`
  symbol: `class FluxAiPanel`
  approximate lines: 30-129
  stable anchor: `void onCaptureViewerPointClicked();`, `QJsonObject _lastPrompt; bool _hasPrompt;`
  reason: Remove prompt capture slots/buttons from AI-panel API; convert remaining prompt state to summary/cache only if needed for compile, not authoritative ownership.
  confidence: high
- file: `Gui/Gui05.cpp`
  symbol: existing `sourceViewerRequested` / SAM3 source handoff path
  approximate lines: inspect only around the code path that computes selected source context and calls `FluxAiPanel::setSourceCaptureContext`
  stable anchor: `sourceViewerRequested`, `setSourceCaptureContext`
  reason: Nick-approved edit scope expansion: forward the already-computed source context to `ViewerGL` through a new source-context setter, using the same layer/source/frame identity already passed to `FluxAiPanel::setSourceCaptureContext`.
  confidence: high

## Allowed Edit Files
- `Gui/ViewerTab.cpp`
- `Gui/ViewerTab.h`
- `Gui/ViewerGL.cpp`
- `Gui/ViewerGL.h`
- `Gui/FluxAiPanel.cpp`
- `Gui/FluxAiPanel.h`
- `Gui/Gui05.cpp`

## Read-Only Context Files
- `tasks/T083-sam3-viewer-toolbar-multiprompt-recovery-plan.md`
- `/tmp/flux-t083-wave1/01-viewer-toolbar-locator.md`
- `/tmp/flux-t083-wave1/02-viewergl-drawing-locator.md`
- `/tmp/flux-t083-wave1/03-viewer-input-hit-test-locator.md`
- `/tmp/flux-t083-wave1/04-source-coordinate-audit.md`
- `/tmp/flux-t083-wave1/05-ai-panel-boundary-audit.md`
- `/tmp/flux-t083-wave1/06-prompt-model-design.md`
- `/tmp/flux-t083-wave1/07-backend-impact-audit.md`
- `/tmp/flux-t083-wave1/08-preview-overlay-impact-audit.md`
- `/tmp/flux-t083-wave1/09-persistence-impact-audit.md`
- `/tmp/flux-t083-wave1/10-validation-proof-plan.md`
- `/tmp/flux-t083-wave1/11-roto-tracker-ux-scout.md`
- `/tmp/flux-t083-wave1/12-regression-risk-review.md`
- `/tmp/flux-t083-wave1/graphify-viewer-toolbar-query.log`
- `Gui/ViewerTab30.cpp`
- `Gui/NodeViewerContext.cpp`
- `Gui/ViewerToolButton.h`
- `Gui/ViewerToolButton.cpp`

## Required Change
Implement the following subtasks in order:

1. Viewer-owned source-pixel prompt model in `ViewerGL`:
   - Add a small typed prompt model in `Gui/ViewerGL.h`/`.cpp` or private `ViewerGL` state using only the allowed files.
   - Each stored prompt must have: stable ID, type (`point` or `box`), label defaulting to positive `1`, selected state, exact source-frame pixel coordinates, source width, source height, source identity, layer identity, and frame identity.
   - Point coordinates must be stored as source-frame pixel `x,y`. Box coordinates must be stored as source-frame pixel rectangle corners/extents, not project/canvas/widget coordinates.
   - Do not store canonical-only prompts and do not store "source if available" prompts. Canonical/viewer coordinates may be derived transiently for drawing only.
   - The collection must support append point, append box, select one prompt, remove selected prompt, clear all prompts, and emit/update UI on collection changes.
   - Store multiple points and multiple boxes; adding a new prompt must not replace existing prompts.

2. Source-context handoff from existing selected-source path:
   - Add a new `ViewerGL` source-context setter that accepts the same source/layer/frame identity and source dimensions required by the prompt model.
   - In `Gui/Gui05.cpp`, edit only the existing `sourceViewerRequested`/SAM3 source handoff path that already computes selected source context and calls `FluxAiPanel::setSourceCaptureContext`.
   - Call the new `ViewerGL` source-context setter from that path using the same layer/source/frame identity already passed to `FluxAiPanel::setSourceCaptureContext`.
   - This `Gui/Gui05.cpp` permission is limited to forwarding already-computed source context to `ViewerGL`; make no other behavior, graph, timeline, panel, serialization, backend, preview, apply, or persistence changes there.

3. Prove and use exact source-coordinate mapping/identity before adding prompts:
   - Before accepting a point or box, prove in the allowed files how a viewer event maps to the active source frame pixel coordinate and how the active source/layer/frame identity and source dimensions are obtained.
   - If exact source-frame pixel mapping, source dimensions, source identity, layer identity, or frame identity cannot be proven and populated from the allowed files, stop and report the blocker. Do not fall back to canonical, project, widget, display-window, or best-effort coordinates.
   - Clamp/reject coordinates only according to proven source dimensions; do not silently reinterpret project-size coordinates as source pixels.

4. Viewer toolbar controls in `ViewerTab`:
   - Add SAM3 point mode, SAM3 box mode, remove selected prompt, and clear all prompt controls to the viewer toolbar/first settings row or an above-viewer toolbar consistent with Roto/Tracker patterns.
   - Wire controls to `ViewerGL` prompt tool/mutation APIs.
   - Keep point and box modes mutually exclusive. Remove selected should delete only the selected prompt. Clear all should delete all prompts for the current viewer/source prompt collection.
   - Use existing viewer toolbar styling/action patterns where possible; do not add these controls to the AI panel.

5. Viewer input and hit-testing in `ViewerGL`:
   - Convert the current one-shot point/box capture path into viewer prompt tool behavior.
   - Point mode: left click appends a point at the exact source-frame pixel location.
   - Box mode: drag appends a box in exact source-frame pixel coordinates; reject zero/tiny boxes safely after source-pixel conversion.
   - Selection: clicking an existing point/box selects it so toolbar remove deletes that one prompt.
   - Event priority must not break existing pan/zoom/wipe/plugin overlay/default selection behavior; prompt hit-test/tool handling should run only when a SAM3 prompt mode is active or when selecting existing prompt indicators.

6. Persistent prompt indicator drawing in `ViewerGL`:
   - Draw every point and box every paint by transforming stored source-frame pixel coordinates to the current viewer/canonical drawing space at draw time.
   - Indicators must remain visible under zoom/pan and be visually distinct from the magenta SAM3 preview mask overlay.
   - Selected prompt must have a distinct visual state.
   - Do not use AI-panel labels as the UI truth; the viewer overlay is authoritative.

7. AI-panel demotion/removal of prompt controls:
   - Remove AI-panel Point/Box/Clear prompt buttons and their constructor signal wiring.
   - Remove or disable AI-panel slots that arm viewer point/box capture or clear prompt state as an owner.
   - Remove/demote `_lastPrompt`/`_hasPrompt` single-prompt ownership so it is not the authoritative point/box store. It may remain only as a compile-required summary/cache fed from viewer-owned collection; do not let it replace multi-prompt behavior.
   - AI panel may show prompt summary/counts and continue task/model/status/run/apply/log UI, but it must not expose point/box/add/remove/clear controls.

8. Backend/preview/apply/persistence boundaries:
   - Preserve backend execution, SAM3 preview mask overlay, Apply, and project save/reopen persistence as deferred.
   - Do not edit `tools/ai/*`, `Gui/ProjectGuiSerialization.*`, `Gui/ProjectGui.cpp`, or timeline serialization in this packet.
   - Do not edit `Gui/Gui05.cpp` except for the source-viewer handoff path described above.
   - If existing AI-panel backend code cannot compile after demoting single-prompt ownership, make the smallest compile-only adaptation inside `Gui/FluxAiPanel.*` to consume/ignore a viewer-owned summary. Do not implement multi-prompt backend CLI, preview result loading, apply-to-mask, or persistence in this packet.

## Non-Goals
- Do not implement SAM3 Python/backend multi-prompt CLI support.
- Do not implement generated mask preview loading/reload behavior for the new prompt collection.
- Do not implement Apply-to-mask/nodegraph binding.
- Do not implement save/reopen persistence for prompts or generated results.
- Do not edit project serialization files or task docs other than this implementation packet.
- Do not edit `Gui/Gui05.cpp` outside the existing selected-source/SAM3 source handoff path.
- Do not create AI-panel point/box/add/remove/clear controls.
- Do not preserve AI-panel single-prompt ownership as the source of truth.
- Do not introduce hardcoded center/full-image/default text prompts.
- Do not weaken the coordinate contract to canonical/display/project/widget coordinates.
- Do not store prompts without source dimensions and source/layer/frame identity.
- Do not make high-level UX/product changes such as alternate control placement or label semantics beyond positive default `1`.

## Validation
Commands:
- `cmake --build /home/npittas/Flux/build --target Natron -- -j$(nproc)`
- If that target is unavailable in this checkout, run the established Flux/Natron GUI build target and record the exact command used.
- Boundary grep after build: `grep -R "_captureViewerPointButton\|_captureViewerBoxButton\|onCaptureViewerPointClicked\|onCaptureViewerBoxClicked" Gui/FluxAiPanel.*` and verify no active AI-panel point/box control ownership remains.

Expected result:
- Build succeeds.
- GUI/source-coordinate proof artifacts are collected under a fresh directory such as `/tmp/flux-t083-viewer-toolbar-proof-$(date +%Y%m%d-%H%M%S)`.
- Required GUI proof artifacts:
  - screenshot/recording showing real import of `/home/npittas/Videos/For_Test/Video_For_Test.mov` and selected footage layer;
  - screenshot/recording using at least one non-project-size source media item whose source dimensions differ from the project/format dimensions;
  - screenshot showing source viewer toolbar with point mode, box mode, remove selected prompt, and clear all prompts;
  - screenshot showing at least two points and at least two boxes visible simultaneously as persistent viewer indicators;
  - screenshot showing remove selected deletes only one selected prompt;
  - screenshot showing clear all removes all prompt indicators;
  - screenshot showing AI panel has no point/box/add/remove/clear controls and only summary/status/run/apply/log-type UI;
  - screenshot/recording showing prompt indicators remain registered under viewer zoom/pan; proxy/render-scale proof if available.
- Required source-coordinate proof:
  - log, debugger output, temporary diagnostic output, or equivalent recorded evidence for the non-project-size media showing each stored prompt contains exact source-frame pixel coordinates, source width/height, source identity, layer identity, and frame identity;
  - evidence that the same prompt positions are not project-size/canonical/widget coordinates when source and project sizes differ;
  - evidence that drawing is derived from the stored source-pixel prompts, not from canonical-only storage.
- GUI validation must use manual recorded interaction if KDE Wayland blocks synthetic cursor automation.

## Stop Conditions
Stop and report if:
- target symbol is missing
- required fix exceeds allowed files
- validation cannot run
- existing architecture contradicts the requested change
- task requires product/design judgment not in packet
- exact viewer-event-to-source-frame-pixel coordinate conversion cannot be proven within allowed files
- source width/height cannot be proven and stored within allowed files
- source identity, layer identity, or frame identity cannot be proven and stored within allowed files
- implementing source-frame coordinate conversion or identity capture requires editing outside the allowed files
- `Gui/Gui05.cpp` changes would exceed forwarding already-computed source context from the existing selected-source/SAM3 source handoff path to `ViewerGL`
- only canonical/project/widget/display coordinates are available
- backend, preview, apply, or persistence changes become necessary for more than compile-only adaptation
- point/box controls cannot be placed in the viewer toolbar
- persistent indicators cannot be drawn in the viewer
- multi-prompt behavior would remain single-prompt/replacement-based
- AI-panel point/box/add/remove/clear controls cannot be removed/demoted within the allowed files

## Required Return Contract
Return only a task-focused summary. Do not include transcript, tool logs, raw file dumps, large code blocks, or broad unrelated issues. Include status, files inspected/changed, validation evidence, blockers, and task-specific risks.
