# T083 Orchestrator Recovery Source of Truth

This file exists because the T083 SAM3/AI Paint workflow drifted and wasted time/tokens. It must be read after compaction before doing any further T083 work.

## Nick's Explicit Critique / Required Operating Change

The orchestrator must not act as a relay between locator, planner, reviewer, and worker agents.

The orchestrator's job is to:

1. Own Nick's direct source-of-truth.
2. Read locator results and make sense of them.
3. Think through the actual application workflow before delegating.
4. Design precise task packets with protected workflows, non-goals, stop conditions, and runtime evidence.
5. Challenge and reject subagent output that only reviews code/builds instead of intent/workflow.
6. Prevent local fixes that break adjacent workflows.
7. Verify application behavior before reporting success.

If a locator returns evidence, do not just forward it to a planner. First decide:

- What does this mean for Nick's intended workflow?
- What subsystems are protected?
- What adjacent workflows might regress?
- What should be done now, what should be deferred, and what must not be touched?
- What concrete evidence is required to call it working?

## Mandatory Review Standard

Review prompts must require reviewing the actual intended application workflow, not only changed code.

Every review must report each relevant adjacent workflow as one of:

- verified working,
- unverified,
- broken/blocker.

A review is not allowed to return `ship` for a user-facing workflow from source inspection/build alone.

For T083/SAM3/AI Paint, reviewers must explicitly check these workflows unless the task is completely unrelated:

- AI Paint node creation and selection.
- AI Paint prompt capture/edit/delete/clear.
- AI Paint Load SAM3 / Unload SAM3.
- AI Paint Live Preview current-frame overlay.
- AI Panel Run.
- AI Panel result history and Preview Again.
- AI Panel Add Mask / Replace Mask.
- Source-frame export and timeline/source-frame mapping.
- Old project load/save/reopen.
- Native Roto/RotoPaint layer masks.
- Timeline effect enable/disable.
- Main comp viewer isolation.

If runtime GUI validation cannot be run, the verdict must be `blocked/unvalidated`, not `ship`.

## Mandatory Agent Prompt Contract

Before sending any subagent prompt, include:

1. Nick's exact intent and expected user-visible behavior.
2. Source-of-truth hierarchy: Nick's direct instruction > approved plan > locator/reviewer/task packet.
3. Protected workflows and protected files/subsystems.
4. Adjacent regression checklist.
5. Allowed edit files and read-only files.
6. Non-goals.
7. Stop conditions.
8. Required evidence, including runtime behavior where applicable.
9. Required return contract: concise status, files changed, validation evidence, blockers, risks.

Do not send generic prompts like “review this plan” or “implement this task” without the above.

## Current T083 Technical State / Known Breakages

Nick reported, from actual runtime logs:

```text
qt.core.qobject.connect: QObject::connect: No such signal QProcess::error(QProcess::ProcessError)
FLUX-SAM3-A1 capture blocked: source-frame export unavailable: stored source frame 0 is outside original range [1,81]
```

Nick also reported:

- In AI Paint node, Load SAM3 fails with “SAM3 not available in this build path.”
- AI Panel Run button does nothing.
- Previous project load crashed before emergency serialization changes.
- Old Apply path was disabled/unwired and Nick could not test.

These are real application failures and must be fixed before more Add/Replace architecture work.

## Recent Changes Already Made

### Wrong path unwired

The AI Apply-to-layer-mask/Roto/Premult path was wrong and has been unwired. Layer masks are for Roto/RotoPaint, not for AI channel copy.

Do not reintroduce AI masks through FluxMask/Roto/Premult.

### New intended AI mask model

AI result application should use visible timeline effect rows, not layer mask rows.

- AI Panel should have Add Mask / Replace Mask, not the old Apply workflow.
- Add Mask creates a visible AI mask copy effect row with next namespace:
  - `ai_mask1.r/g/b/a`
  - `ai_mask2.r/g/b/a`
  - etc.
- Replace Mask only updates the selected AI mask copy row in the timeline.
- If no AI mask copy row is selected, Replace Mask must fail clearly.
- No Premult is added. If the user wants Premult, they add it manually.

### Native node added

A native `FluxAIMaskCopy` built-in effect was added to copy AI Read RGBA into custom `ai_maskN.RGBA` while preserving layer Color/RGBA and prior mask planes.

Files involved:

- `Engine/FluxAIMaskCopy.h`
- `Engine/FluxAIMaskCopy.cpp`
- `Engine/EffectInstance.h`
- `Engine/AppManager.cpp`
- `Gui/FluxAiPanel.h`
- `Gui/FluxAiPanel.cpp`
- `Gui/FluxTimeline.h`
- `Gui/FluxTimeline.cpp`
- `Gui/FluxTimelineSerialization.h`
- `Gui/Gui05.cpp`

Build passed after these changes, but runtime GUI validation is still required.

### Serialization emergency changes

To avoid old-project crashes:

- AI mask effect metadata was moved out of unversioned `FluxEffectSerialization` into `FluxLayerSerialization` version 4 metadata.
- Legacy removed FluxMask fields are consumed/ignored:
  - `ExternalMaskPathProjectRelative`
  - `ExternalReadNode`
  - `ExternalShuffleNode`
  - `PremultiplyAlpha`

Build passed after this, but old project load must be runtime tested.

## Immediate Next Correct Work Order

Do not continue expanding Add/Replace until the broken SAM3 runtime path is repaired.

Correct next sequence:

1. Fix QProcess Qt6 signal hookup:
   - Replace/handle old `QProcess::error(QProcess::ProcessError)` connections with Qt6-compatible `errorOccurred(QProcess::ProcessError)` where needed.
   - Confirm worker errors are logged.

2. Fix SAM3 provider path resolution from the built app:
   - AI Paint Load SAM3 must find the same provider Python/env that shell proof found.
   - Do not assume shell PATH equals app runtime PATH.

3. Fix source-frame context after load/AI Paint selection:
   - Never use stored source frame `0` when footage range is `[1,81]`.
   - On project load, layer selection, AI Paint selection, or Run, refresh source context from selected layer/current timeline frame.
   - Validate timeline/source frame mapping.

4. Runtime-test actual app:
   - Launch `file:///home/npittas/Flux/build/App/Natron`.
   - Load Nick's project/media.
   - AI Paint Load SAM3.
   - AI Panel Run.
   - Confirm no source-frame 0 error.
   - Confirm result manifest/history.

5. Only after SAM3 Run works again:
   - Test Add Mask / Replace Mask.
   - Confirm visible AI Mask effect rows.
   - Confirm node input A/B connected.
   - Confirm no Roto/layer-mask/Premult nodes are created.
   - Confirm save/reopen.

## Apology / Accountability Note

The failure was not lack of subagents. The failure was orchestrator behavior: delegating without enough interpretation, overly generic prompts, code-path reviews accepted as product validation, and failure to protect verified workflows.

Do not repeat this pattern.
