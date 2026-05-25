# T078 — Real glyph rendering for Flux TextRender

Status: BLOCKED  
Phase: P7 — Shapes + Text  
Started: 2026-05-25  
Completed: —  
Owner: opencode

> Completion revoked 2026-05-25: glyph rendering was only proven in controlled/headless paths. This is renderer groundwork only, not accepted product work. Recovery source of truth: `tasks/FLUX_MOTION_TEXT_RECOVERY_SOURCE_OF_TRUTH.md`.

## Goal

Replace the `net.flux.openfx.TextRender` placeholder diagnostic with real visible glyph rendering driven by node parameters.

This task is not complete until `net.sf.openfx.FluxMotionText` renders legible text from `net.flux.openfx.TextRender`, not a checkerboard/bar placeholder.

## Approved Scope

- Keep the dedicated Flux OFX path under `openfx-flux/`.
- Keep legacy `plugins/FluxText.py` untouched.
- Implement a first functional renderer using:
  - Fontconfig for font resolution,
  - HarfBuzz for shaping,
  - FreeType for glyph rasterization,
  - custom premultiplied RGBA compositing into the OFX output image.
- Add renderer parameters for at least:
  - `text`
  - `fillColor`
  - `font`
  - `fontSize`
- Promote/alias the new font controls through `plugins/FluxMotionText.py`.

## Non-goals

- No replacement of the existing timeline/menu text creation path yet.
- No mutation of legacy `plugins/FluxText.py`.
- No fixed-slot animator implementation.
- No full AE text animator evaluation in this first glyph-rendering task.

## Validation Required

- Build `FluxTextRender.ofx.bundle` successfully.
- Deploy the updated bundle and `FluxMotionText.py` to an isolated runtime.
- Render visible glyph text spelling `Flux Motion Text`.
- Prove output changes when:
  - `text` changes,
  - `fillColor` changes,
  - `fontSize` changes.
- Save/reload a project and rerender the same text successfully.
- Review code before any future status change.

## Renderer Groundwork Evidence — Not Product Acceptance

The previous completion claim is revoked. The work below proves only basic glyph rendering in controlled/headless paths. It does not prove the canonical Flux GUI workflow, font picker UX, text animator UI, per-element animation, save/reopen of real animator controls, or final FluxMotionText product behavior.

### Implemented

- Added `TextRasterizer.{h,cpp}` under `openfx-flux/TextRender/`.
- Uses Fontconfig to resolve font families, HarfBuzz to shape UTF-8 text, and FreeType to rasterize glyph coverage.
- Composites glyph coverage into transparent premultiplied output for RGBA, RGB, XY, and alpha host component requests.
- Added `font` and `fontSize` renderer parameters.
- Promoted/aliased `font` and `fontSize` through `plugins/FluxMotionText.py`.
- Kept legacy `plugins/FluxText.py` untouched.

### Validation Evidence

Build/deploy:

```bash
python -m py_compile plugins/FluxMotionText.py
cmake --build /home/npittas/Flux/build/openfx-flux --target FluxTextRender -- -j$(nproc)
cmake --install /home/npittas/Flux/build/openfx-flux --prefix /home/npittas/Flux/plugins
FLUX_USER_PYPLUG_DIR=/tmp/opencode/t078/home/.Natron/PyPlugs \
FLUX_USER_OFX_DIR=/tmp/opencode/t078/home/.OFX/Plugins \
FLUX_OFX_CACHE_DIR=/tmp/opencode/t078/home/.cache/INRIA/Natron/OFXLoadCache \
FLUX_LAUNCHER_PATH=/tmp/opencode/t078/bin/flux \
./tools/linux/flux-linux-setup.sh --deploy-extras --clear-ofx-cache --no-check --force
```

Headless render/save/reload:

```bash
HOME=/tmp/opencode/t078/home \
NATRON_DISK_CACHE_PATH=/tmp/opencode/t078/cache-create-2 \
OFX_PLUGIN_PATH=/tmp/opencode/t078/home/.OFX/Plugins \
NATRON_PLUGIN_PATH=/tmp/opencode/t078/home/.Natron/PyPlugs:/home/npittas/Flux/Gui/Resources/PyPlugs:/home/npittas/Flux/plugins/natron-plugins \
XDG_CACHE_HOME=/tmp/opencode/t078/xdg-cache-create-2 \
OCIO=/opt/Nuke17.0v1/plugins/OCIOConfigs/configs/nuke-default/config.ocio \
QT_PLUGIN_PATH=/usr/lib64/qt6/plugins \
QT_QPA_PLATFORM=xcb \
/home/npittas/Flux/build/Renderer/NatronRenderer -b /tmp/opencode/t078/t078_create_render_save.py

HOME=/tmp/opencode/t078/home \
NATRON_DISK_CACHE_PATH=/tmp/opencode/t078/cache-reload-2 \
OFX_PLUGIN_PATH=/tmp/opencode/t078/home/.OFX/Plugins \
NATRON_PLUGIN_PATH=/tmp/opencode/t078/home/.Natron/PyPlugs:/home/npittas/Flux/Gui/Resources/PyPlugs:/home/npittas/Flux/plugins/natron-plugins \
XDG_CACHE_HOME=/tmp/opencode/t078/xdg-cache-reload-2 \
OCIO=/opt/Nuke17.0v1/plugins/OCIOConfigs/configs/nuke-default/config.ocio \
QT_PLUGIN_PATH=/usr/lib64/qt6/plugins \
QT_QPA_PLATFORM=xcb \
/home/npittas/Flux/build/Renderer/NatronRenderer -b /tmp/opencode/t078/t078_reload_render.py

python /tmp/opencode/t078/t078_analyze_pngs.py
```

Rendered proof artifacts:

- `/tmp/opencode/t078/render/base_0001.png` — visible glyphs spelling `Flux Motion Text`.
- `/tmp/opencode/t078/render/text_changed_0001.png` — output changes when `text` changes.
- `/tmp/opencode/t078/render/color_changed_0001.png` — output changes when `fillColor` changes.
- `/tmp/opencode/t078/render/size_changed_0001.png` — output changes when `fontSize` changes.
- `/tmp/opencode/t078/render/reloaded_0001.png` — reload render matches saved base state.
- `/tmp/opencode/t078/t078_flux_motion_text.ntp` — save/reload proof project.

Analyzer marker:

```text
T078_REAL_GLYPH_RENDER_OK
```

### Review

- Oracle review initially found an RGB component-path caveat; RGB now multiplies by glyph alpha like RGBA.
- Final Oracle verdict applied only to narrow renderer mechanics. It is not product acceptance and does not permit marking this task done.

### Deferred to Follow-up Tasks

- Timeline/menu creation path migration from legacy `FluxText` to `FluxMotionText`.
- UI font picker/panel shortcuts and multi-selected new text layer edits.
- Text layout controls beyond font/size/fill.
- Dynamic animator stack UI and per-element animator evaluation.
- Renderer caching/performance tuning for repeated text renders.
