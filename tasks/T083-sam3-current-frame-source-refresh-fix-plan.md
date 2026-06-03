# Planner Report

## Status
ready

## Rationale
The implementation review Major is isolated to the AI Panel source-frame refresh path used by Run and Live Preview. `FluxAiPanel.cpp` already has access to `Gui::getFluxTimeline()`, `FluxTimeline::getCurrentFrame()`, `getSelectedLayerIndex()`, and `getLayers()`, so the fix can be contained to `FluxAiPanel::refreshSourceFrameMetadata()` without changing timeline behavior, worker protocol, Add/Replace, Roto, preview routing, or git state.

# Task Packet

## User Goal
Fix the T083 SAM3/AI Paint regression recovery Major: Run and Live Preview must export the current timeline frame for the currently selected timeline layer, using `sourceFrame = currentFrame - layer.timeOffset` clamped to the layer original media range, instead of reusing stale stored source-frame context from the moment Source Viewer was selected.

## Mode
general-coding

## Relevant Locations
- file: `Gui/FluxAiPanel.cpp`
  symbol: `FluxAiPanel::refreshSourceFrameMetadata`
  approximate lines: 943-1030
  stable anchor: `AI Panel stored-context source-frame export attempted`
  reason: This helper is called by both Run and Live Preview and currently passes `_sourceTimelineFrame/_sourceSourceFrame` directly to `exportFluxSam3SourceFrameForSourceContext()`.
  confidence: high
- file: `Gui/FluxAiPanel.cpp`
  symbol: `FluxAiPanel::runAIPaintLivePreview`
  approximate lines: 1163-1212
  stable anchor: `if (!refreshSourceFrameMetadata(&message) || !validateSourceCaptureContext(&message))`
  reason: Live Preview depends on `refreshSourceFrameMetadata()` and must inherit the current-frame recompute fix.
  confidence: high
- file: `Gui/FluxAiPanel.cpp`
  symbol: `FluxAiPanel::onRunClicked`
  approximate lines: 1495-1535
  stable anchor: `if (!refreshSourceFrameMetadata(&message) || !validateSourceCaptureContext(&message))`
  reason: Run depends on `refreshSourceFrameMetadata()` and must inherit the current-frame recompute fix.
  confidence: high
- file: `Gui/FluxTimeline.h`
  symbol: `FluxLayer`, `FluxTimeline::getLayers`, `FluxTimeline::getCurrentFrame`, `FluxTimeline::getSelectedLayerIndex`
  approximate lines: 126-139 and 219-231
  stable anchor: `int originalFirstFrame;`, `int originalLastFrame;`, `int timeOffset;`, `int getCurrentFrame() const;`
  reason: Provides the selected layer, current timeline frame, original media range, and time offset needed for the recompute.
  confidence: high
- file: `Gui/Gui05.cpp`
  symbol: `Gui::exportFluxSam3SourceFrameForSelectedLayer`, `Gui::exportFluxSam3SourceFrameForSourceContext`, source context setters
  approximate lines: 747-752, 760-903, 1381-1540
  stable anchor: `const int sourceFrame = qBound(layer.originalFirstFrame, timelineFrame - layer.timeOffset, layer.originalLastFrame);`
  reason: Shows the approved mapping already used when selecting/exporting from timeline context and confirms the exporter accepts explicit timeline/source frame values plus metadata.
  confidence: high
- file: `.pi/agent-artifacts/T083-regression-recovery-implementation-review.md`
  symbol: Review Major
  approximate lines: 21-31
  stable anchor: `Run/Live Preview refresh uses the stored source-frame context`
  reason: Authoritative review finding this plan addresses.
  confidence: high

## Allowed Edit Files
- `Gui/FluxAiPanel.cpp`

## Read-Only Context Files
- `Gui/FluxAiPanel.h`
- `Gui/Gui05.cpp`
- `Gui/FluxTimeline.h`
- `Gui/FluxTimeline.cpp`
- `tasks/T083-sam3-ai-panel-regression-recovery-plan.md`
- `.pi/agent-artifacts/T083-regression-recovery-implementation-review.md`

## Required Change
In `Gui/FluxAiPanel.cpp`, update only `FluxAiPanel::refreshSourceFrameMetadata(QString* message)`.

Before constructing diagnostics and before calling `_gui->exportFluxSam3SourceFrameForSourceContext(...)`:
1. Reacquire `FluxTimeline* timeline = _gui ? _gui->getFluxTimeline() : nullptr`.
2. Block with a clear message if no timeline exists.
3. Read `const int selectedLayerIndex = timeline->getSelectedLayerIndex()` and `const QList<FluxLayer>& layers = timeline->getLayers()`.
4. Preserve mismatch blocking:
   - selected layer index must be in range;
   - selected layer index must equal `_sourceLayerIndex`;
   - selected layer name/file path must match `_sourceLayerName/_sourceFilePath` when those stored strings are non-empty;
   - selected layer reader must be present, active, and match `_sourceReaderNode` when `_sourceReaderNode` is set;
   - selected layer reader label must match `_sourceReaderLabel` when `_sourceReaderLabel` is non-empty.
5. Compute `currentTimelineFrame = timeline->getCurrentFrame()`.
6. Compute requested source frame as `currentTimelineFrame - layer.timeOffset`.
7. Clamp to `[layer.originalFirstFrame, layer.originalLastFrame]` using `qBound`.
8. Update `_sourceTimelineFrame` to `currentTimelineFrame` and `_sourceSourceFrame` to the clamped source frame before export.
9. If clamping occurred, append/log diagnostics including requested source frame, clamped source frame, and original range. Do not silently export frame 0 for media with original range `[1,81]`.
10. Update `_sourceLabel` consistently after recompute, even if metadata later returns the same frame values.
11. Call `_gui->exportFluxSam3SourceFrameForSourceContext(...)` with the recomputed `_sourceTimelineFrame/_sourceSourceFrame`.
12. Keep the existing post-export metadata freshness checks and `exportedSourceMetadataMatchesSelection()` validation. Do not weaken existing mismatch/stale-source blocking.

Implementation notes:
- Prefer local logic in `refreshSourceFrameMetadata()`; do not add a new header declaration unless absolutely necessary.
- `FluxAiPanel.cpp` already includes `Gui/FluxTimeline.h`, so no new include should be needed for `FluxLayer` access.
- Do not edit `Gui/Gui05.cpp`; its exporter already clamps and writes metadata, but the reviewer requires the Run/Live Preview caller to recompute before export.

## Non-Goals
- No git restore/reset/checkout/revert or any git-state restoration.
- No ad-hoc Python/shell rewrite scripts.
- No changes to Python SAM3 worker protocol or provider scripts.
- No changes to Add/Replace mask behavior, `FluxTimeline`, native Roto/RotoPaint, Premult, or main comp viewer routing.
- No broad AI Panel redesign, worker lifecycle rewrite, result-history change, or task/phase status update.
- No attempt to perform Nick’s final manual GUI validation; prepare the fix for it.

## Validation
Commands:
- `cmake --build /home/npittas/Flux/build --target Natron -j$(nproc)`
- If available and lightweight after build, run the existing non-GUI SAM3/provider smoke used for T083 implementation recovery and report the exact command/result. Do not invent a smoke command if none is known from local project context.

Expected result:
- Build succeeds.
- Source inspection confirms `onRunClicked()` and `runAIPaintLivePreview()` still call `refreshSourceFrameMetadata()` and now receive recomputed current-frame source metadata.
- Logs/diagnostics make it possible for Nick’s manual GUI validation to verify that moving the playhead on `[1,81]` media never exports source frame 0; expected mapping is `sourceFrame = currentFrame - layer.timeOffset`, clamped to `[1,81]`.
- After implementation review, hand off for Nick’s manual GUI validation of Run/Live Preview current-frame behavior.

## Stop Conditions
Stop and report if:
- `FluxAiPanel::refreshSourceFrameMetadata` or its Run/Live Preview callers are missing or structurally different from the anchors above.
- Recomputing current frame requires edits outside `Gui/FluxAiPanel.cpp`.
- The selected timeline layer cannot be obtained through `Gui::getFluxTimeline()`, `FluxTimeline::getSelectedLayerIndex()`, and `FluxTimeline::getLayers()`.
- Reader/source identity matching is ambiguous and would require product/design judgment.
- The fix would require changing Add/Replace, native Roto/RotoPaint, SAM3 worker protocol, Python files, or viewer routing.
- Build or smoke validation cannot run; report exact blocker and do not claim validation.

## Planner Self-Check
- locator evidence sufficient: yes — review finding and authorized source anchors identify `refreshSourceFrameMetadata()` as the defective shared Run/Live Preview refresh path.
- allowed edit files minimal and explicit: yes — only `Gui/FluxAiPanel.cpp` is needed because required timeline APIs are already available and included.
- read-only context minimal: yes — limited to AI Panel header, timeline APIs, exporter context, approved T083 plan, and implementation review.
- anchors/lines included: yes — each relevant location includes path, symbol, approximate lines, anchor, reason, and confidence.
- validation concrete: yes — build command plus source-inspection expectations and Nick manual GUI follow-up for user-facing proof.
- parallelization decision explicit and safe: yes — single task; not parallelized because one function in one file gates both Run and Live Preview and shares AI Panel source state.
- non-goals and stop conditions sufficient: yes — explicitly blocks git restoration/scripts, scope expansion, protected workflows, and ambiguous source identity changes.
- reviewer findings addressed, if revision: yes — addresses the Major by requiring current selected layer/current frame recompute before export while preserving mismatch blocking.

## Required Return Contract
Return only a task-focused summary. Do not include transcript, tool logs, raw file dumps, large code blocks, or broad unrelated issues. Include status, files inspected/changed, validation evidence, blockers, and task-specific risks.
