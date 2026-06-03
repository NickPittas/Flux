# Locator Report

## Summary
Native `net.fxarena.openfx.Text` alignment is provided by the bundled binary `Text.ofx`, but its source is not present in this checkout; FluxText only aliases the native `justify`/`align` knobs and can mask/reproduce the issue.

## Confidence
medium

## Relevant Locations

1. `file:///home/npittas/Flux/plugins/ofx-extras/Text.ofx.bundle/Contents/Linux-x86-64/Text.ofx`
   - symbol: native OFX binary for `net.fxarena.openfx.Text`
   - approximate lines: binary, no source lines
   - stable anchor: `strings` shows `pango_layout_set_alignment`, `pango_layout_set_justify`, `pango_layout_set_width`, `_ZN8RichText14setLayoutAlignEP12_PangoLayouti`
   - why relevant: this is the actual standalone native Text provider where left/center/right behavior lives.
   - evidence: `plugins/ofx-extras/README.md` lists `Text.ofx.bundle` runtime IDs `net.fxarena.openfx.Text`, `net.fxarena.openfx.RichText`.

2. `file:///home/npittas/Flux/Documentation/source/plugins/net.fxarena.openfx.Text.rst`
   - symbol: native Text params
   - approximate lines: 1–105
   - stable anchor: `Horizontal align / ``align``` and `Justify / ``justify```
   - why relevant: generated docs verify native param names and documented preconditions.
   - evidence: docs say `align` only works when custom position and auto size are disabled and word wrap is enabled; options are `Left`, `Right`, `Center`.

3. `file:///home/npittas/Flux/plugins/FluxText.py`
   - symbol: `TEXT_PARAM_SPECS`, `createInstance`
   - approximate lines: 136–225, 226–277
   - stable anchor: `{"name": "justify"...}`, `{"name": "align"...}`, `promoted.setAsAlias(source)`
   - why relevant: FluxText promotes/aliases native Text knobs as `Text1justify`, `Text1align`, `Text1wrap`, etc.; no independent layout logic here.
   - evidence: creates `app.createNode("net.fxarena.openfx.Text", 6, group)` and aliases every promoted Text param to the internal native Text node.

4. `file:///home/npittas/Flux/tasks/T069-ofx-plugin-restoration.md`
   - symbol: restored OFX provider notes
   - approximate lines: 27–45
   - stable anchor: `Text.ofx.bundle`
   - why relevant: confirms provider provenance is openfx-arena and installed as restored binary coverage.
   - evidence: table maps `net.fxarena.openfx.Text` to `Text.ofx.bundle`; no source path is cited.

5. `file:///home/npittas/Flux/openfx-io/OIIO/OIIOText.cpp`
   - symbol: `OIIOTextPlugin::render`, `OIIOTextPluginFactory::describeInContext`
   - approximate lines: 252–455, 558–672
   - stable anchor: `#define kPluginIdentifier "fr.inria.openfx.OIIOText"`
   - why relevant: nearby deprecated Text implementation, but **not** the broken native Text node.
   - evidence: plugin ID is `fr.inria.openfx.OIIOText`; params include only `position`, `text`, `fontSize`, `fontName`, `textColor`; no `align`/`justify`.

## Current Behavior Hypothesis
- Standalone native Text alignment likely fails inside the binary openfx-arena Pango/Cairo implementation, around calls equivalent to `RichText::setLayoutAlign`, `setLayoutJustify`, and `pango_layout_set_width`.
- The generated docs imply alignment depends on `wrap != None`, `autoSize == false`, and no custom position. If those preconditions are met and left/center/right still do not move text, likely layout width/canvas width is unset/zero or alignment is applied before/without `pango_layout_set_width`.
- FluxText is not the root cause; it aliases native knobs directly and can only expose/mask the native issue.

## Allowed Edit Scope Recommendation
- Primary: acquire/vendor the exact openfx-arena Text source used to build `plugins/ofx-extras/Text.ofx.bundle`, then edit only that plugin source and rebuild `Text.ofx.bundle`.
- Secondary/temporary: avoid editing `FluxText.py` except for validation scaffolding; aliases are correct.
- Do **not** edit `openfx-io/OIIO/OIIOText.cpp` for this bug; it is a different deprecated plugin ID.

## Read-Only Context Recommendation
- `file:///home/npittas/Flux/Documentation/source/plugins/net.fxarena.openfx.Text.rst`
- `file:///home/npittas/Flux/plugins/FluxText.py`
- `file:///home/npittas/Flux/plugins/ofx-extras/README.md`
- `file:///home/npittas/Flux/tasks/T069-ofx-plugin-restoration.md`
- `file:///home/npittas/Flux/openfx-io/OIIO/OIIOText.cpp` only to avoid wrong-target edits.

## Validation Targets
- tests:
  - Native standalone `net.fxarena.openfx.Text`, not FluxText first.
  - Then FluxText alias parity: `Text1wrap`, `Text1align`, `Text1justify`.
- commands:
  - `./tools/linux/flux-linux-setup.sh --validate-ldd`
  - `./tools/linux/flux-linux-setup.sh --clear-ofx-cache`
  - `cmake --build build --target Natron -j 8`
- GUI/manual checks:
  - Create standalone Text node.
  - Set `autoSize=Off`, `wrap=Word`, canvas/project width nonzero.
  - Enter multi-line/wrappable text.
  - Toggle `align`: Left, Center, Right.
  - Toggle `justify`.
  - Screenshot controls plus viewer result.
  - Repeat through FluxText promoted controls to confirm alias sync only.

## Risks / Unknowns
- Exact native Text source is absent from `/home/npittas/Flux`; only binary bundle and generated docs are present.
- Fixing this properly may require adding/building openfx-arena source in repo/tooling, not a small in-tree C++ edit.
- Codemap index was stale due commit change and untracked `build-logs/`; I refreshed it because the task allowed safe update. Codemap still did not index binary-source internals.

## Stop Recommendation
Implementation should **not** proceed until the openfx-arena Text source/build recipe is located or explicitly vendored. Minimal next task: find/import the exact source for `Text.ofx.bundle` and map `RichText::setLayoutAlign` / `setLayoutJustify` / layout width code.  

Note: I did not write `/home/npittas/Flux/build-logs/locator-T072-native-text-alignment.md` because this assigned role has a hard no-edit/no-write constraint.