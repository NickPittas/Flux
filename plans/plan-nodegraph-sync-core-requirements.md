# Nodegraph → Timeline Sync: Core Requirements

## What This Feature Does

The timeline is a **facade** over the nodegraph. The nodegraph is the **only source of truth**. Natron only respects the nodegraph — the timeline is a UI to help users work with the nodegraph visually.

This feature ensures the timeline **mirrors the nodegraph** when changes are made in the nodegraph. Without it, effects added through the nodegraph are invisible in the timeline, and the user won't know why something is happening in the render.

## Trigger

- Sync does NOT happen continuously — that would interfere with nodegraph building (users often need 5-10 connections before a flow is usable).
- Sync triggers when the user **clicks anywhere in the timeline** (click, not hover — hover-focus would cause accidental syncs).
- A **sync banner** appears: "Nodegraph changes detected — click to sync". The user must click the banner to run the actual sync.
- A **manual sync button** is always visible in the timeline ruler area.
- A **keyboard shortcut** (Ctrl+Shift+Y, pending conflict check) also triggers sync.

## Dirty Flag

- Only nodes that belong to the **compositing tree** (Read nodes, effect nodes, Merge nodes, gizmo nodes, Reformat — everything wired into the compositing graph) trigger the dirty flag.
- Unconnected nodes or nodes outside the tree do NOT trigger dirty.
- The dirty handler is **O(1)** — just sets a bool. Adding 500 nodes = 500 cheap bool sets, zero UI impact.
- The dirty flag is set by node signals (`inputChanged`, `outputsChanged`, `activated`) scoped to compositing-tree nodes only.
- Timeline's own `rebuildCompositingGraph()` must NOT trigger dirty (RAII guard).

## Structural Sync Algorithm (The Core)

Walk the main pipe (the Reformat node's pipe where all Merge B inputs connect) and map the nodegraph structure to timeline layers and effects.

### Sync Rules (from Nick):

**Rule 1: Effect inline in a layer's flow**
- An effect connected in a layer's node chain (between Read/Gizmo and Merge) appears as an effect under that layer in the timeline.
- Example: Read → Blur → Grade → Merge → Blur appears under that layer's effect stack.

**Rule 2: Effect on main pipe between two Merges → adjustment layer**
- An effect connected between two Merge nodes on the main pipe appears in an adjustment layer between the corresponding timeline layers.
- If an adjustment layer already exists between those two layers, add the effect to it. Do NOT create a new one.

**Rule 3: Effect on main pipe after last Merge → bottom adjustment layer**
- An effect after the last Merge on the main pipe (no Merge below) maps to the bottom adjustment layer.

**Rule 4: Merge in main pipe A-input → expand to new layers (1 level deep)**
- If a Merge appears in the main pipe's A-input, expand that A-pipe into new timeline layers.
- One level deep only — do not recurse into nested Merges.
- If no Read node at the top of that A-pipe stack, create a **solid layer** to represent it.

**Rule 5: Merge in a layer's flow (precomp) → skip**
- If a Merge is inside a layer's existing flow (not on the main pipe), it's a precomp. Nothing is added to the timeline for it.

### Matching Strategy

- Match existing timeline layers to nodegraph nodes by **node pointer** and **script name**.
- Match existing effects by **node pointer** and **plugin ID**.
- Matched entries preserve their timeline data: keyframes, trim, opacity, expanded state, masks, solo/mute/lock.
- Unmatched nodegraph nodes become **new** timeline entries.
- Timeline entries with no matching nodegraph node are **removed**.

## Main Pipe Definition

The main pipe in compositing is the pipe where all the Merge nodes' B input is connected. In Flux's specific case, it's the Reformat node's pipe where all the Merge B inputs connect.

- Merge input 0 = B = background/main pipe
- Merge input 1 = A = foreground/layer source

## What Sync Does NOT Do

- Does not sync continuously (only on demand).
- Does not expand precomps recursively (only 1 level deep).
- Does not create cross-layer connections.
- Does not change the nodegraph (sync is nodegraph → timeline only; timeline → nodegraph already works via rebuildCompositingGraph).
- Does not trigger during nodegraph editing — only when user explicitly syncs.

## Files

The sync algorithm should be implemented in new `Gui/FluxNodegraphTimelineSync.{h,cpp}` files to keep the logic separate from the already-large Gui05.cpp and FluxTimeline.cpp.
