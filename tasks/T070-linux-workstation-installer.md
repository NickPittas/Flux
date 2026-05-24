# T070 — Linux workstation installer/checker

## Status

`TESTING`

## Goal

Make a clean Linux workstation reproducibly ready to build and run Flux with the
current Python, PyPlug, and OpenFX requirements.

## Scope

- Document the canonical Fedora-first Linux workstation setup.
- Provide a non-destructive checker for required packages, build output,
  PyPlugs, OFX bundles, OFX cache IDs, and native loader dependencies.
- Provide explicit install actions for Fedora dependencies, Flux PyPlugs, OFX
  bundles, user launcher creation, scoped OFX cache clearing, and transferable
  OFX extras staging.
- Provide `--bootstrap` as the one-command Fedora path after clone: submodules,
  RPM Fusion enablement, repo/package verification, dependency install,
  configure, build, deploy, launcher, cache clear, and OFX `ldd` validation.
- Keep sudo opt-in only; no automatic privileged install.

## Deliverables

- `INSTALL_FLUX_LINUX.md`
- `tools/linux/flux-linux-setup.sh`
- `plugins/ofx-extras/` Linux x86-64 restored OFX bundle artifact set
- `plugins/natron-plugins` registered as the community PyPlug submodule

## Current Linux target

- Fedora 44
- Qt6 / PySide6 / Shiboken6
- Python 3.14
- RPM Fusion FFmpeg stack
- User PyPlug path: `~/.Natron/PyPlugs/`
- User OpenFX path: `~/.OFX/Plugins/`
- Scoped OFX cache: `~/.cache/INRIA/Natron/OFXLoadCache/`

## Validation so far

Validation performed without sudo or changing the real workstation install:

```text
bash -n tools/linux/flux-linux-setup.sh
./tools/linux/flux-linux-setup.sh --help
./tools/linux/flux-linux-setup.sh --stage-extras /tmp/opencode/flux-linux-ofx-extras-test --no-check
FLUX_USER_PYPLUG_DIR=/tmp/opencode/flux-test-home/PyPlugs \
FLUX_USER_OFX_DIR=/tmp/opencode/flux-test-home/OFX \
FLUX_OFX_CACHE_DIR=/tmp/opencode/flux-test-home/OFXLoadCache \
FLUX_LAUNCHER_PATH=/tmp/opencode/flux-test-home/bin/flux \
./tools/linux/flux-linux-setup.sh \
  --deploy-extras \
  --extras-source /tmp/opencode/flux-linux-ofx-extras-test \
  --install-launcher \
  --validate-ldd \
  --no-check \
  --force
```

All staged/deployed OFX binaries passed `ldd` in the temporary install root:

- `IO.ofx`
- `Misc.ofx`
- `CImg.ofx`
- `SeExpr.ofx`
- `Text.ofx`
- `Magick.ofx`
- `ResolveMath.ofx`

Oracle re-review verdict after command-flow and missing-artifact fixes: `SHIP`.

Repository-local extras were then staged under `plugins/ofx-extras/` so a Linux
checkout has the restored OFX bundle set without relying on Nick's user
`~/.OFX/Plugins` directory.

Repository-local deploy validation also passed using the default
`plugins/ofx-extras/` source, with all deployed OFX binaries `ldd` clean in a
temporary install root.

Installer was then simplified for user-facing setup: after clone, Fedora users
run `./tools/linux/flux-linux-setup.sh --bootstrap`
instead of manually running separate CMake configure/build/deploy commands.

Additional bootstrap-path validation:

```text
./tools/linux/flux-linux-setup.sh --verify-fedora-repos
FLUX_BUILD_DIR=/tmp/opencode/flux-build-verify FLUX_BUILD_JOBS=8 \
  ./tools/linux/flux-linux-setup.sh --configure --build
FLUX_BUILD_DIR=/tmp/opencode/flux-build-verify \
FLUX_USER_PYPLUG_DIR=/tmp/opencode/flux-bootstrap-test-2/PyPlugs \
FLUX_USER_OFX_DIR=/tmp/opencode/flux-bootstrap-test-2/OFX \
FLUX_OFX_CACHE_DIR=/tmp/opencode/flux-bootstrap-test-2/OFXLoadCache \
FLUX_LAUNCHER_PATH=/tmp/opencode/flux-bootstrap-test-2/bin/flux \
  ./tools/linux/flux-linux-setup.sh \
    --deploy-extras \
    --install-launcher \
    --clear-ofx-cache \
    --validate-ldd \
    --force
bash -n /tmp/opencode/flux-bootstrap-test-2/bin/flux
```

Results: Fedora repo package availability passed; clean alternate build dir
configured and built `Natron`; temp runtime deployment succeeded; all OFX
binaries were `ldd` clean; generated launcher syntax passed.

## Remaining work

- Run the documented sequence on a clean Fedora workstation or VM and move this
  task to `DONE` only after build, deploy, launch, OFX cache regeneration, and
  PyPlug creation validation pass there.
- Ubuntu/Debian package mapping and validation.
- Arch package mapping and validation.
- True binary packaging/AppImage or distro package.
- macOS and Windows equivalents.
