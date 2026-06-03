# T084 — Plugin payload discovery/deploy completeness

Status: DONE
Owner: forge
Started: 2026-05-26
Completed: 2026-05-26

## Goal

Treat the repository as the source of truth for Flux plugin payloads during Linux
bootstrap/deploy. The installer must not rely only on a stale hardcoded plugin
list when the repo contains additional deployable plugins.

## Scope

- Discover and deploy every top-level `plugins/*.py` PyPlug.
- Discover and deploy every `.ofx.bundle` under:
  - `plugins/`
  - `plugins/ofx-extras/`
- Keep the required minimum plugin set explicit so missing core payloads still
  fail bootstrap/deploy instead of producing a partial install.
- Install to an XDG-derived Flux prefix instead of generic Natron user plugin
  profiles, with `--install-prefix`/`FLUX_INSTALL_PREFIX` and
  `--bin-dir`/`FLUX_BIN_DIR` overrides.
- Copy bundled/community PyPlug trees into the install prefix:
  - `Gui/Resources/PyPlugs/`
  - `plugins/natron-plugins/`
- Do not recursively copy random source trees or `.git` internals into the user
  profile; exclude VCS, `__pycache__`, `*.pyc`, and temp files.
- Install app binaries under `$FLUX_INSTALL_PREFIX/bin`, launch that installed
  binary, and support manifest-driven uninstall.

## Implementation Notes

- `tools/linux/flux-linux-setup.sh` now builds deploy lists from the current repo
  at runtime via `discover_plugin_payloads` and installs them under the Flux
  install prefix.
- Required Flux PyPlugs remain listed as `REQUIRED_PYPLUG_FILES`.
- Required OFX bundles remain listed in `OFX_CORE_BUNDLES` and the initial
  `OFX_EXTRA_BUNDLES`; discovered additional OFX bundles are appended without
  duplicating core bundles.
- Unknown OFX bundles use the conventional binary path
  `Contents/Linux-x86-64/<bundle-name>.ofx` for ldd/check validation.
- The generated launcher now executes `$FLUX_INSTALL_PREFIX/bin/flux`, exports
  only installed plugin paths, and never executes the build tree.
- `install-manifest.txt` records installed files for safe interactive uninstall.

## Validation

```text
bash -n tools/linux/flux-linux-setup.sh
rm -rf /tmp/flux-t084-deploy
mkdir -p /tmp/flux-t084-deploy/home /tmp/flux-install-test/share/Flux/Plugins/OFX /tmp/flux-t084-deploy/cache
FLUX_INSTALL_PREFIX=/tmp/flux-install-test/share/Flux \
FLUX_BIN_DIR=/tmp/flux-install-test/bin \
FLUX_OFX_CACHE_DIR=/tmp/flux-install-test/cache/OFXLoadCache \
# obsolete pre-interactive deploy command removed; use tools/linux/flux-linux-setup.sh and choose Install/repair runtime only
find /tmp/flux-install-test/share/Flux/Plugins/PyPlugs -maxdepth 1 -type f -name '*.py' -printf '%f\\n' | sort
find /tmp/flux-install-test/share/Flux/Plugins/OFX -maxdepth 1 -type d -name '*.ofx.bundle' -printf '%f\\n' | sort
find /tmp/flux-install-test/share/Flux/Plugins -type l -print
FLUX_INSTALL_PREFIX=/tmp/flux-install-test/share/Flux FLUX_BIN_DIR=/tmp/flux-install-test/bin \
  # obsolete pre-interactive uninstall command removed; use tools/linux/flux-linux-setup.sh and choose Uninstall Flux
git diff --check
```

Observed deployed PyPlugs:

```text
FluxLayer.py
FluxMotionText.py
FluxSolid.py
FluxText.py
```

Observed deployed OFX bundles:

```text
CImg.ofx.bundle
FluxTextRender.ofx.bundle
IO.ofx.bundle
Magick.ofx.bundle
Misc.ofx.bundle
ResolveMath.ofx.bundle
SeExpr.ofx.bundle
Text.ofx.bundle
```

## Closure

Nick accepted moving T084 to DONE on 2026-05-26. The deploy/discovery
implementation and local validation above are the recorded closure evidence for
this tracking task.
