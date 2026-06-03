# Locator Report

## Summary
T084 implementation appears largely complete; remaining closure is validation/signoff, not installer logic, and the suspected `FluxTextRender` stale-binary issue is not present in the current tree because build/repo hashes match.

## Confidence
high

## Relevant Locations

1. `file:///home/npittas/Flux/tools/linux/flux-linux-setup.sh`
   - symbol: `discover_plugin_payloads`
   - approximate lines: 114-201
   - stable anchor: `discover_plugin_payloads()`
   - why relevant: Builds runtime deploy lists from repo source of truth.
   - evidence: Required PyPlugs are explicit at lines 114-119; core OFX includes `FluxTextRender.ofx.bundle` at 123-127; extra OFX bundles at 129-136; discovery appends top-level `plugins/*.py` and `.ofx.bundle` under `plugins/` and `plugins/ofx-extras/`.

2. `file:///home/npittas/Flux/tools/linux/flux-linux-setup.sh`
   - symbol: `install_runtime_payloads`, `deploy_pyplugs`, `deploy_ofx_bundles`
   - approximate lines: 861-958
   - stable anchor: `install_runtime_payloads()`
   - why relevant: Actual deploy surface for PyPlugs, bundled/community PyPlug trees, and OFX bundles.
   - evidence: Copies `Gui/Resources/PyPlugs`, `plugins/natron-plugins`, discovered PyPlugs, and OFX bundles into `$FLUX_INSTALL_PREFIX/Plugins`.

3. `file:///home/npittas/Flux/tools/linux/flux-linux-setup.sh`
   - symbol: `build_ofx_flux`
   - approximate lines: 688-700
   - stable anchor: `cmake --install "$ofx_flux_build_dir" --prefix "${PLUGIN_PREFIX}"`
   - why relevant: Fix path for suspected stale `FluxTextRender` binary; `--build` installs fresh OFX bundle into repo `plugins/`.
   - evidence: Builds `openfx-flux` and installs `FluxTextRender.ofx.bundle` to `PLUGIN_PREFIX`.

4. `file:///home/npittas/Flux/tools/linux/flux-linux-setup.sh`
   - symbol: `write_launcher`
   - approximate lines: 1001-1022
   - stable anchor: `export NATRON_PLUGIN_PATH=`
   - why relevant: Confirms installed PyPlug paths are used, not generic Natron profile.
   - evidence: Launcher exports `NATRON_PLUGIN_PATH=$FLUX_INSTALL_PREFIX/Plugins/PyPlugs:$FLUX_INSTALL_PREFIX/Plugins/PyPlugs/natron-plugins` and `OFX_PLUGIN_PATH=$FLUX_INSTALL_PREFIX/Plugins/OFX`.

5. `file:///home/npittas/Flux/INSTALL_FLUX_LINUX.md`
   - symbol: Plugin payloads installed by bootstrap
   - approximate lines: 193-260
   - stable anchor: `## 5. Plugin payloads installed by bootstrap`
   - why relevant: Documents current deploy behavior and stale `FluxTextRender` prevention.
   - evidence: Lists required PyPlugs/OFX bundles and explicitly says to run installer with `--build` or copy `build/openfx-flux/FluxTextRender.ofx` into repo bundle before `--deploy-extras`.

6. `file:///home/npittas/Flux/tasks/T084-plugin-payload-discovery.md`
   - symbol: T084 task record
   - approximate lines: 32-90
   - stable anchor: `## Remaining Validation`
   - why relevant: Captures implemented state and closure items.
   - evidence: Implementation notes say runtime deploy lists are built via `discover_plugin_payloads`; remaining validation is fresh Fedora NVIDIA distrobox + installed Flux runtime cache IDs.

7. `file:///home/npittas/Flux/tasks/TASKS.md`
   - symbol: T084 row
   - approximate line: 162
   - stable anchor: `| T084 | Plugin payload discovery/deploy completeness`
   - why relevant: T084 still `IN_PROGRESS`.
   - evidence: Task list has not been closed.

## Allowed Edit Scope Recommendation
- No code edit is indicated before validation unless validation exposes a failure.
- After validation passes, allowed closure edits should be limited to:
  - `file:///home/npittas/Flux/tasks/T084-plugin-payload-discovery.md`
  - `file:///home/npittas/Flux/tasks/TASKS.md`
  - optionally `file:///home/npittas/Flux/plans/PHASES.md` only if phase progress changes.

## Read-Only Context Recommendation
- `file:///home/npittas/Flux/tools/linux/flux-linux-setup.sh`
- `file:///home/npittas/Flux/INSTALL_FLUX_LINUX.md`
- `file:///home/npittas/Flux/tasks/T084-plugin-payload-discovery.md`
- `file:///home/npittas/Flux/tasks/TASKS.md`

## Validation Targets
- tests:
  - Fresh Fedora NVIDIA distrobox/bootstrap validation.
  - Installed Flux launch after cache clear.
  - Runtime OFX cache contains required IDs, especially `net.flux.openfx.TextRender`.
- commands:
  - `bash -n tools/linux/flux-linux-setup.sh`
  - sandbox deploy/uninstall sequence from `tasks/T084-plugin-payload-discovery.md`
  - `./tools/linux/flux-linux-setup.sh --check --validate-ldd`
  - `./tools/linux/flux-linux-setup.sh --validate-ofx-discovery`
  - hash check:
    ```bash
    sha256sum build/openfx-flux/FluxTextRender.ofx \
      plugins/FluxTextRender.ofx.bundle/Contents/Linux-x86-64/FluxTextRender.ofx
    ```
- manual checks:
  - Launch installed `flux`, not build-tree `Natron`.
  - Confirm launcher points at `$FLUX_INSTALL_PREFIX/Plugins`.

## Risks / Unknowns
- Codemap index was stale due changed git commit and dirty/untracked `build-logs/`; I updated it because the task allowed safe refresh.
- Current hash check shows no stale `FluxTextRender` bug:
  - `build/openfx-flux/FluxTextRender.ofx`
  - `plugins/FluxTextRender.ofx.bundle/Contents/Linux-x86-64/FluxTextRender.ofx`
  - both hash: `e1a27f56c90ac3a56c3d79b89ed284380424a74816cf4c4269f8e7ba9f1c4777`
- I did not write `/home/npittas/Flux/build-logs/locator-T084-plugin-payload-closure.md` because this role has a hard no-edit rule.

## Stop Recommendation
Implementation should not proceed now. Proceed with validation/closure only; edit task status after validation passes.