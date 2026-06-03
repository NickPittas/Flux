# T076 — openfx-flux deploy integration

Status: BLOCKED  
Phase: P7 — Shapes + Text  
Started: 2026-05-25  
Completed: —  
Owner: opencode

> Completion revoked 2026-05-25: deploy/discovery validation did not prove the canonical user GUI runtime could discover `net.flux.openfx.TextRender`. This is groundwork/prototype evidence, not accepted product work. Recovery source of truth: `tasks/FLUX_MOTION_TEXT_RECOVERY_SOURCE_OF_TRUTH.md`.

## Goal

Make the Flux-owned OFX renderer path from T075 part of the normal Linux build/deploy workflow.

`net.flux.openfx.TextRender` should not remain a manual `/tmp` validation artifact. A checkout should be able to build, install, deploy, and cold-cache validate `FluxTextRender.ofx.bundle` through `tools/linux/flux-linux-setup.sh`.

## Scope

- Build `openfx-flux/` as a separate CMake OFX project during the interactive build/full setup actions.
- Install the generated `FluxTextRender.ofx.bundle` into the repository-local `plugins/` runtime bundle area.
- Deploy it with the existing OFX bundle deploy path into `~/.OFX/Plugins/` or the configured `FLUX_USER_OFX_DIR`.
- Include `net.flux.openfx.TextRender` in runtime validation expectations.
- Add a cold-cache Renderer validation path that proves discovery and node creation from the deployed bundle.
- Keep legacy `plugins/FluxText.py` untouched.

## Non-goals

- No `FluxMotionText` nodegroup yet.
- No UI action changes yet.
- No real glyph rendering yet.
- No git staging/commit unless Nick explicitly asks.

## Validation

- `bash -n tools/linux/flux-linux-setup.sh`
- historical installer help check (obsolete argument interface removed)
- Alternate-root configure/build/deploy validation with temporary `FLUX_*` paths.
- Interactive validation reports `FluxTextRender.ofx` clean.
- Interactive OFX discovery validation finds and creates `net.flux.openfx.TextRender` with a cold isolated cache.
- Main Flux build remains valid:
  ```bash
  cmake --build /home/npittas/Flux/build --target Natron -- -j$(nproc)
  ```

## Narrow Implementation Evidence — Not Product Acceptance

The previous completion claim is revoked. The work below proves only a narrow build/deploy/discovery path. It does not prove canonical user GUI runtime behavior, FluxMotionText product UX, font UI, text animator UI, or AE-style per-element animation.

### Implemented

- The interactive build action now builds:
  - `Natron`
  - `NatronRenderer`
  - `openfx-flux` / `FluxTextRender.ofx.bundle`
- `FluxTextRender.ofx.bundle` installs into repository-local `plugins/` via `cmake --install`.
- `FluxTextRender.ofx.bundle` is included in:
  - core OFX bundle inventory,
  - user deployment,
  - staging,
  - `ldd` validation,
  - runtime expected ID checks.
- Added OFX discovery validation:
  - creates an isolated cold-cache validation home,
  - symlinks only the deployed/built `FluxTextRender.ofx.bundle` into a temp OFX path,
  - uses `NatronRenderer` to discover and create `net.flux.openfx.TextRender`,
  - fails hard if `NatronRenderer` is missing.
- Updated `plugins/ofx-extras/README.md` to document source-built `FluxTextRender.ofx.bundle`.

### Validation Evidence

Syntax/help/build:

```bash
bash -n tools/linux/flux-linux-setup.sh
# obsolete pre-interactive installer commands removed; use tools/linux/flux-linux-setup.sh and choose Build Flux from source
```

Build result:

- `Natron` built.
- `NatronRenderer` built.
- `FluxTextRender.ofx.bundle` built and installed to `plugins/FluxTextRender.ofx.bundle`.

Temporary runtime deploy validation:

```bash
FLUX_USER_PYPLUG_DIR=/tmp/opencode/t076-runtime/PyPlugs \
FLUX_USER_OFX_DIR=/tmp/opencode/t076-runtime/OFX \
FLUX_OFX_CACHE_DIR=/tmp/opencode/t076-runtime/OFXLoadCache \
FLUX_LAUNCHER_PATH=/tmp/opencode/t076-runtime/bin/flux \
# obsolete pre-interactive deploy command removed; use tools/linux/flux-linux-setup.sh and choose Install/repair runtime only, then Validate installation
```

Results:

- deployed PyPlugs into temp root,
- deployed all core/extras OFX bundles including `FluxTextRender.ofx.bundle`,
- `ldd` clean for all deployed OFX binaries including `FluxTextRender.ofx`,
- cold-cache discovery/create passed for `net.flux.openfx.TextRender`,
- generated launcher passed `bash -n`.

Staging validation:

```bash
# obsolete pre-interactive staging command removed; current installer is interactive-only
```

Result: staged `FluxTextRender.ofx.bundle` alongside IO/Misc/extras.

Main build validation:

```bash
cmake --build /home/npittas/Flux/build --target Natron -- -j$(nproc)
```

Result: passed.

### Review

- Oracle review initially flagged that discovery validation could silently skip if `NatronRenderer` was missing.
- Fixed by building `NatronRenderer` during the interactive build action and failing hard if discovery validation cannot find Renderer.
- Final Oracle verdict applied only to narrow deploy/discovery mechanics. It is not product acceptance and does not permit marking this task done.

### Notes

- `NatronRenderer` currently exits with status `1` after the Python validation marker because the validation project has no writer node. The setup script treats the explicit discovery/create marker as success and logs a warning.
- Discovery validation intentionally isolates only `FluxTextRender.ofx.bundle` to avoid unrelated third-party OFX scan crashes while still proving the deployed/built Flux bundle path.
