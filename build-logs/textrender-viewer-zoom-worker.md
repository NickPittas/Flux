# Worker Report

## Status
done

## Changed Files
- openfx-flux/TextRender/TextRender.cpp
- openfx-flux/TextRender/TextRasterizer.h
- openfx-flux/TextRender/TextRasterizer.cpp

## What Changed
- Enabled TextRender render-scale support advertisement.
- Passed OFX `args.renderScale.x/y` into `FluxText::RenderRequest`.
- Added `renderScaleX`/`renderScaleY` defaults to `RenderRequest`.
- Converted text project-space distances to render pixels in rasterization: font size uses X/Y pixel sizes, tracking and X animator offsets use X scale, leading/baselines and Y animator offsets use Y scale.

## Validation
Command:
`git diff --check -- openfx-flux/TextRender/TextRender.cpp openfx-flux/TextRender/TextRasterizer.h openfx-flux/TextRender/TextRasterizer.cpp && timeout 600 cmake --build build/openfx-flux -- -j"$(nproc)"`

Result:
Passed. `FluxTextRender.ofx` built successfully.

## Scope Check
Only the three allowed source files were edited for code. This report was written to the user-requested output path.

## Concerns / Follow-up
- No GUI/proxy visual comparison was run; validation was limited to whitespace check and openfx-flux build as requested.
