# Review Report

## Verdict
fix-then-ship

## Scope Compliance
- passed for the scoped worker files; broader worktree is dirty with unrelated files, but the reviewed worker diff/artifact only covers the allowed TextRender files.
- evidence: worker artifact lists only `openfx-flux/TextRender/TextRender.cpp`, `TextRasterizer.h`, and `TextRasterizer.cpp`; `git diff -- openfx-flux/TextRender/...` shows only those three files changed.

## Validation Assessment
- command/result reviewed: worker reported `git diff --check -- openfx-flux/TextRender/TextRender.cpp openfx-flux/TextRender/TextRasterizer.h openfx-flux/TextRender/TextRasterizer.cpp && timeout 600 cmake --build build/openfx-flux -- -j"$(nproc)"` with `[100%] Built target FluxTextRender`.
- sufficient? no
- missing validation: no viewer zoom/cache repro or tile/ROI comparison was run. The requested behavior is specifically viewer zoom >100% must not re-layout/corrupt FluxText/FluxMotionText render/cache; a build only proves compilation, not layout/cache invariance.

## Findings

### Major
- Finding: Validation does not prove the requested zoom/cache corruption fix.
- Why it matters: The bug was runtime/layout-cache dependent; whitespace/build validation can pass while zoom >100% still produces different text placement or stale cached tiles.
- Evidence: worker artifact validation only reports `git diff --check` and `cmake --build build/openfx-flux`; it explicitly says “No GUI/viewer zoom render-cache validation was run.”
- Suggested change: Before shipping, run a minimal repro that renders the same FluxMotionText/TextRender frame at 100% and >100% viewer/render scale, then returns to 100%, and verify text placement/content is stable; include a tiled/ROI render comparison against full-frame render for the same frame.

### Minor
- Finding: `kSupportsRenderScale` is declared but not wired into the plugin descriptor.
- Why it matters: The code now depends on `args.renderScale` for font size, layout bounds, tracking, leading, and animator position, so the advertised host contract should be explicit and coherent.
- Evidence: `file:///home/npittas/Flux/openfx-flux/TextRender/TextRender.cpp:37-39` defines `kSupportsTiles`, `kSupportsMultiResolution`, and `kSupportsRenderScale`; `describe()` sets tiles and multires at `file:///home/npittas/Flux/openfx-flux/TextRender/TextRender.cpp:385-388` but never uses `kSupportsRenderScale`.
- Suggested change: Either set the OFX render-scale support property if this support library exposes it, or remove the unused macro and document that Natron supplies render scale through multi-resolution support.

## Trace Notes
- Entry path traced: `TextRenderPlugin::render()` dispatches by components at `file:///home/npittas/Flux/openfx-flux/TextRender/TextRender.cpp:327-345`, `setupAndProcess()` fetches the output image and builds `RenderRequest` at `file:///home/npittas/Flux/openfx-flux/TextRender/TextRender.cpp:229-300`, and `FluxText::renderText()` allocates/renders the raster at `file:///home/npittas/Flux/openfx-flux/TextRender/TextRasterizer.cpp:404-640`.
- Stable layout split is present: output allocation uses `request.outputBounds` at `file:///home/npittas/Flux/openfx-flux/TextRender/TextRasterizer.cpp:406-418`, while centering uses `layoutBounds` at `file:///home/npittas/Flux/openfx-flux/TextRender/TextRasterizer.cpp:527-530`.
- Layout bounds derivation is stable if `getRegionOfDefinition()` returns the generator/project RoD: it falls back to project size/offset at `file:///home/npittas/Flux/openfx-flux/TextRender/TextRender.cpp:268-276` and scales the RoD by `args.renderScale` at `file:///home/npittas/Flux/openfx-flux/TextRender/TextRender.cpp:277-282`.
- Raster allocation/write indexing is safe for clipped output bounds: raster dimensions are derived from `outputBounds` at `file:///home/npittas/Flux/openfx-flux/TextRender/TextRasterizer.cpp:413-418`, writes are clipped against `raster.bounds` before indexing at `file:///home/npittas/Flux/openfx-flux/TextRender/TextRasterizer.cpp:290-301`, and reads outside the raster return transparent at `file:///home/npittas/Flux/openfx-flux/TextRender/TextRasterizer.cpp:384-402`.
- Render scale is applied consistently in the current path: font pixel size, tracking, leading, layout bounds, and animator position are scaled at `file:///home/npittas/Flux/openfx-flux/TextRender/TextRasterizer.cpp:424-429`, `file:///home/npittas/Flux/openfx-flux/TextRender/TextRasterizer.cpp:452-458`, and `file:///home/npittas/Flux/openfx-flux/TextRender/TextRasterizer.cpp:594-595`. I did not find a double-application in the traced code.
- Scale=1 full-frame behavior is preserved in structure: when `args.renderScale` is 1 and `dst->getBounds()` equals project RoD, `outputBounds`, `layoutBounds`, and the prior single `bounds` canvas coincide (`file:///home/npittas/Flux/openfx-flux/TextRender/TextRender.cpp:263-285`; `file:///home/npittas/Flux/openfx-flux/TextRender/TextRasterizer.cpp:406-418`).

## Simpler Alternative Check
The smaller correct approach is exactly the one attempted: keep layout coordinates based on stable project/RoD bounds and allocate/write only the render window/tile. No simpler code-only change would prove cache correctness without runtime validation.

## Final Recommendation
Fix validation gap with a viewer zoom plus tile/full-frame invariance test, then ship unless that test exposes a remaining descriptor/render-scale contract issue.
