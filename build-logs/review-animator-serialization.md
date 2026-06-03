# Review Report

## Verdict
fix-then-ship

## Scope Compliance
- failed
- evidence: working tree has many modified files outside the implementation list, e.g. `file:///home/npittas/Flux/CMakeLists.txt`, `file:///home/npittas/Flux/Engine/AppManager.cpp`, `file:///home/npittas/Flux/Gui/Gui05.cpp`, `file:///home/npittas/Flux/openfx-flux/TextRender/TextRender.cpp`, plus untracked `build-logs/` and task files. The reviewed diff for the four listed files is in scope, but the repository state is not limited to the allowed implementation files.

## Validation Assessment
- command/result reviewed: worker reported `timeout 900 cmake --build build --target Natron -- -j"$(nproc)"` passed and `git diff --check` passed. I did not rerun the build.
- sufficient? no
- missing validation: no save/reload round-trip test proving `.ntp` persists animator count/order/settings/keyframes; no test checking restored interpolation/derivatives; no test for reopening a project where animator user knobs already exist on the node.

## Findings

### Blocker
- Finding: Restored keyframe derivatives/interpolation are only written to a local `KeyFrame` copy, not back to the curve.
- Why it matters: Bezier/hold/linear interpolation and tangents will not reliably survive save/reload even though the serialization captures them.
- Evidence: `file:///home/npittas/Flux/Gui/FluxTextAnimatorModel.cpp:526-532` calls `dk->setValueAtTime(..., &kf)`, then mutates `kf` with `setLeftDerivative`, `setRightDerivative`, and `setInterpolation`. In `file:///home/npittas/Flux/Engine/KnobImpl.h:1518-1520` / `:1571`, `setValueAtTime` builds and adds the keyframe before returning; later mutations to the caller's `kf` object are not re-added to `curve`.
- Suggested change: Build a `KeyFrame` with the serialized time/value/derivatives/interpolation and add/update it through the curve/knob API after setting all fields, or re-fetch the curve and replace the stored keyframe after setting derivatives/interpolation.

### Major
- Finding: `restoreAnimators()` unconditionally creates user knobs for every serialized animator ID.
- Why it matters: On `.ntp` restore, the gizmo node may already contain restored user knobs; creating same-named knobs again risks duplicate knobs, failed creation, or UI/serialization corruption during project load.
- Evidence: `file:///home/npittas/Flux/Gui/FluxTextAnimatorModel.cpp:583-584` calls `ensureAnimatorKnobs(...)` for every serialized animator. `ensureAnimatorKnobs` creates all animator knobs without checking whether they already exist at `file:///home/npittas/Flux/Gui/FluxTextAnimatorModel.cpp:314-356`. The safer compatibility helper does guard individual missing knobs at `file:///home/npittas/Flux/Gui/FluxTextAnimatorModel.cpp:358-384`, but restore does not use that pattern.
- Suggested change: Make `ensureAnimatorKnobs` idempotent for existing animator IDs, or split it into create-missing-only restore logic before setting values.

### Major
- Finding: The reported validation does not prove the requested behavior.
- Why it matters: A successful compile cannot catch the two persistence-specific risks above, and the task is specifically save/load persistence.
- Evidence: The only known validation is build and whitespace checks; the changed path is `serializeForProject()` capture at `file:///home/npittas/Flux/Gui/FluxTimeline.cpp:4423-4425`, load-time restore at `file:///home/npittas/Flux/Gui/FluxTimeline.cpp:4564-4567`, and keyframe restoration at `file:///home/npittas/Flux/Gui/FluxTextAnimatorModel.cpp:523-533`. None of that is exercised by build-only validation.
- Suggested change: Add/manual-run a round-trip `.ntp` save/reopen check with at least one text animator covering selector values, transform/color values, `scaleSeparated`, and a non-linear/bezier keyframe, then inspect the restored curves and renderer JSON.

### Minor
- Finding: The version gate for animator data is correctly on `FluxLayerSerialization` version 3, but `FluxTimelineSerialization` remains version 2.
- Why it matters: This is probably valid because `Layer` is serialized as a versioned nested type, but it should be intentional; otherwise future readers may assume timeline version 2 fully describes the payload.
- Evidence: animator fields are gated by `version >= 3` in `file:///home/npittas/Flux/Gui/FluxTimelineSerialization.h:357-366`, and `BOOST_CLASS_VERSION(NATRON_NAMESPACE::FluxLayerSerialization, 3)` is set at `file:///home/npittas/Flux/Gui/FluxTimelineSerialization.h:417-419`, while `FluxTimelineSerialization` remains version 2 at `:417`.
- Suggested change: Leave as-is if confirmed with a real load of pre-v3 and v3 projects; otherwise document why only the nested layer version changes.

## Simpler Alternative Check
A smaller/safe approach would be to make animator user knobs Natron-native persistent and only serialize `animatorOrder`/renderer JSON if needed. Given the stated Option A requirement, the current explicit Flux-level serializer is acceptable in scope, but keyframe restore must use the real curve API and knob creation must be idempotent.

## Final Recommendation
Fix keyframe reapplication and idempotent restore-time knob creation, then validate with an actual save/reopen animator round trip before shipping.
