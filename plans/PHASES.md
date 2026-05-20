# Flux — Phase Breakdown

## Phase Status Overview

| Phase | Name | Status | Start | End | Tasks | Progress |
|---|---|---|---|---|---|---|
| P0 | Project Setup | IN_PROGRESS | 2026-05-20 | — | 5/6 | 83% |
| P1 | Fork & Build | PENDING | — | — | 0/0 | 0% |
| P2 | UI Shell | PENDING | — | — | 0/0 | 0% |
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
- [ ] Git repo initialized with Natron fork

**Exit Criteria**: All infrastructure files created, ARCHITECTURE.md updated, ready to begin P1.

---

## P1: Fork & Build

**Goal**: Fork Natron RB-2.6, build it on Linux, verify Engine/ works standalone, understand the codebase.

**Estimated Duration**: 3-5 days

**Dependencies**: P0 complete

**Detailed Plan**: See `plans/phase-1.md`

**Exit Criteria**:
- Natron builds from our fork on Linux
- Engine/ compiles as a shared library
- Can render a frame headlessly (via Renderer/ process)
- We have a working development environment
- Key Engine classes documented with Flux-specific notes

---

## P2: UI Shell

**Goal**: New Qt application shell with dockable panels, dark theme, viewport displaying rendered frames.

**Estimated Duration**: 1-2 weeks

**Dependencies**: P1 complete

**Detailed Plan**: To be created when P1 nears completion

**Exit Criteria**:
- Flux application launches with a main window
- Dark theme applied
- Dockable panel system working
- Viewport panel displays a rendered frame (from Natron engine)
- Project panel shows file tree
- Basic menu bar with File/Edit/View/Help

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
