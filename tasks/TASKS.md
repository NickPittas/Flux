# Flux — Master Task List

Last updated: 2026-05-26

## Active Phase: P7 (Shapes + Text)

### P0 Tasks

| ID | Task | Status | Assigned | Started | Completed | File |
|---|---|---|---|---|---|---|
| T001 | Create AGENTS.md with project guidelines | DONE | forge | 2026-05-20 | 2026-05-20 | — |
| T002 | Create plans/PHASES.md with phase breakdown | DONE | forge | 2026-05-20 | 2026-05-20 | — |
| T003 | Create tasks/TASKS.md with task tracking | DONE | forge | 2026-05-20 | 2026-05-20 | — |
| T004 | Create plans/phase-1.md with detailed P1 plan | DONE | forge | 2026-05-20 | 2026-05-20 | — |
| T005 | Update ARCHITECTURE.md for Qt+Natron approach | DONE | forge | 2026-05-20 | 2026-05-20 | — |
| T006 | Fork Natron RB-2.6 into Flux repo | DONE | forge | 2026-05-20 | 2026-05-20 | — |

---

## P1 Tasks (Fork & Build)

| ID | Task | Status | Assigned | Started | Completed | File |
|---|---|---|---|---|---|---|
| T007 | Install Natron build dependencies on Linux | DONE | forge | 2026-05-20 | 2026-05-20 | — |
| T008 | Build Natron from source (gui-sbk6 branch + Qt6) | DONE | forge | 2026-05-20 | 2026-05-20 | — |
| T009 | Run Natron GUI, verify it works | DONE | forge | 2026-05-20 | 2026-05-20 | — |
| T009.5 | Build & install OpenFX plugins (IO + Misc), validate file import + OCIO | DONE | forge | 2026-05-20 | 2026-05-20 | — |
| T010 | Build Engine/ as shared library (strip Gui/) | SKIPPED | — | — | — | Keeping full Natron app, modifying GUI |
| T011 | Test headless render via Renderer/ process | SKIPPED | — | — | — | Renderer/ already builds and works |
| T012 | Document key Engine classes for Flux | DONE | forge | 2026-05-20 | 2026-05-20 | plans/2026-05-20-engine-*.md |
| T013 | Validate import: test with real mov/mp4/mxf files | DONE | nick | 2026-05-20 | 2026-05-20 | ReadFFmpeg via OpenFX-IO |
| T014 | Validate import: test with real exr/tiff/png/jpg/psd files | DONE | nick | 2026-05-20 | 2026-05-20 | ReadOIIO via OpenFX-IO |
| T015 | Validate OCIO: test color transforms with real config | DONE | nick | 2026-05-20 | 2026-05-20 | Using Nuke OCIO config |
| T016 | Validate cache: test RAM/disk cache with real footage | DEFERRED | — | — | — | Works well enough, optimize later |
| T017 | Validate playback: test real-time playback performance | DEFERRED | — | — | — | Works well enough, optimize later |
| T018 | Set up Flux development workflow (build, test, run) | DONE | forge | 2026-05-20 | 2026-05-20 | Build/run cycle established |

---

## P2 Tasks (UI Shell) — COMPLETE

| ID | Task | Status | Assigned | Started | Completed | File |
|---|---|---|---|---|---|---|
| T019 | Create FluxMainWindow (new layout) | DONE | forge | 2026-05-20 | 2026-05-21 | Gui/Gui05.cpp, Flux*.{h,cpp} |
| T019-A | Project Bin: thumbnails, list view toggle, file import | DONE | forge | 2026-05-20 | 2026-05-20 | Gui/FluxProjectBin.{h,cpp} |
| T019-B | Timeline: accept drops from Project Bin with ghost preview | DONE | forge | 2026-05-20 | 2026-05-20 | Gui/FluxTimeline.{h,cpp} |
| T019-C | Timeline ↔ Viewer playhead sync (via TimeLine::seekFrame) | DONE | forge | 2026-05-20 | 2026-05-20 | Gui/FluxTimeline.{h,cpp} |
| T019-D | Effects Panel: per-layer effect stack | DONE | forge | 2026-05-20 | 2026-05-20 | Gui/FluxEffectsPanel.{h,cpp} |
| T019-E | End-to-end test: import → drag → select → add effect → play | DONE | nick | 2026-05-21 | 2026-05-21 | Manual test |
| T019-F | Timeline interaction: bar drag, trim handles, reorder | DONE | forge | 2026-05-21 | 2026-05-21 | Gui/FluxTimeline.{h,cpp} |
| T019-G | Auto-create Merge nodes + connect viewer | DONE | forge | 2026-05-21 | 2026-05-21 | Gui/Gui05.cpp |
| T019-H | FluxLayer PyPlug gizmo (Read→FrameRange→TimeOffset→Transform→Output) | DONE | forge | 2026-05-21 | 2026-05-21 | plugins/FluxLayer.py |
| T019-I | Fix timeline drop: one gizmo per layer, Merge chain outside | DONE | forge | 2026-05-21 | 2026-05-21 | Gui/Gui05.cpp |
| T019-J | Fix trim: update FrameRange via explicit trimStart/trimEnd | DONE | forge | 2026-05-21 | 2026-05-21 | Gui/FluxTimeline.cpp |
| T019-K | Fix move: update only timeOffset, never frameRange | DONE | forge | 2026-05-21 | 2026-05-22 | Gui/FluxTimeline.cpp |
| T019-L | Fix multi-layer: each layer gets own gizmo | DONE | forge | 2026-05-21 | 2026-05-21 | Gui/Gui05.cpp |
| T019-M | Deferred init with retry + nodeInitialized guard | DONE | forge | 2026-05-21 | 2026-05-21 | Gui/Gui05.cpp |
| T019-N | Fix PyPlug defaults: scale=1, shutter=0.5 via restoreDefaultValue() | DONE | forge | 2026-05-21 | 2026-05-21 | plugins/FluxLayer.py |
| T019-O | Rewrite trim/move: complete separation of frameRange and timeOffset. inPoint/outPoint=source frames (trim only), timeOffset=bar position (move only). Bar clipped at left panel. | DONE | forge | 2026-05-22 | 2026-05-22 | Gui/FluxTimeline.{h,cpp}, Gui/Gui05.cpp |
| T020 | Create FluxTimeline widget | DONE | forge | 2026-05-20 | 2026-05-20 | Gui/FluxTimeline.{h,cpp} (merged into T019) |
| T021 | Create Layer-to-Node Bridge (Merge chain) | DONE | forge | 2026-05-21 | 2026-05-21 | Gui/Gui05.cpp (merged into T019-G) |
| T022 | Create Effects Stack Panel | DONE | forge | 2026-05-20 | 2026-05-20 | Gui/FluxEffectsPanel.{h,cpp} |
| T023 | Create Dark Theme (After Effects-inspired) | DONE | forge | 2026-05-20 | 2026-05-20 | Gui/Gui20.cpp |
| T024 | Create Project Bin | DONE | forge | 2026-05-20 | 2026-05-20 | Gui/FluxProjectBin.{h,cpp} |
| T025 | Create Flux Menu System — Flux mode now uses File/Edit/Layer/Composition/View/Window/Help menus; Edit mirrors NodeGraph context edit actions; Layer drives timeline add/duplicate/split/delete/effect/mask actions; Window focuses Flux panes; Natron menu path preserved. Oracle-reviewed, build and smoke launch passed. | DONE | forge | 2026-05-24 | 2026-05-24 | Gui/Gui.cpp, Gui/FluxTimeline.{h,cpp} |
| T026 | Integration Test (end-to-end) — live P3 signoff: wheel zoom/scroll, Alt-drag, F, label resize, Add Effect, Reset In/Out approved | DONE | nick | 2026-05-22 | 2026-05-22 | Manual test |

---

## P3 Tasks (Timeline)

| ID | Task | Status | Assigned | Started | Completed | File |
|---|---|---|---|---|---|---|
| T032 | Playback controls — viewer already drives shared TimeLine during playback. Removed competing FluxTimeline play loop. FluxTimeline follows via onExternalFrameChanged. | DONE | — | 2026-05-21 | 2026-05-21 | Gui/FluxTimeline.{h,cpp} |
| T033 | Keyboard shortcuts — already working: JKL for play fwd/back/stop, arrows for frame stepping. No changes needed. | DONE | — | 2026-05-21 | 2026-05-21 | — |
| T034 | Layer types: solid (FluxSolid PyPlug with Constant), null (no gizmo, parenting-ready). Context menu for Add Solid/Null. Transform overlay handles registered on gizmo via addTransformInteract. Properties panel opens/closes on layer select/deselect. | DONE | forge | 2026-05-21 | 2026-05-21 | plugins/Flux{Layer,Solid}.py, Gui/Gui05.cpp, Gui/FluxTimeline.{h,cpp} |
| T035 | Solo/Mute/Lock: Solo/Mute already done. Lock now has dedicated L button/toggle in layer header. Locked layers block trim, move, reorder, delete, split, and duplicate operations. | DONE | forge | 2026-05-22 | 2026-05-22 | Gui/FluxTimeline.{h,cpp} |
| T036 | Split layer (Ctrl+Shift+D) — duplicate + trim original outPoint / duplicate inPoint at playhead | DONE | forge | 2026-05-22 | 2026-05-22 | Gui/FluxTimeline.cpp |
| T037 | Duplicate layer (Ctrl+D) — keyboard shortcut + context menu; native Natron copy/paste; Read+Gizmo+Merge for footage, Gizmo+Merge for solids | DONE | forge | 2026-05-22 | 2026-05-22 | Gui/FluxTimeline.cpp, Gui/Gui05.cpp |
| T038 | Delete layer (Delete key) — deactivates Read/Gizmo/Merge and rebuild reconnects remaining chain | DONE | forge | 2026-05-22 | 2026-05-22 | Gui/FluxTimeline.cpp, Gui/Gui05.cpp |
| T039 | Layer bar context menu: Add Solid/Null, Delete, Duplicate, Split, Add Effect, Reset In/Out Points — all implemented | DONE | forge | 2026-05-22 | 2026-05-22 | Gui/FluxTimeline.cpp |
| T040 | Timeline zoom/scroll/pan: plain wheel zoom, Ctrl+wheel horizontal scroll, horizontal wheel/trackpad horizontal scroll, Alt+drag/middle-drag pan, F fit-to-view, resizable label/name panel with fixed L/V/S control column | DONE | forge | 2026-05-22 | 2026-05-22 | Gui/FluxTimeline.cpp |
| T041 | Frame range from project settings — synced via Project::frameRangeChanged signal → FluxTimeline::setFrameRange | DONE | forge | 2026-05-22 | 2026-05-22 | Gui/Gui05.cpp, Gui/FluxTimeline.cpp |
| T042 | Background/Reformat anchor: auto-create Reformat node (project format) at top of chain, inputs disconnected each rebuild. Pure source canvas. | DONE | forge | 2026-05-22 | 2026-05-22 | Gui/Gui05.cpp |

---

## P4 Tasks (Effects + Properties)

| ID | Task | Status | Assigned | Started | Completed | File |
|---|---|---|---|---|---|---|
| T043 | Map Natron plugin discovery/categories and choose effect insertion API for Flux layer branches | DONE | forge | 2026-05-22 | 2026-05-22 | Gui/FluxEffectsPanel.{h,cpp}, Gui/Gui05.cpp |
| T044 | Timeline Tab uses Natron's existing node search dialog; selected layer gets effect at bottom of its stack; no selection creates adjustment-effect row after final merge. Verified live: Tab opens Natron node search; selected layer adds effect at bottom of stack; no selection/context empty-space creates adjustment effect row; effect settings opens on creation. | DONE | forge | 2026-05-22 | 2026-05-22 | Gui/FluxTimeline.{h,cpp}, Gui/Gui05.cpp |
| T045 | Rebuild graph with effect-aware timeline order: per-layer child effects before that layer's Merge, adjustment rows between Merges by timeline position. Verified live: per-layer child effects wire before Merge, adjustment effects wire by timeline order, stale/manual-deleted effect nodes are pruned, graph reconnects to valid nodes. | DONE | forge | 2026-05-22 | 2026-05-22 | Gui/Gui05.cpp |
| T046 | Treat effect/adjustment rows like layer stack entries for move/delete/lock/duplicate/split where applicable, without recreating nodes. Verified live: adjustment rows support select/reorder/delete/lock/mute/Add Effect, solo disabled, main-pipe nodegraph verticality preserved on move, null rows support select/reorder/delete/lock, unsupported duplicate/split/trim/reset safely blocked, effect-bearing layer duplicate/split deferred to T049/T050. | DONE | forge | 2026-05-22 | 2026-05-22 | Gui/FluxTimeline.{h,cpp}, Gui/Gui05.cpp |
| T047 | Show/select actual effect nodes and Natron knob panels; remove hardcoded Flux effect picker as source of truth. Verified live: Effects panel lists actual model effects, click/double-click reopens Natron settings panel, remove deactivates/removes from model and refreshes UI. | DONE | forge | 2026-05-22 | 2026-05-22 | Gui/FluxEffectsPanel.{h,cpp}, Gui/Gui05.cpp |
| T048 | P4 save/reopen persistence — serialize Flux timeline/effects state into project files and restore rows, adjustment rows, effect ownership/order, node references, Flux Background, and graph wiring on reopen | DONE | forge | 2026-05-22 | 2026-05-22 | Gui/FluxTimelineSerialization.h, Gui/ProjectGuiSerialization.*, Gui/FluxTimeline.*, Gui/Gui40.cpp, Gui/Gui.h, Gui/ProjectGui.cpp |
| T049 | Duplicate effect-bearing layers and adjustment rows — clipboard copy/paste includes effect nodes, pasted effects identified by plugin ID and added to new layer/adjustment row model. Adjustment rows duplicatable. | DONE | forge | 2026-05-22 | 2026-05-22 | Gui/FluxTimeline.cpp |
| T050 | Split effect-bearing layers — duplicateLayer now copies effects via clipboard, splitLayer trims both halves as before. No split for adjustment rows (deferred to T051). | DONE | forge | 2026-05-22 | 2026-05-22 | Gui/FluxTimeline.cpp |
| T051 | Adjustment row trim/split/move — keyframe-based enable/disable on each effect's disable knob. Trim: keyframe disabled at startFrame-1, enabled at startFrame, disabled at endFrame+1. Move: shifts keyframes with timeOffset. Split: duplicates row, trims both halves. Reset clears all keyframes. All effects keyframed simultaneously on every change. | DONE | forge | 2026-05-22 | 2026-05-22 | Gui/FluxTimeline.cpp |

---

## P5 Tasks (Import/Export)

| ID | Task | Status | Assigned | Started | Completed | File |
|---|---|---|---|---|---|---|
| T052 | Right-click context menu "Open Read Node" on footage layers — opens the Read node's Natron settings panel in the properties bin so user can set color science, output components, etc. | DONE | forge | 2026-05-22 | 2026-05-22 | Gui/FluxTimeline.cpp |
| T053 | Flux Export/Render panel — Reformat node (disabled by default), Write node, file browse, frame range, advanced settings buttons, Render button via startWritersRendering. Oracle-audited. | DONE | forge | 2026-05-22 | 2026-05-22 | Gui/FluxExportPanel.{h,cpp}, Gui/Gui05.cpp, Gui/GuiPrivate.h |

---

## P6 Tasks (Timeline Tree + Masks)

| ID | Task | Status | Assigned | Started | Completed | File |
|---|---|---|---|---|---|---|
| T054 | Timeline visible-row model — add `FluxVisibleRow`, expanded layer state, `yToRow()`, variable row heights; initially generate only layer rows so behavior stays identical. Oracle-reviewed, build passed. | DONE | forge | 2026-05-23 | 2026-05-23 | Gui/FluxTimeline.{h,cpp} |
| T055 | Timeline effect sub-rows — expanded layer shows main-pipe effects as indented children; selecting an effect row opens only that effect's Natron properties. Oracle-reviewed, build passed. | DONE | forge | 2026-05-23 | 2026-05-23 | Gui/FluxTimeline.{h,cpp}, Gui/Gui05.cpp |
| T056 | Move effect actions into timeline — add/remove/reorder effects from timeline row context menus; begin retiring FluxEffectsPanel. Oracle-reviewed, build passed. | DONE | forge | 2026-05-23 | 2026-05-23 | Gui/FluxTimeline.{h,cpp}, Gui/Gui05.cpp, Gui/FluxEffectsPanel.{h,cpp} |
| T057 | FluxMask data model + serialization — add `FluxMask`, `FluxLayer::masks`, `maskApplyNode`, `hasPrecompBranch`, and save/reopen persistence. Oracle-reviewed, build passed. | DONE | forge | 2026-05-23 | 2026-05-23 | Gui/FluxTimeline.h, Gui/FluxTimelineSerialization.h, Gui/FluxTimeline.cpp |
| T058 | Mask and branch discovery utilities — implement `discoverMaskInput(NodePtr)`, `isPremultNode(NodePtr)`, and classify upstream layer graph as main pipe, mask branches, or precomp branches. Oracle-reviewed, build passed. | DONE | forge | 2026-05-23 | 2026-05-23 | Gui/FluxMaskUtils.{h,cpp} |
| T059 | Timeline mask sub-rows and mask model actions — layer/effect mask model entries can be added from timeline context menus and appear as child rows; selecting a mask opens Roto/RotoPaint properties only when backing nodes already exist. No graph node creation or wiring yet. Oracle-reviewed, build passed. | DONE | forge | 2026-05-23 | 2026-05-23 | Gui/FluxTimeline.{h,cpp}, Gui/Gui05.cpp |
| T060 | Layer mask graph — create/connect Reformat→Roto mask source and mask-apply node in the layer main pipe; enforce terminal Premult rule. Oracle-reviewed, build passed. | DONE | forge | 2026-05-23 | 2026-05-23 | Gui/Gui05.cpp, Gui/FluxTimeline.{h,cpp} |
| T061 | Effect mask graph — create/connect Reformat→Roto side branch into discovered effect mask input; effect mask branches must not count as precomp. Oracle-reviewed, build passed. | DONE | forge | 2026-05-23 | 2026-05-23 | Gui/Gui05.cpp, Gui/FluxTimeline.{h,cpp} |
| T062 | Preserve manual layer branches — rebuild must preserve user-added main-pipe and precomp-branch nodes; precomp icon shown on timeline layer row for non-mask branches. Oracle-reviewed, build passed. | DONE | forge | 2026-05-23 | 2026-05-23 | Gui/Gui05.cpp, Gui/FluxTimeline.{h,cpp}, Gui/FluxMaskUtils.{h,cpp} |
| T062A | P6 scrutinize blocker fixes — FluxLayerSerialization class version, duplicate/split disabled for masked/precomp layers until T063, classifier source base fixed to gizmo, terminal Premult+maskApply classifier input path fixed, restored `hasPrecompBranch` treated as derived, runtime layout artifact removed. | DONE | forge | 2026-05-23 | 2026-05-23 | Gui/FluxTimelineSerialization.h, Gui/FluxTimeline.cpp, Gui/FluxMaskUtils.cpp |
| T063 | Duplicate/split full branch — branch-aware copy/paste for non-adjustment layers copies main pipe, mask branches, and precomp branches; restores FluxEffect/FluxMask refs by old script name; split uses independent copied nodes; adjustment rows with masks remain disabled. Scrutinize-reviewed, maskApply required-ref fix applied, build passed. | DONE | forge | 2026-05-23 | 2026-05-23 | Gui/FluxTimeline.cpp, Gui/Gui05.cpp |
| T064 | Correct layer-mask graph — replaced unapproved Merge(in) layer mask implementation with inline `source → [Unpremult] → Roto → Premult → Merge A`; old Flux-owned Merge/Reformat artifacts migrated narrowly; stale inline mask chains and Flux-owned Unpremult cleanup fixed. Oracle-reviewed, scrutinize verdict ship, build passed. | DONE | forge | 2026-05-23 | 2026-05-23 | Gui/Gui05.cpp, Gui/FluxMaskUtils.{h,cpp}, Gui/FluxTimeline.cpp |
| T065 | Roto/RotoPaint replace selected channels — native `Zero selected input channels` checkbox zeros selected/process channels before Roto/RotoPaint compositing via hidden internal `RotoReplaceChannels`; save/reopen, GL Shadertoy context, stale panel load crash, and user CImg OFX discovery fixes validated. Oracle-reviewed, build passed. | DONE | forge | 2026-05-23 | 2026-05-23 | tasks/T065-roto-replace-selected-channels.md |
| T066 | UI layout/styling update — new pane layout (ProjectBin/NodeGraph | Viewer | Properties/Export top, full-width Timeline/DopeSheet/CurveEditor bottom); modernize flux-dark.qss; remove inline export button stylesheet. Build passed; deferred autosave prompt validated with restore Yes and clear No/Escape while preserving Nick's autosave. | DONE | forge | 2026-05-23 | 2026-05-23 | Gui/Gui05.cpp, Gui/Gui20.cpp, Gui/FluxExportPanel.cpp, Gui/Resources/Stylesheets/flux-dark.qss, Gui/GuiAppInstance.cpp |
| T067 | Property control polish — larger readable line edits/spin boxes/dropdowns/buttons/tabs, modern checkbox treatment, more visible slider axis/handle, and final Flux QSS overrides to beat duplicate Natron rules without rewriting the stylesheet. Build passed; autosave launch smoke passed. | DONE | forge | 2026-05-24 | 2026-05-24 | Gui/Resources/Stylesheets/flux-dark.qss, Gui/LineEdit.cpp, Gui/ComboBox.cpp, Gui/ScaleSliderQWidget.cpp, Gui/AnimatedCheckBox.cpp |
| T068 | Export panel scroll/layout fix — replaced the nested Write-only scroll area with a single full-panel scroll area so Write codec controls and Reformat controls keep natural height and the whole Export panel scrolls when the pane is small. Build passed; screenshot captured. | DONE | forge | 2026-05-24 | 2026-05-24 | Gui/FluxExportPanel.{h,cpp} |
| T069 | Restore missing OFX provider coverage — SeExpr/Text/Tile/Magick/ResolveMath providers installed; dependency audit now reports 0 missing IDs; `lp_roughenEdges`, `lp_SimpleKeyer`, `Luma_to_Normals`, and `Vectors_Normalize` creation validated; missing-plugin/library diagnostics Oracle-reviewed. | DONE | forge | 2026-05-24 | 2026-05-24 | tasks/T069-ofx-plugin-restoration.md |

---

## P7 Tasks (Shapes + Text)

| ID | Task | Status | Assigned | Started | Completed | File |
|---|---|---|---|---|---|---|
| T071 | Text layer v1 — FluxText PyPlug wraps native Text→FrameRange→TimeOffset→Grade→Output. Native Text knobs are promoted as `Text1...` group knobs in Natron PyPlug-exporter style and linked with `setAsAlias()`, with Text controls first in properties. Timeline trim uses FrameRange; opacity uses Grade multiply; viewer transform overlay registers against promoted Text transform knobs; `Text1name` syncs to `Text1font`; promoted cascading font menus preserve grouping; 2D viewer overlay edits now create keyframes correctly. Build passed, `plugins/FluxText.py` compiles, deployed extras passed, Oracle reviewed overlay-keyframe fix, and live GUI validation passed for Text v1. | DONE | forge | 2026-05-24 | 2026-05-24 | plugins/FluxText.py, Engine/KnobTypes.cpp, Gui/FluxTimeline.cpp, Gui/Gui05.cpp, Gui/HostOverlay.cpp |
| T072 | Native Text justification/alignment follow-up — CANCELLED. Standalone native Text OFX alignment lives in external openfx-arena `Text.ofx.bundle` binary/source, not Flux-owned TextRender. Dropped in favor of FluxMotionText/TextRender path. | CANCELLED | — | — | 2026-05-26 | — |
| T073 | Dope Sheet/keyframe readability polish — improved native Dope Sheet row separators, keyframe diamond outlines/selected highlights, selected-key time-label readability, and separator width handling. Build passed; Nick accepted as done. | DONE | forge | 2026-05-26 | 2026-05-26 | Gui/DopeSheetView.cpp |
| T074 | FluxTimeline animated property rows + grouped keyframes v1 — add AE-like animated property rows under layers/effects/masks, backed by native Knob/Curve/Roto data; show only animated properties; toggle simple keyframe vs inline-curve previews; multidim knobs with one animated dim stay separate, two+ animated dims group by default without creating missing keys; hide adjustment disable keys; expose promoted Text1 knobs only; include aggregate Roto shape keys, not per-vertex animation. | DONE | opencode | 2026-05-24 | 2026-05-25 | tasks/T074-flux-timeline-keyframe-editor.md |
| T075 | Flux-owned OFX text boundary spike — groundwork/prototype evidence only; not accepted product work. Prior implementation/headless proof is invalid as completion evidence. See `tasks/FLUX_MOTION_TEXT_RECOVERY_SOURCE_OF_TRUTH.md`. | BLOCKED | opencode | 2026-05-25 | — | tasks/T075-flux-owned-ofx-text-boundary.md |
| T076 | openfx-flux deploy integration — groundwork/prototype evidence only; not accepted product work. Deploy/discovery proof did not prove canonical user GUI runtime discovery. See recovery source of truth. | BLOCKED | opencode | 2026-05-25 | — | tasks/T076-openfx-flux-deploy-integration.md |
| T077 | FluxMotionText nodegroup scaffold — rejected scaffold only; not accepted product work. It lacks required font UI, animator UI, visible animator stack, range selectors, and per-element animation. See recovery source of truth. | BLOCKED | opencode | 2026-05-25 | — | tasks/T077-flux-motion-text-nodegroup-scaffold.md |
| T078 | Real glyph rendering for Flux TextRender — renderer groundwork only; not accepted product work. Controlled/headless glyph proof does not prove FluxMotionText GUI workflow or animator behavior. See recovery source of truth. | BLOCKED | opencode | 2026-05-25 | — | tasks/T078-real-glyph-text-rendering.md |
| T079 | FluxMotionText layer integration — rejected/blocked. Real GUI path failed and the prototype lacks the requested AE-style text animator UX. See recovery source of truth before any further work. | BLOCKED | opencode | 2026-05-25 | — | tasks/T079-flux-motion-text-layer-integration.md |
| T080 | FluxMotionText full UI Text Animators — dynamic Add Animator workflow, visible animator stack, selector shapes, range controls, per-character/word/line renderer evaluation, playback/cache invalidation, direct Add Animator UX, and viewer zoom/proxy-stable TextRender output are manually accepted by Nick as working. Remaining polish is tracked separately in T081 and the recovery source of truth. | DONE | opencode | 2026-05-25 | 2026-05-26 | tasks/FLUX_MOTION_TEXT_RECOVERY_SOURCE_OF_TRUTH.md |
| T081 | Text Animator finishing touches — preserve target values when moving keyframes/changing selector shape; add selectable transform anchor dropdown; add sliders for Scale/Start/End/Offset/Strength; selecting animator timeline rows opens Animator panel and outlines selected animator. Build passed, Oracle-reviewed, and Nick manually validated all requested Natron GUI checks. | DONE | opencode | 2026-05-26 | 2026-05-26 | tasks/FLUX_MOTION_TEXT_RECOVERY_SOURCE_OF_TRUTH.md |
| T082 | Adobe Illustrator import with layered PDF-preview/vector path — research online and locally in Natron/OpenFX first, then import `.ai` files that contain PDF-compatible preview data either as a full-resolution vector-backed file or split into separate Illustrator layers as Flux layers/nodes when possible. Goal is AE-like logo/artwork import where text and shapes remain crisp at any scale/render resolution. Investigate whether the embedded PDF preview exposes layer structure, whether existing Natron/OpenFX/OIIO/PDF/SVG tooling already covers part of this, and whether Flux needs a dedicated vector render path for non-editable-but-resolution-independent AI/PDF content, with future editable vector conversion as a stretch goal. | PENDING | opencode | — | — | — |
| T083 | AI matte/mask generation and video depth tools — IN_PROGRESS planning source of truth created. Approved direction: CUDA-first external Python worker, AI panel plus nodegraph-usable nodes, viewer selection, project-relative generated raster media, secure no-plaintext-token model manager, SAM3.1 user-downloadable, MatAnyone2/DepthCrafter visible non-commercial warnings, RVM/XMem2 external helpers, and Force Save As for unsaved projects before AI generation. | IN_PROGRESS | opencode | 2026-05-26 | — | tasks/T083-ai-matte-depth.md |

---

## Cross-phase Infrastructure Tasks

| ID | Task | Status | Assigned | Started | Completed | File |
|---|---|---|---|---|---|---|
| T070 | Linux workstation installer/checker — Fedora-first path-agnostic `interactive full bootstrap action` helper derives `$FLUX_ROOT`, supports `$BUILD_DIR`/`$PLUGIN_PREFIX`/`$OFX_USER_PLUGIN_DIR`, updates submodules, verifies/installs deps with RPM Fusion conflict handling, configures/builds Flux, bootstraps embedded-Python deps, deploys PyPlugs/OFX bundles, writes user launcher with bundled OFX dependency paths, clears scoped OFX cache, stages transferable OFX extras, and validates OFX binaries with `ldd`. NVIDIA-backed Fedora distrobox fresh install/build/deploy/launch/cache/OFX validation passed. | DONE | forge | 2026-05-24 | 2026-05-26 | tasks/T070-linux-workstation-installer.md |
| T084 | Plugin payload discovery/deploy completeness — installer treats repo plugin payloads as source of truth by discovering top-level `plugins/*.py` and `.ofx.bundle` payloads from `plugins/` plus `plugins/ofx-extras/`, while keeping bundled/community PyPlug trees on `NATRON_PLUGIN_PATH`. Nick accepted moving this tracking task to done. | DONE | forge | 2026-05-26 | 2026-05-26 | tasks/T084-plugin-payload-discovery.md |
| T085 | Build warning audit and cleanup — reduced clean warning baseline from 908 to 32 warning lines, fixed high-risk Flux-owned warnings, scoped generated/third-party suppressions, and documented remaining third-party/generated/Python-compat warnings as accepted residual baseline. | DONE | forge | 2026-05-26 | 2026-05-26 | tasks/T085-build-warning-audit.md |
