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
`$PLUGIN_PREFIX`, `$OFX_USER_PLUGIN_DIR`, `$XDG_CACHE_HOME`, and `$HOME` at the
sandbox.

## 1. Clone

```bash
git clone --recursive <flux-repo-url> Flux
cd Flux
export FLUX_ROOT="$PWD"
export BUILD_DIR="${FLUX_ROOT}/build"
export PLUGIN_PREFIX="${FLUX_ROOT}/plugins"
export OFX_USER_PLUGIN_DIR="${HOME}/.OFX/Plugins"
```

Expected submodules include `libs/OpenFX`, `libs/SequenceParsing`, Breakpad, and
the test dependencies. Flux also uses `plugins/natron-plugins` as the community
PyPlug submodule.

## 2. Fedora bootstrap

The bootstrap helper is the normal Fedora path once the required OpenFX payloads
are present in `$PLUGIN_PREFIX` or `$PLUGIN_PREFIX/ofx-extras`, or supplied with
`--extras-source`. The current source tree does not build IO/Misc/Arena extras
from scratch during bootstrap.

Run the Flux bootstrap helper:

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
8. embedded-Python runtime dependency bootstrap (`qtpy`, `packaging`) into `$BUILD_DIR/Plugins`
9. Flux PyPlug + OpenFX bundle deployment
10. scoped OFX cache clear
11. user launcher install
12. `ldd` validation of installed OFX binaries

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

Manual package list, if needed:

```bash
sudo dnf install -y \
  cmake extra-cmake-modules gcc gcc-c++ make ninja-build git boost-devel \
  qt6-qtbase-devel qt6-qtbase-gui \
  python3 python3-devel python3-pyside6 python3-shiboken6 shiboken6 \
  libX11-devel libXext-devel libXrender-devel mesa-libGL mesa-libGL-devel mesa-libGLU \
  glew-devel expat-devel cairo-devel pango-devel glib2-devel \
  fontconfig-devel freetype-devel \
  libpng-devel libjpeg-turbo-devel libtiff-devel openexr-devel \
  openjpeg-devel libwebp-devel LibRaw-devel \
  ffmpeg-devel OpenColorIO-devel OpenImageIO-devel \
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

1. Flux-specific PyPlugs:
   - `plugins/FluxLayer.py`
   - `plugins/FluxSolid.py`
   - `plugins/FluxText.py`
   - `plugins/FluxMotionText.py`
2. bundled/community PyPlugs used through `NATRON_PLUGIN_PATH`:
   - `Gui/Resources/PyPlugs/`
   - `plugins/natron-plugins/`
3. native OpenFX bundles:
   - `plugins/IO.ofx.bundle`
   - `plugins/Misc.ofx.bundle`
   - `plugins/FluxTextRender.ofx.bundle`
   - `CImg.ofx.bundle`
   - `SeExpr.ofx.bundle`
   - `Text.ofx.bundle`
   - `Magick.ofx.bundle`
   - `ResolveMath.ofx.bundle`

The user install locations are:

```text
$HOME/.Natron/PyPlugs/
$OFX_USER_PLUGIN_DIR/
```

The restored OFX extras and IO/Misc bundle payloads are required artifacts. If a
workstation receives the source tree without those bundles, pass an external
bundle directory via `--extras-source`. If any required bundle is missing,
bootstrap/deployment fails instead of silently producing a partial install.

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
$HOME/.local/bin/flux
```

It sets:

```bash
QT_PLUGIN_PATH=<detected from qtpaths6, qtpaths-qt6, qmake6, or common distro paths>
QT_QPA_PLATFORM=xcb
NATRON_PLUGIN_PATH=$HOME/.Natron/PyPlugs:$FLUX_ROOT/Gui/Resources/PyPlugs:$PLUGIN_PREFIX/natron-plugins
OFX_PLUGIN_PATH=$OFX_USER_PLUGIN_DIR:$PLUGIN_PREFIX
```

Run:

```bash
flux
```

Or run directly from the checkout, letting Qt use its default plugin discovery
unless your distro requires `QT_PLUGIN_PATH` explicitly:

```bash
QT_QPA_PLATFORM=xcb \
NATRON_PLUGIN_PATH="$HOME/.Natron/PyPlugs:$FLUX_ROOT/Gui/Resources/PyPlugs:$PLUGIN_PREFIX/natron-plugins" \
OFX_PLUGIN_PATH="$OFX_USER_PLUGIN_DIR:$PLUGIN_PREFIX" \
"$BUILD_DIR/App/Natron"
```

## 7. OFX cache

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

## 8. Validation checklist

Run the non-mutating checker:

```bash
"${FLUX_ROOT}/tools/linux/flux-linux-setup.sh" --check --validate-ldd
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

## 9. Troubleshooting

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

## 10. Current Linux limitation

This guide is Fedora-first because that is the validated workstation. Ubuntu,
Debian, Arch, AppImage, and fully bundled binary packaging still need their own
verified scripts before we can claim broad Linux installer coverage.
