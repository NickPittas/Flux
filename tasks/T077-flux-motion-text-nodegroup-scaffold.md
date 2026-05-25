# T077 — FluxMotionText nodegroup scaffold

Status: BLOCKED  
Phase: P7 — Shapes + Text  
Started: 2026-05-25  
Completed: —  
Owner: opencode

> Completion revoked 2026-05-25: previous create/render/reload proof was headless and did not prove actual UI creation, viewer display, app-originated render behavior, font UI, or text animator UX. This is rejected scaffold evidence, not accepted product work. Recovery source of truth: `tasks/FLUX_MOTION_TEXT_RECOVERY_SOURCE_OF_TRUTH.md`.

## Goal

Create the first Flux-owned motion-text nodegroup on top of the T075/T076 `net.flux.openfx.TextRender` provider, without touching legacy `plugins/FluxText.py`.

The nodegroup is the future layer-facing boundary for AE-style motion text. It must stay separate from Text v1 so current text behavior remains available while the new native text system develops.

## Scope

- Add a new PyPlug/nodegroup with plugin ID `net.sf.openfx.FluxMotionText`.
- Internal graph:
  ```text
  TextRender -> FrameRange -> TimeOffset -> Grade -> Output
  ```
- Promote/alias initial stable group controls:
  - `text`
  - `fillColor`
  - `frameRange`
  - `timeOffset`
  - `before`
  - `after`
  - `opacity`
  - `blendingMode`
- Add dynamic animator storage scaffolding:
  - schema/version metadata,
  - stack/order metadata,
  - no fixed animator slots,
  - runtime-added animator groups can be keyframed and persisted.
- Keep `plugins/FluxText.py` untouched.

## Non-goals

- No timeline UI creation path yet.
- No Text menu replacement yet.
- No real glyph layout/rendering yet.
- No animator evaluation in `TextRender` yet.
- No fixed/reduced animator slots.

## Validation

- Python compile of the new PyPlug passes.
- Renderer discovers and creates `net.sf.openfx.FluxMotionText`.
- Internal `TextRender1`, `FrameRange1`, `TimeOffset1`, `Grade1`, and `Output1` exist.
- Promoted group params exist and aliases work for renderer/frame/opacity controls.
- Basic render through `WritePNG` produces a visible RGBA image.
- Dynamic runtime animator groups with selector/target params can be added, keyframed, saved, reloaded, and verified.
- Legacy `plugins/FluxText.py` remains unchanged.

## Rejected Scaffold Evidence — Not Product Acceptance

The previous completion claim is revoked. The scaffold exists, but it is rejected as product work: it has no usable font picker, no Add Animator UI, no visible animator stack, no range selectors, no renderer evaluation of animator data, and no canonical GUI workflow proof.

### Implemented

- Added `plugins/FluxMotionText.py` with plugin ID `net.sf.openfx.FluxMotionText`.
- Internal graph:
  ```text
  TextRender1 -> FrameRange1 -> TimeOffset1 -> Grade1 -> Output1
  ```
- Promoted/aliased initial controls:
  - `text` -> `TextRender1.text`
  - `fillColor` -> `TextRender1.fillColor`
  - `frameRange` -> `FrameRange1.frameRange`
  - `timeOffset` -> `TimeOffset1.timeOffset`
  - `before` / `after` -> `FrameRange1`
  - `opacity` -> `Grade1.multiply`
- Added hidden dynamic animator stack metadata:
  - `motionTextSchemaVersion`
  - `animatorNextId`
  - `animatorOrder`
  - `animatorStackJson`
- Updated `.gitignore` so `plugins/FluxMotionText.py` is not hidden by the broad `plugins/*` artifact ignore rule.
- Updated Linux setup PyPlug deployment inventory to include `FluxMotionText.py`.
- Fixed headless Python PyPlug creation for all PyPlugs by passing the just-created group container to `createInstance` via `app.getNode(...)` instead of relying on pre-activation `app.<nodeName>` auto-declaration.

### Validation Evidence

Syntax/deploy:

```bash
python -m py_compile plugins/FluxMotionText.py
bash -n tools/linux/flux-linux-setup.sh
FLUX_USER_PYPLUG_DIR=/tmp/opencode/t077/home/.Natron/PyPlugs \
FLUX_USER_OFX_DIR=/tmp/opencode/t077/home/.OFX/Plugins \
FLUX_OFX_CACHE_DIR=/tmp/opencode/t077/home/.cache/INRIA/Natron/OFXLoadCache \
FLUX_LAUNCHER_PATH=/tmp/opencode/t077/bin/flux \
./tools/linux/flux-linux-setup.sh --deploy-extras --clear-ofx-cache --no-check --force
```

Headless PyPlug creation regression proof:

- Before the `AppInstance.cpp` fix, `app.createNode(...)` failed for all Flux PyPlugs with `app has no attribute FluxLayer1` / `FluxSolid1` / `FluxText1` / `FluxMotionText1`.
- After the fix, Renderer created all four successfully:
  - `net.sf.openfx.FluxLayer`
  - `net.sf.openfx.FluxSolid`
  - `net.sf.openfx.FluxText`
  - `net.sf.openfx.FluxMotionText`

Create/render/save proof:

```bash
HOME=/tmp/opencode/t077/home \
NATRON_DISK_CACHE_PATH=/tmp/opencode/t077/cache-create \
OFX_PLUGIN_PATH=/tmp/opencode/t077/home/.OFX/Plugins \
NATRON_PLUGIN_PATH=/tmp/opencode/t077/home/.Natron/PyPlugs:/home/npittas/Flux/Gui/Resources/PyPlugs:/home/npittas/Flux/plugins/natron-plugins \
XDG_CACHE_HOME=/tmp/opencode/t077/xdg-cache-create \
OCIO=/opt/Nuke17.0v1/plugins/OCIOConfigs/configs/nuke-default/config.ocio \
QT_PLUGIN_PATH=/usr/lib64/qt6/plugins \
QT_QPA_PLATFORM=xcb \
/home/npittas/Flux/build/Renderer/NatronRenderer -b /tmp/opencode/t077/t077_create_render_save.py
```

Results:

- Created `FluxMotionText1`.
- Verified internal `TextRender1`, `FrameRange1`, `TimeOffset1`, `Grade1`, `Output1`.
- Verified group params exist.
- Added two runtime dynamic animator groups with selector/target params.
- Each selector/target animated param had two keys.
- Render proof: `/tmp/opencode/t077/render/flux_motion_text_0001.png`.
- Image check: RGBA `1920x1080`, `777600` nonzero component values.
- Saved proof project: `/tmp/opencode/t077/t077_flux_motion_text.ntp`.

Reload proof:

```bash
HOME=/tmp/opencode/t077/home \
NATRON_DISK_CACHE_PATH=/tmp/opencode/t077/cache-reload \
OFX_PLUGIN_PATH=/tmp/opencode/t077/home/.OFX/Plugins \
NATRON_PLUGIN_PATH=/tmp/opencode/t077/home/.Natron/PyPlugs:/home/npittas/Flux/Gui/Resources/PyPlugs:/home/npittas/Flux/plugins/natron-plugins \
XDG_CACHE_HOME=/tmp/opencode/t077/xdg-cache-reload \
OCIO=/opt/Nuke17.0v1/plugins/OCIOConfigs/configs/nuke-default/config.ocio \
QT_PLUGIN_PATH=/usr/lib64/qt6/plugins \
QT_QPA_PLATFORM=xcb \
/home/npittas/Flux/build/Renderer/NatronRenderer -b /tmp/opencode/t077/t077_reload_verify.py
```

Results:

- Reloaded `FluxMotionText1` and internal `TextRender1`.
- Dynamic params persisted.
- Key counts persisted for selector/target params.
- Marker: `T077_DYNAMIC_STORAGE_RELOAD_OK`.

Build validation:

```bash
cmake --build /home/npittas/Flux/build --target NatronRenderer -- -j$(nproc)
cmake --build /home/npittas/Flux/build --target Natron -- -j$(nproc)
```

Both passed.

### Review

- Oracle reviewed the PyPlug creation blocker and recommended the `app.getNode(...)` route.
- Final Oracle review verdict applied only to narrow scaffold mechanics. It is not product acceptance and does not permit marking this task done.

### Deferred to Follow-up Tasks

- Timeline/menu UI integration for creating motion text layers.
- Multi-selected text-layer panel editing.
- Real glyph layout/rendering in `TextRender`.
- Animator stack UI and add/remove/reorder operations.
- Animator evaluation/mirroring into renderer parameters.
- `blendingMode` is scaffolded for future merge integration but is not wired in T077.
