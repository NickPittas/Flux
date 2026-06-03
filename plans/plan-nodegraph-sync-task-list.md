# Nodegraph → Timeline Sync: Additive-Only Implementation Task List

## Core Principle
The sync is ADDITIVE ONLY. It never rebuilds, never replaces, never removes existing entries.
It walks the compositing graph, finds nodes NOT already tracked in the timeline, and adds only those.

## Task Dependency Chain
A01 → A02 + A03 (parallel) → A04 → A05 → A06

---

## A01: Build the "already tracked" node set
- File: `Gui/FluxNodegraphTimelineSync.cpp`
- New function: `QSet<Node*> collectTrackedNodes(const QList<FluxLayer>& layers)`
- Walks all layers, collects every node pointer: mergeNode, readerNode, gizmoNode, each effect.node, each mask.maskNode, maskApplyNode, aiMaskReadNode, aiMaskShuffleNode, aiMaskChannelMergeNode
- This is the "known" set. Anything in it is left untouched.
- Depends on: nothing

## A02: Find new effects for each Merge's A-input
- File: `Gui/FluxNodegraphTimelineSync.cpp`
- New function: `QList<FluxEffect> findNewEffects(NodePtr mergeNode, const QSet<Node*>& tracked, const QList<FluxEffect>& existingEffects)`
- Walk Merge input 1 backward, skip: gizmo-internal nodes (NodeGroup check), Read nodes, gizmo nodes, nodes in tracked set
- Only nodes NOT in tracked and NOT gizmo-internal → they're new effects
- For each new effect: check for mask input via discoverMaskInput. If mask node is NOT in tracked, create FluxMask child
- Append new effects to the existing effect list (preserve order: existing first, then new)
- Depends on: A01

## A03: Walk main pipe for new adjustment-layer effects
- File: `Gui/FluxNodegraphTimelineSync.cpp`
- New function: `QList<FluxEffect> findNewMainPipeEffects(const QVector<MainPipeSegment>& segments, const QSet<Node*>& tracked)`
- For each segment's inlineNodes: skip nodes in tracked, skip Merges
- Remaining nodes are new effects sitting on the main pipe → they need an adjustment layer
- Depends on: A01

## A04: Additive sync entry point
- File: `Gui/Gui05.cpp` — replace current syncFluxTimelineFromNodeGraph()
- Steps:
  1. Call A01 to get tracked node set
  2. Walk main pipe (existing walkMainPipe)
  3. For each existing layer that has a mergeNode matching a segment's Merge: call A02 to find new effects, APPEND to layer.effects
  4. For new main-pipe effects (A03): find or create adjustment layer, append effects
  5. For any Merge whose A-input leads to a Read node NOT in tracked: new layer → create FluxLayer, insert at correct position
  6. refreshVisibleRows() + rebuildCompositingGraph()
- NEVER removes or modifies existing layer/effect entries
- NEVER replaces the layer list — only appends to effects, appends layers
- Depends on: A01, A02, A03

## A05: Remove old rebuild-from-scratch code
- File: `Gui/FluxNodegraphTimelineSync.cpp`
- Remove: buildLayersFromSegments, mergeEffectLists, buildAdjustmentLayers, expandMergeALayer
- Remove: old Step 2b nested-Merge expansion loop from Gui05.cpp
- Keep: walkMainPipe, createFluxEffectFromNode, collectTrackedNodes, findNewEffects, findNewMainPipeEffects
- Depends on: A04 working

## A06: Remove diagnostic logging
- File: `Gui/Gui05.cpp`
- Remove all fprintf(stderr, "FLUX SYNC:...") lines
- Depends on: A05
