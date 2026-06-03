# T083 SAM3 Viewer Toolbar Multi-Prompt Recovery Packet

## Status
executable-scoped-packet

## Source of Truth
Nick's direct command controls T083 and overrides any conflicting packet text:
- SAM3 point/box selection must be in the viewer toolbar, like roto/tracking/rotobrush.
- Do not use the AI panel for point/box selection.
- The viewer must show persistent visual indicators for every point and every box.
- Multiple points and boxes per image/video must be supported.
- Add/remove/clear controls belong in the viewer toolbar.
- AI panel is limited to task/model/status/run/apply/log and prompt summaries.

Preserve original non-conflicting details from `tasks/T083-sam3-complete-workflow-plan.md`; supersede only AI-panel prompt ownership and single-prompt assumptions.

## Goal
Recover T083 by implementing viewer-toolbar-owned SAM3 point/box multi-prompt UX and prompt storage before backend, preview, apply, or persistence work continues.

## Allowed Edit Files
- `Gui/ViewerTab.cpp`
- `Gui/ViewerTab.h`
- `Gui/ViewerGL.cpp`
- `Gui/ViewerGL.h`
- `Gui/FluxAiPanel.cpp`
- `Gui/FluxAiPanel.h`

If source/viewer handoff requires another file to bind selected source identity into the viewer prompt store, stop and request a packet update instead of editing outside this list.

## Read-Only Context
- `tasks/T083-sam3-complete-workflow-plan.md`
- Existing viewer toolbar/interact patterns in `Gui/ViewerTab.*` and `Gui/ViewerGL.*`
- Existing AI panel state/run/apply patterns in `Gui/FluxAiPanel.*`
- Existing source/selected-layer identity plumbing only as needed to understand how the viewer obtains the current source/frame.

## Required Changes
1. Viewer toolbar provides SAM3 prompt controls:
   - Point mode;
   - Box mode;
   - remove selected prompt;
   - clear all prompts;
   - optional positive/negative label toggle only if supported by the approved SAM3 contract.
2. Viewer owns prompt state per selected source/image/video:
   - stable prompt IDs;
   - prompt type (`point` or `box`);
   - point label, default positive `1` unless approved label UI exists;
   - source-frame float coordinates;
   - clamped integer SAM3 coordinates;
   - source width/height;
   - source/layer/frame identity needed for stale-result checks.
3. Viewer displays persistent visual indicators:
   - every point remains visible until removed/cleared/source changes;
   - every box remains visible until removed/cleared/source changes;
   - selected prompt has a distinct visual state;
   - indicators remain registered under zoom, pan, proxy, and render-scale changes.
4. Multi-prompt support:
   - adding a point appends to the prompt collection;
   - drawing a box appends to the prompt collection;
   - remove deletes only the selected prompt;
   - clear deletes the full collection for the current source;
   - Preview/Run uses the whole current viewer-owned prompt collection.
5. AI panel boundary:
   - shows selected source and prompt summary/counts;
   - enables Preview/Run only when source + model + at least one viewer-owned prompt are available;
   - owns status/log/run/apply only;
   - does not expose point/box add/remove/clear controls.

## Coordinate Contract
- Origin is the top-left pixel of the exported source frame `(0,0)`.
- `x` increases right; `y` increases down.
- Coordinates are zero-based source-frame pixels, not project/composition pixels or widget coordinates.
- Store zero-based float source-frame coordinates.
- Store final integer SAM3 coordinates after explicit rounding/clamping.
- Points: label defaults to positive `1` unless a later approved label UI changes this.
- Boxes: store normalized float min/max and integer `xyxy` using floor(min), ceil(max), then clamp to image bounds.
- Metadata records original float coordinates, final integer SAM3 coordinates, source width/height, source/layer/frame identity, and prompt ID.
- Validation must cover zoom/pan/proxy/render-scale behavior and non-project-size source footage.

## Non-Goals
- Do not implement SAM3 backend execution beyond passing/reading the viewer-owned prompt collection needed by existing AI panel enablement.
- Do not implement preview mask overlay loading.
- Do not implement Apply-to-mask/nodegraph binding.
- Do not implement save/reopen persistence.
- Do not add AI-panel point/box add/remove/clear controls.
- Do not introduce hardcoded center/full-image/default text prompts.
- Do not edit task docs in the implementation packet unless separately authorized.

## Validation
- Build command: run the existing Flux/Natron build target used by this workspace for GUI changes.
- GUI proof: screenshot or recording showing viewer-toolbar point/box modes and add/remove/clear controls.
- GUI proof: multiple persistent points and boxes visible simultaneously in the viewer.
- GUI proof: selected-prompt removal and clear-all from the viewer toolbar.
- Coordinate proof: source-frame coordinates remain correct under zoom/pan and proxy/render-scale changes.
- Coordinate proof: non-project-size footage records exported source-frame pixels, not project-format pixels.
- AI panel proof: screenshot showing summaries/status/run/apply/log only, with no point/box add/remove/clear UX.

## Stop Conditions
- Any required symbol/file is missing or the viewer toolbar/prompt drawing path is not in the allowed edit files.
- Correct implementation requires editing a file not listed under Allowed Edit Files.
- The current code already contains AI-panel-owned point/box controls that cannot be removed within the allowed files.
- Coordinate conversion cannot be proven against source-frame dimensions.
- Build or GUI validation command is unavailable.
- Any product/design decision is needed, including alternate UX placement, label semantics, or changing the prompt contract.

## Reviewer Checklist
- Confirms Nick's command/source-of-truth hierarchy is followed.
- Confirms point/box selection and add/remove/clear are in the viewer toolbar, not the AI panel.
- Confirms persistent visual indicators exist for every point and every box.
- Confirms multiple points and multiple boxes per image/video are supported.
- Confirms prompt state includes stable IDs, labels, float source coordinates, integer SAM3 coordinates, source dimensions, and source/layer/frame identity.
- Confirms coordinates are source-frame pixels and validation covers zoom/pan/proxy/render-scale and non-project-size footage.
- Confirms AI panel owns only task/model/status/run/apply/log and prompt summaries.
- Confirms no backend, preview, apply, persistence, or unrelated refactor work slipped into this packet.

## Rework Impact for Later Packets
- **Backend packet:** must accept a prompt collection, not a single AI-panel prompt.
- **Preview overlay packet:** must clear/reload on viewer prompt add/remove/clear/source changes and associate results with prompt collection metadata.
- **Apply packet:** must bind generated result media/manifest produced from the viewer-owned prompt collection.
- **Persistence packet:** must serialize/restore selected source, prompt collection, generated result manifest/media, and mask binding.
