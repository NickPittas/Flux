# Worker Report

## Status
done-with-concerns

## Changed Files
- file:///home/npittas/Flux/Gui/FluxAiPanel.h
- file:///home/npittas/Flux/Gui/FluxAiPanel.cpp
- file:///home/npittas/Flux/Gui/FluxAiWorkerController.h
- file:///home/npittas/Flux/Gui/FluxAiWorkerController.cpp
- file:///home/npittas/Flux/Gui/Gui05.cpp
- file:///home/npittas/Flux/Gui/CMakeLists.txt
- file:///home/npittas/Flux/tools/ai/flux_ai_worker.py
- file:///home/npittas/Flux/tasks/T083-ai-matte-depth.md

## What Changed
- Added `FluxAiPanel` QWidget/PanelWidget shell with required task/model/source/output/run/cancel/log UI.
- Added manifest runtime loading from `tools/ai/model_manifest.json`, including disabled fallback entry/log on failure.
- Added `FluxAiWorkerController` using direct `QProcess` to run:
  - `python3 <repo-root>/tools/ai/flux_ai_worker.py --job <temp-json-job-file>`
- Added stdlib-only no-op worker script emitting JSON-lines `started`, `progress`, and `finished`.
- Registered AI tab in `Gui05.cpp` after Export and before Text.
- Added explicit CMake entries because existing glob would not reliably include new files in the existing configured build.
- Added T083-1A evidence line while keeping T083-1 `PENDING`.

## Validation
Command:
```bash
python3 -m py_compile tools/ai/flux_ai_worker.py
python3 tools/ai/flux_ai_worker.py --job <(printf '{"task":"Base Matte","model_id":"birefnet","mode":"noop_cuda_probe"}')
cmake --build "${BUILD_DIR:-build}" --target NatronGui Natron -j$(nproc)
git diff --check -- Gui/FluxAiPanel.h Gui/FluxAiPanel.cpp Gui/FluxAiWorkerController.h Gui/FluxAiWorkerController.cpp Gui/Gui05.cpp Gui/CMakeLists.txt tools/ai/flux_ai_worker.py tasks/T083-ai-matte-depth.md
```

Result:
- Python compile passed.
- Worker emitted JSON-lines `started`, `progress`, `finished` and exited 0.
- Build passed for `NatronGui` and `Natron`.
- Diff check passed.
- Manual GUI validation was not run; no screenshot captured.

## Scope Check
Confirmed no files outside Allowed Edit Files were edited. The requested output artifact `t083-research/t083-1a-worker-result.md` was not written because it is outside Allowed Edit Files.

## Concerns / Follow-up
- Remaining T083-1 work: viewer prompt capture, generated media metadata/serialization, generated-media path policy, dummy worker save/reopen behavior, unsaved-project Force Save As behavior, and GUI/screenshot validation.