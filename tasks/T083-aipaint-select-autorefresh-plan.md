# Planner Report

## Status
ready

## Rationale
The locator evidence is specific and sufficient for a single narrow worker task: AI Paint owns the viewer-toolbar capture modes and persisted prompt store, while FluxAiPanel already knows how to recompute prompt summary/run state from that store. The plan limits edits to the AI Paint node toolbar/mode handling, a small public panel refresh hook, and AI Work Viewer wiring for the existing prompt-store knob signal.

# Task Packet

## User Goal
Add a simple idle/select tool to the AI Paint viewer toolbar so users can exit AI Point/AI Box modes, and make the Flux AI Panel immediately refresh its prompt summary and Run enabled state when point/box prompts are added in the AI Work Viewer without closing/reopening the viewer.

## Mode
general-coding

## Relevant Locations
- file: `Engine/AIPaint.cpp`
  symbol: `#define kAIPaintParamPointTool`, `enum class AIPaintTool`, `AIPaintPrivate::activeTool`, `AIPaint::initializeKnobs`, `AIPaint::knobChanged`, `AIPaint::onOverlayPenDown`, `AIPaint::onOverlayPenMotion`, `AIPaint::onOverlayPenUp`
  approximate lines: 39-111, 197-260, 281-303, 397-462
  stable anchor: `pointTool->setDefaultValue(true)`, `_imp->context.addPoint`, `_imp->context.addBox`
  reason: AI Paint currently has only Point/Box tools, defaults Point on, and `activeTool()` falls back to Point unless Box is checked; overlay handlers persist prompt mutations through `aiPaintPromptStore`.
  confidence: high
- file: `Gui/FluxAiPanel.h`
  symbol: `FluxAiPanel`
  approximate lines: 28-75
  stable anchor: `setSourceCaptureContext(...)`, private `updatePromptSummary()`, private `updateUiState()`
  reason: Panel needs a small public refresh hook callable from AI Work Viewer wiring; the hook should not add prompt capture controls.
  confidence: high
- file: `Gui/FluxAiPanel.cpp`
  symbol: `FluxAiPanel::setSourceCaptureContext`, `FluxAiPanel::readAIPaintPrompts`, `FluxAiPanel::hasRunnableAIPaintPrompt`, `FluxAiPanel::updatePromptSummary`, `FluxAiPanel::updateUiState`
  approximate lines: 165-182, 370-530, 699-727
  stable anchor: `_sourceAIPaintNode = aiPaintNode`, `_runButton->setEnabled(...) && hasRunnableAIPaintPrompt()`, `Prompt summary: AI Paint enabled points`
  reason: Panel already recomputes state from AI Paint prompts but only refreshes during context setup/restore; add public refresh that calls summary and UI state together, and use it from context setup.
  confidence: high
- file: `Gui/Gui05.cpp`
  symbol: AI Work Viewer context wiring and existing knob signal wiring pattern
  approximate lines: 1198-1228, 1305-1308, 1377-1380, pattern near 169-175 and 2579-2585
  stable anchor: `aiPanel->setSourceCaptureContext(...)`, `KnobSignalSlotHandler::valueChanged`
  reason: Connect the selected AI Paint node's `aiPaintPromptStore` knob `valueChanged` signal to the panel refresh hook after AI Work Viewer context is set.
  confidence: high
- file: `Engine/Knob.h`
  symbol: `KnobSignalSlotHandler::valueChanged`
  approximate lines: 54-82, 271-283, 1133-1135
  stable anchor: `void valueChanged(ViewSpec view, int dimension, int reason);`
  reason: Confirms the existing signal signature for prompt-store live refresh wiring.
  confidence: high
- file: `Engine/AIPaintContext.h`, `Engine/AIPaintContext.cpp`
  symbol: `AIPaintContext::addPoint`, `AIPaintContext::addBox`, `serialize`, `deserialize`
  approximate lines: h 35-65; cpp 95-150, 150-220
  stable anchor: `int addPoint(...)`, `int addBox(...)`, `return QJsonDocument(root).toJson(...)`
  reason: Context remains the persistent/extensible prompt store; no direct context notification should be added for this task.
  confidence: high

## Allowed Edit Files
- `Engine/AIPaint.cpp`
- `Gui/FluxAiPanel.h`
- `Gui/FluxAiPanel.cpp`
- `Gui/Gui05.cpp`

## Read-Only Context Files
- `Engine/AIPaint.h`
- `Engine/AIPaintContext.h`
- `Engine/AIPaintContext.cpp`
- `Engine/Knob.h`
- `Gui/Gui.h`
- `tasks/T083-ai-matte-depth.md`

## Required Change
1. In `Engine/AIPaint.cpp`, add a third checkable viewer-toolbar tool for idle/select mode:
   - Add stable knob names for select group/button near existing `kAIPaintParamPointToolGroup`, `kAIPaintParamBoxToolGroup`, `kAIPaintParamPointTool`, `kAIPaintParamBoxTool`.
   - Extend `enum class AIPaintTool` with `Select`.
   - Add `KnobButtonWPtr selectTool` to `AIPaintPrivate`.
   - Change `activeTool()` so checked Point returns `Point`, checked Box returns `Box`, otherwise returns `Select`; do not fall back to Point when no paint tool is active.
   - In `initializeKnobs()`, create a toolbar group/button labeled like `Select` or `AI Select`, checkable, non-persistent, viewer-context toolbar compatible, default checked `true`; change Point default to `false`, keep Box default `false`. Use an existing neutral/select icon only if one is already available through surrounding patterns; otherwise a text/iconless tool is acceptable for this narrow fix.
   - Add the select button as an overlay slave param alongside point/box.
   - Update exclusivity handling so Select, Point, and Box are mutually exclusive. If the existing two-button `makeExclusive` helper becomes awkward, replace it with a narrow helper that unchecks all other non-null buttons when a checked button changes. Avoid creating a state where clicking an active paint mode automatically reverts to Point; no checked paint mode should mean idle/select.
   - Update overlay pen handlers so `Select` consumes no prompt input: `onOverlayPenDown` must return `false` and not call `addPoint`, start box dragging, or write the prompt store when active tool is Select. Ensure any in-progress box drag is not left active if switching tools.
2. Preserve prompt persistence/extensibility:
   - Keep point/box prompt creation through `_imp->context.addPoint` / `_imp->context.addBox`.
   - Keep writing serialized prompts through the existing `aiPaintPromptStore` knob after a point is added or a valid box is completed.
   - Do not add prompt notifications to `AIPaintContext`; use the store knob as the change signal source.
3. In `Gui/FluxAiPanel.h/.cpp`, add a public method such as `void refreshAIPaintPromptState();` that calls `updatePromptSummary();` and `updateUiState();`.
   - Replace or augment the final `updatePromptSummary()` in `setSourceCaptureContext(...)` with this public refresh method so the Run enabled state updates immediately on context setup too.
   - Do not add point/box controls or capture ownership to the panel.
4. In `Gui/Gui05.cpp`, wire AI Work Viewer prompt-store changes to the panel refresh hook:
   - At each AI Work Viewer path that calls `aiPanel->setSourceCaptureContext(..., aiPaintNode/node, ...)`, connect the selected AI Paint node's `aiPaintPromptStore` knob signal handler to `FluxAiPanel::refreshAIPaintPromptState` using the existing `KnobSignalSlotHandler::valueChanged` pattern.
   - Disconnect any previous identical prompt-store connection before connecting, matching the existing disconnect/connect style near the `KnobSignalSlotHandler::valueChanged` examples, to avoid duplicate refresh calls when the same context is selected repeatedly.
   - The connection should be scoped to the panel receiver and should refresh only when the current selected `_sourceAIPaintNode` is the node whose prompt store changed, either by relying on context replacement plus receiver disconnect or by checking node identity before calling the hook.
   - If local API for finding the `aiPaintPromptStore` knob is not already obvious from nearby code, inspect only within the allowed edit/read files for existing knob access patterns; if still not possible, stop and report the missing API rather than guessing.

## Non-Goals
- Do not modify or hijack native RotoPaint.
- Do not move point/box capture controls into `FluxAiPanel`.
- Do not change SAM/backend/provider behavior.
- Do not change AI prompt serialization schema except as naturally produced by existing point/box store writes.
- Do not redesign viewer overlays or add remove/clear/selection prompt management in this task.
- Do not edit `AIPaintContext` unless a compile error proves an include/signature adjustment is required; context-level notifications are out of scope.

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

Expected result:
- Build succeeds.
- AI Paint defaults to idle/select rather than forced point capture.
- Point/box tools remain mutually exclusive with select.
- `aiPaintPromptStore` remains the persisted prompt source and emits the refresh path used by the AI Panel.
- Flux AI Panel summary and Run enabled state update immediately after prompt additions in the AI Work Viewer.

## Stop Conditions
Stop and report if:
- target symbol is missing
- required fix exceeds allowed files
- validation cannot run
- existing architecture contradicts the requested change
- task requires product/design judgment not in packet
- no safe local API can be found to access the AI Paint prompt-store knob from `Gui05.cpp`
- connecting prompt-store changes would require AI Panel to own capture controls or backend/SAM changes
- implementing select mode would require modifying native RotoPaint or shared host overlay behavior

## Required Return Contract
Return only a task-focused summary. Do not include transcript, tool logs, raw file dumps, large code blocks, or broad unrelated issues. Include status, files inspected/changed, validation evidence, blockers, and task-specific risks.
