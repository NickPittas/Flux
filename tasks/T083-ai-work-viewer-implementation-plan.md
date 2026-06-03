# Planner Report

## Status
ready

## Rationale
Locator evidence identifies the exact viewer lifecycle, current main-viewer coupling, source-viewer rewiring path, and AI Paint selection hooks. The implementation can be kept narrow by adding Flux-owned AI Work Viewer helpers in `Gui`, using them from AI Paint/source selection paths, and changing composition rebuild to resolve an explicit/non-AI main viewer instead of `_viewerTabs.front()`.

# Task Packet

## User Goal
Create/reuse a dedicated Flux-owned second AI Work Viewer node/tab for AI Paint/source inspection, leaving the main composition viewer untouched. Selecting an AI Paint timeline/effect row should create/recreate/reuse the AI Work Viewer, connect it to selected AI Paint so overlay tools work, show the viewer tab, and pass AI Paint/source context to `FluxAiPanel`. If the user closes/deletes that viewer, selecting AI Paint recreates it. Source/AI worker operations must not rewire the main comp viewer.

## Mode
general-coding

## Relevant Locations
- file: `Gui/Gui05.cpp`
  symbol: `Gui::setupFluxUi` AI panel setup
  approximate lines: 859-866
  stable anchor: `aiPanel->setScriptName("fluxAiPanel");`
  reason: currently seeds AI panel capture viewer from `_viewerTabs.front()`; must not bind AI tooling to implicit main viewer.
  confidence: high
- file: `Gui/Gui05.cpp`
  symbol: `FluxTimeline::sourceViewerRequested` lambda
  approximate lines: 1064-1139
  stable anchor: `QObject::connect(timeline, &FluxTimeline::sourceViewerRequested, this,`
  reason: rejected path rewires active/first viewer input 0 to `layer.readerNode`; redirect to dedicated AI Work Viewer only.
  confidence: high
- file: `Gui/Gui05.cpp`
  symbol: `FluxEffectsPanel::effectSelected` lambda
  approximate lines: 1173-1204
  stable anchor: `QObject::connect(effectsPanel, &FluxEffectsPanel::effectSelected, this,`
  reason: optional but in-scope selection path for AI Paint if effects panel remains active; should call shared AI Paint selection helper when plugin ID matches.
  confidence: high
- file: `Gui/Gui05.cpp`
  symbol: `FluxTimeline::effectSelected` lambda
  approximate lines: 1212-1250
  stable anchor: `QObject::connect(timeline, &FluxTimeline::effectSelected, this,`
  reason: primary AI Paint row selection hook; detect `PLUGINID_NATRON_AIPAINT` and arm/show AI Work Viewer while preserving existing settings-panel behavior.
  confidence: high
- file: `Gui/Gui05.cpp`
  symbol: `Gui::rebuildCompositingGraph`
  approximate lines: 1789-1795 and 2544-2551
  stable anchor: `// Find the first viewer` and `// Connect the final output to the viewer`
  reason: currently treats `_viewerTabs.front()` as main comp viewer and disconnects/connects it to `lastOutput`; must use explicit main comp viewer identity or helper excluding AI Work Viewer.
  confidence: high
- file: `Gui/Gui20.cpp`
  symbol: `Gui::addNewViewerTab`, `Gui::removeViewerTab`, `Gui::createNewViewer`
  approximate lines: 450-498, 589-693, 1360-1371
  stable anchor: `ViewerTab* Gui::addNewViewerTab(ViewerInstance* viewer, TabWidget* where)` / `Gui::createNewViewer()`
  reason: canonical viewer tab lifecycle and lower-level viewer node creation pattern; `createNewViewer()` returns void, so implement returning helper with `CreateNodeArgs` or post-create validation.
  confidence: high
- file: `Gui/GuiPrivate.h`
  symbol: `GuiPrivate::_viewerTabs`
  approximate lines: 155-158
  stable anchor: `std::list<ViewerTab*> _viewerTabs;`
  reason: add narrow state for explicit main viewer and/or cached AI Work Viewer node identity with stale-node guards.
  confidence: high
- file: `Gui/FluxAiPanel.cpp`
  symbol: `FluxAiPanel::setSourceCaptureContext`
  approximate lines: 165-169
  stable anchor: `void FluxAiPanel::setSourceCaptureContext(ViewerGL* viewer, const NodePtr& viewerNode,`
  reason: existing API already accepts viewer, viewer node, reader node, AI Paint node, layer/frame context; worker should use it rather than extend AI panel controls.
  confidence: high

## Allowed Edit Files
- `Gui/Gui05.cpp`
- `Gui/Gui20.cpp`
- `Gui/GuiPrivate.h`
- `Gui/Gui.h`

## Read-Only Context Files
- `Gui/FluxTimeline.h`
- `Gui/FluxTimeline.cpp`
- `Gui/FluxAiPanel.h`
- `Gui/FluxAiPanel.cpp`
- `Engine/EffectInstance.h`
- `tasks/T083-ai-matte-depth.md`

## Required Change
Implement a narrow Flux-owned AI Work Viewer architecture:

1. Add stable AI Work Viewer identity and helpers.
   - Use a stable Flux-owned label/script intent such as `Flux AI Work Viewer` for the viewer node/tab identity.
   - Add helper(s) on `Gui`/`GuiPrivate` to identify AI Work Viewer nodes/tabs by this stable label/script name and activation state.
   - Guard all cached `NodePtr`/`ViewerTab*` use with `node && node->isActivated()` and tab/internal-viewer validity.
   - Reuse an existing activated AI Work Viewer if present after save/reopen; recreate it if missing/deleted; avoid duplicates by choosing one deterministic activated instance and not creating another when one exists. If extras already exist, use the first deterministic match and do not wire extras.

2. Add/replace main composition viewer resolution.
   - Stop using `_viewerTabs.front()` as the implicit main comp viewer in `rebuildCompositingGraph()`.
   - Add either an explicit stored main comp viewer identity initialized from the existing first non-AI viewer, or a robust helper that returns the first activated non-AI viewer tab.
   - Ensure `rebuildCompositingGraph()` connects `lastOutput` only to that main comp viewer helper result and never to a tab/node identified as `Flux AI Work Viewer`.
   - Do not disconnect or connect AI Work Viewer during normal composition rebuild except when deliberately arming AI Paint/source inspection.

3. Create/reuse/show AI Work Viewer for AI Paint/source inspection.
   - Implement a helper that returns an armed AI Work Viewer tab/node, creating a viewer node via lower-level `CreateNodeArgs(PLUGINID_NATRON_VIEWER, graph->getGroup())` / `getApp()->createNode(args)` rather than relying on `Gui::createNewViewer()` returning a value.
   - Set Flux-managed creation flags consistently with existing Flux-managed nodes where available (`AutoConnect=false`, `AddUndoRedoCommand=false`, `SettingsOpened=false`) to avoid Natron side effects.
   - Set the AI Work Viewer node label/script identity to the stable value.
   - Connect AI Work Viewer input 0 to the selected AI Paint node when selecting an AI Paint effect row so overlay tools operate on the AI Paint node context.
   - For source inspection requests, connect AI Work Viewer input 0 to the selected layer reader/source node instead of active/main viewer.
   - Activate/show the AI Work Viewer tab (`setActiveViewer`, make parent pane current tab) after arming it.

4. Route AI Paint selection paths through the helper.
   - In the `FluxTimeline::effectSelected` lambda, after validating `layer.effects[effectIndex].node`, detect AI Paint by `node->getEffectInstance()->getPluginID() == PLUGINID_NATRON_AIPAINT`.
   - Preserve existing behavior that opens the effect settings panel.
   - When AI Paint is selected, arm/show AI Work Viewer and call `FluxAiPanel::setSourceCaptureContext()` with the AI Work Viewer `ViewerGL*`, AI Work Viewer node, layer index/name/file path, reader node/label, AI Paint node, current timeline frame, and computed source frame (`timelineFrame - layer.timeOffset`).
   - Apply the same helper from the `FluxEffectsPanel::effectSelected` path if that path is still reachable for timeline-owned effects.

5. Replace rejected source-viewer rewiring.
   - In the `sourceViewerRequested` lambda, remove active/first viewer rewiring.
   - Use the same AI Work Viewer helper, connect it only to `layer.readerNode`, show it, and pass the source/AI context to `FluxAiPanel`.
   - If no AI Paint effect exists on the selected layer, still pass source context with a null AI Paint node if current behavior expects source inspection without AI Paint; otherwise keep the existing no-op semantics and report narrowly in the return summary.

6. Initial AI panel viewer assignment.
   - Remove or neutralize AI panel initialization from `_viewerTabs.front()` at `Gui05.cpp:859-866` so AI capture source is set when AI Work Viewer is armed, not from the main viewer by default.

## Non-Goals
- Do not modify or hijack native RotoPaint behavior.
- Do not add point/box prompt capture controls to AI Panel.
- Do not change AI Paint viewer toolbar prompt ownership; it remains the prompt source of truth.
- Do not rewire the main composition viewer for source inspection or AI worker operations.
- Do not redesign panes/layout broadly.
- Do not edit provider runtime scripts or implement a new SAM/backend worker.
- Do not perform git operations.

## Validation
Commands:
- `cmake --build /home/npittas/Flux/build -j$(nproc)`

Manual proof required if build succeeds:
- Launch Flux with an existing or new project containing footage and an AI Paint effect.
- Select a normal layer/effect and confirm the main composition viewer still shows final comp output after `rebuildCompositingGraph()`.
- Select an AI Paint timeline effect row and capture screenshot/recording showing a separate tab/node labelled `Flux AI Work Viewer`, that tab active, AI Paint/source context visible in the AI panel, and main comp viewer still untouched.
- Close/delete the AI Work Viewer tab/node, select the AI Paint row again, and capture proof that the AI Work Viewer is recreated rather than the main viewer being rewired.
- Save/reopen a project with an AI Work Viewer present; select AI Paint and confirm the existing stable-identity viewer is reused and no duplicate is created.
- Trigger the existing source-viewer request path and confirm only the AI Work Viewer input changes to the source reader, not the main comp viewer.

Expected result:
- Build succeeds.
- Main composition viewer remains connected to final comp output and is never selected by AI Work Viewer/source actions.
- AI Paint/source inspection consistently uses a single Flux-owned `Flux AI Work Viewer`, recreating it if deleted and reusing it if present.

## Stop Conditions
Stop and report if:
- target symbol is missing
- required fix exceeds allowed files
- validation cannot run
- existing architecture contradicts the requested change
- task requires product/design judgment not in packet
- creating/reidentifying a viewer requires modifying serialization or Node internals outside the allowed edit files
- the only available implementation would disconnect/reconnect the main composition viewer for AI/source operations

## Required Return Contract
Return only a task-focused summary. Do not include transcript, tool logs, raw file dumps, large code blocks, or broad unrelated issues. Include status, files inspected/changed, validation evidence, blockers, and task-specific risks.
