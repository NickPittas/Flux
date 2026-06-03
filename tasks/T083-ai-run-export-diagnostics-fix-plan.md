# Planner Report

## Status
ready

## Rationale
The current source already routes AI Panel Run through the stored-context exporter, but `FluxAiPanel::refreshSourceFrameMetadata()` can still emit the old generic empty-PNG message if the returned path is unusable and the exporter leaves diagnostics empty or the running build does not match expectations. A narrow AI Panel-only change can make that generic message impossible by seeding diagnostics with the stored capture context before export, appending concrete returned PNG path/existence/size facts on unusable output, and logging successful exports so a manual Run proves the new binary/source path is active.

# Task Packet

## User Goal
Make the persistent AI Panel Run error `source-frame export failed or produced an empty PNG` impossible in current source, and add a successful source-frame export log line proving the stored-context export path is in use. Preserve current stored-context exporter and preview behavior.

## Mode
general-coding

## Relevant Locations
- file: `Gui/FluxAiPanel.cpp`
  symbol: `FluxAiPanel::refreshSourceFrameMetadata(QString*)`
  approximate lines: 518-555
  stable anchor: `QString diagnostics;` followed by `exportFluxSam3SourceFrameForSourceContext(...)`
  reason: This is the only AI Panel failure branch that still falls back to the generic empty-PNG message when diagnostics is empty; seed diagnostics before export, append returned PNG file facts on failure, and log success after metadata validation.
  confidence: high
- file: `Gui/FluxAiPanel.cpp`
  symbol: `FluxAiPanel::onRunClicked()`
  approximate lines: 747-805
  stable anchor: `if (!refreshSourceFrameMetadata(&message) || !validateSourceCaptureContext(&message))`
  reason: Manual Run displays/logs the message produced by `refreshSourceFrameMetadata()`; no logic change is expected here, but it is the validation path for the new diagnostics and success log.
  confidence: high
- file: `Gui/Gui05.cpp`
  symbol: `Gui::exportFluxSam3SourceFrameForSourceContext(...)`
  approximate lines: 728-940
  stable anchor: `auto fail = [diagnostics](const QString& text) -> QString`
  reason: Context exporter is already supposed to populate diagnostics on internal failures; preserve this behavior and do not alter it unless implementation discovers a compile/signature issue.
  confidence: high
- file: `Gui/FluxAiPanel.h`
  symbol: `class FluxAiPanel`
  approximate lines: 33-71
  stable anchor: `bool refreshSourceFrameMetadata(QString* message);`
  reason: Confirms no header/API change is required for a local implementation in `refreshSourceFrameMetadata()`.
  confidence: high
- file: `tasks/T083-ai-run-export-preview-fix-plan.md`
  symbol: prior stored-context export/preview plan
  approximate lines: 1-110
  stable anchor: `Change FluxAiPanel::refreshSourceFrameMetadata() to call the context-based exporter`
  reason: Prior plan established stored-context export and preview behavior that must be preserved.
  confidence: high
- file: `tasks/T083-ai-run-export-preview-blocker-fix-plan.md`
  symbol: prior blocker fix plan
  approximate lines: 1-95
  stable anchor: `Preserve the existing good implementation for: stored-context source export`
  reason: Confirms this task should not disturb Save As/output-root fixes, stored-context export, dimensions, manifest paths, or AI Work Viewer preview behavior.
  confidence: high

## Allowed Edit Files
- `Gui/FluxAiPanel.cpp`

## Read-Only Context Files
- `Gui/Gui05.cpp`
- `Gui/FluxAiPanel.h`
- `tasks/T083-ai-run-export-preview-fix-plan.md`
- `tasks/T083-ai-run-export-preview-blocker-fix-plan.md`

## Required Change
1. In `FluxAiPanel::refreshSourceFrameMetadata(QString* message)`, initialize `diagnostics` before calling `_gui->exportFluxSam3SourceFrameForSourceContext(...)` with a rich stored-context snapshot. Include at minimum: stored layer index, layer name, source file path, reader label, whether `_sourceReaderNode` is present, stored timeline frame, stored source frame, and a marker such as `AI Panel stored-context source-frame export attempted`.
2. Keep passing `&diagnostics` into `exportFluxSam3SourceFrameForSourceContext(...)` so specific exporter failures still replace or enrich the seeded context. Do not change exporter routing or preview behavior.
3. Replace the unusable-PNG branch so it never emits `source-frame export failed or produced an empty PNG`. When `png` is empty, missing, not a file, or size <= 0, append returned-output facts to the current diagnostics: returned path (or `<empty>`), `QFileInfo::exists()`, `isFile()`, and `size()`. Assign that detailed string to `message` and return false.
4. After the PNG passes file/size checks and metadata validation/dimension validation succeeds, call `appendLog(...)` with a concise success line proving the current code path was used. Include stored-context wording plus path, size, dimensions, layer index/name, source frame, and timeline frame. Keep the existing success `message` value acceptable for callers.
5. Do not alter provider Python, SAM inference, RotoPaint, prompt selection/serialization, main comp viewer routing, AI Work Viewer preview helper, output path/schema, Save As preflight, or `Gui::exportFluxSam3SourceFrameForSourceContext(...)` unless a compile-only adjustment is strictly required.

## Non-Goals
- No provider/backend Python or SAM process changes.
- No RotoPaint, mask application, layer graph, prompt behavior, or model manager changes.
- No main comp viewer routing changes.
- No refactor of the stored-context exporter or AI Work Viewer result preview.
- No changes to generated output locations, manifest schema, or project Save As behavior.

## Validation
Commands:
- `cmake --build /home/npittas/Flux/build --target Natron -j$(nproc)`
- Manual GUI validation: launch the updated Natron/Flux binary from the existing build/run path, open a saved project with a valid AI Work Viewer source context and SAM3 prompt, click AI Panel Run, and confirm the panel log contains the new successful stored-context source export line when export succeeds.
- Manual GUI validation: if source export fails, confirm the panel log/message contains the stored-context snapshot plus returned PNG path/existence/isFile/size facts, and does not contain the exact generic text `source-frame export failed or produced an empty PNG`.

Expected result:
- Build succeeds.
- Manual Run proves the updated code path with a successful source-frame export log line, or reports detailed context/file diagnostics on failure.
- The exact generic empty-PNG message is no longer emitted by current source.

## Stop Conditions
Stop and report if:
- target symbol is missing
- required fix exceeds allowed files
- validation cannot run
- existing architecture contradicts the requested change
- task requires product/design judgment not in packet
- making the generic error impossible requires changing `Gui::exportFluxSam3SourceFrameForSourceContext(...)`, backend Python, provider code, RotoPaint, prompt behavior, or viewer routing
- `appendLog(...)` is unavailable from `refreshSourceFrameMetadata()` or cannot be safely used there without a broader class/API change
- manual validation indicates the running application is using an old binary/source path despite the source change

## Planner Self-Check
- locator evidence sufficient: yes — high-confidence anchors identify the exact generic-message branch, Run call site, and stored-context exporter behavior.
- allowed edit files minimal and explicit: yes — only `Gui/FluxAiPanel.cpp` is needed for seeded diagnostics, unusable-output message enrichment, and success logging.
- read-only context minimal: yes — limited to the exporter implementation/header and the two prior T083 plans that define behavior to preserve.
- anchors/lines included: yes — every relevant location includes path, symbol, approximate lines, stable anchor, reason, and confidence.
- validation concrete: yes — build plus manual GUI checks for success log and detailed non-generic failure diagnostics.
- parallelization decision explicit and safe: yes — single task; the change is one function in one file, so parallelization would add coordination overhead and risk conflicts.
- non-goals and stop conditions sufficient: yes — they exclude backend/RotoPaint/prompt/main-viewer/exporter refactors and require stopping if broader scope is needed.
- reviewer findings addressed, if revision: not applicable — no reviewer findings were supplied for this new diagnostics plan.

## Required Return Contract
Return only a task-focused summary. Do not include transcript, tool logs, raw file dumps, large code blocks, or broad unrelated issues. Include status, files inspected/changed, validation evidence, blockers, and task-specific risks.
