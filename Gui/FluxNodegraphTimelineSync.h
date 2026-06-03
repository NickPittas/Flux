/* ***** BEGIN LICENSE BLOCK *****
 * Flux — Nodegraph-to-Timeline Sync Utilities
 * (C) 2025 Nick Pittas
 * GPL2 — see LICENSE.txt
 * ***** END LICENSE BLOCK ***** */

#ifndef FLUX_NODEGRAPH_TIMELINE_SYNC_H
#define FLUX_NODEGRAPH_TIMELINE_SYNC_H

#include "Global/Macros.h"

#include "Engine/EngineFwd.h"
#include "Gui/FluxTimeline.h"

#include <QVector>

CLANG_DIAG_OFF(deprecated)
#include <QString>
CLANG_DIAG_ON(deprecated)

NATRON_NAMESPACE_ENTER

/**
 * @brief A segment of the main pipe between two Merge nodes.
 *
 * mergeNode   — the Merge node at the B-input boundary of this segment
 *               (nullptr for the first segment, i.e. above bgReformat)
 * inlineNodes — non-Merge effect nodes between this Merge and the next Merge
 *               (or between bgReformat and the first Merge)
 */
struct MainPipeSegment
{
    NodePtr mergeNode;            /**< The Merge node; nullptr for the bgReformat-adjacent segment */
    QVector<NodePtr> inlineNodes; /**< Non-Merge effect nodes on the main pipe in this segment */
};

/**
 * @brief Creates a FluxEffect from a Natron NodePtr, populating all fields
 *        that have a sensible default derived from the node.
 *
 * Fields populated from the node:
 *   pluginId — node->getPluginID()
 *   label    — node->getLabel()
 *   enabled  — !node->isNodeDisabled()
 *   node     — the input NodePtr
 *
 * Remaining fields are left at their FluxEffect constructor defaults
 * (viewerInputBadges empty, AI mask fields unset).
 */
FluxEffect createFluxEffectFromNode(NodePtr node);

/**
 * @brief Walks the main pipe from finalOutput backward through getInput(0)
 *        to bgReformat, collecting Merge nodes as layer boundaries.
 *
 * Returns segments in top-to-bottom order (bgReformat side first,
 * finalOutput side last). Each segment records the Merge node and any
 * non-Merge effect nodes between it and the next Merge.
 *
 * @param finalOutput  The output node at the bottom of the compositing tree.
 * @param bgReformat   The background reformat node at the top of the main pipe.
 * @return Segments ordered from bgReformat to finalOutput.
 */
QVector<MainPipeSegment> walkMainPipe(NodePtr finalOutput, NodePtr bgReformat);

/**
 * @brief An effect discovered by findNewEffects(), together with any
 *        masks attached to it via its mask-capable input.
 *
 * FluxEffect does not carry a masks list (masks live on FluxLayer), so
 * this struct pairs an effect with the FluxMask entries that the chain
 * walker discovers on it.  The caller (T05) places them into the
 * FluxLayer::masks list with the correct effectIndex.
 */
struct FluxEffectWithMasks
{
    FluxEffect effect;
    QList<FluxMask> masks; ///< masks attached to this effect via mask-capable inputs
};

/**
 * @brief A newly-discovered effect on the main pipe that is not yet tracked
 *        by any existing timeline layer and therefore needs an adjustment layer.
 *
 * Created by findNewMainPipeEffects() when it encounters a non-Merge inline node
 * that is absent from the tracked set. The caller (A04) is responsible for
 * placing these into adjustment layers in the correct timeline order.
 */
struct NewAdjustmentEffect
{
    NodePtr node;         /**< The node in the segment */
    FluxEffect fx;        /**< Effect descriptor populated via createFluxEffectFromNode() */
    int segmentIndex;     /**< Index into the segments QVector passed to findNewMainPipeEffects */
};

/**
 * @brief Discovers new main-pipe effects that are not yet tracked in the timeline.
 *
 * For each segment's inlineNodes, skips Merge nodes and nodes already present
 * in the tracked set. Any remaining node is a newly-added main-pipe effect that
 * needs an adjustment layer.
 *
 * @param segments  Output of walkMainPipe(), ordered bgReformat→finalOutput.
 * @param tracked   Set of raw Node pointers already tracked (from collectTrackedNodes).
 * @return QVector of NewAdjustmentEffect entries, one per untracked inline node.
 */
QVector<NewAdjustmentEffect> findNewMainPipeEffects(const QVector<MainPipeSegment>& segments,
                                                     const QSet<Node*>& tracked);

/**
 * @brief Finds ONLY new effects in a Merge node's A-input chain that are
 *        NOT already tracked and NOT gizmo-internal.
 *
 * Walks the Merge node's A-input chain backward, stopping at source/boundary
 * nodes and at any node already present in the `tracked` set. This makes it suitable for incremental sync: given a
 * Merge node and the set of nodes already accounted for in existing timeline
 * layers, it returns only the truly new effects that need to be added.
 *
 * Walk logic (starting from mergeNode->getInput(1), A-input):
 *   1. Null? → stop
 *   2. Read node? → stop (source boundary)
 *   3. Flux gizmo node? → stop
 *   4. Inside a NodeGroup (gizmo internal)? → stop
 *   5. Is bgReformat? → stop
 *   6. Is a Merge? → stop (precomp boundary)
 *   7. Is in `tracked`? → stop (already known)
 *   8. Otherwise → it's a NEW effect; create FluxEffect + discover mask
 *
 * @param mergeNode    The Merge node whose A-input chain to walk.
 * @param bgReformat   The background reformat node (walk boundary stop).
 * @param tracked      Set of raw Node pointers already tracked in the timeline.
 * @return Ordered QVector<FluxEffectWithMasks> with ONLY new effects (source→Merge).
 */
QVector<FluxEffectWithMasks> findNewEffects(NodePtr mergeNode, NodePtr bgReformat, const QSet<Node*>& tracked);

/**
 * @brief Collects every raw node pointer currently tracked in the timeline layers.
 *
 * Walks all FluxLayer entries and gathers every NodePtr field from layers,
 * their masks, and their effects. The returned QSet<Node*> is used by the
 * sync to distinguish "existing" nodes (leave untouched) from orphaned ones.
 *
 * Fields collected:
 *   FluxLayer: mergeNode, readerNode, gizmoNode, maskApplyNode
 *   FluxMask:  maskNode, reformatNode
 *   FluxEffect: node, aiMaskReadNode, aiMaskShuffleNode, aiMaskChannelMergeNode
 *
 * @param layers  The current timeline layer list.
 * @return QSet of raw Node pointers (may contain nullptr entries if a field is unset).
 */
QSet<Node*> collectTrackedNodes(const QList<FluxLayer>& layers);

NATRON_NAMESPACE_EXIT

#endif // FLUX_NODEGRAPH_TIMELINE_SYNC_H
