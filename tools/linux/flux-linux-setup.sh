#!/usr/bin/env bash
# Flux Linux workstation interactive installer.
#
# Run with no arguments. All installation choices are made through prompts.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
FLUX_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd -P)"

XDG_BIN_HOME="${XDG_BIN_HOME:-${HOME}/.local/bin}"
XDG_DATA_HOME="${XDG_DATA_HOME:-${HOME}/.local/share}"
FLUX_BIN_DIR="${FLUX_BIN_DIR:-${XDG_BIN_HOME}}"
FLUX_DATA_DIR="${FLUX_DATA_DIR:-${XDG_DATA_HOME}}"
FLUX_INSTALL_PREFIX="${FLUX_INSTALL_PREFIX:-${FLUX_DATA_DIR}/Flux}"
FLUX_APP_BIN="${FLUX_INSTALL_PREFIX}/bin/flux"
FLUX_RENDERER_BIN="${FLUX_INSTALL_PREFIX}/bin/FluxRenderer"
INSTALL_MANIFEST="${FLUX_INSTALL_PREFIX}/install-manifest.txt"
USER_PYPLUG_DIR="${USER_PYPLUG_DIR:-${FLUX_USER_PYPLUG_DIR:-${FLUX_INSTALL_PREFIX}/Plugins/PyPlugs}}"
OFX_USER_PLUGIN_DIR="${OFX_USER_PLUGIN_DIR:-${FLUX_USER_OFX_DIR:-${FLUX_INSTALL_PREFIX}/Plugins/OFX}}"
USER_OFX_DIR="${OFX_USER_PLUGIN_DIR}"
OFX_CACHE_DIR="${OFX_CACHE_DIR:-${FLUX_OFX_CACHE_DIR:-${XDG_CACHE_HOME:-${HOME}/.cache}/INRIA/Natron/OFXLoadCache}}"
LAUNCHER_PATH="${LAUNCHER_PATH:-${FLUX_LAUNCHER_PATH:-${FLUX_BIN_DIR}/flux}}"
NATRON_LAUNCHER_PATH="${NATRON_LAUNCHER_PATH:-${FLUX_BIN_DIR}/natron}"

PLUGIN_PREFIX="${PLUGIN_PREFIX:-${FLUX_PLUGIN_PREFIX:-${FLUX_ROOT}/plugins}}"
EXTRAS_SOURCE="${FLUX_OFX_EXTRAS:-${PLUGIN_PREFIX}/ofx-extras}"
STAGE_EXTRAS_DIR=""
BUILD_DIR="${BUILD_DIR:-${FLUX_BUILD_DIR:-${FLUX_ROOT}/build}}"
BUILD_TYPE="${FLUX_BUILD_TYPE:-RelWithDebInfo}"
BUILD_JOBS="${FLUX_BUILD_JOBS:-}"
PYTHON_RUNTIME_DIR_SET=0
if [[ -n "${PYTHON_RUNTIME_DIR:-}" || -n "${FLUX_PYTHON_RUNTIME_DIR:-}" ]]; then
  PYTHON_RUNTIME_DIR_SET=1
fi
PYTHON_RUNTIME_DIR="${PYTHON_RUNTIME_DIR:-${FLUX_PYTHON_RUNTIME_DIR:-${FLUX_INSTALL_PREFIX}/Plugins/python}}"
HOME_ABS="$(cd "$HOME" && pwd -P)"

DO_CHECK=0
CHECK_EXPLICIT=0
ACTION_REQUESTED=0
DO_PRINT_COMMANDS=0
DO_TUI=0
DO_GUI=0
DO_BOOTSTRAP=0
DO_UPDATE_SUBMODULES=0
DO_ENABLE_RPMFUSION=0
DO_VERIFY_FEDORA_REPOS=0
DO_INSTALL_DEPS=0
DO_CONFIGURE=0
DO_BUILD=0
DO_DEPLOY_EXTRAS=0
DO_INSTALL_LAUNCHER=0
DO_INSTALL_APP=0
DO_UNINSTALL=0
DO_CLEAR_OFX_CACHE=0
DO_VALIDATE_LDD=0
DO_VALIDATE_OFX_DISCOVERY=0
DO_BOOTSTRAP_PYTHON=0
FORCE=0
COPY_MODE="copy"

FEDORA_PACKAGES=(
  cmake
  extra-cmake-modules
  gcc
  gcc-c++
  clang
  make
  ninja-build
  git
  boost-devel
  qt6-qtbase-devel
  qt6-qtbase-gui
  python3
  python3-tkinter
  python3-devel
  python3-pyside6
  python3-pyside6-devel
  python3-shiboken6
  python3-shiboken6-devel
  shiboken6
  libX11-devel
  libXext-devel
  libXrender-devel
  mesa-libGL
  mesa-libGL-devel
  mesa-libGLU
  glew-devel
  expat-devel
  cairo-devel
  pango-devel
  glib2-devel
  fontconfig-devel
  freetype-devel
  libpng-devel
  libjpeg-turbo-devel
  libtiff-devel
  openexr-devel
  openjpeg-devel
  libwebp-devel
  LibRaw-devel
  ffmpeg-devel
  OpenColorIO-devel
  OpenImageIO-devel
  ImageMagick-c++
  libraqm
  liblqr-1
  libzip-devel
  minizip-ng-devel
  eigen3-devel
  glog-devel
  ceres-solver-devel
)

REQUIRED_PYPLUG_FILES=(
  "FluxLayer.py"
  "FluxSolid.py"
  "FluxText.py"
  "FluxMotionText.py"
)

PYPLUG_FILES=()

OFX_CORE_BUNDLES=(
  "IO.ofx.bundle"
  "Misc.ofx.bundle"
  "FluxTextRender.ofx.bundle"
)

OFX_EXTRA_BUNDLES=(
  "CImg.ofx.bundle"
  "SeExpr.ofx.bundle"
  "Text.ofx.bundle"
  "Magick.ofx.bundle"
  "ResolveMath.ofx.bundle"
)

EXPECTED_OFX_IDS=(
  "fr.inria.openfx.SeExpr"
  "fr.inria.openfx.SeExprSimple"
  "net.sf.openfx.SeNoise"
  "net.sf.openfx.SeGrain"
  "net.fxarena.openfx.Text"
  "net.fxarena.openfx.RichText"
  "net.fxarena.openfx.Tile"
  "OpenFX.Yo.ResolveMath"
  "net.sf.cimg.CImgBlur"
  "net.sf.cimg.CImgBloom"
  "net.sf.cimg.CImgDilate"
  "net.flux.openfx.TextRender"
)

append_unique() {
  local value="$1"
  shift
  local existing
  for existing in "$@"; do
    [[ "$existing" == "$value" ]] && return 1
  done
  printf '%s\n' "$value"
}

refresh_derived_paths() {
  USER_OFX_DIR="${OFX_USER_PLUGIN_DIR}"
  discover_plugin_payloads
}

discover_plugin_payloads() {
  local file bundle name
  local discovered_pyplugs=()
  local discovered_ofx=()

  for name in "${REQUIRED_PYPLUG_FILES[@]}"; do
    discovered_pyplugs+=("${PLUGIN_PREFIX}/${name}")
  done

  if [[ -d "$PLUGIN_PREFIX" ]]; then
    while IFS= read -r -d '' file; do
      if append_unique "$file" "${discovered_pyplugs[@]}" >/dev/null; then
        discovered_pyplugs+=("$file")
      fi
    done < <(find "$PLUGIN_PREFIX" -maxdepth 1 -type f -name '*.py' -print0 | sort -z)
  fi

  for bundle in "${OFX_EXTRA_BUNDLES[@]}"; do
    if append_unique "$bundle" "${discovered_ofx[@]}" >/dev/null; then
      discovered_ofx+=("$bundle")
    fi
  done

  for file in "$PLUGIN_PREFIX" "$EXTRAS_SOURCE"; do
    [[ -d "$file" ]] || continue
    while IFS= read -r -d '' bundle; do
      name="$(basename "$bundle")"
      if append_unique "$name" "${OFX_CORE_BUNDLES[@]}" "${discovered_ofx[@]}" >/dev/null; then
        discovered_ofx+=("$name")
      fi
    done < <(find "$file" -maxdepth 1 -type d -name '*.ofx.bundle' -print0 | sort -z)
  done

  PYPLUG_FILES=("${discovered_pyplugs[@]}")
  OFX_EXTRA_BUNDLES=("${discovered_ofx[@]}")
}

log() {
  printf '[flux-linux-setup] %s\n' "$*"
}

warn() {
  printf '[flux-linux-setup] WARNING: %s\n' "$*" >&2
}

die() {
  printf '[flux-linux-setup] ERROR: %s\n' "$*" >&2
  exit 1
}

usage() {
  cat <<EOF
Run: tools/linux/flux-linux-setup.sh

Flux Linux setup is interactive-only. Run with no arguments; the installer
will show current status and ask before installing dependencies, building,
deploying runtime payloads, setting up AI models, clearing cache, launching,
or uninstalling.
EOF
}

is_private_dispatch() {
  [[ "${FLUX_SETUP_INTERNAL_DISPATCH:-}" == "1" && "${1:-}" == "__flux_setup_action" ]]
}

parse_args() {
  if is_private_dispatch "$@"; then
    return 0
  fi

  if [[ $# -gt 1 ]]; then
    die "Usage: tools/linux/flux-linux-setup.sh [--gui|--tui]"
  fi

  if [[ $# -eq 1 ]]; then
    case "$1" in
      --gui) DO_GUI=1 ;;
      --tui) DO_TUI=1 ;;
      *) die "Usage: tools/linux/flux-linux-setup.sh [--gui|--tui]" ;;
    esac
    return 0
  fi

  DO_GUI=1
}
fedora_version_for_commands() {
  if command -v rpm >/dev/null 2>&1; then
    rpm -E %fedora 2>/dev/null || printf '<fedora-version>\n'
  else
    printf '<fedora-version>\n'
  fi
}

print_fedora_guidance() {
  local fedora_version rpmfusion_url
  fedora_version="$(fedora_version_for_commands)"
  rpmfusion_url="https://download1.rpmfusion.org/free/fedora/rpmfusion-free-release-${fedora_version}.noarch.rpm"

  cat <<EOF

Flux Fedora setup guidance:
  Enable RPM Fusion free manually if the installer prompts you to:
    sudo dnf install -y ${rpmfusion_url}

  Install Fedora dependencies:
    sudo dnf install -y --allowerasing ${FEDORA_PACKAGES[*]}

  Or rerun the interactive installer and choose the matching menu action.

Notes:
  - This helper is Fedora-first; package guidance is not validated for other distros.
  - In toolbox/distrobox/containers, install inside the container, not on the host.
  - For NVIDIA GUI validation in containers, prefer distrobox --nvidia and confirm nvidia-smi/glxinfo.
EOF
}

sudo_can_run() {
  command -v sudo >/dev/null 2>&1 || return 1
  if [[ -t 0 ]]; then
    return 0
  fi
  sudo -n true >/dev/null 2>&1
}

run_privileged() {
  if command -v sudo >/dev/null 2>&1; then
    if [[ -t 0 ]]; then
      sudo "$@"
      return
    fi
    if sudo -n true >/dev/null 2>&1; then
      sudo -n "$@"
      return
    fi
  fi

  if command -v pkexec >/dev/null 2>&1; then
    pkexec "$@"
    return
  fi

  return 1
}

privilege_can_run() {
  command -v sudo >/dev/null 2>&1 && { [[ -t 0 ]] || sudo -n true >/dev/null 2>&1; } && return 0
  command -v pkexec >/dev/null 2>&1
}


require_privilege_or_guidance() {
  local action="$1"
  if privilege_can_run; then
    return 0
  fi
  warn "No interactive privilege helper is available for ${action}."
  print_fedora_guidance
  return 1
}

detect_os() {
  if [[ -r /etc/os-release ]]; then
    # shellcheck disable=SC1091
    . /etc/os-release
    printf '%s\n' "${ID:-unknown}"
  else
    printf 'unknown\n'
  fi
}

check_commands() {
  local missing=0
  local cmd
  for cmd in cmake c++ python3 ldd; do
    if ! command -v "$cmd" >/dev/null 2>&1; then
      warn "Missing command: ${cmd}"
      missing=1
    fi
  done
  return "$missing"
}

check_fedora_packages() {
  local missing=()
  local pkg

  if ! command -v rpm >/dev/null 2>&1; then
    warn 'rpm not found; skipping Fedora package check.'
    return 0
  fi

  for pkg in "${FEDORA_PACKAGES[@]}"; do
    if ! rpm -q "$pkg" >/dev/null 2>&1; then
      missing+=("$pkg")
    fi
  done

  if [[ ${#missing[@]} -gt 0 ]]; then
    warn 'Missing Fedora packages:'
    printf '  %s\n' "${missing[@]}" >&2
    print_fedora_guidance
    return 1
  fi

  log 'Fedora package check passed.'
}

verify_fedora_repos() {
  local os_id missing=() pkg result
  os_id="$(detect_os)"
  [[ "$os_id" == "fedora" ]] || die "Fedora repository verification only supports Fedora; detected '${os_id}'."
  command -v dnf >/dev/null 2>&1 || die 'dnf is required for Fedora repository verification.'

  log 'Verifying Fedora package availability from enabled repositories.'
  for pkg in "${FEDORA_PACKAGES[@]}"; do
    result="$(dnf -q repoquery --qf '%{name}' "$pkg" 2>/dev/null || true)"
    if [[ -z "$result" ]]; then
      missing+=("$pkg")
    fi
  done

  if [[ ${#missing[@]} -gt 0 ]]; then
    warn 'Fedora packages not available from enabled repositories:'
    printf '  %s\n' "${missing[@]}" >&2
    print_fedora_guidance
    return 1
  fi

  log 'Fedora repository package availability check passed.'
}

enable_rpmfusion_free() {
  local os_id fedora_version rpmfusion_url
  os_id="$(detect_os)"
  [[ "$os_id" == "fedora" ]] || die "RPM Fusion setup only supports Fedora; detected '${os_id}'."
  command -v rpm >/dev/null 2>&1 || die 'rpm is required to check RPM Fusion.'
  require_privilege_or_guidance 'RPM Fusion setup' || die 'sudo or pkexec is required to enable RPM Fusion.'
  command -v dnf >/dev/null 2>&1 || die 'dnf is required to enable RPM Fusion.'

  if rpm -q rpmfusion-free-release >/dev/null 2>&1; then
    log 'RPM Fusion free is already enabled.'
    return 0
  fi

  fedora_version="$(rpm -E %fedora)"
  rpmfusion_url="https://download1.rpmfusion.org/free/fedora/rpmfusion-free-release-${fedora_version}.noarch.rpm"
  log "Enabling RPM Fusion free for Fedora ${fedora_version}."
  run_privileged dnf install -y "$rpmfusion_url"
}

install_fedora_packages() {
  local os_id
  os_id="$(detect_os)"
  [[ "$os_id" == "fedora" ]] || die "Dependency installation currently supports Fedora only; detected '${os_id}'."
  require_privilege_or_guidance 'dependency installation' || die 'sudo or pkexec is required to install dependencies.'
  command -v dnf >/dev/null 2>&1 || die 'dnf is required for dependency installation.'

  log 'Installing Fedora packages. RPM Fusion free must be enabled for ffmpeg-devel.'
  run_privileged dnf install -y --allowerasing "${FEDORA_PACKAGES[@]}"
}

update_openfx_submodule_fallback() {
  local openfx_dir="${FLUX_ROOT}/libs/OpenFX"
  local openfx_url="https://github.com/NatronGitHub/openfx.git"
  local openfx_ref="a5d9ca88512692b0a872044cfde90e3181ff5ad6"

  log "Using reachable Natron-2.5.0 OpenFX ref ${openfx_ref} for installer bootstrap."
  if git -C "$FLUX_ROOT" ls-files --error-unmatch .gitmodules &>/dev/null &&
     git -C "$FLUX_ROOT" config --file .gitmodules --get submodule.libs/OpenFX.path >/dev/null; then
    git -C "$FLUX_ROOT" submodule init libs/OpenFX || true
  fi
  if [[ ! -d "${openfx_dir}/.git" && ! -f "${openfx_dir}/.git" ]]; then
    git clone "$openfx_url" "$openfx_dir"
  fi
  git -C "$openfx_dir" fetch origin "$openfx_ref"
  git -C "$openfx_dir" checkout --detach "$openfx_ref"
}


ensure_required_submodule_file() {
  local path="$1"
  local required_file="$2"

  git -C "$FLUX_ROOT" submodule update --init --recursive "$path" || return
  if [[ ! -f "${FLUX_ROOT}/${required_file}" ]]; then
    die "Required submodule content is missing after update: ${required_file}"
  fi
}

ensure_required_submodules() {
  ensure_required_submodule_file libs/SequenceParsing libs/SequenceParsing/SequenceParsing.cpp || return
  ensure_required_submodule_file Tests/google-test Tests/google-test/README || return
  ensure_required_submodule_file Tests/google-mock Tests/google-mock/README || return
  ensure_required_submodule_file libs/google-breakpad libs/google-breakpad/src/tools/linux/symupload/sym_upload.cc || return
  ensure_required_submodule_file plugins/natron-plugins plugins/natron-plugins/README.md || return
}

update_submodules() {
  command -v git >/dev/null 2>&1 || die 'git is required to update submodules.'
  log 'Updating git submodules.'
  git -C "$FLUX_ROOT" submodule update --init --recursive -- ':!libs/OpenFX' || return
  ensure_required_submodules || return
  update_openfx_submodule_fallback
}

configure_flux() {
  command -v cmake >/dev/null 2>&1 || die 'cmake is required to configure Flux.'
  ensure_required_submodules || return
  log "Configuring Flux: build_dir=${BUILD_DIR}, build_type=${BUILD_TYPE}"
  cmake -S "$FLUX_ROOT" -B "$BUILD_DIR" \
    -DNATRON_QT6=ON \
    -DNATRON_BUILD_TESTS=ON \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
}

build_flux() {
  command -v cmake >/dev/null 2>&1 || die 'cmake is required to build Flux.'
  if [[ -z "$BUILD_JOBS" ]]; then
    if command -v nproc >/dev/null 2>&1; then
      BUILD_JOBS="$(nproc)"
    else
      BUILD_JOBS=8
    fi
  fi
  log "Building Flux target Natron with ${BUILD_JOBS} jobs."
  cmake --build "$BUILD_DIR" --target Natron -j "$BUILD_JOBS"
  log "Building Flux target NatronRenderer with ${BUILD_JOBS} jobs."
  cmake --build "$BUILD_DIR" --target NatronRenderer -j "$BUILD_JOBS"
}

ensure_build_jobs() {
  if [[ -z "$BUILD_JOBS" ]]; then
    if command -v nproc >/dev/null 2>&1; then
      BUILD_JOBS="$(nproc)"
    else
      BUILD_JOBS=8
    fi
  fi
}

build_ofx_flux() {
  local ofx_flux_build_dir="${BUILD_DIR}/openfx-flux"
  command -v cmake >/dev/null 2>&1 || die 'cmake is required to build openfx-flux.'
  [[ -d "${FLUX_ROOT}/openfx-flux" ]] || die "Missing Flux OFX source path: ${FLUX_ROOT}/openfx-flux"
  [[ -d "${FLUX_ROOT}/openfx-misc/openfx/include" ]] || die 'Missing OpenFX support headers. Use the installer submodule update action or run git submodule update --init --recursive from a developer shell.'

  ensure_build_jobs
  log "Building Flux OFX bundle FluxTextRender.ofx.bundle with ${BUILD_JOBS} jobs."
  cmake -S "${FLUX_ROOT}/openfx-flux" -B "$ofx_flux_build_dir" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
  cmake --build "$ofx_flux_build_dir" -j "$BUILD_JOBS"
  cmake --install "$ofx_flux_build_dir" --prefix "${PLUGIN_PREFIX}"
  log "Installed FluxTextRender.ofx.bundle to ${PLUGIN_PREFIX}"
}

bootstrap_python_runtime() {
  command -v python3 >/dev/null 2>&1 || die 'python3 is required to install embedded Python dependencies.'
  mkdir -p "$PYTHON_RUNTIME_DIR"
  log "Installing embedded Python runtime deps into ${PYTHON_RUNTIME_DIR}."
  python3 -m pip install --upgrade --target "$PYTHON_RUNTIME_DIR" qtpy packaging huggingface_hub keyring
  manifest_add_tree "$PYTHON_RUNTIME_DIR"
}

canonical_existing_parent() {
  local path="$1"
  local dir base
  if [[ -e "$path" && ! -d "$path" ]]; then
    dir="$(dirname "$path")"
    base="/$(basename "$path")"
  else
    dir="$path"
    while [[ ! -e "$dir" && "$dir" != "/" ]]; do
      dir="$(dirname "$dir")"
    done
    base="${path#${dir}}"
  fi
  printf '%s%s\n' "$(cd "$dir" && pwd -P)" "$base"
}

validate_install_prefix() {
  local prefix_abs flux_root_abs build_dir_abs
  [[ -n "$FLUX_INSTALL_PREFIX" ]] || die 'Install prefix is empty.'
  prefix_abs="$(canonical_existing_parent "$FLUX_INSTALL_PREFIX")"
  flux_root_abs="$(cd "$FLUX_ROOT" && pwd -P)"
  build_dir_abs="$(canonical_existing_parent "$BUILD_DIR")"

  case "$prefix_abs" in
    /|"$HOME_ABS"|"$flux_root_abs"|"$build_dir_abs") die "Refusing unsafe Flux install prefix: ${FLUX_INSTALL_PREFIX}" ;;
    "$flux_root_abs"/*|"$build_dir_abs"/*) die "Refusing install prefix inside source/build tree: ${FLUX_INSTALL_PREFIX}" ;;
  esac

  FLUX_INSTALL_PREFIX="$prefix_abs"
  FLUX_APP_BIN="${FLUX_INSTALL_PREFIX}/bin/flux"
  FLUX_RENDERER_BIN="${FLUX_INSTALL_PREFIX}/bin/FluxRenderer"
  INSTALL_MANIFEST="${FLUX_INSTALL_PREFIX}/install-manifest.txt"
  USER_PYPLUG_DIR="${FLUX_INSTALL_PREFIX}/Plugins/PyPlugs"
  NATRON_LAUNCHER_PATH="${FLUX_BIN_DIR}/natron"
  OFX_USER_PLUGIN_DIR="${FLUX_INSTALL_PREFIX}/Plugins/OFX"
  USER_OFX_DIR="$OFX_USER_PLUGIN_DIR"
  PYTHON_RUNTIME_DIR="${FLUX_INSTALL_PREFIX}/Plugins/python"
}

path_within_install_prefix() {
  local path_abs="$1"
  [[ "$path_abs" == "$FLUX_INSTALL_PREFIX" || "$path_abs" == "${FLUX_INSTALL_PREFIX}/"* ]]
}

manifest_reset() {
  validate_install_prefix
  mkdir -p "$FLUX_INSTALL_PREFIX"
  : > "$INSTALL_MANIFEST"
  printf '%s\n' "$INSTALL_MANIFEST" >> "$INSTALL_MANIFEST"
}

manifest_add_path() {
  local path="$1"
  [[ -n "$path" ]] || return 0
  mkdir -p "$FLUX_INSTALL_PREFIX"
  touch "$INSTALL_MANIFEST"
  if ! grep -Fx -- "$path" "$INSTALL_MANIFEST" >/dev/null 2>&1; then
    printf '%s
' "$path" >> "$INSTALL_MANIFEST"
  fi
}

manifest_add_tree() {
  local path="$1"
  manifest_add_path "$path"
  if [[ -d "$path" ]]; then
    while IFS= read -r -d '' item; do
      manifest_add_path "$item"
    done < <(find "$path" -mindepth 1 -print0 | sort -zr)
  fi
}

replace_target() {
  local target="$1"
  if [[ -e "$target" || -L "$target" ]]; then
    if [[ "$FORCE" -ne 1 ]]; then
      warn "Target exists, leaving unchanged: ${target} (choose refresh/repair in the interactive installer to replace)"
      return 1
    fi
    rm -rf "$target"
  fi
  return 0
}

copy_or_symlink() {
  local source="$1"
  local target="$2"
  local source_abs target_abs
  [[ -e "$source" ]] || die "Missing source: ${source}"
  mkdir -p "$(dirname "$target")"

  source_abs="$(cd "$(dirname "$source")" && pwd -P)/$(basename "$source")"
  target_abs="$(cd "$(dirname "$target")" && pwd -P)/$(basename "$target")"
  if [[ "$source_abs" == "$target_abs" ]]; then
    log "Already installed: ${target}"
    return 0
  fi

  replace_target "$target" || return 0

  if [[ "$COPY_MODE" == "symlink" ]]; then
    ln -s "$source" "$target"
  else
    cp -a "$source" "$target"
  fi
  manifest_add_tree "$target"
  log "Installed ${target}"
}

copy_payload() {
  local source="$1"
  local target="$2"
  [[ -e "$source" ]] || die "Missing source: ${source}"
  mkdir -p "$(dirname "$target")"
  replace_target "$target" || return 0
  cp -aL "$source" "$target"
  manifest_add_tree "$target"
  log "Installed ${target}"
}

copy_dir_contents_filtered() {
  local source_dir="$1"
  local target_dir="$2"
  [[ -d "$source_dir" ]] || return 0
  mkdir -p "$target_dir"
  while IFS= read -r -d '' item; do
    local rel="${item#${source_dir}/}"
    [[ "$rel" == .git* || "$rel" == *__pycache__* || "$rel" == *.pyc || "$rel" == *~ || "$rel" == *.tmp ]] && continue
    if [[ -d "$item" ]]; then
      mkdir -p "${target_dir}/${rel}"
      manifest_add_path "${target_dir}/${rel}"
    elif [[ -f "$item" ]]; then
      mkdir -p "$(dirname "${target_dir}/${rel}")"
      cp -aL "$item" "${target_dir}/${rel}"
      manifest_add_path "${target_dir}/${rel}"
    fi
  done < <(find "$source_dir" -mindepth 1 -print0 | sort -z)
}

install_app() {
  local source_app="${BUILD_DIR}/App/Natron"
  local source_renderer="${BUILD_DIR}/Renderer/NatronRenderer"
  [[ -x "$source_app" ]] || die "Flux build binary missing: ${source_app}. Build Flux first from the interactive installer, or skip app installation."
  copy_payload "$source_app" "$FLUX_APP_BIN"
  chmod 0755 "$FLUX_APP_BIN"
  if [[ -x "$source_renderer" ]]; then
    copy_payload "$source_renderer" "$FLUX_RENDERER_BIN"
    chmod 0755 "$FLUX_RENDERER_BIN"
  fi
}

install_runtime_payloads() {
  mkdir -p "$USER_PYPLUG_DIR" "$USER_OFX_DIR"
  manifest_add_path "${FLUX_INSTALL_PREFIX}/Plugins"
  manifest_add_path "$USER_PYPLUG_DIR"
  manifest_add_path "$USER_OFX_DIR"
  deploy_pyplugs
  copy_dir_contents_filtered "${FLUX_ROOT}/Gui/Resources/PyPlugs" "$USER_PYPLUG_DIR"
  copy_dir_contents_filtered "${PLUGIN_PREFIX}/natron-plugins" "${USER_PYPLUG_DIR}/natron-plugins"
  deploy_ofx_bundles
  install_ai_payloads
}

install_ai_payloads() {
  local ai_source="${FLUX_ROOT}/tools/ai"
  local ai_target="${FLUX_INSTALL_PREFIX}/tools/ai"
  [[ -d "$ai_source" ]] || die "Missing Flux AI tools source: ${ai_source}"
  mkdir -p "$ai_target"
  copy_payload "${ai_source}/flux_model_manager.py" "${ai_target}/flux_model_manager.py"
  copy_payload "${ai_source}/flux_provider_runtime.py" "${ai_target}/flux_provider_runtime.py"
  copy_payload "${ai_source}/flux_ai_worker.py" "${ai_target}/flux_ai_worker.py"
  copy_payload "${ai_source}/sam3_transformers_worker.py" "${ai_target}/sam3_transformers_worker.py"
  copy_payload "${ai_source}/matanyone2_worker.py" "${ai_target}/matanyone2_worker.py"
  copy_payload "${ai_source}/videomama_worker.py" "${ai_target}/videomama_worker.py"
  copy_dir_contents_filtered "${ai_source}/videomama" "${ai_target}/videomama"
  copy_payload "${ai_source}/model_manifest.json" "${ai_target}/model_manifest.json"
  copy_payload "${ai_source}/provider_runtime_manifest.json" "${ai_target}/provider_runtime_manifest.json"
  copy_payload "${ai_source}/sam31_real_inference_probe.py" "${ai_target}/sam31_real_inference_probe.py"
  copy_payload "${ai_source}/sam3_transformers_real_inference_probe.py" "${ai_target}/sam3_transformers_real_inference_probe.py"
}

ensure_ai_tools() {
  local old_force="$FORCE"
  if [[ ! -d "$PYTHON_RUNTIME_DIR" ]]; then
    confirm_default_yes "Flux AI Python dependencies are missing. Install them now without rebuilding Flux?" || die 'AI Python dependencies are required for this action.'
    validate_install_prefix || return
    mkdir -p "$FLUX_INSTALL_PREFIX"
    touch "$INSTALL_MANIFEST"
    bootstrap_python_runtime || return
  fi
  validate_install_prefix || return
  mkdir -p "$FLUX_INSTALL_PREFIX"
  touch "$INSTALL_MANIFEST"
  FORCE=1
  install_ai_payloads || return
  FORCE="$old_force"
}

run_ai_manager() {
  local manager="${FLUX_INSTALL_PREFIX}/tools/ai/flux_model_manager.py"
  ensure_ai_tools || return
  [[ -f "$manager" ]] || die "Missing installed Flux model manager: ${manager}"
  PYTHONPATH="${PYTHON_RUNTIME_DIR}${PYTHONPATH:+:${PYTHONPATH}}" python3 "$manager" "$@"
}

setup_default_ai_models() {
  confirm_default_yes "Install default Flux AI models now?" || { log "AI model setup skipped by user."; return 0; }
  log "Installing default Flux AI models; gated models prompt securely for Hugging Face tokens when needed."
  run_ai_manager install --all-default || die "Flux AI model setup failed."
}

run_ai_model_install() {
  local model_id="${1:-}"
  [[ -n "$model_id" ]] || die 'AI model install requires a model id.'
  run_ai_manager install "$model_id"
}

run_ai_token_entry() {
  log 'Opening secure Hugging Face token prompt. Token persistence uses only the desktop secure keyring.'
  run_ai_manager login --remember
}

run_ai_model_status() {
  if [[ -f "${FLUX_INSTALL_PREFIX}/tools/ai/flux_model_manager.py" ]]; then
    PYTHONPATH="${PYTHON_RUNTIME_DIR}${PYTHONPATH:+:${PYTHONPATH}}" python3 "${FLUX_INSTALL_PREFIX}/tools/ai/flux_model_manager.py" status
  else
    python3 "${FLUX_ROOT}/tools/ai/flux_model_manager.py" --manifest "${FLUX_ROOT}/tools/ai/model_manifest.json" status
  fi
}

run_ai_model_remove() {
  local model_id="${1:-}"
  [[ -n "$model_id" ]] || die 'AI model remove requires a model id.'
  run_ai_manager remove "$model_id"
}

run_ai_model_list() {
  python3 "${FLUX_ROOT}/tools/ai/flux_model_manager.py" --manifest "${FLUX_ROOT}/tools/ai/model_manifest.json" list --json
}

run_ai_runtime_manager() {
  local manager="${FLUX_ROOT}/tools/ai/flux_provider_runtime.py"
  [[ -f "$manager" ]] || die "Missing Flux provider runtime manager: ${manager}"
  python3 "$manager" "$@"
}

run_ai_runtime_status() {
  local runtime_id="${1:-sam31}"
  run_ai_runtime_manager status "$runtime_id" --json
}

run_ai_runtime_install() {
  local runtime_id="${1:-sam31}"
  log "Installing ${runtime_id} provider runtime into its isolated Flux/ai-envs/${runtime_id} environment. This never uses Flux/Plugins/python or system Python for model execution."
  run_ai_runtime_manager install "$runtime_id" --cuda "${FLUX_AI_RUNTIME_CUDA:-cu128}" --json
}

run_ai_runtime_self_check() {
  local runtime_id="${1:-sam31}"
  run_ai_runtime_manager self-check "$runtime_id" --json
}

deploy_pyplugs() {
  local pyplug
  mkdir -p "$USER_PYPLUG_DIR"
  for pyplug in "${PYPLUG_FILES[@]}"; do
    copy_payload "$pyplug" "${USER_PYPLUG_DIR}/$(basename "$pyplug")"
  done
}

bundle_source_for() {
  local bundle="$1"
  local candidate

  candidate="${PLUGIN_PREFIX}/${bundle}"
  if [[ -d "$candidate" ]]; then
    printf '%s\n' "$candidate"
    return 0
  fi

  candidate="${EXTRAS_SOURCE}/${bundle}"
  if [[ -d "$candidate" ]]; then
    printf '%s\n' "$candidate"
    return 0
  fi

  candidate="${USER_OFX_DIR}/${bundle}"
  if [[ -d "$candidate" ]]; then
    printf '%s\n' "$candidate"
    return 0
  fi

  return 1
}

bundle_dir_for_check() {
  local bundle="$1"
  local candidate

  candidate="${USER_OFX_DIR}/${bundle}"
  if [[ -d "$candidate" ]]; then
    printf '%s\n' "$candidate"
    return 0
  fi

  candidate="${PLUGIN_PREFIX}/${bundle}"
  if [[ -d "$candidate" ]]; then
    printf '%s\n' "$candidate"
    return 0
  fi

  candidate="${EXTRAS_SOURCE}/${bundle}"
  if [[ -d "$candidate" ]]; then
    printf '%s\n' "$candidate"
    return 0
  fi

  return 1
}

preflight_ofx_bundle_sources() {
  local bundle missing=0

  for bundle in "${OFX_CORE_BUNDLES[@]}" "${OFX_EXTRA_BUNDLES[@]}"; do
    if ! bundle_source_for "$bundle" >/dev/null; then
      if [[ "$bundle" == "FluxTextRender.ofx.bundle" && "$DO_BUILD" -eq 1 && -d "${FLUX_ROOT}/openfx-flux" ]]; then
        log "FluxTextRender.ofx.bundle will be built from ${FLUX_ROOT}/openfx-flux."
        continue
      fi
      warn "Missing OFX bundle source for ${bundle}. Provide extras in the configured extras source directory or stage extras first from a developer shell."
      missing=1
    fi
  done

  if [[ "$missing" -ne 0 ]]; then
    die 'Required OFX bundle sources are missing; Flux runtime plugin deploy would be incomplete.'
  fi
}

deploy_ofx_bundles() {
  local bundle source
  mkdir -p "$USER_OFX_DIR"

  preflight_ofx_bundle_sources

  for bundle in "${OFX_CORE_BUNDLES[@]}" "${OFX_EXTRA_BUNDLES[@]}"; do
    source="$(bundle_source_for "$bundle")"
    copy_payload "$source" "${USER_OFX_DIR}/${bundle}"
  done
}

stage_extras() {
  local dest="$1"
  local bundle source missing=0
  [[ -n "$dest" ]] || return 0

  for bundle in "${OFX_CORE_BUNDLES[@]}" "${OFX_EXTRA_BUNDLES[@]}"; do
    if ! bundle_source_for "$bundle" >/dev/null; then
      warn "Cannot stage missing bundle: ${bundle}"
      missing=1
    fi
  done

  if [[ "$missing" -ne 0 ]]; then
    die 'Required OFX bundle sources are missing; stage artifact would be incomplete.'
  fi

  mkdir -p "$dest"
  for bundle in "${OFX_CORE_BUNDLES[@]}" "${OFX_EXTRA_BUNDLES[@]}"; do
    source="$(bundle_source_for "$bundle")"
    rm -rf "${dest}/${bundle}"
    cp -a "$source" "${dest}/${bundle}"
    log "Staged ${bundle} -> ${dest}/${bundle}"
  done
}

clear_ofx_cache() {
  if [[ -z "$OFX_CACHE_DIR" || "$OFX_CACHE_DIR" == "/" || "$OFX_CACHE_DIR" == "$HOME" ]]; then
    die "Refusing unsafe OFX cache path: ${OFX_CACHE_DIR}"
  fi
  if [[ "$OFX_CACHE_DIR" != *"OFXLoadCache"* ]]; then
    die "Refusing to clear non-OFX cache path: ${OFX_CACHE_DIR}"
  fi
  if [[ -d "$OFX_CACHE_DIR" ]]; then
    rm -rf "$OFX_CACHE_DIR"
    log "Cleared ${OFX_CACHE_DIR}"
  else
    log "OFX cache not present: ${OFX_CACHE_DIR}"
  fi
}

write_launcher() {
  mkdir -p "$(dirname "$LAUNCHER_PATH")"
  cat > "$LAUNCHER_PATH" <<EOF
#!/usr/bin/env bash
set -euo pipefail
FLUX_INSTALL_PREFIX="${FLUX_INSTALL_PREFIX}"
if [[ -z "\${QT_PLUGIN_PATH:-}" ]]; then
  for tool in qtpaths6 qtpaths-qt6 qmake6; do
    if command -v "\$tool" >/dev/null 2>&1; then
      if [[ "\$tool" == qmake6 ]]; then QT_PLUGIN_PATH="\$(qmake6 -query QT_INSTALL_PLUGINS)"; else QT_PLUGIN_PATH="\$("\$tool" --plugin-dir)"; fi
      break
    fi
  done
fi
export QT_PLUGIN_PATH="\${QT_PLUGIN_PATH:-/usr/lib64/qt6/plugins}"
export QT_QPA_PLATFORM="\${QT_QPA_PLATFORM:-xcb}"
export NATRON_PLUGIN_PATH="\${NATRON_PLUGIN_PATH:-\${FLUX_INSTALL_PREFIX}/Plugins/PyPlugs:\${FLUX_INSTALL_PREFIX}/Plugins/PyPlugs/natron-plugins}"
export OFX_PLUGIN_PATH="\${OFX_PLUGIN_PATH:-\${FLUX_INSTALL_PREFIX}/Plugins/OFX}"
export FLUX_OFX_STRICT_PATH="\${FLUX_OFX_STRICT_PATH:-1}"
export PYTHONPATH="\${FLUX_INSTALL_PREFIX}/Plugins/python\${PYTHONPATH:+:\${PYTHONPATH}}"
export LD_LIBRARY_PATH="\${FLUX_INSTALL_PREFIX}/Plugins/OFX/SeExpr.ofx.bundle/Contents/Linux-x86-64/seexpr-deps/lib:\${FLUX_INSTALL_PREFIX}/Plugins/OFX/Magick.ofx.bundle/Contents/Linux-x86-64/magick-deps/lib:\${LD_LIBRARY_PATH:-}"
if [[ "\${FLUX_VERBOSE_CONSOLE:-0}" == "1" || "\${1:-}" == "--help" || "\${1:-}" == "-h" ]]; then
  exec "\${FLUX_INSTALL_PREFIX}/bin/flux" "\$@"
fi
LOG_DIR="\${XDG_STATE_HOME:-\${HOME}/.local/state}/Flux"
mkdir -p "\${LOG_DIR}"
exec "\${FLUX_INSTALL_PREFIX}/bin/flux" "\$@" >>"\${LOG_DIR}/flux-launch.log" 2>&1
EOF
  chmod 0755 "$LAUNCHER_PATH"
  if [[ "$NATRON_LAUNCHER_PATH" != "$LAUNCHER_PATH" ]]; then
    cp "$LAUNCHER_PATH" "$NATRON_LAUNCHER_PATH"
    chmod 0755 "$NATRON_LAUNCHER_PATH"
    manifest_add_path "$NATRON_LAUNCHER_PATH"
    log "Installed launcher alias: ${NATRON_LAUNCHER_PATH}"
  fi
  manifest_add_path "$LAUNCHER_PATH"
  log "Installed launcher: ${LAUNCHER_PATH}"
}

uninstall_flux() {
  local path path_abs launcher_abs natron_launcher_abs
  validate_install_prefix
  [[ -f "$INSTALL_MANIFEST" ]] || die "Install manifest not found: ${INSTALL_MANIFEST}"
  if [[ "$FORCE" -ne 1 ]]; then
    confirm_mutation "Uninstall Flux files listed in ${INSTALL_MANIFEST}?" || die 'Uninstall cancelled.'
  fi
  mapfile -t paths < <(grep -v '^$' "$INSTALL_MANIFEST" | sort -ru)
  for path in "${paths[@]}"; do
    path_abs="$(canonical_existing_parent "$path")"
    if path_within_install_prefix "$path_abs"; then
      [[ -e "$path_abs" || -L "$path_abs" ]] && rm -rf "$path_abs"
    else
      warn "Skipping out-of-prefix manifest path: ${path}"
    fi
  done
  launcher_abs="$(canonical_existing_parent "$LAUNCHER_PATH")"
  if [[ -f "$launcher_abs" ]] && grep -F "$FLUX_INSTALL_PREFIX" "$launcher_abs" >/dev/null 2>&1; then
    rm -f "$launcher_abs"
  fi
  natron_launcher_abs="$(canonical_existing_parent "$NATRON_LAUNCHER_PATH")"
  if [[ -f "$natron_launcher_abs" ]] && grep -F "$FLUX_INSTALL_PREFIX" "$natron_launcher_abs" >/dev/null 2>&1; then
    rm -f "$natron_launcher_abs"
  fi
  if [[ -d "$FLUX_INSTALL_PREFIX" ]]; then
    find "$FLUX_INSTALL_PREFIX" -depth -type d -empty -delete 2>/dev/null || true
  fi
  rmdir "$FLUX_INSTALL_PREFIX" 2>/dev/null || true
  log "Uninstalled Flux prefix: ${FLUX_INSTALL_PREFIX}"
}
ofx_binary_path() {
  local bundle="$1"
  local bundle_dir

  if ! bundle_dir="$(bundle_dir_for_check "$bundle")"; then
    bundle_dir="${USER_OFX_DIR}/${bundle}"
  fi

  local base="${bundle_dir}/Contents/Linux-x86-64"
  case "$bundle" in
    CImg.ofx.bundle) printf '%s\n' "${base}/CImg.ofx" ;;
    SeExpr.ofx.bundle) printf '%s\n' "${base}/SeExpr.ofx" ;;
    Text.ofx.bundle) printf '%s\n' "${base}/Text.ofx" ;;
    Magick.ofx.bundle) printf '%s\n' "${base}/Magick.ofx" ;;
    ResolveMath.ofx.bundle) printf '%s\n' "${base}/ResolveMath.ofx" ;;
    IO.ofx.bundle) printf '%s\n' "${base}/IO.ofx" ;;
    Misc.ofx.bundle) printf '%s\n' "${base}/Misc.ofx" ;;
    FluxTextRender.ofx.bundle) printf '%s\n' "${base}/FluxTextRender.ofx" ;;
    *) printf '%s\n' "${base}/${bundle%.ofx.bundle}.ofx" ;;
  esac
}

validate_ldd() {
  local bundle binary output failed=0
  for bundle in "${OFX_CORE_BUNDLES[@]}" "${OFX_EXTRA_BUNDLES[@]}"; do
    binary="$(ofx_binary_path "$bundle")"
    if [[ ! -f "$binary" ]]; then
      warn "Missing OFX binary: ${binary}"
      failed=1
      continue
    fi
    output="$(LD_LIBRARY_PATH="${USER_OFX_DIR}/SeExpr.ofx.bundle/Contents/Linux-x86-64/seexpr-deps/lib:${USER_OFX_DIR}/Magick.ofx.bundle/Contents/Linux-x86-64/magick-deps/lib:${LD_LIBRARY_PATH:-}" ldd "$binary" 2>&1 || true)"
    if [[ "$output" == *'not found'* ]]; then
      warn "Missing shared library for ${binary}:"
      printf '%s\n' "$output" >&2
      failed=1
    else
      log "ldd clean: ${binary}"
    fi
  done
  return "$failed"
}

validate_ofx_discovery() {
  local renderer="${BUILD_DIR}/Renderer/NatronRenderer"
  local temp_dir script output status=0 source_bundle

  if [[ ! -x "$renderer" ]]; then
    die "NatronRenderer not found; cannot validate OFX discovery: ${renderer}. Choose the build action first."
  fi

  temp_dir="$(mktemp -d /tmp/flux-ofx-discovery.XXXXXX)"
  mkdir -p "${temp_dir}/home" "${temp_dir}/cache" "${temp_dir}/disk-cache" "${temp_dir}/plugins"
  if [[ -d "${USER_OFX_DIR}/FluxTextRender.ofx.bundle" ]]; then
    source_bundle="${USER_OFX_DIR}/FluxTextRender.ofx.bundle"
  elif [[ -d "${PLUGIN_PREFIX}/FluxTextRender.ofx.bundle" ]]; then
    source_bundle="${PLUGIN_PREFIX}/FluxTextRender.ofx.bundle"
  else
    rm -rf "$temp_dir"
    die 'FluxTextRender.ofx.bundle is not built/deployed. Build Flux, then deploy runtime payloads from the interactive installer.'
  fi
  ln -s "$source_bundle" "${temp_dir}/plugins/FluxTextRender.ofx.bundle"

  script="${temp_dir}/validate_flux_text_render.py"
  cat > "$script" <<'PY'
import NatronEngine

app = NatronEngine.natron.getInstance(0)
assert app is not None, 'No NatronEngine app instance available'
ids = list(NatronEngine.natron.getPluginIDs('TextRender'))
print('FLUX_OFX_DISCOVERY_IDS:', ids)
assert 'net.flux.openfx.TextRender' in ids, 'net.flux.openfx.TextRender was not discovered'
node = app.createNode('net.flux.openfx.TextRender')
assert node is not None, 'net.flux.openfx.TextRender node creation failed'
print('FLUX_OFX_DISCOVERY_CREATE_OK:', node.getPluginID(), node.getScriptName())
PY

  log 'Cold-cache validating net.flux.openfx.TextRender discovery via NatronRenderer.'
  output="$(HOME="${temp_dir}/home" \
    XDG_CACHE_HOME="${temp_dir}/cache" \
    NATRON_DISK_CACHE_PATH="${temp_dir}/disk-cache" \
    OFX_PLUGIN_PATH="${temp_dir}/plugins" \
    QT_PLUGIN_PATH="${QT_PLUGIN_PATH:-/usr/lib64/qt6/plugins}" \
    QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-xcb}" \
    "$renderer" -b "$script" 2>&1)" || status=$?

  if [[ "$output" != *'FLUX_OFX_DISCOVERY_CREATE_OK: net.flux.openfx.TextRender'* ]]; then
    warn 'Flux OFX discovery validation failed. Output:'
    printf '%s\n' "$output" >&2
    rm -rf "$temp_dir"
    return 1
  fi

  if [[ "$status" -ne 0 ]]; then
    warn "NatronRenderer exited with status ${status} after validation marker; ignoring expected no-writer batch exit."
  fi
  log 'Flux OFX discovery validation passed: net.flux.openfx.TextRender.'
  rm -rf "$temp_dir"
}

check_pyplugs() {
  local missing=0
  local pyplug
  for pyplug in "${PYPLUG_FILES[@]}"; do
    if [[ ! -f "${USER_PYPLUG_DIR}/$(basename "$pyplug")" ]]; then
      warn "Missing installed PyPlug: ${USER_PYPLUG_DIR}/$(basename "$pyplug")"
      missing=1
    fi
  done
  if [[ ! -d "${FLUX_ROOT}/Gui/Resources/PyPlugs" ]]; then
    warn "Missing bundled PyPlug directory: ${FLUX_ROOT}/Gui/Resources/PyPlugs"
    missing=1
  fi
  if [[ ! -d "${PLUGIN_PREFIX}/natron-plugins" || ! -f "${PLUGIN_PREFIX}/natron-plugins/README.md" ]]; then
    warn "Missing community PyPlug submodule content: ${PLUGIN_PREFIX}/natron-plugins"
    warn 'Use the installer submodule update action or run git submodule update --init --recursive from a developer shell.'
    missing=1
  fi
  return "$missing"
}

check_ofx_bundles() {
  local missing=0 bundle binary
  for bundle in "${OFX_CORE_BUNDLES[@]}" "${OFX_EXTRA_BUNDLES[@]}"; do
    binary="$(ofx_binary_path "$bundle")"
    if [[ ! -f "$binary" ]]; then
      warn "Missing installed OFX binary: ${binary}"
      missing=1
    fi
  done
  return "$missing"
}

check_cache_ids() {
  local cache_file="${OFX_CACHE_DIR}/OFXCache_2.6_Devel_0.xml"
  local missing=0 id content

  if [[ ! -f "$cache_file" ]]; then
    warn "OFX cache file not found yet: ${cache_file}. Launch Flux once to regenerate it."
    return 1
  fi

  content="$(<"$cache_file")"
  for id in "${EXPECTED_OFX_IDS[@]}"; do
    if [[ "$content" != *"$id"* ]]; then
      warn "Runtime OFX cache missing ID: ${id}"
      missing=1
    fi
  done

  if [[ "$missing" -eq 0 ]]; then
    log 'Expected restored OFX IDs are present in the runtime cache.'
  fi
  return "$missing"
}

check_build_output() {
  if [[ ! -x "${BUILD_DIR}/App/Natron" ]]; then
    warn "Flux binary missing: ${BUILD_DIR}/App/Natron"
    warn "Build from the interactive installer menu."
    return 1
  fi
  log 'Flux binary exists.'
}

run_checks() {
  local status=0
  check_commands || status=1
  if [[ "$(detect_os)" == "fedora" ]]; then
    check_fedora_packages || status=1
  else
    warn 'Non-Fedora Linux detected; package check is not implemented yet.'
  fi
  check_build_output || status=1
  check_pyplugs || status=1
  check_ofx_bundles || status=1
  check_cache_ids || status=1
  return "$status"
}

status_word() {
  if "$@" >/dev/null 2>&1; then
    printf 'ok'
  else
    printf 'missing/needs attention'
  fi
}

rpmfusion_status() {
  if command -v rpm >/dev/null 2>&1 && rpm -q rpmfusion-free-release >/dev/null 2>&1; then
    printf 'enabled'
  else
    printf 'not detected'
  fi
}

fedora_deps_status() {
  local pkg
  if ! command -v rpm >/dev/null 2>&1; then
    printf 'not checked (rpm unavailable)'
    return 0
  fi
  for pkg in "${FEDORA_PACKAGES[@]}"; do
    if ! rpm -q "$pkg" >/dev/null 2>&1; then
      printf 'missing packages'
      return 0
    fi
  done
  printf 'installed'
}

ofx_bundle_status() {
  local bundle binary
  for bundle in "${OFX_CORE_BUNDLES[@]}" "${OFX_EXTRA_BUNDLES[@]}"; do
    binary="$(ofx_binary_path "$bundle")"
    [[ -f "$binary" ]] || { printf 'missing bundles'; return 0; }
  done
  printf 'present'
}

pyplug_status() {
  local pyplug
  for pyplug in "${PYPLUG_FILES[@]}"; do
    [[ -f "${USER_PYPLUG_DIR}/$(basename "$pyplug")" ]] || { printf 'missing'; return 0; }
  done
  printf 'present'
}

ofx_cache_status() {
  local cache_file="${OFX_CACHE_DIR}/OFXCache_2.6_Devel_0.xml"
  [[ -f "$cache_file" ]] && printf 'present' || printf 'not generated yet'
}

print_tui_summary() {
  local os_id container_hint nvidia_hint gl_hint
  os_id="$(detect_os)"
  if [[ -f /.dockerenv || -n "${container:-}" ]]; then
    container_hint='container detected'
  else
    container_hint='no container marker detected'
  fi
  if command -v nvidia-smi >/dev/null 2>&1; then
    nvidia_hint="$(nvidia-smi --query-gpu=name --format=csv,noheader 2>/dev/null | head -n 1 || true)"
    [[ -n "$nvidia_hint" ]] || nvidia_hint='nvidia-smi present, no GPU reported'
  elif [[ -e /dev/nvidia0 ]]; then
    nvidia_hint='/dev/nvidia0 present, nvidia-smi missing'
  else
    nvidia_hint='not detected'
  fi
  if command -v glxinfo >/dev/null 2>&1; then
    gl_hint="$(glxinfo -B 2>/dev/null | awk -F: '/OpenGL renderer string/ {sub(/^ /,"",$2); print $2; exit}')"
    [[ -n "$gl_hint" ]] || gl_hint='glxinfo present, renderer unavailable'
  else
    gl_hint='glxinfo not installed'
  fi

  cat <<EOF

Flux Linux setup summary
  OS: ${os_id}
  RPM Fusion free: $(rpmfusion_status)
  Fedora deps: $(fedora_deps_status)
  Build binary: $(status_word test -x "${BUILD_DIR}/App/Natron") (${BUILD_DIR}/App/Natron)
  Install prefix: ${FLUX_INSTALL_PREFIX}
  Launcher: $(status_word test -x "${LAUNCHER_PATH}") (${LAUNCHER_PATH})
  Natron command: $(status_word test -x "${NATRON_LAUNCHER_PATH}") (${NATRON_LAUNCHER_PATH})
  Installed app: $(status_word test -x "${FLUX_APP_BIN}") (${FLUX_APP_BIN})
  PyPlugs: $(pyplug_status) (${USER_PYPLUG_DIR})
  OFX bundles: $(ofx_bundle_status) (${USER_OFX_DIR})
  OFX cache: $(ofx_cache_status) (${OFX_CACHE_DIR})
  AI model manager: $(status_word test -f "${FLUX_INSTALL_PREFIX}/tools/ai/flux_model_manager.py") (${FLUX_INSTALL_PREFIX}/tools/ai/flux_model_manager.py)
  NVIDIA: ${nvidia_hint}
  OpenGL: ${gl_hint}
  Container: ${container_hint}
EOF
}

confirm_mutation() {
  local prompt="$1" answer
  printf '%s [y/N]: ' "$prompt"
  read -r answer || return 1
  [[ "$answer" == "y" || "$answer" == "Y" || "$answer" == "yes" || "$answer" == "YES" ]]
}

confirm_default_yes() {
  local prompt="$1" answer
  printf '%s [Y/n]: ' "$prompt"
  read -r answer || return 1
  [[ -z "$answer" || "$answer" == "y" || "$answer" == "Y" || "$answer" == "yes" || "$answer" == "YES" ]]
}

run_full_bootstrap() {
  DO_BUILD=1
  confirm_default_yes "Refresh/replace existing Flux-managed install files if present?" && FORCE=1 || FORCE=0
  validate_install_prefix || return
  update_submodules || return
  preflight_ofx_bundle_sources || return
  enable_rpmfusion_free || return
  verify_fedora_repos || return
  install_fedora_packages || return
  configure_flux || return
  build_flux || return
  build_ofx_flux || return
  manifest_reset || return
  install_app || return
  bootstrap_python_runtime || return
  install_runtime_payloads || return
  setup_default_ai_models || return
  clear_ofx_cache || return
  write_launcher || return
  validate_ldd || return
  validate_ofx_discovery || return
}

run_deploy_runtime() {
  confirm_default_yes "Refresh/replace existing Flux-managed install files if present?" && FORCE=1 || FORCE=0
  validate_install_prefix || return
  manifest_reset || return
  install_app || return
  bootstrap_python_runtime || return
  install_runtime_payloads || return
  setup_default_ai_models || return
  clear_ofx_cache || return
  write_launcher || return
  validate_ldd || return
}

run_build_all() {
  build_flux || return
  build_ofx_flux || return
}

run_tui_action() {
  local label="$1"
  shift
  if "$@"; then
    log "${label} completed."
  else
    warn "${label} failed; review the output above and choose the next step."
  fi
}

launch_flux() {
  if [[ -x "$LAUNCHER_PATH" ]]; then
    "$LAUNCHER_PATH"
  elif [[ -x "${BUILD_DIR}/App/Natron" ]]; then
    "${BUILD_DIR}/App/Natron"
  else
    die "Flux binary not found: ${BUILD_DIR}/App/Natron. Build first."
  fi
}

print_status_summary() {
  local os_id container_hint nvidia_hint gl_hint
  os_id="$(detect_os)"
  if [[ -f /.dockerenv || -n "${container:-}" ]]; then container_hint='container detected'; else container_hint='no container marker detected'; fi
  if command -v nvidia-smi >/dev/null 2>&1; then
    nvidia_hint="$(nvidia-smi --query-gpu=name --format=csv,noheader 2>/dev/null | head -n 1 || true)"
    [[ -n "$nvidia_hint" ]] || nvidia_hint='nvidia-smi present, no GPU reported'
  elif [[ -e /dev/nvidia0 ]]; then nvidia_hint='/dev/nvidia0 present, nvidia-smi missing'; else nvidia_hint='not detected'; fi
  if command -v glxinfo >/dev/null 2>&1; then
    gl_hint="$(glxinfo -B 2>/dev/null | awk -F: '/OpenGL renderer string/ {sub(/^ /,"",$2); print $2; exit}')"
    [[ -n "$gl_hint" ]] || gl_hint='glxinfo present, renderer unavailable'
  else gl_hint='glxinfo not installed'; fi
  printf 'os=%s\n' "$os_id"
  printf 'rpmfusion=%s\n' "$(rpmfusion_status)"
  printf 'fedora_deps=%s\n' "$(fedora_deps_status)"
  printf 'build_binary=%s\n' "$(status_word test -x "${BUILD_DIR}/App/Natron")"
  printf 'build_binary_path=%s\n' "${BUILD_DIR}/App/Natron"
  printf 'install_prefix=%s\n' "${FLUX_INSTALL_PREFIX}"
  printf 'launcher=%s\n' "$(status_word test -x "${LAUNCHER_PATH}")"
  printf 'launcher_path=%s\n' "${LAUNCHER_PATH}"
  printf 'natron_launcher=%s\n' "$(status_word test -x "${NATRON_LAUNCHER_PATH}")"
  printf 'natron_launcher_path=%s\n' "${NATRON_LAUNCHER_PATH}"
  printf 'installed_app=%s\n' "$(status_word test -x "${FLUX_APP_BIN}")"
  printf 'installed_app_path=%s\n' "${FLUX_APP_BIN}"
  printf 'pyplugs=%s\n' "$(pyplug_status)"
  printf 'pyplugs_path=%s\n' "${USER_PYPLUG_DIR}"
  printf 'ofx_bundles=%s\n' "$(ofx_bundle_status)"
  printf 'ofx_path=%s\n' "${USER_OFX_DIR}"
  printf 'ofx_cache=%s\n' "$(ofx_cache_status)"
  printf 'ofx_cache_path=%s\n' "${OFX_CACHE_DIR}"
  printf 'ai_model_manager=%s\n' "$(status_word test -f "${FLUX_INSTALL_PREFIX}/tools/ai/flux_model_manager.py")"
  printf 'nvidia=%s\n' "$nvidia_hint"
  printf 'opengl=%s\n' "$gl_hint"
  printf 'container=%s\n' "$container_hint"
}

run_installer_diagnostics_json() {
  FLUX_ROOT="$FLUX_ROOT" \
  BUILD_DIR="$BUILD_DIR" \
  FLUX_INSTALL_PREFIX="$FLUX_INSTALL_PREFIX" \
  FLUX_APP_BIN="$FLUX_APP_BIN" \
  LAUNCHER_PATH="$LAUNCHER_PATH" \
  NATRON_LAUNCHER_PATH="$NATRON_LAUNCHER_PATH" \
  USER_PYPLUG_DIR="$USER_PYPLUG_DIR" \
  USER_OFX_DIR="$USER_OFX_DIR" \
  OFX_CACHE_DIR="$OFX_CACHE_DIR" \
  python3 - <<'PY'
import json, os, platform, shutil
from pathlib import Path

def exists_file(path):
    p = Path(path)
    return {"path": str(p), "exists": p.is_file(), "executable": os.access(p, os.X_OK)}

def exists_dir(path):
    p = Path(path)
    return {"path": str(p), "exists": p.is_dir()}

root = Path(os.environ["FLUX_ROOT"])
prefix = Path(os.environ["FLUX_INSTALL_PREFIX"])
pyplug_dir = Path(os.environ["USER_PYPLUG_DIR"])
ofx_dir = Path(os.environ["USER_OFX_DIR"])
required_pyplugs = ["FluxLayer.py", "FluxSolid.py", "FluxText.py", "FluxMotionText.py"]
required_ofx = ["IO.ofx.bundle", "Misc.ofx.bundle", "FluxTextRender.ofx.bundle"]
ai_root = prefix / "tools" / "ai"
runtime_root = Path(os.environ.get("XDG_DATA_HOME") or (Path.home() / ".local/share")) / "Flux" / "ai-envs"
model_root = Path(os.environ.get("XDG_DATA_HOME") or (Path.home() / ".local/share")) / "Flux" / "models"
payload = {
    "schema": "org.flux.installer.diagnostics.v1",
    "system": {
        "platform": platform.platform(),
        "python": shutil.which("python3"),
        "cmake": shutil.which("cmake"),
        "ninja": shutil.which("ninja"),
        "git": shutil.which("git"),
    },
    "paths": {
        "flux_root": str(root),
        "build_dir": os.environ["BUILD_DIR"],
        "install_prefix": str(prefix),
        "ofx_cache": os.environ["OFX_CACHE_DIR"],
    },
    "app": {
        "build_binary": exists_file(str(Path(os.environ["BUILD_DIR"]) / "App" / "Natron")),
        "installed_app": exists_file(os.environ["FLUX_APP_BIN"]),
        "launcher": exists_file(os.environ["LAUNCHER_PATH"]),
        "natron_launcher": exists_file(os.environ["NATRON_LAUNCHER_PATH"]),
    },
    "plugins": {
        "pyplug_dir": exists_dir(str(pyplug_dir)),
        "required_pyplugs": [{"name": name, "installed": (pyplug_dir / name).is_file()} for name in required_pyplugs],
        "ofx_dir": exists_dir(str(ofx_dir)),
        "required_ofx": [{"name": name, "installed": (ofx_dir / name).is_dir()} for name in required_ofx],
    },
    "ai": {
        "tools": {
            "model_manager": (ai_root / "flux_model_manager.py").is_file(),
            "runtime_manager": (ai_root / "flux_provider_runtime.py").is_file(),
            "manifest": (ai_root / "model_manifest.json").is_file(),
        },
        "runtimes": [
            {"id": rid, "venv_python": str(runtime_root / rid / "venv" / "bin" / "python"), "installed": (runtime_root / rid / "venv" / "bin" / "python").is_file()}
            for rid in ("sam3", "matanyone2", "videomama", "sam31")
        ],
        "models": [
            {"id": mid, "path": str(model_root / mid), "present": (model_root / mid).is_dir()}
            for mid in ("sam3_transformers", "matanyone2", "videomama", "sam31_sam3plus")
        ],
    },
}
print(json.dumps(payload, indent=2, sort_keys=True))
PY
}

run_private_action() {
  shift
  local action="${1:-}"; shift || true
  case "$action" in
    full-bootstrap) run_full_bootstrap ;;
    deploy-runtime) run_deploy_runtime ;;
    build-all) run_build_all ;;
    configure) configure_flux ;;
    fedora-deps) install_fedora_packages ;;
    rpmfusion) enable_rpmfusion_free ;;
    checks) run_checks ;;
    launch) launch_flux ;;
    uninstall) uninstall_flux ;;
    status-summary) print_status_summary ;;
    diagnostics-json) run_installer_diagnostics_json ;;
    ai-list) run_ai_model_list ;;
    ai-status) run_ai_model_status ;;
    ai-install) run_ai_model_install "$@" ;;
    ai-remove) run_ai_model_remove "$@" ;;
    ai-token) run_ai_token_entry ;;
    ai-runtime-status) run_ai_runtime_status "$@" ;;
    ai-runtime-install) run_ai_runtime_install "$@" ;;
    ai-runtime-self-check) run_ai_runtime_self_check "$@" ;;
    *) die "Unknown private Flux setup action: ${action}" ;;
  esac
}

launch_curses_tui() {
  command -v python3 >/dev/null 2>&1 || die 'python3 is required for the Flux installer TUI.'
  python3 - <<'PY' >/dev/null 2>&1 || die 'Python stdlib curses is required for the Flux installer TUI.'
import curses
PY
  exec python3 "${SCRIPT_DIR}/flux_linux_setup_tui.py"
}

launch_python_gui() {
  command -v python3 >/dev/null 2>&1 || die 'python3 is required for the Flux graphical installer.'
  if python3 "${SCRIPT_DIR}/flux_linux_setup_gui.py"; then
    return 0
  fi
  if [[ -t 0 && -t 1 ]]; then
    warn 'Graphical installer could not start; falling back to terminal installer.'
    launch_curses_tui
  else
    die 'Graphical installer could not start and no terminal is available for fallback.'
  fi
}

main() {
  parse_args "$@"
  refresh_derived_paths
  if is_private_dispatch "$@"; then
    run_private_action "$@"
  elif [[ "$DO_TUI" -eq 1 ]]; then
    launch_curses_tui
  else
    launch_python_gui
  fi
}

main "$@"
