# Review Report

## Verdict
ship

## Scope Compliance
- passed
- evidence: Re-reviewed `Gui/FluxTextAnimatorModel.cpp` restore/capture path and supporting Natron keyframe implementation. The requested fixes are confined to animator serialization/restore logic; no edit performed by reviewer.

## Validation Assessment
- command/result reviewed: User-reported `timeout 900 cmake --build build --target Natron -- -j"$(nproc)"` passed; `git diff --check` passed.
- sufficient? no
- missing validation: No save/reopen behavioral test proving an animator with Free/Broken Bezier keys restores the same interpolation/derivatives, and no reopen test proving existing user knobs are reused without duplicate `fta_{id}_*` knobs.

## Findings

### Minor
- Finding: Validation only proves compile/format, not the serialization behavior under review.
- Why it matters: The fixed failure modes are runtime project restore behaviors; a successful Natron build would pass even if restored curves or duplicate user knobs were still wrong.
- Evidence: The relevant runtime path is `FluxTimeline::captureProjectSerialization()` capturing animators at `Gui/FluxTimeline.cpp:4423-4426`, then `FluxTimeline::restoreFromProjectSerialization()` resolving the gizmo and calling `FluxTextAnimatorModel::restoreAnimators()` at `Gui/FluxTimeline.cpp:4564-4567`. The reported validation does not exercise that path.
- Suggested change: Add/manual-run a save-reopen check with one text animator containing non-default interpolation/tangents and confirm the reopened node has one knob set per `fta_{id}_*` suffix and matching keyframe interpolation/tangent data.

## Simpler Alternative Check
No smaller code fix is apparent for the two reported defects. `restoreProperty()` now uses Natron's existing `Knob<T>::setKeyFrame(const KeyFrame&, ...)` path instead of manually setting value then mutating a local copy, and `restoreAnimators()` uses a single existing-knob sentinel before creating dynamic animator knobs.

## Trace Notes
- Full keyframe restore: `Gui/FluxTextAnimatorModel.cpp:526-532` constructs a `KeyFrame(time, value, leftDerivative, rightDerivative, interpolation)` and passes it to `dk->setKeyFrame(...)`. Natron's implementation adds that exact key object to the curve via `curve->addKeyFrame(key)` in `Engine/KnobImpl.h:2208-2225`. `Curve::addKeyFrame()` inserts/replaces by time at `Engine/Curve.cpp:421-435`. Natron recomputes derivatives for auto interpolation types but explicitly does not do so for `eKeyframeTypeBroken` or `eKeyframeTypeFree` at `Engine/Curve.cpp:1675-1678`, so user-authored free/broken tangents survive while automatic interpolation remains Natron-controlled.
- Duplicate avoidance: `Gui/FluxTextAnimatorModel.cpp:583-586` calls `ensureAnimatorKnobs()` only when `fta_{id}_enabled` is absent. Since `ensureAnimatorKnobs()` creates the complete knob group including `enabled` first at `Gui/FluxTextAnimatorModel.cpp:314-356`, an already-loaded animator knob group will not be recreated on restore. Subsequent value/key restore uses lookups and skips missing knobs rather than creating duplicate knobs at `Gui/FluxTextAnimatorModel.cpp:588-617`.

## Final Recommendation
Ship; the code fixes address the prior blocker and major, but follow with an actual save/reopen animator-curve validation.
