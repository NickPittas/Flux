# Planner Report

## Status
ready

## Rationale
This plan is sufficient and scoped because it implements one coherent runtime-infrastructure slice before any torch/SAM dependency installation: a separate provider runtime manager plus first SAM3.1 runtime manifest/self-check path, while preserving the existing model manager as model-assets-only and keeping Flux/Natron in-process Python free of PyTorch imports.

# Task Packet

## User Goal
Create provider-specific external AI runtime environments, starting with SAM3.1, before installing torch/dependencies. Do not install PyTorch/SAM dependencies into Natron's in-process Python or a single shared environment. Model assets stay under `~/.local/share/Flux/models`; runtime dependencies live in per-provider external subprocess environments such as `~/.local/share/Flux/ai-envs/sam31`. Flux launches the provider environment Python and never imports PyTorch in-process.

## Mode
general-coding

## Relevant Locations
- file: `tools/ai/flux_model_manager.py`
  symbol: `flux_paths()`, `model_local_path()`, CLI parser/actions
  approximate lines: 1-390
  stable anchor: `def flux_paths():`
  reason: Existing canonical model asset paths, secure Hugging Face token behavior, and CLI style to mirror without merging runtime dependencies into the model store.
  confidence: high
- file: `tools/ai/model_manifest.json`
  symbol: `sam31_sam3plus` model entry
  approximate lines: 1-35
  stable anchor: `"id": "sam31_sam3plus"`
  reason: Provides SAM3.1 model id, repo, pinned revision, install/access warnings, and current `runtime_type`; runtime manifest must reference this model id without moving model weights.
  confidence: high
- file: `tools/ai/sam31_real_inference_probe.py`
  symbol: `self_check_payload()`, `load_flux_model()`, CLI entry points
  approximate lines: 1-350
  stable anchor: `RESULT_NAME = "sam31_real_inference_result.json"`
  reason: Existing SAM3.1 probe currently reports missing `torch`/`transformers`; it must remain the real-inference/self-check target but be invoked through the provider env Python.
  confidence: high
- file: `tasks/T083-restart-real-inference-plan.md`
  symbol: prior restart plan
  approximate lines: whole file
  stable anchor: `Primary first target: SAM3.1/SAM3+ real inference`
  reason: Confirms SAM3.1-first direction, no placeholder success, and no in-GUI proof before real media output.
  confidence: high
- file: `tasks/T083-sam31-real-inference-proof-plan.md`
  symbol: prior SAM3.1 proof packet
  approximate lines: whole file
  stable anchor: `Prove SAM3.1 real inference outside the GUI first`
  reason: Existing proof packet validates the probe behavior but assumed dependencies in the current Python; this task supersedes that dependency-placement assumption.
  confidence: high
- file: `tools/linux/flux-linux-setup.sh`
  symbol: installer path defaults and AI action dispatch
  approximate lines: 1-220 and existing AI action sections as needed
  stable anchor: `PYTHON_RUNTIME_DIR="${PYTHON_RUNTIME_DIR:-${FLUX_PYTHON_RUNTIME_DIR:-${FLUX_INSTALL_PREFIX}/Plugins/python}}"`
  reason: Shows current installed app Python runtime path; new runtime env actions must not bootstrap torch into this in-process/plugin Python runtime.
  confidence: high
- file: `tools/linux/flux_linux_setup_tui.py`
  symbol: `ACTIONS`, `model_rows()`, AI menu actions
  approximate lines: 1-140
  stable anchor: `("ai-install-menu", "Install or update AI models"`
  reason: Existing TUI exposes AI model install/status; add narrow runtime install/status/self-check actions without changing GUI inference integration.
  confidence: high

## Allowed Edit Files
- `tools/ai/flux_provider_runtime.py`
- `tools/ai/provider_runtime_manifest.json`
- `tools/ai/sam31_real_inference_probe.py`
- `tools/linux/flux-linux-setup.sh`
- `tools/linux/flux_linux_setup_tui.py`

## Read-Only Context Files
- `tools/ai/flux_model_manager.py`
- `tools/ai/model_manifest.json`
- `tasks/T083-restart-real-inference-plan.md`
- `tasks/T083-sam31-real-inference-proof-plan.md`

## Required Change
Implement a minimal provider runtime manager and SAM3.1 runtime definition.

1. Runtime env root/layout:
   - Add canonical runtime root resolver using XDG data home: `${XDG_DATA_HOME:-~/.local/share}/Flux/ai-envs`.
   - First runtime env path: `${XDG_DATA_HOME:-~/.local/share}/Flux/ai-envs/sam31`.
   - Keep model assets exactly in the existing model manager path: `${XDG_DATA_HOME:-~/.local/share}/Flux/models/<model>/<revision>`.
   - Runtime layout must separate dependency/runtime files from model files, for example:
     - `ai-envs/sam31/venv/` or venv root directly under `sam31/` (choose one and document in status output)
     - `ai-envs/sam31/flux_runtime_install.json` with schema, provider id, Python executable, requested Python version, installed packages, torch CUDA index/channel, timestamps, and self-check result summary
     - optional `ai-envs/sam31/src/` only if a provider-specific external repo is explicitly installed later; do not clone Meta SAM repo by default in this first task unless required by the detected/verified API.

2. Runtime manifest/schema:
   - Choose a separate file `tools/ai/provider_runtime_manifest.json` rather than extending `model_manifest.json`. Rationale: model manifest remains about model assets, licenses, and downloads; provider runtime manifest describes Python/dependency environments and can evolve per provider without breaking model-manager validation.
   - Manifest v1 must include at least:
     - top-level `manifest_version`, `schema`, `runtimes[]`
     - runtime fields: `id`, `display_name`, `provider`, `models`, `env_dir_name`, `python`, `install_policy`, `packages`, `cuda`, `self_check`, `launch`
   - First runtime entry `sam31` must reference model id `sam31_sam3plus` and define the probe path `tools/ai/sam31_real_inference_probe.py` as the self-check target.

3. Add `tools/ai/flux_provider_runtime.py` CLI:
   - Commands: `list`, `status`, `install`, `self-check`, and `python` (prints resolved provider env Python path for orchestration/scripts).
   - Required usage examples:
     - `python3 tools/ai/flux_provider_runtime.py list --json`
     - `python3 tools/ai/flux_provider_runtime.py status sam31 --json`
     - `python3 tools/ai/flux_provider_runtime.py install sam31 --cuda cu128` (or equivalent explicit CUDA selector)
     - `python3 tools/ai/flux_provider_runtime.py self-check sam31 --json`
     - `$(python3 tools/ai/flux_provider_runtime.py python sam31) tools/ai/sam31_real_inference_probe.py --self-check --json`
   - The manager may use stdlib `venv` and subprocess `pip`; it must not import torch/transformers itself.
   - `install` must create/use only the provider env, upgrade pip tooling inside that env, install declared packages there, and write install metadata. Do not touch `Flux/Plugins/python`, Natron Python, or a shared mega-env.
   - `status` must report env path, env Python existence, installed metadata, model asset path/status by reusing/read-only importing `flux_model_manager` helpers where safe, and whether the runtime is ready/blocked.
   - `self-check` must launch the provider env Python as a subprocess running the configured self-check script; it must not import heavy ML modules in the manager process.

4. SAM3.1 first runtime dependency strategy:
   - Python version strategy: use the system `python3` only if it is compatible with torch wheels. Because this workspace reports Python 3.14, the installer must detect unsupported Python for torch and stop with a clear message instructing use of a supported interpreter override, e.g. `--python /usr/bin/python3.12` or `FLUX_AI_PYTHON=/path/to/python3.12`, rather than creating an env that cannot install torch.
   - CUDA PyTorch strategy: default to CUDA-first on Nick's NVIDIA workstation, but require an explicit supported torch CUDA wheel selector/index. The manifest/CLI should support `--cuda cu128`/`cu126`/`cpu` (exact accepted values may be limited to known PyTorch indices). Stop if the selected CUDA wheel/index is not known or pip cannot resolve matching torch packages for the chosen Python.
   - Packages: install `torch`, `torchvision` if needed by the SAM API, `transformers`, `accelerate` if required, `huggingface_hub`, `pillow`, `numpy`, and `opencv-python-headless` or `opencv-python` only if the probe/runtime actually needs cv2. Prefer headless OpenCV for subprocess workers.
   - Meta SAM repo vs Transformers uncertainty: do not assume both are required. First install the minimal Transformers path because the current probe tests `AutoProcessor`/`AutoModel` against the local Hugging Face model. If the self-check or actual API proves Transformers cannot load SAM3.1 and Meta's `sam3` repo/package is required, stop and report that a second, explicit runtime-revision task is needed to add the external repo/package under this provider env.
   - Do not download model weights in runtime install. Model download remains an explicit model-manager action.

5. Probe invocation through provider env Python:
   - Update `sam31_real_inference_probe.py` only as needed to make `--self-check --json` and normal proof execution robust when launched by an external venv Python from the repo root.
   - Preserve its no-placeholder behavior and existing model manager path lookup.
   - The runtime manager `self-check sam31` must invoke the probe as a subprocess using the resolved provider Python, not current/Natron Python.

6. Installer/TUI actions:
   - In `tools/linux/flux-linux-setup.sh`, add private actions for runtime manager operations, such as `ai-runtime-status`, `ai-runtime-install`, and `ai-runtime-self-check` for `sam31`.
   - Ensure existing Python runtime bootstrap (`PYTHON_RUNTIME_DIR` / `Flux/Plugins/python`) remains separate and is not used for torch/SAM packages.
   - In `tools/linux/flux_linux_setup_tui.py`, add actions for installing SAM3.1 runtime, checking runtime status, and running runtime self-check. Keep model install/download as separate menu actions.

7. Exact stop conditions for implementation:
   - Stop if any step would install torch/transformers/SAM dependencies into Natron in-process Python, `Flux/Plugins/python`, or a single shared env.
   - Stop if the only available Python is 3.14 and no compatible torch wheel exists for the selected torch/CUDA version.
   - Stop if runtime install would download SAM3.1 model weights implicitly.
   - Stop if SAM3.1 requires Meta repo/API rather than Transformers and the required repo/package/version is not verified in the manifest.
   - Stop if CUDA wheel selection is ambiguous or unsupported by PyTorch for the chosen Python.
   - Stop if self-check cannot run through the provider env Python.
   - Stop if success would be based on imports only while the self-check still reports model/runtime blockers.

## Non-Goals
- No PyTorch, SAM, Transformers, or OpenCV installation into Natron's in-process Python or `Flux/Plugins/python`.
- No shared mega-env for all providers.
- No model downloads unless the user explicitly runs the existing model install path.
- No GUI inference integration, nodegraph integration, AI panel behavior changes, or generated media workflow changes.
- No placeholder success, dummy masks, fake readiness, or treating import-only checks as real inference proof.
- No broad redesign of `flux_model_manager.py`; keep it focused on model assets.

## Validation
Commands:
- `cd /home/npittas/Flux && python3 tools/ai/flux_provider_runtime.py list --json`
- `cd /home/npittas/Flux && python3 tools/ai/flux_provider_runtime.py status sam31 --json`
- `cd /home/npittas/Flux && python3 tools/ai/flux_provider_runtime.py python sam31`
- `cd /home/npittas/Flux && python3 tools/ai/flux_provider_runtime.py self-check sam31 --json`
- If a compatible Python and approved CUDA selector are available: `cd /home/npittas/Flux && python3 tools/ai/flux_provider_runtime.py install sam31 --python /path/to/supported/python --cuda cu128`
- After install: `cd /home/npittas/Flux && $(python3 tools/ai/flux_provider_runtime.py python sam31) tools/ai/sam31_real_inference_probe.py --self-check --json`
- Confirm separation: `test ! -d /home/npittas/.local/share/Flux/Plugins/python/torch && test ! -d /home/npittas/.local/share/Flux/models/sam31/venv`

Expected result:
- Runtime commands exist and report the SAM3.1 runtime root under `~/.local/share/Flux/ai-envs/sam31`.
- Runtime status distinguishes missing env, missing model assets, unsupported Python, missing packages, and ready state.
- Self-check runs via provider env Python and reports real blockers or readiness without importing torch in the manager/Natron process.
- Model assets remain under `~/.local/share/Flux/models/<model>/<revision>` and runtime deps remain under `~/.local/share/Flux/ai-envs/<provider>`.

## Stop Conditions
Stop and report if:
- target symbols/files are missing or existing installer AI action structure differs enough that safe narrow edits are not possible
- required fix exceeds the allowed files
- validation cannot run
- current architecture contradicts external provider subprocess runtimes
- task requires product/design judgment not in packet
- dependency installation would mutate Natron/in-process Python or create a shared all-model environment
- Python/CUDA/PyTorch compatibility cannot be determined without a separate research/approval pass
- Meta SAM3 repo/package is required but unverified

## Required Return Contract
Return only a task-focused summary. Do not include transcript, tool logs, raw file dumps, large code blocks, or broad unrelated issues. Include status, files inspected/changed, validation evidence, blockers, and task-specific risks.
