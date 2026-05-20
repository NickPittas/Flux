# Flux — Phase Breakdown

## Phase Status Overview

| Phase | Name | Status | Start | End | Tasks | Progress |
|---|---|---|---|---|---|---|
| P0 | Project Setup | DONE | 2026-05-20 | 2026-05-20 | 6/6 | 100% |
| P1 | Fork & Build | DONE | 2026-05-20 | 2026-05-20 | 9/12 | 100% |
| P2 | UI Shell | IN_PROGRESS | 2026-05-20 | — | 7/8 | 87% |
| P3 | Timeline | PENDING | — | — | 0/0 | 0% |
| P4 | Effects + Properties | PENDING | — | — | 0/0 | 0% |
| P5 | Import/Export | PENDING | — | — | 0/0 | 0% |
| P6 | Shapes + Text | PENDING | — | — | 0/0 | 0% |
| P7 | Polish + Cache | PENDING | — | — | 0/0 | 0% |

---

## P0: Project Setup (CURRENT)

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

**Dependencies**: P0 complete

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

**Documentation**:
- `plans/2026-05-20-engine-node-system-1.0.md` — Node, EffectInstance, AppManager, AppInstance, signals
- `plans/2026-05-20-engine-rendering-cache-1.0.md` — Render pipeline, cache, Image, Viewer, Timeline
- `plans/2026-05-20-engine-params-serialization-1.0.md` — Knobs, animation, serialization, settings, Python

**Exit Criteria**:
- [x] Natron builds from our fork on Linux
- [x] ~~Engine/ compiles as a shared library~~ (SKIPPED — keeping full app)
- [x] ~~Can render a frame headlessly~~ (Renderer/ works, proven)
- [x] We have a working development environment
- [x] Key Engine classes documented with Flux-specific notes
- [x] File import validated with real files
- [x] OCIO validated with real config
- [x] OpenFX plugins built and installed

---

## P2: UI Shell

**Goal**: Replace Natron's node-graph GUI with Flux's layer-based motion graphics UI. Keep the engine, build new panels.

**Started**: 2026-05-20

**Dependencies**: P1 complete

**Detailed Plan**: See `plans/phase-2.md`

**Results so far**:
- Dark theme applied (After Effects-inspired, using Natron's mainstyle.qss with Flux color palette)
- FluxMainWindow layout: Project Bin | Viewport | Effects+Properties (top), Timeline (bottom)
- FluxProjectBin: thumbnail grid/list view, drag-and-drop import, creates reader nodes
- FluxTimeline: custom-painted layer bars, playhead synced to viewer, clip drag/trim/reorder
- FluxEffectsPanel: effect stack per layer, enabled only when layer selected
- Layer-to-Node Bridge: auto-creates Merge chain for compositing, connects viewer to output
- Drag-and-drop from Project Bin to Timeline working
- Video thumbnails via ffmpeg subprocess

**Remaining**:
- T025: Flux Menu System
- T026: Integration Test

**Exit Criteria**:
- Flux application launches with a main window (replaces Natron GUI)
- Dark theme applied (After Effects-inspired)
- Dockable panel system working
- Viewport panel displays a rendered frame (from Natron engine)
- Timeline panel with layer rows (basic)
- Effects panel per layer
- Properties panel for selected effect/layer
- Basic menu bar with File/Edit/View/Help
- Project panel shows imported files

---

## P3: Timeline

**Goal**: Layer-based timeline widget that drives the Natron node graph.

**Estimated Duration**: 2-3 weeks

**Dependencies**: P2 complete

**Exit Criteria**:
- Timeline panel with layer rows
- Add/remove/reorder layers
- Layer types: footage, solid, adjustment, null
- Playhead, scrub, playback at correct FPS
- Layer-to-Node bridge functional (timeline ops create/manage Natron nodes)
- Solo/Mute/Lock per layer
- Trim layers (in/out points)

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

**Exit Criteria**:
- Import: mov, mp4, mxf via ReadFFmpeg
- Import: exr, tiff, png, jpg, psd via ReadOIIO
- Import: svg via ReadSVG
- File browser UI for importing
- Export: mov (ProRes, H264), mp4 via WriteFFmpeg
- Export: image sequences via WriteOIIO
- Export dialog with format/codec settings

---

## P6: Shapes + Text

**Goal**: Shape and text layer types for motion graphics.

**Estimated Duration**: 2-3 weeks

**Dependencies**: P5 complete

**Exit Criteria**:
- Shape layers: rect, ellipse, star, bezier paths
- Shape fill and stroke
- Text layers with font/size/alignment
- Per-character animation basics
- All shape/text properties animatable via keyframes

---

## P7: Polish + Cache

**Goal**: Improved cache, undo/redo, OCIO integration, performance optimization.

**Estimated Duration**: 2-3 weeks

**Dependencies**: P6 complete

**Exit Criteria**:
- Persistent disk cache
- Background rendering
- Full undo/redo
- OCIO config selection in settings
- Real-time preview at 1080p for 5+ layers with effects
- Performance profiling and optimization

---

## Notes

- Phase durations are estimates. Actual time depends on what we discover during each phase.
- Each phase plan file is created when the previous phase nears completion.
- Task files are created as tasks are identified during planning.
- All dates are ISO 8601 format (YYYY-MM-DD).
