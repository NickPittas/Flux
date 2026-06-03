# Locator Report

## Summary
The smallest reliable edit surface is the Flux-owned OFX TextRender plugin (`openfx-flux/TextRender/*`), specifically render-scale support propagation into `TextRasterizer`, with `FluxMotionText.py` only as read-only graph context.

## Confidence
high

## Relevant Locations

1. `file:///home/npittas/Flux/openfx-flux/TextRender/TextRender.cpp`
   - symbol: `TextRenderPlugin::setupAndProcess`
   - approximate lines: 229–277
   - stable anchor: `FluxText::RenderRequest request;`
   - why relevant: Builds the rasterizer request from OFX params, but does **not** pass `args.renderScale` into `RenderRequest`; only `processor.setRenderWindow(args.renderWindow, args.renderScale)` receives it.
   - evidence: lines 247–265 populate `request`, line 263 uses `dst->getBounds()`, line 274 passes render scale only to processor window handling.

2. `file:///home/npittas/Flux/openfx-flux/TextRender/TextRender.cpp`
   - symbol: `TextRenderPluginFactory::describe`
   - approximate lines: 37–40, 362–375
   - stable anchor: `#define kSupportsRenderScale 0`
   - why relevant: The plugin declares multi-resolution support but has a render-scale flag constant unused by descriptor setup. This is likely central to viewer proxy/zoom render-scale behavior.
   - evidence: line 39 defines `kSupportsRenderScale 0`; descriptor sets multi-resolution at line 364 but no explicit `setSupportsRenderScale(...)` call appears in current file.

3. `file:///home/npittas/Flux/openfx-flux/TextRender/TextRasterizer.h`
   - symbol: `FluxText::RenderRequest`
   - approximate lines: 22–35
   - stable anchor: `OfxRectI bounds = {0, 0, 1, 1};`
   - why relevant: Request has font size, tracking, leading, bounds, but no render-scale fields. Any fix that makes text size stable across viewer/proxy render scales likely needs a scale value here.
   - evidence: struct fields lines 24–34 have no `renderScale`.

4. `file:///home/npittas/Flux/openfx-flux/TextRender/TextRasterizer.cpp`
   - symbol: `FluxText::renderText`
   - approximate lines: 402–618
   - stable anchor: `FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(std::max(1.0, std::round(fontSize))));`
   - why relevant: Text glyph size is rasterized directly in destination pixel space from unscaled `request.fontSize`; bounds and centering also use destination bounds directly. Viewer/proxy render-scale changes can therefore change apparent text size unless project-space distances are scaled consistently.
   - evidence: line 440 uses `fontSize` directly; lines 512–514 center using `raster->width/height`; lines 539–544 store glyph pixel positions directly; lines 578–583 apply animator position/scale in the same unscaled space.

5. `file:///home/npittas/Flux/plugins/FluxMotionText.py`
   - symbol: `createInstance`
   - approximate lines: 259–297
   - stable anchor: `_create_node(app, "net.flux.openfx.TextRender"...`
   - why relevant: Confirms FluxMotionText is a PyPlug wrapper around TextRender → FrameRange → TimeOffset → Transform → Grade → Output, and aliases `fontSize`/text params to TextRender. Likely read-only context, not the bug surface.
   - evidence: lines 259–270 create and connect `TextRender1`; lines 272–281 alias motion text params to TextRender params.

6. `file:///home/npittas/Flux/Engine/ViewerInstance.cpp`
   - symbol: viewer render-scale/proxy path
   - approximate lines: 1166–1197 from grep evidence
   - stable anchor: `supportsRenderScaleMaybe`
   - why relevant: Read-only context for how viewer proxy/render scale is passed downstream. Grep showed active input support determines whether identity or mipmap `RenderScale` is used.
   - evidence: grep hit shows `supportsRS == eSupportsNo ? RenderScale::identity : scale` around line 1197.

## Allowed Edit Scope Recommendation
- Primary:
  - `file:///home/npittas/Flux/openfx-flux/TextRender/TextRender.cpp`
  - `file:///home/npittas/Flux/openfx-flux/TextRender/TextRasterizer.h`
  - `file:///home/npittas/Flux/openfx-flux/TextRender/TextRasterizer.cpp`
- Do **not** edit Flux project serialization/load-save paths for this issue; source evidence points to render-scale/rasterization, independent of project save/load.
- Treat `plugins/FluxMotionText.py` as read-only unless implementation discovers aliases are overwritten, which current evidence does not show.

## Read-Only Context Recommendation
- `file:///home/npittas/Flux/plugins/FluxMotionText.py`
- `file:///home/npittas/Flux/Engine/ViewerInstance.cpp`
- `file:///home/npittas/Flux/Engine/EffectInstance.cpp`
- `file:///home/npittas/Flux/build-logs/review-textrender-renderscale.md` — prior review already flags render-scale validation gaps/non-uniform scale risk.

## Validation Targets
- tests:
  - Build `openfx-flux` / TextRender OFX bundle.
  - Run `git diff --check` on the three TextRender files.
- commands:
  - Existing setup/deploy command for OFX bundle so `net.flux.openfx.TextRender` is rediscovered.
  - Launch Flux and create FluxMotionText.
- manual checks:
  - Set Font Size to a non-default value, e.g. 180.
  - Zoom viewer in/out and verify rendered text stays same project-space size.
  - Toggle viewer proxy/render-scale levels 1/2/4/8 if available and verify text occupies same project-space size after display scaling.
  - Re-test animator position/tracking/scale with viewer zoom/proxy.
  - Capture screenshot/recording because this is GUI-visible behavior.

## Risks / Unknowns
- Codemap index was stale because the working tree had dirty/untracked files; I updated it because this location task explicitly allowed safe refresh. Index now reflects dirty workspace state.
- Current source has no `changedParam` implementation in TextRender and no `getRegionOfDefinition`/`getRegionsOfInterest` override found in `openfx-flux`; if implementation needs exact RoD under render scale, that may expand within the same three TextRender files.
- Prior review log indicates a previous/expected render-scale fix may mishandle non-uniform X/Y render scale; decide whether Flux/Natron viewer only sends isotropic scales before accepting a scalar-only fix.

## Stop Recommendation
Implementation can proceed now with high confidence, limited to the three TextRender files above. I did not write `/home/npittas/Flux/build-logs/locator-textrender-viewer-zoom.md` because my role instructions prohibit editing/writing files.