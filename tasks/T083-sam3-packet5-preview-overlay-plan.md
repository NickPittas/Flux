# T083 Packet 5 — DEFERRED PENDING VIEWER TOOLBAR RECOVERY

## Status
deferred-rework-required

## Reason
Preview overlay work depends on the corrected viewer-owned multi-prompt model. Overlay behavior must reflect source changes and prompt collection changes from the viewer toolbar, not AI-panel-owned single-prompt state.

## Required Rework Impact
The preview overlay plan must consume:
- viewer-owned source/prompt collection metadata;
- multiple point and box prompt IDs;
- exported source-frame dimensions and coordinates;
- backend result manifests keyed to that prompt collection.

Overlay clearing must happen on viewer-toolbar add/remove/clear operations, source changes, failed/stale runs, and prompt collection replacement. The graph/layer model remains untouched until Apply.

## Replacement Dependency
First complete `tasks/T083-sam3-viewer-toolbar-multiprompt-recovery-plan.md`, then update preview overlay work to match the recovered prompt ownership model.
