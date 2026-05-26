# T070 — Linux workstation installer/checker

## Status

`DONE`

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
  configure, build, embedded-Python runtime dependency bootstrap, deploy,
  launcher, cache clear, and OFX `ldd` validation.
- Keep sudo opt-in only; no automatic privileged install.
- Keep user-facing paths portable. Use `$FLUX_ROOT`, `$BUILD_DIR`,
  `$PLUGIN_PREFIX`, `$OFX_USER_PLUGIN_DIR`, `$HOME`, and `$XDG_CACHE_HOME`
  instead of developer-specific absolute paths.

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
- User PyPlug path: `$HOME/.Natron/PyPlugs/`
- User OpenFX path: `$OFX_USER_PLUGIN_DIR` (default `$HOME/.OFX/Plugins/`)
- Scoped OFX cache: `${XDG_CACHE_HOME:-$HOME/.cache}/INRIA/Natron/OFXLoadCache/`
- Plugin prefix: `$PLUGIN_PREFIX` (default `$FLUX_ROOT/plugins`)
- Build directory: `$BUILD_DIR` (default `$FLUX_ROOT/build`)

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
checkout has the restored OFX bundle set without relying on any developer's user
OpenFX plugin directory.

Repository-local deploy validation also passed using the default
`plugins/ofx-extras/` source, with all deployed OFX binaries `ldd` clean in a
temporary install root.

Path portability cleanup updated the installer to derive `$FLUX_ROOT` from the
script location, support `$BUILD_DIR`, `$PLUGIN_PREFIX`, `$OFX_USER_PLUGIN_DIR`,
and `$XDG_CACHE_HOME`, and bootstrap `qtpy`/`packaging` into `$BUILD_DIR/Plugins`
for Natron's embedded Python path.

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

Additional safe validation on 2026-05-26, performed without sudo on the
production host and without using the existing production install:

```text
bash -n tools/linux/flux-linux-setup.sh
./tools/linux/flux-linux-setup.sh --help
command -v podman distrobox docker toolbox systemd-nspawn
podman info --format '{{.Host.OCIRuntime.Name}} rootless={{.Host.Security.Rootless}}'
podman run --rm fedora:44 cat /etc/fedora-release
podman run --rm -v "$PWD:/workspace:ro,Z" -w /workspace fedora:44 \
  bash -lc './tools/linux/flux-linux-setup.sh --help >/tmp/help && \
  bash -n tools/linux/flux-linux-setup.sh && \
  ./tools/linux/flux-linux-setup.sh --verify-fedora-repos'
podman run --rm -v "$PWD:/workspace:ro,Z" -w /workspace fedora:44 \
  bash -lc './tools/linux/flux-linux-setup.sh --bootstrap --no-check'
podman run --rm -v "$PWD:/workspace:ro,Z" -w /workspace fedora:44 \
  bash -lc 'dnf -y install \
  https://download1.rpmfusion.org/free/fedora/rpmfusion-free-release-$(rpm -E %fedora).noarch.rpm \
  >/tmp/dnf-rpmfusion.log && \
  ./tools/linux/flux-linux-setup.sh --verify-fedora-repos'
```

Environment notes and results:

- Rootless Podman is available (`crun rootless=true`), and `toolbox` /
  `systemd-nspawn` are installed; `distrobox` and `docker` are not installed.
- Pulling and running `fedora:44` under rootless Podman succeeded and reported
  `Fedora release 44 (Forty Four)`.
- Script syntax and help passed both on the host and inside the clean Fedora 44
  container with the repository mounted read-only.
- Fedora repo verification in the untouched Fedora 44 container failed only for
  `ffmpeg-devel`, confirming the expected RPM Fusion dependency.
- After enabling RPM Fusion free inside the disposable container, Fedora package
  availability verification passed.
- A direct read-only container `--bootstrap --no-check` attempt stopped at
  `git is required to update submodules`; this is useful container knowledge,
  not a production-host failure. The minimal Fedora container does not include
  `git`/`sudo`, while a real post-clone workstation necessarily has `git` and
  the helper intentionally uses `sudo dnf` for privileged installs.

A fresh temp-root deploy/stage validation also passed:

```text
tmp=$(mktemp -d /tmp/flux-t070-XXXXXX)
./tools/linux/flux-linux-setup.sh --stage-extras "$tmp/extras" --no-check
FLUX_USER_PYPLUG_DIR="$tmp/home/PyPlugs" \
FLUX_USER_OFX_DIR="$tmp/home/OFX" \
FLUX_OFX_CACHE_DIR="$tmp/home/OFXLoadCache" \
FLUX_LAUNCHER_PATH="$tmp/home/bin/flux" \
./tools/linux/flux-linux-setup.sh \
  --deploy-extras \
  --extras-source "$tmp/extras" \
  --install-launcher \
  --clear-ofx-cache \
  --validate-ldd \
  --no-check \
  --force
bash -n "$tmp/home/bin/flux"
```

Results: all eight staged OFX bundles deployed to the temporary OFX root;
`FluxLayer.py`, `FluxSolid.py`, `FluxText.py`, and `FluxMotionText.py` deployed
to the temporary PyPlug root; missing temp OFX cache was handled non-fatally;
the generated launcher passed shell syntax; and `ldd` was clean for
`IO.ofx`, `Misc.ofx`, `FluxTextRender.ofx`, `CImg.ofx`, `SeExpr.ofx`,
`Text.ofx`, `Magick.ofx`, and `ResolveMath.ofx`.

Fresh-install Fedora 44 sandbox validation on 2026-05-26:

```text
podman exec -u root flux-t070-fedora44-gui bash -lc \
  'dnf -y install sudo git xauth mesa-demos procps-ng gdb'
podman exec flux-t070-fedora44-gui bash -lc \
  'sudo -n true && glxinfo -B | sed -n "1,12p"'
podman exec flux-t070-fedora44-gui bash -lc \
  'rm -rf /tmp/flux-t070-newinstall && \
   mkdir -p /tmp/flux-t070-newinstall/home \
            /tmp/flux-t070-newinstall/cache \
            /tmp/flux-t070-newinstall/build && \
   cd /workspace && \
   HOME=/tmp/flux-t070-newinstall/home \
   XDG_CACHE_HOME=/tmp/flux-t070-newinstall/cache \
   FLUX_BUILD_DIR=/tmp/flux-t070-newinstall/build \
   FLUX_BUILD_JOBS=8 \
   ./tools/linux/flux-linux-setup.sh --bootstrap --force'
```

Results and good-to-know notes:

- This was a fresh install into `/tmp/flux-t070-newinstall`: fresh `HOME`,
  fresh `XDG_CACHE_HOME`, fresh build directory, fresh user PyPlug and OFX
  roots. It did not reuse the production host install, cache, launcher, or
  build output.
- The Fedora sandbox had NVIDIA device nodes mounted, but `nvidia-smi` was not
  installed in the minimal image. `glxinfo -B` reported the NVIDIA PCI ID but
  fell back to llvmpipe with `failed to load driver: nvidia-drm`; this is a
  sandbox GPU plumbing limitation to document for GUI validation.
- `sudo` and `git` were installed inside the disposable sandbox only. No host
  packages were installed.
- Fresh `--bootstrap --force` configured and built `Natron`, `NatronRenderer`,
  and `FluxTextRender.ofx.bundle`; installed embedded Python deps; deployed
  `FluxLayer.py`, `FluxSolid.py`, `FluxText.py`, and `FluxMotionText.py`; and
  deployed all eight required OFX bundles into the fresh sandbox home.
- The first fresh bootstrap exposed missing runtime coverage for restored OFX
  extras: `SeExpr.ofx` needed bundled `seexpr-deps/lib` on
  `LD_LIBRARY_PATH`, and `Magick.ofx` needed bundled `magick-deps/lib` plus
  Fedora `ImageMagick-c++`, `libraqm`, and `liblqr-1`. The installer was fixed
  to install those Fedora runtime packages, include the bundled OFX dependency
  directories in the generated launcher, and run `ldd` with the same dependency
  paths.
- After the fix, a second fresh bootstrap passed with `ldd clean` for
  `IO.ofx`, `Misc.ofx`, `FluxTextRender.ofx`, `CImg.ofx`, `SeExpr.ofx`,
  `Text.ofx`, `Magick.ofx`, and `ResolveMath.ofx`, and cold-cache
  `net.flux.openfx.TextRender` discovery passed through `NatronRenderer`.

Launch/cache validation:

```text
HOME=/tmp/flux-t070-newinstall/home \
XDG_CACHE_HOME=/tmp/flux-t070-newinstall/cache \
LIBGL_ALWAYS_SOFTWARE=1 \
LD_LIBRARY_PATH=/tmp/flux-t070-newinstall/home/.OFX/Plugins/SeExpr.ofx.bundle/Contents/Linux-x86-64/seexpr-deps/lib:/tmp/flux-t070-newinstall/home/.OFX/Plugins/Magick.ofx.bundle/Contents/Linux-x86-64/magick-deps/lib \
gdb -batch -ex run --args /tmp/flux-t070-newinstall/build/App/Natron

cd /workspace && \
HOME=/tmp/flux-t070-newinstall/home \
XDG_CACHE_HOME=/tmp/flux-t070-newinstall/cache \
FLUX_BUILD_DIR=/tmp/flux-t070-newinstall/build \
./tools/linux/flux-linux-setup.sh --check --validate-ldd --validate-ofx-discovery
```

Results:

- Flux/Natron launched far enough in the fresh sandbox to generate
  `DiskCache`, `ViewerCache`, and `OFXLoadCache`. Nick observed the GUI working
  in the sandbox.
- A direct timed launch in the minimal Podman GUI container can abort with
  `terminate called without an active exception`; running under `gdb` kept the
  process alive long enough for GUI/cache validation. Treat this as a sandbox
  GUI/runtime quirk, not as a completed production packaging concern.
- Post-launch checker passed: Fedora package check passed, Flux binary exists,
  all OFX `ldd` checks passed, cold-cache `TextRender` discovery passed, and
  all expected restored OFX IDs were present in the runtime cache.

Additional guided-check UX update on 2026-05-26:

```text
bash -n tools/linux/flux-linux-setup.sh
./tools/linux/flux-linux-setup.sh --help
./tools/linux/flux-linux-setup.sh --check
./tools/linux/flux-linux-setup.sh --print-commands
./tools/linux/flux-linux-setup.sh --verify-fedora-repos --print-commands
```

Results: syntax passed; help documents `--print-commands`; the default/check
path remains non-mutating; missing Fedora dependencies/repository coverage now
prints exact RPM Fusion and `sudo dnf install -y --allowerasing ...` commands
plus helper alternatives (`--enable-rpmfusion`, `--install-deps`, `--bootstrap`);
`--print-commands` prints the same guidance without installing anything; sudo
unavailability/non-interactive cases now print manual commands before stopping;
container/distrobox/NVIDIA validation notes are included in the guidance.

Additional TUI validation update on 2026-05-26:

```text
bash -n tools/linux/flux-linux-setup.sh
./tools/linux/flux-linux-setup.sh --help
./tools/linux/flux-linux-setup.sh --print-commands
printf '9\nq\n' | ./tools/linux/flux-linux-setup.sh --tui
printf '5\nn\nq\n' | ./tools/linux/flux-linux-setup.sh --tui
env FLUX_BUILD_DIR=/root/flux-t070-denied bash -lc "printf '4\\ny\\nq\\n' | ./tools/linux/flux-linux-setup.sh --tui"
bash -lc './tools/linux/flux-linux-setup.sh </dev/null'
./tools/linux/flux-linux-setup.sh --check
```

Results: the helper now has a pure Bash TUI (`--tui`, and interactive no-arg
use) with a concise status summary for OS, RPM Fusion, Fedora deps, build
binary, PyPlugs, OFX bundles/cache, NVIDIA/OpenGL/container hints; menu actions
call existing bootstrap/install/configure/build/deploy/launch/check/guidance
functions; mutating actions require confirmation; non-interactive no-arg use
remains non-mutating and runs the checker. Review found and the implementation
fixed a TUI shell-control blocker: under Bash `errexit`, functions called in an
`if` condition can continue after a failed substep. Multi-step TUI actions now
explicitly short-circuit each step with `|| return`, and the denied-build-dir
validation proves a failed configure reports a warning and returns to the menu
instead of continuing or exiting abruptly. `--check` remains useful but can exit
nonzero when the local workstation lacks installed runtime pieces or a generated
OFX cache.

## Remaining work

- Fedora NVIDIA distrobox validation completed after installing `distrobox` on
  the host and creating `flux-t070-fedora44-nvidia` with `--nvidia`. Inside the
  box, `nvidia-smi` worked and `glxinfo -B` reported `NVIDIA GeForce RTX
  4090/PCIe/SSE2`, OpenGL 4.6, driver 595.71.05.
- Fresh distrobox install used `/tmp/flux-t070-distrobox-install` for `HOME`,
  `XDG_CACHE_HOME`, and `FLUX_BUILD_DIR`; no production host install/cache/build
  output was reused.
- `--install-deps` initially exposed an RPM Fusion `ffmpeg-devel` conflict with
  Fedora `libswresample-free`; installer now uses `dnf install --allowerasing`
  for Fedora deps. Dependency installation then passed inside the distrobox.
- `--configure` passed using the fresh distrobox build dir.
- `--build` passed for `Natron`, `NatronRenderer`, and
  `FluxTextRender.ofx.bundle`.
- Runtime deploy passed: embedded Python deps, Flux PyPlugs, all eight OFX
  bundles, launcher generation, scoped cache clear, and `ldd` validation.
- Plain `timeout flux` can abort during timeout shutdown in this distrobox, but
  debugger launch stayed alive until timeout and printed `FLUX: Layout created
  successfully`, confirming startup/layout under NVIDIA GL.
- Post-launch checker passed: all OFX binaries `ldd` clean, cold-cache
  `TextRender` discovery passed, Fedora package check passed, Flux binary
  exists, and all expected restored OFX IDs were present in the runtime cache.

## Remaining work outside T070

- Ubuntu/Debian package mapping and validation.
- Arch package mapping and validation.
- True binary packaging/AppImage or distro package.
- macOS and Windows equivalents.
