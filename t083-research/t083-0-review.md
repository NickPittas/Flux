# Review Report

## Verdict
ship

## Scope Compliance
- passed
- evidence: `git status --short -- tools/ai/model_manifest.json tools/ai/flux_model_manager.py tasks/T083-ai-matte-depth.md t083-research/t083-0-review.md` shows only the three expected implementation files as untracked before this review artifact: `tasks/T083-ai-matte-depth.md`, `tools/ai/flux_model_manager.py`, `tools/ai/model_manifest.json`. No installer, downloader, keyring, or auth files were changed.

## Validation Assessment
- command/result reviewed:
  - `python3 -m json.tool tools/ai/model_manifest.json >/dev/null` — passed.
  - `python3 -m py_compile tools/ai/flux_model_manager.py` — passed.
  - `python3 tools/ai/flux_model_manager.py --manifest tools/ai/model_manifest.json list` — passed; output visibly labels SAM3.1 as `GATED/USER-DOWNLOAD`, MatAnyone2 and DepthCrafter as `NON-COMMERCIAL/RESTRICTIVE`, and RVM/XMem2 as `EXTERNAL HELPER`.
  - `python3 tools/ai/flux_model_manager.py --manifest tools/ai/model_manifest.json verify --offline` — passed with `Manifest verification OK (offline); no network, downloads, logins, or secret writes performed.`
  - `python3 tools/ai/flux_model_manager.py --manifest tools/ai/model_manifest.json status` — passed; reports XDG-derived model/cache/config paths and per-model local paths.
  - temp XDG status command — passed; status reported paths under temporary XDG homes and follow-up `test ! -d` checks confirmed no directories were created.
  - `git diff --check -- tools/ai/model_manifest.json tools/ai/flux_model_manager.py tasks/T083-ai-matte-depth.md` — passed.
- sufficient? yes
- missing validation: none for T083-0 scope.

## Findings

No blockers or major findings.

### Minor
- Finding: `tools/ai/flux_model_manager.py` imports `Tuple` but does not use it.
- Why it matters: It is harmless at runtime but leaves a small cleanup item in a new stdlib-only utility.
- Evidence: `tools/ai/flux_model_manager.py:16` imports `Tuple`; no `Tuple` usage exists in the file.
- Suggested change: Remove `Tuple` from the typing import in a later cleanup; not required to ship T083-0.

## Trace Checked
- Manifest contents: `tools/ai/model_manifest.json:4-149` contains exactly eight entries with the required IDs/roles: SAM3.1/SAM3+ (`:6-21`), MatAnyone2 (`:24-39`), BiRefNet (`:42-57`), ViTMatte (`:60-75`), Video Depth Anything (`:78-93`), DepthCrafter (`:96-111`), RVM (`:114-129`), and XMem2 (`:132-147`). Each entry includes id, display name, category/role, priority, provider/runtime, install policy, bundling policy, license/warning, gated/token requirement, default flags, output notes, and placeholder source/revision/hash fields.
- CLI entry path: `main()` builds argparse and dispatches to command functions (`tools/ai/flux_model_manager.py:175-199`); `--manifest` defaults to sibling `model_manifest.json` via `default_manifest_path()` (`:62-63`, `:177`).
- `list`: loads the manifest, sorts by priority, and prints labels/warnings from manifest policy fields (`tools/ai/flux_model_manager.py:109-139`). The observed output satisfies the required visible labels.
- `verify --offline`: loads JSON and validates top-level/model required fields, unique IDs, priority type, boolean default flags, and `source` object (`tools/ai/flux_model_manager.py:74-106`, `:142-152`). It performs no network or writes.
- `status`: computes XDG paths from environment/defaults and prints existence checks only (`tools/ai/flux_model_manager.py:40-59`, `:155-172`); no `mkdir`, download, login, token, or persistence path exists.
- Task doc: T083-0 is marked DONE and current stage points to T083-1 next/PENDING (`tasks/T083-ai-matte-depth.md:3-5`, `:40-48`, `:50-57`).

## Simpler Alternative Check
This is already the smaller/safe approach for T083-0: a static manifest plus read-only, stdlib-only CLI skeleton. A smaller manifest-only change would not satisfy the requested list/verify/status behavior or warning proof.

## Final Recommendation
Ship T083-0; the implementation satisfies the packet without downloads, installer edits, token persistence, or scope creep.
