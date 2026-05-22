# Flux — Master Task List

Last updated: 2026-05-22

## Active Phase: P5 (Import/Export)

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
| T025 | Create Flux Menu System | PENDING | — | — | — | Gui/Gui.cpp |
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
