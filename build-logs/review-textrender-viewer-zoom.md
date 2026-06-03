# Review Report

## Verdict
ship

## Scope Compliance
- passed
- evidence: `git diff -- openfx-flux/TextRender/TextRender.cpp openfx-flux/TextRender/TextRasterizer.h openfx-flux/TextRender/TextRasterizer.cpp` shows code changes only in the three allowed files. The wider working tree has unrelated modified files, but this scoped diff did not touch them.

## Validation Assessment
- command/result reviewed: I reran `git diff --check -- openfx-flux/TextRender/TextRender.cpp openfx-flux/TextRender/TextRasterizer.h openfx-flux/TextRender/TextRasterizer.cpp && timeout 600 cmake --build build/openfx-flux -- -j"$(nproc)"`; result: passed, `[100%] Built target FluxTextRender`.
- sufficient? yes for compile/build validation; behavior is supported by source trace, but there is still no GUI/proxy visual comparison artifact.
- missing validation: optional render/proxy comparison at renderScale 1.0 vs 0.5 and a non-uniform synthetic renderScale case would give stronger visual proof.

## Findings

No blocker/major/minor findings.

Traced path: `TextRenderPlugin::setupAndProcess()` copies OFX `args.renderScale.x/y` into `FluxText::RenderRequest` at `openfx-flux/TextRender/TextRender.cpp:263-264`; defaults remain scale=1 in `openfx-flux/TextRender/TextRasterizer.h:34-35`; `renderText()` clamps invalid scales to 1 at `openfx-flux/TextRender/TextRasterizer.cpp:417-418`; font pixel size is scaled in both axes via `FT_Set_Pixel_Sizes(width=fontSizeX, height=fontSizeY)` at `openfx-flux/TextRender/TextRasterizer.cpp:419-447`; tracking uses X scale and leading/baseline use Y scale at `openfx-flux/TextRender/TextRasterizer.cpp:421-461`; animator position/tracking offsets are axis-scaled at `openfx-flux/TextRender/TextRasterizer.cpp:585-586`; scale=1 preserves previous font/tracking/leading/offset values because all multipliers reduce to 1. Support intent is coherent with `kSupportsMultiResolution` and `kSupportsRenderScale` enabled at `openfx-flux/TextRender/TextRender.cpp:37-39`, with render-scale-aware render-window processing at `openfx-flux/TextRender/TextRender.cpp:276`.

## Simpler Alternative Check
A smaller scalar-only renderScale would satisfy only isotropic viewer/proxy scales; the two-axis propagation is justified because OFX renderScale is a 2D value and the implementation now handles non-uniform X/Y text distances.

## Final Recommendation
Ship; the scoped fix propagates renderScale end-to-end and preserves scale=1 behavior, with build validation passed.
