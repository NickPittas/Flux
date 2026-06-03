# Worker Report

## Status
done-with-concerns

## Changed Files
- file:///home/npittas/Flux/tools/ai/model_manifest.json
- file:///home/npittas/Flux/tools/ai/flux_model_manager.py
- file:///home/npittas/Flux/tasks/T083-ai-matte-depth.md

## What Changed
- Added T083 model manifest with exactly 8 required entries:
  - SAM3.1 / SAM3+ line
  - MatAnyone2
  - BiRefNet
  - ViTMatte
  - Video Depth Anything
  - DepthCrafter
  - RVM
  - XMem2
- Added stdlib-only offline CLI skeleton:
  - `list`
  - `verify --offline`
  - `status`
  - XDG path resolution
  - manifest loading/schema validation
  - no downloads, no logins, no secret writes
- Updated T083-0 status to `DONE` and current stage to `T083-1 is next/PENDING`.

## Validation
Command:
```bash
python3 -m json.tool tools/ai/model_manifest.json >/dev/null
python3 -m py_compile tools/ai/flux_model_manager.py
python3 tools/ai/flux_model_manager.py --manifest tools/ai/model_manifest.json list
python3 tools/ai/flux_model_manager.py --manifest tools/ai/model_manifest.json verify --offline
python3 tools/ai/flux_model_manager.py --manifest tools/ai/model_manifest.json status
XDG_CONFIG_HOME="$(mktemp -d)" XDG_CACHE_HOME="$(mktemp -d)" XDG_DATA_HOME="$(mktemp -d)" python3 tools/ai/flux_model_manager.py --manifest tools/ai/model_manifest.json status
git diff --check -- tools/ai/model_manifest.json tools/ai/flux_model_manager.py tasks/T083-ai-matte-depth.md
```

Result:
- All commands exited `0`.
- `list` visibly labels:
  - SAM3.1/SAM3+ as `GATED/USER-DOWNLOAD`
  - MatAnyone2 and DepthCrafter as `NON-COMMERCIAL/RESTRICTIVE`
  - RVM/XMem2 as `EXTERNAL HELPER`
- `status` reported intended XDG paths and did not create directories.

## Scope Check
Confirmed no files outside Allowed Edit Files were edited.  
Did not write `/home/npittas/Flux/t083-research/t083-0-worker-result.md` because it was outside Allowed Edit Files.

## Concerns / Follow-up
- `git diff --check` does not inspect untracked file contents, but the required command was run and exited `0`.
- Exact upstream repo/revision/hash fields remain null/TODO-style manifest data for T083-2 pinning.