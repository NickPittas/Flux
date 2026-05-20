# Flux P2: UI Shell — Implementation Plan

> **Goal**: Replace Natron's node-graph GUI with Flux's layer-based motion graphics UI.
> Keep the engine, build new panels on top of the existing Qt infrastructure.
> **Estimated Duration**: 2-3 weeks

---

## Natron GUI Architecture (What We're Replacing)

### Class Hierarchy

```
AppManager (singleton, Engine/)
  └── GuiApplicationManager (Gui/) — Qt app init, splash screen, shortcuts, icons
        └── GuiAppInstance (Gui/) — per-project GUI instance
              └── Gui (Gui/) — QMainWindow, the main window
                    ├── TabWidget — dockable panel container (tab-based)
                    ├── Splitter — resizable split between panels
                    ├── ViewerTab — viewer panel (contains ViewerGL)
                    │     └── ViewerGL — QOpenGLWidget, renders frames
                    ├── NodeGraph — QGraphicsView, node graph canvas
                    ├── CurveEditor — keyframe curve editor
                    ├── DopeSheet — dope sheet editor
                    ├── DockablePanel — properties panel for a node's knobs
                    ├── PropertiesBin — container for multiple DockablePanels
                    ├── ProjectGui — project settings panel
                    ├── Histogram — histogram panel
                    ├── ScriptEditor — Python script editor
                    └── ToolButton — left toolbar buttons
```

### Layout Structure (from `createDefaultLayout1()`)

```
┌─────────────────────────────────────────────────────────┐
│ Menu Bar: File | Edit | Layout | Display | Render | ... │
├────┬────────────────────────────┬───────────────────────┤
│    │                            │                       │
│ T  │     Viewer / Histogram     │   Properties Bin      │
│ o  │     (TabWidget)            │   (TabWidget)         │
│ o  │                            │                       │
│ l  ├────────────────────────────┤                       │
│    │                            │                       │
│ B  │  NodeGraph / CurveEditor   │                       │
│ a  │  / DopeSheet               │                       │
│ r  │  (TabWidget)               │                       │
│    │                            │                       │
└────┴────────────────────────────┴───────────────────────┘
```

### Key Insight: What to Keep vs. Replace

| Component | Action | Reason |
|---|---|---|
| `GuiApplicationManager` | **KEEP, extend** | App lifecycle, plugin loading, shortcuts |
| `GuiAppInstance` | **KEEP, extend** | Virtual methods for dialogs, rendering |
| `Gui` (QMainWindow) | **REPLACE** | Our layout is completely different |
| `TabWidget` | **KEEP** | Dockable panel system works well |
| `Splitter` | **KEEP** | Panel resizing works well |
| `ViewerGL` | **KEEP** | OpenGL rendering works, just restyle |
| `ViewerTab` | **KEEP, extend** | Add layer-aware controls |
| `NodeGraph` | **KEEP as hidden tab** | Power-user access to node graph |
| `DockablePanel` | **KEEP** | Knob parameter UI works well |
| `CurveEditor` | **KEEP** | Keyframe editing works |
| `DopeSheet` | **KEEP** | Timeline keyframe view works |
| Menu system | **REPLACE** | Flux needs different menus |

---

## Flux Layout (Target)

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

---

## Implementation Tasks

### T019: Create FluxMainWindow (replaces `Gui::setupUi()`)

**Objective**: New `FluxMainWindow` class that inherits from `QMainWindow` and creates the Flux panel layout instead of Natron's default.

**Approach**:
- Subclass `Gui` (or replace `setupUi()` via a flag)
- Create 4 main panel areas: **Project Bin** (top-left), **Viewport** (top-center), **Right Panel** (Effects + Properties), **Timeline** (bottom)
- Use existing `TabWidget` and `Splitter` classes for dockable panels
- Create a new `FluxTimeline` widget (custom QWidget)

**Files to create**:
- `Gui/FluxMainWindow.h` — new main window class
- `Gui/FluxMainWindow.cpp` — layout setup

**Files to modify**:
- `Gui/Gui.cpp` — call `FluxMainWindow::setupFluxUi()` instead of `setupUi()`
- `Gui/Gui.h` — add friend class or virtual method

**Validation**: Flux launches with new layout, panels are dockable and resizable.

---

### T020: Create FluxTimeline widget

**Objective**: Layer-based timeline widget that replaces Natron's node-graph-centric workflow.

**Approach**:
- Custom QWidget with three sections: TimeRuler, LayerList, LayerTracks
- LayerList shows layer names with controls (solo/mute/lock, visibility eye)
- LayerTracks shows horizontal bars for each layer's duration
- Playhead is a vertical line that scrubs across all tracks
- Playback controls (play/pause/stop, fps, frame counter) in a toolbar

**Key classes**:
- `FluxTimeline` — main timeline widget
- `FluxTimeRuler` — time ruler with playhead
- `FluxLayerRow` — single layer row (name + track bar)
- `FluxPlaybackControls` — play/pause/stop buttons

**Integration with Engine**:
- Connect to `TimeLine::frameChanged()` signal for playhead updates
- Layer operations create/remove Natron `Node` instances via `AppInstance::createNode()`
- Layer reorder changes node connection order via `NodeCollection::connectNodes()`

**Files to create**:
- `Gui/FluxTimeline.h/cpp`
- `Gui/FluxTimeRuler.h/cpp`
- `Gui/FluxLayerRow.h/cpp`
- `Gui/FluxPlaybackControls.h/cpp`

**Validation**: Timeline displays layers, playhead moves, playback works.

---

### T021: Create Layer-to-Node Bridge

**Objective**: Translation layer that converts timeline operations to Natron node graph operations.

**Approach**:
- A `FluxLayer` represents one layer in the timeline
- Each `FluxLayer` owns one or more Natron `Node` instances:
  - Footage layer → `ReadOIIO` or `ReadFFmpeg` node
  - Solid layer → `Constant` node (from Misc.ofx)
  - Adjustment layer → passthrough (affects layers below)
  - Null layer → no node (just a transform parent)
- Layers are composited top-to-bottom using `Merge` nodes (from Misc.ofx)
- Effect stack per layer = chain of effect nodes connected serially

**Key class**:
- `FluxLayerBridge` — manages the mapping between FluxLayers and Natron nodes
  - `addLayer(type, filePath)` → creates nodes, connects merge chain
  - `removeLayer(index)` → disconnects and destroys nodes
  - `reorderLayer(from, to)` → reconnects merge chain
  - `addEffect(layerIndex, pluginId)` → creates effect node, inserts into chain
  - `removeEffect(layerIndex, effectIndex)` → removes effect node, reconnects chain

**Files to create**:
- `Engine/FluxLayerBridge.h/cpp`
- `Engine/FluxLayer.h/cpp`

**Validation**: Adding/removing/reordering layers creates the correct node graph.

---

### T022: Create Effects Stack Panel

**Objective**: Panel showing the effects applied to the selected layer, with add/remove/reorder.

**Approach**:
- QWidget with a vertical list of effect entries
- Each entry shows: effect name, enable/disable toggle, delete button
- "+" button at the bottom opens an effect picker dialog
- Drag to reorder effects (changes node chain order)
- Clicking an effect selects it and shows its properties in the Properties panel

**Files to create**:
- `Gui/FluxEffectsPanel.h/cpp`
- `Gui/FluxEffectPickerDialog.h/cpp`

**Validation**: Effects can be added/removed/reordered, properties update correctly.

---

### T023: Create Dark Theme (After Effects-inspired)

**Objective**: Modern dark theme applied via Qt stylesheet.

**Approach**:
- Create a `.qss` stylesheet file (Qt CSS)
- Dark backgrounds (#1e1e1e, #2d2d2d), light text (#e0e0e0)
- Accent color for selection (#4a90d9 blue)
- Custom styling for: panels, tabs, buttons, sliders, scrollbars, menus
- Apply via `QApplication::setStyleSheet()`

**Files to create**:
- `Resources/themes/flux-dark.qss`

**Files to modify**:
- `Gui/Gui.cpp` or `GuiApplicationManager.cpp` — load Flux stylesheet instead of Natron's

**Validation**: All UI elements styled consistently, looks professional.

---

### T024: Create Project Bin (top-left panel)

**Objective**: Panel showing imported files/assets in the project. This is the top-left column in the Flux layout — the first thing users see when importing footage.

**Approach**:
- QWidget with a QListView/QTreeView showing imported assets
- Each item shows: thumbnail, filename, resolution, duration, framerate, codec
- **Import**: Drag-and-drop from file manager, or File → Import menu, or "Import" button at bottom
- **Add to timeline**: Double-click an asset to create a new layer, or drag from Project Bin to Timeline
- **Thumbnail preview**: Generate thumbnails for images and video (first frame)
- **Asset info**: Right-click for properties, reveal in file manager, replace footage
- **Filter/search**: Text filter to find assets by name

**Integration with Engine**:
- Each imported asset creates a `Node` (ReadOIIO/ReadFFmpeg) but does NOT connect it to the render tree until dragged to timeline
- The Project Bin is essentially a "library" of available reader nodes
- When an asset is added to the timeline, the Layer-to-Node Bridge connects the existing reader node into the Merge chain

**Files to create**:
- `Gui/FluxProjectBin.h/cpp`
- `Gui/FluxProjectBinModel.h/cpp` (QAbstractItemModel for the asset list)
- `Gui/FluxProjectBinItem.h/cpp` (individual asset item with metadata)

**Files to modify**:
- `Gui/Gui.h/cpp` — add FluxProjectBin* member, create in setupFluxUi()
- `Engine/AppInstance` — hook into project file loading to populate the bin

**Validation**: Import files via drag-and-drop, see thumbnails, double-click to add layer to timeline.

---

### T025: Create Flux Menu System

**Objective**: Menu bar with Flux-specific actions (not Natron's compositing-focused menus).

**Menus**:
- **File**: New, Open, Save, Save As, Import, Export, Recent, Quit
- **Edit**: Undo, Redo, Cut, Copy, Paste, Delete, Select All, Preferences
- **Composition**: New Layer (Footage/Solid/Adjustment/Null/Text/Shape), Composition Settings
- **Layer**: Layer settings, Add Effect, Pre-compose, Move Up/Down
- **Effects**: Browse effects (opens picker), apply last effect
- **View**: Zoom In/Out, Fit, Toggle panels, Full Screen
- **Help**: About, Documentation

**Files to modify**:
- `Gui/Gui.cpp` — replace `createMenuActions()` with Flux menu structure

**Validation**: All menu items functional, keyboard shortcuts work.

---

### T026: Integrate and Test End-to-End

**Objective**: Full integration test of all P2 components.

**Test scenarios**:
1. Launch Flux → see new layout with dark theme
2. Import a JPG → appears in project panel
3. Drag JPG to timeline → creates a layer, renders in viewport
4. Add a Blur effect → appears in effects stack, renders on layer
5. Add a second layer → composites correctly
6. Reorder layers → compositing order changes
7. Playback → plays through timeline at correct FPS
8. Open node graph tab → see the underlying Natron nodes

**Validation**: All scenarios pass without crashes.

---

## Execution Order

| Order | Task | Depends On | Estimated Time |
|---|---|---|---|
| 1 | T023: Dark Theme | Nothing | 0.5 day |
| 2 | T019: FluxMainWindow | T023 | 1-2 days |
| 3 | T024: Project Bin | T019 | 1-1.5 days |
| 4 | T025: Flux Menu System | T019 | 0.5 day |
| 5 | T020: FluxTimeline | T019 | 2-3 days |
| 6 | T021: Layer-to-Node Bridge | T020, T024 | 2-3 days |
| 7 | T022: Effects Stack Panel | T021 | 1-2 days |
| 8 | T026: Integration Test | All above | 1 day |

**Total**: ~10-14 days

---

## Architecture Decision: Extend vs. Replace `Gui`

**Decision**: Extend `Gui` class, not replace it.

**Rationale**:
- `Gui` has deep integration with `GuiAppInstance` and `GuiApplicationManager`
- `Gui` manages viewers, histograms, undo stacks, menus, node creation
- Replacing `Gui` would require reimplementing hundreds of connections
- Instead, we override `setupUi()` to create our layout, and add new methods

**Implementation**:
1. Add a `bool _fluxMode` flag to `Gui`
2. When `true`, `setupUi()` calls `setupFluxUi()` instead of the default layout
3. `setupFluxUi()` creates: Viewport, Right Panel, Timeline, Project Panel
4. The node graph, curve editor, dope sheet still exist but as hidden tabs
5. New panels (FluxTimeline, FluxEffectsPanel) are added as TabWidget tabs

---

## Files Summary

### New Files (in `Gui/`)

| File | Purpose |
|---|---|
| `FluxTimeline.h/cpp` | Layer-based timeline widget |
| `FluxTimeRuler.h/cpp` | Time ruler with playhead |
| `FluxLayerRow.h/cpp` | Single layer row in timeline |
| `FluxPlaybackControls.h/cpp` | Play/pause/stop controls |
| `FluxEffectsPanel.h/cpp` | Effects stack per layer |
| `FluxEffectPickerDialog.h/cpp` | Effect browser/picker |
| `FluxProjectBin.h/cpp` | Project Bin (imported assets with thumbnails) |
| `FluxProjectBinModel.h/cpp` | Data model for Project Bin asset list |
| `FluxProjectBinItem.h/cpp` | Individual asset item with metadata |

### New Files (in `Engine/`)

| File | Purpose |
|---|---|
| `FluxLayerBridge.h/cpp` | Layer-to-Node translation |
| `FluxLayer.h/cpp` | Layer data model |

### Modified Files

| File | Change |
|---|---|
| `Gui/Gui.h` | Add `setupFluxUi()`, `FluxTimeline*`, `FluxEffectsPanel*`, `FluxProjectBin*`, etc. |
| `Gui/Gui.cpp` | Conditional `setupUi()` → `setupFluxUi()` |
| `Gui/Gui05.cpp` | New `setupFluxUi()` implementation |
| `Gui/GuiApplicationManager.cpp` | Load Flux stylesheet, set Flux mode |
| `App/NatronApp_main.cpp` | Pass `--flux` flag or detect Flux build |
| `CMakeLists.txt` | Add new source files to build |

### Resource Files

| File | Purpose |
|---|---|
| `Resources/themes/flux-dark.qss` | Dark theme stylesheet |
