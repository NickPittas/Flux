# Planner Report

## Status
ready

## Rationale
This revision keeps the implementation unit small and directly addresses the second reviewer blocker by making AI Panel auto-refresh signal-only: the worker must prove `aiPaintPromptStore` emits `KnobSignalSlotHandler::valueChanged` for real point and box prompt writes, and must stop/report if that proof fails. The select/idle tool and refresh wiring are limited to the existing AI Paint, AI Panel, and AI Work Viewer integration files; no fallback, backend, RotoPaint, or capture-ownership changes are allowed.

# Task Packet

## User Goal
Add a simple idle/select tool to the AI Paint viewer toolbar so users can exit AI Point/AI Box paint modes, and make the Flux AI Panel auto-refresh immediately after AI Paint point/box prompt edits only if the required prompt-store knob-signal proof succeeds.

## Mode
general-coding

## Relevant Locations
- file: `Engine/AIPaint.cpp`
  symbol: `kAIPaintParamPointToolGroup`, `kAIPaintParamBoxToolGroup`, `kAIPaintParamPointTool`, `kAIPaintParamBoxTool`, `kAIPaintParamPromptStore`, `enum class AIPaintTool`, `makeExclusive`, `AIPaintPrivate::activeTool`, `AIPaint::initializeKnobs`, `AIPaint::knobChanged`, overlay pen handlers
  approximate lines: 42-115, 150-235, 397-462
  stable anchor: `pointTool->setDefaultValue(true)`, `makeExclusive(point, box)`, `_imp->context.addPoint`, `_imp->context.addBox`, `store->setValue(_imp->context.serialize(), ViewSpec::all(), 0, true)`
  reason: AI Paint currently has Point/Box tools only, defaults Point on, falls back to Point when Box is not checked, and persists prompt mutations through `aiPaintPromptStore` after point/box writes.
  confidence: high
- file: `Gui/FluxAiPanel.h`
  symbol: `FluxAiPanel`
  approximate lines: 25-75
  stable anchor: `setSourceCaptureContext(...)`, private `updatePromptSummary()`, private `updateUiState()`
  reason: Panel needs one public refresh hook callable from AI Work Viewer wiring; the hook must not add point/box capture ownership.
  confidence: high
- file: `Gui/FluxAiPanel.cpp`
  symbol: `FluxAiPanel::setSourceCaptureContext`, `FluxAiPanel::readAIPaintPrompts`, `FluxAiPanel::hasRunnableAIPaintPrompt`, `FluxAiPanel::updatePromptSummary`, `FluxAiPanel::updateUiState`
  approximate lines: 165-200, 370-530, 699-727
  stable anchor: `_sourceAIPaintNode = aiPaintNode`, `_runButton->setEnabled(... && hasRunnableAIPaintPrompt())`, `Prompt summary: AI Paint enabled points`
  reason: Panel already recomputes summary/run state from the current AI Paint prompt state; expose a public method that calls existing recomputation paths.
  confidence: high
- file: `Gui/Gui05.cpp`
  symbol: `installFluxTextFontSync`, source-viewer request lambda, `FluxEffectsPanel::effectSelected` AI Paint branch, `FluxTimeline::effectSelected` AI Paint branch
  approximate lines: 150-175, 1198-1228, 1305-1308, 1377-1380
  stable anchor: `QObject::disconnect(handler, &KnobSignalSlotHandler::valueChanged, receiver, nullptr)`, `aiPanel->setSourceCaptureContext(...)`
  reason: Existing local pattern shows valueChanged disconnect/connect style, and the three AI Work Viewer paths already set the panel source context for the current AI Paint node.
  confidence: high

## Allowed Edit Files
- `Engine/AIPaint.cpp`
- `Gui/FluxAiPanel.h`
- `Gui/FluxAiPanel.cpp`
- `Gui/Gui05.cpp`

## Read-Only Context Files
- `tasks/T083-aipaint-select-autorefresh-plan-v2.md`

## Required Change
1. In `Engine/AIPaint.cpp`, add a third checkable viewer-toolbar tool for idle/select mode:
   - Add stable knob names for select group/button near existing AI Paint tool constants, for example `aiPaintSelectToolGroup` and `aiPaintSelectTool`.
   - Extend `enum class AIPaintTool` with `Select`.
   - Add `KnobButtonWPtr selectTool` to `AIPaintPrivate` and initialize it.
   - Change `activeTool()` so checked Point returns `Point`, checked Box returns `Box`, otherwise returns `Select`; do not fall back to Point when no paint tool is active.
   - In `initializeKnobs()`, create a toolbar group/button labeled `Select` or `AI Select`, checkable, non-persistent, viewer-context toolbar compatible, default checked `true`; change Point default to `false`, keep Box default `false`. Use an existing neutral/select icon only if one is obvious in the surrounding code/assets; otherwise text/iconless is acceptable.
   - Add the select button as an overlay slave param with point/box.
   - Update mutual-exclusivity handling so Select, Point, and Box are exclusive. Prefer a narrow helper that unchecks all other non-null buttons only when the changed button is checked. If a paint tool is unchecked and no other tool is checked, leave behavior idle/select; do not force Point.
   - When switching tools, cancel any in-progress box drag so stale drag state cannot complete a box after selecting idle or point.
   - Update overlay pen handlers so `Select` consumes no prompt input: `onOverlayPenDown` must return `false` and must not call `addPoint`, start box dragging, or write the prompt store when active tool is Select.
2. Preserve prompt persistence:
   - Keep point/box prompt creation through `_imp->context.addPoint` / `_imp->context.addBox`.
   - Keep writing serialized prompts through the existing `aiPaintPromptStore` knob after a point is added or a valid box is completed.
   - Do not add notifications/signals to `AIPaintContext`.
3. In `Gui/FluxAiPanel.h/.cpp`, add a public method such as `void refreshAIPaintPromptState();` that calls `updatePromptSummary();` and `updateUiState();`.
   - Use this public refresh method at the end of `setSourceCaptureContext(...)` so context setup and signal refresh use the same recomputation path.
   - Do not add point/box controls or capture ownership to the panel.
4. In `Gui/Gui05.cpp`, add signal-only AI Work Viewer prompt-store wiring:
   - After each AI Work Viewer path that calls `aiPanel->setSourceCaptureContext(..., aiPaintNode/node, ...)`, obtain the same AI Paint node's prompt-store knob with `node->getKnobByName("aiPaintPromptStore")` and its `KnobSignalSlotHandler`.
   - Before connecting, disconnect previous prompt-store `valueChanged` connections targeting `aiPanel`, matching the existing `QObject::disconnect(handler, &KnobSignalSlotHandler::valueChanged, receiver, nullptr)` style. Avoid duplicate refresh calls when the same context is selected repeatedly.
   - Connect `KnobSignalSlotHandler::valueChanged(ViewSpec,int,int)` to a lambda whose receiver is `aiPanel` and whose body calls `aiPanel->refreshAIPaintPromptState()` for the selected AI Paint source. A narrow identity guard is preferred if an existing safe way to compare with the panel's current source exists; otherwise rely on disconnecting all prior prompt-store connections immediately before each new connection and only connect after `setSourceCaptureContext` succeeds. Do not add broad panel state exposure solely for this task.
   - Apply the wiring to the source-viewer request path and both existing AI Paint effect-selection paths in `Gui05.cpp` where `setSourceCaptureContext` is called.
5. Mandatory reviewer-blocker requirement: signal proof only, no fallback.
   - During validation, prove that plugin-originated prompt-store writes from `store->setValue(_imp->context.serialize(), ViewSpec::all(), 0, true)` emit `KnobSignalSlotHandler::valueChanged` and invoke `FluxAiPanel::refreshAIPaintPromptState()` for both one point write and one box write.
   - Temporary instrumentation is allowed while developing, but any temporary debug counters/logs must be removed before final code unless they match an existing project-acceptable logging style and are intentionally kept.
   - Acceptable proof in the worker return: exact observed code path plus log/screenshot/recording/debugger/trace evidence showing one point write and one box write each reach the refresh hook via the knob signal.
   - If the signal does not emit for plugin-originated prompt-store writes, stop and report. Do not implement a one-shot refresh fallback, polling fallback, timer fallback, edit-time refresh hook, `AIPaintContext` notification, RotoPaint change, SAM/backend/provider change, or panel-owned capture workaround under this packet.

## Non-Goals
- Do not modify or hijack native RotoPaint.
- Do not move point/box capture controls into `FluxAiPanel`.
- Do not add `AIPaintContext` notifications/signals.
- Do not change SAM/backend/provider behavior.
- Do not add one-shot connection refresh, polling, timer, or deterministic edit-time fallback if the knob signal does not emit.
- Do not change AI prompt serialization schema except as naturally produced by existing point/box store writes.
- Do not redesign viewer overlays or add remove/clear/selection prompt management.
- Do not edit files outside the Allowed Edit Files list.

## Validation
Commands:
- `cmake --build /home/npittas/Flux/build --target Natron -j$(nproc)`
- If the configured target name differs locally, run the repository's established Flux/Natron GUI build target and report the exact command used.

Manual proof:
- Launch Flux GUI, open/create a footage layer with AI Paint in the AI Work Viewer.
- Screenshot or short recording showing the AI Paint toolbar has Select/idle plus AI Point and AI Box.
- With Select active, click/drag in the viewer: no point/box prompt appears and the prompt summary/Run state does not change because of the click.
- Switch to AI Point and click in the viewer: persistent point indicator appears; Flux AI Panel prompt summary updates immediately and Run becomes enabled when SAM3 model/source preconditions are otherwise satisfied, without closing/reopening the viewer.
- Switch to AI Box and drag a valid rectangle: persistent box indicator appears; prompt summary updates immediately without reopening.
- Switch back to Select: further clicks do not add prompts.
- Mandatory signal proof: demonstrate that one point write and one box write each invoke `FluxAiPanel::refreshAIPaintPromptState()` through `aiPaintPromptStore`'s `KnobSignalSlotHandler::valueChanged`.

Expected result:
- Build succeeds.
- AI Paint defaults to idle/select rather than forced point capture.
- Point/box/select tools are mutually exclusive enough that only active Point adds points, only active Box drags boxes, and Select/none is idle.
- `aiPaintPromptStore` remains the persisted prompt source.
- Flux AI Panel summary and Run enabled state update immediately after prompt additions in the AI Work Viewer, with explicit proof that the knob signal path works for both point and box writes.

## Stop Conditions
Stop and report if:
- target symbol is missing
- required fix exceeds allowed files
- validation cannot run
- existing architecture contradicts the requested change
- task requires product/design judgment not in packet
- no safe local API can be found to access the AI Paint prompt-store knob from `Gui05.cpp`
- connecting prompt-store changes would require AI Panel to own capture controls, native RotoPaint changes, AIPaintContext notifications, backend/SAM/provider changes, or files outside Allowed Edit Files
- plugin-originated prompt-store `setValue` does not emit `KnobSignalSlotHandler::valueChanged` for either the point write or the box write

## Required Return Contract
Return only a task-focused summary. Do not include transcript, tool logs, raw file dumps, large code blocks, or broad unrelated issues. Include status, files inspected/changed, validation evidence, explicit point/box signal proof, blockers, and task-specific risks.
