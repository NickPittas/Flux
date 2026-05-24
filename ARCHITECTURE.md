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
| **Footage** | Video or image sequence layer | External Read node → FluxLayer gizmo (Input node) |
| **Shape** | Vector shape with fill/stroke | Roto node + Constant node |
| **Text** | Text layer with font/size/alignment | FluxText PyPlug: Text → FrameRange → TimeOffset → Grade → Output |
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

                                    Reformat (Flux Background) ──┐
Layer 3: "Title" (Text)       ->   FluxLayer gizmo #3 ──────────┤
                                                                  Merge3 ──→ Viewer
Layer 2: "Glow" (Solid)       ->   FluxSolid gizmo #2 ──────────┤
                                                                  Merge2
Layer 1: "BG" (Footage.mov)   ->   Read1 → FluxLayer gizmo #1 ──┘
```

Each **FluxLayer gizmo** contains: Input → FrameRange → TimeOffset → Transform → Multiply → Output.
Footage layers have an external **Read** node connected to the gizmo input. Solid layers use **FluxSolid** with an internal Constant source. Text layers use **FluxText** with an internal native Text source followed by FrameRange, TimeOffset, and Grade opacity.

- Each **layer** = one FluxLayer PyPlug gizmo node
- **Effects** on a layer = additional nodes inserted in the chain (future)
- **Layer order** = Merge nodes stacked bottom-to-top (outside gizmos)
- **Solo/Mute** = enable/disable nodes
- **Trim** = FrameRange knob on gizmo (trimStart/trimEnd state tracked in FluxLayer struct)
- **Move** = TimeOffset knob on gizmo (never touches FrameRange)
- **Transform** = translate, scale, rotate, center knobs on gizmo (aliased to internal Transform node)
- **Text layer controls** = promoted group knobs named after the internal Text node (`Text1...`) and linked with `setAsAlias()`, matching Natron's PyPlug exporter style. Text properties are shown first; the native Text `center` remains the Text node's own position/transform knob.
- **Text font selection** = promoted `Text1name` choice is synchronized to native `Text1font` in `Gui/Gui05.cpp` because the Text renderer reads the font-family string.
- **Viewer overlay keyframes** = `Gui/HostOverlay.cpp` passes a `KeyFrame` object for 2D overlay writes so animated translate/center/scale edits author curve keys instead of only changing the current value.
- **Precomps** = Natron Group nodes
- **External Read** = footage layers own a Read node outside the gizmo; metadata/range probing uses this node.
- **Background canvas** = persistent Reformat node labelled "Flux Background"; all its inputs are forcibly disconnected each rebuild so it remains a pure source.
- **Merge wiring** = input 0 is B/background (previous chain output), input 1 is A/foreground (this layer output). Inputs are disconnected before reconnecting.
- **Non-destructive rebuild** = existing Read/Gizmo/Merge nodes are reused. Rebuild only creates missing nodes, then reconnects and repositions.
- **Flux-managed creation** = `CreateNodeArgs` disables `AutoConnect`, `AddUndoRedoCommand`, and `SettingsOpened` to prevent Natron side effects.
- **Duplicate Layer** = native Natron clipboard copy/paste; footage copies Read+Gizmo+Merge, solids copy Gizmo+Merge.
- **Split Layer** = duplicate + trim. Original `outPoint` and duplicate `inPoint` are set to the playhead source frame.
- **Project Bin** = item double-click adds footage as a layer; empty-space double-click opens import.
- **Timeline duration** = synchronized to project frame range at startup and through `Project::frameRangeChanged`.

The compositing bridge lives in `Gui/Gui05.cpp` (`rebuildCompositingGraph`, `deferredInitGizmoParams`).
The FluxLayer PyPlug is in `plugins/FluxLayer.py` (installed to `~/.Natron/PyPlugs/`).

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
| **FluxMainWindow** | New layout: Project Bin + Viewport + Effects + Timeline | P2 | Done |
| **FluxTimeline** | Layer-based timeline widget with drag/trim/reorder | P2 | Done |
| **FluxLayer PyPlug** | Input→FrameRange→TimeOffset→Transform→Multiply→Output gizmo per footage layer | P2 | Done |
| **FluxProjectBin** | Thumbnail grid, drag-and-drop import | P2 | Done |
| **FluxEffectsPanel** | Per-layer effect stack UI | P2 | Done |
| **Flux Compositing Bridge** | Non-destructive Merge chain + viewer connection; external Read per footage layer; Background/Reformat anchor | P2 | Done |
| **Dark Theme** | Qt stylesheet, After Effects-inspired | P2 | Done |
| **Flux Menu System** | Composition-focused menus | P2 | Pending |
| **Playback Controls** | Play/pause/stop, fps display, keyboard shortcuts | P3 | Pending |
| **Layer Types** | Solid, adjustment, null layers | P3 | Pending |
| **Solo/Mute/Lock** | Per-layer visibility controls | P3 | Pending |
| **Split/Duplicate** | Duplicate via Natron clipboard; Split = duplicate + trim | P3 | Done |
| **Shape Layers** | Rect, ellipse, star, bezier | P7 | Planned |
| **Text Layers** | FluxText PyPlug with native Text controls, trim, opacity, viewer overlay animation | P7 | Done |
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
│   ├── Gui05.cpp                  <- NEW: setupFluxUi(), rebuildCompositingGraph(), deferredInitGizmoParams()
│   ├── FluxTimeline.h/cpp         <- NEW: Layer-based timeline widget
│   ├── FluxProjectBin.h/cpp       <- NEW: Project bin with drag-and-drop
│   ├── FluxEffectsPanel.h/cpp     <- NEW: Effects stack per layer
│   └── ...
├── plugins/                       <- NEW: Flux PyPlug gizmos
│   └── FluxLayer.py               <- NEW: Input→FrameRange→TimeOffset→Transform→Multiply→Output gizmo
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
│   ├── phase-2.md                 <- P2 plan (completed)
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
| **P2: UI Shell** | Flux app shell, dark theme, panels, timeline, gizmo | 2 days | DONE | Drag footage → timeline → trim/move → viewer |
| **P3: Timeline** | Playback controls, keyboard shortcuts, layer types | 2-3 weeks | NEXT | Full timeline interaction |
| **P4: Effects + Properties** | Effects stack, property inspector | 1-2 weeks | PENDING | Effects applied per-layer |
| **P5: Import/Export** | File browser, export dialog | 1 week | PENDING | All formats work end-to-end |
| **P6: Timeline Tree + Masks** | Timeline hierarchy and native Roto/RotoPaint masks | 1 day | DONE | Layer/effect masks render and persist |
| **P7: Shapes + Text** | Shape/text layer types | 2-3 weeks | IN_PROGRESS | Shapes and text render |
| **P8: Polish + Cache** | Improved cache, undo/redo, performance | 2-3 weeks | PENDING | 1080p 5-layer real-time target |

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
| Fit to View | F |
| Zoom In | Scroll Up |
| Zoom Out | Scroll Down |
| Pan | Alt+Drag / Middle Mouse Drag |
| Scroll Horizontal | Ctrl+Scroll / Horizontal Wheel / Trackpad |

---

## Timeline Trim/Move Model (CRITICAL — DO NOT DEVIATE)

This section defines how trim and move work. Every agent working on timeline code MUST follow these rules exactly.

### Data Model

| Field | What It Is | Changes On |
|---|---|---|
| `inPoint` | Current frameRange first value (source frame start) | Trim only |
| `outPoint` | Current frameRange last value (source frame end) | Trim only |
| `originalInPoint` | First frame of source media (Read node start). NEVER changes. | Never |
| `originalOutPoint` | Last frame of source media (Read node end). NEVER changes. | Never |
| `timeOffset` | How many frames the bar moved from its original position | Move only |

### Trim

Mouse moves delta frames (positive = right, negative = left).

- **Trim start:** `inPoint = inPoint + delta` → write to `frameRange` first knob. Done.
- **Trim end:** `outPoint = outPoint + delta` → write to `frameRange` last knob. Done.

- NEVER touch `timeOffset` during trim.
- NEVER touch `inPoint`/`outPoint` on move.
- `before`/`after` on FrameRange node = **black** (index 2).
- `frameRange` values CAN go negative.

### Move

Mouse moves delta frames (positive = right, negative = left).

- `timeOffset = timeOffset + delta` → write to `timeOffset` knob. Done.
- NEVER touch `frameRange`, `inPoint`, or `outPoint` during move.

### Bar Drawing (Two Steps)

1. **Draw everything** using `inPoint`, `outPoint`, `originalInPoint`, `originalOutPoint`. No `timeOffset` involved.
   - Active zone: `originalInPoint` to `originalOutPoint` (real source content)
   - Left desaturated zone: if `inPoint < originalInPoint`, desaturate from `inPoint` to `originalInPoint`
   - Right desaturated zone: if `outPoint > originalOutPoint`, desaturate from `originalOutPoint` to `outPoint`
   - Cut (hidden): if `inPoint > originalInPoint`, bar starts at `inPoint`. If `outPoint < originalOutPoint`, bar ends at `outPoint`.
2. **Shift the entire drawing** by `timeOffset` frames. Everything moves together.

### Forbidden

- NEVER use `timeOffset` to calculate `frameRange` values or vice versa.
- NEVER mix trim and move calculations.
- NEVER re-evaluate or recalculate frameRange from inPoint/outPoint after a move.
- NEVER change `originalInPoint` or `originalOutPoint` after initial creation.

---

## Rebuild Semantics (CRITICAL)

The compositing graph is reconciled on every structural timeline change (add, remove, reorder, duplicate, split). `rebuildCompositingGraph` follows these rules:

1. **Never destroy existing nodes during rebuild.** Read, Gizmo, and Merge nodes are created once per layer and reused.
2. **Reconnect and reposition only.** On every rebuild, managed inputs are disconnected then reconnected according to current layer order. Node positions are recalculated.
3. **Background Reformat is a pure source.** Its inputs are forcibly disconnected every rebuild. It anchors the project-format canvas at the top of the chain.
4. **Merge input mapping is fixed:** input 0 = B/background/previous chain output, input 1 = A/foreground/current layer output.
5. **Footage Read is external.** Read nodes feed FluxLayer input 0; metadata and frame-range probing reads from the external Read node.
6. **Disable Natron auto-connect for Flux-managed nodes.** Every `CreateNodeArgs` for Flux-managed nodes sets `AutoConnect=false`, `AddUndoRedoCommand=false`, and `SettingsOpened=false`.

---

## Notes for Development

- **Platform:** Linux-first (Fedora 44, Wayland). Cross-platform is a bonus.
- **Wayland:** Natron runs via xcb compat layer. Native Wayland support depends on Qt6 Wayland EGL fixing its GLES default (Natron uses desktop GLSL).
- **No .aep support:** Clean break from After Effects.
- **Qt dependency:** Accepted. Qt6 Core + Widgets + OpenGL + Wayland is required.
- **Testing:** Every feature validated with real-world artifacts (images, videos). No feature is "done" until tested.
- **Known Text follow-ups:** Native Text justification/alignment is currently a standalone Text OFX behavior issue, not a FluxText blocker. Dope Sheet/keyframe readability remains separate polish work.
- **Task tracking:** All tasks in `tasks/TASKS.md`. All phases in `plans/PHASES.md`.
- **Code review:** Every implementation reviewed before marking complete.
- **500-line max per file:** Carry this forward for new code.
- **OpenFX plugins:** Built separately from main Natron build. Installed to `/usr/OFX/Plugins/`.
- **Run command:** `QT_PLUGIN_PATH=/usr/lib64/qt6/plugins QT_QPA_PLATFORM=xcb /path/to/Natron`
