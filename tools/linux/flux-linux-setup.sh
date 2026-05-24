#!/usr/bin/env bash
# Flux Linux workstation setup/check helper.
#
# Default mode is non-destructive: it checks the current machine and prints
# missing pieces. Mutating actions require explicit flags.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
FLUX_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd -P)"

USER_PYPLUG_DIR="${FLUX_USER_PYPLUG_DIR:-${HOME}/.Natron/PyPlugs}"
USER_OFX_DIR="${FLUX_USER_OFX_DIR:-${HOME}/.OFX/Plugins}"
OFX_CACHE_DIR="${FLUX_OFX_CACHE_DIR:-${HOME}/.cache/INRIA/Natron/OFXLoadCache}"
LAUNCHER_PATH="${FLUX_LAUNCHER_PATH:-${HOME}/.local/bin/flux}"

EXTRAS_SOURCE="${FLUX_OFX_EXTRAS:-${FLUX_ROOT}/plugins/ofx-extras}"
STAGE_EXTRAS_DIR=""
BUILD_DIR="${FLUX_BUILD_DIR:-${FLUX_ROOT}/build}"
BUILD_TYPE="${FLUX_BUILD_TYPE:-RelWithDebInfo}"
BUILD_JOBS="${FLUX_BUILD_JOBS:-}"

DO_CHECK=0
CHECK_EXPLICIT=0
ACTION_REQUESTED=0
DO_BOOTSTRAP=0
DO_UPDATE_SUBMODULES=0
DO_ENABLE_RPMFUSION=0
DO_VERIFY_FEDORA_REPOS=0
DO_INSTALL_DEPS=0
DO_CONFIGURE=0
DO_BUILD=0
DO_DEPLOY_EXTRAS=0
DO_INSTALL_LAUNCHER=0
DO_CLEAR_OFX_CACHE=0
DO_VALIDATE_LDD=0
FORCE=0
COPY_MODE="copy"

FEDORA_PACKAGES=(
  cmake
  extra-cmake-modules
  gcc
  gcc-c++
  make
  ninja-build
  git
  boost-devel
  qt6-qtbase-devel
  qt6-qtbase-gui
  python3
  python3-devel
  python3-pyside6
  python3-shiboken6
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
  libzip-devel
  minizip-ng-devel
  eigen3-devel
  glog-devel
  ceres-solver-devel
)

PYPLUG_FILES=(
  "${FLUX_ROOT}/plugins/FluxLayer.py"
  "${FLUX_ROOT}/plugins/FluxSolid.py"
  "${FLUX_ROOT}/plugins/FluxText.py"
)

OFX_CORE_BUNDLES=(
  "IO.ofx.bundle"
  "Misc.ofx.bundle"
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
)

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
Usage: ${0##*/} [options]

Checks and prepares a Linux workstation for Flux.

Default:
  --check                    Check packages, build output, PyPlugs, OFX bundles.

One-command setup:
  --bootstrap                Fedora path after clone: update submodules, verify
                             RPM Fusion free, verify package availability,
                             install deps, configure, build, deploy plugins,
                             clear OFX cache, install launcher, and
                             ldd-validate OFX bundles.

Mutating actions:
  --update-submodules        Run git submodule update --init --recursive.
  --enable-rpmfusion         Install Fedora RPM Fusion free release package.
  --verify-fedora-repos      Verify all Fedora package names are available.
  --install-deps             Install Fedora build/runtime packages with sudo dnf.
  --configure                Configure CMake for Flux Qt6 build.
  --build                    Build the Natron/Flux GUI target.
  --deploy-extras            Install Flux PyPlugs and OFX bundles into user paths.
  --install-launcher         Write ${LAUNCHER_PATH}.
  --clear-ofx-cache          Remove only ${OFX_CACHE_DIR}.
  --stage-extras DIR         Copy validated OFX bundles into DIR for transfer.

Validation:
  --validate-ldd             Run ldd checks on installed OFX binaries.

Options:
  --extras-source DIR        Source directory for extra OFX bundles.
                             Default: ${EXTRAS_SOURCE}
                             Fallback during deploy: ${USER_OFX_DIR}
  --symlink                  Symlink PyPlugs/OFX bundles instead of copying.
  --copy                     Copy PyPlugs/OFX bundles. Default.
  --build-dir DIR            CMake build directory. Default: ${BUILD_DIR}
  --build-type TYPE          CMake build type. Default: ${BUILD_TYPE}
  --jobs N                   Parallel build jobs. Default: nproc.
  --force                    Replace Flux-managed target files/bundles.
  --no-check                 Skip default check stage.
  -h, --help                 Show this help.

Examples:
  ${0##*/} --bootstrap
  ${0##*/} --check
  ${0##*/} --install-deps
  ${0##*/} --configure --build
  ${0##*/} --deploy-extras --install-launcher --validate-ldd
  ${0##*/} --stage-extras dist/flux-linux-ofx-extras
EOF
}

parse_args() {
  while [[ $# -gt 0 ]]; do
    case "$1" in
      --check)
        DO_CHECK=1
        CHECK_EXPLICIT=1
        shift
        ;;
      --no-check)
        DO_CHECK=0
        shift
        ;;
      --bootstrap)
        DO_BOOTSTRAP=1
        ACTION_REQUESTED=1
        shift
        ;;
      --update-submodules)
        DO_UPDATE_SUBMODULES=1
        ACTION_REQUESTED=1
        shift
        ;;
      --enable-rpmfusion)
        DO_ENABLE_RPMFUSION=1
        ACTION_REQUESTED=1
        shift
        ;;
      --verify-fedora-repos)
        DO_VERIFY_FEDORA_REPOS=1
        ACTION_REQUESTED=1
        shift
        ;;
      --install-deps)
        DO_INSTALL_DEPS=1
        ACTION_REQUESTED=1
        shift
        ;;
      --configure)
        DO_CONFIGURE=1
        ACTION_REQUESTED=1
        shift
        ;;
      --build)
        DO_BUILD=1
        ACTION_REQUESTED=1
        shift
        ;;
      --deploy-extras)
        DO_DEPLOY_EXTRAS=1
        ACTION_REQUESTED=1
        shift
        ;;
      --install-launcher)
        DO_INSTALL_LAUNCHER=1
        ACTION_REQUESTED=1
        shift
        ;;
      --clear-ofx-cache)
        DO_CLEAR_OFX_CACHE=1
        ACTION_REQUESTED=1
        shift
        ;;
      --validate-ldd)
        DO_VALIDATE_LDD=1
        ACTION_REQUESTED=1
        shift
        ;;
      --extras-source)
        [[ $# -ge 2 ]] || die '--extras-source requires a directory'
        EXTRAS_SOURCE="$2"
        shift 2
        ;;
      --stage-extras)
        [[ $# -ge 2 ]] || die '--stage-extras requires a directory'
        STAGE_EXTRAS_DIR="$2"
        ACTION_REQUESTED=1
        shift 2
        ;;
      --symlink)
        COPY_MODE="symlink"
        shift
        ;;
      --copy)
        COPY_MODE="copy"
        shift
        ;;
      --build-dir)
        [[ $# -ge 2 ]] || die '--build-dir requires a directory'
        BUILD_DIR="$2"
        shift 2
        ;;
      --build-type)
        [[ $# -ge 2 ]] || die '--build-type requires a value'
        BUILD_TYPE="$2"
        shift 2
        ;;
      --jobs)
        [[ $# -ge 2 ]] || die '--jobs requires a number'
        BUILD_JOBS="$2"
        shift 2
        ;;
      --force)
        FORCE=1
        shift
        ;;
      -h|--help)
        usage
        exit 0
        ;;
      *)
        die "Unknown option: $1"
        ;;
    esac
  done

  if [[ "$DO_BOOTSTRAP" -eq 1 ]]; then
    DO_UPDATE_SUBMODULES=1
    DO_ENABLE_RPMFUSION=1
    DO_VERIFY_FEDORA_REPOS=1
    DO_INSTALL_DEPS=1
    DO_CONFIGURE=1
    DO_BUILD=1
    DO_DEPLOY_EXTRAS=1
    DO_CLEAR_OFX_CACHE=1
    DO_INSTALL_LAUNCHER=1
    DO_VALIDATE_LDD=1
  fi

  if [[ "$ACTION_REQUESTED" -eq 0 && "$CHECK_EXPLICIT" -eq 0 ]]; then
    DO_CHECK=1
  fi
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
    warn "Install with: ${0} --install-deps"
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
    warn 'Enable Fedora updates and RPM Fusion free, then retry.'
    return 1
  fi

  log 'Fedora repository package availability check passed.'
}

enable_rpmfusion_free() {
  local os_id fedora_version rpmfusion_url
  os_id="$(detect_os)"
  [[ "$os_id" == "fedora" ]] || die "RPM Fusion setup only supports Fedora; detected '${os_id}'."
  command -v rpm >/dev/null 2>&1 || die 'rpm is required to check RPM Fusion.'
  command -v sudo >/dev/null 2>&1 || die 'sudo is required to enable RPM Fusion.'
  command -v dnf >/dev/null 2>&1 || die 'dnf is required to enable RPM Fusion.'

  if rpm -q rpmfusion-free-release >/dev/null 2>&1; then
    log 'RPM Fusion free is already enabled.'
    return 0
  fi

  fedora_version="$(rpm -E %fedora)"
  rpmfusion_url="https://download1.rpmfusion.org/free/fedora/rpmfusion-free-release-${fedora_version}.noarch.rpm"
  log "Enabling RPM Fusion free for Fedora ${fedora_version}."
  sudo dnf install -y "$rpmfusion_url"
}

install_fedora_packages() {
  local os_id
  os_id="$(detect_os)"
  [[ "$os_id" == "fedora" ]] || die "--install-deps currently supports Fedora only; detected '${os_id}'."
  command -v sudo >/dev/null 2>&1 || die 'sudo is required for --install-deps.'
  command -v dnf >/dev/null 2>&1 || die 'dnf is required for --install-deps.'

  log 'Installing Fedora packages. RPM Fusion free must be enabled for ffmpeg-devel.'
  sudo dnf install -y "${FEDORA_PACKAGES[@]}"
}

update_submodules() {
  command -v git >/dev/null 2>&1 || die 'git is required to update submodules.'
  log 'Updating git submodules.'
  git -C "$FLUX_ROOT" submodule update --init --recursive
}

configure_flux() {
  command -v cmake >/dev/null 2>&1 || die 'cmake is required to configure Flux.'
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
}

replace_target() {
  local target="$1"
  if [[ -e "$target" || -L "$target" ]]; then
    if [[ "$FORCE" -ne 1 ]]; then
      warn "Target exists, leaving unchanged: ${target} (use --force to replace)"
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
  log "Installed ${target}"
}

deploy_pyplugs() {
  local pyplug
  mkdir -p "$USER_PYPLUG_DIR"
  for pyplug in "${PYPLUG_FILES[@]}"; do
    copy_or_symlink "$pyplug" "${USER_PYPLUG_DIR}/$(basename "$pyplug")"
  done
}

bundle_source_for() {
  local bundle="$1"
  local candidate

  candidate="${FLUX_ROOT}/plugins/${bundle}"
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

  candidate="${FLUX_ROOT}/plugins/${bundle}"
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
      warn "Missing OFX bundle source for ${bundle}. Provide --extras-source DIR or stage extras first."
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
    copy_or_symlink "$source" "${USER_OFX_DIR}/${bundle}"
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

if [[ -z "\${QT_PLUGIN_PATH:-}" ]]; then
  if command -v qtpaths6 >/dev/null 2>&1; then
    QT_PLUGIN_PATH="\$(qtpaths6 --plugin-dir)"
  elif command -v qtpaths-qt6 >/dev/null 2>&1; then
    QT_PLUGIN_PATH="\$(qtpaths-qt6 --plugin-dir)"
  elif command -v qmake6 >/dev/null 2>&1; then
    QT_PLUGIN_PATH="\$(qmake6 -query QT_INSTALL_PLUGINS)"
  else
    for candidate in \
      /usr/lib64/qt6/plugins \
      /usr/lib/x86_64-linux-gnu/qt6/plugins \
      /usr/lib/qt6/plugins; do
      if [[ -d "\$candidate" ]]; then
        QT_PLUGIN_PATH="\$candidate"
        break
      fi
    done
  fi
fi
if [[ -n "\${QT_PLUGIN_PATH:-}" ]]; then
  export QT_PLUGIN_PATH
fi
export QT_QPA_PLATFORM="\${QT_QPA_PLATFORM:-xcb}"
export NATRON_PLUGIN_PATH="\${NATRON_PLUGIN_PATH:-${USER_PYPLUG_DIR}:${FLUX_ROOT}/Gui/Resources/PyPlugs:${FLUX_ROOT}/plugins/natron-plugins}"
export OFX_PLUGIN_PATH="\${OFX_PLUGIN_PATH:-${USER_OFX_DIR}:${FLUX_ROOT}/plugins}"

exec "${BUILD_DIR}/App/Natron" "\$@"
EOF
  chmod 0755 "$LAUNCHER_PATH"
  log "Installed launcher: ${LAUNCHER_PATH}"
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
    *) return 1 ;;
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
    output="$(ldd "$binary" 2>&1 || true)"
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
  if [[ ! -d "${FLUX_ROOT}/plugins/natron-plugins" || ! -f "${FLUX_ROOT}/plugins/natron-plugins/README.md" ]]; then
    warn "Missing community PyPlug submodule content: ${FLUX_ROOT}/plugins/natron-plugins"
    warn 'Run: git submodule update --init --recursive'
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
    warn "Build with: ${0} --configure --build"
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

main() {
  parse_args "$@"

  if [[ "$DO_UPDATE_SUBMODULES" -eq 1 ]]; then
    update_submodules
  fi

  if [[ "$DO_BOOTSTRAP" -eq 1 ]]; then
    preflight_ofx_bundle_sources
  fi

  if [[ "$DO_ENABLE_RPMFUSION" -eq 1 ]]; then
    enable_rpmfusion_free
  fi

  if [[ "$DO_VERIFY_FEDORA_REPOS" -eq 1 ]]; then
    verify_fedora_repos
  fi

  if [[ "$DO_INSTALL_DEPS" -eq 1 ]]; then
    install_fedora_packages
  fi

  if [[ "$DO_CONFIGURE" -eq 1 ]]; then
    configure_flux
  fi

  if [[ "$DO_BUILD" -eq 1 ]]; then
    build_flux
  fi

  if [[ "$DO_DEPLOY_EXTRAS" -eq 1 ]]; then
    deploy_pyplugs
    deploy_ofx_bundles
  fi

  if [[ -n "$STAGE_EXTRAS_DIR" ]]; then
    stage_extras "$STAGE_EXTRAS_DIR"
  fi

  if [[ "$DO_CLEAR_OFX_CACHE" -eq 1 ]]; then
    clear_ofx_cache
  fi

  if [[ "$DO_INSTALL_LAUNCHER" -eq 1 ]]; then
    write_launcher
  fi

  if [[ "$DO_VALIDATE_LDD" -eq 1 ]]; then
    validate_ldd
  fi

  if [[ "$DO_CHECK" -eq 1 ]]; then
    run_checks
  fi

  if [[ "$DO_BOOTSTRAP" -eq 1 ]]; then
    log "Bootstrap complete. Launch Flux with: ${LAUNCHER_PATH}"
    log "After the first launch rebuilds the OFX cache, run: ${0} --check --validate-ldd"
  fi
}

main "$@"
