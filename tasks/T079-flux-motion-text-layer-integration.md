# T079 — FluxMotionText layer integration

Status: BLOCKED  
Phase: P7 — Shapes + Text  
Started: 2026-05-25  
Completed: —  
Owner: opencode

> Completion revoked 2026-05-25: Nick reproduced immediate GUI failure when adding a text layer (`Failed to create TextRender1`). Prior validation did not use the canonical user GUI runtime and is not acceptance evidence. The prototype is rejected as product work because it lacks the required font UI, Add Animator UI, visible animator stack, range selectors, and per-element animation. Recovery source of truth: `tasks/FLUX_MOTION_TEXT_RECOVERY_SOURCE_OF_TRUTH.md`.

## Goal

Make newly added Flux text layers use the new `net.sf.openfx.FluxMotionText` nodegroup backed by `net.flux.openfx.TextRender`, while keeping legacy `plugins/FluxText.py` untouched and usable for old projects/reference.

## Approved Scope

- Update menu/context text layer gating to require the new FluxMotionText path instead of the old `net.fxarena.openfx.Text` provider.
- Make new text layer graph creation instantiate `net.sf.openfx.FluxMotionText`.
- Double-check and update viewer overlay registration for the new text transform knob names.
- Double-check and update Flux keyframe filtering so new text knobs can appear when animated.
- Fix any gating issues discovered during integration.
- Do not mutate legacy `plugins/FluxText.py`.

## Validation Required

- Build `Natron` and `NatronRenderer`.
- Compile `plugins/FluxMotionText.py`.
- Create a text layer through the Flux layer path and verify it uses `net.sf.openfx.FluxMotionText`.
- Verify new text renders real visible glyphs.
- Verify trim/move/opacity still work.
- Verify overlay/keyframe code can target new text knob names.
- Save/reload and render the new text layer.

## Rejected Implementation Evidence — Not Product Acceptance

The previous integration claim is revoked. The implementation below is factual history only; it does not prove a usable FluxMotionText product.

- New Flux text layer graph creation now instantiates `net.sf.openfx.FluxMotionText`.
- Layer menu and timeline context gating now require discoverable `net.sf.openfx.FluxMotionText` instead of depending on the legacy native Text provider.
- `FluxMotionText.py` now exposes a `Transform1` stage and aliases transform controls (`translate`, `scale`, `rotate`, `center`, etc.) alongside text/font/fill/timing/opacity controls.
- Viewer overlay registration supports both legacy `Text1...` transform names and the new direct FluxMotionText transform names.
- Timeline keyframe filtering exposes the new direct FluxMotionText text/transform controls while preserving legacy `Text1...` support.
- Legacy `plugins/FluxText.py` was not modified.

## Validation

- `python -m py_compile plugins/FluxMotionText.py` passed.
- `cmake --build /home/npittas/Flux/build --target NatronRenderer -- -j$(nproc)` passed.
- `cmake --build /home/npittas/Flux/build --target Natron -- -j$(nproc)` passed.
- Headless create/render/save/reload validation created `net.sf.openfx.FluxMotionText`, verified internal `TextRender1 -> FrameRange1 -> TimeOffset1 -> Transform1 -> Grade1 -> Output1`, verified public params and transform alias key persistence, rendered visible glyphs, verified opacity changes, and verified save/reload equality.
- Render/analyzer marker: `T079_FLUX_MOTION_TEXT_LAYER_OK`.
- Gating discovery marker: `T079_GATING_ID_OK`.

These validation results are invalid as product acceptance because they were headless/narrow and did not measure Nick's requested GUI workflow or AE-style text animator behavior.

## Review

- Oracle review verdict applied only to narrow implementation mechanics. It is not product acceptance and does not permit marking this task done.
- A product blocker exists: the real GUI path failed and the user-facing animator workflow does not exist.
- Minor non-blocker: provider gating logic is duplicated between `Gui.cpp` and `FluxTimeline.cpp`; acceptable for T079 and can be centralized later if it grows.

## Current Failure

- Canonical/default runtime failed to create the internal provider:
  - `FluxMotionText.createInstance(app1, app1.getNode("FluxMotionText1"))`
  - `RuntimeError: Failed to create TextRender1`
- Root symptom: `net.sf.openfx.FluxMotionText` was discoverable, but required internal provider `net.flux.openfx.TextRender` was not available in the real GUI runtime path.
- T079 must remain blocked/rejected. The next approved recovery step is RMT-002 quarantine/cleanup, not more acceptance claims for this prototype.
