# Flux — 2D Motion Graphics Compositor for Linux

## Project Overview

**Flux** is a production-grade, 2D-only motion graphics and compositing application built for Linux. It is a fork of [Natron](https://github.com/NatronGitHub/Natron) (GPL2) with a redesigned UI focused on layer-based motion graphics (After Effects paradigm) while retaining Natron's node graph for power users.

**Owner:** Nick Pittas — VFX Supervisor, motion graphics company owner.
**Motivation:** No viable After Effects alternative exists on Linux. Natron is a compositor (Nuke-like), not a motion graphics tool. Flux bridges this gap by adding a timeline-driven layer system on top of Natron's proven engine.

**License:** GPL2 (inherited from Natron)

---

## Tech Stack

| Layer | Technology | Rationale |
|---|---|---|
| **UI Framework** | Qt5 (5.15+) or Qt6 (6.3+) | Native to Natron Engine, eliminates IPC bottleneck, dockable panels, OpenGL widgets |
| **Render Engine** | Natron Engine (C++17) | Battle-tested: 32-bit float pipeline, OCIO, OIIO, FFmpeg, OpenFX, cache, animation |
| **GPU Pipeline** | OpenGL (via Natron's existing GL pipeline) | Already working in Natron. Future: Vulkan migration if needed |
| **Video I/O** | FFmpeg (via OpenFX-IO ReadFFmpeg/WriteFFmpeg) | mov, mp4, mxf, ProRes, H264, H265 |
| **Image I/O** | OpenImageIO (via OpenFX-IO ReadOIIO/WriteOIIO) | EXR, TIFF (8/16/32-bit), PNG, JPEG, PSD, DPX |
| **Color Management** | OpenColorIO (integrated in Natron) | Industry-standard, GPU shader generation, ACES support |
| **Plugin System** | OpenFX 1.4 (hosted by Natron) | Access to existing plugin ecosystem (openfx-misc, openfx-arena, openfx-gmic, commercial) |
| **Animation** | Natron Curve/Keyframe system | Keyframes, Bezier interpolation, expressions (Python) |
| **Rotoscoping** | Natron RotoContext | Bezier shapes, feathering, motion blur |
| **Tracking** | Natron TrackerContext + openMVG | Point tracking, planar tracking |
| **Serialization** | Natron XML project format | Human-readable, diffable |
| **Build System** | CMake 3.16.7+ | Matches Natron build system |
| **Scripting** | Python 3 (via Natron's Python integration) | Expressions, automation, plugin scripting |

---

## Architecture Decision Record

### ADR-001: Qt over Electron
- **Date**: 2026-05-20
- **Decision**: Use Qt for the UI instead of Electron + React + Rust backend
- **Rationale**: Natron Engine is deeply coupled to Qt (QObject, signals/slots, QThread, QMutex in 329 Engine files). Replacing Qt would be weeks of refactoring. Qt eliminates IPC latency for pixel data. No validation spikes needed.
- **Consequence**: Qt is a required dependency. UI is built with Qt widgets, not web technologies.

### ADR-002: Fork Natron
- **Date**: 2026-05-20
- **Decision**: Fork Natron RB-2.6 under GPL2 rather than building from scratch
- **Rationale**: Natron provides years of proven engine code. Building from scratch would take months before rendering a single frame.
- **Consequence**: GPL2 license. Inherited codebase has C++98 heritage (now C++17).

### ADR-003: Layer-based timeline
- **Date**: 2026-05-20
- **Decision**: Primary UI is a layer-based timeline (After Effects paradigm). Node graph is secondary.
- **Rationale**: Motion graphics requires a timeline, not a node graph. Layers are UI abstractions over Natron nodes internally.

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

The Layer-to-Node bridge is the core new code in Flux. It translates timeline operations into Natron node graph operations.

---

## What Comes from Natron (Unchanged)

| Capability | Natron Component | Status |
|---|---|---|
| 32-bit float rendering pipeline | Engine/ | Battle-tested |
| OCIO color management | Engine/ + Settings | Full integration |
| Image I/O (EXR, TIFF, PSD, etc.) | OpenFX-IO (ReadOIIO) | Production-grade |
| Video I/O (mov, mp4, mxf) | OpenFX-IO (ReadFFmpeg) | Production-grade |
| OpenFX plugin host | Engine/ + HostSupport/ | OFX 1.4 compliant |
| RAM + Disk cache | Engine/Cache | LRU, tiled, multi-threaded |
| Keyframe animation | Engine/Curve, Engine/Knob | Full Bezier interpolation |
| Rotoscoping | Engine/RotoContext | Bezier shapes, feathering |
| Tracking | Engine/TrackerContext | Point + planar tracking |
| Python scripting | Engine/ Python integration | Expressions, plugins |
| Multi-threaded rendering | Engine/ ThreadPool | Production-grade |
| Headless rendering | Renderer/ | CLI batch rendering |
| Crash reporting | BreakpadClient/ | Separate process safety |

---

## What Flux Builds New

| Component | Description | Complexity |
|---|---|---|
| **Flux Application Shell** | New main window, menu bar, panel management | Medium |
| **Timeline Panel** | Layer-based timeline widget | High |
| **Layer-to-Node Bridge** | Translates timeline ops to node graph ops | High |
| **Effects Stack Panel** | Per-layer effect list (vertical stack) | Medium |
| **Properties Panel** | Per-effect parameter inspector | Medium (reuse Natron Knob UI) |
| **Dark Theme** | Qt stylesheet, After AE-inspired | Low |
| **Shape Layers** | Rect, ellipse, star, bezier | Medium |
| **Text Layers** | Text rendering with animation | High |
| **Export Templates** | Saveable export configurations | Low |
| **Improved Cache** | Persistent disk cache, background rendering | Medium |

---

## Repository Structure (Flux Fork of Natron RB-2.6)

```
flux/                              <- Forked from NatronGitHub/Natron (RB-2.6)
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
├── Gui/                           <- REDESIGN: Flux UI replaces Natron Qt widgets (745 files)
├── App/                           <- REDESIGN: Flux application shell
├── Shiboken/                      <- EVALUATE: Python bindings generator
│
├── ARCHITECTURE.md                <- This file
├── AGENTS.md                      <- Project-specific agent guidelines
├── plans/                         <- Phase plans and status
│   ├── PHASES.md                  <- Phase overview
│   └── phase-1.md                 <- Detailed phase 1 plan
├── tasks/                         <- Task tracking
│   └── TASKS.md                   <- Master task list
└── CMakeLists.txt                 <- Modified Natron root CMake
```

---

## Build Phases

| Phase | Deliverable | Est. Duration | Key Validation |
|---|---|---|---|
| **P0: Setup** | Project infrastructure, plans, tasks | 1 day | All tracking files created |
| **P1: Fork & Build** | Natron builds, engine validated | 3-5 days | Real images/videos imported, OCIO tested, cache tested |
| **P2: UI Shell** | Flux app shell, dockable panels, viewport | 1-2 weeks | Frame displays in viewport |
| **P3: Timeline** | Layer-based timeline, Layer-to-Node bridge | 2-3 weeks | Layers drive node graph, playback works |
| **P4: Effects + Properties** | Effects stack, property inspector | 1-2 weeks | Effects applied per-layer, parameters editable |
| **P5: Import/Export** | File browser, export dialog | 1 week | All formats work end-to-end |
| **P6: Shapes + Text** | Shape/text layer types | 2-3 weeks | Shapes render, text renders, both animatable |
| **P7: Polish + Cache** | Improved cache, undo/redo, performance | 2-3 weeks | 1080p 5-layer real-time target met |

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

- **Platform:** Linux-first. Cross-platform is a bonus.
- **No .aep support:** Clean break from After Effects.
- **Qt dependency:** Accepted. Qt Core + Widgets + OpenGL is ~30MB. Worth it for native engine integration.
- **Testing:** Every feature validated with real-world artifacts (images, videos). No feature is "done" until tested.
- **Task tracking:** All tasks in `tasks/TASKS.md`. All phases in `plans/PHASES.md`.
- **Code review:** Every implementation reviewed before marking complete.
- **500-line max per file:** Carry this forward for new code.
