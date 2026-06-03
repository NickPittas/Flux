/* ***** BEGIN LICENSE BLOCK *****
 * Flux — Nodegraph-to-Timeline Sync Utilities
 * (C) 2025 Nick Pittas
 * GPL2 — see LICENSE.txt
 * ***** END LICENSE BLOCK ***** */

#include "Gui/FluxNodegraphTimelineSync.h"

#include "Engine/EffectInstance.h"
#include "Engine/Node.h"
#include "Engine/NodeGroup.h"
#include "Gui/FluxMaskUtils.h"

#include <algorithm>
#include <QSet>

NATRON_NAMESPACE_ENTER

FluxEffect
createFluxEffectFromNode(NodePtr node)
{
    if (!node) {
        return FluxEffect();
    }

    FluxEffect fx;

    fx.pluginId = QString::fromStdString(node->getPluginID());
    fx.label    = QString::fromStdString(node->getLabel());
    fx.enabled  = !node->isNodeDisabled();
    fx.node     = node;

    // viewerInputBadges, isAIMaskCopy, aiMaskUsage, aiMaskTargetPlane,
    // aiMaskSourceChannel, aiMaskOperation, aiMaskSourceRelativePath,
    // aiMaskManifestRelativePath, aiMaskReadNode, aiMaskShuffleNode,
    // aiMaskChannelMergeNode are left at their FluxEffect constructor defaults.
    return fx;
}

QVector<MainPipeSegment>
walkMainPipe(NodePtr finalOutput, NodePtr bgReformat)
{
    QVector<MainPipeSegment> result;

    if (!finalOutput || !bgReformat) {
        return result;
    }

    // Walk backward from finalOutput through input(0) to bgReformat,
    // collecting nodes in reverse order (bottom to top).
    QVector<NodePtr> walked;
    {
        NodePtr cur = finalOutput;
        QSet<Node*> visited;
        while (cur && cur != bgReformat) {
            if (visited.contains(cur.get())) break;
            visited.insert(cur.get());
            walked.push_back(cur);
            cur = cur->getInput(0);
        }
    }

    // walked is bottom→top. Build segments top→bottom.
    // The first segment (adjacent to bgReformat) has mergeNode == nullptr.
    MainPipeSegment currentSeg;
    currentSeg.mergeNode = NodePtr();  // no Merge yet (bgReformat side)

    // Iterate walked in reverse: top (closest to bgReformat) to bottom (finalOutput)
    for (int i = walked.size() - 1; i >= 0; --i) {
        NodePtr node = walked[i];
        const std::string pluginId = node->getPluginID();

        if (pluginId == PLUGINID_OFX_MERGE) {
            // Flush the current segment (if it has content or a merge already recorded)
            result.push_back(currentSeg);
            // Start a new segment with this Merge
            currentSeg = MainPipeSegment();
            currentSeg.mergeNode = node;
        } else {
            currentSeg.inlineNodes.push_back(node);
        }
    }

    // Flush the last segment (adjacent to finalOutput)
    result.push_back(currentSeg);

    return result;
}

/**
 * Helper: return true if the node is a Flux gizmo (FluxLayer / FluxSolid / FluxMotionText
 * NodeGroup). These are transform overlays — not inline effects.
 */
static bool
isFluxGizmoNode(const NodePtr& node)
{
    if (!node) {
        return false;
    }
    // Gizmo nodes are NodeGroup (PyPlug) instances
    if (!node->isEffectGroup()) {
        return false;
    }
    const std::string pid = node->getPluginID();
    return pid == "net.sf.openfx.FluxLayer" ||
           pid == "net.sf.openfx.FluxSolid" ||
           pid == "net.sf.openfx.FluxMotionText";
}

/**
 * Helper: return true if the node is a Read-type source node.
 */
static bool
isReadNode(const NodePtr& node)
{
    if (!node) {
        return false;
    }
    const std::string pid = node->getPluginID();
    return pid == PLUGINID_NATRON_READ ||
           pid == PLUGINID_NATRON_READQT;
}

QVector<NewAdjustmentEffect>
findNewMainPipeEffects(const QVector<MainPipeSegment>& segments,
                       const QSet<Node*>& tracked)
{
    QVector<NewAdjustmentEffect> result;

    for (int segIdx = 0; segIdx < segments.size(); ++segIdx) {
        const MainPipeSegment& seg = segments[segIdx];

        for (const NodePtr& node : seg.inlineNodes) {
            if (!node) {
                continue;
            }

            // Skip Merge nodes — they are layer boundaries, not effects
            if (node->getPluginID() == PLUGINID_OFX_MERGE) {
                continue;
            }

            // Skip if already tracked by an existing timeline layer
            if (tracked.contains(node.get())) {
                continue;
            }

            // New main-pipe effect — record it
            NewAdjustmentEffect entry;
            entry.node = node;
            entry.fx = createFluxEffectFromNode(node);
            entry.segmentIndex = segIdx;
            result.push_back(entry);
        }
    }

    return result;
}

QVector<FluxEffectWithMasks>
findNewEffects(NodePtr mergeNode, NodePtr bgReformat, const QSet<Node*>& tracked)
{
    QVector<FluxEffectWithMasks> result;

    if (!mergeNode) {
        return result;
    }

    // Start from the Merge node's input 1 (A = foreground / layer source)
    NodePtr cur = mergeNode->getInput(1);
    if (!cur) {
        return result;
    }

    // Collect nodes in the chain by walking backward through input(0)
    // until we hit a source, boundary, or already-tracked node.
    QVector<NodePtr> chain;
    {
        NodePtr walk = cur;
        QSet<Node*> visited;
        while (walk) {
            if (visited.contains(walk.get())) break;
            visited.insert(walk.get());

            // 1. Stop at a Read node (source boundary)
            if (isReadNode(walk)) {
                break;
            }
            // 2. Stop at a gizmo node
            if (isFluxGizmoNode(walk)) {
                break;
            }
            // 3. Stop at nodes internal to a gizmo NodeGroup
            if (dynamic_cast<NodeGroup*>(walk->getGroup().get())) {
                break;
            }
            // 4. Stop at the bgReformat boundary
            if (walk == bgReformat) {
                break;
            }
            // 5. Stop at another Merge node (precomp boundary)
            if (walk->getPluginID() == PLUGINID_OFX_MERGE) {
                break;
            }
            // 6. Skip already-tracked nodes — they belong to existing layers.
            //    Don't stop — there may be new effects further upstream.
            if (tracked.contains(walk.get())) {
                walk = walk->getInput(0);
                continue;
            }

            // This is a NEW effect
            chain.push_back(walk);
            walk = walk->getInput(0);
        }
    }

    // chain is ordered Merge→source (closest to Merge first).
    // Reverse to get source→Merge order.
    std::reverse(chain.begin(), chain.end());

    // Build FluxEffectWithMasks entries.
    for (int i = 0; i < chain.size(); ++i) {
        NodePtr node = chain[i];
        FluxEffectWithMasks entry;
        entry.effect = createFluxEffectFromNode(node);

        // Check for mask input using discoverMaskInput
        int maskInputIdx = discoverMaskInput(node);
        if (maskInputIdx >= 0) {
            // Walk the entire mask tree connected to this input
            NodePtr maskStart = node->getInput(maskInputIdx);
            if (maskStart) {
                // Collect all nodes in the mask tree by walking backward through inputs
                // Stop at: null, already visited, source boundaries (Read, gizmo-internal,
                // bgReformat, tracked node)
                QVector<NodePtr> maskTree;
                QSet<Node*> maskVisited;
                NodePtr walk = maskStart;
                while (walk) {
                    if (maskVisited.contains(walk.get())) break;
                    maskVisited.insert(walk.get());

                    // Stop at boundaries but include the boundary node first
                    bool isBoundary = false;
                    if (isReadNode(walk)) {
                        isBoundary = true;
                    }
                    // Stop at Merge nodes
                    if (walk->getPluginID() == PLUGINID_OFX_MERGE) {
                        isBoundary = true;
                    }
                    // Stop at Flux gizmo nodes
                    if (isFluxGizmoNode(walk)) {
                        isBoundary = true;
                    }
                    if (dynamic_cast<NodeGroup*>(walk->getGroup().get())) {
                        isBoundary = true;
                    }
                    if (walk == bgReformat) {
                        isBoundary = true;
                    }
                    if (tracked.contains(walk.get())) {
                        isBoundary = true;
                    }

                    maskTree.push_back(walk);

                    if (isBoundary) {
                        break;
                    }

                    walk = walk->getInput(0); // walk upstream
                }

                // Create a FluxMask for each node in the tree
                for (int m = 0; m < maskTree.size(); ++m) {
                    FluxMask mask;
                    mask.type = QString::fromUtf8("effect");
                    mask.effectIndex = i; // effect position in the list
                    mask.name = QString::fromUtf8("Flux Mask (") +
                                QString::fromStdString(maskTree[m]->getLabel()) +
                                QString::fromUtf8(")");
                    mask.maskNode = maskTree[m];
                    // reformatNode left null — we don't create one for nodegraph masks
                    entry.masks.push_back(mask);
                }
            }
        }

        result.push_back(entry);
    }

    return result;
}

QSet<Node*>
collectTrackedNodes(const QList<FluxLayer>& layers)
{
    QSet<Node*> tracked;

    for (const FluxLayer& layer : layers) {
        if (layer.mergeNode) {
            tracked.insert(layer.mergeNode.get());
        }
        if (layer.readerNode) {
            tracked.insert(layer.readerNode.get());
        }
        if (layer.gizmoNode) {
            tracked.insert(layer.gizmoNode.get());
        }
        if (layer.maskApplyNode) {
            tracked.insert(layer.maskApplyNode.get());
        }

        for (const FluxMask& mask : layer.masks) {
            if (mask.maskNode) {
                tracked.insert(mask.maskNode.get());
            }
            if (mask.reformatNode) {
                tracked.insert(mask.reformatNode.get());
            }
        }

        for (const FluxEffect& effect : layer.effects) {
            if (effect.node) {
                tracked.insert(effect.node.get());
            }
            if (effect.aiMaskReadNode) {
                tracked.insert(effect.aiMaskReadNode.get());
            }
            if (effect.aiMaskShuffleNode) {
                tracked.insert(effect.aiMaskShuffleNode.get());
            }
            if (effect.aiMaskChannelMergeNode) {
                tracked.insert(effect.aiMaskChannelMergeNode.get());
            }
        }
    }

    return tracked;
}

NATRON_NAMESPACE_EXIT
