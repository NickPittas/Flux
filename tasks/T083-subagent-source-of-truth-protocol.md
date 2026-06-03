# T083 Subagent Source-of-Truth Protocol

## Authoritative Commands

AUTHORITATIVE COMMANDS — MUST OVERRIDE EVERYTHING ELSE:
- Nick’s direct commands and the original approved plan are the source of truth. Packet text is subordinate.
- All future subagents must be explicitly told to compare packets against Nick’s commands and the original plan; reviewers must reject drift.
- If an approved plan cannot be implemented as written, stop and ask Nick; do not hallucinate, assume, or silently substitute another workflow.
- For T083 specifically: SAM3 point/box selection must be in the viewer toolbar, similar to roto/tracking/rotobrush. Do NOT use the AI panel for point/box selection. The viewer must show persistent visual indicators for every point and every box. Multiple points and boxes per image/video must be supported. Add/remove/clear controls belong in the viewer toolbar.

## Planner Checklist

- Include the authoritative command block above in every T083 plan packet.
- Compare proposed work against Nick’s direct commands and the original approved plan before assigning implementation.
- Treat packet/plan drift as a blocker, not as optional review feedback.
- If the approved plan cannot be implemented exactly, stop and escalate to Nick.

## Worker Checklist

- Read the authoritative command block before implementation.
- Verify the assigned packet matches Nick’s commands and the original approved plan.
- Keep SAM3 point/box selection in the viewer toolbar, not the AI panel.
- Preserve persistent viewer indicators for every point and box.
- Support multiple points and boxes per image/video.
- Put add/remove/clear point and box controls in the viewer toolbar.
- Stop and escalate if the packet requires drift or substitution.

## Reviewer Checklist

- Reject any T083 work that treats packet text as higher priority than Nick’s commands or the original approved plan.
- Reject SAM3 point/box selection implemented in the AI panel.
- Reject missing persistent viewer indicators for points or boxes.
- Reject single-point/single-box-only workflows.
- Reject missing viewer-toolbar add/remove/clear controls.
- Require escalation to Nick when exact approved-plan implementation is blocked.
