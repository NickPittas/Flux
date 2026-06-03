# Planner Report

## Status
ready

## Rationale
The failure is isolated to `FluxAiPanel::validateSourceCaptureContext()`: stored source export now succeeds from the reader context, but validation still assumes AI Work Viewer input 0 must remain connected directly to the reader. Authorized evidence shows the intended AI Paint workflow deliberately reconnects AI Work Viewer input 0 to the selected AI Paint node while preserving `_sourceReaderNode` and `_sourceAIPaintNode`, so a narrow validation-only change in `Gui/FluxAiPanel.cpp` is sufficient and avoids main viewer, provider/Python, RotoPaint, or auto-run scope.

# Task Packet

## User Goal
Fix Flux AI Panel Run validation so the AI Work Viewer may be connected to the selected/stored AI Paint node while Run continues to use the stored reader/source context for source-frame export and source coordinate conversion. Prefer accepting viewer input 0 when it matches the stored activated AI Paint node; otherwise require the stored reader node as fallback.

## Mode
general-coding

## Relevant Locations
- file: `Gui/FluxAiPanel.cpp`
  symbol: `FluxAiPanel::validateSourceCaptureContext(QString*) const`
  approximate lines: 609-646
  stable anchor: `if (_sourceViewerNode->getInput(0) != _sourceReaderNode) {`
  reason: This is the blocker: validation rejects the correct AI Paint workflow with `viewer input 0 no longer matches selected reader` even after stored source-frame export succeeds.
  confidence: high
- file: `Gui/FluxAiPanel.cpp`
  symbol: `FluxAiPanel::onRunClicked()`
  approximate lines: 782-846
  stable anchor: `if (!refreshSourceFrameMetadata(&message) || !validateSourceCaptureContext(&message))`
  reason: Run invokes stored-context export first, then this validation before building SAM3 prompts; the fix must let execution progress to `buildAIPaintPromptForSam(...)` when AI Work Viewer is connected to the stored AI Paint node.
  confidence: high
- file: `Gui/FluxAiPanel.cpp`
  symbol: `FluxAiPanel::readAIPaintPrompts(std::vector<AIPaintPrompt>*, QString*) const`
  approximate lines: 395-405
  stable anchor: `if (!_sourceAIPaintNode || !_sourceAIPaintNode->isActivated())`
  reason: Existing prompt read path already requires an activated stored AI Paint node for Run, matching the intended preferred validation acceptance case.
  confidence: high
- file: `Gui/FluxAiPanel.h`
  symbol: `class FluxAiPanel` private members
  approximate lines: 83-91
  stable anchor: `NodePtr _sourceAIPaintNode;`
  reason: Confirms stored AI Paint and reader nodes already exist in panel state; no header change should be needed unless implementation chooses a very small private helper.
  confidence: high
- file: `Gui/Gui05.cpp`
  symbol: `timeline->sourceViewerRequested` handler
  approximate lines: 1228-1288
  stable anchor: `viewerNode->connectInput(layer.readerNode, 0);`
  reason: Source Viewer arming initially connects AI Work Viewer input 0 to the reader and stores reader plus optional AI Paint node context.
  confidence: high
- file: `Gui/Gui05.cpp`
  symbol: `FluxEffectsPanel::effectSelected` AI Paint branch
  approximate lines: 1398-1417
  stable anchor: `viewerNode->connectInput(node, 0);`
  reason: Selecting AI Paint intentionally reconnects AI Work Viewer input 0 to AI Paint and stores the same reader plus AI Paint context, causing the current stale reader-only validation failure.
  confidence: high
- file: `Gui/Gui05.cpp`
  symbol: `timeline->effectSelected` AI Paint branch
  approximate lines: 1472-1489
  stable anchor: `viewerNode->connectInput(node, 0);`
  reason: Timeline effect selection has the same intended AI Paint viewer routing and stored source context path.
  confidence: high
- file: `tasks/T083-ai-run-export-preview-fix-plan.md`
  symbol: previous Task Packet / validation requirements
  approximate lines: 1-110
  stable anchor: `Change FluxAiPanel::refreshSourceFrameMetadata() to call the context-based exporter using _sourceLayerIndex`
  reason: Previous plan established that source-frame export should use stored source context and that main comp viewer routing is out of scope; this follow-up must preserve that direction.
  confidence: high

## Allowed Edit Files
- `Gui/FluxAiPanel.cpp`

## Read-Only Context Files
- `Gui/FluxAiPanel.h`
- `Gui/Gui05.cpp`
- `tasks/T083-ai-run-export-preview-fix-plan.md`

## Required Change
1. In `FluxAiPanel::validateSourceCaptureContext(QString*) const`, keep the existing viewer identity checks, `_sourceViewerNode` activation check, `_sourceReaderNode` activation check, source-frame freshness/dimension checks, and `exportedSourceMetadataMatchesSelection(...)` check.
2. Replace the strict `getInput(0) == _sourceReaderNode` requirement with narrow accepted-input logic:
   - Read `NodePtr viewerInput0 = _sourceViewerNode->getInput(0)` once.
   - If `_sourceAIPaintNode` exists, is activated, and `viewerInput0 == _sourceAIPaintNode`, accept the viewer input as valid for the AI Paint workflow.
   - Otherwise require `viewerInput0 == _sourceReaderNode` as the fallback source-viewer mode.
3. Preserve source reader validation regardless of accepted viewer input: `_sourceReaderNode` must still exist and be activated because stored-context export/source file correctness depends on it.
4. If validation fails, update the diagnostic so it accurately describes both accepted states, for example that AI Work Viewer input 0 no longer matches the stored AI Paint node or stored reader. Do not keep the misleading reader-only message.
5. Do not modify `refreshSourceFrameMetadata()`, `onRunClicked()` ordering, prompt building, provider/Python invocation, source export metadata matching, RotoPaint behavior, auto-run behavior, or any `Gui05.cpp` viewer wiring.
6. Do not add broad fallback behavior that accepts arbitrary viewer inputs, deactivated AI Paint nodes, main comp viewer nodes, or mismatched readers.

## Non-Goals
- No main comp viewer routing changes.
- No provider/Python/SAM3 worker changes.
- No RotoPaint or mask-application changes.
- No auto-run on prompt placement.
- No changes to stored source-frame export or metadata identity validation.
- No broad AI Panel refactor or new product behavior beyond this validation acceptance fix.

## Validation
Commands:
- `cmake --build /home/npittas/Flux/build --target Natron -j$(nproc)`
- Manual GUI validation: open a saved project with a footage layer and AI Paint effect, arm Source Viewer/AI Work Viewer for the layer, select the AI Paint effect so AI Work Viewer input 0 is connected to AI Paint, then press AI Panel Run.
- Manual GUI validation: repeat the source-viewer fallback path where AI Work Viewer input 0 remains connected to the reader, then press AI Panel Run.
- Manual GUI validation: verify the main comp viewer remains on the normal comp output and is not switched by this change.

Expected result:
- Build succeeds.
- Run progresses past the previous `viewer input 0 no longer matches selected reader` validation when AI Work Viewer input 0 matches the stored activated AI Paint node; the next visible progress should be SAM3 prompt/build/start logging or a later unrelated SAM3/backend failure.
- Reader-connected fallback remains valid.
- Source-frame export/source metadata validation still requires the stored reader context and nonzero dimensions.
- Main comp viewer behavior is unchanged.

## Stop Conditions
Stop and report if:
- target symbol is missing
- required fix exceeds allowed files
- validation cannot run
- existing architecture contradicts the requested change
- task requires product/design judgment not in packet
- `_sourceAIPaintNode` is not reliably stored for AI Paint selection paths shown in `Gui05.cpp`
- accepting AI Paint input would require bypassing `_sourceReaderNode` activation or metadata validation
- coordinate conversion proves to require a broader source-reader transform API rather than the existing viewer/source metadata checks

## Planner Self-Check
- locator evidence sufficient: yes — high-confidence anchors show the failing validation, Run call order, stored AI Paint member, and Gui05 AI Work Viewer routing to reader vs AI Paint.
- allowed edit files minimal and explicit: yes — only `Gui/FluxAiPanel.cpp` is needed for the validation logic and diagnostic change.
- read-only context minimal: yes — header confirms stored nodes, Gui05 confirms intended wiring, previous plan confirms stored export/main-viewer constraints.
- anchors/lines included: yes — each relevant location includes path, symbol, approximate lines, stable anchor, reason, and confidence.
- validation concrete: yes — build plus two manual AI Work Viewer input states and main viewer non-regression.
- parallelization decision explicit and safe: yes — single task; the only edit is one validation function, so parallelization would add overhead and risk conflicting in `FluxAiPanel.cpp`.
- non-goals and stop conditions sufficient: yes — they prevent backend/RotoPaint/main-viewer/export refactors and require stopping on missing stored AI Paint context or broader coordinate requirements.
- reviewer findings addressed, if revision: not applicable — no reviewer findings were supplied for this plan.

## Required Return Contract
Return only a task-focused summary. Do not include transcript, tool logs, raw file dumps, large code blocks, or broad unrelated issues. Include status, files inspected/changed, validation evidence, blockers, and task-specific risks.
