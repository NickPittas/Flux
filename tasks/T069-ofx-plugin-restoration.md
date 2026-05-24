# T069 — Restore missing OFX provider coverage

## Status

`DONE`

## Goal

Restore full native OFX coverage expected by bundled Natron/PyPlug tools, starting with PyPlugs that currently fail because their internal `app.createNode()` dependencies are missing at runtime.

## Approved Direction

Build and install the missing OFX providers. Do **not** hide, skip, unregister, or otherwise mask broken PyPlugs as the fix.

## Scope

- Audit bundled PyPlug `app.createNode()` dependencies.
- Compare dependencies against Natron's runtime OFX registry/cache, not just filesystem bundle presence.
- Identify missing provider source/provenance.
- Build/install exact legacy-compatible OFX plugins where possible.
- Validate each new `.ofx` binary with loader checks before relying on it.
- Clear only the scoped Natron OFX load cache after installation.
- Validate the failing PyPlugs by creating them successfully in the app.

## Known Missing/Risk Dependencies

| Plugin ID | Known consumer | Expected provider |
|---|---|---|
| `net.sf.openfx.SeNoise` | `lp_roughenEdges`, `lp_NoiseDistort` | NatronGitHub/openfx-io `SeExpr.ofx` |
| `fr.inria.openfx.SeExprSimple` | `lp_SimpleKeyer`, Vector Tools | NatronGitHub/openfx-io `SeExpr.ofx` |
| `net.fxarena.openfx.Text` | PyPlug dependency audit | openfx-arena |
| `net.fxarena.openfx.Tile` | PyPlug dependency audit | openfx-arena |
| `OpenFX.Yo.ResolveMath` | Vector Tools | OpenFX.Yo provider audit required |

## Current Results — 2026-05-24

Installed exact-source user OFX bundles under `/home/npittas/.OFX/Plugins`:

| Bundle | Restored IDs | Validation |
|---|---|---|
| `SeExpr.ofx.bundle` | `fr.inria.openfx.SeExpr`, `fr.inria.openfx.SeExprSimple`, `net.sf.openfx.SeNoise`, `net.sf.openfx.SeGrain` | `ldd` clean; runtime OFX cache contains all 4 IDs |
| `Text.ofx.bundle` | `net.fxarena.openfx.Text`, `net.fxarena.openfx.RichText` | `ldd` clean; runtime OFX cache contains `net.fxarena.openfx.Text` |
| `Magick.ofx.bundle` | `net.fxarena.openfx.Tile` | `ldd` clean with bundled Magick libs; runtime OFX cache contains `net.fxarena.openfx.Tile` |
| `ResolveMath.ofx.bundle` | `OpenFX.Yo.ResolveMath` | `ldd` clean; runtime OFX cache contains `OpenFX.Yo.ResolveMath`; patched from closest public BaldavengerOFX ResolveMath source per approval |

Bundled PyPlug providers found and accounted for in the dependency audit:

- `fr.inria.Fill` → `Gui/Resources/PyPlugs/Fill.py`
- `fr.inria.ZRemap` → `Gui/Resources/PyPlugs/ZRemap.py`

Post-install artifacts regenerated:

- `/tmp/opencode/flux-pyplug-dependencies.csv`
- `/tmp/opencode/flux-pyplug-dependencies.json`
- `/tmp/opencode/flux-ofx-runtime-registry.csv`
- `/tmp/opencode/flux-ofx-runtime-registry.json`
- `/tmp/opencode/flux-missing-ofx-dependencies.md`

Validation passed for the original failing PyPlugs via background script-file mode, avoiding unsafe `-b -c` command mode:

```text
FLUX_VALIDATE_NODE_OK lp_roughenEdges lp_roughenEdges1
FLUX_VALIDATE_NODE_OK lp_SimpleKeyer lp_SimpleKeyer1
FLUX_VALIDATE_NODE_OK comunity.plugins.Luma_to_Normals Luma_to_Normals1
FLUX_VALIDATE_NODE_OK comunity.plugins.Vectors_Normalize Vectors_Normalize1
```

Build validation passed:

```text
cmake --build build --target Natron -j 8
```

Final dependency audit:

- Runtime OFX IDs: 466
- PyPlug providers indexed: 305
- PyPlug dependency edges scanned: 4351
- Missing dependency IDs: 0

Diagnostics validation also passed:

- Missing node ID emits plugin ID, requested version, reason, and help text instead of only downstream `NoneType` confusion.
- Cold-cache and warm-cache broken OFX binary tests report the exact missing shared library and `ldd`/cache-clear help.
- Oracle re-review verdict: SHIP.

## Validation Plan

1. Generate dependency artifacts:
   - `/tmp/opencode/flux-pyplug-dependencies.csv`
   - `/tmp/opencode/flux-pyplug-dependencies.json`
   - `/tmp/opencode/flux-missing-ofx-dependencies.md`
2. Generate runtime registry artifacts:
   - `/tmp/opencode/flux-ofx-runtime-registry.csv`
   - `/tmp/opencode/flux-ofx-runtime-registry.json`
3. Build/install missing bundles into a confirmed scanned path, preferably `/home/npittas/.OFX/Plugins`.
4. Run `ldd` or equivalent loader checks on each installed `.ofx` binary.
5. Clear only `/home/npittas/.cache/INRIA/Natron/OFXLoadCache/`.
6. Launch Flux to rebuild the cache.
7. Confirm the runtime registry contains the formerly missing IDs.
8. Validate actual failing PyPlugs:
   - `lp_roughenEdges`
   - `lp_SimpleKeyer`

## Guardrails

- Preserve autosaves.
- Do not use command-mode `-b -c` validation while the Python 3.14 GIL crash remains unresolved.
- Do not change UI/export/autosave behavior during OFX restoration.
- Avoid `sudo` unless Nick approves.
- Do not map/rename plugin IDs unless Nick explicitly approves.
