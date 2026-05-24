# Flux — Phase Breakdown

## Phase Status Overview

| Phase | Name | Status | Start | End | Progress |
|---|---|---|---|---|---|
| P0 | Project Setup | DONE | 2026-05-20 | 2026-05-20 | 100% |
| P1 | Fork & Build | DONE | 2026-05-20 | 2026-05-20 | 100% |
| P2 | UI Shell | DONE | 2026-05-20 | 2026-05-21 | 100% |
| P3 | Timeline | DONE | 2026-05-21 | 2026-05-22 | 100% |
| P4 | Effects + Properties | DONE | 2026-05-22 | 2026-05-22 | 100% |
| P5 | Import/Export | DONE | 2026-05-22 | 2026-05-23 | 100% |
| P6 | Timeline Tree + Masks | DONE | 2026-05-23 | 2026-05-23 | 100% |
| P7 | Shapes + Text | IN_PROGRESS | 2026-05-24 | — | 10% |
| P8 | Polish + Cache | PENDING | — | — | 0% |

---

## P0: Project Setup

**Goal**: Establish project infrastructure, plans, task tracking, and workspace conventions.

**Deliverables**:
- [x] AGENTS.md with project-specific guidelines
- [x] plans/PHASES.md with phase breakdown
- [x] tasks/TASKS.md with task tracking
- [x] plans/phase-1.md with detailed P1 plan
- [x] ARCHITECTURE.md updated to reflect Qt+Natron fork approach
- [x] Git repo initialized with Natron fork

**Exit Criteria**: All infrastructure files created, ARCHITECTURE.md updated, ready to begin P1.

---

## P1: Fork & Build

**Goal**: Fork Natron, build it on Linux with Qt6, validate the full pipeline (import, OCIO, rendering), document the Engine API.

**Completed**: 2026-05-20

**Actual Duration**: 1 day

**Results**:
- Forked Natron RB-2.6 → rebased onto `gui-sbk6` branch for Qt6 support
- Built with Qt6 6.11.1, GCC 16.1, Python 3.14, Fedora 44
- Fixed: Python 3.14 PyConfig API, Shiboken6 compat, qhttpserver Qt6 compat, NodeGroup char16_t
- OpenFX-IO plugin built with OIIO 3.1.12, FFmpeg, OCIO 2.4.2, OpenEXR 3.2.4
- OpenFX-Misc plugin built (Merge, Transform, ColorCorrect, etc.)
- GUI launches on Wayland (via xcb + OpenGL 4.6 / RTX 4090)
- File import validated: jpg, png, mov, mp4 via ReadOIIO/ReadFFmpeg
- OCIO validated: working with Nuke OCIO config
- Engine API documented: node system, rendering/cache, params/serialization

---

## P2: UI Shell

**Goal**: Replace Natron's node-graph GUI with Flux's layer-based motion graphics UI.

**Started**: 2026-05-20 | **Completed**: 2026-05-21

**Actual Duration**: 2 days

**Results**:
- Dark theme applied (After Effects-inspired, Natron's mainstyle.qss with Flux color palette)
- FluxMainWindow layout: Project Bin (20%) | Viewport (50%) | Effects+Properties (30%) top, Timeline (30%) bottom
- FluxProjectBin: thumbnail grid view, drag-and-drop import, file import to reader nodes
- Flux Menu System: File/Edit/Layer/Composition/View/Window/Help menu bar for Flux mode, with NodeGraph edit actions, timeline layer actions, and pane focus actions; Natron menu path preserved
- FluxTimeline: custom-painted layer bars, playhead synced to Natron's shared TimeLine, drag/trim/reorder
- FluxEffectsPanel: effect stack per layer, add effect button, active only when layer selected
- FluxLayer PyPlug gizmo (`net.sf.openfx.FluxLayer`): Input→FrameRange→TimeOffset→Transform→Multiply→Output wrapped in one group node per footage layer, with stable parameter aliases (frameRange, timeOffset, translate, scale, rotate, center, motionBlur, shutter)
- Compositing graph: non-destructive Merge chain outside gizmos, external Read per footage layer, Flux Background/Reformat anchor, final output to viewer
- Drag-and-drop from Project Bin to Timeline: creates layer, gizmo, deferred file probe + parameter init
- Trim left/right: updates FrameRange knob via explicit trimStart/trimEnd state
- Move: updates TimeOffset knob only (never touches FrameRange)
- Deferred init with retry: 200ms probe, 300ms retry if file range not yet available, nodeInitialized guard prevents overwrite
- Gizmo preservation: rebuildCompositingGraph skips layers with existing gizmoNode
- Node graph layout: staggered gizmo + merge node positioning

**Key Files**:
- `Gui/Gui05.cpp` — setupFluxUi(), rebuildCompositingGraph(), deferredInitGizmoParams()
- `Gui/FluxTimeline.{h,cpp}` — FluxLayer struct, timeline widget, interactions
- `Gui/FluxProjectBin.{h,cpp}` — project bin with drag export
- `Gui/FluxEffectsPanel.{h,cpp}` — effects stack UI
- `plugins/FluxLayer.py` — PyPlug gizmo (installed to ~/.Natron/PyPlugs/)

---

## P3: Timeline (COMPLETE)

**Goal**: Full-featured timeline with playback controls, layer types, solo/mute/lock, keyboard shortcuts.

**Started**: 2026-05-21

**Completed**: 2026-05-22

**Dependencies**: P2 complete

**Completed so far**:
- Playback controls — viewer drives shared TimeLine, FluxTimeline follows via onExternalFrameChanged
- Keyboard shortcuts — JKL play fwd/back/stop, arrows for frame stepping
- Layer types: solid (FluxSolid PyPlug with Constant), null (no gizmo), footage via drag-drop
- Context menu: Add Solid, Add Null, Delete Layer, Duplicate Layer, Split Layer
- Transform overlay handles on gizmo via addTransformInteract
- Properties panel opens/closes on layer select/deselect
- Trim/move model rewritten: complete separation of frameRange (trim only) and timeOffset (move only)
- Desaturated bar zones for extended trim regions
- Bar clipped at left panel boundary
- Duplicate Layer: Ctrl+D keyboard shortcut + context menu; native Natron clipboard copy/paste, Read+Gizmo+Merge for footage, Gizmo+Merge for solids
- Split Layer: duplicate + trim original outPoint / duplicate inPoint at playhead
- Delete Layer: deactivates Read/Gizmo/Merge and rebuild reconnects the remaining chain
- Solo/Mute/Lock per layer: L button toggle in layer header; locked layers block trim, move, reorder, delete, split, and duplicate
- Background/Reformat anchor: persistent Reformat node at top of chain, inputs forcibly disconnected each rebuild
- Non-destructive rebuild: existing Read/Gizmo/Merge nodes reused; only missing nodes created, reconnect + reposition only
- External Read node: footage layers have Read outside gizmo; gizmo uses internal Input node
- Flux-managed creation: AutoConnect=false, AddUndoRedoCommand=false, SettingsOpened=false
- Merge input mapping: 0=B/background, 1=A/foreground (documented and enforced)
- Project Bin: empty-space double-click opens import; item double-click adds footage as layer
- Timeline duration synced to project frame range via Project::frameRangeChanged signal
- Context menu complete: Add Solid/Null, Delete, Duplicate, Split, Add Effect, Reset In/Out Points
- Zoom/scroll/pan: plain wheel zoom, Ctrl+wheel horizontal scroll, horizontal wheel/trackpad horizontal scroll, Alt+drag/middle-drag pan, F fit-to-view
- Resizable layer header panel: label/name column drag-resizable, fixed L/V/S control column stays pinned

**Signoff**:
- Live manual approval on 2026-05-22: plain wheel zoom, Ctrl+wheel horizontal scroll, Alt-drag pan, F fit-to-view, label resize, Add Effect, and Reset In/Out all verified.

**Exit Criteria**:
- Playback controls (play/pause/stop, fps display)
- Layer types: footage, solid, adjustment, null
- Solo/Mute/Lock per layer — DONE
- Keyboard shortcuts (Space=play, PageDown/Up=frame step, etc.)
- Split layer (Ctrl+Shift+D) — DONE
- Duplicate layer (Ctrl+D) — DONE
- Delete layer (Delete key) — DONE
- Zoom timeline horizontally (scroll wheel) — DONE
- Horizontal scroll via Ctrl+wheel / horizontal wheel / trackpad — DONE
- Layer bar context menu (delete, duplicate, split, properties) — DONE
- Playhead follow during playback — DONE
- Frame range from project settings — DONE

---

## P4: Effects + Properties (COMPLETE)

**Goal**: Effects stack panel per layer, properties panel for selected effect/layer.

**Started**: 2026-05-22 | **Completed**: 2026-05-22

**Dependencies**: P3 complete

**Results**:
- T043–T047: Effect insertion, wiring, adjustment rows, effects panel (all live-approved)
- T048: Save/reopen persistence via Boost XML serialization embedded in ProjectGuiSerialization
- T049: Duplicate effect-bearing layers and adjustment rows via Natron clipboard copy/paste
- T050: Split effect-bearing layers (clipboard copies effects, then trims both halves)
- T051: Adjustment row trim/split/move via disable-knob keyframes on all effects simultaneously

**T051 adjustment row trim semantics** (critical for future reference):
- Adjustment rows have no FrameRange knob — effects have no built-in time range.
- The disable knob's boolean animation IS the time range.
- On trim: `setAnimationEnabled(true)` on each effect's disable knob, clear old keyframes, then set:
  - Disabled at `startFrame - 1`
  - Enabled at `startFrame`
  - Disabled at `endFrame + 1`
- On move: same keyframes but shifted by new `timeOffset`
- On split: duplicate row, trim both halves, update keyframes on each
- On reset: clear all keyframes, set disable=false
- ALL effects in the row must be keyframed simultaneously on every change

**Exit Criteria**:
- Effects stack panel shows effects per layer
- Add/remove/reorder effects in stack
- Properties panel displays selected effect's parameters
- Parameters are editable and update rendering
- Reuse Natron's Knob system for parameter UI
- OpenFX plugins appear in effects list

---

## P5: Import/Export (COMPLETE)

**Goal**: Expose Read node settings per footage layer, add export/render panel.

**Dependencies**: P4 complete

**Started**: 2026-05-22
**Completed**: 2026-05-23

**Tasks**:
- T052: ✅ Right-click "Open Read Node" on footage layers
- T053: ✅ Flux Export/Render panel — reparents Write/Reformat NodeSettingsPanel with all knobs exposed (encoder options, codec, FPS, bitrate, OCIO, frame range, etc.), browse output path, Render button using Natron's standard render pipeline

---

## P6: Timeline Tree + Masks (COMPLETE)

**Goal**: Replace the separate effects-list workflow with an expandable timeline tree, then add layer/effect masks using Natron-native Roto/RotoPaint branches while preserving manual nodegraph edits.

**Estimated Duration**: 2-3 weeks

**Dependencies**: P5 complete

**Started**: 2026-05-23

**Completed**: 2026-05-23

**UX Decision**:
- The timeline becomes the hierarchy for layers, effects, masks, and later keyframes.
- Selecting a layer row opens only the layer/gizmo properties.
- Selecting an effect row opens only that effect's properties.
- Selecting a mask row opens the Roto/RotoPaint properties and activates viewer tools.
- The old FluxEffectsPanel is phased out once effect rows work in the timeline.
- Precomp/manual branch nodes are preserved but not shown as layer children; a precomp icon appears on the layer row.

**Graph Rules**:
- Layer mask: `Read/Solid → Gizmo → Effects → [Unpremult] → Roto → Premult → Merge A`.
- Effect mask: `Reformat → Roto` side branch into the effect mask input.
- Effect mask branches do **not** count as precomp branches.
- Manual non-mask branches merging into the layer's main pipe count as precomp and must be preserved.

**Tasks**:
- T054: ✅ Timeline visible-row model — add `FluxVisibleRow`, expanded layer state, `yToRow()`, variable row heights; behavior initially identical to flat layer rows. Oracle-reviewed, build passed.
- T055: ✅ Timeline effect sub-rows — expanded layer shows main-pipe effects as indented children; selecting effect opens only that effect properties. Oracle-reviewed, build passed.
- T056: ✅ Move effect actions into timeline — add/remove/reorder effects from timeline context menus; start retiring FluxEffectsPanel. Oracle-reviewed, build passed.
- T057: ✅ FluxMask data model + serialization — `FluxMask`, `layer.masks`, `maskApplyNode`, `hasPrecompBranch`, project save/reopen. Oracle-reviewed, build passed.
- T058: ✅ Mask/branch discovery utilities — `discoverMaskInput(NodePtr)`, `isPremultNode(NodePtr)`, upstream branch classifier. Oracle-reviewed, build passed.
- T059: ✅ Timeline mask sub-rows and mask model actions — layer/effect mask model entries can be added from timeline context menus and appear as child rows; selecting a mask opens Roto/RotoPaint properties only when backing nodes already exist. No graph node creation or wiring yet. Oracle-reviewed, build passed.
- T060: ✅ Layer mask graph — create/connect mask apply in the layer main pipe with Reformat→Roto source; handle terminal Premult rule. Oracle-reviewed, build passed.
- T061: ✅ Effect mask graph — create/connect Reformat→Roto side branch into effect mask input. Oracle-reviewed, build passed.
- T062: ✅ Preserve manual layer branches — classify main pipe vs mask branches vs precomp branches; never destroy user-added nodes. Oracle-reviewed, build passed.
- T062A: ✅ P6 scrutinize blocker fixes — serialization versioning, temporary duplicate/split guards for masked/precomp layers, classifier source/path fixes, derived precomp-state reset, runtime artifact cleanup. Oracle-reviewed, build passed.
- T063: ✅ Duplicate/split full branch — branch-aware copy/paste for non-adjustment layers copies main pipe, mask branches, and precomp branches; restores FluxMask/FluxEffect references by old script name. Adjustment rows with masks remain disabled. Scrutinize-reviewed, build passed.
- T064: ✅ Correct layer-mask graph — removed unapproved Merge(in) layer-mask implementation; layer masks are now inline `source → [Unpremult] → Roto → Premult → Merge A`, with narrow old-artifact migration and stale-chain cleanup. Scrutinize-reviewed, build passed.
- T065: ✅ Roto/RotoPaint replace selected channels — added native `Zero selected input channels` checkbox, hidden internal `RotoReplaceChannels` preprocessing node, empty-Roto zeroing path, save/reopen crash fix, GL context fix for Shadertoy, and user CImg OFX discovery. Oracle-reviewed, build passed, CImg cache verified.

---

## P7: Shapes + Text

**Goal**: Shape and text layer types for motion graphics.

**Estimated Duration**: 2-3 weeks

**Dependencies**: P6 complete

**Started**: 2026-05-24

**Prerequisite restoration work**:
- T069: ✅ Restore missing OFX provider coverage for bundled PyPlugs before implementing text/shape features that may depend on legacy native OpenFX providers. SeExpr/Text/Tile/Magick/ResolveMath providers are installed and validated; dependency audit reports 0 missing IDs; `lp_roughenEdges`, `lp_SimpleKeyer`, `Luma_to_Normals`, and `Vectors_Normalize` creation passes. Missing-plugin/library diagnostics Oracle-reviewed and validated with cold/warm cache broken-binary tests.

**Tasks**:
- T071: 🧪 Text layer v1 — FluxText PyPlug and UI actions implemented. Current architecture uses native Text OFX knobs directly: no extra Transform or FrameRange node; live Text provider knobs are exposed as required aliases, Flux `frameRange` aliases to Text `frameRange`, trim/split/reset sync Text/host `enableNodeLifeTime` + `nodeLifeTime`, and C++ no longer sets Text center. Build passed and full alias smoke reached `FLUX_TEXT_ALL_ALIAS_SMOKE_OK`; still needs live GUI validation for create/edit/animate/trim/duplicate/split/save-reopen/export before DONE.

---

## P8: Polish + Cache

**Goal**: Improved cache, undo/redo, OCIO integration, performance optimization.

**Estimated Duration**: 2-3 weeks

**Dependencies**: P7 complete

---

## Notes

- Phase durations are estimates. Actual time depends on what we discover during each phase.
- Each phase plan file is created when the previous phase nears completion.
- Task files are created as tasks are identified during planning.
- All dates are ISO 8601 format (YYYY-MM-DD).
