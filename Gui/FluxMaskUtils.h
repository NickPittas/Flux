/* ***** BEGIN LICENSE BLOCK *****
 * Flux — Mask & Branch Discovery Utilities
 * (C) 2025 Nick Pittas
 * GPL2 — see LICENSE.txt
 * ***** END LICENSE BLOCK ***** */

#ifndef FLUXMASKUTILS_H
#define FLUXMASKUTILS_H

#include "Global/Macros.h"

#include "Gui/FluxTimeline.h"

#include <QList>
#include <QSet>

NATRON_NAMESPACE_ENTER

enum FluxBranchKind {
    eFluxBranchMainPipe,
    eFluxBranchMask,
    eFluxBranchPrecomp,
    eFluxBranchUnknown
};

struct FluxDiscoveredInput {
    int inputIndex;
    NodePtr node;
    FluxBranchKind kind;

    FluxDiscoveredInput()
        : inputIndex(-1)
        , kind(eFluxBranchUnknown)
    {}
};

struct FluxLayerBranchClassification {
    QList<NodePtr> mainPipeNodes;
    QList<NodePtr> maskBranchNodes;
    QList<NodePtr> precompBranchNodes;
    QList<NodePtr> unknownNodes;
    bool hasPrecompBranch;

    FluxLayerBranchClassification()
        : hasPrecompBranch(false)
    {}
};

/** @brief Return the mask-capable input index for a node, or -1 if none. */
int discoverMaskInput(const NodePtr& node);

/** @brief Return true if the node is a Premult operation (not Unpremult). */
bool isPremultNode(const NodePtr& node);

/** @brief Return true if the node is an Unpremult operation (not Premult). */
bool isUnpremultNode(const NodePtr& node);

/** @brief Return true if the node is a Roto or RotoPaint node. */
bool isRotoMaskNode(const NodePtr& node);

/** @brief Classify all connected nodes for a layer into main/mask/precomp branches. */
FluxLayerBranchClassification classifyLayerBranches(const FluxLayer& layer);

NATRON_NAMESPACE_EXIT

#endif // FLUXMASKUTILS_H
