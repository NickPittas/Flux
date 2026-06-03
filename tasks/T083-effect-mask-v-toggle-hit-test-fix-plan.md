# Planner Report

## Status
ready

## Rationale
The locator evidence and focused source inspection identify a single coordinate-space mismatch in `FluxTimeline::mousePressEvent`: effect/mask V buttons are painted at viewport Y but hit-tested with unscrolled content row Y. The fix is confined to the existing hit rectangles in `Gui/FluxTimeline.cpp` and does not require graph-path changes because the existing toggle path already emits compositing changes.

# Task Packet

## User Goal
Fix the visible effect/mask row `V` toggle in the Flux timeline so clicking the drawn `V` button toggles enable state.

## Mode
general-coding

## Relevant Locations
- file: `Gui/FluxTimeline.cpp`
  symbol: `FluxTimeline::drawLayerBars`
  approximate lines: 1589-1593, 1855-1881, 1903-1929
  stable anchor: `int y = kTimeRulerHeight + visibleRow.y - _scrollOffsetY;` and `QRect enableRect(20, y + 4, 16, qMax(10, rowHeight - 8));`
  reason: effect and mask V buttons are painted using viewport-space `y`.
  confidence: high
- file: `Gui/FluxTimeline.cpp`
  symbol: `FluxTimeline::mousePressEvent`
  approximate lines: 3015-3069
  stable anchor: `if (clickRow->type == eFluxVisibleRowEffect)` / `if (clickRow->type == eFluxVisibleRowMask)` and `QRect enableRect(20, clickRow->y + 4, 16, qMax(10, clickRow->height - 8));`
  reason: effect/mask V hit-test currently uses content-space `clickRow->y`, causing misses when scrolled relative to the painted viewport position.
  confidence: high
- file: `Gui/FluxTimeline.cpp`
  symbol: `FluxTimeline::yToRow`
  approximate lines: 2649-2655
  stable anchor: `int contentY = y - kTimeRulerHeight + _scrollOffsetY;`
  reason: confirms rows store content coordinates and mouse Y is converted to content coordinates for row lookup.
  confidence: high
- file: `Gui/FluxTimeline.cpp`
  symbol: graph/compositing update path
  approximate lines: 3032-3040, 3066-3074, 4437-4441
  stable anchor: `_layers[li].effects[ei].enabled = !_layers[li].effects[ei].enabled;` and `setNodeDisabled(!visible || !layer.effects[e].enabled);`
  reason: existing toggle and graph disabled propagation should remain unchanged.
  confidence: high

## Allowed Edit Files
- `Gui/FluxTimeline.cpp`

## Read-Only Context Files
- `/home/npittas/.pi/agent/COLLABORATION.md`
- `/home/npittas/Flux/AGENTS.md`
- `Gui/FluxTimeline.cpp`

## Required Change
In `FluxTimeline::mousePressEvent`, update only the effect-row and mask-row enable button hit rectangles so their Y coordinate matches the viewport-space paint coordinate:

- For effect rows, replace the hit-test rect Y source `clickRow->y + 4` with `kTimeRulerHeight + clickRow->y - _scrollOffsetY + 4` (or an equivalent local `rowViewportY` used only in this branch).
- For mask rows, make the same replacement.
- Leave X/width/height, bounds checks, selection behavior, emitted signals, and graph/compositing logic unchanged.
- Do not change layer-row L/V/S hit-tests; those already use `rowYForLayer(layerIdx)`.

Expected behavior: clicking directly on a visible effect or mask row `V` button toggles that effect/mask enabled state at any vertical scroll offset, and clicking elsewhere on the effect/mask label row still selects the row.

## Non-Goals
- Do not redesign timeline hit-testing or row layout.
- Do not change effect/mask enable semantics, node graph propagation, or `setNodeDisabled` calls.
- Do not modify layer-row L/V/S controls.
- Do not edit task status files, phase files, generated files, tests, build config, or unrelated GUI code.

## Validation
Commands:
- `cmake --build build --target Flux`
- Manual GUI check: create/open a timeline with at least one expanded layer containing an effect and a mask; scroll vertically if needed; click the visible `V` button on an effect row and on a mask row; capture screenshot/recording showing the controls and resulting enabled/disabled visual state.

Expected result:
- Build succeeds, or if the local build directory/target is unavailable, report that clearly without substituting unrelated validation.
- Manual GUI proof shows effect and mask `V` buttons toggle when clicked at their drawn positions, including after vertical scroll; row selection still works when clicking outside the `V` button.

## Stop Conditions
Stop and report if:
- target symbol is missing
- required fix exceeds allowed files
- validation cannot run
- existing architecture contradicts the requested change
- task requires product/design judgment not in packet
- effect or mask V drawing no longer uses `kTimeRulerHeight + visibleRow.y - _scrollOffsetY`, making the locator evidence stale
- fixing the issue requires changing graph propagation, row construction, or selection behavior

## Planner Self-Check
- locator evidence sufficient: yes — locator and source inspection agree on paint/hit-test coordinate mismatch in `FluxTimeline::mousePressEvent`.
- allowed edit files minimal and explicit: yes — only `Gui/FluxTimeline.cpp` is needed.
- read-only context minimal: yes — only required Pi/project instructions and focused timeline source context were used.
- anchors/lines included: yes — relevant paint, hit-test, y conversion, and graph propagation anchors are listed with approximate lines.
- validation concrete: yes — build command plus GUI screenshot/recording check for the actual control behavior.
- parallelization decision explicit and safe: yes — single task; no parallelization because the coherent change is one source file and one behavior.
- non-goals and stop conditions sufficient: yes — prevent graph, layout, layer-control, and unrelated project-file scope creep.
- reviewer findings addressed, if revision: not applicable — no reviewer findings were supplied for this plan.

## Required Return Contract
Return only a task-focused summary. Do not include transcript, tool logs, raw file dumps, large code blocks, or broad unrelated issues. Include status, files inspected/changed, validation evidence, blockers, and task-specific risks.
