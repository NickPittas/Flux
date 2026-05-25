# T075 — Flux-owned OFX text boundary spike

Status: BLOCKED  
Phase: P7 — Shapes + Text  
Started: 2026-05-25  
Completed: —  
Owner: opencode

> Completion revoked 2026-05-25: previous validation was implementation/headless only. This is groundwork/prototype evidence, not accepted product work. Recovery source of truth: `tasks/FLUX_MOTION_TEXT_RECOVERY_SOURCE_OF_TRUTH.md`.

## Goal

Prove a dedicated Flux-owned OpenFX source/build path for the new motion-graphics text system before any renderer or UI work depends on it.

The approved direction is a **new text nodegroup** and **new Flux OFX provider**, not an upgrade of `plugins/FluxText.py`.

## Approved Architecture Constraints

- Create a dedicated Flux OFX source path, tentatively `openfx-flux/`.
- Use existing OFX projects only as references.
- Do not mutate or replace `plugins/FluxText.py`.
- The old `net.sf.openfx.FluxText` remains legacy/reference.
- The new nodegroup identity is tentative: `net.sf.openfx.FluxMotionText`.
- The new render provider identity is tentative: `net.flux.openfx.TextRender`.
- One text layer remains one nodegroup/layer, never one node per character/word/line.
- Dynamic/layered text animators are required. Fixed animator slots are not approved.

## Hard Stop Rule

If the dedicated Flux OFX path cannot fully support the approved plan, stop and ask Nick before switching architecture.

Blockers include inability to support:

- dynamic/layered animator groups,
- keyframeable selector/target controls,
- project persistence,
- timeline row visibility,
- selected-layer panel binding,
- render performance compatible with motion graphics text.

Do **not** silently reduce scope. Do **not** fall back to fixed animator slots.

## Deliverables

1. Create/prove a dedicated Flux OFX source path, tentatively `openfx-flux/`.
2. Prove CMake can build a Flux OFX bundle without relying on prebuilt binary extras.
3. Prove install/deploy/discovery path for the new Flux OFX bundle.
4. Add a minimal `net.flux.openfx.TextRender` generator prototype that renders a simple visible image/text placeholder.
5. Verify generator basics:
   - Region of Definition,
   - project-format coordinates,
   - render-window clipping,
   - premultiplied alpha,
   - FP32 output path,
   - pixel aspect handling,
   - thread-safety assumptions.
6. Verify the new text layer can be represented in `FluxLayer` without touching legacy Text v1.
7. Verify whether dynamic animator controls can be added/keyframed/serialized through the chosen boundary.

## Validation

- Build passes for the new OFX target.
- Flux still builds:
  ```bash
  cmake --build /home/npittas/Flux/build --target Natron -- -j$(nproc)
  ```
- Flux launches:
  ```bash
  QT_PLUGIN_PATH=/usr/lib64/qt6/plugins QT_QPA_PLATFORM=xcb /home/npittas/Flux/build/App/Natron
  ```
- New OFX bundle is discoverable by Natron after cache clear.
- Minimal node can be created manually or via harness.
- Minimal node renders a valid premultiplied RGBA image in the viewer/export path.
- Existing Text v1 still adds/renders exactly as before.
- Dynamic animator storage path is proven, or implementation stops for architecture reassessment.

## Initial Implementation Notes

Reference but do not copy blindly:

- `openfx-misc/CMakeLists.txt` — existing standalone OFX build/bundle install patterns.
- `openfx-misc/Constant/Constant.cpp` — simple generator reference.
- `openfx-misc/SupportExt/ofxsGenerator.*` — generator helper infrastructure.
- `tools/linux/flux-linux-setup.sh` — OFX deploy/cache-clear patterns.

Important correction from plan review: any suggestion to replace `plugins/FluxText.py` or redirect the current `net.sf.openfx.FluxText` path is out of scope for T075.

## Original Acceptance Criteria — Superseded by Recovery Source of Truth

Original T075 acceptance was written as:

1. The dedicated Flux OFX path is proven viable for the approved architecture and documented, or
2. A hard blocker is proven and the task is marked BLOCKED with a clear architecture-switch recommendation for Nick.

## Narrow Implementation Evidence — Not Product Acceptance

The previous completion claim is revoked. The work below proves only a narrow dedicated Flux OFX path and placeholder/basic provider behavior in isolated/headless contexts. It does not prove the requested FluxMotionText product, the canonical GUI workflow, font UI, text animator UI, or AE-style per-element animation.

### Implemented

- Added dedicated Flux-owned OFX source/build path:
  - `openfx-flux/CMakeLists.txt`
  - `openfx-flux/TextRender/TextRender.cpp`
  - `openfx-flux/TextRender/Info.plist`
- Added minimal native generator provider:
  - plugin ID: `net.flux.openfx.TextRender`
  - bundle: `FluxTextRender.ofx.bundle`
  - params: `text`, `fillColor`
- Kept legacy `plugins/FluxText.py` untouched.
- Fixed the prototype placeholder to compute geometry from destination/project bounds and use the render window only for clipping, so tile-local `procWindow` does not define the graphic.

### Validation Commands

Configure/build/install prototype:

```bash
cmake -S "openfx-flux" -B "openfx-flux/build" -DCMAKE_BUILD_TYPE=Release
cmake --build "openfx-flux/build" -- -j$(nproc)
cmake --install "openfx-flux/build" --prefix "/tmp/opencode/t075-ofx-install"
```

Main app build:

```bash
cmake --build /home/npittas/Flux/build --target Natron -- -j$(nproc)
```

Isolated discovery/create validation:

```bash
HOME=/tmp/opencode/ofx-validation/home \
NATRON_DISK_CACHE_PATH=/tmp/opencode/ofx-validation/cache \
OFX_PLUGIN_PATH=/tmp/opencode/ofx-validation/home/.OFX/Plugins \
XDG_CACHE_HOME=/tmp/opencode/ofx-validation/cache \
OCIO=/opt/Nuke17.0v1/plugins/OCIOConfigs/configs/nuke-default/config.ocio \
QT_PLUGIN_PATH=/usr/lib64/qt6/plugins \
QT_QPA_PLATFORM=xcb \
/home/npittas/Flux/build/Renderer/NatronRenderer -b /tmp/opencode/t075_validate_flux_text_render.py
```

Render validation:

```bash
HOME=/tmp/opencode/ofx-validation/home \
NATRON_DISK_CACHE_PATH=/tmp/opencode/ofx-validation/cache \
OFX_PLUGIN_PATH=/tmp/opencode/ofx-validation/home/.OFX/Plugins \
XDG_CACHE_HOME=/tmp/opencode/ofx-validation/cache \
OCIO=/opt/Nuke17.0v1/plugins/OCIOConfigs/configs/nuke-default/config.ocio \
QT_PLUGIN_PATH=/usr/lib64/qt6/plugins \
QT_QPA_PLATFORM=xcb \
/home/npittas/Flux/build/Renderer/NatronRenderer -b /tmp/opencode/t075_render_flux_text_render.py
```

Dynamic animator storage validation:

```bash
HOME=/tmp/opencode/ofx-validation/home \
NATRON_DISK_CACHE_PATH=/tmp/opencode/ofx-validation/cache \
OFX_PLUGIN_PATH=/tmp/opencode/ofx-validation/home/.OFX/Plugins \
XDG_CACHE_HOME=/tmp/opencode/ofx-validation/cache \
QT_PLUGIN_PATH=/usr/lib64/qt6/plugins \
QT_QPA_PLATFORM=xcb \
/home/npittas/Flux/build/Renderer/NatronRenderer -b /tmp/opencode/t075_dynamic_animator_storage_create.py

HOME=/tmp/opencode/ofx-validation/home \
NATRON_DISK_CACHE_PATH=/tmp/opencode/ofx-validation/cache \
OFX_PLUGIN_PATH=/tmp/opencode/ofx-validation/home/.OFX/Plugins \
XDG_CACHE_HOME=/tmp/opencode/ofx-validation/cache \
QT_PLUGIN_PATH=/usr/lib64/qt6/plugins \
QT_QPA_PLATFORM=xcb \
/home/npittas/Flux/build/Renderer/NatronRenderer -b /tmp/opencode/t075_dynamic_animator_storage_load.py
```

### Evidence

- Natron isolated discovery output contained:
  - `OpenFX: loading net.flux.openfx.TextRender v0.1`
  - `T075_PLUGIN_MATCHES: ['net.flux.openfx.TextRender']`
  - `T075_CREATE_TEXT_RENDER_OK`
- Render proof:
  - `/tmp/opencode/t075-render/flux_text_render_0001.png`
  - RGBA, `1920x1080`, `194400` nonzero pixels after project-space geometry fix.
- Dynamic animator storage proof:
  - project: `/tmp/opencode/t075-render/t075_animator_storage.ntp`
  - two dynamic animator groups represented by persistent user params on a wrapper Group node
  - selector/target params keyframed with 2 keys each
  - save/reload preserved params, persistence, internal `net.flux.openfx.TextRender`, and key counts.

### Review

- Oracle review initially flagged tile-local placeholder geometry.
- The geometry fix was applied and re-reviewed.
- Final review verdict applied only to the narrow implementation mechanics. It is not product acceptance and does not permit marking this task done.

### Deferred to Follow-up Tasks

- Real glyph layout/rendering is not part of T075.
- `text` is declared but not consumed by the prototype renderer yet.
- Dynamic animator storage is proven at the nodegroup/user-param boundary; later tasks must bridge/mirror that dynamic stack into renderer evaluation.
- Render-scale and pixel-aspect behavior must be defined explicitly in the real text renderer.
