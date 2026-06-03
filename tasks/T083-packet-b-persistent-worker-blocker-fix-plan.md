# Planner Report

## Status
ready

## Rationale
This is a narrow Packet B blocker-fix plan: it keeps the existing one-shot AI Paint Run path intact, treats the persistent SAM3 worker scaffolding as inert readiness for later packets, and closes the missing real `infer_still` validation with a nonzero artifact smoke. It does not add Packet C/D controls, live preview scheduling, or AI Paint UI behavior.

# Task Packet

## User Goal
Bring T083 Packet B persistent SAM3 worker work to ready-for-next-packet by validating real worker `infer_still`, ensuring the FluxAiPanel persistent worker scaffolding is inert until later packets wire it, and preserving current validated one-shot AI Paint Run behavior.

## Mode
general-coding

## Relevant Locations
- file: `Gui/FluxAiPanel.h`
  symbol: `_sam3Process`, `_sam3WorkerProcess`, worker slots/helpers
  approximate lines: 43-121
  stable anchor: `QProcess* _sam3WorkerProcess;`
  reason: declares both the existing one-shot process and the new persistent worker scaffolding; Packet B readiness must not replace or remove one-shot Run wiring.
  confidence: high
- file: `Gui/FluxAiPanel.cpp`
  symbol: constructor/destructor process setup
  approximate lines: 50-101
  stable anchor: `_sam3Process(new QProcess(this))` and `_sam3WorkerProcess(new QProcess(this))`
  reason: persistent worker lifecycle exists; verify it only starts when explicitly requested and shuts down safely.
  confidence: high
- file: `Gui/FluxAiPanel.cpp`
  symbol: `updateUiState`, `onRunClicked`, `startSam3StillMask`
  approximate lines: 394-407 and 801-997
  stable anchor: `_runButton->setEnabled` and `_sam3Process->start(providerPython, args)`
  reason: current AI Paint Run behavior must remain the one-shot validated path for Packet B unless later packets explicitly wire persistent inference.
  confidence: high
- file: `Gui/FluxAiPanel.cpp`
  symbol: `ensureSam3WorkerStarted`, `sendSam3WorkerRequest`, `processSam3WorkerLine`
  approximate lines: 1000-1077
  stable anchor: `SAM3 persistent worker started.`
  reason: Packet B readiness contract is scaffolding only: start, JSON-lines parse, request ids, stdout protection logging, and no active call site from Run/live preview.
  confidence: high
- file: `tools/ai/sam3_transformers_worker.py`
  symbol: `Worker.infer_still`, `main`
  approximate lines: 129-168 and 182-218
  stable anchor: `elif command == "infer_still"`
  reason: worker must accept real point/box prompt JSON-lines, emit JSON-only stdout, and write nonzero mask/result artifacts.
  confidence: high
- file: `tools/ai/sam3_transformers_real_inference_probe.py`
  symbol: `build_parser`, standalone CLI prompt options
  approximate lines: 381-392
  stable anchor: `--prompt-kind` / `--point` / `--box`
  reason: standalone one-shot probe remains the current Run backend and provides validation parity for prompt coordinates.
  confidence: high
- file: `tools/ai/flux_provider_runtime.py`
  symbol: provider runtime lookup/status
  approximate lines: per existing Packet B plan, `cmd_python`, `cmd_status`, `cmd_self`
  stable anchor: `python sam3` / `status sam3 --json`
  reason: validation must use the same SAM3 provider Python resolution as FluxAiPanel.
  confidence: high

## Allowed Edit Files
- `Gui/FluxAiPanel.h`
- `Gui/FluxAiPanel.cpp`
- `tools/ai/sam3_transformers_worker.py`
- `tools/ai/sam3_transformers_real_inference_probe.py`

## Read-Only Context Files
- `tools/ai/flux_provider_runtime.py`
- `tasks/T083-ai-paint-live-sam3-workflow-plan.md`

## Required Change
1. First inspect whether changes are actually needed. If the current code already satisfies the inert Packet B contract, do not edit C++ just to churn.
2. Preserve current one-shot AI Paint Run behavior:
   - `onRunClicked()` must continue selecting the current AI Paint point/box prompt, exporting/using the source frame metadata, and calling `startSam3StillMask(...)`.
   - `startSam3StillMask(...)` must continue launching `tools/ai/sam3_transformers_real_inference_probe.py` through provider-runtime Python using `_sam3Process`.
   - Do not route Run through `_sam3WorkerProcess` in this fix plan.
   - Do not remove manual Run, prompt validation, manifest writing, Apply availability, or cancel behavior for the one-shot process.
3. Verify/minimally adjust persistent worker scaffolding so it is inert and ready for Packet C/D/E:
   - `_sam3WorkerProcess` may be constructed and connected, but it must not be started by panel construction, model changes, `updateUiState()`, `onRunClicked()`, or `startSam3StillMask()`.
   - `ensureSam3WorkerStarted()` must remain an explicit helper only, with no current Run/live-preview call site.
   - `sendSam3WorkerRequest()` must retain request-id JSON-lines behavior and not write when the worker is stopped.
   - `processSam3WorkerLine()` must tolerate ready events and non-JSON stdout by logging rather than corrupting the one-shot result path.
   - Destructor shutdown is acceptable only for a worker that had actually been started.
4. Run a real persistent worker `infer_still` smoke using an available exported/test frame. Prefer an existing exported source frame from prior T083 AI Paint validation if present; otherwise use the smallest real image asset available in the workspace. Use a point prompt if the image dimensions allow a central positive point; also run a box prompt if a sane in-bounds rectangle is possible. The smoke must verify:
   - provider runtime status resolves (`status sam3 --json`), or clearly reports provider/model blocker;
   - worker starts and emits the `ready` event as JSON on stdout;
   - `infer_still` writes `sam3_transformers_real_inference_result.json` and at least one mask PNG;
   - result JSON reports a succeeded proof with `nonzero_pixels > 0` for at least one point or box prompt;
   - stdout remains parseable JSON-lines, with library chatter redirected to stderr.
5. If worker smoke fails because local SAM3 runtime/model/assets are unavailable, do not fake success. Record the exact runtime blocker and leave source behavior unchanged unless a code bug within the allowed files is identified.

## Non-Goals
- No Packet C/D/E controls.
- No live preview scheduling or preview overlay.
- No Load/Unload buttons or AI Paint properties UI.
- No main comp viewer rewiring.
- No change from one-shot Run to persistent Run.
- No broad prompt-management redesign.
- No generated asset/checkpoint commits.

## Validation
Commands:
- `python3 -m py_compile tools/ai/sam3_transformers_worker.py tools/ai/sam3_transformers_real_inference_probe.py`
- `python3 tools/ai/flux_provider_runtime.py status sam3 --json`
- `PROVIDER_PY=$(python3 tools/ai/flux_provider_runtime.py python sam3)` then launch `"$PROVIDER_PY" tools/ai/sam3_transformers_worker.py` and send JSON-lines: `status`, one `infer_still` with a real image and central point prompt, optional second `infer_still` with an in-bounds box prompt, `unload`, `shutdown`.
- Inspect the worker output directory for `sam3_transformers_real_inference_result.json`, mask PNG(s), and `nonzero_pixels > 0` in result proofs.
- `cmake --build /home/npittas/Flux/build --target Natron -j$(nproc)` if C++ files are edited. If no C++ files are edited, state why build was not required and still run the Python validation above.

Expected result:
Python files compile; provider runtime is available or reports a concrete blocker; the persistent worker smoke produces parseable JSON-lines and nonzero real mask artifacts for point/box where possible; current one-shot AI Paint Run remains wired to `_sam3Process` and `sam3_transformers_real_inference_probe.py`; persistent worker scaffolding has no active Run/live-preview call site before later packets.

## Stop Conditions
Stop and report if:
- target symbol is missing
- required fix exceeds allowed files
- validation cannot run
- existing architecture contradicts the requested change
- task requires product/design judgment not in packet
- preserving one-shot Run conflicts with making persistent worker active
- real SAM3 runtime/model/checkpoint is unavailable and no local validation asset/runtime can be used
- `infer_still` cannot produce nonzero artifacts without changing prompt semantics beyond Packet B
- fixing reviewer concerns requires Packet C/D controls or live preview wiring

## Planner Self-Check
- locator evidence sufficient: yes; user supplied exact files and anchors were verified in authorized context.
- allowed edit files minimal and explicit: yes; only FluxAiPanel C++/header and the two SAM3 Python scripts are editable.
- read-only context minimal: yes; provider runtime and original T083 plan only.
- anchors/lines included: yes; relevant paths include symbols, approximate lines, stable anchors, reasons, confidence.
- validation concrete: yes; includes py_compile, provider status, real JSON-lines infer_still smoke, artifact/nonzero checks, and conditional C++ build.
- parallelization decision explicit and safe: yes; single task because validation and any minimal edits share the same FluxAiPanel/worker behavior and artifact state.
- non-goals and stop conditions sufficient: yes; explicitly blocks C/D/E controls, live preview, and replacing one-shot Run.
- reviewer findings addressed, if revision: yes; clarifies Packet B readiness contract, preserves one-shot Run, and requires real infer_still nonzero smoke while acknowledging stdout protection already acceptable.

## Required Return Contract
Return only a task-focused summary. Do not include transcript, tool logs, raw file dumps, large code blocks, or broad unrelated issues. Include status, files inspected/changed, whether C++ behavior was unchanged or minimally adjusted, worker smoke command/result evidence, artifact paths/nonzero proof, blockers, and task-specific risks.
