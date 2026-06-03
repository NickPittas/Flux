# Planner Report

## Status
ready

## Rationale
This revision addresses the reviewer blockers by narrowing T083 to Nick's source of truth: SAM point/box prompts are RotoPaint viewer tools in the existing left vertical toolbar, stored in RotoPaint coordinates, and drawn by the normal RotoPaint overlay path. The implementation must not use the prior top-bar/custom `ViewerGL` prompt capture/draw path, and the editable file set is limited to the RotoPaint tool path plus the already-located `ViewerTab`/`ViewerGL` deactivation points.

# Task Packet

## User Goal
Create SAM viewer prompt tools that appear beside the existing RotoBrush/RotoPaint tools in the left vertical viewer toolbar. The SAM point tool creates exactly one stored RotoPaint stroke point at click position. The SAM box tool creates one RotoPaint rectangle selection area. No backend, preview, apply, or persistence work is included.

## Mode
general-coding

## Relevant Locations
- file: `Engine/RotoPaint.cpp`
  symbol: `RotoPaint::initializeKnobs`
  approximate lines: 622-740, 867-880, 1111
  stable anchor: `KnobPagePtr toolbar = AppManager::createKnob<KnobPage>( this, std::string(kRotoUIParamToolbar) );`, `_imp->ui->drawRectangleAction = tool;`, `_imp->ui->brushAction = tool;`
  reason: Existing RotoPaint toolbar page/action setup consumed by the left vertical viewer toolbar; add SAM point/box actions here beside brush/rectangle.
  confidence: high
- file: `Engine/RotoPaintInteract.cpp`
  symbol: `RotoPaintInteract::getToolForAction`, role/tool change handling
  approximate lines: 597-786
  stable anchor: `drawRectangleAction.lock()`, `brushAction.lock()`, `sizeSpinbox.lock()->setInViewerContextSecret(...)`
  reason: Map new SAM actions to explicit SAM tool state and ensure brush/display size handling is defined.
  confidence: high
- file: `Engine/RotoPaintInteract.h`
  symbol: `RotoToolEnum`, `RotoPaintInteract` action/state members
  approximate lines: around existing Roto tool enum/state and 545-576
  stable anchor: `KnobButtonWPtr drawRectangleAction;`, `KnobButtonWPtr brushAction;`
  reason: Add minimal SAM-specific tool enum/state: `eRotoToolSamPoint` and `eRotoToolSamBox`, plus action weak pointers. Do not map SAM actions directly to ordinary `eRotoToolBrush`/`eRotoToolDrawRectangle` because point click semantics and visual identity must differ from normal tools.
  confidence: high
- file: `Engine/RotoPaint.cpp`
  symbol: Roto overlay pen-down/move/up creation paths
  approximate lines: 2705-2765 and nearby mouse-move/finalize handlers
  stable anchor: `case eRotoToolDrawRectangle`, `strokeBeingPaint->appendPoint( true, RotoPoint(pos.x(), pos.y(), pressure, timestamp) )`, `_imp->ui->makeStroke( false, RotoPoint(pos.x(), pos.y(), pressure, timestamp) )`
  reason: SAM box must delegate to the existing rectangle storage path; SAM point must create one and only one `RotoPoint(pos.x(), pos.y(), pressure, timestamp)` and then finish without drag-stroke accumulation.
  confidence: high
- file: `Engine/RotoPaint.cpp`
  symbol: `RotoPaint::drawOverlay`
  approximate lines: 1774-2211
  stable anchor: `getNode()->getRotoContext()->getCurvesByRenderOrder()`
  reason: SAM prompts must be visible through the RotoPaint overlay/drawable path, not custom `ViewerGL` indicators.
  confidence: high
- file: `Engine/RotoContext.cpp`
  symbol: `RotoContext::makeBezier`, `RotoContext::makeSquare`
  approximate lines: 474-596
  stable anchor: `curve->addControlPoint(x, y, time)`, `RotoContext::makeSquare`
  reason: Read-only confirmation of existing coordinate storage for curve points and rectangles.
  confidence: high
- file: `Gui/NodeViewerContext.cpp`
  symbol: `NodeViewerContext::createGui`, `NodeViewerContextPrivate::addToolBarTool`
  approximate lines: 113-220, 365-433
  stable anchor: `toolbar->setOrientation(Qt::Vertical)`, `toolbar->addWidget(toolButton)`
  reason: Preserve this left vertical toolbar path exactly; do not add a new toolbar or top-bar path.
  confidence: high
- file: `Gui/ViewerTab20.cpp`
  symbol: `ViewerTab::drawOverlays`, `ViewerTab::notifyOverlaysPenDown_internal`
  approximate lines: 76-272
  stable anchor: `effect->drawOverlay_public(...)`, `effect->onOverlayPenDown_public(...)`
  reason: Read-only confirmation that viewer overlay draw/input dispatch reaches RotoPaint.
  confidence: high
- file: `Gui/ViewerTab.cpp`
  symbol: existing top-bar SAM prompt controls
  approximate lines: 251, 1150-1165
  stable anchor: `fluxAiPromptPointButton`, `viewer->setAiViewerPromptToolMode(...)`
  reason: Deactivate/remove only the conflicting active top-bar/custom prompt UX already present here.
  confidence: high
- file: `Gui/ViewerGL.cpp`
  symbol: existing custom SAM prompt mode/draw hooks
  approximate lines: 525, 648-657, 859, 2929-2941
  stable anchor: `drawAiViewerPromptIndicators()`, `ViewerGL::setAiViewerPromptToolMode`, `eAiViewerPromptToolPoint`, `eAiViewerPromptToolBox`
  reason: Explicitly forbid relying on this capture/draw path; remove/deactivate calls that make it active for this slice.
  confidence: high
- file: `Gui/ViewerGL.h`
  symbol: `AiViewerPromptToolMode` declarations
  approximate lines: 458-472, 628, 710
  stable anchor: `enum AiViewerPromptToolMode`, `drawAiViewerPromptIndicators`, `_aiViewerPromptToolMode`
  reason: Remove/deactivate declarations/state only as needed to ensure the custom path is not active.
  confidence: high

## Allowed Edit Files
- `Engine/RotoPaint.cpp`
- `Engine/RotoPaintInteract.cpp`
- `Engine/RotoPaintInteract.h`
- `Gui/ViewerTab.cpp`
- `Gui/ViewerGL.cpp`
- `Gui/ViewerGL.h`

## Read-Only Context Files
- `Engine/RotoContext.cpp`
- `Engine/RotoContext.h`
- `Engine/BezierCP.cpp`
- `Engine/RotoDrawableItem.cpp`
- `Engine/RotoDrawableItem.h`
- `Gui/ViewerTab20.cpp`
- `Gui/ViewerTab30.cpp`
- `Gui/ViewerTab40.cpp`
- `Gui/NodeViewerContext.cpp`
- `tasks/T083-ai-matte-depth.md`
- `tasks/T083-sam3-complete-workflow-plan.md`
- `tasks/T083-sam3-authoritative-drift-audit.md`
- `tasks/T083-sam3-viewer-toolbar-multiprompt-recovery-plan.md`

## Required Change
1. Add minimal SAM-specific Roto tool state in `Engine/RotoPaintInteract.h`: `eRotoToolSamPoint` and `eRotoToolSamBox`, with corresponding SAM point/box toolbar action weak pointers. These actions must be distinct from ordinary brush/rectangle actions so labels, behavior, and visual identity do not collapse into normal RotoBrush/rectangle items.
2. In `RotoPaint::initializeKnobs`, add SAM point and SAM box actions to the existing RotoPaint toolbar page near the existing brush/rectangle actions. This must preserve the left vertical viewer toolbar path: `RotoPaint::initializeKnobs` toolbar page -> `NodeViewerContext::createGui` -> `NodeViewerContextPrivate::addToolBarTool` -> `toolbar->addWidget(toolButton)`. Do not create a top viewer bar, custom toolbar, or alternate viewer button path.
3. SAM point behavior: on click/pen-down, create/finalize exactly one RotoPaint stroke containing one stored `RotoPoint(pos.x(), pos.y(), pressure, timestamp)`. Use pressure `1.0` if the event has no pressure value. Define SAM point brush/display size as a fixed 16-pixel diameter prompt marker for this slice, implemented through the existing RotoPaint stroke/brush-size mechanism where available; if the existing stroke stores radius/brush size separately, set the created SAM point stroke to that value. Do not append additional points during drag/move, do not accumulate a normal brush stroke, and do not keep painting after the initial click.
4. SAM box behavior: route through the existing RotoPaint rectangle path (`RotoContext::makeSquare` / current rectangle finalization path) while using the new `eRotoToolSamBox` state/action. The stored geometry must be normal RotoPaint rectangle/control-point coordinates.
5. Prompt identity/visual distinction: created SAM drawables must be minimally distinguishable from ordinary RotoBrush/rectangle items without adding a backend schema. Assign explicit labels/names such as `SAM Point 1`, `SAM Box 1` (or stable equivalent names matching local naming conventions) and make their overlay appearance visibly distinct using existing Roto drawable properties if available, e.g. SAM point in cyan with fixed 16px marker/brush size and SAM box in cyan/blue rectangle styling. Do not introduce a prompt JSON/backend model in this task.
6. Deactivate the existing top-bar/custom prompt UX found in `Gui/ViewerTab.cpp` and `Gui/ViewerGL.*` only to the extent needed to prevent conflict. The implementation must not rely on `ViewerGL::drawAiViewerPromptIndicators`, `AiViewerPromptToolMode`, or `ViewerGL::setAiViewerPromptToolMode` for SAM prompt capture or drawing. It is acceptable to leave dead declarations only if they are not called, not visible, and not active; otherwise remove them within the allowed files.
7. Do not edit `Gui/FluxAiPanel.*` or `Gui/Gui05.cpp` for this task. If the worker finds active conflicting SAM top-bar/custom prompt UX in those files, stop and report with exact anchors rather than expanding the edit set.
8. Do not implement backend prompt extraction yet. Later work may consume SAM prompt drawables from RotoContext/curve/stroke coordinates after this slice proves visible/stored prompts.

## Non-Goals
- No SAM backend execution, model invocation, or prompt JSON contract changes.
- No generated mask preview overlay.
- No Apply-to-mask/nodegraph integration.
- No project save/reopen persistence.
- No source-frame pixel extraction or non-project-size conversion work beyond RotoPaint's existing coordinate storage.
- No `ViewerGL::drawAiViewerPromptIndicators` / `AiViewerPromptToolMode` capture or draw basis.
- No top viewer bar point/box prompt UX.
- No edits to `Gui/FluxAiPanel.*` or `Gui/Gui05.cpp` unless a new reviewed plan explicitly authorizes anchored changes.
- No broad RotoPaint refactor or generic Roto tool redesign.

## Validation
Commands:
- `cmake --build <build-dir> --target Natron --parallel` using the active local build directory.
- Run the built Flux/Natron GUI.

Expected result:
- Build completes with no errors.
- With a RotoPaint/RotoBrush-capable node active, the left vertical viewer toolbar shows distinct SAM point and SAM box tools in the same toolbar area as existing RotoBrush/RotoPaint tools.
- Selecting SAM point and clicking in the viewer creates one visible, distinguishable SAM point prompt through RotoPaint overlay drawing, backed by exactly one stored `RotoPoint(pos.x(), pos.y(), pressure, timestamp)` and no drag-stroke accumulation.
- Selecting SAM box and dragging in the viewer creates one visible, distinguishable SAM rectangle prompt through the existing Roto rectangle storage/draw path.
- Existing RotoBrush/RotoPaint tools still activate and draw as before.
- Top-bar/custom `ViewerGL` SAM prompt controls/indicators are not visible/active and are not used for prompt capture/draw.
- GUI proof must include screenshots/recording showing the left toolbar SAM tools and resulting visible point and rectangle prompts.

## Stop Conditions
Stop and report if:
- target symbol is missing
- required fix exceeds allowed files
- validation cannot run
- existing architecture contradicts the requested change
- task requires product/design judgment not in packet
- adding SAM tools to the left viewer toolbar requires bypassing `RotoPaint::initializeKnobs`, `NodeViewerContext`, or `NodeViewerContextPrivate::addToolBarTool`
- point or rectangle creation cannot be routed through RotoPaint/RotoContext coordinate storage
- SAM point cannot be made exactly one stored `RotoPoint(pos.x(), pos.y(), pressure, timestamp)` without normal brush drag accumulation
- deactivating top-bar/custom `ViewerGL` prompt UX requires edits to `Gui/FluxAiPanel.*`, `Gui/Gui05.cpp`, or unrelated AI backend/preview/apply code
- a backend prompt extraction contract is needed before point/box visibility and storage can be completed

## Required Return Contract
Return only a task-focused summary. Do not include transcript, tool logs, raw file dumps, large code blocks, or broad unrelated issues. Include status, files inspected/changed, validation evidence, blockers, and task-specific risks.
