# T074 DopeSheet Alias Keyframes — Handoff

Date: 2026-05-24

## Reason for Handoff

Current agent failed the debugging process and should not continue this thread.

Specific failures:

- Treated the native DopeSheet alias-keyframe bug as a diff-validation problem instead of an app-behavior problem.
- Delegated reviews that validated the hypothesis/patch instead of tracing the live runtime path.
- Proposed and implemented a curve-source patch that built successfully but had **zero user-visible effect**.
- Added debug logging without first researching how Natron/Flux logging is routed, where Qt logging goes, or how to reliably capture logs in this application.
- Gave incorrect run commands despite the project source of truth containing the canonical runtime command.
- Failed to prove that instrumentation actually emitted in the live workflow.

Do not rely on conclusions from the failed reviews. Treat them as suspect until independently revalidated against live behavior.

## Nick's Verified Repro

Use this as ground truth:

1. Launch Flux/Natron with the canonical command:

   ```bash
   QT_PLUGIN_PATH=/usr/lib64/qt6/plugins QT_QPA_PLATFORM=xcb /home/npittas/Flux/build/App/Natron
   ```

2. Add footage.
3. Add that footage to the Flux timeline.
4. Add a keyframe to the layer translation at frame 0.
5. Move forward several frames.
6. Add another translation keyframe.
7. Flux timeline shows the keyframes.
8. Switch to native DopeSheet.
9. Native DopeSheet shows **no keyframes** for the gizmo/PyPlug alias knob.

Current confirmed behavior:

- Native DopeSheet no longer crashes.
- Key diamonds are visible for normal/effect/non-gizmo knobs.
- Key diamonds remain invisible for FluxLayer/FluxText promoted alias knobs inside gizmos/PyPlugs.
- The previous patch made no visible difference.

## Current Task State

- Superseded 2026-05-25: Nick validated the final behavior and T074 is now `DONE`.
- This handoff remains as historical debug context for the failed pre-harness path.
- Relevant tracking files:
  - `file:///home/npittas/Flux/tasks/TASKS.md`
  - `file:///home/npittas/Flux/plans/PHASES.md`
  - `file:///home/npittas/Flux/tasks/T074-flux-timeline-keyframe-editor.md`

## Critical Constraints

- Do not mark T074 done until Nick validates manually.
- Do not duplicate animation state. Native Natron `Knob` / `Curve` / Roto data remains the source of truth.
- Do not revert crash guards without explicit approval.
- Do not commit, stage, reset, revert, amend, or push without explicit approval.
- Nick explicitly instructed: never reuse the same agent more than once. Ignore resumable-session reuse for future work unless Nick explicitly changes that instruction.
- If adding any instrumentation, first research Natron's actual logging mechanism and prove the log route works with a tiny probe before instrumenting broader paths.

## Current Worktree State at Handoff

`git status --short` showed:

```text
 M AGENTS.md
 M Engine/Knob.cpp
 M Gui/DopeSheet.cpp
 M Gui/DopeSheet.h
 M Gui/DopeSheetEditorUndoRedo.cpp
 M Gui/DopeSheetHierarchyView.cpp
 M Gui/DopeSheetView.cpp
 M Gui/FluxTimeline.cpp
 M Gui/FluxTimeline.h
 M Gui/FluxTimelineSerialization.h
 M plans/PHASES.md
 M tasks/TASKS.md
?? Gui/FluxKeyframeModel.cpp
?? Gui/FluxKeyframeModel.h
?? Gui/FluxKeyframeModelOps.cpp
?? TxtGroup.py
?? tasks/T074-flux-timeline-keyframe-editor.md
```

`TxtGroup.py` is unrelated/untracked. Do not touch unless Nick says otherwise.

## Code Changes Already Made Before This Handoff

### T074 implementation files

T074 animated property/keyframe rows were implemented across:

- `file:///home/npittas/Flux/Gui/FluxKeyframeModel.h`
- `file:///home/npittas/Flux/Gui/FluxKeyframeModel.cpp`
- `file:///home/npittas/Flux/Gui/FluxKeyframeModelOps.cpp`
- `file:///home/npittas/Flux/Gui/FluxTimeline.h`
- `file:///home/npittas/Flux/Gui/FluxTimeline.cpp`
- `file:///home/npittas/Flux/Gui/FluxTimelineSerialization.h`

Features included:

- Animated property rows in FluxTimeline.
- Read-only key diamonds.
- Property-row context menu and selection.
- Add/delete/move keys backed by native knobs/curves.
- Keyframe/inline-curve toggle.
- Simple inline `Curve::getValueAt()` previews.
- Multidim grouping/ungrouping with persisted UI state.
- Native knob/Bezier/Roto invalidation wiring.
- FluxText filtering: expose promoted `Text1...` knobs and public `opacity`; hide duplicate internals and approved adjustment `disableNode` rows.

### Native DopeSheet crash hardening already done

Files touched:

- `file:///home/npittas/Flux/Gui/DopeSheet.h`
- `file:///home/npittas/Flux/Gui/DopeSheet.cpp`
- `file:///home/npittas/Flux/Gui/DopeSheetView.cpp`
- `file:///home/npittas/Flux/Gui/DopeSheetHierarchyView.cpp`
- `file:///home/npittas/Flux/Gui/DopeSheetEditorUndoRedo.cpp`

Intent:

- Skip/prune stale DopeSheet entries instead of crashing/asserting on null/stale weak refs.

Confirmed by Nick:

- DopeSheet crash is gone.

### Native DopeSheet texture/key visibility fix already done

File touched:

- `file:///home/npittas/Flux/Gui/DopeSheetView.cpp`

Intent:

- Fix invisible key diamonds for normal/effect/non-gizmo keys by correcting texture upload/draw state.

Key pieces included:

- Convert keyframe images to `QImage::Format_RGBA8888`.
- Upload with `GL_RGBA` + `GL_UNSIGNED_BYTE`.
- Set `glPixelStorei(GL_UNPACK_ALIGNMENT, 1)`.
- Add fallback texture/image handling.
- Force `glColor4f(1,1,1,1)` before textured draw.
- Add non-textured diamond fallback.

Confirmed by Nick:

- Normal/effect/non-gizmo key diamonds are visible.

### Failed alias-curve patch

Files touched:

- `file:///home/npittas/Flux/Gui/DopeSheetView.cpp`
- `file:///home/npittas/Flux/Gui/DopeSheetHierarchyView.cpp`
- `file:///home/npittas/Flux/Gui/DopeSheet.cpp`

Hypothesis attempted:

- DopeSheet was reading stale/empty `KnobGui::getCurve()` clones for alias knobs instead of the authoritative native `Knob::getCurve()`.

Implemented changes:

- Prefer `dsKnob->getInternalKnob()->getCurve(ViewIdx(0), dim)` before `knobGui->getCurve()` in paint, visibility, hit-test, box-select, selection, move, and frame-all paths.

Build result:

- Build passed.

Nick validation:

- No visible change. Alias keyframes still invisible in native DopeSheet.

Conclusion:

- The hypothesis is incomplete or wrong, or the patched paths are not the active fail path.

### Failed debug instrumentation

Temporary instrumentation was added in:

- `file:///home/npittas/Flux/Engine/Knob.cpp`
- `file:///home/npittas/Flux/Gui/DopeSheet.cpp`
- `file:///home/npittas/Flux/Gui/DopeSheetHierarchyView.cpp`
- `file:///home/npittas/Flux/Gui/DopeSheetView.cpp`

Probe tag:

```text
[DBG-DSALIAS]
```

Probe env var:

```text
FLUX_DS_ALIAS_DEBUG=1
```

Build result:

- Build passed.

Runtime log checked:

- `file:///tmp/flux-dsalias.log`

Result:

- No `[DBG-DSALIAS]` lines appeared.

Important: the absence of `[DBG-DSALIAS]` is **not reliable evidence** about the app path, because the logging strategy itself was not researched/proven. The next agent should remove or replace these probes after understanding Natron logging.

## Exact Runtime Command

Project source of truth: `file:///home/npittas/Flux/ARCHITECTURE.md:451`

```bash
QT_PLUGIN_PATH=/usr/lib64/qt6/plugins QT_QPA_PLATFORM=xcb /home/npittas/Flux/build/App/Natron
```

Do not add extra plugin env vars unless Nick explicitly asks or the current project docs are updated.

## What Went Wrong in the Debugging Process

The failed path assumed too much:

1. Assumed alias key invisibility was caused by stale GUI curve clones.
2. Patched code to prefer engine curves without first proving that DopeSheet paint/visibility was reaching those rows.
3. Asked reviewers to validate the patch rather than validate the user-visible workflow.
4. Added logging without researching Natron's logging channels or how Qt logs are captured in this build/run environment.
5. Did not first add a one-line proven probe to confirm logs were visible.

The next investigation must start from the app behavior and runtime path, not the previous diff.

## Recommended Next Investigation

Do not start by patching DopeSheet drawing again.

Start with a read-only/runtime-path investigation plus a proven logging strategy:

1. Research original Natron logging conventions and this fork's logging code:
   - Search for existing logging macros/functions/classes.
   - Check how GUI/Qt messages are routed.
   - Check whether `qDebug()` is compiled, filtered, redirected, or suppressed.
   - Check existing Natron logs in the original upstream repo if needed.

2. Prove logging with one minimal probe in an already-known-executed path:
   - Example path: a Flux log path that already prints `FLUX: Layout created successfully` or `FLUX REBUILD`.
   - Confirm the probe appears in the same terminal/log file with the canonical run command.
   - Remove or keep only if proven.

3. Only then instrument the keyframe workflow.

The runtime questions to answer:

### A. Where are the visible Flux timeline keyframes coming from?

Trace `FluxTimeline` / `FluxKeyframeModel` for the footage layer translation row:

- Which node is represented?
- Which knob is represented?
- Is it the promoted alias knob on the group node, or the internal Transform node's master knob?
- What exact curve has the two keys?

Likely files:

- `file:///home/npittas/Flux/Gui/FluxKeyframeModel.cpp`
- `file:///home/npittas/Flux/Gui/FluxKeyframeModelOps.cpp`
- `file:///home/npittas/Flux/Gui/FluxTimeline.cpp`
- `file:///home/npittas/Flux/Gui/HostOverlay.cpp`

### B. What nodes/knobs does native DopeSheet model actually contain?

Trace `DopeSheet::addNode`, `DSNode::DSNode`, and `DSKnob` construction:

- Does the DopeSheet contain the FluxLayer group node?
- Does it contain the internal Transform node?
- Does it skip group internals?
- Does it hide nodes whose settings panels are not visible?
- Does it create DSKnob rows for the promoted alias knob at all?

Likely files:

- `file:///home/npittas/Flux/Gui/DopeSheet.cpp`
- `file:///home/npittas/Flux/Gui/DopeSheetHierarchyView.cpp`
- `file:///home/npittas/Flux/Gui/DopeSheetEditor.cpp`

### C. Is native DopeSheet only painting rows that are visible in the hierarchy?

The node/row visibility logic may be the real blocker:

- `HierarchyViewPrivate::checkNodeVisibleState()` hides common nodes if `nodeHasAnimation(nodeGui)` is false or if settings panel is not visible.
- `nodeHasAnimation()` checks `nodeGui->getNode()->getKnobs()` and `knob->hasAnimation()`.
- For FluxLayer/FluxText, keys may be on internal nodes or aliased master knobs not counted by this visibility gate.

Files:

- `file:///home/npittas/Flux/Gui/DopeSheet.cpp`
- `file:///home/npittas/Flux/Gui/DopeSheetHierarchyView.cpp`

### D. Does DopeSheet refresh happen after key creation?

Trace signal path:

- `KnobGui::onInternalKeySet()` emits `keyFrameSet()`.
- `DSNode::DSNode()` connects each model knobGui to `DopeSheet::onKeyframeSetOrRemoved()`.
- `DopeSheet::onKeyframeSetOrRemoved()` maps sender `KnobGui` to a `DSKnob` with `findDSKnob(k)` and emits `keyframeSetOrRemoved(dsKnob)` if found.
- If the keyframe signal comes from the internal Transform node's `KnobGui`, but the DopeSheet row is the group alias knob, `findDSKnob(k)` may fail and hierarchy never refreshes.

Files:

- `file:///home/npittas/Flux/Gui/KnobGui.cpp`
- `file:///home/npittas/Flux/Gui/KnobGui20.cpp`
- `file:///home/npittas/Flux/Gui/DopeSheet.cpp`
- `file:///home/npittas/Flux/Engine/Knob.cpp`

## High-Probability Hypotheses for Next Agent

These are hypotheses, not conclusions:

1. **Signal sender mismatch**
   - Keys are authored on internal Transform/master knob.
   - DopeSheet rows are for group alias knobs.
   - Keyframe signal sender is the internal/master `KnobGui`, so `DopeSheet::findDSKnob(senderKnobGui)` fails.
   - Result: hierarchy rows do not unhide/refresh, even though FluxTimeline sees native curves.

2. **Node visibility gate hides FluxLayer group nodes**
   - `checkNodeVisibleState()` requires `nodeHasAnimation(nodeGui)` and settings-panel visibility for common nodes.
   - Alias/master animation may not make `nodeHasAnimation()` true for the group node as expected, or settings-panel visibility may be false when switching to DopeSheet.
   - Result: paint code may never run for alias rows.

3. **DopeSheet model contains the wrong node set**
   - Native DopeSheet may model internal nodes or group nodes differently from FluxTimeline.
   - Internal group nodes may be excluded, hidden, or not settings-panel-visible.
   - Result: visible FluxTimeline keys have no corresponding native DopeSheet row.

4. **FluxTimeline and DopeSheet are intentionally looking at different abstractions**
   - FluxTimeline deliberately maps layer rows to promoted knobs / internals via Flux-specific model code.
   - Native DopeSheet uses generic Natron node/knob discovery and may not understand Flux's layer abstraction.
   - Result: native DopeSheet cannot show Flux alias keys without a bridge in model population/refresh.

5. **Build/run mismatch is still possible**
   - The app was run and showed Flux logs, but the failed probes did not emit.
   - Before drawing conclusions, next agent must prove any new instrumentation is present and routed.

## Do Not Do Next

- Do not ask Nick for another vague repro; the repro is already clear.
- Do not rerun the same oracle/fixer sessions.
- Do not claim a fix based only on build success.
- Do not add more `qDebug()` blindly.
- Do not globally change `KnobGui::getCurve()`.
- Do not duplicate animation data into Flux-specific storage.

## Required Cleanup Before Final Fix

Temporary `[DBG-DSALIAS]` instrumentation must be removed or replaced with a researched/proven logging mechanism.

Search before cleanup:

```bash
rg "DBG-DSALIAS|FLUX_DS_ALIAS_DEBUG" Engine Gui
```

Currently expected matches are in:

- `file:///home/npittas/Flux/Engine/Knob.cpp`
- `file:///home/npittas/Flux/Gui/DopeSheet.cpp`
- `file:///home/npittas/Flux/Gui/DopeSheetHierarchyView.cpp`
- `file:///home/npittas/Flux/Gui/DopeSheetView.cpp`

## Build Command Used

```bash
cmake --build /home/npittas/Flux/build --target Natron -- -j$(nproc)
```

Build passed after the failed alias patch and after the failed instrumentation.

## Final Note for Next Agent

Start over from live behavior. The key fact is:

> FluxTimeline shows the two translation keyframes for a FluxLayer footage row; native DopeSheet shows no keyframes for that same alias/gizmo workflow.

Find why those two views diverge. Do not validate the previous patch. Do not trust the failed logging. Research logging first, then instrument the actual runtime path.
