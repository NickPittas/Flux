# T083 SAM3 Authoritative Drift Audit

## 1. Original authoritative intent

Nick's direct command is authoritative: SAM3 point/box selection belongs in the viewer toolbar, similar to roto/tracking/rotobrush, not in the AI panel. The viewer must show persistent visual indicators for every selected point and every selected box. Multiple points and multiple boxes per image/video must be supported. Add/remove/clear controls belong in the viewer toolbar. If that plan cannot be implemented, the implementation must stop and ask Nick rather than inventing alternate UX.

The original complete workflow plan also required the source-frame pixel coordinate contract for prompts, backend inference from user prompts, preview overlays, graph/mask Apply, and persistence. Those packet details are subordinate to Nick's viewer-toolbar/multi-prompt/persistent-indicator requirements.

## 2. Where packet docs drifted from intent

- `tasks/T083-sam3-packet2-ai-panel-prompt-state-plan.md` moved point/box/text prompt controls into `FluxAiPanel` and explicitly allowed panel-owned point/box buttons. This conflicts with Nick's requirement that point/box selection and add/remove/clear controls belong in the viewer toolbar.
- Packet 2 framed prompt state as a single selected prompt (`_hasPrompt` / `_lastPrompt`) and allowed point/box capture from the AI panel, which drifts from multiple persistent points and boxes per image/video.
- `tasks/T083-sam3-packet3-viewer-prompt-coordinate-plan.md` mentions viewer toolbar controls only conditionally (`If adding viewer-toolbar controls...`) instead of making the viewer toolbar mandatory. It also still routes state/storage through the AI panel.
- `tasks/T083-sam3-packet4-backend-prompt-contract-plan.md` makes backend inference run for only one selected prompt kind (`text OR point OR box`), conflicting with the required support for multiple point and box prompts per image/video.
- `tasks/T083-sam3-packet5-preview-overlay-plan.md` covers generated mask overlay only. It does not require persistent visual indicators for prompt points/boxes themselves.

## 3. Implemented code that follows the drift

- `Gui/FluxAiPanel.cpp` implements panel prompt controls: `Point`, `Box`, text entry, and `Clear / Reset` in `FluxAiPanel::setupUi()`. This puts point/box selection controls in the AI panel, not the viewer toolbar.
- `Gui/FluxAiPanel.cpp` stores one prompt in `_lastPrompt` with `_hasPrompt`; point, box, and text handlers overwrite that single prompt. This implements single-prompt state, not multiple points and boxes.
- `Gui/FluxAiPanel.cpp` has point/box capture initiated by AI panel slots (`onCaptureViewerPointClicked`, `onCaptureViewerBoxClicked`) that arm `ViewerGL`, then converts captured canonical positions to source pixels in the panel.
- `Gui/ViewerGL.cpp` emits one-shot point/box capture JSON from armed capture modes. It does not maintain a prompt model, toolbar actions, add/remove controls, or persistent point/box indicator drawing.
- `Gui/ViewerTab.cpp` has no implemented SAM3 AI prompt toolbar controls in the inspected toolbar construction area.
- `Gui/FluxAiPanel.cpp` / `Gui/ViewerGL.cpp` implement preview mask overlay plumbing, but that is a generated-result overlay, not persistent prompt indicators for every point and box.
- `Gui/FluxAiPanel.cpp` backend launch/manifest code follows packet 4's single selected prompt contract (`selected_prompt_kind`, selected mask path), not a multi-point/multi-box prompt contract.

## 4. Salvage / rework / blocked

### Salvageable
- Source selection handoff and source metadata display in `FluxAiPanel`.
- Source-frame export metadata (`exported_png_width`, `exported_png_height`) and much of the coordinate conversion/validation logic, if reused by viewer-owned prompt state.
- Backend provider-runtime launch plumbing and result manifest writing, after adapting the prompt contract for multiple points/boxes.
- Generated mask preview overlay path, if kept separate from prompt-indicator overlays and validated after the prompt workflow is corrected.

### Must be reworked
- Move point/box add/remove/clear controls out of `FluxAiPanel` and into the viewer toolbar (`ViewerTab`/`ViewerGL`) per Nick's command.
- Replace `_lastPrompt` single-prompt UX with a viewer-owned multi-prompt model supporting multiple points and boxes per image/video.
- Add persistent viewer drawing for every point and every box before/independent of SAM3 result previews.
- Rework backend prompt contract away from exactly-one selected point/box prompt if SAM3 should consume multiple prompt items.
- Ensure AI panel no longer acts as the point/box selection surface; it can remain for task/model/run/status if needed.

### Blocked
- The current packet sequence is not authoritative for T083 because it normalized the drift into implementation packets. Next implementation should not continue from packets 2-5 as-is.
- Backend multi-prompt behavior needs an explicit contract decision if SAM3 probe/API expectations differ for multiple points and boxes.

## 5. Screenshot attempts are non-evidence

Invalid screenshot or recording attempts are non-evidence. A failed/blank/partial capture, a screenshot that does not show the actual viewer-toolbar prompt controls, or a screenshot that does not show persistent point/box indicators in the viewer cannot validate T083 behavior.

## 6. Required next implementation order

1. Stop treating the AI panel as the point/box selection UI.
2. Design/confirm the viewer-toolbar prompt UX exactly within Nick's constraints: Point, Box, add/remove/clear, persistent indicators, and multiple prompts.
3. Implement viewer-toolbar prompt controls and viewer-owned persistent prompt state/indicator drawing.
4. Wire source-frame coordinate conversion to that viewer-owned prompt state and prove zoom/pan/proxy/non-project-size stability.
5. Update AI panel only as a non-selection surface for task/model/source summary, Preview/Run, Apply status, and logs.
6. Rework backend prompt contract to consume the viewer-owned prompt set, including multiple points/boxes as required.
7. Revalidate preview overlay, Apply/nodegraph integration, and persistence after the authoritative prompt workflow is fixed.
