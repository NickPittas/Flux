# T083 — AI Matte/Mask Generation and Video Depth Tools

**Status:** IN_PROGRESS
**Started:** 2026-05-26
**Current stage:** SAM3 AI Paint routing is moving from still-frame inference to trimmed-range video inference. The AI Panel now exposes an editable SAM3 frame range defaulted from the selected layer trim, shows worker progress, exports the selected source range as temporary PNG frames, and can send `infer_video` requests to the persistent SAM3 worker for project-relative mask sequences.

## Nick-Approved Decisions

1. SAM3.1/SAM3+ is user-downloadable through the installation/model manager script, not bundled by default.
2. MatAnyone2 nodes/panel must warn that the model is non-commercial; Flux informs users of model licensing because Flux is FOSS.
3. DepthCrafter follows the same policy: user-downloadable with visible non-commercial/restrictive license warnings.
4. RVM/XMem2 are allowed as external subprocess helpers.
5. Unsaved projects must force **Save As** before AI generation.

## Architecture Summary

- CUDA-first external Python worker launched out-of-process from Flux; keep PyTorch/CUDA isolated from the Qt/Natron process.
- AI panel for timeline users plus nodegraph-usable AI nodes for power users.
- **Authoritative T083 prompt UX:** point/box selection, add/remove/clear controls, and persistent prompt indicators belong in the viewer toolbar, similar to roto/tracking/rotobrush. Do not use the AI panel for point/box selection. The AI panel may summarize viewer-owned prompts and own task/model/status/run/apply/log controls only.
- Viewer prompt selection is first-class: multiple points and boxes per image/video, visible persistent viewer indicators for every prompt, source-frame coordinate metadata, plus later text/current alpha/mask prompt support where approved.
- AI outputs are project-relative generated raster media: alpha/mask sequences, foregrounds, and depth EXRs referenced by Flux masks/effects/nodes.
- Secure model handling: no plaintext tokens, no `huggingface_hub.login()`/`hf auth login`; use desktop secret stores where available or one-shot token use.
- Unsaved projects force Save As before any generated media is written.
- Roto spline conversion is deferred; primary output is raster alpha/depth media.

## Model Roles and Legal/Install Status

| Model | Role | Install status | Legal/status | Notes |
|---|---|---|---|---|
| SAM3.1 / SAM3+ line | Viewer prompt segmentation, base matte, video object masks/tracks | User-downloadable/gated via secure model manager | Meta SAM License; not bundled by default | Highest-priority promptable path. |
| MatAnyone2 | Video human matte refinement from base/first-frame mask | User-downloadable/legal-gated | Non-commercial; visible warnings required | Real option, not default commercial bundle. |
| BiRefNet | Base matte/background removal | Default candidate after testing | MIT | First permissive worker validation target. |
| ViTMatte | Edge refinement from base mask/trimap | Default candidate after testing | MIT | Refines SAM/BiRefNet base mattes. |
| Video Depth Anything | Baseline depth node | Manifest/license check required | Pending final license review | Baseline depth provider. |
| DepthCrafter | Video-stable depth option | User-downloadable/legal-gated | Restrictive/non-commercial; warnings required | Heavy VRAM; expose resolution/stride controls. |
| RVM | Fast human video matting helper | External subprocess helper only | GPL3; do not link/bundle into GPL2 app | Allowed as external helper. |
| XMem2 | Semi-supervised video mask propagation helper | External subprocess helper only | GPL3; do not link/bundle into GPL2 app | Allowed as external helper. |

## Milestones

### T083-0 — Legal/model manifest decisions

**Status:** DONE
**Goal:** Record model manifest, license gates, install policy, and approved user-download/external-helper decisions.
**User-visible behavior:** Model manager/panel can explain why a model is installed, missing, gated, non-commercial, or external-only.
**Implementation surfaces/files:** `tools/ai/model_manifest.json`, `tools/ai/flux_model_manager.py`, installer/model-manager docs.
**Validation/evidence required:** Manifest lists SAM3.1, MatAnyone2, BiRefNet, ViTMatte, Video Depth Anything, DepthCrafter, RVM, XMem2 with license/install status; screenshots/CLI output show warnings for non-commercial models.
**Evidence:** Added `tools/ai/model_manifest.json` and stdlib-only offline `tools/ai/flux_model_manager.py`; validated with `python3 -m json.tool`, `py_compile`, `list`, `verify --offline`, `status` including temporary XDG homes, and `git diff interactive validation action`.
**Stop conditions:** New license ambiguity; request to bundle gated/restrictive weights; any plaintext-token persistence.

### T083-1 — Foundation: AI panel shell, viewer-toolbar prompt capture, generated media metadata, CUDA worker skeleton

**Status:** RECOVERY_REQUIRED
**Goal:** Establish non-model-specific AI infrastructure while enforcing viewer-toolbar prompt ownership.
**User-visible behavior:** AI panel appears, reflects selected source, shows generated output path/status/logs, and runs workers; viewer toolbar owns point/box add/remove/clear prompt controls and shows persistent indicators for every point and box.
**Implementation surfaces/files:** `Gui/FluxAIPanel.{h,cpp}`, `Gui/FluxAIJobController.{h,cpp}`, `Gui/Gui05.cpp`, `Gui/FluxTimeline.{h,cpp}`, `Gui/FluxTimelineSerialization.h`, `Gui/ViewerTab.{h,cpp}`, `Gui/ViewerGL.cpp`, `Gui/HostOverlay.cpp`, `tools/ai/flux_ai_worker.py`, `tools/ai/flux_ai_common.py`.
**Validation/evidence required:** Build passes; screenshot of AI panel without point/box add/remove UX; recording of viewer-toolbar point/box add/remove/clear controls; recording showing multiple persistent point/box indicators in the viewer; dummy worker progress/cancel/error; save/reopen preserves prompt/artifact metadata after viewer-toolbar recovery; unsaved project forces Save As.
**Evidence:** Earlier sub-packets added AI panel shell, progress/log UI, Force Save As preflight, project-relative output policy, result manifest contract, and some one-shot AI-panel-owned prompt capture/serialization. The AI-panel-owned point/box capture and single-prompt assumptions are now superseded/invalid. Use `tasks/T083-sam3-authoritative-drift-audit.md` and `tasks/T083-sam3-viewer-toolbar-multiprompt-recovery-plan.md` before further T083-1 work.
**Stop conditions:** Generated media cannot be project-relative; viewer prompt conflicts with existing overlays; graph rebuild would break P6 mask semantics; implementation attempts to put point/box add/remove controls in the AI panel; implementation cannot support multiple visible prompt indicators without asking Nick.

### T083-2 — Secure model manager / installer download flow

**Status:** PENDING
**Goal:** Install/verify/remove models with pinned revisions, hashes, license gates, and secure token handling.
**User-visible behavior:** Public models install without a token; gated models request license/token flow; non-commercial models show warnings.
**Implementation surfaces/files:** `tools/ai/flux_model_manager.py`, `tools/ai/model_manifest.json`, `tools/linux/flux-linux-setup.sh`.
**Validation/evidence required:** interactive installer/model setup proof; model-manager developer command proof; no plaintext token in config/cache/logs; gated SAM3.1 missing-token flow guides user.
**Evidence:** T083-2A implements real model installation through the interactive installer/model-manager flow; HF installs use `snapshot_download(..., token=..., revision=..., allow_patterns=...)`; direct URL installs use stdlib downloads. T083 installer UX fix adds explicit installer menu actions for model install/download, secure HF token entry, status, and removal; those actions bootstrap AI tooling/deps if missing without rebuilding the app. Validation in this run used compile/schema/list/offline verify/status, bash syntax, dry-run developer install plans, SAM3.1 missing-token path, fake-token no-persistence audit, and installer UX grep/non-TTY arg rejection checks; full model downloads are not complete because weights are large/gated.
**Stop conditions:** No secure keyring and persistence requested; HF tooling tries to write plaintext token; legal status missing.

### T083-3 — BiRefNet base matte node

**Status:** PENDING
**Goal:** First permissive CUDA model path for base mattes.
**User-visible behavior:** User generates alpha/mask sequence from image/video layer and applies it as a mask/readable node output.
**Implementation surfaces/files:** Python worker provider, AI panel provider selection, generated media ingestion, nodegraph base matte node.
**Validation/evidence required:** Real image/video artifact; mask sequence under project-relative path; viewport screenshot and nodegraph screenshot.
**Stop conditions:** License/hash mismatch; output cannot reconnect after save/reopen.

### T083-4 — ViTMatte refinement node

**Status:** PENDING
**Goal:** Refine base mattes using generated trimaps.
**User-visible behavior:** Same base matte can be edge-refined with controls for trimap erosion/dilation.
**Implementation surfaces/files:** Worker ViTMatte provider, matte refine node/panel controls.
**Validation/evidence required:** Side-by-side visual comparison showing improved edges; saved project reloads refined media.
**Stop conditions:** Trimap generation produces destructive artifacts without controllable settings.

### T083-5 — SAM3.1 viewer-selection base matte/video mask node

**Status:** PENDING
**Goal:** Promptable SAM3.1 image/video masks from viewer selections.
**User-visible behavior:** Point/box/text/current-mask prompts produce base masks/tracks; missing gated model state is clear.
**Implementation surfaces/files:** Secure model manager, worker SAM provider, viewer prompt serialization, AI panel, segment/track node.
**Validation/evidence required:** Recording of viewer prompt to generated mask; gated-token flow proof; no plaintext tokens.
**Stop conditions:** User has not accepted gated model terms; token storage cannot be secure or one-shot.

### T083-6 — MatAnyone2 video matte refinement node

**Status:** PENDING
**Goal:** Legal-gated video human matte refinement from source video and base/first-frame mask.
**User-visible behavior:** Panel/node warns non-commercial; installed users can refine to alpha/foreground sequence.
**Implementation surfaces/files:** Model manifest, worker provider, matte refine node/panel warnings.
**Validation/evidence required:** Short real clip refined; warning screenshot; output alpha/foreground sequence proof.
**Stop conditions:** License warning absent; commercial/default bundling requested without permission.

### T083-7 — Nodegraph AI nodes integration

**Status:** PENDING
**Goal:** Expose AI BaseMatte, MatteRefine, SegmentTrack, and Depth nodes to nodegraph users.
**User-visible behavior:** Tab/node dialog creates AI nodes usable without timeline-only workflow.
**Implementation surfaces/files:** `Gui/NodeCreationDialog.cpp`, node wrappers/PyPlugs/OFX as chosen, generated media read integration.
**Validation/evidence required:** Nodegraph creation screenshots and simple source-to-output workflow proof for each node class.
**Stop conditions:** Node wrappers require unapproved public API rename or break normal plugin discovery.

### T083-8 — Video Depth Anything depth node

**Status:** PENDING
**Goal:** Baseline video depth generation.
**User-visible behavior:** Source video/image sequence generates EXR depth/disparity and normalized preview.
**Implementation surfaces/files:** Worker depth provider, `FluxAIDepth` node/panel controls, generated EXR read path.
**Validation/evidence required:** Real video depth EXR sequence; viewport preview; save/reopen proof.
**Stop conditions:** Final license/weights manifest cannot be approved.

### T083-9 — DepthCrafter depth option

**Status:** PENDING
**Goal:** Restrictive-license video-stable depth option.
**User-visible behavior:** Non-commercial/restrictive warning is shown before install/run; resolution/stride/VRAM controls are visible.
**Implementation surfaces/files:** Model manifest, worker provider, depth node/panel warnings and controls.
**Validation/evidence required:** Short clip run if user-installed; warning screenshot; VRAM/performance notes.
**Stop conditions:** Warning absent; default commercial bundle requested; VRAM requirements exceed usable presets.

### T083-10 — RVM/XMem2 external helper hooks

**Status:** PENDING
**Goal:** Optional external subprocess hooks for GPL3 helper tools without linking/bundling into Flux.
**User-visible behavior:** Users can point Flux to external helper installs; Flux labels them external/GPL3.
**Implementation surfaces/files:** Model/helper manifest, external command resolver, worker/helper bridge.
**Validation/evidence required:** External helper discovery and run proof; no GPL3 code/weights bundled by accident.
**Stop conditions:** Integration would link GPL3 code into GPL2-only Flux or hide license status.

### T083-11 — Validation/packaging/performance pass

**Status:** PENDING
**Goal:** Prove T083 is restartable, packageable, secure, and performant enough on real media.
**User-visible behavior:** Documented install/manage/run workflow with proof artifacts and known limits.
**Implementation surfaces/files:** task docs, validation notes, installer/model manager, tests/assets when available.
**Validation/evidence required:** Screenshots/recordings, real image/video/depth artifacts, nodegraph proofs, save/reopen, package install, no plaintext token audit, CUDA/VRAM timings.
**Stop conditions:** Missing GUI proof, generated absolute paths, token leak, or broken P6 mask graph preservation.

## Validation Matrix

| Area | Required evidence |
|---|---|
| AI panel | Screenshot showing task/model selectors, install/legal state, source/frame range/output path, Run/Cancel/progress. |
| Viewer prompts | Recording showing point/box/text/current-mask prompt controls and overlay, plus resulting generated mask/depth behavior. |
| Generated media | Files created under project-relative generated-media directory; save/reopen reconnects without absolute-path-only dependence. |
| Nodegraph nodes | Screenshots of AI nodes created from node dialog and connected to source/result nodes. |
| Masks/effects | Layer and effect mask graph survives rebuild, duplicate/split where supported, and save/reopen. |
| Model manager | Interactive installer/model setup proof plus developer command proof for list/install/verify/remove; license warnings for SAM3.1, MatAnyone2, DepthCrafter, RVM, XMem2 as applicable. |
| Token security | Audit confirms no HF token in Flux config, project files, logs, shell commands, Hugging Face plaintext token files, or git credentials. |
| Performance | CUDA device, precision, resolution/stride, VRAM use, frame timings on real media. |
| Packaging | Installer/model manager uses XDG paths and path-agnostic commands. |

## Restart Protocol

1. Read `tasks/TASKS.md` T083 row and this file first.
2. Read `plans/PHASES.md` P7 T083 entry for phase-level status.
3. Use this file plus `tasks/T083-orchestrator-recovery-source-of-truth.md`, `tasks/T083-sam3-authoritative-drift-audit.md`, and `tasks/T083-subagent-source-of-truth-protocol.md` as the compact source of truth. Removed research packets remain available in git history if forensic detail is needed.
4. Pick the first milestone whose status is `PENDING` after any `IN_PROGRESS` milestone is either completed or explicitly blocked.
5. Before coding, create a narrow task packet for that milestone with allowed edit files and validation commands.
6. Do not implement code from this planning file without an approved coding packet.
