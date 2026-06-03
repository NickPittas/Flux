# Locator Report

## Summary
TextRender still uses the fetched destination image bounds (`dst->getBounds()`) as the text layout canvas, so viewer zoom/ROI/tile-sized output bounds can re-center/re-layout the raster and then enter the viewer texture cache.

## Confidence
high

## Relevant Locations

1. `file:///home/npittas/Flux/openfx-flux/TextRender/TextRender.cpp`
   - symbol: `TextRenderPlugin::setupAndProcess`
   - approximate lines: 229–277
   - stable anchor: `request.bounds = dst->getBounds();`
   - why relevant: This is the primary bug surface. The raster layout canvas is `dst->getBounds()`, not a stable project/format RoD.
   - evidence:
     - `request.renderScaleX = args.renderScale.x;`
     - `request.renderScaleY = args.renderScale.y;`
     - `request.bounds = dst->getBounds();`
     - `processor.setDstBounds(dst->getBounds());`
     - `processor.setRenderWindow(args.renderWindow, args.renderScale);`

2. `file:///home/npittas/Flux/openfx-flux/TextRender/TextRasterizer.cpp`
   - symbol: `FluxText::renderText`
   - approximate lines: 404–650
   - stable anchor: `raster->bounds = request.bounds;`
   - why relevant: Raster size and layout origin derive directly from `request.bounds`; block centering uses those bounds.
   - evidence:
     - `raster->bounds = request.bounds;`
     - `raster->width = std::max(1, request.bounds.x2 - request.bounds.x1);`
     - `raster->height = std::max(1, request.bounds.y2 - request.bounds.y1);`
     - `const double blockOffsetX = request.bounds.x1 + (raster->width - refAdvance) * 0.5 - minX;`
     - `const double offsetY = request.bounds.y1 + (raster->height - textHeight) * 0.5 - minY;`

3. `file:///home/npittas/Flux/openfx-flux/TextRender/TextRasterizer.h`
   - symbol: `FluxText::RenderRequest`
   - approximate lines: 22–36
   - stable anchor: `OfxRectI bounds = {0, 0, 1, 1};`
   - why relevant: Request has only one bounds field; no separate stable layout/project canvas vs render/tile bounds.
   - evidence:
     - `double renderScaleX = 1.0;`
     - `double renderScaleY = 1.0;`
     - `OfxRectI bounds = {0, 0, 1, 1};`

4. `file:///home/npittas/Flux/openfx-flux/TextRender/TextRender.cpp`
   - symbol: `TextRenderPluginFactory::describe`
   - approximate lines: 27–40, 347–376
   - stable anchor: `desc.setSupportsMultiResolution(kSupportsMultiResolution);`
   - why relevant: Declarations now say tiles + multires supported, and previous renderScale propagation exists, but layout is not tile/ROI invariant.
   - evidence:
     - `#define kSupportsTiles 1`
     - `#define kSupportsMultiResolution 1`
     - `#define kSupportsRenderScale 1`
     - `desc.setSupportsMultiResolution(kSupportsMultiResolution);`
     - `desc.setSupportsTiles(kSupportsTiles);`
   - note: `kSupportsRenderScale` is defined but not directly used by this descriptor; Natron probes render-scale support through RoD actions.

5. `file:///home/npittas/Flux/openfx-misc/SupportExt/ofxsGenerator.cpp`
   - symbol: `GeneratorPlugin::getRegionOfDefinition`
   - approximate lines: 333–374
   - stable anchor: `case eGeneratorExtentProject:`
   - why relevant: TextRender inherits generator RoD behavior. Stable project/format RoD exists here, but TextRender render code does not use that RoD as its layout canvas.
   - evidence:
     - `case eGeneratorExtentProject: {`
     - `OfxPointD siz = getProjectSize();`
     - `OfxPointD off = getProjectOffset();`
     - `rod.x1 = off.x; ... rod.x2 = off.x + siz.x;`

6. `file:///home/npittas/Flux/Engine/OfxEffectInstance.cpp`
   - symbol: `OfxEffectInstance::render`
   - approximate lines: 2020–2095
   - stable anchor: `_imp->effect->renderAction`
   - why relevant: Confirms host passes OFX renderWindow and mapped renderScale into plugin render args.
   - evidence:
     - `ofxRoI.x1 = args.roi.left();`
     - `ofxRoI.x2 = args.roi.right();`
     - `_imp->effect->renderAction(... ofxRoI, args.mappedScale.toOfxPointD(), ...)`

7. `file:///home/npittas/Flux/Engine/ViewerInstance.cpp`
   - symbol: `ViewerInstance::setupMinimalUpdateViewerParams`
   - approximate lines: 840–880
   - stable anchor: `double zoomFactor = _imp->uiContext->getZoomFactor();`
   - why relevant: Viewer zoom affects mipmap/proxy level; above 100% forces mipmap level 0, but visible ROI/texture bounds still vary with viewport.
   - evidence:
     - `double zoomFactor = _imp->uiContext->getZoomFactor();`
     - `double closestPowerOf2 = zoomFactor >= 1 ? 1 : ...`
     - `outArgs->mipmapLevelWithoutDraft = std::max(..., zoomMipmapLevel);`

8. `file:///home/npittas/Flux/Engine/ViewerInstance.cpp`
   - symbol: `ViewerInstance::getViewerRoIAndTexture`
   - approximate lines: 978–1135
   - stable anchor: `getExactImageRectangleDisplayed`
   - why relevant: Viewer computes rendered tiles/ROI from displayed rectangle and caches texture entries by tile rect/mipmap.
   - evidence:
     - `outArgs->params->roi = _imp->uiContext->getExactImageRectangleDisplayed(...)`
     - `getImageRectangleDisplayedRoundedToTileSize(...)`
     - `FrameKey(... it->rect, mipmapLevel, inputToRenderName, ...)`

## Direct Answers

- **Does TextRender use `dst->getBounds()` or `renderWindow` as layout canvas instead of project/format RoD?**
  - Yes: it uses `dst->getBounds()` as the layout canvas via `request.bounds = dst->getBounds()`.
  - It uses `renderWindow` only for processing/window clipping through `processor.setRenderWindow(args.renderWindow, args.renderScale)`, not for stable text layout.
  - This is not coherent for tiled/ROI rendering because `dst->getBounds()` can be tile/visible-region dependent.

- **Are tiling / multi-resolution / render-scale declarations coherent?**
  - Not fully.
  - Current source declares tile and multires support (`kSupportsTiles 1`, `kSupportsMultiResolution 1`) and propagates `args.renderScale` into rasterizer.
  - But the rasterizer centers text inside the fetched destination bounds, so it is not tile/ROI invariant. A plugin that supports tiles must produce identical pixels for a tile as it would for the same pixels rendered in the full RoD.
  - `kSupportsRenderScale` is defined but not directly set in `describe`; Natron appears to infer/probe support via `getRegionOfDefinitionAction` at scale 1 and 0.5.

## Ranked Hypotheses

1. **Highest:** Text layout canvas is unstable because `dst->getBounds()` changes with viewer ROI/tile/zoom; rasterizer re-centers text inside that smaller/different bounds, corrupting the tile result and viewer caches it.
2. **High:** Declaring `kSupportsTiles=1` is invalid until layout is based on stable project/format RoD and rendering clips to `renderWindow`; cached tiles can contain full-text re-centered into each tile.
3. **Medium:** Render-scale propagation fixed glyph size but not layout invariance; above-100% zoom changes viewer ROI/texture rect enough to expose the dst-bounds centering bug.
4. **Medium/Low:** Natron’s render-scale support probing may mark TextRender as render-scale capable because inherited generator RoD responds OK, but descriptor has no explicit `setSupportsRenderScale`; however the observed symptom points more strongly to tile/ROI layout.

## Allowed Edit Scope Recommendation
- Minimal primary:
  - `file:///home/npittas/Flux/openfx-flux/TextRender/TextRender.cpp`
  - `file:///home/npittas/Flux/openfx-flux/TextRender/TextRasterizer.h`
  - `file:///home/npittas/Flux/openfx-flux/TextRender/TextRasterizer.cpp`
- Likely change: separate stable layout/project/format bounds from destination/render-window bounds. Rasterizer should layout against stable RoD/canvas, while processor writes only `args.renderWindow`.
- Temporary diagnostic/fallback option: set `kSupportsTiles 0` to disprove tile-layout corruption, but this is less ideal than making rasterization tile invariant.

## Read-Only Context Recommendation
- `file:///home/npittas/Flux/openfx-misc/SupportExt/ofxsGenerator.cpp`
- `file:///home/npittas/Flux/Engine/OfxEffectInstance.cpp`
- `file:///home/npittas/Flux/Engine/ViewerInstance.cpp`
- `file:///home/npittas/Flux/Gui/ViewerGLPrivate.cpp`

## Validation Targets
- tests:
  - Build TextRender OFX bundle.
  - Render same TextRender frame full-frame vs tiled/ROI and pixel-compare.
- commands:
  - `cmake --build /home/npittas/Flux/build --target FluxTextRender`
  - `rg -n "request.bounds|setSupportsTiles|setSupportsMultiResolution|renderScale" openfx-flux/TextRender`
- manual checks:
  - Create FluxMotionText, zoom viewer 100%, 125%, 150%, 200%, pan viewport.
  - Toggle viewer proxy/render scale if available.
  - Clear viewer cache/restart only as a control; bug should not require restart after fix.
  - Screenshot/recording required for GUI-visible validation.

## Risks / Unknowns
- Codemap index is stale because the working tree has dirty/untracked files; I did not update because task said update stale only if safe, and dirty source includes relevant files.
- I did not write `/home/npittas/Flux/build-logs/locator-textrender-viewer-zoom-cache-corruption-retry.md` because the user request also explicitly said “Do not write files”; this final response contains the report content.

## Stop Recommendation
Implementation should proceed now, limited to TextRender’s layout bounds/tile invariance surface.