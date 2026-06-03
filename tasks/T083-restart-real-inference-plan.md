# Planner Report

## Status
ready

## Rationale
This plan is sufficient and scoped because Nick requested a documentation-only restart plan, not implementation code. It consolidates the current T083 source-of-truth, task/phase status, manifest evidence, and scaffold code evidence into one explicit restart file centered on real SAM3.1 inference before GUI/product claims.

# Task Packet

## User Goal
Create a fresh T083 restart documentation/plan file centered on real SAM3.1 inference first, incorporating recent research and Nick's corrections, without editing implementation code.

## Mode
general-coding

## Relevant Locations
- file: `tasks/T083-ai-matte-depth.md`
  symbol: T083 current source-of-truth and milestone list
  approximate lines: whole file
  stable anchor: `# T083 — AI Matte/Mask Generation and Video Depth Tools`
  reason: Prior plan contains scaffold/no-op milestones, approved infrastructure, and stale license/model prioritization language that the restart document must supersede.
  confidence: high
- file: `tasks/TASKS.md`
  symbol: T083 row
  approximate lines: P7 Tasks table, T083 entry
  stable anchor: `T083 | AI matte/mask generation and video depth tools`
  reason: Confirms T083 is active/in-progress and points to the old plan.
  confidence: high
- file: `plans/PHASES.md`
  symbol: P7 T083 entry and known follow-ups
  approximate lines: P7 section
  stable anchor: `T083: 🟡 IN_PROGRESS`
  reason: Confirms phase-level status and prior approved direction.
  confidence: high
- file: `tools/ai/model_manifest.json`
  symbol: model entries for SAM3.1, BiRefNet, ViTMatte, VDA, DepthCrafter, MatAnyone2, RVM, XMem2
  approximate lines: whole file
  stable anchor: `"id": "sam31_sam3plus"`
  reason: Provides existing model IDs, current warning/install metadata, and evidence of license language that must be reframed as informational, not prioritizing/blocking.
  confidence: high
- file: `Gui/FluxAiPanel.cpp`
  symbol: `FluxAiPanel::setupUi`, serialization/restore, model manifest loading, no-op worker wiring
  approximate lines: 1-200 and nearby continuation as needed
  stable anchor: `FluxAiPanel::FluxAiPanel`
  reason: Evidence of support infrastructure to keep: visual panel shell, manifest-backed model selector, prompt/result metadata serialization, viewer box capture wiring.
  confidence: high
- file: `tools/ai/flux_ai_worker.py`
  symbol: no-op worker bridge
  approximate lines: whole file
  stable anchor: `"Flux AI no-op worker bridge for T083-1B"`
  reason: Evidence of support infrastructure that must be disabled/removed from the product path: no-op mode, placeholder manifest/output, and claims of completion without real media.
  confidence: high

## Allowed Edit Files
- `tasks/T083-restart-real-inference-plan.md`

## Read-Only Context Files
- `tasks/T083-ai-matte-depth.md`
- `tasks/TASKS.md`
- `plans/PHASES.md`
- `tools/ai/model_manifest.json`
- `Gui/FluxAiPanel.cpp`
- `tools/ai/flux_ai_worker.py`

## Required Change
Write `tasks/T083-restart-real-inference-plan.md` as a documentation/restart plan, not a code change. The document must:

1. State clearly that prior T083 panel/no-op/placeholder work was scaffold only and must not be treated as product progress.
2. Identify support infrastructure that may remain useful:
   - AI panel visual shell.
   - Model manager/security infrastructure.
   - Installer model actions.
   - Viewer capture primitives.
3. Identify product-path items to disable/remove before claiming T083 progress:
   - no-op worker path.
   - placeholder result manifest/output.
   - panel scratchpad/prompt persistence as completion evidence.
   - UI status/log claims of generation without real media files.
4. Correct licensing policy:
   - Licensing must not prioritize, dismiss, or block any selected model.
   - All selected models are equal product targets and intended to ship with the app/runtime where technically possible.
   - License warnings are informational/user-side only.
5. Put the technical dependency order first:
   - Primary first target: SAM3.1/SAM3+ real inference because it supports the core UX: point, box, text/prompt, paint/mask selection/refinement, and video propagation.
   - Document that SAM3 propagates selection through video and users should be able to refine/fix propagation by adding new points or masks on later frames; exact API path must be verified.
   - Then document BiRefNet, RVM, ViTMatte, MatAnyone2, XMem2, VDA/DepthCrafter according to their actual roles and dependencies.
6. Include model-specific tensor/runtime contracts from Nick's research:
   - SAM3.1: RGB/PIL/uint8 0-255 image; HF processor handles boxes/points; image tensor through processor; video MP4 or JPEG frame folder; stateful sessions; outputs masks/boxes/scores/object IDs; prompt labels positive/negative; backend choice Meta repo vs HF must be proven.
   - BiRefNet: RGB PIL -> resize model resolution -> ToTensor 0-1 -> ImageNet normalize -> `[B,3,H,W]` -> `model(x)[-1].sigmoid` -> alpha resized to source.
   - ViTMatte: RGB + trimap 0/128/255 -> 4-channel `[RGB normalized + trimap 0-1]`, pad /32 -> alphas `[B,1,H,W]`, crop.
   - VDA: video frames RGB float 0-1, ImageNet normalize, multiple-of-14, `[B,T,C,H,W]`, 32-frame windows overlap, EXR Z output.
   - DepthCrafter: video path/decord, frames `[T,H,W,C]` 0-1, multiples/64 and /8, fp16 `[-1,1]`, 110-frame windows, normalized relative depth.
   - MatAnyone2: video/frame folder + first-frame mask, RGB /255, grayscale mask, propagation alpha/fgr outputs.
   - RVM: video/image sequence RGB tensors `[B,T,C,H,W]`, recurrent state, alpha/foreground.
   - XMem2: video/JPEG frames + palette PNG mask annotations, outputs propagated masks.
7. Define restart sequence:
   - Documentation and research consolidation first.
   - SAM3.1 real worker proof outside GUI second.
   - GUI/panel/object binding only after real SAM3.1 output exists.
8. Include explicit non-goals:
   - no placeholder success.
   - no dummy verification requests.
   - no panel scratchpad as completion.
   - no implementation code edits in this documentation task.
9. Include concrete validation gates and stop conditions for future work, including real media output, token/security audit, no plaintext tokens, no generated-media absolute-path dependence, SAM3 API/backend proof, and screenshot/recording only after real media output exists.
10. Make clear that this restart file supersedes stale T083 milestone ordering where SAM3 was deferred behind BiRefNet.

## Non-Goals
- Do not edit `Gui/FluxAiPanel.cpp`, `tools/ai/flux_ai_worker.py`, `tools/ai/model_manifest.json`, installer scripts, task index files, phase files, or any implementation code.
- Do not implement SAM3.1, model downloads, GUI binding, nodegraph nodes, or worker providers in this task.
- Do not mark T083 complete.
- Do not claim generated output from placeholder/no-op paths.

## Validation
Commands:
- `test -f /home/npittas/Flux/tasks/T083-restart-real-inference-plan.md`
- `grep -F "SAM3.1" /home/npittas/Flux/tasks/T083-restart-real-inference-plan.md`
- `grep -F "no-op" /home/npittas/Flux/tasks/T083-restart-real-inference-plan.md`
- `grep -F "placeholder" /home/npittas/Flux/tasks/T083-restart-real-inference-plan.md`
- `grep -F "Licensing must not prioritize" /home/npittas/Flux/tasks/T083-restart-real-inference-plan.md`

Expected result:
The file exists and contains a restart plan centered on real SAM3.1 inference first, explicitly demotes prior scaffold/no-op evidence, documents model contracts, and defines gates/stop conditions without implementation edits.

## Stop Conditions
Stop and report if:
- the requested output path cannot be written
- the task requires editing implementation files or task index/phase status files
- required research facts conflict with Nick's provided corrections
- source evidence contradicts the existence of the scaffold/no-op support infrastructure
- the restart document would require licensing/product policy judgment beyond Nick's stated corrections

## Required Return Contract
Return only a task-focused summary. Do not include transcript, tool logs, raw file dumps, large code blocks, or broad unrelated issues. Include status, files inspected/changed, validation evidence, blockers, and task-specific risks.
