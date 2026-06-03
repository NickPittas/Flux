# Planner Report

## Status
ready

## Rationale
The reviewer blockers are confined to two unscoped behavior changes in `Gui/Gui05.cpp`: SAM3 source export dimension/error metadata and removal of the pre-existing source-frame capture signal path. The fix can be safely implemented in the single authorized edit file without changing the approved AI Work Viewer helper/routing architecture or any provider/runtime/backend code.

# Task Packet

## User Goal
Fix the AI Work Viewer implementation review blockers by reverting unapproved SAM3 source export metadata/error behavior and restoring the pre-existing `sourceFrameCaptureRequested` capture connection, while leaving the approved AI Work Viewer routing architecture intact.

## Mode
general-coding

## Relevant Locations
- file: `Gui/Gui05.cpp`
  symbol: `Gui::exportFluxSam3SourceFrameForSelectedLayer`
  approximate lines: 788-812
  stable anchor: `const QSize exportedSize = QImageReader(outputPath).size();`
  reason: reviewer identified unscoped export blocking when image dimensions are invalid and added dimension metadata; revert this non-viewer-architecture behavior.
  confidence: high
- file: `Gui/Gui05.cpp`
  symbol: `Gui::setupFluxUi` timeline signal wiring
  approximate lines: 1188-1244
  stable anchor: `QObject::connect(timeline, &FluxTimeline::sourceViewerRequested, this,`
  reason: nearby AI/source viewer wiring exists, but the prior `sourceFrameCaptureRequested -> exportFluxSam3SourceFrameForSelectedLayer()` connection is missing and must be restored without removing `sourceViewerRequested` behavior.
  confidence: high

## Allowed Edit Files
- `Gui/Gui05.cpp`

## Read-Only Context Files
- `tasks/T083-ai-work-viewer-implementation-plan.md`
- `Gui/Gui.h`
- `Gui/FluxTimeline.h`
- `Gui/FluxTimeline.cpp`

## Required Change
1. In `Gui::exportFluxSam3SourceFrameForSelectedLayer` around the `outputOk` validation and `sourceMetadata` construction:
   - Remove the `QImageReader(outputPath).size()` dimension read that was added for metadata/error behavior.
   - Remove the invalid-dimension hard block that logs `FLUX-SAM3-A1 capture blocked: exported source dimensions unavailable` and returns `QString()`.
   - Remove the added dimension metadata entries (`exported_png_width`, `exported_png_height`, `width`, `height`) unless they already existed in the original approved plan; current review says they did not and are out of scope.
   - Preserve existing file-existence/size validation, existing source metadata fields, and the final capture completion logging.

2. In `Gui::setupFluxUi` timeline signal wiring near the existing `sourceViewerRequested` connection:
   - Restore a `QObject::connect(timeline, &FluxTimeline::sourceFrameCaptureRequested, this, ...)` connection that invokes `exportFluxSam3SourceFrameForSelectedLayer()`.
   - Do not remove or weaken the approved AI Work Viewer `sourceViewerRequested` path.
   - Do not route `sourceFrameCaptureRequested` through AI Work Viewer unless that was already present before the implementation; the reviewer-required restoration is the direct capture call behavior.
   - Keep the connection narrow: no AI panel prompt control changes, no backend calls beyond the existing export helper, and no main viewer rewiring.

## Non-Goals
- Do not change `Gui/Gui.h`, `Gui/FluxTimeline.h`, or `Gui/FluxTimeline.cpp` unless the target signal/symbol is missing, in which case stop and report.
- Do not change RotoPaint, AI Panel prompt controls, provider runtime scripts, SAM backend, or SAM worker behavior.
- Do not remove or redesign the approved AI Work Viewer helper/routing architecture.
- Do not remove `sourceFrameCaptureRequested` behavior.
- Do not perform git operations.

## Validation
Commands:
- `cmake --build /home/npittas/Flux/build -j$(nproc)`

Expected result:
- Build succeeds.
- `Gui/Gui05.cpp` no longer blocks SAM3 source export solely because `QImageReader(outputPath).size()` is invalid.
- `Gui/Gui05.cpp` no longer adds unapproved exported PNG dimension metadata.
- `sourceFrameCaptureRequested` is connected to `exportFluxSam3SourceFrameForSelectedLayer()` again.
- Manual GUI validation for the AI Work Viewer remains outstanding after this code fix and should be reported as remaining, not treated as a blocker for applying this narrow fix.

## Stop Conditions
Stop and report if:
- target symbol is missing
- required fix exceeds allowed files
- validation cannot run
- existing architecture contradicts the requested change
- task requires product/design judgment not in packet
- restoring `sourceFrameCaptureRequested` requires changing `FluxTimeline` declarations or emissions outside `Gui/Gui05.cpp`
- reverting the dimension metadata/error behavior would require changing SAM backend/provider runtime files

## Required Return Contract
Return only a task-focused summary. Do not include transcript, tool logs, raw file dumps, large code blocks, or broad unrelated issues. Include status, files inspected/changed, validation evidence, blockers, and task-specific risks.
