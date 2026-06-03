# T083 SAM3 Complete Workflow Plan — Corrected Source of Truth

## Status
recovery-required

## Authoritative Nick Commands / Source-of-truth hierarchy
1. Nick's direct commands and the original approved plan override all packet text.
2. Preserve original non-conflicting details from the approved plan; supersede only details that conflict with Nick's commands.
3. For T083, SAM3 point/box selection belongs in the viewer toolbar, similar to roto/tracking/rotobrush.
4. Do **not** use the AI panel for point/box selection.
5. The viewer must show persistent visual indicators for every point and every box.
6. Multiple points and multiple boxes per image/video must be supported.
7. Add/remove/clear controls for point and box prompts belong in the viewer toolbar.
8. If the original plan cannot be implemented, stop and ask Nick; do not invent alternatives.

## Superseded Details Only
The following original/packet details are superseded and must not guide implementation:
- AI-panel ownership of point/box prompt selection, add/remove, reset, or clear controls.
- Single-prompt-only state or APIs for SAM3 point/box prompts.
- Any workflow where the backend, preview overlay, apply, or persistence path consumes an AI-panel-owned prompt instead of the viewer-owned prompt collection.

All other non-conflicting original details remain in force.

## Rationale for the Corrected Split
T083 must remain split so each packet is buildable, reviewable, and testable without hiding UX/backend coupling:
1. **Entry point and source handoff first**: establish the selected layer/source/frame identity before prompt capture.
2. **Viewer toolbar prompt UX next**: prompt ownership and visual truth must be correct before backend work.
3. **Coordinate contract before SAM3 invocation**: backend correctness depends on exact source-frame pixels, not widget coordinates.
4. **AI panel state after prompt ownership is settled**: panel controls can reflect source/model/run/apply status without owning prompts.
5. **Backend, preview, apply, persistence last**: each consumes the same viewer-owned prompt/source metadata and can be validated independently.

## Corrected Task Sequence
1. **Clean SAM3 entry points and source-viewer/AI-panel handoff**
   - Remove dev/test labels and provide the production action path to open the source/AI viewer for the selected footage layer.
   - Bind the selected source/layer/frame identity used by later prompt, preview, apply, and persistence work.
   - AI panel may display selected source/frame state only; no prompt ownership.
2. **Viewer toolbar multi-prompt UX and persistent visual prompt management**
   - Add viewer-toolbar point/box modes and add/remove/clear controls.
   - Support multiple points and boxes per selected source/image/video.
   - Draw persistent visual indicators for every point and box in the viewer.
   - AI panel may summarize prompt counts/details but must not own add/remove UX.
3. **Exact source-coordinate prompt capture contract**
   - Convert viewer prompt positions to exported source-frame pixel coordinates.
   - Store all point and box prompts with float source coordinates, clamped integer SAM3 coordinates, labels, source dimensions, source/layer/frame identity, and stable prompt IDs.
4. **AI panel task/model/status/run/apply/log state**
   - Keep task/model selection, status, logs, Preview/Run, Apply, and prompt summaries.
   - Disable Preview/Run until source + model + at least one viewer-owned prompt are available.
5. **SAM3 backend multi-prompt contract**
   - Pass the viewer-owned prompt collection to the external SAM3 process.
   - No hardcoded/default center/full-image/text prompts.
   - Backend must account for multiple points and boxes per source.
6. **Preview mask overlay path**
   - Load generated preview mask(s) as viewer-only overlays, registered to exported source-frame coordinates.
   - Overlay work must consume the same viewer-owned prompt/source metadata and must clear on prompt/source changes.
7. **Apply SAM3 raster result into Flux mask/nodegraph pipeline**
   - On Apply, write durable project-relative generated media/manifest and bind mask media into the selected layer's existing Flux mask/nodegraph pipeline.
8. **Save/reopen preservation and final E2E proof**
   - Persist selected source, viewer-owned prompt collection, generated result manifest/media references, mask binding, and restore through project save/reopen.

## Source-Frame Pixel Coordinate Contract
- Coordinate origin is the top-left pixel of the exported source frame `(0,0)`.
- `x` increases right; `y` increases down.
- Coordinates are zero-based source-frame pixels, not project/composition pixels, not screen/widget pixels, and not normalized UI coordinates.
- Store original float source-frame coordinates for every prompt.
- Store final integer SAM3 coordinates after explicit rounding/clamping.
- Points store prompt ID, label, float `(x,y)`, integer `(x,y)`, source width/height, and source/layer/frame identity.
- Boxes store prompt ID, label if applicable, normalized float min/max, integer `xyxy`, source width/height, and source/layer/frame identity.
- Box integer conversion uses floor(min) and ceil(max), then clamps to image bounds.
- Validation must prove correctness under viewer zoom, pan, proxy/render-scale changes, and non-project-size source footage.
- Validation must prove coordinates are based on the exported source frame when source dimensions differ from the project format.

## AI Panel Boundary
The AI panel is limited to:
- task/model selection;
- selected source/status display;
- prompt summary/counts for viewer-owned prompts;
- Preview/Run and Apply controls;
- logs/errors/progress.

The AI panel must not provide point/box add/remove/clear UX. Those controls belong in the viewer toolbar.

## Planning Appendix: Packet Hygiene to Preserve
Implementation packets derived from this plan should include, where useful:
- strict allowed edit files;
- read-only context files/symbols;
- exact required behavior and non-goals;
- validation commands and GUI proof requirements;
- stop conditions for missing symbols, wrong ownership boundaries, impossible validation, or design ambiguity;
- reviewer checklist that verifies Nick's commands and the original approved plan hierarchy.

## Superseded Packets
- `tasks/T083-sam3-packet2-ai-panel-prompt-state-plan.md` is superseded/invalid only where it routes point/box capture and clear/reset through the AI panel or assumes single-prompt ownership.
- `tasks/T083-sam3-packet3-viewer-prompt-coordinate-plan.md` is superseded/invalid only where it allows AI-panel-owned point/box prompt controls or fails to require multi-prompt persistent viewer indicators as the first UX priority.

## Preserved Original Packet Details Appendix

### Original Split Rationale
T083 SAM3 full workflow crosses separable UI entry, source-coordinate prompt capture, backend inference, preview, graph/media application, and save/reopen persistence boundaries. Splitting keeps packets narrow and executable while making later packets explicit about source-frame pixel coordinates, generated raster-mask media, nodegraph binding, and durable project serialization.

### Original Task Sequence, Corrected for Viewer-Toolbar Prompt Ownership
1. **Clean SAM3 entry points and source-viewer/AI-panel handoff**
   - purpose: remove dev/test SAM3 context-menu noise and make the user path explicit: selected footage layer -> source/AI viewer -> AI panel source state.
   - allowed files: `Gui/FluxTimeline.cpp`, `Gui/FluxTimeline.h`, `Gui/Gui05.cpp`, `Gui/Gui.h`, `Gui/FluxAiPanel.cpp`, `Gui/FluxAiPanel.h`, `Gui/ViewerTab.cpp`, `Gui/ViewerTab.h`
   - validation: build `NatronGui`/app target; right-click footage layer shows only production AI/source actions and no `SAM3 Dev`, `A0`, `A1/A2/A3` labels; source/AI viewer opens for selected footage and AI panel shows selected source/frame metadata.
2. **Viewer toolbar prompt UX, prompt summaries, and run/preview/apply state model**
   - purpose: replace default prompt generation with viewer-toolbar-owned point/box multi-prompt controls, task/model selection, prompt summaries in source media pixel coordinates, explicit Preview/Run and Apply controls, and disabled states for missing source/model/prompt.
   - allowed files: `Gui/ViewerTab.cpp`, `Gui/ViewerTab.h`, `Gui/ViewerGL.cpp`, `Gui/ViewerGL.h`, `Gui/FluxAiPanel.cpp`, `Gui/FluxAiPanel.h`
   - validation: build; screenshot proof of viewer toolbar point/box add/remove/clear controls, persistent viewer indicators, AI panel SAM3 task/model, selected source, prompt summaries, Preview/Run, and Apply disabled until preview exists.
3. **Viewer toolbar/modes and exact source-coordinate prompt capture**
   - purpose: add explicit AI selection modes/icons/actions for point and box in the viewer toolbar; viewer interactions must map displayed image positions to exported source-frame pixel coordinates, independent of viewer zoom, pan, proxy/render scale, or project format.
   - allowed files: `Gui/ViewerGL.cpp`, `Gui/ViewerGL.h`, `Gui/ViewerTab.cpp`, `Gui/ViewerTab.h`, `Gui/FluxAiPanel.cpp`, `Gui/FluxAiPanel.h`
   - validation: build; screenshot/recording of point and box prompt entry on source viewer with visible persistent indicators and source-frame pixel coordinates; repeat at different zoom/pan, with proxy/render-scale enabled if available, and with non-project-size footage to prove coordinates stay in source media pixels.
4. **SAM3 backend prompt contract and multi-prompt inference**
   - purpose: update the out-of-process SAM3 probe invocation so it consumes the viewer-owned point/box prompt collection in exported source-frame pixel coordinates, not hardcoded full-image/center/default text, while keeping PyTorch external.
   - allowed files: `Gui/FluxAiPanel.cpp`, `Gui/FluxAiPanel.h`, `tools/ai/sam3_transformers_real_inference_probe.py`
   - validation: `python3 -m py_compile tools/ai/sam3_transformers_real_inference_probe.py`; self-check/help shows prompt collection args; C++ build; logs/manifests record source frame dimensions, original float prompt coordinates, and final integer SAM3 coordinates in source media pixels matching the exported source PNG; repeat validation at different viewer zoom/pan, with proxy/render-scale enabled if available, and with non-project-size footage.
5. **Preview mask overlay path**
   - purpose: load generated SAM3 preview mask(s) from project-relative output and display them as viewer overlay previews before Apply, without altering graph/layer state.
   - allowed files: `Gui/ViewerGL.cpp`, `Gui/ViewerGL.h`, `Gui/FluxAiPanel.cpp`, `Gui/FluxAiPanel.h`, `Gui/Gui05.cpp`, `Gui/Gui.h`
   - validation: build; generated mask PNG appears as translucent overlay registered to the exported source frame in source media pixel space; overlay can be cleared/reset on prompt/source changes; graph/layer model remains unchanged until Apply.
6. **Apply SAM3 raster result into Flux mask/nodegraph pipeline**
   - purpose: on Apply, write durable project-relative generated mask media/manifest, create or reuse Read node(s) for the generated raster mask image/sequence, and bind the mask media into the selected layer's existing Flux mask/nodegraph pipeline through `FluxMask`/`maskApplyNode`/`FluxMaskUtils` surfaces using project-relative references only.
   - allowed files: `Gui/Gui05.cpp`, `Gui/Gui.h`, `Gui/FluxTimeline.cpp`, `Gui/FluxTimeline.h`, `Gui/FluxMaskUtils.cpp`, `Gui/FluxMaskUtils.h`, `Gui/FluxAiPanel.cpp`, `Gui/FluxAiPanel.h`
   - validation: build; Apply creates/reuses generated-mask Read node(s) instead of treating raster output as an in-memory-only overlay; nodegraph screenshot shows generated mask media wired into selected layer mask branch/apply path; viewport shows masked result; no absolute generated-output paths in project-facing metadata.
7. **Save/reopen preservation of SAM3 source/prompt/result/mask relationship and final internal E2E proof**
   - purpose: persist the selected SAM3 source, exported source-frame metadata, prompt JSON/source-pixel coordinates, generated result manifest/media references, generated mask Read node references, and Flux mask relationship through project save/reopen, then run the complete Nick-approved workflow internally before asking the user to verify.
   - allowed files: `Gui/FluxTimelineSerialization.h`, `Gui/ProjectGuiSerialization.h`, `Gui/ProjectGuiSerialization.cpp`, `Gui/ProjectGui.cpp`, `Gui/FluxTimeline.cpp`, `Gui/FluxTimeline.h`, `Gui/FluxAiPanel.cpp`, `Gui/FluxAiPanel.h`, `Gui/Gui05.cpp`, `Gui/Gui.h`, `Gui/FluxMaskUtils.cpp`, `Gui/FluxMaskUtils.h`
   - validation: build; save project, reopen, SAM3 source/prompt/result/mask relationship restores; generated media and manifests remain project-relative; generated-mask Read node(s) and Flux mask/apply branch reconnect; internal recording covers import -> timeline select -> source/AI viewer -> SAM3 task/model -> viewer-toolbar prompts -> preview overlay -> Apply -> save/reopen.

### Preserved First Packet Details
- **User goal:** Start the T083 SAM3 full user workflow by cleaning the user entry path: after importing footage and selecting a timeline footage layer, the user can open the correct source/AI viewer from a production context-menu/action path, and the AI panel reflects the selected source/frame without dev/test labels or direct generation side effects.
- **Mode:** general-coding.
- **Relevant locations:**
  - `Gui/FluxTimeline.cpp`, `FluxTimeline::contextMenuEvent`, anchors `Open Original Source Viewer (SAM3 A0)`, `SAM3 Dev: Arm Source Viewer Positive Point (A3)`, `SAM3 Dev: Capture Source Frame + SAM3 Preview (A1/A2/A3)`.
  - `Gui/Gui05.cpp`, `Gui::setupFluxUi` signal wiring and `Gui::exportFluxSam3SourceFrameForSelectedLayer`, anchors `exportFluxSam3SourceFrameForSelectedLayer`, `sourceFrameCaptureRequested`, `_imp->_fluxAiPanel = aiPanel`.
  - `Gui/FluxAiPanel.cpp`, `FluxAiPanel::setViewerForCapture`, serialization/restore/setup/run symbols, anchors `Source: none selected`, `Capture Viewer Box`, `exportFluxSam3SourceFrameForSelectedLayer`.
  - `Gui/FluxAiPanel.h`, anchor `setViewerForCapture(ViewerGL* viewer)`.
  - `Gui/FluxTimeline.h`, source viewer/capture request signals used by `Gui05.cpp`.
  - `Gui/ViewerTab.cpp`, `Gui/ViewerTab.h`, source/active viewer accessors used by `Gui::setActiveViewer`/`addNewViewerTab`.
- **Allowed edit files:** `Gui/FluxTimeline.cpp`, `Gui/FluxTimeline.h`, `Gui/Gui05.cpp`, `Gui/Gui.h`, `Gui/FluxAiPanel.cpp`, `Gui/FluxAiPanel.h`, `Gui/ViewerTab.cpp`, `Gui/ViewerTab.h`.
- **Read-only context files:** `tasks/T083-ai-matte-depth.md`, `Gui/ViewerGL.cpp`, `Gui/ViewerGL.h`, `Gui/FluxMaskUtils.cpp`, `Gui/FluxMaskUtils.h`, `Gui/FluxTimelineSerialization.h`, `Gui/ProjectGuiSerialization.h`, `Gui/ProjectGuiSerialization.cpp`, `Gui/ProjectGui.cpp`, `tools/ai/sam3_transformers_real_inference_probe.py`.
- **Required change:** Replace timeline footage-layer SAM3 dev/test context-menu entries with production wording and behavior for the first user step. Keep `Open Read Node`. Add/keep one production action for opening the original/source AI viewer for the selected footage layer, and optionally one production action to focus/open the AI panel for that selected source if supported by existing pane APIs. Remove visible labels containing `SAM3 Dev`, `SAM3 A0`, `A1/A2/A3`, or other test/probe wording. Ensure selecting the action updates timeline selection, opens/focuses the correct source/AI viewer through existing `sourceViewerRequested` wiring, and updates `FluxAiPanel` with selected layer/source/frame metadata without starting SAM3 inference or writing generated media. Preserve existing serialization, Save As/inference behavior, prompt schema, preview overlay, generated media, and Apply behavior for later packets.
- **Non-goals:** do not run SAM3 inference from a context-menu action; do not add preview mask overlays; do not integrate generated masks into nodegraph/layer mask pipeline; do not change project serialization schemas or save/reopen behavior in packet 1; do not alter model-manager/token/install policy; do not add in-process PyTorch or link Python model code into Flux; do not request user validation, only internal build/smoke evidence.
- **Validation:** build the Flux GUI/app target; search edited UI strings for removed dev labels with `grep -R "SAM3 Dev\|SAM3 A0\|A1/A2/A3" Gui/FluxTimeline.cpp Gui/FluxAiPanel.cpp Gui/Gui05.cpp`; smoke test imported footage selection, production source/AI action(s), source/AI viewer opening, and AI panel selected source/frame metadata without inference or output files.
- **Stop conditions:** stop if target symbol is missing, required fix exceeds allowed files, validation cannot run, architecture contradicts the requested change, task requires product/design judgment, opening a distinct source/AI viewer cannot be done with existing APIs without designing new viewer architecture, or AI panel source metadata cannot be updated without changing serialization/coordinate/generated-media/mask-apply contracts beyond the packet.

## Recovery Plan
Use `tasks/T083-sam3-viewer-toolbar-multiprompt-recovery-plan.md` as the next executable scoped packet before continuing backend/preview/apply implementation.
