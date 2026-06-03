# Review Report

## Verdict
fix-then-ship

## Scope Compliance
- passed
- evidence: `git diff -- openfx-flux/TextRender/TextRender.cpp openfx-flux/TextRender/TextRasterizer.h openfx-flux/TextRender/TextRasterizer.cpp` shows changes only in the three allowed files.

## Validation Assessment
- command/result reviewed: user-reported `openfx-flux` build passed, `git diff --check` for the three files passed, installer deploy with `ldd`/plugin discovery passed.
- sufficient? no
- missing validation: a render/proxy comparison that exercises `args.renderScale` and verifies text occupies the same project-space size at full and proxy/zoom render scales; ideally also one animator-position/tracking case.

## Findings

### Major
- Finding: Non-uniform render scales still produce horizontally wrong glyph geometry because the FreeType face is only scaled by `renderScaleY`, while glyph bitmap widths and HarfBuzz advances are then used directly.
- Why it matters: The fix claims to carry both `renderScaleX` and `renderScaleY`, but at `renderScaleX != renderScaleY` text width follows Y scale, not X scale; only explicit tracking and animator X offsets follow X.
- Evidence: `TextRasterizer.cpp:417-442` computes `fontSize = request.fontSize * scaleY` and calls `FT_Set_Pixel_Sizes(face, 0, round(fontSize))`; later `shapeLine()` consumes HarfBuzz advances from that same face (`TextRasterizer.cpp:456`, `TextRasterizer.cpp:261-282`) and glyph draw positions/bitmap widths are copied directly (`TextRasterizer.cpp:539-552`). There is no compensating X transform for base glyph outlines/advances. `TextRasterizer.cpp:580-581` only scales animator position/tracking.
- Suggested change: Either document/guard that renderScale must be isotropic, or add proper horizontal handling for non-uniform render scale (e.g. set FreeType char size with X/Y scale, or render at Y scale and apply an X scale to glyph positions/bitmap sampling consistently).

## Simpler Alternative Check
A smaller safe alternative exists if Natron/Flux only ever sends isotropic proxy scales: carry a single `renderScale` scalar and scale all project-pixel distances uniformly. If non-uniform scales are part of the contract, the current two-axis surface is appropriate but incomplete for base glyph geometry.

## Final Recommendation
Fix or explicitly rule out non-uniform renderScale before shipping; otherwise the isotropic proxy/zoom symptom path is plausibly addressed but not fully proven by the reported validation.
