# Locator Report

## Summary
T083-0’s smallest reliable implementation surface is new `tools/ai/` files plus narrow installer/docs/task-status edits; existing repo conventions are concentrated in `tools/linux/flux-linux-setup.sh`, `INSTALL_FLUX_LINUX.md`, and T083 research/task docs.

## Confidence
high

## Relevant Locations

1. `file:///home/npittas/Flux/tools/linux/flux-linux-setup.sh`
   - symbol: top-level XDG/install path variables
   - approximate lines: 8–37
   - stable anchor: `XDG_BIN_HOME="${XDG_BIN_HOME:-${HOME}/.local/bin}"`
   - why relevant: Canonical installer path conventions for XDG data/bin/cache, install prefix, plugin/runtime locations.
   - evidence: Defines `FLUX_INSTALL_PREFIX="${FLUX_DATA_DIR}/Flux"`, `PYTHON_RUNTIME_DIR="${FLUX_INSTALL_PREFIX}/Plugins/python"`, `OFX_CACHE_DIR="${XDG_CACHE_HOME:-${HOME}/.cache}/INRIA/Natron/OFXLoadCache"`.

2. `file:///home/npittas/Flux/tools/linux/flux-linux-setup.sh`
   - symbol: `bootstrap_python_runtime`
   - approximate lines: 702–708
   - stable anchor: `bootstrap_python_runtime()`
   - why relevant: Existing Python dependency bootstrap hook; T083-0/2 can later add `huggingface_hub`/`keyring` here or defer docs-only for T083-0.
   - evidence: Runs `python3 -m pip install --upgrade --target "$PYTHON_RUNTIME_DIR" qtpy packaging`.

3. `file:///home/npittas/Flux/tools/linux/flux-linux-setup.sh`
   - symbols: `manifest_reset`, `manifest_add_path`, `manifest_add_tree`
   - approximate lines: 752–777
   - stable anchor: `manifest_add_path()`
   - why relevant: Existing install-manifest pattern for anything installed by helper scripts.
   - evidence: Writes `${FLUX_INSTALL_PREFIX}/install-manifest.txt`, recursively records installed trees.

4. `file:///home/npittas/Flux/tools/linux/flux-linux-setup.sh`
   - symbol: cold-cache validation pattern
   - approximate lines: 1100–1156
   - stable anchor: `XDG_CACHE_HOME="${temp_dir}/cache"`
   - why relevant: Good validation style for isolated cache/home checks; model-manager validation should similarly use temp XDG paths and avoid production secrets/cache.
   - evidence: Runs `NatronRenderer` with temp `HOME`, `XDG_CACHE_HOME`, `NATRON_DISK_CACHE_PATH`, `OFX_PLUGIN_PATH`.

5. `file:///home/npittas/Flux/INSTALL_FLUX_LINUX.md`
   - symbols/anchors: “Default XDG install layout”, “Lower-level commands for debugging/CI”
   - approximate lines: 160–340
   - stable anchor: `The default XDG install layout is:`
   - why relevant: Canonical user-facing docs style and validation commands; any T083 model-manager docs should use these variables, not absolute paths.
   - evidence: Documents `$XDG_DATA_HOME`, `$XDG_BIN_HOME`, `$XDG_CACHE_HOME`; command patterns include `tools/linux/flux-linux-setup.sh --check`, `--bootstrap-python`, `--validate-ldd`.

6. `file:///home/npittas/Flux/tasks/T083-ai-matte-depth.md`
   - symbol: `T083-0 — Legal/model manifest decisions`
   - approximate lines: 47–66
   - stable anchor: `### T083-0 — Legal/model manifest decisions`
   - why relevant: Direct task contract and stop conditions.
   - evidence: Names implementation surfaces `tools/ai/model_manifest.json`, `tools/ai/flux_model_manager.py`, requires model list/license/install status, warning CLI output, and forbids plaintext-token persistence.

7. `file:///home/npittas/Flux/t083-research/secure-token-install-strategy.md`
   - symbol: “Concrete recommended strategy for Flux”
   - approximate lines: 26–55
   - stable anchor: `## Concrete recommended strategy for Flux`
   - why relevant: Authoritative token/keyring/XDG design source for manager skeleton.
   - evidence: Requires no `huggingface_hub.login()`/`hf auth login`; uses Python `keyring` only with secure backends; one-shot token fallback; paths:
     - `${XDG_CACHE_HOME:-$HOME/.cache}/Flux/huggingface`
     - `${XDG_CACHE_HOME:-$HOME/.cache}/Flux/model-downloads`
     - `${XDG_DATA_HOME:-$HOME/.local/share}/Flux/models/...`
     - `${XDG_CONFIG_HOME:-$HOME/.config}/Flux/models.json`

8. `file:///home/npittas/Flux/t083-research/revised-synthesized-plan.md`
   - symbol: `Implement model manifest and manager`
   - approximate lines: 130–160
   - stable anchor: `Add flux-model-manager Python helper`
   - why relevant: Defines expected CLI shape: `list`, `install`, `verify`, `remove`, `login`, `logout`, offline bundle import.
   - evidence: Repeats secure token policy and model-manager commands.

9. `file:///home/npittas/Flux/plans/PHASES.md`
   - symbol: T083 phase row/details
   - approximate lines: 270–279
   - stable anchor: `T083: 🟡 IN_PROGRESS`
   - why relevant: Phase-level status and path-agnostic installer constraint.
   - evidence: States approved direction and warns installer/docs must use `$FLUX_ROOT`, `$BUILD_DIR`, `$PLUGIN_PREFIX`, `$OFX_USER_PLUGIN_DIR`, `$HOME`, `$XDG_CACHE_HOME`.

10. `file:///home/npittas/Flux/tasks/TASKS.md`
   - symbol: T083 row
   - approximate lines: 150–158
   - stable anchor: `T083 | AI matte/mask generation and video depth tools`
   - why relevant: Task status update surface if T083-0 is completed.
   - evidence: T083 is `IN_PROGRESS`; file points to `tasks/T083-ai-matte-depth.md`.

## Allowed Edit Scope Recommendation

- Create:
  - `file:///home/npittas/Flux/tools/ai/model_manifest.json`
  - `file:///home/npittas/Flux/tools/ai/flux_model_manager.py`
- Optional narrow edits for first coding packet:
  - `file:///home/npittas/Flux/tasks/T083-ai-matte-depth.md` — mark T083-0 evidence/status if completed.
  - `file:///home/npittas/Flux/tasks/TASKS.md` — only if task lifecycle requires status/date update.
  - `file:///home/npittas/Flux/INSTALL_FLUX_LINUX.md` — only if adding documented model-manager CLI usage.
- Defer for T083-2 unless explicitly approved:
  - `file:///home/npittas/Flux/tools/linux/flux-linux-setup.sh` dependency/install integration.

## Read-Only Context Recommendation

- `file:///home/npittas/Flux/tools/linux/flux-linux-setup.sh`
- `file:///home/npittas/Flux/INSTALL_FLUX_LINUX.md`
- `file:///home/npittas/Flux/t083-research/secure-token-install-strategy.md`
- `file:///home/npittas/Flux/t083-research/revised-synthesized-plan.md`
- `file:///home/npittas/Flux/tasks/T083-ai-matte-depth.md`

## Validation Targets

- tests:
  - JSON manifest parses and contains SAM3.1, MatAnyone2, BiRefNet, ViTMatte, Video Depth Anything, DepthCrafter, RVM, XMem2.
  - CLI skeleton can run offline: `list`, `verify`, model status output.
  - Non-commercial/gated/external-only warnings visible in CLI output.
  - Token audit: no plaintext token written to config/cache/logs.
- commands:
  - `python3 -m json.tool tools/ai/model_manifest.json >/dev/null`
  - `python3 -m py_compile tools/ai/flux_model_manager.py`
  - `python3 tools/ai/flux_model_manager.py --manifest tools/ai/model_manifest.json list`
  - `python3 tools/ai/flux_model_manager.py --manifest tools/ai/model_manifest.json verify --offline`
  - `XDG_CONFIG_HOME="$(mktemp -d)" XDG_CACHE_HOME="$(mktemp -d)" XDG_DATA_HOME="$(mktemp -d)" python3 tools/ai/flux_model_manager.py list`
- manual checks:
  - Confirm output labels gated/non-commercial/external-helper models clearly.
  - Confirm no instruction suggests `hf auth login` or `huggingface_hub.login()`.

## Risks / Unknowns

- Codemap index is stale because working tree has dirty/untracked files: `build-logs/`, `lans/PHASES.md`, `t083-research/`, task docs. I did not update because stale reason is dirty/untracked work.
- No existing `tools/ai/` convention exists; this is a new repo sublayout.
- No existing in-repo secure keyring implementation; T083-0 should skeleton/detect policy only, not persist secrets unless secure backend validation is implemented.
- User requested writing to `file:///home/npittas/Flux/t083-research/t083-0-locator.md`, but this locator role is explicitly no-edit/read-only, so I did not create or modify that file.

## Stop Recommendation

Implementation can proceed now for a narrow T083-0 packet limited to manifest + offline CLI skeleton + optional docs/status update; do not integrate downloads or installer mutations until T083-2.