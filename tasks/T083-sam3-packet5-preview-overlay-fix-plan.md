# T083 Packet 5 Preview Overlay Fix Plan — DEFERRED / SUPERSEDED

## Status
deferred-pending-viewer-toolbar-multiprompt-recovery

## Authoritative Nick Commands
Nick's direct commands and the original approved plan override this packet. For T083, SAM3 point/box selection must be in the viewer toolbar, not the AI panel; the viewer must show persistent visual indicators for every point and box; multiple points and boxes per image/video must be supported; add/remove/clear controls belong in the viewer toolbar.

## Why This Packet Is Deferred
This packet was written to finish preview-overlay validation after the implementation had already drifted into AI-panel-owned prompt selection and single-prompt assumptions. That premise is invalid.

Preview-overlay work must not continue as the next fix path until `tasks/T083-sam3-viewer-toolbar-multiprompt-recovery-plan.md` is implemented and reviewed. The overlay must consume viewer-owned multi-prompt/source metadata, not AI-panel point/box reset flows.

## Salvageable Notes
The non-conflicting status fix remains conceptually useful: if SAM3 generation succeeds but a preview overlay cannot be displayed, the UI must show a non-success preview-blocked status rather than generic success.

The non-conflicting validation expectation remains useful after recovery: GUI proof must show overlay visible, overlay cleared after viewer-toolbar prompt clear/source changes, and preview-blocked status when overlay display is rejected.

## Superseded Guidance
Any instruction in the old version of this packet that references AI-panel point/box prompt/reset ownership is superseded. Future overlay work must be replanned after viewer-toolbar multi-prompt recovery.
