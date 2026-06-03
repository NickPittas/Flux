# Planner Report

## Status
ready

## Rationale
The locator evidence identifies one narrow race: live SAM3 results currently call the AI Work Viewer Read-node preview helper, which rewires viewer input. AI Paint already owns prompt overlay drawing in viewer canonical coordinates, and FluxAiPanel already receives live mask paths and tracks live-preview state, so the smallest safe fix is to route live result paths into the selected/source AI Paint node and draw the matte as an overlay from `AIPaint::drawOverlay()` without touching graph wiring or RotoPaint.

# Task Packet

## User Goal
Fix live SAM3 preview so editing AI Paint keeps the AI Work Viewer on the source/AI Paint node and displays the generated matte as a semi-transparent viewer overlay. Live preview must not rewire the AI Work Viewer to the generated mask Read node, while existing final/manual Run result preview behavior remains available unless intentionally documented as separate.

## Mode
general-coding

## Relevant Locations
- file: `Gui/FluxAiPanel.cpp`
  symbol: `FluxAiPanel::processSam3WorkerLine` / live `infer_still` response handling
  approximate lines: 1278-1292
  stable anchor: `const bool previewOk = _gui->previewFluxAiResultPngInWorkViewer(maskPath, &previewMessage);`
  reason: this live-result call is the viewer-jump culprit; replace only the live-preview branch with AI Paint overlay update.
  confidence: high
- file: `Gui/FluxAiPanel.cpp`
  symbol: `FluxAiPanel::setAIPaintLivePreviewEnabled`, `requestAIPaintSam3Unload`, `runAIPaintLivePreview`
  approximate lines: 462-499, 862-922
  stable anchor: `setAIPaintSam3StatusKnob(_sourceAIPaintNode, status);` and `aipaintLivePreviewEnabled(_sourceAIPaintNode)`
  reason: live-preview enable/disable/unload state should clear or retain the overlay appropriately and already has access to `_sourceAIPaintNode`.
  confidence: high
- file: `Gui/FluxAiPanel.cpp`
  symbol: final/manual Run result preview path
  approximate lines: 1506-1510
  stable anchor: `SAM3 result preview skipped:`
  reason: this non-live result preview may continue using `Gui::previewFluxAiResultPngInWorkViewer()` if it is the separate final/manual Run behavior.
  confidence: high
- file: `Engine/AIPaint.h`
  symbol: `class AIPaint`
  approximate lines: 77-89
  stable anchor: `std::vector<AIPaintPrompt> getPrompts() const;` and `virtual void drawOverlay(...)`
  reason: add a minimal public live-overlay API for FluxAiPanel to set/clear the current live mask PNG path; overlay drawing remains private.
  confidence: high
- file: `Engine/AIPaint.cpp`
  symbol: `struct AIPaintPrivate`, `AIPaint::drawOverlay`, `AIPaint::knobChanged`
  approximate lines: 119-159, 523-607
  stable anchor: `AIPaintContext context;`, `GLProtectAttrib a(...)`, and point/box overlay loop
  reason: store live mask path/texture cache and draw a tinted textured quad before or behind prompt primitives without changing render output.
  confidence: high
- file: `Gui/TextRenderer.cpp`
  symbol: texture upload and draw pattern
  approximate lines: 139-150, 307-323
  stable anchor: `glTexImage2D( GL_TEXTURE_2D... image.bits() )` and `glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);`
  reason: read-only example for QImage-to-OpenGL texture upload and alpha-blended textured drawing in the existing viewer GL style.
  confidence: medium
- file: `Engine/Texture.cpp`
  symbol: `Texture::ensureTextureHasSize`, `Texture::fillOrAllocateTexture`
  approximate lines: 78-109, 128-142
  stable anchor: `glTexImage2D (_target` and `glTexSubImage2D(_target`
  reason: read-only example for texture sizing/update if using existing texture helper patterns.
  confidence: medium
- file: `Gui/Gui05.cpp`
  symbol: `Gui::previewFluxAiResultPngInWorkViewer`
  approximate lines: 898-968
  stable anchor: `viewerNode->disconnectInput(0); viewerNode->connectInput(readNode, 0);`
  reason: do not use this helper for live preview; leave it available for final/manual Run preview unless a compile-only signature adjustment is unavoidable.
  confidence: high

## Allowed Edit Files
- `Engine/AIPaint.h`
- `Engine/AIPaint.cpp`
- `Gui/FluxAiPanel.cpp`

## Read-Only Context Files
- `Gui/FluxAiPanel.h`
- `Gui/Gui.h`
- `Gui/Gui05.cpp`
- `Gui/TextRenderer.cpp`
- `Engine/Texture.cpp`
- `tasks/T083-ai-paint-live-sam3-workflow-plan.md`

## Required Change
1. Add a minimal AI Paint live-overlay API in `Engine/AIPaint.h/.cpp`, for example `setLivePreviewMaskPath(const QString& absolutePngPath)` and `clearLivePreviewMask()`, or equivalent names consistent with existing style. These methods must store state on the AI Paint instance and call `redrawOverlayInteract()`; they must not affect `render()` or final RGBA output.
2. Extend `AIPaintPrivate` with live mask overlay state: current absolute path, loaded path, `QImage` or pixel buffer metadata, OpenGL texture id/size, and a dirty/valid flag. Keep ownership/destruction local to AI Paint and delete any GL texture safely when replaced/destroyed if a texture was created.
3. In `AIPaint::drawOverlay()`, if a valid live mask path is set, load/update the PNG into an OpenGL texture and draw it as a semi-transparent tinted matte overlay in the same canonical viewer coordinate space used by prompt overlays. Draw the matte before point/box primitives so prompt editing handles remain visible. Use alpha blending (`GL_SRC_ALPHA`, `GL_ONE_MINUS_SRC_ALPHA`) and restore GL state via existing `GLProtectAttrib` patterns. Treat the mask as viewer overlay only; do not feed it into `render()` or node output.
4. Map the mask quad to source/AI Paint image bounds. Prefer using the node/input format/region available in `AIPaint`/overlay context; if that exact bound is unavailable in the allowed files, use the same canonical source extent already used by AI Paint prompt coordinates. Stop rather than guessing if the matte cannot be aligned without broader viewer/format API work.
5. In `Gui/FluxAiPanel.cpp`, replace only the live-preview `infer_still` success path call to `_gui->previewFluxAiResultPngInWorkViewer(maskPath, ...)` with a call that resolves `_sourceAIPaintNode` to its `AIPaint` effect instance and sets the live overlay mask path. Log an overlay-specific message. Do not call `ensureFluxAiWorkViewerTab`, do not disconnect/connect viewer inputs, and do not create/update `FluxSAM3LiveResultPreview` for live preview.
6. Clear the AI Paint live overlay when Live Preview is disabled via `setAIPaintLivePreviewEnabled(false)`, when SAM3 unload is requested/completed, and when live preview is no longer valid because selected source/AI Paint changes if that state transition is present in `FluxAiPanel.cpp`. If clear-on-`Clear All Prompts` is feasible within `AIPaint::knobChanged`, clear the overlay there too; otherwise document that overlay clears on live disable/unload and is replaced by the next live result.
7. Preserve prompt overlay behavior: point/box drawing, selection, delete selected, clear prompts, and prompt editing must remain interactive above the matte. Do not change RotoPaint, main comp viewer, or live-preview graph wiring.
8. Leave `Gui::previewFluxAiResultPngInWorkViewer()` intact for non-live final/manual Run preview unless the implementation intentionally separates final preview; if final preview behavior changes, document it in the return summary and validation evidence.

## Non-Goals
- No RotoPaint changes.
- No main comp viewer changes.
- No graph rewiring, Read-node creation, temporary Multiply/red-channel overlay graph, or viewer input switching for live preview.
- No rendering the live matte into AI Paint's final RGBA output.
- No broad SAM3 worker protocol changes beyond routing live result paths.
- No prompt editing/select/delete/clear regressions or UI redesign.
- No cleanup/refactor of `Gui::previewFluxAiResultPngInWorkViewer()` beyond leaving it unused by live preview.

## Validation
Commands:
- `cmake --build /home/npittas/Flux/build --target Natron -j$(nproc)`

Manual GUI checks:
- With saved project footage and AI Paint selected, enable Live Preview, add/remove/select point and box prompts, and verify the AI Work Viewer remains on the source/AI Paint instead of switching to `FluxSAM3LiveResultPreview`.
- Verify the SAM3 matte appears as a semi-transparent tinted overlay and updates on subsequent live results.
- Verify prompts remain visible/editable/selectable/deletable/clearable while the matte overlay is visible.
- Disable Live Preview and unload SAM3; verify the matte overlay clears and viewer input remains unchanged.
- Inspect node graph or node list during live preview; verify no new live preview Read-node buildup and no live viewer rewiring.
- Run the existing final/manual SAM3 Run path; verify final result preview still works through the existing AI Work Viewer preview helper, or document any intentional separation.
- Capture screenshot/recording evidence showing the live overlay plus editable prompt controls and unchanged viewer source.

Expected result:
Build succeeds. Live Preview no longer calls the Read-node preview helper or rewires AI Work Viewer input. AI Paint draws live SAM3 matte as an overlay, prompt editing remains usable, overlay updates/clears on live state changes, no live Read-node buildup occurs, and final/manual Run preview remains available or is explicitly documented as separate.

## Stop Conditions
Stop and report if:
- target symbol is missing
- required fix exceeds allowed files
- validation cannot run
- existing architecture contradicts the requested change
- task requires product/design judgment not in packet
- `_sourceAIPaintNode` cannot be safely resolved to an `AIPaint` instance from `Gui/FluxAiPanel.cpp` without editing additional files
- the mask cannot be aligned to AI Paint/source canonical viewer coordinates using APIs available in the allowed edit/context files
- drawing the PNG as an overlay requires RotoPaint, main comp viewer, render-output, or graph rewiring changes
- final/manual Run preview and live preview cannot be separated without changing public helper APIs outside the allowed files

## Planner Self-Check
- locator evidence sufficient: yes — culprit live call, AI Paint overlay hook, and Read-node rewiring helper are directly located with high confidence.
- allowed edit files minimal and explicit: yes — only `Engine/AIPaint.h`, `Engine/AIPaint.cpp`, and `Gui/FluxAiPanel.cpp` are needed for AI Paint overlay state/drawing and live result routing.
- read-only context minimal: yes — limited to panel header, existing preview helper/header, GL texture examples, and prior T083 plan.
- anchors/lines included: yes — each relevant location includes path, symbol/anchor, approximate lines, reason, and confidence.
- validation concrete: yes — build command plus GUI race, overlay update/clear, prompt editability, graph/read-node buildup, final preview, and screenshot checks.
- parallelization decision explicit and safe: yes — single task; edits share AI Paint/panel live state, so parallelization would risk conflicting live-preview behavior.
- non-goals and stop conditions sufficient: yes — explicitly forbid RotoPaint, main comp viewer, render-output matte, graph rewiring, and broad worker/UI changes.
- reviewer findings addressed, if revision: not applicable — no reviewer findings supplied for this plan revision.

## Required Return Contract
Return only a task-focused summary. Do not include transcript, tool logs, raw file dumps, large code blocks, or broad unrelated issues. Include status, files inspected/changed, validation evidence, blockers, and task-specific risks, including whether final/manual Run preview remains using the existing helper.
