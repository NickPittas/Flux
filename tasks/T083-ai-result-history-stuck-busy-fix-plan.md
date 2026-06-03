# Planner Report

## Status
ready

## Rationale
This plan targets the single remaining Packet E review blocker in the persistent SAM3 worker exit path. Locator evidence is high-confidence in `Gui/FluxAiPanel.cpp`: `onSam3Finished()` clears worker maps/flags on `_sam3WorkerProcess` exit, while `updateUiState()` treats a non-empty `_sam3RunPendingRequestId` as busy. The allowed edit surface is one file and the required change is limited to clearing stale pending state so the UI unblocks after worker crash/exit during inference.

# Task Packet

## User Goal
Fix only the stuck-busy SAM3 persistent worker exit path found in Packet E review: when the persistent SAM3 worker exits during inference, Run/Apply/Preview Again must not remain disabled forever because `_sam3RunPendingRequestId` stayed set.

## Mode
general-coding

## Relevant Locations
- file: `Gui/FluxAiPanel.cpp`
  symbol: `FluxAiPanel::updateUiState()`
  approximate lines: 725-756
  stable anchor: `bool sam3PersistentInferencePending = !_sam3RunPendingRequestId.isEmpty();`
  reason: Non-empty `_sam3RunPendingRequestId` contributes to the `running` state that disables Run/Apply/Preview Again.
  confidence: high
- file: `Gui/FluxAiPanel.cpp`
  symbol: SAM3 worker response handling in `FluxAiPanel::onSam3ReadyReadStandardOutput()`
  approximate lines: 1530-1600
  stable anchor: `} else if (!id.isEmpty() && id == _sam3RunPendingRequestId) {`
  reason: Normal persistent `infer_still` response clears `_sam3RunPendingRequestId`; worker exit currently bypasses this normal cleanup.
  confidence: high
- file: `Gui/FluxAiPanel.cpp`
  symbol: `FluxAiPanel::onSam3Finished(int exitCode, QProcess::ExitStatus exitStatus)`
  approximate lines: 1801-1812
  stable anchor: `if (process == _sam3WorkerProcess) {`
  reason: Persistent worker exit branch clears pending command maps and worker flags but does not clear `_sam3RunPendingRequestId`, causing the stuck-busy state.
  confidence: high
- file: `Gui/FluxAiPanel.cpp`
  symbol: `FluxAiPanel::cancelSam3()`
  approximate lines: 1853-1868
  stable anchor: `if (_sam3WorkerProcess && _sam3WorkerProcess->state() != QProcess::NotRunning && !_sam3WorkerPendingCommands.isEmpty())`
  reason: Context for pending command cancellation; do not broaden this task into cancellation behavior unless required for the worker-exit cleanup.
  confidence: medium

## Allowed Edit Files
- `Gui/FluxAiPanel.cpp`

## Read-Only Context Files
- `/home/npittas/.pi/agent/COLLABORATION.md`
- `/home/npittas/Flux/AGENTS.md`
- `ARCHITECTURE.md`
- `plans/PHASES.md`
- `tasks/TASKS.md`

## Required Change
In `FluxAiPanel::onSam3Finished()`, inside the `_sam3WorkerProcess` branch only:
1. Detect whether `_sam3RunPendingRequestId` was non-empty before clearing it.
2. Clear `_sam3RunPendingRequestId` when the persistent worker exits, so `updateUiState()` no longer sees a stale persistent inference pending state.
3. If a run was pending, log a concise failure/abort message and update the visible status label/status knob to an error/failure state that explains the SAM3 worker exited before the inference completed.
4. Preserve the existing unloaded final worker state: `_sam3WorkerPendingCommands.clear()`, `_sam3WorkerLoading = false`, `_sam3WorkerUnloading = false`, `_sam3WorkerLoaded = false`, and `setAIPaintSam3StatusKnob(..., "unloaded")` behavior must remain semantically intact. If an error message is shown for the pending run, do not leave the UI busy; if preserving the final knob as `unloaded` conflicts with the error text, prefer the existing unloaded final worker status while logging the pending-run failure.
5. Do not modify Packet F, Gui05, result history, live preview UX, git state, task status files, build files, or unrelated dirty files.

Review note: scope contamination in `git status` is known from broader ongoing T083 work. Review should inspect only the delta in `Gui/FluxAiPanel.cpp` for this stuck-busy code path and ignore unrelated pre-existing dirty files.

## Non-Goals
- No Packet F work.
- No `Gui/Gui05.cpp` changes.
- No broader SAM3 protocol redesign.
- No result history, manifest, preview-again, or live-preview feature changes beyond clearing stale state if strictly necessary to unblock the worker-exit path.
- No task/status documentation updates.
- No git commits, staging, resets, or cleanup of unrelated dirty files.

## Validation
Commands:
- `cmake --build build --target Natron -j$(nproc)`

Expected result:
Build completes successfully. If this workspace uses a different existing build directory/target, use the established Flux build command for the Natron GUI target and report the exact command. No automated GUI crash simulation is required for this minimal fix, but the reviewer must verify by code inspection that `_sam3RunPendingRequestId` is cleared on `_sam3WorkerProcess` exit before `updateUiState()` runs.

## Stop Conditions
Stop and report if:
- target symbol is missing
- required fix exceeds allowed files
- validation cannot run
- existing architecture contradicts the requested change
- task requires product/design judgment not in packet
- clearing `_sam3RunPendingRequestId` in `onSam3Finished()` would conflict with another verified owner of that field
- implementing the fix requires modifying live preview semantics beyond minimal pending-state cleanup

## Planner Self-Check
- locator evidence sufficient: yes — exact symbols and anchors in `Gui/FluxAiPanel.cpp` identify the stale pending-id path.
- allowed edit files minimal and explicit: yes — only `Gui/FluxAiPanel.cpp` is editable.
- read-only context minimal: yes — mandatory project docs plus the located source windows; no unrelated source files required.
- anchors/lines included: yes — relevant locations include path, symbol, approximate lines, anchors, reasons, and confidence.
- validation concrete: yes — build command specified, with fallback to established existing Flux GUI build command if the build directory differs.
- parallelization decision explicit and safe: yes — single task; no parallelization because the sole allowed edit file is shared state logic in `Gui/FluxAiPanel.cpp`.
- non-goals and stop conditions sufficient: yes — Packet F, Gui05, unrelated dirty files, git actions, and broader SAM3/result-history changes are excluded.
- reviewer findings addressed, if revision: yes — the Packet E reviewer blocker is directly addressed; no other reviewer findings are in scope.

## Required Return Contract
Return only a task-focused summary. Do not include transcript, tool logs, raw file dumps, large code blocks, or broad unrelated issues. Include status, files inspected/changed, validation evidence, blockers, and task-specific risks. Explicitly note that review should inspect only the `Gui/FluxAiPanel.cpp` delta for this stuck-busy fix and not unrelated pre-existing dirty files.
