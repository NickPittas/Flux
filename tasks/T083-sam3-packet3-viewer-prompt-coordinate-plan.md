# T083 Packet 3 — SUPERSEDED / INVALID

## Status
superseded-invalid

## Reason
This packet did not correctly enforce Nick's source-of-truth UX: point/box selection must be a viewer-toolbar workflow with persistent visual indicators for every point and box, and it must support multiple points and boxes per image/video before backend/preview/apply work proceeds. It also allowed AI-panel point/box controls, which are invalid.

## Correct Boundary
Viewer toolbar owns:
- point mode;
- box mode;
- add/remove selected prompt;
- clear prompts;
- persistent display of every point and box;
- multi-point and multi-box prompt state.

AI panel may summarize viewer-owned prompts but must not own point/box add/remove UX.

## Replacement
Use `tasks/T083-sam3-viewer-toolbar-multiprompt-recovery-plan.md` and the corrected sequence in `tasks/T083-sam3-complete-workflow-plan.md`.
