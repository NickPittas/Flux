/* ***** BEGIN LICENSE BLOCK *****
 * Flux — Mask & Branch Discovery Utilities
 * (C) 2025 Nick Pittas
 * GPL2 — see LICENSE.txt
 * ***** END LICENSE BLOCK ***** */

#include "Gui/FluxMaskUtils.h"

#include "Engine/Node.h"
#include "Engine/EffectInstance.h"

#include <algorithm>
#include <string>

NATRON_NAMESPACE_ENTER

namespace {

QString
toLower(const std::string& s)
{
    return QString::fromStdString(s).toLower();
}

bool
containsNode(const QList<NodePtr>& list,
             const NodePtr& node)
{
    if (!node) {
        return false;
    }
    for (const NodePtr& n : list) {
        if (n.get() == node.get()) {
            return true;
        }
    }
    return false;
}

void
addUniqueNode(QList<NodePtr>* list,
              const NodePtr& node)
{
    if (!node || !list) {
        return;
    }
    if (!containsNode(*list, node)) {
        list->append(node);
    }
}

void
collectUpstreamBranch(const NodePtr& start,
                       QList<NodePtr>* out,
                       QSet<Node*>* visited,
                       const QSet<Node*>* stopNodes = nullptr)
{
    if (!start || !out || !visited) {
        return;
    }
    Node* raw = start.get();
    if (visited->contains(raw)) {
        return;
    }
    // Stop at known boundary nodes — do NOT add them to result
    if (stopNodes && stopNodes->contains(raw)) {
        return;
    }
    visited->insert(raw);
    addUniqueNode(out, start);

    int nInputs = start->getNInputs();
    for (int i = 0; i < nInputs; ++i) {
        NodePtr input = start->getInput(i);
        if (input) {
            collectUpstreamBranch(input, out, visited, stopNodes);
        }
    }
}

} // anonymous namespace

int
discoverMaskInput(const NodePtr& node)
{
    if (!node) {
        return -1;
    }

    EffectInstancePtr effect = node->getEffectInstance();
    int nInputs = node->getNInputs();

    // Primary: use isInputMask from EffectInstance
    if (effect) {
        for (int i = 0; i < nInputs; ++i) {
            if (effect->isInputMask(i)) {
                return i;
            }
        }
    }

    // Fallback: check input label for mask/matte/alpha keywords
    for (int i = 0; i < nInputs; ++i) {
        std::string labelStr = node->getInputLabel(i);
        QString label = toLower(labelStr);

        // Skip common non-mask input labels
        if (label == QLatin1String("source") ||
            label == QLatin1String("fg") ||
            label == QLatin1String("a") ||
            label == QLatin1String("b") ||
            label == QLatin1String("bg")) {
            continue;
        }

        if (label.contains(QLatin1String("mask")) ||
            label.contains(QLatin1String("matte")) ||
            label.contains(QLatin1String("alpha"))) {
            return i;
        }
    }

    return -1;
}

bool
isPremultNode(const NodePtr& node)
{
    if (!node) {
        return false;
    }

    std::string pluginIdStr = node->getPluginID();
    QString pluginId = toLower(pluginIdStr);

    std::string labelStr = node->getLabel();
    QString label = toLower(labelStr);

    bool hasPremult = pluginId.contains(QLatin1String("premult")) ||
                      label.contains(QLatin1String("premult"));
    bool hasUnpremult = pluginId.contains(QLatin1String("unpremult")) ||
                        label.contains(QLatin1String("unpremult"));

    return hasPremult && !hasUnpremult;
}

bool
isUnpremultNode(const NodePtr& node)
{
    if (!node) {
        return false;
    }

    std::string pluginIdStr = node->getPluginID();
    QString pluginId = toLower(pluginIdStr);

    std::string labelStr = node->getLabel();
    QString label = toLower(labelStr);

    return pluginId.contains(QLatin1String("unpremult")) ||
           label.contains(QLatin1String("unpremult"));
}

bool
isRotoMaskNode(const NodePtr& node)
{
    if (!node) {
        return false;
    }

    std::string pluginIdStr = node->getPluginID();
    QString pluginId = toLower(pluginIdStr);

    std::string labelStr = node->getLabel();
    QString label = toLower(labelStr);

    return pluginId.contains(QLatin1String("roto")) ||
           label.contains(QLatin1String("roto"));
}

// DFS path-finder: search from `current` to `expectedSource` via non-mask inputs.
// On success, populates `path` with nodes on the path (excluding expectedSource).
// `stopNodes` prevents traversal past known boundaries.
static bool
findInlinePathToSource(const NodePtr& current,
                        const NodePtr& expectedSource,
                        const QSet<Node*>& stopNodes,
                        QSet<Node*>* visited,
                        QList<NodePtr>* path)
{
    if (!current || !expectedSource) {
        return false;
    }
    if (current.get() == expectedSource.get()) {
        return true;
    }
    if (stopNodes.contains(current.get())) {
        return false;
    }
    if (visited->contains(current.get())) {
        return false;
    }
    visited->insert(current.get());

    int maskInput = discoverMaskInput(current);

    for (int i = 0; i < current->getNInputs(); ++i) {
        if (i == maskInput) {
            continue;
        }
        NodePtr input = current->getInput(i);
        if (!input) {
            continue;
        }
        if (findInlinePathToSource(input, expectedSource, stopNodes, visited, path)) {
            path->prepend(current);
            return true;
        }
    }
    return false;
}

// Collect inline manual nodes between `start` and `expectedSource`.
// Only nodes on the actual path are added to `out`.
// `stopNodes` prevents traversal past known main-pipe boundaries.
static void
collectInlinePath(const NodePtr& start,
                   const NodePtr& expectedSource,
                   const QSet<Node*>& stopNodes,
                   QList<NodePtr>* out)
{
    if (!start || !expectedSource || !out) {
        return;
    }
    QSet<Node*> visited;
    QList<NodePtr> path;
    if (findInlinePathToSource(start, expectedSource, stopNodes, &visited, &path)) {
        for (const NodePtr& n : path) {
            addUniqueNode(out, n);
        }
    }
}

FluxLayerBranchClassification
classifyLayerBranches(const FluxLayer& layer)
{
    FluxLayerBranchClassification result;

    // Build known main pipe nodes
    QList<NodePtr> mainPipe;
    if (layer.readerNode) {
        addUniqueNode(&mainPipe, layer.readerNode);
    }
    if (layer.gizmoNode) {
        addUniqueNode(&mainPipe, layer.gizmoNode);
    }
    for (int e = 0; e < layer.effects.size(); ++e) {
        if (layer.effects[e].node) {
            addUniqueNode(&mainPipe, layer.effects[e].node);
        }
    }

    // --- Layer mask inline nodes (T064) ---
    // Layer masks are inline: source -> [Unpremult] -> Roto -> Premult -> downstream
    // maskApplyNode is the inline Premult for layer masks.
    // Layer mask Roto (maskNode for effectIndex < 0) is main pipe.
    // Effect mask nodes (effectIndex >= 0) remain mask branch nodes.
    bool hasInlineLayerMask = false;
    NodePtr layerMaskRoto;
    NodePtr layerMaskPremult; // = maskApplyNode for layer masks
    NodePtr layerMaskUnpremult; // discovered, not stored in model

    for (int m = 0; m < layer.masks.size(); ++m) {
        if (layer.masks[m].enabled && layer.masks[m].effectIndex < 0 && layer.masks[m].maskNode) {
            hasInlineLayerMask = true;
            layerMaskRoto = layer.masks[m].maskNode;
            break;
        }
    }
    if (hasInlineLayerMask && layer.maskApplyNode) {
        layerMaskPremult = layer.maskApplyNode;
    }
    // Discover optional inline Unpremult between expected source and Roto
    if (hasInlineLayerMask && layerMaskRoto) {
        NodePtr rotoInput0 = layerMaskRoto->getInput(0);
        if (rotoInput0 && isUnpremultNode(rotoInput0)) {
            layerMaskUnpremult = rotoInput0;
            addUniqueNode(&mainPipe, layerMaskUnpremult);
        }
    }
    if (hasInlineLayerMask && layerMaskRoto) {
        addUniqueNode(&mainPipe, layerMaskRoto);
    }
    if (layerMaskPremult) {
        addUniqueNode(&mainPipe, layerMaskPremult);
    }

    if (layer.mergeNode) {
        addUniqueNode(&mainPipe, layer.mergeNode);
    }

    // Expand mainPipe with inline manual nodes between expected Flux-owned nodes.
    struct ExpectedSegment {
        NodePtr target;
        NodePtr expectedSource;
        int targetInputIndex;
    };
    QList<ExpectedSegment> segments;

    // Source-pipe base: prefer gizmoNode over readerNode for segment discovery.
    NodePtr sourceNode = layer.gizmoNode ? layer.gizmoNode : layer.readerNode;

    // source → first effect
    if (!layer.effects.isEmpty()) {
        if (layer.effects[0].node && sourceNode) {
            segments.push_back({layer.effects[0].node, sourceNode, 0});
        }
        // effect[i-1] → effect[i]
        for (int e = 1; e < layer.effects.size(); ++e) {
            if (layer.effects[e].node && layer.effects[e - 1].node) {
                segments.push_back({layer.effects[e].node, layer.effects[e - 1].node, 0});
            }
        }
    }

    // Terminal segments after effects
    // Last effect output or sourceNode if no effects
    NodePtr lastSource = sourceNode;
    if (!layer.effects.isEmpty() && layer.effects.last().node) {
        lastSource = layer.effects.last().node;
    }

    if (hasInlineLayerMask) {
        // Inline layer mask chain:
        //   lastSource → [Unpremult input 0] (optional)
        //   [Unpremult or lastSource] → Roto input 0
        //   Roto → Premult (maskApplyNode) input 0
        //   Premult → merge input 1
        NodePtr rotoExpectedSource = lastSource;
        if (layerMaskUnpremult) {
            segments.push_back({layerMaskUnpremult, lastSource, 0});
            rotoExpectedSource = layerMaskUnpremult;
        }
        if (layerMaskRoto) {
            segments.push_back({layerMaskRoto, rotoExpectedSource, 0});
        }
        if (layerMaskPremult && layerMaskRoto) {
            segments.push_back({layerMaskPremult, layerMaskRoto, 0});
        }
        NodePtr preMerge = layerMaskPremult ? layerMaskPremult : layerMaskRoto;
        if (layer.mergeNode && preMerge) {
            segments.push_back({layer.mergeNode, preMerge, 1});
        }
    } else {
        // No inline layer mask — direct to merge
        if (layer.mergeNode && lastSource) {
            segments.push_back({layer.mergeNode, lastSource, 1});
        }
    }

    // Build initial stop set from known Flux-owned nodes
    QSet<Node*> mainPipeStop;
    for (const NodePtr& n : mainPipe) {
        if (n) {
            mainPipeStop.insert(n.get());
        }
    }

    // For each segment, check target's specified input for inline manual nodes
    for (int s = 0; s < segments.size(); ++s) {
        const ExpectedSegment& seg = segments[s];
        NodePtr target = seg.target;
        NodePtr expected = seg.expectedSource;
        if (!target || !expected) continue;

        NodePtr currentInput = target->getInput(seg.targetInputIndex);
        if (!currentInput || currentInput == expected) continue;
        if (containsNode(mainPipe, currentInput)) continue;

        QList<NodePtr> inlineNodes;
        collectInlinePath(currentInput, expected, mainPipeStop, &inlineNodes);
        for (const NodePtr& n : inlineNodes) {
            addUniqueNode(&mainPipe, n);
            mainPipeStop.insert(n.get());
        }
    }

    result.mainPipeNodes = mainPipe;

    // Build known mask source nodes — only EFFECT masks (effectIndex >= 0).
    // Layer mask Roto/Premult are main pipe, NOT mask branch.
    QSet<Node*> knownMaskNodes;
    for (int m = 0; m < layer.masks.size(); ++m) {
        if (layer.masks[m].effectIndex >= 0) {
            if (layer.masks[m].maskNode) {
                knownMaskNodes.insert(layer.masks[m].maskNode.get());
            }
            if (layer.masks[m].reformatNode) {
                knownMaskNodes.insert(layer.masks[m].reformatNode.get());
            }
        }
    }

    // Inspect inputs of each main pipe node
    for (const NodePtr& mainNode : mainPipe) {
        if (!mainNode) {
            continue;
        }

        int maskInput = discoverMaskInput(mainNode);
        int nInputs = mainNode->getNInputs();

        for (int i = 0; i < nInputs; ++i) {
            NodePtr input = mainNode->getInput(i);
            if (!input) {
                continue;
            }

            // Skip merge input 0: that is the previous composite/background
            if (mainNode == layer.mergeNode && i == 0) {
                continue;
            }

            // Skip nodes already in the main pipe
            if (containsNode(mainPipe, input)) {
                continue;
            }

            // Skip known effect-mask source nodes — they are mask branches
            if (knownMaskNodes.contains(input.get())) {
                continue;
            }

            // Classify by input index
            if (i == maskInput) {
                QSet<Node*> visitedLocal;
                collectUpstreamBranch(input, &result.maskBranchNodes, &visitedLocal, &mainPipeStop);
            } else {
                // Build stop set that includes mask branch nodes and known mask nodes
                QSet<Node*> combinedStop = mainPipeStop;
                for (const NodePtr& mn : result.maskBranchNodes) {
                    if (mn) combinedStop.insert(mn.get());
                }
                combinedStop.unite(knownMaskNodes);
                QSet<Node*> visitedLocal;
                collectUpstreamBranch(input, &result.precompBranchNodes, &visitedLocal, &combinedStop);
            }
        }
    }

    // Remove any mask branch nodes / known mask nodes that accidentally ended up in precomp
    QSet<Node*> excludeFromPrecomp;
    for (const NodePtr& mn : result.maskBranchNodes) {
        if (mn) excludeFromPrecomp.insert(mn.get());
    }
    excludeFromPrecomp.unite(knownMaskNodes);
    for (Node* raw : excludeFromPrecomp) {
        for (int j = result.precompBranchNodes.size() - 1; j >= 0; --j) {
            if (result.precompBranchNodes[j].get() == raw) {
                result.precompBranchNodes.removeAt(j);
            }
        }
    }

    result.hasPrecompBranch = !result.precompBranchNodes.isEmpty();

    return result;
}

NATRON_NAMESPACE_EXIT
