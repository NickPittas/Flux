# Flux Linux Workstation Setup

This is the canonical Linux setup path for a clean workstation. It covers the
Flux application build, Python/PySide/Shiboken, PyPlugs, OpenFX bundles, runtime
environment, OFX cache handling, and validation.

Current validated workstation:

- Fedora 44
- GCC 16.1
- CMake 4.3
- Qt6 6.11.1
- Python 3.14.4
- PySide6/Shiboken6 6.11.1
- OpenColorIO 2.4.2
- OpenImageIO 3.1.12
- FFmpeg 8.1 from RPM Fusion

Older inherited Natron install docs remain in `INSTALL_LINUX.md`; use this file
for Flux.

Installer validation must not be run on a production workstation while the
installer is still in `TESTING`. Use a clean Fedora VM/container; for local
development, prefer a sandboxed distrobox pod and point `$BUILD_DIR`,
`$PLUGIN_PREFIX`, `$FLUX_INSTALL_PREFIX`, `$FLUX_BIN_DIR`, `$XDG_CACHE_HOME`, and
`$HOME` at the sandbox.

## 1. Clone

```bash
git clone --recursive <flux-repo-url> Flux
cd Flux
export FLUX_ROOT="$PWD"
export BUILD_DIR="${FLUX_ROOT}/build"
export PLUGIN_PREFIX="${FLUX_ROOT}/plugins"
export FLUX_BIN_DIR="${XDG_BIN_HOME:-${HOME}/.local/bin}"
export FLUX_DATA_DIR="${XDG_DATA_HOME:-${HOME}/.local/share}"
export FLUX_INSTALL_PREFIX="${FLUX_DATA_DIR}/Flux"
```

Expected submodules include `libs/OpenFX`, `libs/SequenceParsing`, Breakpad, and
the test dependencies. Flux also uses `plugins/natron-plugins` as the community
PyPlug submodule.

## 2. Fedora bootstrap

Start the installer from an interactive terminal with no arguments. The default
interactive path is a pure Bash guided menu: it probes the workstation, explains
current status, offers bootstrap/deploy/build/validation choices, and asks for
confirmation before any mutating action.

```bash
"${FLUX_ROOT}/tools/linux/flux-linux-setup.sh"
```

For testing or scripted menu smoke checks, force the same menu with `--tui`.
When stdin/stdout are not terminals and no arguments are given, the helper stays
safe and falls back to the existing non-mutating check behavior.

CLI alternatives remain available:

```bash
"${FLUX_ROOT}/tools/linux/flux-linux-setup.sh" --check
"${FLUX_ROOT}/tools/linux/flux-linux-setup.sh" --print-commands
```

Typical guidance includes:

```bash
sudo dnf install -y https://download1.rpmfusion.org/free/fedora/rpmfusion-free-release-$(rpm -E %fedora).noarch.rpm
sudo dnf install -y --allowerasing <Flux Fedora package list>
"${FLUX_ROOT}/tools/linux/flux-linux-setup.sh" --enable-rpmfusion
"${FLUX_ROOT}/tools/linux/flux-linux-setup.sh" --install-deps
"${FLUX_ROOT}/tools/linux/flux-linux-setup.sh" --bootstrap
```

If `sudo` is unavailable or non-interactive, the helper stops and prints the
commands to run manually instead of failing cryptically. In toolbox/distrobox or
other containers, run those package commands inside the container. For NVIDIA GUI
validation in containers, prefer `distrobox --nvidia` and confirm `nvidia-smi`
and `glxinfo -B` inside the box.

The bootstrap helper is the normal Fedora path once the required OpenFX payloads
are present in `$PLUGIN_PREFIX` or `$PLUGIN_PREFIX/ofx-extras`, or supplied with
`--extras-source`. The current source tree does not build IO/Misc/Arena extras
from scratch during bootstrap.

Run the Flux bootstrap helper from a post-clone shell where `git` is already
available. Minimal Fedora containers may need `git`/`sudo` installed in the
container before they can exercise the exact bootstrap path; do not install
those on the production host just for T070 validation.

```bash
"${FLUX_ROOT}/tools/linux/flux-linux-setup.sh" --bootstrap
```

For a machine that already had an older local Flux/Natron plugin install and
should be refreshed in-place, use:

```bash
"${FLUX_ROOT}/tools/linux/flux-linux-setup.sh" --bootstrap --force
```

`--bootstrap` performs, in order:

1. `git submodule update --init --recursive`
2. RPM Fusion free enablement when missing (`ffmpeg-devel` comes from RPM Fusion)
3. Fedora package availability verification against enabled repos
4. Fedora build/runtime dependency installation with `sudo dnf install`
5. CMake Qt6 configure
6. `Natron` GUI target build
7. Flux-owned `TextRender` OpenFX build into `$PLUGIN_PREFIX`
8. built app install to `$FLUX_INSTALL_PREFIX/bin/flux` and renderer install to `$FLUX_INSTALL_PREFIX/bin/FluxRenderer` when present
9. embedded-Python runtime dependency bootstrap (`qtpy`, `packaging`) into `$FLUX_INSTALL_PREFIX/Plugins/python`
10. Flux PyPlug + OpenFX bundle deployment under `$FLUX_INSTALL_PREFIX/Plugins/`
11. scoped OFX cache clear
12. user launcher install at `$FLUX_BIN_DIR/flux`
13. `ldd` validation of installed OFX binaries
14. cold-cache `net.flux.openfx.TextRender` discovery validation via `NatronRenderer`

Mutating actions do not run the full checker unless `--check` is also passed,
because the OFX registry cache cannot be validated until Flux has launched once.

After bootstrap completes, launch Flux once:

```bash
flux
```

Then validate the final runtime registry:

```bash
"${FLUX_ROOT}/tools/linux/flux-linux-setup.sh" --check --validate-ldd
```

If you run the checker before launching Flux once, the OFX cache part may fail;
that is expected because the runtime registry cache is created on first launch.

## 3. What bootstrap installs on Fedora

Manual package list, if needed. The helper prints this exact command with
`--print-commands` and repeats it when Fedora package checks fail:

```bash
sudo dnf install -y --allowerasing \
  cmake extra-cmake-modules gcc gcc-c++ clang make ninja-build git boost-devel \
  qt6-qtbase-devel qt6-qtbase-gui \
  python3 python3-devel python3-pyside6 python3-pyside6-devel \
  python3-shiboken6 python3-shiboken6-devel shiboken6 \
  libX11-devel libXext-devel libXrender-devel mesa-libGL mesa-libGL-devel mesa-libGLU \
  glew-devel expat-devel cairo-devel pango-devel glib2-devel \
  fontconfig-devel freetype-devel \
  libpng-devel libjpeg-turbo-devel libtiff-devel openexr-devel \
  openjpeg-devel libwebp-devel LibRaw-devel \
  ffmpeg-devel OpenColorIO-devel OpenImageIO-devel \
  ImageMagick-c++ libraqm liblqr-1 \
  libzip-devel minizip-ng-devel eigen3-devel glog-devel ceres-solver-devel
```

## 4. Lower-level commands for debugging/CI

The manual pieces remain available for debugging, CI, or partial reruns.

Install deps only:

```bash
"${FLUX_ROOT}/tools/linux/flux-linux-setup.sh" --install-deps
```

Configure and build only:

```bash
"${FLUX_ROOT}/tools/linux/flux-linux-setup.sh" --configure --build
```

The built executable is:

```text
$BUILD_DIR/App/Natron
```

Deploy runtime plugins only:

```bash
"${FLUX_ROOT}/tools/linux/flux-linux-setup.sh" \
  --deploy-extras \
  --install-launcher \
  --clear-ofx-cache \
  --validate-ldd \
  --force
```

## 5. Plugin payloads installed by bootstrap

Flux needs three classes of plugin material:

1. Flux-specific PyPlugs discovered from `plugins/*.py`. The required minimum is:
   - `plugins/FluxLayer.py`
   - `plugins/FluxSolid.py`
   - `plugins/FluxText.py`
   - `plugins/FluxMotionText.py`
2. bundled/community PyPlugs copied into the Flux install prefix, not into
   generic Natron user-profile folders:
   - `Gui/Resources/PyPlugs/`
   - `plugins/natron-plugins/`
3. native OpenFX bundles discovered from `plugins/*.ofx.bundle` and
   `plugins/ofx-extras/*.ofx.bundle`. The required minimum is:
   - `plugins/IO.ofx.bundle`
   - `plugins/Misc.ofx.bundle`
   - `plugins/FluxTextRender.ofx.bundle`
   - `CImg.ofx.bundle`
   - `SeExpr.ofx.bundle`
   - `Text.ofx.bundle`
   - `Magick.ofx.bundle`
   - `ResolveMath.ofx.bundle`

The default XDG install layout is:

```text
${XDG_DATA_HOME:-$HOME/.local/share}/Flux/bin/flux
${XDG_DATA_HOME:-$HOME/.local/share}/Flux/bin/FluxRenderer
${XDG_DATA_HOME:-$HOME/.local/share}/Flux/Plugins/PyPlugs/
${XDG_DATA_HOME:-$HOME/.local/share}/Flux/Plugins/OFX/
${XDG_DATA_HOME:-$HOME/.local/share}/Flux/Plugins/python/
${XDG_DATA_HOME:-$HOME/.local/share}/Flux/install-manifest.txt
${XDG_BIN_HOME:-$HOME/.local/bin}/flux
```

Override with environment or CLI:

```bash
FLUX_INSTALL_PREFIX=/tmp/flux/share/Flux FLUX_BIN_DIR=/tmp/flux/bin   "${FLUX_ROOT}/tools/linux/flux-linux-setup.sh" --deploy-extras --install-app --install-launcher --force
# or
"${FLUX_ROOT}/tools/linux/flux-linux-setup.sh" --install-prefix /tmp/flux/share/Flux --bin-dir /tmp/flux/bin --deploy-extras --install-app --install-launcher --force
```

The repository is the source of truth for plugin payloads. The installer copies
(no symlinks for the installed prefix) every top-level `plugins/*.py` PyPlug,
`Gui/Resources/PyPlugs/`, `plugins/natron-plugins/` excluding VCS/cache/temp
files, and every discovered `.ofx.bundle` from `plugins/` and
`plugins/ofx-extras/`. The restored OFX
extras and IO/Misc bundle payloads are required artifacts. If a workstation
receives the source tree without those bundles, pass an external bundle
directory via `--extras-source`. If any required bundle is missing,
bootstrap/deployment fails instead of silently producing a partial install.

When changing `openfx-flux`, either run the installer with `--build` so it
installs the freshly built `FluxTextRender.ofx.bundle` into `plugins/`, or copy
the built binary into the repo bundle before `--deploy-extras`:

```bash
cmake --build "$BUILD_DIR/openfx-flux" -j"$(nproc)"
cp "$BUILD_DIR/openfx-flux/FluxTextRender.ofx" \
  "$FLUX_ROOT/plugins/FluxTextRender.ofx.bundle/Contents/Linux-x86-64/FluxTextRender.ofx"
"$FLUX_ROOT/tools/linux/flux-linux-setup.sh" --deploy-extras --clear-ofx-cache --force
sha256sum \
  "$BUILD_DIR/openfx-flux/FluxTextRender.ofx" \
  "${XDG_DATA_HOME:-$HOME/.local/share}/Flux/Plugins/OFX/FluxTextRender.ofx.bundle/Contents/Linux-x86-64/FluxTextRender.ofx"
```

The two hashes must match before GUI testing; otherwise Flux is still running an
old TextRender plugin.

If the restored OFX extras are not already in `plugins/ofx-extras/`, pass their
directory explicitly:

```bash
"${FLUX_ROOT}/tools/linux/flux-linux-setup.sh" \
  --deploy-extras \
  --extras-source /path/to/flux-linux-ofx-extras \
  --install-launcher \
  --clear-ofx-cache \
  --validate-ldd \
  --force
```

To create such an extras directory from a known-good workstation:

```bash
"${FLUX_ROOT}/tools/linux/flux-linux-setup.sh" --stage-extras dist/flux-linux-ofx-extras
```

Transfer `dist/flux-linux-ofx-extras/` to the target workstation and use it as
`--extras-source`.

## 6. Run Flux

The setup helper can install this user launcher:

```text
${XDG_BIN_HOME:-$HOME/.local/bin}/flux
```

It sets:

```bash
QT_PLUGIN_PATH=<detected from qtpaths6, qtpaths-qt6, qmake6, or common distro paths>
QT_QPA_PLATFORM=xcb
NATRON_PLUGIN_PATH=$FLUX_INSTALL_PREFIX/Plugins/PyPlugs:$FLUX_INSTALL_PREFIX/Plugins/PyPlugs/natron-plugins
OFX_PLUGIN_PATH=$FLUX_INSTALL_PREFIX/Plugins/OFX
LD_LIBRARY_PATH=$FLUX_INSTALL_PREFIX/Plugins/OFX/SeExpr.ofx.bundle/Contents/Linux-x86-64/seexpr-deps/lib:$FLUX_INSTALL_PREFIX/Plugins/OFX/Magick.ofx.bundle/Contents/Linux-x86-64/magick-deps/lib:$LD_LIBRARY_PATH
```

Run:

```bash
flux
```

The launcher runs the installed app binary at `$FLUX_INSTALL_PREFIX/bin/flux`
and points Flux at the copied plugin payloads under that same prefix. Users do
not need to export plugin paths manually.

## 7. Uninstall

The installer writes `$FLUX_INSTALL_PREFIX/install-manifest.txt`. To remove only
manifest-owned Flux files plus the launcher when it belongs to the same prefix:

```bash
"${FLUX_ROOT}/tools/linux/flux-linux-setup.sh" --uninstall
"${FLUX_ROOT}/tools/linux/flux-linux-setup.sh" --uninstall --force
```

Uninstall does not remove the source checkout, build tree, generic `~/.Natron`,
generic `~/.OFX`, package-manager dependencies, or system packages.

## 8. OFX cache

Flux/Natron caches OpenFX discovery. After installing or replacing OFX bundles,
clear only this scoped cache:

```bash
rm -rf "${XDG_CACHE_HOME:-$HOME/.cache}/INRIA/Natron/OFXLoadCache/"
```

Or use:

```bash
"${FLUX_ROOT}/tools/linux/flux-linux-setup.sh" --clear-ofx-cache
```

Do not delete autosaves as part of plugin maintenance.

## 9. Validation checklist

Run the TUI first on an interactive workstation, or the non-mutating checker in
CI/non-interactive shells. Add `--print-commands` when you want the actionable
Fedora command/options block even if only part of the check fails:

```bash
"${FLUX_ROOT}/tools/linux/flux-linux-setup.sh"
"${FLUX_ROOT}/tools/linux/flux-linux-setup.sh" --tui
"${FLUX_ROOT}/tools/linux/flux-linux-setup.sh" --check --validate-ldd
"${FLUX_ROOT}/tools/linux/flux-linux-setup.sh" --check --print-commands
```

On the first run after clearing the OFX cache, the checker may report that the
cache file does not exist yet. That is expected until Flux has launched once and
rebuilt the OpenFX registry cache.

Then launch Flux once so the OFX registry cache is regenerated:

```bash
flux
```

Run the checker again. It should confirm that the restored OFX IDs are present
in the runtime cache.

Required restored IDs:

```text
fr.inria.openfx.SeExpr
fr.inria.openfx.SeExprSimple
net.sf.openfx.SeNoise
net.sf.openfx.SeGrain
net.fxarena.openfx.Text
net.fxarena.openfx.RichText
net.fxarena.openfx.Tile
OpenFX.Yo.ResolveMath
net.sf.cimg.CImgBlur
net.sf.cimg.CImgBloom
net.sf.cimg.CImgDilate
net.flux.openfx.TextRender
```

Previously failing PyPlugs that must create successfully:

```text
lp_roughenEdges
lp_SimpleKeyer
comunity.plugins.Luma_to_Normals
comunity.plugins.Vectors_Normalize
```

Use script-file Python validation, not command-mode `-b -c`; command-mode is
still unsafe with the current Python 3.14 path.

## 10. Troubleshooting

### Missing OFX plugin ID

1. Confirm the bundle exists in `$OFX_USER_PLUGIN_DIR/` or in a directory exported via
   `OFX_PLUGIN_PATH`.
2. Run:

   ```bash
   "${FLUX_ROOT}/tools/linux/flux-linux-setup.sh" --validate-ldd
   ```

3. Clear only the OFX cache and relaunch Flux:

   ```bash
   "${FLUX_ROOT}/tools/linux/flux-linux-setup.sh" --clear-ofx-cache
   flux
   ```

Flux now reports missing plugin/library diagnostics with the plugin ID, version,
load reason, `ldd` hint, and cache-clear guidance.

### Python/PySide/Shiboken issues

Check CMake found the correct Python and Qt6 bindings:

```bash
cmake -S . -B build -DNATRON_QT6=ON
```

Expected on Fedora 44:

```text
Python3: /usr/bin/python3.14
PySide6: /usr/lib64/cmake/PySide6
Shiboken6: /usr/lib64/cmake/Shiboken6
```

Install/reinstall the Fedora packages:

```bash
sudo dnf install -y python3 python3-devel python3-pyside6 python3-shiboken6 shiboken6
```

### Qt platform issues under Wayland

Use the validated xcb compatibility path:

```bash
QT_QPA_PLATFORM=xcb flux
```

## 11. Current Linux limitation

This guide is Fedora-first because that is the validated workstation. Ubuntu,
Debian, Arch, AppImage, and fully bundled binary packaging still need their own
verified scripts before we can claim broad Linux installer coverage.
