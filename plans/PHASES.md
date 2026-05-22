# Flux — Phase Breakdown

## Phase Status Overview

| Phase | Name | Status | Start | End | Progress |
|---|---|---|---|---|---|
| P0 | Project Setup | DONE | 2026-05-20 | 2026-05-20 | 100% |
| P1 | Fork & Build | DONE | 2026-05-20 | 2026-05-20 | 100% |
| P2 | UI Shell | DONE | 2026-05-20 | 2026-05-21 | 100% |
| P3 | Timeline | IN_PROGRESS | 2026-05-21 | — | 30% |
| P4 | Effects + Properties | PENDING | — | — | 0% |
| P5 | Import/Export | PENDING | — | — | 0% |
| P6 | Shapes + Text | PENDING | — | — | 0% |
| P7 | Polish + Cache | PENDING | — | — | 0% |

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
- FluxTimeline: custom-painted layer bars, playhead synced to Natron's shared TimeLine, drag/trim/reorder
- FluxEffectsPanel: effect stack per layer, add effect button, active only when layer selected
- FluxLayer PyPlug gizmo (`net.sf.openfx.FluxLayer`): Read→FrameRange→TimeOffset→Transform→Output wrapped in one group node per layer, with stable parameter aliases (frameRange, timeOffset, translate, scale, rotate, center, motionBlur, shutter)
- Compositing graph: auto-creates Merge chain outside gizmos, connects final output to viewer
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

## P3: Timeline (IN PROGRESS)

**Goal**: Full-featured timeline with playback controls, layer types, solo/mute/lock, keyboard shortcuts.

**Started**: 2026-05-21

**Dependencies**: P2 complete

**Completed so far**:
- Playback controls — viewer drives shared TimeLine, FluxTimeline follows via onExternalFrameChanged
- Keyboard shortcuts — JKL play fwd/back/stop, arrows for frame stepping
- Layer types: solid (FluxSolid PyPlug with Constant), null (no gizmo), footage via drag-drop
- Context menu: Add Solid, Add Null, Delete Layer
- Transform overlay handles on gizmo via addTransformInteract
- Properties panel opens/closes on layer select/deselect
- Trim/move model rewritten: complete separation of frameRange (trim only) and timeOffset (move only)
- Desaturated bar zones for extended trim regions
- Bar clipped at left panel boundary

**Remaining**:
- Solo/Mute/Lock per layer (T035 — partially done, needs keyboard shortcut wiring)
- Split layer (Ctrl+Shift+D)
- Duplicate layer (Ctrl+D)
- Delete layer (Delete key)
- Zoom timeline horizontally (scroll wheel)
- Scroll timeline vertically
- Frame range from project settings
- Fit to view / frame selected layers

**Exit Criteria**:
- Playback controls (play/pause/stop, fps display)
- Layer types: footage, solid, adjustment, null
- Solo/Mute/Lock per layer
- Keyboard shortcuts (Space=play, PageDown/Up=frame step, etc.)
- Split layer (Ctrl+Shift+D)
- Duplicate layer (Ctrl+D)
- Delete layer (Delete key)
- Zoom timeline horizontally (scroll wheel)
- Scroll timeline vertically
- Layer bar context menu (delete, duplicate, split, properties)
- Playhead follow during playback
- Frame range from project settings

---

## P4: Effects + Properties

**Goal**: Effects stack panel per layer, properties panel for selected effect/layer.

**Estimated Duration**: 1-2 weeks

**Dependencies**: P3 complete

**Exit Criteria**:
- Effects stack panel shows effects per layer
- Add/remove/reorder effects in stack
- Properties panel displays selected effect's parameters
- Parameters are editable and update rendering
- Reuse Natron's Knob system for parameter UI
- OpenFX plugins appear in effects list

---

## P5: Import/Export

**Goal**: Full import/export pipeline using Natron's existing readers/writers.

**Estimated Duration**: 1 week

**Dependencies**: P4 complete

---

## P6: Shapes + Text

**Goal**: Shape and text layer types for motion graphics.

**Estimated Duration**: 2-3 weeks

**Dependencies**: P5 complete

---

## P7: Polish + Cache

**Goal**: Improved cache, undo/redo, OCIO integration, performance optimization.

**Estimated Duration**: 2-3 weeks

**Dependencies**: P6 complete

---

## Notes

- Phase durations are estimates. Actual time depends on what we discover during each phase.
- Each phase plan file is created when the previous phase nears completion.
- Task files are created as tasks are identified during planning.
- All dates are ISO 8601 format (YYYY-MM-DD).
