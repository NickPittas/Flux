# Research: T072 OpenFX Arena Text source/build recipe

## Summary
The bundled `plugins/ofx-extras/Text.ofx.bundle` comes from **NatronGitHub/openfx-arena**, specifically the `Text/` bundle sources that register `net.fxarena.openfx.Text` and optional `net.fxarena.openfx.RichText`. The alignment/justification path is in `Text/RichText.{h,cpp}` and is called by both `Text/TextOFX.cpp` and `Text/RichTextOFX.cpp`; the likely T072 bug is that Pango alignment/justify only has visible effect when the layout has a finite width, while the plugin's own hint says horizontal align needs word wrap enabled and auto-size/custom-position disabled.

## Findings
1. **Upstream repository** — Source is `https://github.com/NatronGitHub/openfx-arena`, not Natron core. README lists `Text` as a normal plugin and `RichText` as experimental/WIP, with build options for both Makefile and CMake. [openfx-arena README](https://github.com/NatronGitHub/openfx-arena)

2. **Plugin IDs and versions** — `Text/TextOFX.cpp` defines `kPluginIdentifier "net.fxarena.openfx.Text"`, `kPluginVersionMajor 6`, `kPluginVersionMinor 14`. `Text/RichTextOFX.cpp` defines `kPluginIdentifier "net.fxarena.openfx.RichText"`, version `0.8`. [TextOFX.cpp](https://github.com/NatronGitHub/openfx-arena/blob/master/Text/TextOFX.cpp), [RichTextOFX.cpp](https://github.com/NatronGitHub/openfx-arena/blob/master/Text/RichTextOFX.cpp)

3. **Likely commit/tag** — The bundled binary is most likely from current/recent `NatronGitHub/openfx-arena` `master` or a Natron 2.4.x/2.5.x release, because upstream `master` exposes Text `6.14` and RichText `0.8`, matching the requested bundled plugin IDs/version identifiers. Natron 2.4.2 docs mention Text `6.13`, so this bundle appears newer than that doc snapshot. I could not infer an exact commit from `Info.plist` (`CFBundleVersion 0.0.1d1`) alone. [Natron Text 2.4.2 docs](https://natron.readthedocs.io/en/v2.4.2/plugins/net.fxarena.openfx.Text.html), [openfx-arena releases](https://github.com/NatronGitHub/openfx-arena/releases)

4. **Exact files/functions for alignment/justify/width** — Primary shared implementation is:
   - `Text/RichText.h`: declares `RichText::setLayoutAlign(PangoLayout*, int)`, `setLayoutJustify(PangoLayout*, bool)`, `setLayoutWidth(PangoLayout*, int)`, `renderRichText(...)`, `renderText(...)`.
   - `Text/RichText.cpp`: `setLayoutAlign()` maps enum values to `pango_layout_set_alignment(layout, PANGO_ALIGN_RIGHT/CENTER/LEFT)`; `setLayoutJustify()` calls `pango_layout_set_justify(layout, justify)`; `setLayoutWidth()` calls `pango_layout_set_width(layout, width * PANGO_SCALE)`.
   - `Text/RichText.cpp`: `renderRichText()` calls `setLayoutWidth(layout, width)`, `setLayoutWrap(layout, wrap)`, `setLayoutAlign(layout, align)`, `setLayoutJustify(layout, justify)` before `pango_cairo_update_layout()`/`pango_cairo_show_layout()`.
   - `Text/RichText.cpp`: `renderText()` calls `setLayoutWidth(layout, width)`, `setLayoutWrap(layout, style.wrap)`, `setLayoutAlign(layout, style.align)`, vertical-align offset code, then `setLayoutJustify(layout, style.justify)`.
   [RichText.cpp](https://github.com/NatronGitHub/openfx-arena/blob/master/Text/RichText.cpp), [RichText.h](https://github.com/NatronGitHub/openfx-arena/blob/master/Text/RichText.h)

5. **Text plugin render path** — `Text/TextOFX.cpp::TextFXPlugin::render()` fetches `justify`, `wrap`, `align`, `autoSize`, `canvas`, transform/custom-position knobs, creates a `PangoLayout`, sets text/markup/font/options, and uses the shared RichText helpers. Its parameter hint explicitly says: `Horizontal text align. Custom position and auto size must be disabled and word wrap must be enabled (any option except none) to get anything else than left align.` This is probably the first local behavior to verify before patching. [TextOFX.cpp](https://github.com/NatronGitHub/openfx-arena/blob/master/Text/TextOFX.cpp)

6. **RichText plugin render path** — `Text/RichTextOFX.cpp::RichTextPlugin::render()` fetches `align`, `wrap`, `justify`, then delegates directly to `RichText::renderRichText(width, height, _fc, html, wrap, align, justify, ...)`. `getRegionOfDefinition()` also uses `renderRichText(..., noBuffer=true)` for autosize probing. [RichTextOFX.cpp](https://github.com/NatronGitHub/openfx-arena/blob/master/Text/RichTextOFX.cpp)

7. **Build recipe** — Upstream supports both category Makefile and CMake builds. For just this bundle: `git submodule update -i --recursive`, then `make CONFIG=release -C Text`, copy `Text/*-release/Text.ofx.bundle` to an OpenFX plugin path. CMake builds one `Arena.ofx.bundle` by default; use `-DRICHTEXT=ON` to include RichText in the aggregate Arena bundle. README also documents `-DBUNDLE_FONTS_CONF=ON` and `-DMAGICK_PKG_CONFIG=...`. [openfx-arena README](https://github.com/NatronGitHub/openfx-arena)

8. **Dependencies** — Text needs OpenFX support submodule plus `fontconfig`, `pango`/`pangocairo`, and `cairo`; README lists broader openfx-arena deps: OpenColorIO, libxml2, libzip, librsvg2, libcdr, librevenge, poppler-glib, lcms2, ImageMagick, OpenCL for optional OCL, libcurl for HaldCLUT, libsox for AudioCurve. `Text/Makefile` links only `FCONFIG_*` and `PANGO_*`; CMake requires many deps for the all-in-one Arena target. [Text/Makefile](https://github.com/NatronGitHub/openfx-arena/blob/master/Text/Makefile), [CMakeLists.txt](https://github.com/NatronGitHub/openfx-arena/blob/master/CMakeLists.txt)

## Sources
- Kept: NatronGitHub/openfx-arena README (https://github.com/NatronGitHub/openfx-arena) — primary upstream build/dependency documentation.
- Kept: Text/TextOFX.cpp (https://github.com/NatronGitHub/openfx-arena/blob/master/Text/TextOFX.cpp) — registers `net.fxarena.openfx.Text` and contains the standalone Text render/parameter path.
- Kept: Text/RichText.cpp (https://github.com/NatronGitHub/openfx-arena/blob/master/Text/RichText.cpp) — exact Pango alignment/justify/width helpers and render calls.
- Kept: Text/RichText.h (https://github.com/NatronGitHub/openfx-arena/blob/master/Text/RichText.h) — exact declarations/enums used by both plugins.
- Kept: Text/RichTextOFX.cpp (https://github.com/NatronGitHub/openfx-arena/blob/master/Text/RichTextOFX.cpp) — registers `net.fxarena.openfx.RichText` and delegates layout to `RichText::renderRichText`.
- Kept: Text/Makefile (https://github.com/NatronGitHub/openfx-arena/blob/master/Text/Makefile) — minimal per-bundle build object list and Text-specific link deps.
- Kept: CMakeLists.txt (https://github.com/NatronGitHub/openfx-arena/blob/master/CMakeLists.txt) — aggregate CMake build and `RICHTEXT` option.
- Dropped: FXMisc/RichTextFX/OpenJFX results — unrelated JavaFX rich text projects.
- Dropped: SEO/package index pages except AUR/FreshPorts as secondary confirmation — less authoritative than upstream source.

## Recommendation for Flux vendoring/building
Vendor `NatronGitHub/openfx-arena` as a source dependency (submodule or pinned source snapshot) and build the **Text category bundle** from source, not the whole Arena target, unless Flux needs every Arena plugin. For T072, patch `Text/RichText.cpp`/`Text/TextOFX.cpp` locally, pin the upstream commit in a `plugins/ofx-extras/openfx-arena` source tree or `third_party/openfx-arena`, and deploy `Text.ofx.bundle` through the existing Flux plugin deploy path.

Suggested first patch direction: create a small repro matrix for standalone Text with `(wrap none/word, autoSize on/off, transform/custom position on/off, canvas/project width)`, then ensure `pango_layout_set_width()` is called with a finite layout width whenever align/justify are exposed as effective. If alignment is expected to work without word-wrap, that is a behavior change from upstream's own hint and should be treated as a Flux-specific patch.

## Gaps
- Exact binary provenance cannot be proven from `Info.plist`; it does not encode a git hash. Next step would be to inspect `strings/readelf` on `Text.ofx` and compare symbols/version constants against tagged source builds.
- Need runtime confirmation of whether Flux wants upstream-compatible behavior (alignment only with wrap + finite layout width) or AE-like behavior (align always within layer/canvas bounds). That decision affects whether to patch only width setup or also UI/parameter semantics.
