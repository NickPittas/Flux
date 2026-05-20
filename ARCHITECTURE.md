# Flux — 2D Motion Graphics Compositor for Linux

## Project Overview

**Flux** is a production-grade, 2D-only motion graphics and compositing application built for Linux. It is a fork of [Natron](https://github.com/NatronGitHub/Natron) (GPL2) with a redesigned UI focused on layer-based motion graphics (After Effects paradigm) while retaining Natron's node graph for power users.

**Owner:** Nick Pittas — VFX Supervisor, motion graphics company owner.
**Motivation:** No viable After Effects alternative exists on Linux. Natron is a compositor (Nuke-like), not a motion graphics tool. Flux bridges this gap by adding a timeline-driven layer system on top of Natron's proven engine.

**License:** GPL2 (inherited from Natron)

---

## Tech Stack (Validated)

| Layer | Technology | Version (Validated) | Status |
|---|---|---|---|
| **UI Framework** | Qt6 | 6.11.1 | Working (Wayland via xcb compat) |
| **Render Engine** | Natron Engine (C++17) | gui-sbk6 branch | Building, all targets compile |
| **GPU Pipeline** | OpenGL (via Natron ViewerGL) | 4.6 (NVIDIA RTX 4090) | Working |
| **Video I/O** | FFmpeg (via OpenFX-IO ReadFFmpeg) | FFmpeg 7.x | Validated with mov, mp4 |
| **Image I/O** | OpenImageIO (via OpenFX-IO ReadOIIO) | 3.1.12 | Validated with jpg, png |
| **Color Management** | OpenColorIO (integrated in Natron) | 2.4.2 | Validated with Nuke OCIO config |
| **Plugin System** | OpenFX 1.4 (hosted by Natron) | — | IO.ofx + Misc.ofx built and installed |
| **Animation** | Natron Curve/Keyframe system | — | Available |
| **Rotoscoping** | Natron RotoContext | — | Available |
| **Tracking** | Natron TrackerContext + openMVG | — | Available |
| **Serialization** | Natron XML project format (.ntp) | — | Working |
| **Build System** | CMake | 4.3 | Working |
| **Scripting** | Python 3 (via Natron's Python integration) | 3.14 | Working (PyConfig API fix applied) |
| **Compiler** | GCC | 16.1 | Working |
| **OS** | Fedora 44, KDE Plasma, Wayland | — | Working |

### Build Environment

- **OS**: Fedora 44, KDE Plasma, Wayland (not X11)
- **GPU**: NVIDIA RTX 4090, driver 595.71.05, OpenGL 4.6
- **Qt**: Qt6 6.11.1 (Qt5 not used — PySide2 unavailable for Python 3.14)
- **Python**: 3.14 (system default), PySide6 + Shiboken6 from Fedora repos
- **Natron base**: `gui-sbk6` branch (has Qt6 fixes, QRegExp→QRegularExpression, Shiboken6 adaptation)

### P1 Validation Results (2026-05-20)

| Capability | Status | Evidence |
|---|---|---|
| Natron builds from source | Validated | All targets: NatronEngine, NatronGui, Natron, NatronRenderer |
| GUI launches on Wayland | Validated | Via xcb compat + OpenGL 4.6 |
| Image import (jpg, png) | Validated | ReadOIIO via OpenFX-IO plugin |
| Video import (mov, mp4) | Validated | ReadFFmpeg via OpenFX-IO plugin |
| OCIO color management | Validated | Working with Nuke OCIO config from /opt/Nuke |
| OpenFX effects | Validated | Misc.ofx: Merge, Transform, ColorCorrect, Grade, Roto, Shuffle, etc. |
| Plugin installation | Validated | /usr/OFX/Plugins/ standard path |

---

## Architecture Decision Record

### ADR-001: Qt over Electron
- **Date**: 2026-05-20
- **Decision**: Use Qt for the UI instead of Electron + React + Rust backend
- **Rationale**: Natron Engine is deeply coupled to Qt (QObject, signals/slots, QThread, QMutex in 329 Engine files). Replacing Qt would be weeks of refactoring. Qt eliminates IPC latency for pixel data. No validation spikes needed.
- **Consequence**: Qt is a required dependency. UI is built with Qt widgets, not web technologies.

### ADR-002: Fork Natron
- **Date**: 2026-05-20
- **Decision**: Fork Natron (gui-sbk6 branch) under GPL2 rather than building from scratch
- **Rationale**: Natron provides years of proven engine code. Building from scratch would take months before rendering a single frame.
- **Consequence**: GPL2 license. Inherited codebase has C++98 heritage (now C++17).

### ADR-003: Layer-based timeline
- **Date**: 2026-05-20
- **Decision**: Primary UI is a layer-based timeline (After Effects paradigm). Node graph is secondary.
- **Rationale**: Motion graphics requires a timeline, not a node graph. Layers are UI abstractions over Natron nodes internally.

### ADR-004: gui-sbk6 branch over RB-2.6
- **Date**: 2026-05-20
- **Decision**: Base Flux on the `gui-sbk6` branch instead of RB-2.6 or RB-2.7
- **Rationale**: RB-2.6 lacks Qt6 support. RB-2.7 has partial Qt6 support but misses QRegExp→QRegularExpression substitution and other fixes. The `gui-sbk6` branch has complete Qt6 adaptation including Shiboken6 support.
- **Consequence**: We carry 3 extra patches on top of gui-sbk6 (Shiboken --clang-option, qhttpserver Q_PROPERTY fix, NodeGroup char16_t cast).

---

## Supported Import Formats

- **Video**: `.mov` (ProRes, H264, H265), `.mp4` (H264, H265), `.mxf`
- **Images**: `.exr` (multi-layer, 16/32-bit), `.tiff` (8/16/32-bit), `.png` (8/16-bit), `.jpeg`/`.jpg`
- **Layered**: `.psd` (Photoshop layers via OpenImageIO)
- **Vector**: `.svg` (via OpenFX-IO ReadSVG)
- **Documents**: `.pdf` (via OpenFX-IO ReadPDF)
- **Image sequences**: Any supported image format with sequential numbering

## Supported Export Formats

- **Video**: `.mov` (ProRes, H264, H265), `.mp4` (H264, H265), `.mxf`
- **Image sequences**: `.exr`, `.tiff`, `.png`
- **Export templates**: Saveable export configurations (JSON)

---

## Core Features

1. **Layer-based timeline** — Motion graphics timeline with drag-reorder, solo/mute/lock, trim, split
2. **Import footage** — All formats above, via Natron's existing reader plugins
3. **Canvas/Viewport** — Real-time GPU-rendered preview with pan, zoom, scrubbing
4. **Effects stack** — Per-layer effect chain (vertical stack UI, not node graph)
5. **Node graph** — Full Natron node graph accessible for power users
6. **Keyframe animation** — Per-property keyframes with interpolation (linear, bezier, hold)
7. **Graph editor** — Bezier curve editor for animation timing
8. **OpenFX plugins** — Full OFX 1.4 plugin support (existing ecosystem)
9. **OCIO color management** — Input/display/output transforms, configurable configs
10. **Export** — Video and image sequence export with configurable settings
11. **Rotoscoping** — Bezier shapes, feathering, motion blur
12. **Tracking** — Point and planar tracking
13. **Python scripting** — Expressions, automation, plugin development
14. **Shape layers** — Rect, ellipse, star, bezier paths
15. **Text layers** — Text rendering with per-character animation
16. **Dockable panels** — Drag, resize, detach panels

---

## Layer Types

| Type | Description | Engine Mapping |
|---|---|---|
| **Footage** | Video or image sequence layer | ReadFFmpeg / ReadOIIO node |
| **Shape** | Vector shape with fill/stroke | Roto node + Constant node |
| **Text** | Text layer with font/size/alignment | Text rendering node (new) |
| **Solid** | Solid color fill | Constant node |
| **Adjustment** | Applies effects to all layers below | Effects chain passthrough |
| **Null** | Invisible transform container | Transform node (no source) |
| **Precomp** | Nested composition | Group node |

### Layer Properties (all animatable)
- Position (x, y)
- Scale (x, y)
- Rotation (degrees)
- Anchor Point (x, y)
- Opacity (0-100%)
- Blend Mode
- Masks (multiple per layer)
- Effects (ordered stack)

---

## How Layers Map to Nodes

After Effects is internally node-based. Flux does the same thing:

```
User sees (Timeline):              Engine creates (Node Graph):

Layer 3: "Title" (Text)       ->   Text -> Transform -> Merge(over Layer 2)
Layer 2: "Glow" (Adjustment)  ->   Blur -> Glow (applied to Layer 1 output)
Layer 1: "BG" (Footage.mov)   ->   ReadFFmpeg
```

- Each **layer** = a chain of Natron nodes
- **Effects** on a layer = additional nodes inserted in the chain
- **Layer order** = Merge nodes stacked bottom-to-top
- **Solo/Mute** = enable/disable nodes
- **Trim** = time range parameters on reader nodes
- **Precomps** = Natron Group nodes

The Layer-to-Node bridge (`Engine/FluxLayerBridge`) is the core new code in Flux. It translates timeline operations into Natron node graph operations.

---

## What Comes from Natron (Unchanged)

| Capability | Natron Component | Status |
|---|---|---|
| 32-bit float rendering pipeline | Engine/ | Battle-tested |
| OCIO color management | Engine/ + Settings | Validated |
| Image I/O (EXR, TIFF, PSD, etc.) | OpenFX-IO (ReadOIIO) | Validated |
| Video I/O (mov, mp4, mxf) | OpenFX-IO (ReadFFmpeg) | Validated |
| OpenFX plugin host | Engine/ + HostSupport/ | Validated |
| RAM + Disk cache | Engine/Cache | Available |
| Keyframe animation | Engine/Curve, Engine/Knob | Available |
| Rotoscoping | Engine/RotoContext | Available |
| Tracking | Engine/TrackerContext | Available |
| Python scripting | Engine/ Python integration | Working |
| Multi-threaded rendering | Engine/ ThreadPool | Available |
| Headless rendering | Renderer/ | Available |
| Crash reporting | BreakpadClient/ | Available |

---

## What Flux Builds New

| Component | Description | Phase | Status |
|---|---|---|---|
| **FluxMainWindow** | New layout: Viewport + Right Panel + Timeline | P2 | Planned |
| **FluxTimeline** | Layer-based timeline widget | P2/P3 | Planned |
| **FluxLayerBridge** | Layer-to-Node translation engine | P2/P3 | Planned |
| **FluxEffectsPanel** | Per-layer effect stack UI | P2 | Planned |
| **FluxProjectPanel** | Project file tree | P2 | Planned |
| **Dark Theme** | Qt stylesheet, After Effects-inspired | P2 | Planned |
| **Flux Menu System** | Composition-focused menus | P2 | Planned |
| **Shape Layers** | Rect, ellipse, star, bezier | P6 | Planned |
| **Text Layers** | Text rendering with animation | P6 | Planned |
| **Export Templates** | Saveable export configurations | P5 | Planned |
| **Improved Cache** | Persistent disk cache, background rendering | P7 | Planned |

### Natron GUI Components We Keep

| Component | Reason |
|---|---|
| `TabWidget` | Dockable panel container — works well |
| `Splitter` | Panel resizing — works well |
| `ViewerGL` (QOpenGLWidget) | OpenGL frame rendering — works well |
| `ViewerTab` | Viewer panel controls — restyle only |
| `DockablePanel` | Knob parameter UI — works well |
| `CurveEditor` | Keyframe curve editing — works well |
| `DopeSheet` | Timeline keyframe view — works well |
| `NodeGraph` (QGraphicsView) | Hidden tab for power users |

---

## Repository Structure

```
flux/                              <- Forked from NatronGitHub/Natron (gui-sbk6 branch)
├── Engine/                        <- KEEP: Core rendering engine (329 files)
├── Global/                        <- KEEP: Shared utilities (32 files)
├── HostSupport/                   <- KEEP: OpenFX host (3 files)
├── libs/                          <- KEEP: Eigen3, OpenFX, ceres, openMVG, etc.
├── Renderer/                      <- KEEP: Headless render entry point (3 files)
├── BreakpadClient/                <- KEEP: Crash reporting
├── CrashReporter/                 <- KEEP: Crash reporting
├── Tests/                         <- KEEP: Existing test suite
├── Documentation/                 <- KEEP: Natron docs (reference)
├── PythonBin/                     <- KEEP: Python scripting support
├── Gui/                           <- EXTEND: Flux UI added, Natron widgets kept
│   ├── (existing Natron GUI)      <- KEPT: TabWidget, ViewerGL, DockablePanel, etc.
│   ├── FluxTimeline.h/cpp         <- NEW: Layer-based timeline
│   ├── FluxTimeRuler.h/cpp        <- NEW: Time ruler with playhead
│   ├── FluxLayerRow.h/cpp         <- NEW: Single layer row
│   ├── FluxPlaybackControls.h/cpp <- NEW: Play/pause/stop controls
│   ├── FluxEffectsPanel.h/cpp     <- NEW: Effects stack per layer
│   ├── FluxEffectPickerDialog.h/cpp <- NEW: Effect browser
│   └── FluxProjectPanel.h/cpp     <- NEW: Project file tree
├── Engine/
│   ├── (existing Natron Engine)   <- KEPT: All rendering, caching, I/O
│   ├── FluxLayerBridge.h/cpp      <- NEW: Layer-to-Node translation
│   └── FluxLayer.h/cpp            <- NEW: Layer data model
├── App/                           <- EXTEND: Flux mode flag in main window
├── Shiboken/                      <- KEEP: Python bindings generator
├── Resources/
│   └── themes/
│       └── flux-dark.qss          <- NEW: Dark theme stylesheet
├── openfx-io/                     <- EXTERNAL: Built separately, installed to /usr/OFX/Plugins/
├── openfx-misc/                   <- EXTERNAL: Built separately, installed to /usr/OFX/Plugins/
│
├── ARCHITECTURE.md                <- This file
├── AGENTS.md                      <- Project-specific agent guidelines
├── plans/                         <- Phase plans and status
│   ├── PHASES.md                  <- Phase overview
│   ├── phase-1.md                 <- P1 plan (completed)
│   ├── phase-2.md                 <- P2 plan (current)
│   ├── 2026-05-20-engine-node-system-1.0.md      <- Engine API: nodes, signals
│   ├── 2026-05-20-engine-rendering-cache-1.0.md  <- Engine API: rendering, cache
│   └── 2026-05-20-engine-params-serialization-1.0.md <- Engine API: knobs, serialization
├── tasks/
│   └── TASKS.md                   <- Master task list
└── CMakeLists.txt                 <- Modified Natron root CMake
```

---

## Build Phases

| Phase | Deliverable | Est. Duration | Status | Key Validation |
|---|---|---|---|---|
| **P0: Setup** | Project infrastructure, plans, tasks | 1 day | DONE | All tracking files created |
| **P1: Fork & Build** | Natron builds, engine validated | 1 day | DONE | Real images/videos imported, OCIO tested |
| **P2: UI Shell** | Flux app shell, dark theme, panels, timeline | 2-3 weeks | IN_PROGRESS | Flux layout with layer timeline |
| **P3: Timeline** | Layer-to-Node bridge, playback | 2-3 weeks | PENDING | Layers drive node graph |
| **P4: Effects + Properties** | Effects stack, property inspector | 1-2 weeks | PENDING | Effects applied per-layer |
| **P5: Import/Export** | File browser, export dialog | 1 week | PENDING | All formats work end-to-end |
| **P6: Shapes + Text** | Shape/text layer types | 2-3 weeks | PENDING | Shapes and text render |
| **P7: Polish + Cache** | Improved cache, undo/redo, performance | 2-3 weeks | PENDING | 1080p 5-layer real-time target |

### P2 Tasks (Current)

| ID | Task | Est. Time |
|---|---|---|
| T023 | Dark Theme (flux-dark.qss) | 0.5 day |
| T019 | FluxMainWindow (new layout) | 1-2 days |
| T025 | Flux Menu System | 0.5 day |
| T020 | FluxTimeline widget | 2-3 days |
| T021 | Layer-to-Node Bridge | 2-3 days |
| T022 | Effects Stack Panel | 1-2 days |
| T024 | Project Panel | 1 day |
| T026 | Integration Test | 1 day |

---

## Flux UI Layout (Target)

```
┌─────────────────────────────────────────────────────────────────────┐
│ Menu: File | Edit | Composition | Layer | Effects | View           │
├────────────────┬────────────────────────┬──────────────────────────┤
│                │                        │  Effects Stack           │
│  Project Bin   │                        │  ┌────────────────────┐  │
│  ┌──────────┐  │     Viewport           │  │ Transform          │  │
│  │ bg.mov   │  │     (ViewerGL)         │  │ Blur               │  │
│  │ logo.png │  │                        │  │ Color Correct      │  │
│  │ title.svg│  │                        │  │ + Add Effect       │  │
│  │          │  │                        │  └────────────────────┘  │
│  │ Import ▼ │  │                        ├──────────────────────────┤
│  └──────────┘  │                        │  Properties              │
│                │                        │  (DockablePanel for      │
│                │                        │   selected effect/layer) │
├────────────────┴────────────────────────┴──────────────────────────┤
│  Timeline                                                          │
│  ┌────────────────────────────────────────────────────────────────┐ │
│  │ TimeRuler: |  0   5   10  15  20  25  30  35  40              │ │
│  ├──────────┬─────────────────────────────────────────────────────┤ │
│  │ Controls │ Layer 3: Text "Title"    ████░░░░░░░░░░             │ │
│  │ ▶ ⏸ ⏹   │ Layer 2: Blur           ████████████░░             │ │
│  │ 24fps    │ Layer 1: Footage.mov     ████████████████           │ │
│  └──────────┴─────────────────────────────────────────────────────┤ │
│  └────────────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────────┘
```

**Top row (left to right)**: Project Bin (imported footage/assets) | Viewport (rendered preview) | Effects Stack + Properties (per-layer controls)

**Bottom row**: Timeline (layer-based, not node-graph-based)

---

## Performance Target

Real-time preview at **1080p for 5+ layers with effects** at 24fps on modern Linux hardware.

Natron already achieves this for compositing workloads. Flux adds minimal overhead (the Layer-to-Node bridge is just node graph manipulation, not rendering).

---

## Keyboard Shortcuts (After Effects Parity)

| Action | Shortcut |
|---|---|
| Play/Pause | Space |
| Next Frame | Page Down |
| Previous Frame | Page Up |
| Go to Start | Home |
| Go to End | End |
| Split Layer | Ctrl+Shift+D |
| Duplicate | Ctrl+D |
| Delete | Delete |
| Undo | Ctrl+Z |
| Redo | Ctrl+Shift+Z |
| RAM Preview | Numpad 0 |
| Fit to View | Shift+/ |
| Zoom In | = |
| Zoom Out | - |
| Pan | Middle Mouse Drag |

---

## Notes for Development

- **Platform:** Linux-first (Fedora 44, Wayland). Cross-platform is a bonus.
- **Wayland:** Natron runs via xcb compat layer. Native Wayland support depends on Qt6 Wayland EGL fixing its GLES default (Natron uses desktop GLSL).
- **No .aep support:** Clean break from After Effects.
- **Qt dependency:** Accepted. Qt6 Core + Widgets + OpenGL + Wayland is required.
- **Testing:** Every feature validated with real-world artifacts (images, videos). No feature is "done" until tested.
- **Task tracking:** All tasks in `tasks/TASKS.md`. All phases in `plans/PHASES.md`.
- **Code review:** Every implementation reviewed before marking complete.
- **500-line max per file:** Carry this forward for new code.
- **OpenFX plugins:** Built separately from main Natron build. Installed to `/usr/OFX/Plugins/`.
- **Run command:** `QT_PLUGIN_PATH=/usr/lib64/qt6/plugins QT_QPA_PLATFORM=xcb /path/to/Natron`
