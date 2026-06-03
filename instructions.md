# Flux Workstation Installation and Repair Guide

This document describes the prerequisites, installation options, installer architecture, and troubleshooting procedures for a Flux workstation.

---

## 1. System Requirements

Before running the installer, ensure the host system matches these requirements:
*   **Operating System**: Fedora Workstation (first-class support; installer installs dependencies using `dnf`). Other distributions require manual installation of equivalents.
*   **Hardware**: NVIDIA GPU (RTX 30-series or newer recommended for AI features).
*   **Compiler & Toolchain**: GCC/G++ supporting C++17, CMake (3.16+), and Ninja.
*   **System Python**: Python 3.10 to 3.13 (Fedora 44 uses Python 3.14 by default; the installer will search for and locate Python 3.10-3.13 to bootstrap AI environments, as PyTorch does not support 3.14).

---

## 2. Installation Prerequisities

If running on a fresh Fedora installation, the installer will attempt to configure these, but they can be installed manually:
1.  **RPM Fusion**: Required for GPU drivers and restricted libraries.
2.  **System Packages**:
    *   Development tools: `cmake`, `ninja-build`, `gcc-c++`, `git`.
    *   Libraries: `qt6-qtbase-devel`, `qt6-qtdeclarative-devel`, `ffmpeg-devel`, `openimageio-devel`, `opencolorio-devel`.
    *   Python bindings: `python3-tkinter` (required for the GUI installer).

---

## 3. How to Run the Installer

The primary installer entry point is a single path-agnostic script in the repository root:

```bash
tools/linux/flux-linux-setup.sh
```

### Execution Modes:
1.  **Graphical Mode (Default)**: If a display is available, running the script with no arguments launches the Python Tkinter GUI.
2.  **Terminal Mode (Fallback)**: If no display is detected, or if passed explicitly, the script falls back to a curses TUI:
    ```bash
    tools/linux/flux-linux-setup.sh --tui
    ```
3.  **Command Mode (Quiet)**: Executed by launching `flux` after a successful install prefix deployment. All startup logs and console messages are written silently to `~/.local/state/Flux/flux-launch.log`.

---

## 4. What Each Installer Action Does

The installer exposes the following tasks:

### App Actions:
*   **Full Bootstrap**: Chains all steps sequentially (system dependencies → configure CMake → build app → deploy runtime → run checks).
*   **Install/Repair Runtime**: Deploys the compiled app binaries (`flux`, `FluxRenderer`) to the install prefix (`~/.local/share/Flux/bin`), creates launchers in `~/.local/bin/`, copies Python libraries, and deploys plugins.
*   **Configure CMake**: Runs CMake configuration for the Flux app using build files.
*   **Build Flux**: Compiles the `Natron` and `NatronRenderer` targets.
*   **Run Installer Checks**: Validates system packages, binaries, cache directories, and libraries.
*   **Launch Flux**: Spawns the compiled/installed launcher.

### Plugins & OFX Actions:
*   **PyPlugs**: Deploys native python plugins (`FluxLayer.py`, `FluxSolid.py`, `FluxText.py`, `FluxMotionText.py`) to the user PyPlug path.
*   **OFX Bundles**: Deploys OpenFX plugins (`IO.ofx.bundle`, `Misc.ofx.bundle`, `FluxTextRender.ofx.bundle`, etc.) to the user plugins path and clears the OFX cache to force discovery.

### AI Runtimes & Models:
*   **Isolated Virtual Environments**: Creates separate Python virtual environments for each provider under `~/.local/share/Flux/ai-envs/<id>`. This prevents dependency conflicts and avoids polluting the system Python.
    *   **SAM3**: Dedicated venv for Segment Anything 3.
    *   **MatAnyone2**: Dedicated venv for MatAnyone2 (installs dependencies and clones/patches the source repository).
    *   **VideoMaMa**: Dedicated venv for VideoMaMa workflows.
*   **Composite Model Downloader**: Downloads weights and configurations sequentially from Hugging Face into `~/.local/share/Flux/models/<id>`:
    *   Downloads SVD and VideoMaMa components from upstream repositories.
    *   Copies the bundled `pipeline_svd_mask_numpy.py` adapter next to the weights.
    *   Validates all required model/pipeline files before marking the model ready.
*   **Self-Checks**: Runs isolated worker checks (`--self-check`) inside the provider's venv to verify CUDA, torch, and model imports.

---

## 5. Post-Installation Commands

Once installation is complete, the following commands are available:
*   **flux**: Quiet launcher. Launch logs are directed to log files.
*   **natron**: Alias launcher. Runs the same wrapper.
*   **FLUX_VERBOSE_CONSOLE=1 flux**: Runs Flux with output directed back to the terminal.
