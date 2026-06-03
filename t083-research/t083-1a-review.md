# Review Report

## Verdict
fix-then-ship

## Scope Compliance
- failed
- evidence:
  - Expected/allowed files were limited to T083-1A files, but working tree also has modified/untracked paths outside that set: `plans/PHASES.md`, `tasks/FLUX_MOTION_TEXT_RECOVERY_SOURCE_OF_TRUTH.md`, `tasks/T084-plugin-payload-discovery.md`, `tasks/TASKS.md`, `build-logs/`, `t083-research/`.
  - `tools/ai/model_manifest.json` and `tools/ai/flux_model_manager.py` are present under untracked `tools/ai/`; if these are T083-0 carryover, they need to be separated from this T083-1A review scope.

## Validation Assessment
- command/result reviewed:
  - `python3 -m py_compile tools/ai/flux_ai_worker.py` — passed.
  - Temp job run via `python3 tools/ai/flux_ai_worker.py --job TEMP` — passed; emitted JSON-lines `started`, `progress`, `finished` with `cuda_available: null`.
  - `git diff --check -- ...` — passed.
  - `cmake --build "${BUILD_DIR:-build}" --target NatronGui Natron -j$(nproc)` — passed.
- sufficient? mostly yes for T083-1A shell/worker compile path.
- missing validation:
  - No GUI screenshot/proof of the AI tab and manifest rendering.
  - No manual cancel-path proof from the panel/QProcess.

## Findings

### Blocker
- Finding: Working tree contains out-of-scope changes beyond the approved T083-1A file list.
- Why it matters: The implementation cannot be accepted as a narrow T083-1A change while unrelated task/phase docs and directories are mixed into the review set.
- Evidence: `git status --short` shows modified `plans/PHASES.md`, `tasks/FLUX_MOTION_TEXT_RECOVERY_SOURCE_OF_TRUTH.md`, `tasks/T084-plugin-payload-discovery.md`, `tasks/TASKS.md`, plus untracked `build-logs/` and `t083-research/`.
- Suggested change: Separate or revert unrelated working-tree changes before accepting T083-1A.

### Minor
- Finding: Worker stdout parsing does not buffer partial JSON lines.
- Why it matters: `QProcess::readyReadStandardOutput()` can deliver partial lines; the UI log could show fragmented JSON and the `started()` signal may be missed if `"event":"started"` is split across reads.
- Evidence: `file://Gui/FluxAiWorkerController.cpp:111-120` splits only the current read buffer and does not retain an incomplete trailing line.
- Suggested change: Add a `_stdoutBuffer` member, append reads, process complete newline-delimited records, and keep the incomplete tail.

### Minor
- Finding: Run remains enabled when the manifest is unavailable.
- Why it matters: The model combo is disabled with `Model manifest unavailable`, but the user can still launch a no-op job with an empty `model_id`, which weakens the “manifest unavailable” disabled-state behavior.
- Evidence: Manifest failure disables only `_modelCombo` at `file://Gui/FluxAiPanel.cpp:124-127`; `onRunClicked()` always starts a job at `file://Gui/FluxAiPanel.cpp:155-158`.
- Suggested change: Disable Run when manifest load fails or guard `onRunClicked()` against empty model IDs.

## Simpler Alternative Check
The chosen approach is already small: one panel shell plus one direct `QProcess` controller and stdlib worker. No smaller approach would satisfy panel + worker bridge requirements, but buffering stdout is a safer minimal addition.

## Final Recommendation
Fix the out-of-scope working-tree changes before shipping; the T083-1A code path itself builds and validates with only minor runtime hardening needed.

Note: I did not write `/home/npittas/Flux/t083-research/t083-1a-review.md` because the task also said “Do not edit files,” and reviewer instructions say no edits unless explicitly allowed.