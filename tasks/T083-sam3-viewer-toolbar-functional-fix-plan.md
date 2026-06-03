# Planner Report

## Status
ready

## Rationale
The locator evidence and inspected code show a narrow functional failure: viewer-toolbar buttons already exist and `ViewerGL` already stores/draws multi-prompts, but source context is only initialized inside an AI-panel-gated branch and click conversion rejects prompts when the viewer canonical format does not exactly match exported source dimensions. This packet limits implementation to making the approved toolbar workflow functional without moving prompt ownership to the AI panel or touching deferred backend/preview/apply/persistence work.

# Task Packet

## User Goal
Make T083 viewer-toolbar prompt buttons functional in Nick's workflow: open Natron/Flux, import footage, put image/footage in the timeline, right-click the layer to open the source viewer, then use viewer toolbar `Pt`/`Box`/`Rm`/`Clr`. The buttons must not rely on AI-panel prompt controls.

## Mode
general-coding

## Relevant Locations
- file: `Gui/ViewerTab.cpp`
  symbol: `ViewerTab::onAIPromptPointButtonClicked`, `ViewerTab::onAIPromptBoxButtonClicked`, `ViewerTab::onAIPromptRemoveButtonClicked`, `ViewerTab::onAIPromptClearButtonClicked`
  approximate lines: 250-275, 1140-1180
  stable anchor: `fluxAiPromptPointButton`
  reason: viewer-toolbar controls already toggle point/box mode and call remove/clear; inspect only if button mode synchronization or visible diagnostics are needed.
  confidence: high
- file: `Gui/ViewerGL.cpp`
  symbol: `ViewerGL::setAiViewerPromptSourceContext`, `ViewerGL::canonicalToAiViewerSourcePixel`, `ViewerGL::appendAiViewerPointPrompt`, `ViewerGL::appendAiViewerBoxPrompt`, `ViewerGL::mousePressEvent`, `ViewerGL::mouseReleaseEvent`, `ViewerGL::drawAiViewerPromptIndicators`
  approximate lines: 602-918, 2178-2406
  stable anchor: `canonicalToAiViewerSourcePixel`
  reason: actual prompt capture/storage/drawing path; current exact canonical-size equality check can reject clicks silently even when exported source context exists.
  confidence: high
- file: `Gui/Gui05.cpp`
  symbol: `FluxTimeline::sourceViewerRequested` lambda in `Gui::setupFluxUi`
  approximate lines: 1064-1146
  stable anchor: `source viewer blocked: exact exported source dimensions unavailable`
  reason: only verified source-context setup path; currently wrapped in `if (aiPanel)` so the viewer prompt context depends on AI-panel plumbing.
  confidence: high
- file: `Gui/FluxTimeline.cpp`
  symbol: timeline context action emitting source viewer request
  approximate lines: 3816-3824
  stable anchor: `Q_EMIT sourceViewerRequested(layerIdx)`
  reason: confirms Nick's right-click source-viewer workflow entry; no edit expected.
  confidence: high

## Allowed Edit Files
- `Gui/Gui05.cpp`
- `Gui/ViewerGL.cpp`
- `Gui/ViewerTab.cpp`

## Read-Only Context Files
- `Gui/ViewerTab.h`
- `Gui/ViewerGL.h`
- `Gui/FluxTimeline.cpp`
- `Gui/FluxTimeline.h`
- `Gui/FluxAiPanel.cpp`
- `Gui/FluxAiPanel.h`
- `tasks/T083-sam3-viewer-toolbar-multiprompt-recovery-plan.md`
- `tasks/T083-sam3-authoritative-drift-audit.md`
- `tasks/T083-sam3-complete-workflow-plan.md`

## Required Change
1. In `Gui/Gui05.cpp`, make viewer source-context setup independent of AI-panel availability:
   - After `sourceViewerArmed` succeeds for the selected footage layer, export the exact source frame and read `exported_png_width` / `exported_png_height` as now.
   - Call `viewerTab->getViewer()->setAiViewerPromptSourceContext(...)` whenever the viewer is armed and exported dimensions are valid, regardless of whether `aiPanel` exists.
   - If export/dimensions fail, clear the viewer prompt source context as the existing failure path does.
   - Keep `aiPanel->setSourceCaptureContext(...)`, pane focusing, and summary behavior only inside an `if (aiPanel)` block; do not make toolbar prompt capture depend on this panel.
2. In `Gui/ViewerGL.cpp`, make `canonicalToAiViewerSourcePixel()` convert from the displayed source frame's canonical rectangle into the exported source-frame pixel dimensions instead of requiring `canonicalWidth == _aiViewerSourceWidth` and `canonicalHeight == _aiViewerSourceHeight`.
   - Keep the exported source width/height, layer index, layer/file/reader identity, timeline frame, and source frame as the authoritative prompt state.
   - Reject only missing/invalid source context, invalid canonical extents, or clicks outside the displayed canonical frame.
   - Compute zero-based source pixels by scaling the canonical point relative to `getCanonicalFormat(0)` into `_aiViewerSourceWidth`/`_aiViewerSourceHeight`, with top-left origin and y-down semantics, then clamp to `[0,width-1]` / `[0,height-1]`.
   - Do not store canonical/project/widget/display coordinates as authoritative prompt data.
3. Preserve existing viewer-owned multi-prompt behavior:
   - `Pt` appends points.
   - `Box` appends boxes on drag release.
   - `Rm` removes only the selected prompt.
   - `Clr` clears all prompts for the current source context.
   - Persistent indicators continue to draw all points/boxes via `drawAiViewerPromptIndicators()`.
4. If button behavior still appears non-functional after source-context and conversion fixes, inspect only the allowed `ViewerTab.cpp` handlers for mode synchronization or missed redraw; do not introduce AI-panel point/box controls.

## Non-Goals
- Do not implement SAM3 backend execution, preview mask overlay loading, Apply-to-mask/nodegraph binding, or save/reopen persistence.
- Do not add AI-panel point/box add/remove/clear controls.
- Do not route point/box prompt capture through `FluxAiPanel`.
- Do not change timeline/source-viewer UX wording or add alternate prompt UX.
- Do not change the prompt contract away from exported source-frame pixel coordinates.
- Do not edit task/status docs in this implementation packet.

## Validation
Commands:
- Build the existing Flux/Natron GUI target used in this workspace, for example the established `cmake --build <build-dir> --target NatronGui` or equivalent local GUI build command.
- `grep -R "SAM3 Dev\|SAM3 A0\|A1/A2/A3" Gui/FluxTimeline.cpp Gui/FluxAiPanel.cpp Gui/Gui05.cpp || true`

Expected result:
- Build succeeds.
- No reintroduced dev/test source-viewer labels in edited user-facing paths.
- GUI proof by screenshot/recording: import real image/footage, place it in the timeline, right-click layer, choose source viewer action, enable `Pt`, click multiple times, and see multiple persistent point indicators.
- GUI proof by screenshot/recording: enable `Box`, drag at least two boxes, and see multiple persistent box indicators at the same time as points.
- GUI proof by screenshot/recording: select a prompt and `Rm` removes only that prompt; `Clr` removes all prompts.
- Coordinate proof: repeat point/box entry after zoom/pan and with non-project-size source footage; recorded/logged prompt metadata must be exported source-frame pixels using exported source dimensions, not widget/project/canonical dimensions.
- AI-panel boundary proof: point/box add/remove/clear remains in the viewer toolbar; AI panel, if visible, is only source/task/model/status/run/apply/log/summary and is not required to arm toolbar prompt capture.

## Stop Conditions
Stop and report if:
- target symbol is missing
- required fix exceeds allowed files
- validation cannot run
- existing architecture contradicts the requested change
- task requires product/design judgment not in packet
- source viewer cannot be armed for the selected footage layer through the existing `sourceViewerRequested` path
- exact exported source dimensions cannot be obtained for the selected source frame
- making toolbar prompt capture work requires adding AI-panel point/box controls or making AI-panel prompt state authoritative

## Required Return Contract
Return only a task-focused summary. Do not include transcript, tool logs, raw file dumps, large code blocks, or broad unrelated issues. Include status, files inspected/changed, validation evidence, blockers, and task-specific risks.
