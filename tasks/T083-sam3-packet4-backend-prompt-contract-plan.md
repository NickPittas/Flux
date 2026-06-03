# T083 Packet 4 — DEFERRED PENDING VIEWER TOOLBAR RECOVERY

## Status
deferred-rework-required

## Reason
Backend prompt contract work must not proceed until the viewer-toolbar multi-prompt UX is defined and implemented. Earlier packet assumptions about a single AI-panel-owned prompt are superseded.

## Required Rework Impact
The backend contract must consume the viewer-owned prompt collection:
- multiple points per source/image/video;
- multiple boxes per source/image/video;
- persistent prompt IDs and labels;
- exported source-frame float coordinates and clamped integer SAM3 coordinates;
- source width/height and run metadata.

No backend path may fall back to hardcoded/default center/full-image/text prompts. The AI panel may trigger Preview/Run, but it must pass viewer-owned prompt state.

## Replacement Dependency
First complete `tasks/T083-sam3-viewer-toolbar-multiprompt-recovery-plan.md` and update this backend packet from that contract.
