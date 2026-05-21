# FluxLayer Gizmo Handoff — 2026-05-21

## Purpose

This handoff documents the current state after the FluxLayer PyPlug/gizmo session so another agent can continue without relying on chat memory.

## Authority and non-negotiables

Nick is the authority. Follow his instructions literally.

Do not simplify the implementation, do not add placeholders, do not leave TODOs instead of working code, and do not invent architecture when Nick has specified one.

## Current repository state at handoff

- Branch: `flux/main`
- Current uncommitted file reported by `git status --short`: `Gui/FluxTimeline.cpp`
- The FluxLayer PyPlug work is **not accepted** and must be treated as **broken/incomplete** until verified in Natron.
- Do not assume the current `plugins/FluxLayer.py`, `~/.Natron/PyPlugs/FluxLayer.py`, `Gui/Gui05.cpp`, or `Gui/FluxTimeline.cpp` are correct.

## What has been validated previously

The following earlier work was validated before the FluxLayer gizmo effort:

1. Natron fork builds with Qt6 on Fedora 44.
2. Natron GUI launches under Wayland using xcb compatibility:
   ```bash
   QT_PLUGIN_PATH=/usr/lib64/qt6/plugins QT_QPA_PLATFORM=xcb /home/npittas/Flux/build/App/Natron
   ```
3. OpenFX-IO and OpenFX-Misc plugins were built/installed.
4. JPG/PNG/MOV/MP4 import works after selecting a valid OCIO config.
5. Project Bin import shows assets.
6. Drag/drop from Project Bin to Timeline was confirmed working.
7. Timeline UI move/trim interactions were confirmed visually working, but they did **not** correctly drive the underlying node parameters.

## Current problem
## Critical Contract: Stable Gizmo-Level Parameters

The FluxLayer PyPlug must expose stable group-level parameters. C++ must never depend on internal node auto-numbered names such as `FrameRange1frameRange`, `FrameRange2frameRange`, `TimeOffset1timeOffset`, or `Transform1center`.

Required external parameter names on every FluxLayer gizmo instance:

- `frameRange`
- `timeOffset`
- `translate`
- `scale`
- `rotate`
- `center`
- `motionBlur`
- `shutter`

The PyPlug should create those parameters on the group and alias/link them to the internal node knobs. Timeline code must store a direct reference to each layer's `gizmoNode` and update parameters on that exact node:

```cpp
layer.gizmoNode->getKnobByName("frameRange");
layer.gizmoNode->getKnobByName("timeOffset");
```

not by searching node names globally and not by guessing internal generated node names.

## Current Status


Observed problems from Nick:

1. Importing footage previously auto-added it to the timeline; import must only add to Project Bin.
2. Dropping to timeline must create exactly one layer gizmo for that footage.
3. Multi-layer behavior was cross-wired: second footage connected to first footage's nodes.
4. Trim/move updated UI bars but not the real node parameters.
5. Existing separated node chain approach produced messy node graphs.
6. Current PyPlug/gizmo attempts were not properly verified before C++ integration.
7. Current implementation did not reliably create the intended gizmo and did not correctly initialize/update its exposed knobs.

## Correct architecture required by Nick

### Per-layer gizmo

Each timeline footage layer must create one Natron PyPlug/gizmo group based on the pattern in `FinalPlugin.py`.

The gizmo must contain internal nodes:

```text
Read -> FrameRange -> TimeOffset -> Transform -> Output
```

The gizmo must contain a **Read node inside the group**. Do not replace it with an Input node.

### Merge nodes outside the gizmo

Merge nodes stay outside the gizmo. They compose the outputs of layer gizmos.

For multiple layers:

```text
Layer 1 Gizmo -> Merge chain background/input B
Layer 2 Gizmo -> Merge A over previous result
Layer 3 Gizmo -> Merge A over previous result
Viewer -> final Merge output
```

The viewer must always connect to the last node in the external merge chain.

### Exposed knobs

The group must expose internal node knobs via `setAsAlias`, following the style in `FinalPlugin.py`.

Required exposed controls:

- FrameRange trim control: both start/in and end/out values
- TimeOffset control: layer start position/move in timeline
- Transform translate
- Transform scale
- Transform rotate
- Transform center
- Transform motion blur settings
- Transform shutter settings

Use the actual parameter names from the created internal Natron nodes. Do not guess names. Verify against a working PyPlug instance in Natron.

### Initial frame range is imperative

When a footage file is dropped to the timeline and the gizmo is created:

1. Create the gizmo.
2. Set the filename/path on the gizmo's internal Read node.
3. Read or calculate the real first and last frames from the Read node / Natron metadata.
4. Initialize the FrameRange exposed knob to the real start/end frame values.

This is mandatory. If the FrameRange node starts with the wrong range, timeline trimming cannot work correctly.

### Transform center is required

When creating the gizmo, calculate the transform center from the footage resolution:

```text
center.x = width / 2
center.y = height / 2
```

Use Natron/application-provided metadata when available. Do not invent values.

### Timeline behavior required

- Import into Project Bin: must not create reader nodes and must not create timeline layers.
- Drag asset from Project Bin to Timeline: create exactly one layer gizmo for that footage.
- Trim left edge: update the gizmo FrameRange start/in value.
- Trim right edge: update the gizmo FrameRange end/out value.
- Move clip left/right: update the gizmo TimeOffset value.
- Reorder layers: reconnect external Merge nodes only; do not recreate or cross-wire gizmo internals.
- Each layer must retain a reliable mapping to its own gizmo node and its exposed knobs.

## Files involved

Primary files for the next agent:

- `FinalPlugin.py` — working reference PyPlug pattern provided by Nick.
- `plugins/FluxLayer.py` — current FluxLayer PyPlug; must be audited/fixed against `FinalPlugin.py`.
- `/home/npittas/.Natron/PyPlugs/FluxLayer.py` — installed copy Natron loads; keep in sync with repo version.
- `Gui/Gui05.cpp` — `setupFluxUi()` signal wiring and `Gui::rebuildCompositingGraph()`.
- `Gui/FluxTimeline.h` — `FluxLayer` data model and gizmo/node pointers.
- `Gui/FluxTimeline.cpp` — timeline trim/move/reorder handlers and knob update logic.
- `Gui/FluxProjectBin.cpp` — project bin import/drag behavior.

## Recommended next steps

Do these in order. Do not skip verification.

1. Revert or repair the current broken FluxLayer-related edits until the project builds.
2. Make `plugins/FluxLayer.py` match `FinalPlugin.py` structurally, with internal Read/FrameRange/TimeOffset/Transform/Output.
3. Copy it to `/home/npittas/.Natron/PyPlugs/FluxLayer.py`.
4. Launch Natron and manually verify the PyPlug appears and can be created from the UI before touching C++ integration.
5. Verify the group exposes the exact expected knob names. Record them in this handoff or task docs.
6. Only after that, update C++ to create the gizmo by its exact plugin ID.
7. On drop, set the internal Read filename, initialize frame range from real footage, initialize center from resolution.
8. Store a per-layer mapping to the created gizmo node.
9. Update trim/move code to write to that layer's gizmo knobs.
10. Rebuild external Merge chain from gizmo outputs and connect Viewer to final Merge.
11. Build and then test with one layer, then two layers.

## Acceptance test for this task

A future agent should not mark this done until all are true:

1. Importing footage into Project Bin does not create a timeline layer or node graph reader.
2. Dropping one footage asset to the timeline creates one FluxLayer gizmo containing a Read node inside it.
3. The gizmo's FrameRange starts with the real media first/last frame.
4. The gizmo's Transform center starts at the real media center.
5. Trimming left changes the gizmo's frame range start.
6. Trimming right changes the gizmo's frame range end.
7. Moving the clip changes the gizmo's time offset.
8. Dropping a second footage asset creates a second independent gizmo with its own internal Read node.
9. External Merge nodes composite the layer gizmos in timeline order.
10. Viewer is connected to the final merge/output and shows the composited result.
11. Node graph layout is readable, not stacked on top of itself.
12. The code builds cleanly.

## Warning

Do not trust any unverified parameter names in current code. The correct names must come from a verified PyPlug created in Natron and from Natron's actual Python/C++ API, not guesses.
