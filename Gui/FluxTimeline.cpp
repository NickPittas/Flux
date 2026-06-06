/* ***** BEGIN LICENSE BLOCK *****
 * Flux — Layer-based Timeline Widget
 * (C) 2025 Nick Pittas
 * GPL2 — see LICENSE.txt
 * ***** END LICENSE BLOCK ***** */

#include "Gui/FluxTimeline.h"

#include <QPainter>
#include <QPainterPath>
#include <QFontMetrics>
#include <QCursor>
#include <QApplication>
#include <QFileInfo>
#include <QDir>
#include <QMimeData>
#include <QUrl>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QDrag>
#include <QKeyEvent>
#include <QShortcut>

#include <algorithm>
#include <cmath>

#include "Gui/Gui.h"
#include "Gui/GuiAppInstance.h"
#include "Gui/FluxEffectsPanel.h"
#include "Gui/FluxStyleUtils.h"
#include "Gui/FluxTextAnimatorModel.h"
#include "Gui/NodeGraph.h"
#include "Gui/NodeCreationDialog.h"
#include "Gui/NodeClipBoard.h"
#include "Gui/NodeGui.h"
#include "Gui/NodeSettingsPanel.h"
#include "Gui/DockablePanel.h"
#include "Engine/Project.h"
#include "Engine/AppInstance.h"
#include "Engine/Node.h"
#include "Engine/NodeGroup.h"
#include "Engine/EffectInstance.h"
#include "Engine/Knob.h"
#include "Engine/KnobTypes.h"
#include "Engine/Curve.h"
#include "Engine/Bezier.h"
#include "Engine/RotoContext.h"
#include "Engine/ViewIdx.h"
#include "Engine/CreateNodeArgs.h"
#include "Engine/ImagePlaneDesc.h"
#include "Gui/ViewerTab.h"
#include "Gui/GuiApplicationManager.h"
#include "Engine/ViewerInstance.h"
#include "Gui/FluxTimelineSerialization.h"
#include "Gui/FluxMaskUtils.h"
#include "Gui/FluxKeyframeModel.h"

NATRON_NAMESPACE_ENTER

namespace {

void syncTextAnimatorPropertyIfNeeded(const FluxKeyframeProperty& prop)
{
    if (prop.ownerNode && prop.knob && FluxTextAnimatorModel::isAnimatorKnobName(prop.knob->getName())) {
        FluxTextAnimatorModel::syncAnimatorStackToRenderer(prop.ownerNode);
    }
}

void deactivateFluxOwnedNode(const NodePtr& node)
{
    if (node && node->isActivated()) {
        node->deactivate(std::list<NodePtr>(), true, false, true);
    }
}

void deactivateFluxEffectOwnedNodes(FluxEffect& effect)
{
    if (effect.aiMaskTimeOffsetNode && effect.aiMaskTimeOffsetNode != effect.node) {
        deactivateFluxOwnedNode(effect.aiMaskTimeOffsetNode);
        effect.aiMaskTimeOffsetNode.reset();
    }
    if (effect.aiMaskShuffleNode && effect.aiMaskShuffleNode != effect.node) {
        deactivateFluxOwnedNode(effect.aiMaskShuffleNode);
        effect.aiMaskShuffleNode.reset();
    }
    if (effect.aiMaskReadNode && effect.aiMaskReadNode != effect.node) {
        deactivateFluxOwnedNode(effect.aiMaskReadNode);
        effect.aiMaskReadNode.reset();
    }
    if (effect.aiMaskChannelMergeNode && effect.aiMaskChannelMergeNode != effect.node) {
        deactivateFluxOwnedNode(effect.aiMaskChannelMergeNode);
        effect.aiMaskChannelMergeNode.reset();
    }
    deactivateFluxOwnedNode(effect.node);
    effect.node.reset();
    effect.aiMaskChannelMergeNode.reset();
}

}

FluxTimeline::FluxTimeline(Gui* gui,
                           QWidget* parent)
    : QWidget(parent)
      , PanelWidget(this, gui)
      , _totalContentHeight(0)
      , _firstFrame(0)
      , _lastFrame(100)
      , _currentFrame(0)
      , _selectedLayer(-1)
      , _selectedType(eFluxSelectionNone)
      , _selectedEffectIndex(-1)
      , _selectedMaskIndex(-1)
      , _selectedPropertyIndex(-1)
      , _selectedKeyPropertyIndex(-1)
      , _selectedKeyTime(0.0)
      , _rubberBandStart(0, 0)
      , _rubberBandCurrent(0, 0)
      , _zoom(10.0)
    , _scrollOffsetX(0)
    , _scrollOffsetY(0)
    , _layerLabelWidth(180)
    , _interactionMode(eModeNone)
      , _interactionLayerIndex(-1)
      , _interactionStartX(0)
      , _interactionStartY(0)
       , _interactionOrigInPoint(0)
      , _interactionOrigOutPoint(0)
      , _interactionOrigTimeOffset(0)
      , _interactionOrigTrimStart(0)
      , _interactionOrigTrimEnd(0)
      , _reorderTargetRow(-1)
    , _panStartScrollX(0)
    , _panStartScrollY(0)
    , _resizeStartX(0)
    , _resizeStartWidth(0)
    , _dragPropertyIndex(-1)
    , _dragOrigKeyTime(0.0)
    , _dragCurrentKeyTime(0.0)
    , _snapTolerancePixels(10)
    , _snapActive(false)
    , _snapFrame(0)
    , _snapKind(eSnapNone)
    , _snapShiftHeld(false)
    , _showKeyframeCurves(false)
    , _ungroupedKeyframeProperties()
    , _rowsDirty(false)
    , _reorderEffectLayerIndex(-1)
    , _reorderEffectFromIndex(-1)
    , _reorderEffectTargetIndex(-1)
    , _isDragOver(false)
      , _dragPreviewPos()
      , _timeline()
{
    setObjectName( QString::fromUtf8("FluxTimeline") );
    setMinimumHeight(120);
    setMouseTracking(true);
    setFocusPolicy(Qt::ClickFocus);
    setAcceptDrops(true);

    // T019-C: Connect to the app's shared TimeLine for playhead sync
    if (gui && gui->getApp()) {
        _timeline = gui->getApp()->getTimeLine();
        if (_timeline) {
            QObject::connect(_timeline.get(), SIGNAL(frameChanged(SequenceTime,int)),
                             this, SLOT(onExternalFrameChanged(SequenceTime,int)));
        }
    }

    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    // Ctrl+Shift+Y shortcut to trigger nodegraph sync
    QShortcut* syncShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Y), this);
    syncShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    QObject::connect(syncShortcut, &QShortcut::activated, this, [this]() {
        Q_EMIT nodegraphSyncRequested();
    });
}

FluxTimeline::~FluxTimeline()
{
}

void
FluxTimeline::addLayer(const QString& name,
                       const QString& filePath,
                       const QString& type)
{
    FluxLayer layer;
    layer.name = name;
    layer.filePath = filePath;
    layer.type = type;
    layer.inPoint = _firstFrame;
    layer.outPoint = _lastFrame;

    // Keep the timeline visually coherent: all bars live in the same blue family
    // while still varying slightly by layer type.
    if (type == QString::fromUtf8("footage")) {
        layer.color = QColor(95, 137, 216);
    } else if (type == QString::fromUtf8("solid")) {
        layer.color = QColor(86, 132, 198);
    } else if (type == QString::fromUtf8("text")) {
        layer.color = QColor(108, 151, 223);
    } else if (type == QString::fromUtf8("adjustment")) {
        layer.color = QColor(118, 138, 186);
    } else {
        layer.color = QColor(92, 104, 124);
    }

    _layers.append(layer);
    rebuildVisibleRows();
    Q_EMIT compositingChanged();
    update();
}

bool
FluxTimeline::insertGeneratedFootageLayerAboveSelected(const QString& name,
                                                       const QString& filePath,
                                                       QString* message)
{
    int targetLayer = _selectedLayer;
    if (targetLayer < 0 || targetLayer >= _layers.size()) {
        if (message) { *message = QString::fromUtf8("Import generated footage failed: select the source layer first."); }
        return false;
    }
    if (filePath.isEmpty()) {
        if (message) { *message = QString::fromUtf8("Import generated footage failed: generated footage path is empty."); }
        return false;
    }

    FluxLayer layer;
    layer.name = name.isEmpty() ? QFileInfo(filePath).fileName() : name;
    layer.filePath = filePath;
    layer.type = QString::fromUtf8("footage");
    layer.inPoint = _layers[targetLayer].inPoint;
    layer.outPoint = _layers[targetLayer].outPoint;
    layer.originalInPoint = _layers[targetLayer].originalInPoint;
    layer.originalOutPoint = _layers[targetLayer].originalOutPoint;
    layer.originalFirstFrame = _layers[targetLayer].originalFirstFrame;
    layer.originalLastFrame = _layers[targetLayer].originalLastFrame;
    layer.timeOffset = _layers[targetLayer].timeOffset;
    layer.trimStart = _layers[targetLayer].trimStart;
    layer.trimEnd = _layers[targetLayer].trimEnd;
    layer.sourceFrameRate = _layers[targetLayer].sourceFrameRate;
    layer.color = QColor(122, 158, 224);

    const int newIndex = targetLayer;
    _layers.insert(newIndex, layer);
    _selectedType = eFluxSelectionLayer;
    _selectedLayer = newIndex;
    _selectedEffectIndex = -1;
    _selectedMaskIndex = -1;
    _selectedPropertyIndex = -1;

    rebuildVisibleRows();
    Q_EMIT compositingChanged();
    Q_EMIT layerSelected(newIndex);
    update();
    if (message) { *message = QString::fromUtf8("Imported generated footage layer above the selected source layer."); }
    return true;
}

void
FluxTimeline::addSolidLayer(const QColor& color)
{
    FluxLayer layer;
    layer.name = QString::fromUtf8("Solid");
    layer.type = QString::fromUtf8("solid");
    layer.inPoint = _firstFrame;
    layer.outPoint = _lastFrame;
    layer.color = QColor(86, 132, 198);
    // Store the solid color as a QString for later use by the gizmo
    // (actual color is set on the Constant node's "color" knob)
    layer.solidColor = color;

    _layers.append(layer);
    rebuildVisibleRows();
    Q_EMIT compositingChanged();
    update();
}

void
FluxTimeline::addTextLayer()
{
    FluxLayer layer;
    layer.name = QString::fromUtf8("Text");
    layer.type = QString::fromUtf8("text");
    layer.inPoint = _firstFrame;
    layer.outPoint = _lastFrame;
    layer.color = QColor(108, 151, 223);

    int newIndex = _layers.size();
    _layers.append(layer);

    // Auto-select the new text layer so the GUI (Text panel, Properties)
    // can bind to it once rebuildCompositingGraph creates the gizmoNode.
    _selectedType = eFluxSelectionLayer;
    _selectedLayer = newIndex;
    _selectedEffectIndex = -1;
    _selectedMaskIndex = -1;
    _selectedPropertyIndex = -1;

    rebuildVisibleRows();
    Q_EMIT compositingChanged();
    Q_EMIT layerSelected(newIndex);
    update();
}

void
FluxTimeline::addNullLayer()
{
    FluxLayer layer;
    layer.name = QString::fromUtf8("Null");
    layer.type = QString::fromUtf8("null");
    layer.inPoint = _firstFrame;
    layer.outPoint = _lastFrame;
    layer.color = QColor(92, 104, 124);
    _layers.append(layer);
    rebuildVisibleRows();
    Q_EMIT compositingChanged();
    update();
}

void
FluxTimeline::duplicateSelectedLayer()
{
    if (_selectedType == eFluxSelectionLayer) {
        duplicateLayer(_selectedLayer);
    }
}

void
FluxTimeline::splitSelectedLayer()
{
    if (_selectedType == eFluxSelectionLayer && canSplitRow(_selectedLayer)) {
        splitLayer(_selectedLayer, _currentFrame);
    }
}

void
FluxTimeline::deleteSelectedLayer()
{
    if (_selectedType == eFluxSelectionEffect &&
        _selectedLayer >= 0 && _selectedLayer < _layers.size() &&
        _selectedEffectIndex >= 0) {
        removeEffectFromLayer(_selectedLayer, _selectedEffectIndex);
        return;
    }

    if (_selectedType == eFluxSelectionMask &&
        _selectedLayer >= 0 && _selectedLayer < _layers.size() &&
        _selectedMaskIndex >= 0) {
        removeMaskFromLayer(_selectedLayer, _selectedMaskIndex);
        return;
    }

    if (_selectedType == eFluxSelectionLayer &&
        _selectedLayer >= 0 && _selectedLayer < _layers.size()) {
        removeLayer(_selectedLayer);
    }
}

void
FluxTimeline::addEffectToSelectedLayer()
{
    if (_selectedType != eFluxSelectionLayer || !canAddEffectToRow(_selectedLayer)) {
        return;
    }

    Q_EMIT layerSelected(_selectedLayer);
    showNodeCreationDialog();
    update();
}

void
FluxTimeline::addMaskToSelectedRow()
{
    if (_selectedType == eFluxSelectionEffect &&
        _selectedLayer >= 0 && _selectedLayer < _layers.size() &&
        canAddEffectMask(_selectedLayer, _selectedEffectIndex)) {
        addEffectMask(_selectedLayer, _selectedEffectIndex);
        return;
    }

    if (_selectedType == eFluxSelectionLayer &&
        _selectedLayer >= 0 && _selectedLayer < _layers.size() &&
        !_layers[_selectedLayer].locked) {
        addLayerMask(_selectedLayer);
    }
}

void
FluxTimeline::addAdjustmentEffectRow()
{
    _selectedType = eFluxSelectionNone;
    _selectedLayer = -1;
    _selectedEffectIndex = -1;
    _selectedMaskIndex = -1;
    _selectedPropertyIndex = -1;
    Q_EMIT layerSelected(-1);
    showNodeCreationDialog();
    update();
}

void
FluxTimeline::removeLayer(int index)
{
    if (index >= 0 && index < _layers.size() && !_layers[index].locked) {
        clearAllViewerInputBadges();
        FluxLayer& layer = _layers[index];
        // Deactivate mask nodes before removing layer
        for (FluxMask& mask : layer.masks) {
            if (mask.maskNode && mask.maskNode->isActivated()) {
                mask.maskNode->deactivate(std::list<NodePtr>(), false, true);
            }
            if (mask.reformatNode && mask.reformatNode->isActivated()) {
                mask.reformatNode->deactivate(std::list<NodePtr>(), false, true);
            }
        }
        if (layer.maskApplyNode && layer.maskApplyNode->isActivated()) {
            layer.maskApplyNode->deactivate(std::list<NodePtr>(), false, true);
        }
        // Deactivate nodes from the node graph
        if (layer.mergeNode) {
            deactivateFluxOwnedNode(layer.mergeNode);
            layer.mergeNode.reset();
        }
        for (int e = 0; e < layer.effects.size(); ++e) {
            deactivateFluxEffectOwnedNodes(layer.effects[e]);
        }
        if (layer.gizmoNode) {
            deactivateFluxOwnedNode(layer.gizmoNode);
            layer.gizmoNode.reset();
        }
        if (layer.readerNode) {
            deactivateFluxOwnedNode(layer.readerNode);
            layer.readerNode.reset();
        }
        _layers.removeAt(index);
        rebuildVisibleRows();
        if (_selectedLayer == index) {
            _selectedLayer = -1;
            _selectedType = eFluxSelectionNone;
            _selectedEffectIndex = -1;
            _selectedMaskIndex = -1;
            _selectedPropertyIndex = -1;
            Q_EMIT layerSelected(-1);
        } else if (_selectedLayer > index) {
            --_selectedLayer;
        }
        Q_EMIT compositingChanged();
        update();
    }
}

bool
FluxTimeline::duplicateLayer(int index)
{
    if (!canDuplicateRow(index)) {
        return false;
    }

    clearAllViewerInputBadges();

    const FluxLayer& layer = _layers[index];

    Gui* gui = getGui();
    if (!gui) {
        return false;
    }
    NodeGraph* nodeGraph = gui->getNodeGraph();
    if (!nodeGraph) {
        return false;
    }

    const bool isAdjustment = (layer.type == QString::fromUtf8("adjustment"));

    // ---- Adjustment row duplicate ----
    if (isAdjustment) {
        // Adjustment rows have only effect nodes (no gizmo/merge/read).
        // Copy each effect via clipboard, create new adjustment row with pasted effects.
        if (layer.effects.isEmpty()) {
            // Nothing to duplicate
            return false;
        }

        NodesGuiList nodesToCopy;
        for (int e = 0; e < layer.effects.size(); ++e) {
            if (layer.effects[e].node) {
                NodeGuiIPtr eguiI = layer.effects[e].node->getNodeGui();
                NodeGuiPtr egui = std::dynamic_pointer_cast<NodeGui>(eguiI);
                if (egui) {
                    nodesToCopy.push_back(egui);
                }
            }
        }

        if (nodesToCopy.empty()) {
            return false;
        }

        NodeClipBoard clipboard;
        nodeGraph->copyNodes(nodesToCopy, clipboard);

        if (clipboard.nodes.size() != nodesToCopy.size()) {
            fprintf(stderr, "FLUX ERROR: duplicateLayer(adj) — clipboard has %zu nodes, expected %zu\n",
                    clipboard.nodes.size(), nodesToCopy.size());
            return false;
        }

        std::list<std::pair<std::string, NodeGuiPtr>> newNodes;
        nodeGraph->pasteCliboard(clipboard, &newNodes);

        if (newNodes.size() != nodesToCopy.size()) {
            fprintf(stderr, "FLUX ERROR: duplicateLayer(adj) — paste returned %zu nodes, expected %zu\n",
                    newNodes.size(), nodesToCopy.size());
            return false;
        }

        // Match pasted nodes to original effects by plugin ID (order preserved by clipboard)
        QList<FluxEffect> newEffects;
        auto pasteIt = newNodes.begin();
        for (int e = 0; e < layer.effects.size() && pasteIt != newNodes.end(); ++e) {
            if (!layer.effects[e].node) {
                continue; // skip effects with null nodes
            }
            NodePtr pastedNode = pasteIt->second->getNode();
            if (pastedNode) {
                FluxEffect fe;
                fe.pluginId = layer.effects[e].pluginId;
                fe.label = QString::fromStdString(pastedNode->getLabel());
                fe.node = pastedNode;
                fe.enabled = layer.effects[e].enabled;
                newEffects.append(fe);
            }
            ++pasteIt;
        }

        // Create new adjustment row
        FluxLayer duplicate;
        duplicate.name = layer.name + QString::fromUtf8(" copy");
        duplicate.type = QString::fromUtf8("adjustment");
        duplicate.muted = layer.muted;
        duplicate.locked = false;
        duplicate.solo = false;
        duplicate.color = layer.color;
        duplicate.effects = newEffects;
        // Copy range state so trim/move work correctly on the duplicate
        duplicate.inPoint = layer.inPoint;
        duplicate.outPoint = layer.outPoint;
        duplicate.originalInPoint = layer.originalInPoint;
        duplicate.originalOutPoint = layer.originalOutPoint;
        duplicate.originalFirstFrame = layer.originalFirstFrame;
        duplicate.originalLastFrame = layer.originalLastFrame;
        duplicate.timeOffset = layer.timeOffset;
        duplicate.nodeInitialized = true;

        _layers.insert(index, duplicate);

        if (_selectedLayer >= index) {
            ++_selectedLayer;
        }

        fprintf(stderr, "FLUX DUPLICATE: adjustment row '%s' duplicated with %lld effects at index %d\n",
                layer.name.toStdString().c_str(), static_cast<long long>(newEffects.size()), index);

        rebuildVisibleRows();
        Q_EMIT compositingChanged();
        update();
        return true;
    }

    // ---- Footage/solid row duplicate (branch-aware, T063) ----

    // 1. Collect all layer-owned nodes for branch-aware copy.
    // Start with explicit Flux model refs, then add discovered nodes from classifier.
    QList<NodePtr> nodesToCopyRaw;
    QSet<Node*> seenNodes;

    auto addNodeRaw = [&](const NodePtr& n) {
        if (n && !seenNodes.contains(n.get())) {
            seenNodes.insert(n.get());
            nodesToCopyRaw.append(n);
        }
    };

    // Explicit model refs (in order for stable clipboard serialization)
    addNodeRaw(layer.readerNode);
    addNodeRaw(layer.gizmoNode);
    for (int e = 0; e < layer.effects.size(); ++e) {
        addNodeRaw(layer.effects[e].node);
    }
    addNodeRaw(layer.maskApplyNode);
    for (int m = 0; m < layer.masks.size(); ++m) {
        addNodeRaw(layer.masks[m].reformatNode);
        addNodeRaw(layer.masks[m].maskNode);
    }
    addNodeRaw(layer.mergeNode);

    // Discovered nodes from classifier (inline manual, mask branch, precomp branch)
    {
        FluxLayerBranchClassification cls = classifyLayerBranches(layer);
        for (const NodePtr& n : cls.mainPipeNodes) { addNodeRaw(n); }
        for (const NodePtr& n : cls.maskBranchNodes) { addNodeRaw(n); }
        for (const NodePtr& n : cls.precompBranchNodes) { addNodeRaw(n); }
    }

    // Convert to NodesGuiList (NodeGuiPtr), skipping nodes without GUI
    NodesGuiList nodesToCopy;
    for (const NodePtr& n : nodesToCopyRaw) {
        NodeGuiIPtr guiI = n->getNodeGui();
        NodeGuiPtr gui = std::dynamic_pointer_cast<NodeGui>(guiI);
        if (gui) {
            nodesToCopy.push_back(gui);
        }
    }

    if (nodesToCopy.size() < 2) {
        fprintf(stderr, "FLUX ERROR: duplicateLayer — could not find NodeGui for gizmo/merge\n");
        return false;
    }

    // 2. Build old script name → old NodePtr map BEFORE copy
    QHash<QString, NodePtr> oldScriptNameToNode;
    for (const NodePtr& n : nodesToCopyRaw) {
        if (n) {
            oldScriptNameToNode.insert(QString::fromStdString(n->getScriptName()), n);
        }
    }

    size_t expectedCount = nodesToCopy.size();

    // 3. Copy nodes into clipboard
    NodeClipBoard clipboard;
    nodeGraph->copyNodes(nodesToCopy, clipboard);

    if (clipboard.nodes.size() != expectedCount) {
        fprintf(stderr, "FLUX ERROR: duplicateLayer — clipboard has %zu nodes, expected %zu\n",
                clipboard.nodes.size(), expectedCount);
        return false;
    }

    // 4. Paste — creates new nodes. pair.first = old script name.
    std::list<std::pair<std::string, NodeGuiPtr>> newNodes;
    nodeGraph->pasteCliboard(clipboard, &newNodes);

    if (newNodes.size() != expectedCount) {
        fprintf(stderr, "FLUX ERROR: duplicateLayer — paste returned %zu nodes, expected %zu\n",
                newNodes.size(), expectedCount);
        // Deactivate any pasted nodes on failure
        for (auto& pair : newNodes) {
            if (pair.second) {
                NodePtr n = pair.second->getNode();
                if (n && n->isActivated()) {
                    n->deactivate(std::list<NodePtr>(), false, true);
                }
            }
        }
        return false;
    }

    // 5. Build old script name → pasted NodePtr map
    QHash<QString, NodePtr> pastedByOldScriptName;
    for (auto& pair : newNodes) {
        NodePtr n = pair.second->getNode();
        if (n) {
            pastedByOldScriptName.insert(QString::fromStdString(pair.first), n);
        }
    }

    // Helper: map an old node to its pasted counterpart, or nullptr
    auto mapNode = [&](const NodePtr& oldNode) -> NodePtr {
        if (!oldNode) return NodePtr();
        QString key = QString::fromStdString(oldNode->getScriptName());
        return pastedByOldScriptName.value(key, NodePtr());
    };

    // 6. Resolve required mappings — fail if any are missing
    NodePtr newGizmo = mapNode(layer.gizmoNode);
    NodePtr newMerge = mapNode(layer.mergeNode);
    if (!newGizmo || !newMerge) {
        fprintf(stderr, "FLUX ERROR: duplicateLayer — failed to map gizmo (%p→%p) or merge (%p→%p) for layer '%s'\n",
                layer.gizmoNode.get(), newGizmo.get(),
                layer.mergeNode.get(), newMerge.get(),
                layer.name.toStdString().c_str());
        for (auto& pair : newNodes) {
            if (pair.second) {
                NodePtr n = pair.second->getNode();
                if (n && n->isActivated()) {
                    n->deactivate(std::list<NodePtr>(), false, true);
                }
            }
        }
        return false;
    }

    NodePtr newRead;
    if (layer.readerNode) {
        newRead = mapNode(layer.readerNode);
        if (!newRead) {
            fprintf(stderr, "FLUX ERROR: duplicateLayer — failed to map readerNode '%s' for footage layer '%s'\n",
                    layer.readerNode->getScriptName().c_str(), layer.name.toStdString().c_str());
            for (auto& pair : newNodes) {
                if (pair.second) {
                    NodePtr n = pair.second->getNode();
                    if (n && n->isActivated()) {
                        n->deactivate(std::list<NodePtr>(), false, true);
                    }
                }
            }
            return false;
        }
    }

    // Map effects
    QList<FluxEffect> newEffects;
    for (int e = 0; e < layer.effects.size(); ++e) {
        if (!layer.effects[e].node) continue;
        NodePtr mapped = mapNode(layer.effects[e].node);
        if (!mapped) {
            fprintf(stderr, "FLUX ERROR: duplicateLayer — failed to map effect[%d] node '%s' for layer '%s'\n",
                    e, layer.effects[e].node->getScriptName().c_str(), layer.name.toStdString().c_str());
            for (auto& pair : newNodes) {
                if (pair.second) {
                    NodePtr n = pair.second->getNode();
                    if (n && n->isActivated()) {
                        n->deactivate(std::list<NodePtr>(), false, true);
                    }
                }
            }
            return false;
        }
        FluxEffect fe;
        fe.pluginId = layer.effects[e].pluginId;
        fe.label = QString::fromStdString(mapped->getLabel());
        fe.node = mapped;
        fe.enabled = layer.effects[e].enabled;
        newEffects.append(fe);
    }

    // Map maskApplyNode — required if original had one
    NodePtr newMaskApply = mapNode(layer.maskApplyNode);
    if (layer.maskApplyNode && !newMaskApply) {
        fprintf(stderr, "FLUX ERROR: duplicateLayer — failed to map maskApplyNode '%s' for layer '%s'\n",
                layer.maskApplyNode->getScriptName().c_str(), layer.name.toStdString().c_str());
        for (auto& pair : newNodes) {
            if (pair.second) {
                NodePtr n = pair.second->getNode();
                if (n && n->isActivated()) {
                    n->deactivate(std::list<NodePtr>(), false, true);
                }
            }
        }
        return false;
    }

    // Map masks
    QList<FluxMask> newMasks;
    for (int m = 0; m < layer.masks.size(); ++m) {
        const FluxMask& om = layer.masks[m];
        FluxMask nm;
        nm.name = om.name;
        nm.type = om.type;
        nm.pluginId = om.pluginId;
        nm.enabled = om.enabled;
        nm.inverted = om.inverted;
        nm.effectIndex = om.effectIndex;
        if (om.reformatNode) {
            nm.reformatNode = mapNode(om.reformatNode);
            if (!nm.reformatNode) {
                fprintf(stderr, "FLUX ERROR: duplicateLayer — failed to map mask[%d] reformatNode '%s' for layer '%s'\n",
                        m, om.reformatNode->getScriptName().c_str(), layer.name.toStdString().c_str());
                for (auto& pair : newNodes) {
                    if (pair.second) {
                        NodePtr n = pair.second->getNode();
                        if (n && n->isActivated()) {
                            n->deactivate(std::list<NodePtr>(), false, true);
                        }
                    }
                }
                return false;
            }
        }
        if (om.maskNode) {
            nm.maskNode = mapNode(om.maskNode);
            if (!nm.maskNode) {
                fprintf(stderr, "FLUX ERROR: duplicateLayer — failed to map mask[%d] maskNode '%s' for layer '%s'\n",
                        m, om.maskNode->getScriptName().c_str(), layer.name.toStdString().c_str());
                for (auto& pair : newNodes) {
                    if (pair.second) {
                        NodePtr n = pair.second->getNode();
                        if (n && n->isActivated()) {
                            n->deactivate(std::list<NodePtr>(), false, true);
                        }
                    }
                }
                return false;
            }
        }
        newMasks.append(nm);
    }

    // 7. Build duplicate FluxLayer from mapped refs
    FluxLayer duplicate;
    duplicate.name = layer.name + QString::fromUtf8(" copy");
    duplicate.type = layer.type;
    duplicate.muted = layer.muted;
    duplicate.locked = false;
    duplicate.solo = layer.solo;
    duplicate.color = layer.color;
    duplicate.filePath = layer.filePath;

    // Range state
    duplicate.inPoint = layer.inPoint;
    duplicate.outPoint = layer.outPoint;
    duplicate.originalInPoint = layer.originalInPoint;
    duplicate.originalOutPoint = layer.originalOutPoint;
    duplicate.originalFirstFrame = layer.originalFirstFrame;
    duplicate.originalLastFrame = layer.originalLastFrame;
    duplicate.timeOffset = layer.timeOffset;
    duplicate.trimStart = layer.trimStart;
    duplicate.trimEnd = layer.trimEnd;
    duplicate.nodeInitialized = true;
    duplicate.solidColor = layer.solidColor;
    duplicate.parentLayerIndex = layer.parentLayerIndex;
    duplicate.expanded = layer.expanded;

    // Mapped node refs
    duplicate.readerNode = newRead;
    duplicate.gizmoNode = newGizmo;
    duplicate.mergeNode = newMerge;
    duplicate.effects = newEffects;
    duplicate.maskApplyNode = newMaskApply;
    duplicate.masks = newMasks;
    duplicate.hasPrecompBranch = false; // recomputed by classifier on next rebuild

    // 8. For footage layers, re-set filename on the new external Read node.
    // Paste may not carry the filename correctly for Read nodes.
    if (layer.type == QString::fromUtf8("footage") && !layer.filePath.isEmpty() && newRead) {
        KnobIPtr filenameKnob = newRead->getKnobByName("filename");
        if (filenameKnob) {
            KnobStringBasePtr strKnob = std::dynamic_pointer_cast<KnobStringBase>(filenameKnob);
            if (strKnob) {
                strKnob->setValue(layer.filePath.toStdString(), ViewSpec::all(), 0);
            }
        }
        // Connect Read → Gizmo input (deterministic: disconnect first)
        newGizmo->disconnectInput(0);
        if (!newGizmo->connectInput(newRead, 0)) {
            fprintf(stderr, "FLUX ERROR: connectInput(Read→Gizmo) failed for duplicated layer '%s'\n",
                    layer.name.toStdString().c_str());
        }

        // Force output components to RGBA
        KnobIPtr outCompKnob = newRead->getKnobByName("outputComponents");
        if (outCompKnob) {
            KnobIntBasePtr choiceKnob = std::dynamic_pointer_cast<KnobIntBase>(outCompKnob);
            if (choiceKnob) {
                choiceKnob->setValue(0, ViewSpec::all(), 0); // 0 = RGBA
            }
        }
    }

    // 9. Insert duplicate above the original (at same index, pushing original down)
    _layers.insert(index, duplicate);

    // 10. Adjust selected layer
    if (_selectedLayer >= index) {
        ++_selectedLayer;
    }

    fprintf(stderr, "FLUX DUPLICATE: layer '%s' branch-aware duplicated with %lld effects, %lld masks at index %d\n",
            layer.name.toStdString().c_str(), static_cast<long long>(newEffects.size()), static_cast<long long>(newMasks.size()), index);

    // 11. Trigger rebuild to reconnect and reposition the node graph
    rebuildVisibleRows();
    Q_EMIT compositingChanged();
    update();
    return true;
}

void
FluxTimeline::splitLayer(int index, int frame)
{
    if (!canSplitRow(index)) {
        return;
    }

    const FluxLayer& original = _layers[index];

    // Convert timeline frame to source frame for the original layer
    int sourceFrame = frame - original.timeOffset;

    // Can't split outside the layer's range
    if (sourceFrame <= original.inPoint || sourceFrame >= original.outPoint) {
        return;
    }

    // 1. Duplicate the layer — duplicate is inserted at `index` (above original)
    const int previousCount = _layers.size();
    if (!duplicateLayer(index) || _layers.size() != previousCount + 1 || index + 1 >= _layers.size()) {
        return;
    }

    // After duplicate, the new layer is at `index`, original shifted to `index + 1`
    FluxLayer& dup = _layers[index];       // the duplicate (top)
    FluxLayer& orig = _layers[index + 1];  // the original (bottom)

    const bool isAdj = (orig.type == QString::fromUtf8("adjustment"));

    // 2. Trim original's outPoint to the split frame
    orig.outPoint = sourceFrame;

    // 3. Trim duplicate's inPoint to the split frame
    dup.inPoint = sourceFrame;

    if (isAdj) {
        // Adjustment rows: update disable-knob keyframes on both halves
        updateAdjustmentTrimKeyframes(index);      // duplicate (top half)
        updateAdjustmentTrimKeyframes(index + 1);  // original (bottom half)
    } else {
        // Footage/solid rows: update FrameRange knobs
        if (orig.gizmoNode) {
            KnobIPtr frameRangeKnob = orig.gizmoNode->getKnobByName(std::string("frameRange"));
            if (frameRangeKnob) {
                KnobIntBasePtr int2D = std::dynamic_pointer_cast<KnobIntBase>(frameRangeKnob);
                if (int2D) {
                    int2D->setValue(orig.outPoint, ViewSpec::all(), 1); // only change last frame
                }
            }
        }

        if (dup.gizmoNode) {
            KnobIPtr frameRangeKnob = dup.gizmoNode->getKnobByName(std::string("frameRange"));
            if (frameRangeKnob) {
                KnobIntBasePtr int2D = std::dynamic_pointer_cast<KnobIntBase>(frameRangeKnob);
                if (int2D) {
                    int2D->setValue(dup.inPoint, ViewSpec::all(), 0); // only change first frame
                }
            }
        }
    }

    fprintf(stderr, "FLUX SPLIT: at frame %d (source=%d)\n  original[%d]: inPoint=%d outPoint=%d\n  duplicate[%d]: inPoint=%d outPoint=%d timeOffset=%d\n",
            frame, sourceFrame, index + 1, orig.inPoint, orig.outPoint,
            index, dup.inPoint, dup.outPoint, dup.timeOffset);

    Q_EMIT compositingChanged();
    update();
}


void
FluxTimeline::moveLayer(int from,
                        int to)
{
    if (from >= 0 && from < _layers.size() &&
        to >= 0 && to < _layers.size() &&
        !_layers[from].locked) {
        _layers.move(from, to);
        rebuildVisibleRows();
        if (_selectedLayer == from) {
            _selectedLayer = to;
        }
        Q_EMIT layersReordered();
        Q_EMIT compositingChanged();
        update();
    }
}

const QList<FluxLayer>&
FluxTimeline::getLayers() const
{
    return _layers;
}

void
FluxTimeline::setFrameRange(int first,
                            int last)
{
    _firstFrame = first;
    _lastFrame = last;
    updateZoom();
    update();
}

void
FluxTimeline::setCurrentFrame(int frame)
{
    if (frame >= _firstFrame && frame <= _lastFrame) {
        _currentFrame = frame;
        update();
    }
}

int
FluxTimeline::getCurrentFrame() const
{
    return _currentFrame;
}

int
FluxTimeline::getSelectedLayerIndex() const
{
    return _selectedLayer;
}

 void
 FluxTimeline::refreshVisibleRows()
 {
     rebuildVisibleRows();
     clampScrollOffsets();
     update();
 }

bool
FluxTimeline::removeEffectFromLayer(int layerIndex, int effectIndex)
{
    if (layerIndex < 0 || layerIndex >= _layers.size()) {
        return false;
    }
    FluxLayer& layer = _layers[layerIndex];
    if (layer.locked) {
        return false;
    }
    if (effectIndex < 0 || effectIndex >= layer.effects.size()) {
        return false;
    }

    clearAllViewerInputBadges();

    FluxEffect effect = layer.effects.takeAt(effectIndex);
    deactivateFluxEffectOwnedNodes(effect);

    // Remove masks targeting the removed effect; deactivate their nodes first
    for (int i = layer.masks.size() - 1; i >= 0; --i) {
        if (layer.masks[i].effectIndex == effectIndex) {
            FluxMask& mask = layer.masks[i];
            if (mask.maskNode && mask.maskNode->isActivated()) {
                mask.maskNode->deactivate(std::list<NodePtr>(), false, true);
            }
            if (mask.reformatNode && mask.reformatNode->isActivated()) {
                mask.reformatNode->deactivate(std::list<NodePtr>(), false, true);
            }
            layer.masks.removeAt(i);
        } else if (layer.masks[i].effectIndex > effectIndex) {
            --layer.masks[i].effectIndex;
        }
    }

    _selectedType = eFluxSelectionLayer;
    _selectedLayer = layerIndex;
    _selectedEffectIndex = -1;
    _selectedMaskIndex = -1;
    _selectedPropertyIndex = -1;

    refreshVisibleRows();
    Q_EMIT effectsChanged(layerIndex);
    Q_EMIT layerSelected(layerIndex);
    Q_EMIT compositingChanged();
    return true;
}

bool
FluxTimeline::moveEffectInLayer(int layerIndex, int fromEffectIndex, int toEffectIndex)
{
    if (layerIndex < 0 || layerIndex >= _layers.size()) {
        return false;
    }
    FluxLayer& layer = _layers[layerIndex];
    if (layer.locked) {
        return false;
    }
    if (fromEffectIndex < 0 || fromEffectIndex >= layer.effects.size()) {
        return false;
    }
    if (toEffectIndex < 0 || toEffectIndex >= layer.effects.size()) {
        return false;
    }
    if (fromEffectIndex == toEffectIndex) {
        return false;
    }

    layer.effects.move(fromEffectIndex, toEffectIndex);

    // Remap mask.effectIndex to follow the move
    for (int i = 0; i < layer.masks.size(); ++i) {
        int& ei = layer.masks[i].effectIndex;
        if (ei < 0) {
            continue;   // layer-level mask, unaffected
        }
        if (ei == fromEffectIndex) {
            ei = toEffectIndex;
        } else if (fromEffectIndex < toEffectIndex) {
            // Moving down: items between from+1 and to shift up one slot
            if (ei > fromEffectIndex && ei <= toEffectIndex) {
                --ei;
            }
        } else {
            // Moving up: items between to and from-1 shift down one slot
            if (ei >= toEffectIndex && ei < fromEffectIndex) {
                ++ei;
            }
        }
    }

    _selectedType = eFluxSelectionEffect;
    _selectedLayer = layerIndex;
    _selectedEffectIndex = toEffectIndex;
    _selectedPropertyIndex = -1;

    refreshVisibleRows();
    Q_EMIT effectsChanged(layerIndex);
    Q_EMIT effectSelected(layerIndex, toEffectIndex);
    Q_EMIT compositingChanged();
    return true;
}

QString
FluxTimeline::makeUniqueMaskName(int layerIndex, int effectIndex) const
{
    if (layerIndex < 0 || layerIndex >= _layers.size()) {
        return QString::fromUtf8("Mask");
    }
    const FluxLayer& layer = _layers[layerIndex];
    QString baseName;
    if (effectIndex >= 0 && effectIndex < layer.effects.size()) {
        baseName = layer.effects[effectIndex].label + QString::fromUtf8(" Mask");
    } else {
        baseName = QString::fromUtf8("Layer Mask");
    }

    QString candidate = baseName;
    int suffix = 2;
    bool unique = false;
    while (!unique) {
        unique = true;
        for (int m = 0; m < layer.masks.size(); ++m) {
            if (layer.masks[m].name == candidate) {
                unique = false;
                candidate = baseName + QString::fromUtf8(" %1").arg(suffix);
                ++suffix;
                break;
            }
        }
    }
    return candidate;
}

bool
FluxTimeline::canAddEffectMask(int layerIndex, int effectIndex) const
{
    if (layerIndex < 0 || layerIndex >= _layers.size()) {
        return false;
    }
    if (effectIndex < 0 || effectIndex >= _layers[layerIndex].effects.size()) {
        return false;
    }
    NodePtr effectNode = _layers[layerIndex].effects[effectIndex].node;
    if (!effectNode) {
        return false;
    }
    return discoverMaskInput(effectNode) >= 0;
}

NodePtr
FluxTimeline::selectedViewerSwitchTargetNode(int viewerInputIndex) const
{
    // Input 0 (key 1) is "full comp" — Gui05 resolves via _fluxFinalOutputNode.
    if (viewerInputIndex == 0) {
        return NodePtr();
    }

    if (_selectedType == eFluxSelectionEffect) {
        if (_selectedLayer < 0 || _selectedLayer >= _layers.size()) {
            return NodePtr();
        }
        const FluxLayer& layer = _layers[_selectedLayer];
        if (_selectedEffectIndex < 0 || _selectedEffectIndex >= layer.effects.size()) {
            return NodePtr();
        }
        const FluxEffect& effect = layer.effects[_selectedEffectIndex];
        if (!effect.node || !effect.node->isActivated()) {
            return NodePtr();
        }
        return effect.node;
    }

    if (_selectedType == eFluxSelectionLayer) {
        if (_selectedLayer < 0 || _selectedLayer >= _layers.size()) {
            return NodePtr();
        }
        const FluxLayer& layer = _layers[_selectedLayer];

        if (layer.type == QString::fromUtf8("adjustment")) {
            // Adjustment layers do not have a mergeNode; output is the last enabled active effect.
            NodePtr lastEnabledEffect;
            for (int e = 0; e < layer.effects.size(); ++e) {
                const FluxEffect& fx = layer.effects[e];
                if (fx.enabled && fx.node && fx.node->isActivated()) {
                    lastEnabledEffect = fx.node;
                }
            }
            return lastEnabledEffect;
        }

        // Non-adjustment layer: full layer output is mergeNode->getInput(1), which is
        // the layer's output pipe after effects/masks, immediately before the Merge.
        if (!layer.mergeNode || !layer.mergeNode->isActivated()) {
            return NodePtr();
        }
        NodePtr layerOutput = layer.mergeNode->getInput(1);
        return (layerOutput && layerOutput->isActivated()) ? layerOutput : NodePtr();
    }

    // Masks, properties, text animators, no selection → no target.
    return NodePtr();
}

void
FluxTimeline::clearAllViewerInputBadges()
{
    bool changed = false;
    for (int i = 0; i < _layers.size(); ++i) {
        FluxLayer& layer = _layers[i];
        if (!layer.viewerInputBadges.isEmpty()) {
            layer.viewerInputBadges.clear();
            changed = true;
        }
        for (int e = 0; e < layer.effects.size(); ++e) {
            if (!layer.effects[e].viewerInputBadges.isEmpty()) {
                layer.effects[e].viewerInputBadges.clear();
                changed = true;
            }
        }
    }
    if (changed) {
        update();
    }
}

void
FluxTimeline::clearViewerInputBadge(int viewerInputIndex)
{
    const int badgeNum = viewerInputIndex + 1; // 1-based
    bool changed = false;
    for (int i = 0; i < _layers.size(); ++i) {
        FluxLayer& layer = _layers[i];
        if (layer.viewerInputBadges.removeOne(badgeNum)) {
            changed = true;
        }
        for (int e = 0; e < layer.effects.size(); ++e) {
            if (layer.effects[e].viewerInputBadges.removeOne(badgeNum)) {
                changed = true;
            }
        }
    }
    if (changed) {
        update();
    }
}

void
FluxTimeline::setViewerInputBadgeForNode(int viewerInputIndex, const NodePtr& node)
{
    if (!node) {
        return;
    }
    const int badgeNum = viewerInputIndex + 1; // 1-based

    // Remove this badge from all rows first (one badge per input).
    clearViewerInputBadge(viewerInputIndex);

    // Check effect rows first (more specific match).
    for (int i = 0; i < _layers.size(); ++i) {
        FluxLayer& layer = _layers[i];
        for (int e = 0; e < layer.effects.size(); ++e) {
            if (layer.effects[e].node == node) {
                // For adjustment layers, badge goes on the layer row, not the effect row.
                if (layer.type == QString::fromUtf8("adjustment")) {
                    if (!layer.viewerInputBadges.contains(badgeNum)) {
                        layer.viewerInputBadges.append(badgeNum);
                    }
                } else {
                    if (!layer.effects[e].viewerInputBadges.contains(badgeNum)) {
                        layer.effects[e].viewerInputBadges.append(badgeNum);
                    }
                }
                update();
                return;
            }
        }
    }

    // Check if node matches a non-adjustment layer's mergeNode->getInput(1) (full layer output).
    for (int i = 0; i < _layers.size(); ++i) {
        FluxLayer& layer = _layers[i];
        if (layer.type == QString::fromUtf8("adjustment")) {
            continue;
        }
        if (layer.mergeNode && layer.mergeNode->isActivated()) {
            if (layer.mergeNode->getInput(1) == node) {
                if (!layer.viewerInputBadges.contains(badgeNum)) {
                    layer.viewerInputBadges.append(badgeNum);
                }
                update();
                return;
            }
        }
    }

    // Check if node matches an adjustment layer's last enabled active effect.
    for (int i = 0; i < _layers.size(); ++i) {
        FluxLayer& layer = _layers[i];
        if (layer.type != QString::fromUtf8("adjustment")) {
            continue;
        }
        // Find last enabled active effect node
        NodePtr lastEnabledEffect;
        for (int e = 0; e < layer.effects.size(); ++e) {
            if (layer.effects[e].enabled && layer.effects[e].node && layer.effects[e].node->isActivated()) {
                lastEnabledEffect = layer.effects[e].node;
            }
        }
        if (lastEnabledEffect == node) {
            if (!layer.viewerInputBadges.contains(badgeNum)) {
                layer.viewerInputBadges.append(badgeNum);
            }
            update();
            return;
        }
    }
}

bool
FluxTimeline::addLayerMask(int layerIndex)
{
    if (layerIndex < 0 || layerIndex >= _layers.size()) {
        return false;
    }
    FluxLayer& layer = _layers[layerIndex];
    if (layer.locked) {
        return false;
    }

    FluxMask mask;
    mask.type = QString::fromUtf8("layer");
    mask.effectIndex = -1;
    mask.name = makeUniqueMaskName(layerIndex, -1);

    layer.masks.append(mask);
    layer.expanded = true;

    int newMaskIndex = layer.masks.size() - 1;
    _selectedType = eFluxSelectionMask;
    _selectedLayer = layerIndex;
    _selectedEffectIndex = -1;
    _selectedMaskIndex = newMaskIndex;
    _selectedPropertyIndex = -1;

    refreshVisibleRows();
    Q_EMIT masksChanged(layerIndex);
    Q_EMIT compositingChanged();
    Q_EMIT maskSelected(layerIndex, newMaskIndex);
    return true;
}

bool
FluxTimeline::addEffectMask(int layerIndex, int effectIndex)
{
    if (layerIndex < 0 || layerIndex >= _layers.size()) {
        return false;
    }
    FluxLayer& layer = _layers[layerIndex];
    if (layer.locked) {
        return false;
    }
    if (effectIndex < 0 || effectIndex >= layer.effects.size()) {
        return false;
    }
    NodePtr effectNode = layer.effects[effectIndex].node;
    if (!effectNode || discoverMaskInput(effectNode) < 0) {
        return false;
    }

    FluxMask mask;
    mask.type = QString::fromUtf8("effect");
    mask.effectIndex = effectIndex;
    mask.name = makeUniqueMaskName(layerIndex, effectIndex);

    layer.masks.append(mask);
    layer.expanded = true;

    int newMaskIndex = layer.masks.size() - 1;
    _selectedType = eFluxSelectionMask;
    _selectedLayer = layerIndex;
    _selectedEffectIndex = effectIndex;
    _selectedMaskIndex = newMaskIndex;
    _selectedPropertyIndex = -1;

    refreshVisibleRows();
    Q_EMIT masksChanged(layerIndex);
    Q_EMIT compositingChanged();
    Q_EMIT maskSelected(layerIndex, newMaskIndex);
    return true;
}

static int fluxAIChannelIndex(const QString& channel)
{
    const QString c = channel.toLower();
    if (c == QLatin1String("green")) return 1;
    if (c == QLatin1String("blue")) return 2;
    if (c == QLatin1String("alpha")) return 3;
    return 0;
}

static int fluxAIOperationIndex(const QString& operation)
{
    const QString op = operation.toLower();
    if (op == QLatin1String("copy")) return 0;
    if (op == QLatin1String("plus")) return 1;
    if (op == QLatin1String("multiply")) return 2;
    if (op == QLatin1String("screen")) return 3;
    if (op == QLatin1String("max")) return 4;
    if (op == QLatin1String("min")) return 5;
    if (op == QLatin1String("subtract")) return 6;
    if (op == QLatin1String("overlay")) return 7;
    return 4;
}

static QString fluxAIShuffleChannelExpression(const QString& channel)
{
    const QString c = channel.toLower();
    if (c == QLatin1String("green")) {
        return QString::fromUtf8("B.uk.co.thefoundry.OfxImagePlaneColour.G");
    }
    if (c == QLatin1String("blue")) {
        return QString::fromUtf8("B.uk.co.thefoundry.OfxImagePlaneColour.B");
    }
    if (c == QLatin1String("alpha")) {
        return QString::fromUtf8("B.uk.co.thefoundry.OfxImagePlaneColour.A");
    }
    return QString::fromUtf8("B.uk.co.thefoundry.OfxImagePlaneColour.R");
}

static QString fluxAIShuffleChannelChoiceId(const QString& channel)
{
    const QString c = channel.toLower();
    if (c == QLatin1String("green")) {
        return QString::fromUtf8("B.g");
    }
    if (c == QLatin1String("blue")) {
        return QString::fromUtf8("B.b");
    }
    if (c == QLatin1String("alpha")) {
        return QString::fromUtf8("B.a");
    }
    return QString::fromUtf8("B.r");
}

static void fluxSetShuffleChannelKnob(const NodePtr& node,
                                      const char* knobName,
                                      const QString& expression,
                                      const QString& choiceId)
{
    KnobStringPtr stringKnob = std::dynamic_pointer_cast<KnobString>(node->getKnobByName(knobName));
    if (stringKnob) {
        stringKnob->setValue(expression.toStdString(), ViewSpec::all(), 0, true);
    }

    KnobChoicePtr choiceKnob = std::dynamic_pointer_cast<KnobChoice>(node->getKnobByName(knobName));
    if (choiceKnob) {
        choiceKnob->setValueFromID(choiceId.toStdString(), 0, true);
    }
}

static void fluxConfigureAIReadNodeTiming(const NodePtr& readNode,
                                          double sourceFrameRate)
{
    if (!readNode) {
        return;
    }

    if (std::isfinite(sourceFrameRate) && sourceFrameRate > 0.0) {
        KnobIPtr customFpsKnobI = readNode->getKnobByName("customFps");
        if (customFpsKnobI) {
            KnobBoolPtr customFpsKnob = std::dynamic_pointer_cast<KnobBool>(customFpsKnobI);
            if (customFpsKnob) {
                customFpsKnob->setValue(true, ViewSpec::all(), 0);
            }
        }
        KnobIPtr frameRateKnobI = readNode->getKnobByName("frameRate");
        if (frameRateKnobI) {
            KnobDoublePtr frameRateKnob = std::dynamic_pointer_cast<KnobDouble>(frameRateKnobI);
            if (frameRateKnob) {
                frameRateKnob->setValue(sourceFrameRate, ViewSpec::all(), 0);
            }
        }
    }
}

static void fluxConfigureAIMaskTimeOffsetNode(const NodePtr& timeOffsetNode,
                                              int timeOffset)
{
    if (!timeOffsetNode) {
        return;
    }
    KnobIPtr timeOffsetKnobI = timeOffsetNode->getKnobByName("timeOffset");
    if (timeOffsetKnobI) {
        KnobIntBasePtr timeOffsetKnob = std::dynamic_pointer_cast<KnobIntBase>(timeOffsetKnobI);
        if (timeOffsetKnob) {
            timeOffsetKnob->setValue(timeOffset, ViewSpec::all(), 0);
        }
    }
}

static int fluxAIMaskGenerationTimeOffset(Gui* gui,
                                          const QString& manifestRelative,
                                          int fallbackTimeOffset)
{
    if (!gui || !gui->getApp() || !gui->getApp()->getProject() || manifestRelative.isEmpty()) {
        return fallbackTimeOffset;
    }

    QFile file(QDir(gui->getApp()->getProject()->getProjectPath()).filePath(manifestRelative));
    if (!file.open(QIODevice::ReadOnly)) {
        return fallbackTimeOffset;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return fallbackTimeOffset;
    }

    const QJsonObject sourceMetadata = doc.object().value(QString::fromUtf8("source_metadata")).toObject();
    const char* frameRangeKeys[] = {
        "sam3_frame_range",
        "matanyone2_frame_range",
        "videomama_frame_range"
    };
    for (const char* key : frameRangeKeys) {
        const QJsonObject frameRange = sourceMetadata.value(QString::fromUtf8(key)).toObject();
        if (frameRange.contains(QString::fromUtf8("time_offset"))) {
            return frameRange.value(QString::fromUtf8("time_offset")).toInt(fallbackTimeOffset);
        }
    }
    if (sourceMetadata.contains(QString::fromUtf8("time_offset"))) {
        return sourceMetadata.value(QString::fromUtf8("time_offset")).toInt(fallbackTimeOffset);
    }
    return fallbackTimeOffset;
}

static NodePtr fluxCreateAIReadNode(Gui* gui,
                                    const QString& relativeMask,
                                    double sourceFrameRate = 0.0)
{
    if (!gui || !gui->getApp() || !gui->getApp()->getProject()) {
        return NodePtr();
    }
    NodeCollectionPtr collection = std::dynamic_pointer_cast<NodeCollection>(gui->getApp()->getProject());
    if (!collection) {
        return NodePtr();
    }

    std::string absMask = QDir(gui->getApp()->getProject()->getProjectPath()).filePath(relativeMask).toStdString();
    gui->getApp()->getProject()->canonicalizePath(absMask);
    CreateNodeArgs readArgs(PLUGINID_NATRON_READ, collection);
    readArgs.setProperty<bool>(kCreateNodeArgsPropAutoConnect, false);
    readArgs.setProperty<bool>(kCreateNodeArgsPropAddUndoRedoCommand, false);
    readArgs.setProperty<bool>(kCreateNodeArgsPropSettingsOpened, false);
    NodePtr readNode = gui->getApp()->createReader(absMask, readArgs);
    fluxConfigureAIReadNodeTiming(readNode, sourceFrameRate);
    return readNode;
}

static NodePtr fluxCreateNode(Gui* gui,
                              const char* pluginId)
{
    if (!gui || !gui->getApp() || !gui->getApp()->getProject()) {
        return NodePtr();
    }
    NodeCollectionPtr collection = std::dynamic_pointer_cast<NodeCollection>(gui->getApp()->getProject());
    if (!collection) {
        return NodePtr();
    }

    CreateNodeArgs args(pluginId, collection);
    args.setProperty<bool>(kCreateNodeArgsPropAutoConnect, false);
    args.setProperty<bool>(kCreateNodeArgsPropAddUndoRedoCommand, false);
    args.setProperty<bool>(kCreateNodeArgsPropSettingsOpened, false);
    return gui->getApp()->createNode(args);
}

static void fluxConfigureAIChannelMergeNode(const NodePtr& node,
                                            const QString& sourceChannel,
                                            const QString& operation)
{
    if (!node) {
        return;
    }

    KnobChoicePtr aChannel = std::dynamic_pointer_cast<KnobChoice>(node->getKnobByName("aChannel"));
    if (aChannel) {
        aChannel->setValue(fluxAIChannelIndex(sourceChannel), ViewSpec::all(), 0);
    }

    KnobChoicePtr bChannel = std::dynamic_pointer_cast<KnobChoice>(node->getKnobByName("bChannel"));
    if (bChannel) {
        bChannel->setValue(3, ViewSpec::all(), 0); // alpha
    }

    KnobChoicePtr op = std::dynamic_pointer_cast<KnobChoice>(node->getKnobByName("operation"));
    if (op) {
        op->setValue(fluxAIOperationIndex(operation), ViewSpec::all(), 0);
    }

    KnobChoicePtr out = std::dynamic_pointer_cast<KnobChoice>(node->getKnobByName("outputChannel"));
    if (out) {
        out->setValue(3, ViewSpec::all(), 0); // alpha
    }

    KnobChoicePtr bbox = std::dynamic_pointer_cast<KnobChoice>(node->getKnobByName("bbox"));
    if (bbox) {
        bbox->setValue(0, ViewSpec::all(), 0); // B
    }

    KnobDoublePtr mix = std::dynamic_pointer_cast<KnobDouble>(node->getKnobByName("mix"));
    if (mix) {
        mix->setValue(1.0, ViewSpec::all(), 0);
    }
}

static void fluxConfigureAIShuffleAlphaNode(const NodePtr& node,
                                            const QString& sourceChannel)
{
    if (!node) {
        return;
    }

    const QString expression = fluxAIShuffleChannelExpression(sourceChannel);
    const QString choiceId = fluxAIShuffleChannelChoiceId(sourceChannel);
    fluxSetShuffleChannelKnob(node, "outputA", expression, choiceId);
    fluxSetShuffleChannelKnob(node, "outputAChoice", choiceId, choiceId);
}

bool
FluxTimeline::addAIMaskCopyToSelectedLayer(const QString& relativeMask, const QString& manifestRelative, QString* message, const QString& readRelativeMask)
{
    Gui* gui = getGui();
    if (!gui || !gui->getApp() || !gui->getApp()->getProject()) {
        if (message) *message = QString::fromUtf8("Add Mask failed: app/project unavailable.");
        return false;
    }

    const QString readMask = readRelativeMask.isEmpty() ? relativeMask : readRelativeMask;

    const QString sourceChannel = QString::fromUtf8("red");
    const QString operation = QString::fromUtf8("max");

    if (_selectedType == eFluxSelectionEffect) {
        if (_selectedLayer < 0 || _selectedLayer >= _layers.size() ||
            _selectedEffectIndex < 0 || _selectedEffectIndex >= _layers[_selectedLayer].effects.size()) {
            if (message) *message = QString::fromUtf8("Add Mask failed: select an effect row.");
            return false;
        }

        FluxEffect& effect = _layers[_selectedLayer].effects[_selectedEffectIndex];
        if (!effect.node || discoverMaskInput(effect.node) < 0) {
            if (message) *message = QString::fromUtf8("Add Mask failed: selected effect has no mask input.");
            return false;
        }

        NodePtr readNode = fluxCreateAIReadNode(gui, readMask, _layers[_selectedLayer].sourceFrameRate);
        NodePtr shuffleNode = effect.aiMaskShuffleNode;
        if (!shuffleNode || !shuffleNode->isActivated()) {
            shuffleNode = fluxCreateNode(gui, PLUGINID_OFX_SHUFFLE);
        }
        NodePtr timeOffsetNode = fluxCreateNode(gui, PLUGINID_OFX_TIMEOFFSET);
        if (!readNode || !timeOffsetNode || !shuffleNode) {
            if (readNode) readNode->deactivate(std::list<NodePtr>(), false, true);
            if (timeOffsetNode) timeOffsetNode->deactivate(std::list<NodePtr>(), false, true);
            if (shuffleNode && shuffleNode != effect.aiMaskShuffleNode) shuffleNode->deactivate(std::list<NodePtr>(), false, true);
            if (message) *message = QString::fromUtf8("Add Mask failed: could not create AI Read, TimeOffset, or Shuffle node.");
            return false;
        }

        if (effect.aiMaskReadNode) {
            effect.aiMaskReadNode->deactivate(std::list<NodePtr>(), false, true);
        }
        if (effect.aiMaskTimeOffsetNode) {
            effect.aiMaskTimeOffsetNode->deactivate(std::list<NodePtr>(), false, true);
        }
        effect.aiMaskReadNode = readNode;
        effect.aiMaskTimeOffsetNode = timeOffsetNode;
        effect.aiMaskShuffleNode = shuffleNode;
        effect.aiMaskUsage = QString::fromUtf8("effect-mask");
        effect.aiMaskSourceChannel = sourceChannel;
        effect.aiMaskOperation.clear();
        // Persist the actual path used by the Read node.  For generated AI
        // sequences this is the ###### pattern; storing only the first still
        // frame makes rebuild/reopen recreate a one-frame Read.
        effect.aiMaskSourceRelativePath = readMask;
        effect.aiMaskManifestRelativePath = manifestRelative;
        effect.aiMaskBaseTimeOffset = fluxAIMaskGenerationTimeOffset(gui, manifestRelative, _layers[_selectedLayer].timeOffset);
        fluxConfigureAIMaskTimeOffsetNode(effect.aiMaskTimeOffsetNode, _layers[_selectedLayer].timeOffset - effect.aiMaskBaseTimeOffset);
        effect.aiMaskTargetPlane.clear();
        effect.isAIMaskCopy = false;
        shuffleNode->setLabel(QString::fromUtf8("AI Effect Mask").toStdString());
        fluxConfigureAIShuffleAlphaNode(shuffleNode, sourceChannel);

        Q_EMIT effectsChanged(_selectedLayer);
        Q_EMIT compositingChanged();
        gui->rebuildCompositingGraph(this);

        const int maskInput = discoverMaskInput(effect.node);
        const bool shuffleHasRead = (timeOffsetNode->getInput(0) == readNode && shuffleNode->getInput(0) == timeOffsetNode);
        const bool effectHasMask = (maskInput >= 0 && effect.node->getInput(maskInput) == shuffleNode);
        if (!shuffleHasRead || !effectHasMask) {
            if (message) *message = QString::fromUtf8("Add Mask failed: AI mask Shuffle was created but effect mask wiring did not verify.");
            update();
            return false;
        }

        update();
        if (message) *message = QString::fromUtf8("Added AI mask to selected effect.");
        return true;
    }

    int targetLayer = _selectedLayer;
    if (targetLayer < 0 || targetLayer >= _layers.size() || _layers[targetLayer].type == QString::fromUtf8("adjustment") || _layers[targetLayer].type == QString::fromUtf8("null") || _layers[targetLayer].locked) {
        if (message) *message = QString::fromUtf8("Add Mask failed: select a normal layer or layer effect row in the timeline.");
        return false;
    }

    int next = 1;
    for (int e = 0; e < _layers[targetLayer].effects.size(); ++e) {
        if (_layers[targetLayer].effects[e].aiMaskUsage == QString::fromUtf8("layer-alpha")) {
            ++next;
        }
    }
    NodePtr readNode = fluxCreateAIReadNode(gui, readMask, _layers[targetLayer].sourceFrameRate);
    NodePtr timeOffsetNode = fluxCreateNode(gui, PLUGINID_OFX_TIMEOFFSET);
    NodePtr mergeNode = fluxCreateNode(gui, PLUGINID_FLUX_CHANNEL_MERGE);
    if (!readNode || !timeOffsetNode || !mergeNode) {
        if (readNode) readNode->deactivate(std::list<NodePtr>(), false, true);
        if (timeOffsetNode) timeOffsetNode->deactivate(std::list<NodePtr>(), false, true);
        if (mergeNode) mergeNode->deactivate(std::list<NodePtr>(), false, true);
        if (message) *message = QString::fromUtf8("Add Mask failed: could not create AI Read, TimeOffset, or Flux ChannelMerge node.");
        return false;
    }
    mergeNode->setLabel(QString::fromUtf8("AI Mask Alpha %1").arg(next).toStdString());
    fluxConfigureAIChannelMergeNode(mergeNode, sourceChannel, operation);

    FluxEffect effect;
    effect.pluginId = QString::fromUtf8(PLUGINID_FLUX_CHANNEL_MERGE);
    effect.label = QString::fromUtf8("AI Mask Alpha %1").arg(next);
    effect.node = mergeNode;
    effect.enabled = true;
    effect.isAIMaskCopy = false;
    effect.aiMaskUsage = QString::fromUtf8("layer-alpha");
    effect.aiMaskSourceChannel = sourceChannel;
    effect.aiMaskOperation = operation;
    // Persist the actual path used by the Read node.  For generated AI
    // sequences this is the ###### pattern; storing only the first still
    // frame makes rebuild/reopen recreate a one-frame Read.
    effect.aiMaskSourceRelativePath = readMask;
    effect.aiMaskManifestRelativePath = manifestRelative;
    effect.aiMaskBaseTimeOffset = fluxAIMaskGenerationTimeOffset(gui, manifestRelative, _layers[targetLayer].timeOffset);
    effect.aiMaskReadNode = readNode;
    effect.aiMaskTimeOffsetNode = timeOffsetNode;
    effect.aiMaskChannelMergeNode = mergeNode;
    fluxConfigureAIMaskTimeOffsetNode(effect.aiMaskTimeOffsetNode, _layers[targetLayer].timeOffset - effect.aiMaskBaseTimeOffset);
    _layers[targetLayer].effects.append(effect);
    _layers[targetLayer].expanded = true;
    _selectedType = eFluxSelectionEffect;
    _selectedLayer = targetLayer;
    _selectedEffectIndex = _layers[targetLayer].effects.size() - 1;
    _selectedMaskIndex = -1;
    refreshVisibleRows();
    Q_EMIT effectsChanged(targetLayer);
    Q_EMIT effectSelected(targetLayer, _selectedEffectIndex);
    Q_EMIT compositingChanged();
    if (gui) {
        gui->rebuildCompositingGraph(this);
    }
    const bool hasLayerInput = (mergeNode->getInput(0) != NodePtr());
    const bool hasMaskInput = (timeOffsetNode->getInput(0) == readNode && mergeNode->getInput(1) == timeOffsetNode);
    if (!hasLayerInput || !hasMaskInput) {
        qDebug() << "FluxTimeline::addAIMaskCopyToSelectedLayer: wiring verification failed"
                 << "layer" << targetLayer
                 << "effect" << _selectedEffectIndex
                 << "input0" << hasLayerInput
                 << "input1" << hasMaskInput;
        if (message) {
            *message = QString::fromUtf8("Add Mask failed: AI Mask node was created but graph wiring did not connect both layer and mask inputs.");
        }
        update();
        return false;
    }
    update();
    if (message) *message = QString::fromUtf8("Added AI mask alpha replacement.");
    return true;
}

bool
FluxTimeline::createCorridorKeyNodeForSelectedLayer(const QString& relativeMask, const QString& manifestRelative, QString* message, const QString& readRelativeMask)
{
    Gui* gui = getGui();
    if (!gui || !gui->getApp() || !gui->getApp()->getProject()) {
        if (message) *message = QString::fromUtf8("Create CorridorKey Node failed: app/project unavailable.");
        return false;
    }

    const QString readMask = readRelativeMask.isEmpty() ? relativeMask : readRelativeMask;

    int targetLayer = _selectedLayer;
    if (targetLayer < 0 || targetLayer >= _layers.size() || _layers[targetLayer].type == QString::fromUtf8("adjustment") || _layers[targetLayer].type == QString::fromUtf8("null") || _layers[targetLayer].locked) {
        if (message) *message = QString::fromUtf8("Create CorridorKey Node failed: select a normal layer or layer effect row in the timeline.");
        return false;
    }

    NodePtr readNode = fluxCreateAIReadNode(gui, readMask, _layers[targetLayer].sourceFrameRate);
    NodePtr timeOffsetNode = fluxCreateNode(gui, PLUGINID_OFX_TIMEOFFSET);
    NodePtr corridorNode = fluxCreateNode(gui, PLUGINID_FLUX_CORRIDOR_KEY);
    if (!readNode || !timeOffsetNode || !corridorNode) {
        if (readNode) readNode->deactivate(std::list<NodePtr>(), false, true);
        if (timeOffsetNode) timeOffsetNode->deactivate(std::list<NodePtr>(), false, true);
        if (corridorNode) corridorNode->deactivate(std::list<NodePtr>(), false, true);
        if (message) *message = QString::fromUtf8("Create CorridorKey Node failed: could not create AI Read, TimeOffset, or CorridorKey node.");
        return false;
    }
    corridorNode->setLabel(std::string("CorridorKey"));

    FluxEffect effect;
    effect.pluginId = QString::fromUtf8(PLUGINID_FLUX_CORRIDOR_KEY);
    effect.label = QString::fromUtf8("CorridorKey");
    effect.node = corridorNode;
    effect.enabled = true;
    effect.isAIMaskCopy = true;
    effect.aiMaskUsage = QString::fromUtf8("corridorkey-hint");
    effect.aiMaskSourceChannel = QString::fromUtf8("red");
    effect.aiMaskSourceRelativePath = readMask;
    effect.aiMaskManifestRelativePath = manifestRelative;
    effect.aiMaskBaseTimeOffset = fluxAIMaskGenerationTimeOffset(gui, manifestRelative, _layers[targetLayer].timeOffset);
    effect.aiMaskReadNode = readNode;
    effect.aiMaskTimeOffsetNode = timeOffsetNode;
    fluxConfigureAIMaskTimeOffsetNode(effect.aiMaskTimeOffsetNode, _layers[targetLayer].timeOffset - effect.aiMaskBaseTimeOffset);

    _layers[targetLayer].effects.append(effect);
    _layers[targetLayer].expanded = true;
    _selectedType = eFluxSelectionEffect;
    _selectedLayer = targetLayer;
    _selectedEffectIndex = _layers[targetLayer].effects.size() - 1;
    _selectedMaskIndex = -1;
    refreshVisibleRows();
    Q_EMIT effectsChanged(targetLayer);
    Q_EMIT effectSelected(targetLayer, _selectedEffectIndex);
    Q_EMIT compositingChanged();
    if (gui) {
        gui->rebuildCompositingGraph(this);
    }

    const bool hasLayerInput = (corridorNode->getInput(0) != NodePtr());
    const bool hasMaskOffset = (timeOffsetNode->getInput(0) == readNode);
    const bool hasMaskInput = (corridorNode->getInput(1) == timeOffsetNode);
    if (!hasLayerInput || !hasMaskOffset || !hasMaskInput) {
        qDebug() << "FluxTimeline::createCorridorKeyNodeForSelectedLayer: wiring verification failed"
                 << "layer" << targetLayer
                 << "effect" << _selectedEffectIndex
                 << "input0" << hasLayerInput
                 << "timeOffsetInput0" << hasMaskOffset
                 << "input1" << hasMaskInput;
        if (message) {
            *message = QString::fromUtf8("Create CorridorKey Node failed: node was created but graph wiring did not connect correctly.");
        }
        update();
        return false;
    }
    update();
    if (message) *message = QString::fromUtf8("Created CorridorKey effect node.");
    return true;
}

NodePtr
FluxTimeline::getCorridorKeyNodeForSelectedLayer() const
{
    if (_selectedLayer < 0 || _selectedLayer >= _layers.size()) {
        return NodePtr();
    }
    const FluxLayer& layer = _layers[_selectedLayer];
    for (QList<FluxEffect>::const_iterator it = layer.effects.begin(); it != layer.effects.end(); ++it) {
        if (it->pluginId == QString::fromUtf8(PLUGINID_FLUX_CORRIDOR_KEY) && it->node && it->node->isActivated()) {
            return it->node;
        }
    }
    return NodePtr();
}

bool
FluxTimeline::replaceSelectedAIMaskCopy(const QString& relativeMask, const QString& manifestRelative, QString* message, const QString& readRelativeMask)
{
    if (_selectedType != eFluxSelectionEffect || _selectedLayer < 0 || _selectedLayer >= _layers.size() || _selectedEffectIndex < 0 || _selectedEffectIndex >= _layers[_selectedLayer].effects.size()) {
        if (message) *message = QString::fromUtf8("Replace Mask failed: select an AI mask effect row or an effect with an AI mask.");
        return false;
    }
    FluxEffect& effect = _layers[_selectedLayer].effects[_selectedEffectIndex];
    Gui* gui = getGui();
    if (!gui || !gui->getApp() || !gui->getApp()->getProject()) {
        if (message) *message = QString::fromUtf8("Replace Mask failed: app/project unavailable.");
        return false;
    }

    const QString readMask = readRelativeMask.isEmpty() ? relativeMask : readRelativeMask;

    const bool isLayerAlpha = effect.aiMaskUsage == QString::fromUtf8("layer-alpha");
    const bool isEffectMask = effect.aiMaskUsage == QString::fromUtf8("effect-mask");
    const bool isLegacyCopy = effect.isAIMaskCopy;
    if (!isLayerAlpha && !isEffectMask && !isLegacyCopy) {
        if (message) *message = QString::fromUtf8("Replace Mask failed: selected row has no AI mask source to replace.");
        return false;
    }

    NodePtr newRead = fluxCreateAIReadNode(gui, readMask, _layers[_selectedLayer].sourceFrameRate);
    if (!newRead) {
        if (message) *message = QString::fromUtf8("Replace Mask failed: could not create replacement AI Read node.");
        return false;
    }
    if (effect.aiMaskReadNode) effect.aiMaskReadNode->deactivate(std::list<NodePtr>(), false, true);
    effect.aiMaskReadNode = newRead;
    // Persist the actual path used by the Read node.  For generated AI
    // sequences this is the ###### pattern; storing only the first still
    // frame makes rebuild/reopen recreate a one-frame Read.
    effect.aiMaskSourceRelativePath = readMask;
    effect.aiMaskManifestRelativePath = manifestRelative;
    effect.aiMaskBaseTimeOffset = fluxAIMaskGenerationTimeOffset(gui, manifestRelative, _layers[_selectedLayer].timeOffset);
    if (!effect.aiMaskTimeOffsetNode || !effect.aiMaskTimeOffsetNode->isActivated()) {
        effect.aiMaskTimeOffsetNode = fluxCreateNode(gui, PLUGINID_OFX_TIMEOFFSET);
    }
    if (!effect.aiMaskTimeOffsetNode) {
        if (message) *message = QString::fromUtf8("Replace Mask failed: could not create replacement AI TimeOffset node.");
        return false;
    }
    fluxConfigureAIMaskTimeOffsetNode(effect.aiMaskTimeOffsetNode, _layers[_selectedLayer].timeOffset - effect.aiMaskBaseTimeOffset);

    if (isLayerAlpha) {
        if (effect.aiMaskSourceChannel.isEmpty()) {
            effect.aiMaskSourceChannel = QString::fromUtf8("red");
        }
        effect.aiMaskChannelMergeNode = effect.node;
    } else if (isEffectMask) {
        if (effect.aiMaskSourceChannel.isEmpty()) {
            effect.aiMaskSourceChannel = QString::fromUtf8("red");
        }
        if (!effect.aiMaskShuffleNode || !effect.aiMaskShuffleNode->isActivated()) {
            effect.aiMaskShuffleNode = fluxCreateNode(gui, PLUGINID_OFX_SHUFFLE);
        }
        if (!effect.aiMaskShuffleNode) {
            if (message) *message = QString::fromUtf8("Replace Mask failed: could not create replacement AI Shuffle node.");
            return false;
        }
        effect.aiMaskShuffleNode->setLabel(QString::fromUtf8("AI Effect Mask").toStdString());
        fluxConfigureAIShuffleAlphaNode(effect.aiMaskShuffleNode, effect.aiMaskSourceChannel);
    }

    Q_EMIT effectsChanged(_selectedLayer);
    Q_EMIT compositingChanged();
    if (gui) {
        gui->rebuildCompositingGraph(this);
    }

    bool hasMaskInput = false;
    if (isEffectMask) {
        const int maskInput = discoverMaskInput(effect.node);
        const NodePtr maskSource = effect.aiMaskTimeOffsetNode ? effect.aiMaskTimeOffsetNode : newRead;
        hasMaskInput = (effect.aiMaskShuffleNode && effect.aiMaskTimeOffsetNode &&
                        effect.aiMaskTimeOffsetNode->getInput(0) == newRead &&
                        effect.aiMaskShuffleNode->getInput(0) == maskSource &&
                        maskInput >= 0 && effect.node->getInput(maskInput) == effect.aiMaskShuffleNode);
    } else {
        const NodePtr maskSource = effect.aiMaskTimeOffsetNode ? effect.aiMaskTimeOffsetNode : newRead;
        hasMaskInput = (effect.node && effect.node->getNInputs() > 1 &&
                        effect.aiMaskTimeOffsetNode && effect.aiMaskTimeOffsetNode->getInput(0) == newRead &&
                        effect.node->getInput(1) == maskSource);
    }
    if (!hasMaskInput) {
        qDebug() << "FluxTimeline::replaceSelectedAIMaskCopy: wiring verification failed"
                 << "layer" << _selectedLayer
                 << "effect" << _selectedEffectIndex
                 << "usage" << effect.aiMaskUsage
                 << "mask" << hasMaskInput;
        if (message) {
            *message = QString::fromUtf8("Replace Mask failed: AI mask source was updated but graph wiring did not verify.");
        }
        update();
        return false;
    }
    update();
    if (message) *message = QString::fromUtf8("Replaced AI mask source.");
    return true;
}

bool
FluxTimeline::removeMaskFromLayer(int layerIndex, int maskIndex)
{
    if (layerIndex < 0 || layerIndex >= _layers.size()) {
        return false;
    }
    FluxLayer& layer = _layers[layerIndex];
    if (layer.locked) {
        return false;
    }
    if (maskIndex < 0 || maskIndex >= layer.masks.size()) {
        return false;
    }

    // Deactivate graph nodes before removing model entry
    FluxMask& mask = layer.masks[maskIndex];

    // For layer masks, discover Flux-owned Unpremult from Roto input 0
    // before deactivating Roto, so we can clean it up too.
    NodePtr staleUnpremult;
    const bool isLayerMask = (mask.effectIndex < 0);
    if (isLayerMask && mask.maskNode && mask.maskNode->isActivated()) {
        NodePtr rotoIn0 = mask.maskNode->getInput(0);
        if (rotoIn0 && rotoIn0->isActivated()) {
            // Check if it's a Flux-owned Unpremult by label
            std::string lbl = rotoIn0->getLabel();
            if (lbl == "Flux Layer Mask Unpremult") {
                staleUnpremult = rotoIn0;
            }
        }
    }

    if (mask.maskNode && mask.maskNode->isActivated()) {
        mask.maskNode->deactivate(std::list<NodePtr>(), false, true);
    }
    if (mask.reformatNode && mask.reformatNode->isActivated()) {
        mask.reformatNode->deactivate(std::list<NodePtr>(), false, true);
        mask.reformatNode.reset();
    }

    // If removing a layer mask and no other layer masks remain, deactivate
    // the inline Premult (maskApplyNode) and any Flux-owned Unpremult.
    layer.masks.removeAt(maskIndex);

    if (isLayerMask) {
        bool hasOtherLayerMask = false;
        for (int m = 0; m < layer.masks.size(); ++m) {
            if (layer.masks[m].effectIndex < 0) {
                hasOtherLayerMask = true;
                break;
            }
        }
        if (!hasOtherLayerMask) {
            if (layer.maskApplyNode && layer.maskApplyNode->isActivated()) {
                layer.maskApplyNode->deactivate(std::list<NodePtr>(), false, true);
            }
            layer.maskApplyNode.reset();
            // Deactivate Flux-owned Unpremult discovered above
            if (staleUnpremult && staleUnpremult->isActivated()) {
                staleUnpremult->deactivate(std::list<NodePtr>(), false, true);
            }
        }
    }

    _selectedType = eFluxSelectionLayer;
    _selectedLayer = layerIndex;
    _selectedEffectIndex = -1;
    _selectedMaskIndex = -1;
    _selectedPropertyIndex = -1;

    refreshVisibleRows();
    Q_EMIT masksChanged(layerIndex);
    Q_EMIT compositingChanged();
    Q_EMIT layerSelected(layerIndex);
    return true;
}

bool
FluxTimeline::isAdjustmentRow(int index) const
{
    return index >= 0 && index < _layers.size() &&
           _layers[index].type == QString::fromUtf8("adjustment");
}

bool
FluxTimeline::isNullRow(int index) const
{
    return index >= 0 && index < _layers.size() &&
           _layers[index].type == QString::fromUtf8("null");
}

bool
FluxTimeline::canTrimRow(int index) const
{
    if (index < 0 || index >= _layers.size()) return false;
    const FluxLayer& l = _layers[index];
    if (l.locked) return false;
    if (l.type == QString::fromUtf8("null")) return false;
    // Adjustment rows: trim via disable-knob keyframes on each effect
    // Footage/solid rows: trim via FrameRange knob
    return true;
}

bool
FluxTimeline::canHorizontallyMoveRow(int index) const
{
    return canTrimRow(index);
}

bool
FluxTimeline::canDuplicateRow(int index) const
{
    if (index < 0 || index >= _layers.size()) return false;
    const FluxLayer& l = _layers[index];
    if (l.locked) return false;
    if (l.type == QString::fromUtf8("null")) return false;
    if (l.type == QString::fromUtf8("adjustment")) {
        // Adjustment rows with masks are not supported yet — no branch-aware copy
        if (!l.masks.isEmpty()) return false;
        // Adjustment rows can duplicate (effects only, no gizmo/merge)
        return !l.effects.isEmpty();
    }
    // Footage/solid rows: branch-aware duplicate supports masks/precomp (T063)
    if (!l.gizmoNode || !l.mergeNode) return false;
    return true;
}

bool
FluxTimeline::canSplitRow(int index) const
{
    if (index < 0 || index >= _layers.size()) return false;
    const FluxLayer& l = _layers[index];
    if (l.locked) return false;
    if (l.type == QString::fromUtf8("null")) return false;
    if (l.type == QString::fromUtf8("adjustment")) {
        // Adjustment rows with masks are not supported yet
        if (!l.masks.isEmpty()) return false;
        return !l.effects.isEmpty();
    }
    // Footage/solid rows: branch-aware split supports masks/precomp (T063)
    if (!l.gizmoNode || !l.mergeNode) return false;
    return true;
}

bool
FluxTimeline::canAddEffectToRow(int index) const
{
    if (index < 0 || index >= _layers.size()) return false;
    const FluxLayer& l = _layers[index];
    if (l.locked) return false;
    if (l.type == QString::fromUtf8("null")) return false;
    return true;
}

// Playback is driven by the viewer's render engine via the shared TimeLine.
// The viewer calls TimeLine::seekFrame() during playback, which emits
// frameChanged → our onExternalFrameChanged() updates the playhead.
// No separate play loop needed here.

void
FluxTimeline::onExternalFrameChanged(SequenceTime time,
                                     int /*reason*/)
{
    // T019-C: Update local frame from external source (Viewer, etc.)
    // Do NOT re-emit frameChanged to avoid infinite loop.
    if (time != _currentFrame) {
        _currentFrame = time;
        update();
    }
}

void
FluxTimeline::paintEvent(QPaintEvent* /*event*/)
{
    // Rebuild property rows if flagged dirty (set after keyframe edits, graph
    // changes, etc.). We do NOT poll animation state during painting — rows are
    // refreshed explicitly via refreshVisibleRows() or _rowsDirty after mutations.
    if (_rowsDirty) {
        _rowsDirty = false;
        rebuildVisibleRows();
    }

    // Safety: if layer count is out of sync (e.g. external structural change),
    // rebuild. This is a lightweight structural check only, not a full keyframe poll.
    {
        int layerRowCount = 0;
        for (const FluxVisibleRow& vr : _visibleRows) {
            if (vr.type == eFluxVisibleRowLayer) {
                ++layerRowCount;
            }
        }
        if (layerRowCount != _layers.size()) {
            rebuildVisibleRows();
        }
    }
    QPainter painter(this);

    // Style parent tabs programmatically using a dynamic property guard to avoid loops
    if (!property("tabsStyleApplied").toBool()) {
        setProperty("tabsStyleApplied", true);
        QWidget* p = this->parentWidget();
        while (p) {
            QTabBar* tabBar = p->findChild<QTabBar*>();
            if (tabBar) {
                tabBar->setStyleSheet(QStringLiteral(
                    "QTabBar { background-color: #0b0f13; border: none; }"
                    "QTabBar::tab { background-color: transparent; color: #8a909a; border: none; padding: 6px 16px; margin: 0; }"
                    "QTabBar::tab:selected { color: #ffffff; border-bottom: 2px solid #3871cc; background-color: transparent; }"
                    "QTabBar::tab:hover { color: #ffffff; }"
                    "QTabBar::close-button { image: none; }"
                    "QTabBar::close-button:hover { image: url(:/Resources/Images/close.png); }"));
            }
            if (qobject_cast<QTabWidget*>(p)) {
                QTabWidget* tabWidget = qobject_cast<QTabWidget*>(p);
                tabWidget->setStyleSheet(QStringLiteral("QTabWidget::pane { border: none; background-color: #0b0f13; }"));
            }
            p = p->parentWidget();
        }
    }

    // ── Darker visual system palette ──
    const QColor windowBg = QColor(11, 15, 19);    // #0b0f13 (window/chrome)
    const QColor panelBg = QColor(17, 22, 28);     // #11161c (panel shell)
    const QColor controlBg = QColor(23, 29, 36);   // #171d24 (inner controls / sidebar background)
    const QColor labelBg = QColor(23, 29, 36);     // #171d24 (sidebar name column background)
    const QColor accent = QColor(56, 113, 204);    // #3871cc (restrained blue accent)
    const QColor accentFill = QColor(56, 113, 204, 38);
    const QColor border = QColor(28, 34, 42);      // #1c222a (very soft separator)
    const QColor mutedText = QColor(138, 144, 154); // neutral gray text

    QRect totalRect = rect();
    painter.fillRect(totalRect, windowBg); // Outer gap color

    // Draw the timeline panel as a floating rounded card
    QRect cardRect = totalRect.adjusted(3, 3, -3, -3);
    QPainterPath cardPath;
    cardPath.addRoundedRect(cardRect, 8.0, 8.0);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillPath(cardPath, panelBg);

    // Clip all subsequent painting to the floating card
    painter.setClipPath(cardPath);

    QRect rulerRect(_layerLabelWidth, 0, totalRect.width() - _layerLabelWidth, kTimeRulerHeight);
    drawTimeRuler(painter, rulerRect);

    painter.fillRect(0, kTimeRulerHeight, kControlColumnWidth, totalRect.height() - kTimeRulerHeight, controlBg);
    painter.fillRect(kControlColumnWidth, kTimeRulerHeight, _layerLabelWidth - kControlColumnWidth, totalRect.height() - kTimeRulerHeight, labelBg);

    {
        Gui* gui = getGui();
        if (gui && gui->isFluxNodeGraphDirty()) {
            const QRect dirtyRect(0, 2, totalRect.width(), 22);
            const QColor warningBg(176, 118, 36, 200);
            painter.fillRect(dirtyRect, warningBg);
            painter.setPen(QPen(Qt::white));
            QFont bannerFont = painter.font();
            bannerFont.setPixelSize(13);
            painter.setFont(bannerFont);
            painter.drawText(dirtyRect,
                             Qt::AlignCenter,
                             QString::fromUtf8("Node graph modified \342\200\224 click here or press Ctrl+Shift+Y to sync"));
        }
    }

    {
        Gui* gui = getGui();
        if (gui && gui->isFluxNodeGraphDirty()) {
            const int syncBtnW = 48;
            const int syncBtnH = kTimeRulerHeight - 4;
            const int syncBtnX = _layerLabelWidth + 4;
            const int syncBtnY = 2;
            painter.fillRect(syncBtnX, syncBtnY, syncBtnW, syncBtnH, QColor(176, 118, 36, 220));
            painter.setPen(QPen(Qt::white));
            QFont btnFont = painter.font();
            btnFont.setPixelSize(11);
            btnFont.setBold(true);
            painter.setFont(btnFont);
            painter.drawText(QRect(syncBtnX, syncBtnY, syncBtnW, syncBtnH), Qt::AlignCenter, QString::fromUtf8("Sync"));
        }
    }

    QRect barsRect(_layerLabelWidth, kTimeRulerHeight, totalRect.width() - _layerLabelWidth, totalRect.height() - kTimeRulerHeight);
    drawLayerBars(painter, barsRect);
    drawPlayhead(painter, totalRect);
    drawSnapIndicator(painter, totalRect);

    if (_isDragOver) {
        drawDragPreview(painter, totalRect);
    }

    if (_interactionMode == eModeReorderEffect &&
        _reorderEffectLayerIndex >= 0 && _reorderEffectFromIndex >= 0 &&
        _reorderEffectTargetIndex >= 0 && _reorderEffectFromIndex != _reorderEffectTargetIndex) {
        for (int ri = 0; ri < _visibleRows.size(); ++ri) {
            const FluxVisibleRow& vr = _visibleRows[ri];
            if (vr.type != eFluxVisibleRowEffect || vr.layerIndex != _reorderEffectLayerIndex) {
                continue;
            }
            if (vr.childIndex == _reorderEffectTargetIndex) {
                const int vrY = kTimeRulerHeight + vr.y - _scrollOffsetY;
                const int lineY = (_reorderEffectTargetIndex < _reorderEffectFromIndex) ? vrY : vrY + vr.height;
                painter.setPen(QPen(accent, 2));
                painter.drawLine(_layerLabelWidth, lineY, totalRect.width(), lineY);
                painter.setBrush(accent);
                painter.setPen(Qt::NoPen);
                QPolygon triangle;
                triangle << QPoint(_layerLabelWidth, lineY - 4)
                         << QPoint(_layerLabelWidth, lineY + 4)
                         << QPoint(_layerLabelWidth + 6, lineY);
                painter.drawPolygon(triangle);
                triangle.clear();
                const int rightX = totalRect.width();
                triangle << QPoint(rightX, lineY - 4)
                         << QPoint(rightX, lineY + 4)
                         << QPoint(rightX - 6, lineY);
                painter.drawPolygon(triangle);
                painter.setBrush(Qt::NoBrush);
                break;
            }
        }
    }

    painter.setPen(border);
    painter.drawLine(0, kTimeRulerHeight, totalRect.width(), kTimeRulerHeight);
    painter.drawLine(_layerLabelWidth, 0, _layerLabelWidth, totalRect.height());

    {
        const int handleX = _layerLabelWidth;
        const int cy = totalRect.height() / 2;
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(60, 68, 78));
        for (int dy = -12; dy <= 12; dy += 6) {
            painter.drawRect(handleX - 2, cy + dy - 1, 4, 2);
        }
        painter.setBrush(Qt::NoBrush);
    }

    {
        const QRect fitBtnRect(2, 2, kControlColumnWidth - 4, kTimeRulerHeight - 4);
        painter.setPen(mutedText);
        QFont fitFont;
        fitFont.setPointSize(8);
        painter.setFont(fitFont);
        painter.drawText(fitBtnRect, Qt::AlignCenter, QString::fromUtf8("Fit"));
    }

    {
        const int toggleX = kControlColumnWidth + 4;
        const int toggleW = qMin(80, _layerLabelWidth - kControlColumnWidth - 8);
        if (toggleW > 20) {
            const QRect toggleRect(toggleX, 2, toggleW, kTimeRulerHeight - 4);
            painter.setPen(_showKeyframeCurves ? accent : mutedText);
            QFont toggleFont;
            toggleFont.setPointSize(8);
            painter.setFont(toggleFont);
            const QString toggleLabel = _showKeyframeCurves ? QString::fromUtf8("Curves") : QString::fromUtf8("Keys");
            painter.drawText(toggleRect, Qt::AlignCenter, toggleLabel);
        }
    }

    if (_interactionMode == eModeRubberBandSelect) {
        const QRect rubberRect = QRect(_rubberBandStart, _rubberBandCurrent).normalized();
        painter.setPen(QPen(accent, 1));
        painter.setBrush(accentFill);
        painter.drawRect(rubberRect);
        painter.setBrush(Qt::NoBrush);
    }
}


void
FluxTimeline::drawTimeRuler(QPainter& painter,
                            const QRect& rect)
{
    const QColor rulerBg = QColor(17, 22, 28);
    const QColor majorTick = QColor(80, 90, 105);
    const QColor minorTick = QColor(50, 56, 66);
    painter.fillRect(rect, rulerBg);

    painter.setPen(majorTick);
    QFont font;
    font.setPointSize(8);
    painter.setFont(font);

    QFontMetrics fm(font);
    int frameRange = _lastFrame - _firstFrame;
    if (frameRange <= 0) {
        return;
    }

    // Determine tick interval based on zoom
    int tickInterval = 1;
    if (_zoom < 3) {
        tickInterval = 50;
    } else if (_zoom < 6) {
        tickInterval = 20;
    } else if (_zoom < 12) {
        tickInterval = 10;
    } else if (_zoom < 30) {
        tickInterval = 5;
    }

    for (int frame = _firstFrame; frame <= _lastFrame; frame += tickInterval) {
        int x = frameToX(frame);
        if (x < rect.left() || x > rect.right()) {
            continue;
        }

        // Major tick
        painter.drawLine(x, rect.bottom() - 10, x, rect.bottom());

        // Frame number label
        QString label = QString::number(frame);
        painter.drawText(x - fm.horizontalAdvance(label) / 2, rect.top() + fm.height() + 2, label);
    }

    // Minor ticks
    int minorInterval = qMax(1, tickInterval / 5);
    for (int frame = _firstFrame; frame <= _lastFrame; frame += minorInterval) {
        if (frame % tickInterval == 0) {
            continue;
        }
        int x = frameToX(frame);
        if (x < rect.left() || x > rect.right()) {
            continue;
        }
        painter.setPen(minorTick);
        painter.drawLine(x, rect.bottom() - 4, x, rect.bottom());
        painter.setPen(majorTick);
    }
}

void
FluxTimeline::drawLayerBars(QPainter& painter,
                            const QRect& rect)
{
    for (int ri = 0; ri < _visibleRows.size(); ++ri) {
        const FluxVisibleRow& visibleRow = _visibleRows[ri];
        int y = kTimeRulerHeight + visibleRow.y - _scrollOffsetY;
        int rowHeight = visibleRow.height;

        if (y + rowHeight < rect.top() || y > rect.bottom()) {
            continue;
        }

        if (visibleRow.type == eFluxVisibleRowLayer) {
            int i = visibleRow.layerIndex;
            const FluxLayer& layer = _layers[i];

            bool isSelected = (_selectedType == eFluxSelectionLayer && i == _selectedLayer);

            // ── Control and name columns background (unified sidebar) ──
            QRect sidebarRect(0, y, _layerLabelWidth, rowHeight);
            QRect gridRect(_layerLabelWidth, y, width() - _layerLabelWidth, rowHeight);
            if (isSelected) {
                painter.fillRect(sidebarRect, QColor(30, 48, 80));
                painter.fillRect(gridRect, QColor(23, 33, 50));
                painter.setPen(QColor(230, 235, 245));
            } else {
                painter.fillRect(sidebarRect, QColor(23, 29, 36));
                painter.fillRect(gridRect, QColor(17, 22, 28));
                painter.setPen(QColor(185, 190, 200));
            }
            int nameColX = kControlColumnWidth;
            int nameColW = _layerLabelWidth - kControlColumnWidth;
            QRect nameRect(nameColX, y, nameColW, rowHeight);
            // Disclosure arrow if layer has visible children or animated property rows
            const bool hasChildren = layerHasExpandableChildren(i);
            int textLeftPad = 4;
            if (hasChildren) {
                int arrowX = nameColX + 4;
                int arrowY = y + (rowHeight - kDisclosureSize) / 2;
                painter.setPen(isSelected ? Qt::white : QColor(180, 180, 190));
                painter.setBrush(isSelected ? Qt::white : QColor(180, 180, 190));
                if (layer.expanded) {
                    // Down triangle
                    QPainterPath tri;
                    tri.moveTo(arrowX, arrowY);
                    tri.lineTo(arrowX + kDisclosureSize, arrowY);
                    tri.lineTo(arrowX + kDisclosureSize / 2, arrowY + kDisclosureSize);
                    tri.closeSubpath();
                    painter.drawPath(tri);
                } else {
                    // Right triangle
                    QPainterPath tri;
                    tri.moveTo(arrowX, arrowY);
                    tri.lineTo(arrowX + kDisclosureSize, arrowY + kDisclosureSize / 2);
                    tri.lineTo(arrowX, arrowY + kDisclosureSize);
                    tri.closeSubpath();
                    painter.drawPath(tri);
                }
                painter.setBrush(Qt::NoBrush);
                textLeftPad = kDisclosureSize + 8;
            }

            // Precomp indicator icon — drawn before layer name
            if (layer.hasPrecompBranch) {
                QFont iconFont;
                iconFont.setPointSize(9);
                painter.setFont(iconFont);
                painter.setPen(QColor(180, 160, 90));
                int iconX = nameColX + textLeftPad;
                painter.drawText(QRect(iconX, y, 16, rowHeight),
                                 Qt::AlignVCenter | Qt::AlignLeft,
                                 QString::fromUtf8("\xE2\x97\x86")); // ◆
                textLeftPad += 16;
            }

            // Layer name with proper elision
            QFont font;
            font.setPointSize(9);
            painter.setFont(font);
            QFontMetrics fm(font);
            // Badge width reservation for viewer-input badges
            int badgeReserveW = 0;
            if (!layer.viewerInputBadges.isEmpty()) {
                badgeReserveW = layer.viewerInputBadges.size() * 16 + (layer.viewerInputBadges.size() - 1) * 2 + 4;
            }

            int availableWidth = nameRect.width() - textLeftPad - 4 - badgeReserveW;
            QString displayName = fm.elidedText(layer.name, Qt::ElideRight, qMax(0, availableWidth));
            painter.drawText(nameRect.adjusted(textLeftPad, 0, -4 - badgeReserveW, 0), Qt::AlignVCenter | Qt::AlignLeft, displayName);

            // ── Viewer-input badges on layer row ──
            if (!layer.viewerInputBadges.isEmpty()) {
                QFont badgeFont;
                badgeFont.setPointSize(7);
                badgeFont.setBold(true);
                painter.setFont(badgeFont);
                QFontMetrics bfm(badgeFont);
                int bx = nameRect.right() - 4;
                for (int bi = layer.viewerInputBadges.size() - 1; bi >= 0; --bi) {
                    int bnum = layer.viewerInputBadges[bi];
                    QString btxt = QString::number(bnum);
                    int bw = qMax(14, bfm.horizontalAdvance(btxt) + 6);
                    bx -= bw;
                    QRect badgeRect(bx, y + (rowHeight - 14) / 2, bw, 14);
                    painter.setPen(Qt::NoPen);
                    painter.setBrush(QColor(40, 90, 170));
                    painter.drawRoundedRect(badgeRect, 3, 3);
                    painter.setBrush(Qt::NoBrush);
                    painter.setPen(Qt::white);
                    painter.drawText(badgeRect, Qt::AlignCenter, btxt);
                    bx -= 2; // gap between badges
                }
                // Restore font
                painter.setFont(font);
            }

            // ── L/V/S buttons in control column at fixed positions ──
            QFont smallFont;
            smallFont.setPointSize(8);
            painter.setFont(smallFont);
            int btnY = y;
            int btnH = rowHeight;
            int btnInsetY = 6;
            int btnHeight = btnH - 12;

            // L button (Lock) at x=4
            int lockBtnX = 4;
            if (layer.locked) {
                painter.setPen(QColor(95, 137, 216)); // active blue accent
            } else {
                painter.setPen(QColor(90, 95, 105)); // dim gray
            }
            painter.drawText(QRect(lockBtnX, btnY, 16, btnH), Qt::AlignCenter, QString::fromUtf8("L"));

            // V button (Visibility) at x=20
            int visBtnX = 20;
            if (!layer.muted) {
                painter.setPen(QColor(95, 137, 216)); // active blue accent (visible)
            } else {
                painter.setPen(QColor(90, 95, 105)); // dim gray (hidden)
            }
            painter.drawText(QRect(visBtnX, btnY, 16, btnH), Qt::AlignCenter, QString::fromUtf8("V"));

            // S button (Solo) at x=36
            int soloBtnX = 36;
            if (isAdjustmentRow(i)) {
                painter.setPen(QColor(50, 50, 55)); // disabled/dim
            } else if (layer.solo) {
                painter.setPen(QColor(95, 137, 216)); // active blue accent
            } else {
                painter.setPen(QColor(90, 95, 105)); // dim gray
            }
            painter.drawText(QRect(soloBtnX, btnY, 16, btnH), Qt::AlignCenter, QString::fromUtf8("S"));

            // ── Layer bar (right area) ──
            // Step 1: compute positions from inPoint/outPoint (source frames)
            // Step 2: add timeOffset to shift the drawn position on the timeline
            int drawStart = layer.inPoint + layer.timeOffset;
            int drawEnd = layer.outPoint + layer.timeOffset;
            int barX1 = frameToX(drawStart);
            int barX2 = frameToX(drawEnd);

            // Clip bar to the right of the left panel
            int clipLeft = _layerLabelWidth;
            if (barX1 < clipLeft) {
                barX1 = clipLeft;
            }
            QRect barRect(barX1, y + 4, barX2 - barX1, rowHeight - 8);

            // Determine effective visibility:
            // If any layer is soloed, only soloed layers are visible (adjustment rows ignore solo)
            bool anySoloed = false;
            for (int j = 0; j < _layers.size(); ++j) {
                if (_layers[j].solo && !isAdjustmentRow(j)) {
                    anySoloed = true;
                    break;
                }
            }
            // Dim bar if muted or solo-muted (adjustment rows: mute only, no solo effect)
            bool effectivelyMuted = layer.muted || (!isAdjustmentRow(i) && anySoloed && !layer.solo);

            if (barRect.width() > 0) {
                // Compute active zone (where actual source frames play) vs desaturated zones
                // originalInPoint/originalOutPoint define the real source boundaries
                int origDrawStart = layer.originalInPoint + layer.timeOffset;
                int origDrawEnd = layer.originalOutPoint + layer.timeOffset;

                // Active zone = intersection of [drawStart,drawEnd] with [origDrawStart,origDrawEnd]
                int activeX1 = frameToX(qMax(drawStart, origDrawStart));
                int activeX2 = frameToX(qMin(drawEnd, origDrawEnd));

                QPainterPath barPath;
                barPath.addRoundedRect(barRect, 6.0, 6.0);

                QColor desatColor = layer.color.darker(150);
                if (effectivelyMuted) {
                    desatColor = desatColor.darker(180);
                }
                if (layer.locked) {
                    desatColor = desatColor.darker(140);
                }
                if (isSelected) {
                    desatColor = desatColor.lighter(110);
                }

                painter.save();
                painter.setClipPath(barPath, Qt::IntersectClip);
                painter.fillPath(barPath, desatColor);

                if (activeX2 > activeX1) {
                    QRect activeRect(activeX1, barRect.top(), activeX2 - activeX1, barRect.height());
                    QColor activeColor = effectivelyMuted ? layer.color.darker(170) : layer.color;
                    if (layer.locked) {
                        activeColor = activeColor.darker(130);
                    }
                    if (isSelected) {
                        activeColor = activeColor.lighter(115);
                    } else {
                        activeColor = activeColor.darker(110);
                    }
                    painter.fillRect(activeRect, activeColor);
                }
                painter.restore();

                QColor borderColor = isSelected ? layer.color.lighter(120) : layer.color.darker(140);
                painter.setPen(QPen(borderColor, isSelected ? 1.5 : 1.0));
                painter.drawPath(barPath);
                // Locked indicator: thin diagonal hatch overlay
                if (layer.locked) {
                    painter.setPen(QPen(QColor(255, 255, 255, 25), 1));
                    for (int hx = barRect.left() - barRect.height(); hx < barRect.right(); hx += 8) {
                        painter.drawLine(hx, barRect.bottom(), hx + barRect.height(), barRect.top());
                    }
                }

                // Trim handle highlights (only for rows that support trimming)
                if (canTrimRow(i) && barRect.width() > kTrimHandleWidth * 2) {
                    // Left trim handle zone
                    QRect leftHandle(barRect.left(), barRect.top(), kTrimHandleWidth, barRect.height());
                    painter.fillRect(leftHandle, QColor(255, 255, 255, 30));

                    // Right trim handle zone
                    QRect rightHandle(barRect.right() - kTrimHandleWidth, barRect.top(), kTrimHandleWidth, barRect.height());
                    painter.fillRect(rightHandle, QColor(255, 255, 255, 30));
                }

                // Bar text (filename)
                if (barRect.width() > 40) {
                    painter.setPen(layer.locked ? QColor(180, 180, 180) : Qt::white);
                    QFont barFont;
                    barFont.setPointSize(8);
                    painter.setFont(barFont);
                    QFileInfo fi(layer.filePath);
                    QString barText = fi.fileName();
                    if (barText.isEmpty()) {
                        barText = layer.name;
                    }
                    painter.drawText(barRect.adjusted(4, 0, -4, 0), Qt::AlignVCenter | Qt::AlignLeft, barText);
                }
            }

            // Row separator
            // Row separator
            painter.setPen(QColor(28, 34, 42));
            painter.drawLine(0, y + rowHeight, width(), y + rowHeight);

        } else if (visibleRow.type == eFluxVisibleRowTextAnimator) {
            int li = visibleRow.layerIndex;
            if (li < 0 || li >= _layers.size()) {
                continue;
            }
            QString label = QString::fromUtf8("Text Animator %1").arg(visibleRow.childIndex);
            for (const FluxTextAnimatorSummary& a : FluxTextAnimatorModel::animators(_layers[li].gizmoNode)) {
                if (a.id == visibleRow.childIndex) {
                    label = a.name;
                    break;
                }
            }

            const bool isAnimatorSelected = (_selectedType == eFluxSelectionTextAnimator &&
                                             _selectedLayer == li &&
                                             _selectedEffectIndex == visibleRow.childIndex);
            QRect sidebarRect(0, y, _layerLabelWidth, rowHeight);
            QRect gridRect(_layerLabelWidth, y, width() - _layerLabelWidth, rowHeight);
            painter.fillRect(sidebarRect, isAnimatorSelected ? QColor(32, 40, 54) : QColor(17, 22, 28));
            painter.fillRect(gridRect, isAnimatorSelected ? QColor(24, 31, 42) : QColor(14, 18, 24));
            painter.setPen(QColor(28, 34, 42));
            painter.drawLine(0, y + rowHeight, width(), y + rowHeight);

            int labelX = kControlColumnWidth + kEffectIndent;
            QRect labelRect(labelX, y, _layerLabelWidth - labelX - 4, rowHeight);
            QFont font;
            font.setPointSize(8);
            painter.setFont(font);
            QFontMetrics fm(font);
            painter.setPen(isAnimatorSelected ? QColor(229, 233, 240) : QColor(170, 176, 186));
            painter.drawText(labelRect, Qt::AlignVCenter | Qt::AlignLeft,
                             QString::fromUtf8("A ") + fm.elidedText(label, Qt::ElideRight, qMax(0, labelRect.width() - 16)));

        } else if (visibleRow.type == eFluxVisibleRowEffect) {
            int li = visibleRow.layerIndex;
            int ei = visibleRow.childIndex;
            if (li < 0 || li >= _layers.size() || ei < 0 || ei >= _layers[li].effects.size()) {
                continue;
            }
            const FluxEffect& effect = _layers[li].effects[ei];

            bool isEffectSelected = (_selectedType == eFluxSelectionEffect &&
                                     _selectedLayer == li &&
                                     _selectedEffectIndex == ei);

            QRect sidebarRect(0, y, _layerLabelWidth, rowHeight);
            QRect gridRect(_layerLabelWidth, y, width() - _layerLabelWidth, rowHeight);
            painter.fillRect(sidebarRect, isEffectSelected ? QColor(32, 40, 54) : QColor(17, 22, 28));
            painter.fillRect(gridRect, isEffectSelected ? QColor(24, 31, 42) : QColor(14, 18, 24));

            if (_interactionMode == eModeReorderEffect &&
                _reorderEffectLayerIndex == li && _reorderEffectFromIndex == ei &&
                _reorderEffectFromIndex != _reorderEffectTargetIndex) {
                painter.fillRect(0, y, width(), rowHeight, QColor(0, 0, 0, 80));
            }

            painter.setPen(QColor(28, 34, 42));
            painter.drawLine(0, y + rowHeight, width(), y + rowHeight);

            painter.setPen(effect.enabled ? QColor(95, 137, 216) : QColor(90, 95, 105));
            painter.drawText(QRect(20, y, 16, rowHeight), Qt::AlignCenter, QString::fromUtf8("V"));
            int labelX = kControlColumnWidth + kEffectIndent;

            int effectBadgeReserveW = 0;
            if (!effect.viewerInputBadges.isEmpty()) {
                effectBadgeReserveW = effect.viewerInputBadges.size() * 16 + (effect.viewerInputBadges.size() - 1) * 2 + 4;
            }

            QRect effectLabelRect(labelX, y, _layerLabelWidth - labelX - 4 - effectBadgeReserveW, rowHeight);
            QFont effectFont;
            effectFont.setPointSize(8);
            painter.setFont(effectFont);
            painter.setPen(isEffectSelected ? QColor(229, 233, 240) : (effect.enabled ? QColor(170, 176, 186) : QColor(105, 112, 122)));
            QFontMetrics efm(effectFont);
            QString effectLabel = efm.elidedText(effect.label, Qt::ElideRight, qMax(0, effectLabelRect.width()));

            painter.setPen(isEffectSelected ? QColor(95, 137, 216) : (effect.enabled ? QColor(120, 140, 180) : QColor(80, 90, 105)));
            painter.drawText(effectLabelRect, Qt::AlignVCenter | Qt::AlignLeft,
                             QString::fromUtf8("\xE2\x97\x8F ") + effectLabel);

            if (!effect.viewerInputBadges.isEmpty()) {
                QFont badgeFont;
                badgeFont.setPointSize(7);
                badgeFont.setBold(true);
                painter.setFont(badgeFont);
                QFontMetrics bfm(badgeFont);
                int badgeAreaRight = _layerLabelWidth - 4;
                int bx = badgeAreaRight;
                for (int bi = effect.viewerInputBadges.size() - 1; bi >= 0; --bi) {
                    int bnum = effect.viewerInputBadges[bi];
                    QString btxt = QString::number(bnum);
                    int bw = qMax(14, bfm.horizontalAdvance(btxt) + 6);
                    bx -= bw;
                    QRect badgeRect(bx, y + (rowHeight - 14) / 2, bw, 14);
                    painter.setPen(Qt::NoPen);
                    painter.setBrush(QColor(40, 90, 170));
                    painter.drawRoundedRect(badgeRect, 3, 3);
                    painter.setBrush(Qt::NoBrush);
                    painter.setPen(Qt::white);
                    painter.drawText(badgeRect, Qt::AlignCenter, btxt);
                    bx -= 2;
                }
                painter.setFont(effectFont);
            }

            // Separator in name column removed
        } else if (visibleRow.type == eFluxVisibleRowMask) {
            int li = visibleRow.layerIndex;
            int mi = visibleRow.maskIndex;
            if (li < 0 || li >= _layers.size() || mi < 0 || mi >= _layers[li].masks.size()) {
                continue;
            }
            const FluxMask& mask = _layers[li].masks[mi];

            bool isMaskSelected = (_selectedType == eFluxSelectionMask &&
                                   _selectedLayer == li &&
                                   _selectedMaskIndex == mi);

            // Dark subtle row background across full width
            QColor rowBg = isMaskSelected ? QColor(46, 93, 174) : QColor(30, 30, 34);
            QRect sidebarRect(0, y, _layerLabelWidth, rowHeight);
            QRect gridRect(_layerLabelWidth, y, width() - _layerLabelWidth, rowHeight);
            if (isMaskSelected) {
                painter.fillRect(sidebarRect, QColor(30, 48, 80));
                painter.fillRect(gridRect, QColor(23, 33, 50));
            } else {
                painter.fillRect(sidebarRect, QColor(23, 29, 36));
                painter.fillRect(gridRect, QColor(17, 22, 28));
            }
            painter.setPen(QColor(28, 34, 42));
            painter.drawLine(0, y + rowHeight, width(), y + rowHeight);
            QRect enableRect(20, y + 4, 16, qMax(10, rowHeight - 8));
            if (mask.enabled) {
                painter.setPen(QColor(95, 137, 216));
            } else {
                painter.setPen(QColor(90, 95, 105));
            }
            painter.drawText(QRect(20, y, 16, rowHeight), Qt::AlignCenter, QString::fromUtf8("V"));
            int labelX = kControlColumnWidth + kEffectIndent + 12;
            QRect maskLabelRect(labelX, y, _layerLabelWidth - labelX - 4, rowHeight);
            QFont maskFont;
            maskFont.setPointSize(8);
            painter.setFont(maskFont);
            QFontMetrics mfm(maskFont);
            int maskAvailW = maskLabelRect.width();
            QString maskLabel = mask.name.isEmpty() ? QString::fromUtf8("Mask") : mask.name;
            maskLabel = mfm.elidedText(maskLabel, Qt::ElideRight, qMax(0, maskAvailW));

            painter.setPen(isMaskSelected ? QColor(200, 200, 255) : (mask.enabled ? QColor(100, 160, 140) : QColor(70, 95, 88)));
            painter.drawText(maskLabelRect, Qt::AlignVCenter | Qt::AlignLeft,
                             QString::fromUtf8("M ") + maskLabel);

            // Separator in name column
            // Separator in name column removed
        } else if (visibleRow.type == eFluxVisibleRowProperty) {
            // ── Animated property row ──
            if (visibleRow.propertyIndex < 0 || visibleRow.propertyIndex >= _propertyRows.size()) {
                continue;
            }
            const FluxKeyframeProperty& prop = _propertyRows[visibleRow.propertyIndex];

            bool isPropSelected = (_selectedType == eFluxSelectionProperty &&
                                   _selectedPropertyIndex == visibleRow.propertyIndex);

            // Subtle indented row background
            QColor propBg = isPropSelected ? QColor(42, 50, 68) : QColor(28, 28, 32);
            QRect sidebarRect(0, y, _layerLabelWidth, rowHeight);
            QRect gridRect(_layerLabelWidth, y, width() - _layerLabelWidth, rowHeight);
            if (isPropSelected) {
                painter.fillRect(sidebarRect, QColor(30, 48, 80));
                painter.fillRect(gridRect, QColor(23, 33, 50));
            } else {
                painter.fillRect(sidebarRect, QColor(23, 29, 36));
                painter.fillRect(gridRect, QColor(17, 22, 28));
            }
            int propIndent = kControlColumnWidth + kEffectIndent + 8;
            QRect propLabelRect(propIndent, y, _layerLabelWidth - propIndent - 4, rowHeight);
            QFont propFont;
            propFont.setPointSize(7);
            painter.setFont(propFont);
            QFontMetrics pfm(propFont);
            QString propLabel = pfm.elidedText(prop.label, Qt::ElideRight, qMax(0, propLabelRect.width()));
            painter.setPen(QColor(150, 155, 170));
            painter.drawText(propLabelRect, Qt::AlignVCenter | Qt::AlignLeft, propLabel);

            // Separator in name column
            painter.setPen(QColor(28, 34, 42));
            painter.drawLine(0, y + rowHeight, width(), y + rowHeight);
            // ── Draw keyframe diamonds in the timeline area ──
            QList<FluxKeyframeKey> keys = keysForProperty(prop);

            // Clip: skip if row entirely outside visible timeline rect
            int timelineLeft = _layerLabelWidth;
            int timelineRight = width();
            if (y + rowHeight < rect.top() || y > rect.bottom()) {
                // Already handled by outer clip, but skip keys if row invisible
                continue;
            }

            const int diamondSize = 6; // half-width of diamond
            int diamondY = y + rowHeight / 2;

            bool isThisDragRow = (_interactionMode == eModeDragKeyframe &&
                                  _dragPropertyIndex == visibleRow.propertyIndex);

            for (const FluxKeyframeKey& key : keys) {
                // Skip the key being dragged (primary or multi-selected) — we draw ghosts
                bool isDraggedKey = false;
                if (_interactionMode == eModeDragKeyframe) {
                    isDraggedKey = isKeyframeSelected(visibleRow.propertyIndex, key.time);
                }
                if (isDraggedKey) {
                    // Draw ghost at original position
                    int keyX = frameToX(static_cast<int>(key.time + 0.5));
                    if (keyX >= timelineLeft - diamondSize && keyX <= timelineRight + diamondSize) {
                        QPainterPath diamond;
                        diamond.moveTo(keyX, diamondY - diamondSize);
                        diamond.lineTo(keyX + diamondSize, diamondY);
                        diamond.lineTo(keyX, diamondY + diamondSize);
                        diamond.lineTo(keyX - diamondSize, diamondY);
                        diamond.closeSubpath();
                        QColor ghostColor(120, 110, 60, 100);
                        painter.setPen(QPen(QColor(120, 110, 60, 120), 1));
                        painter.setBrush(ghostColor);
                        painter.drawPath(diamond);
                        painter.setBrush(Qt::NoBrush);
                    }
                    continue;
                }

                int keyX = frameToX(static_cast<int>(key.time + 0.5));

                // Skip if outside visible timeline area
                if (keyX < timelineLeft - diamondSize || keyX > timelineRight + diamondSize) {
                    continue;
                }

                QPainterPath diamond;
                diamond.moveTo(keyX, diamondY - diamondSize);
                diamond.lineTo(keyX + diamondSize, diamondY);
                diamond.lineTo(keyX, diamondY + diamondSize);
                diamond.lineTo(keyX - diamondSize, diamondY);
                diamond.closeSubpath();

                const bool isSelectedKey = isKeyframeSelected(visibleRow.propertyIndex, key.time);

                if (key.isPartial) {
                    // Hollow diamond (partial grouped keys)
                    painter.setPen(QPen(isSelectedKey ? QColor(255, 240, 140) : QColor(200, 180, 80),
                                        isSelectedKey ? 2 : 1));
                    painter.setBrush(Qt::NoBrush);
                    painter.drawPath(diamond);
                } else {
                    // Solid diamond (full grouped keys)
                    painter.setPen(isSelectedKey ? QPen(QColor(255, 240, 140), 2) : Qt::NoPen);
                    painter.setBrush(QColor(200, 180, 80));
                    painter.drawPath(diamond);
                    painter.setBrush(Qt::NoBrush);
                }
            }

            // ── Inline curve preview (only when _showKeyframeCurves && knob-backed) ──
            if (_showKeyframeCurves && !prop.isRotoAggregate && prop.knob) {
                // Determine visible frame range in pixels
                int visLeftFrame = xToFrame(timelineLeft);
                int visRightFrame = xToFrame(timelineRight);
                int frameSpan = visRightFrame - visLeftFrame;
                if (frameSpan < 1) {
                    frameSpan = 1;
                }

                // Subtle dim colors per dimension
                static const QColor kDimColors[] = {
                    QColor(100, 180, 255, 120),  // blue
                    QColor(255, 130, 100, 120),  // red-orange
                    QColor(100, 255, 140, 120),  // green
                    QColor(255, 220, 100, 120),  // yellow
                    QColor(200, 130, 255, 120),  // purple
                    QColor(255, 180, 200, 120),  // pink
                    QColor(130, 255, 255, 120),  // cyan
                    QColor(255, 160, 80, 120)    // orange
                };
                const int kNumDimColors = 8;

                int rowPad = 2;
                int plotTop = y + rowPad;
                int plotHeight = rowHeight - rowPad * 2;
                if (plotHeight < 4) {
                    plotHeight = 4;
                }

                for (size_t di = 0; di < prop.dims.size(); ++di) {
                    int dim = prop.dims[di];
                    std::shared_ptr<Curve> curve = prop.knob->getCurve(ViewSpec::current(), dim);
                    if (!curve) {
                        continue;
                    }

                    // Sample the curve at ~2 samples per pixel for smoothness
                    int numSamples = qMax(2, (timelineRight - timelineLeft) * 2);
                    numSamples = qMin(numSamples, 4000); // cap for performance
                    double sampleStep = (double)frameSpan / numSamples;

                    // First pass: compute min/max from samples
                    double yMin = 1e30, yMax = -1e30;
                    for (int s = 0; s <= numSamples; ++s) {
                        double sampleFrame = visLeftFrame + s * sampleStep;
                        double val = curve->getValueAt(sampleFrame);
                        if (val < yMin) yMin = val;
                        if (val > yMax) yMax = val;
                    }

                    double yRange = yMax - yMin;
                    bool isFlat = (yRange < 1e-10);

                    // Build the polyline
                    QPainterPath linePath;
                    bool first = true;
                    for (int s = 0; s <= numSamples; ++s) {
                        double sampleFrame = visLeftFrame + s * sampleStep;
                        double val = curve->getValueAt(sampleFrame);
                        int px = frameToX(static_cast<int>(sampleFrame + 0.5));
                        int py;
                        if (isFlat) {
                            py = plotTop + plotHeight / 2;
                        } else {
                            double normalized = (val - yMin) / yRange;
                            // Invert: higher values toward top
                            py = plotTop + static_cast<int>((1.0 - normalized) * (plotHeight - 1));
                        }
                        if (first) {
                            linePath.moveTo(px, py);
                            first = false;
                        } else {
                            linePath.lineTo(px, py);
                        }
                    }

                    QColor dimColor = kDimColors[di % kNumDimColors];
                    painter.setPen(QPen(dimColor, 1.2));
                    painter.setBrush(Qt::NoBrush);
                    painter.drawPath(linePath);
                }
            }

            // Draw all dragged selected keys at their current target positions
            if (_interactionMode == eModeDragKeyframe && !_selectedKeys.isEmpty()) {
                double delta = _dragCurrentKeyTime - _dragOrigKeyTime;
                for (const SelectedKeyframe& sk : _selectedKeys) {
                    if (sk.propertyIndex != visibleRow.propertyIndex) {
                        continue;
                    }
                    double newTime = sk.keyTime + delta;
                    int dragX = frameToX(static_cast<int>(newTime + 0.5));
                    if (dragX >= timelineLeft - diamondSize && dragX <= timelineRight + diamondSize) {
                        QPainterPath diamond;
                        diamond.moveTo(dragX, diamondY - diamondSize);
                        diamond.lineTo(dragX + diamondSize, diamondY);
                        diamond.lineTo(dragX, diamondY + diamondSize);
                        diamond.lineTo(dragX - diamondSize, diamondY);
                        diamond.closeSubpath();
                        painter.setPen(QPen(QColor(255, 220, 80), 2));
                        painter.setBrush(QColor(255, 220, 80));
                        painter.drawPath(diamond);
                        painter.setBrush(Qt::NoBrush);
                    }
                }
            } else if (isThisDragRow) {
                // Single keyframe drag (no multi-select)
                int dragX = frameToX(static_cast<int>(_dragCurrentKeyTime + 0.5));
                if (dragX >= timelineLeft - diamondSize && dragX <= timelineRight + diamondSize) {
                    QPainterPath diamond;
                    diamond.moveTo(dragX, diamondY - diamondSize);
                    diamond.lineTo(dragX + diamondSize, diamondY);
                    diamond.lineTo(dragX, diamondY + diamondSize);
                    diamond.lineTo(dragX - diamondSize, diamondY);
                    diamond.closeSubpath();
                    painter.setPen(QPen(QColor(255, 220, 80), 2));
                    painter.setBrush(QColor(255, 220, 80));
                    painter.drawPath(diamond);
                    painter.setBrush(Qt::NoBrush);
                }
            }
        }
    }
}

void
FluxTimeline::drawPlayhead(QPainter& painter,
                           const QRect& rect)
{
    int x = frameToX(_currentFrame);
    if (x < _layerLabelWidth) {
        return;
    }

    const QColor playheadColor = QColor(56, 113, 204);
    painter.setPen(QPen(playheadColor, 1));
    painter.drawLine(x, 0, x, rect.height());

    QPainterPath triangle;
    triangle.moveTo(x - 4, 0);
    triangle.lineTo(x + 4, 0);
    triangle.lineTo(x, 7);
    triangle.closeSubpath();
    painter.fillPath(triangle, playheadColor);

    painter.setPen(Qt::white);
    QFont font;
    font.setPointSize(7);
    font.setBold(false);
    painter.setFont(font);
    painter.drawText(x + 4, 10, QString::number(_currentFrame));
}

void
FluxTimeline::drawDragPreview(QPainter& painter,
                              const QRect& /*rect*/)
{
    if (!_isDragOver) {
        return;
    }

    int x = _dragPreviewPos.x();
    int y = _dragPreviewPos.y();
    if (x <= _layerLabelWidth || y <= kTimeRulerHeight) {
        return;
    }

    int inFrame = xToFrame(x);
    int outFrame = inFrame + 50; // Default 50-frame duration for preview

    int row = insertionLayerIndexForY(y);
    if (row < 0) {
        row = 0;
    }
    if (row > _layers.size()) {
        row = _layers.size();
    }

    int barY = insertionYForLayerIndex(row) + 4;
    int barX1 = frameToX(inFrame);
    int barX2 = frameToX(outFrame);

    QColor ghostColor = FluxStyle::withAlpha(FluxStyle::accent(this), 0.35);
    QRect ghostRect(barX1, barY, barX2 - barX1, kLayerRowHeight - 8);
    painter.fillRect(ghostRect, ghostColor);

    painter.setPen(QPen(FluxStyle::withAlpha(FluxStyle::accent(this), 0.75), 1, Qt::DashLine));
    painter.drawRect(ghostRect);

    painter.setPen(FluxStyle::withAlpha(FluxStyle::text(this), 0.85));
    QFont font;
    font.setPointSize(8);
    painter.setFont(font);
    painter.drawText(ghostRect.adjusted(4, 0, -4, 0), Qt::AlignVCenter | Qt::AlignLeft,
                     QString::fromUtf8("Drop here (frame %1)").arg(inFrame));
}

int
FluxTimeline::frameToX(int frame) const
{
    return _layerLabelWidth + (int)( (frame - _firstFrame) * _zoom ) - _scrollOffsetX;
}

int
FluxTimeline::xToFrame(int x) const
{
    int localX = x - _layerLabelWidth + _scrollOffsetX;
    return _firstFrame + (int)( localX / _zoom );
}

void
FluxTimeline::rebuildVisibleRows()
{
    _visibleRows.clear();
    _propertyRows.clear();
    _selectedKeyPropertyIndex = -1;
    _selectedKeyTime = 0.0;
    _selectedKeys.clear();
    int y = 0;
    for (int i = 0; i < _layers.size(); ++i) {
        FluxVisibleRow row;
        row.type = eFluxVisibleRowLayer;
        row.layerIndex = i;
        row.childIndex = -1;
        row.y = y;
        row.height = kLayerRowHeight;
        _visibleRows.append(row);
        y += row.height;

        if (_layers[i].expanded) {
            if (_layers[i].type == QString::fromUtf8("text")) {
                QList<FluxTextAnimatorSummary> textAnimators = FluxTextAnimatorModel::animators(_layers[i].gizmoNode);
                for (int a = 0; a < textAnimators.size(); ++a) {
                    FluxVisibleRow animRow;
                    animRow.type = eFluxVisibleRowTextAnimator;
                    animRow.layerIndex = i;
                    animRow.childIndex = textAnimators[a].id;
                    animRow.effectIndex = -1;
                    animRow.maskIndex = -1;
                    animRow.y = y;
                    animRow.height = kEffectRowHeight;
                    _visibleRows.append(animRow);
                    y += animRow.height;
                }
            }

            // Layer-level animated property rows
            {
                QList<FluxKeyframeProperty> props = buildLayerProperties(_layers[i], _ungroupedKeyframeProperties);
                for (int p = 0; p < props.size(); ++p) {
                    int propIdx = _propertyRows.size();
                    _propertyRows.append(props[p]);
                    FluxVisibleRow propRow;
                    propRow.type = eFluxVisibleRowProperty;
                    propRow.layerIndex = i;
                    propRow.childIndex = -1;
                    propRow.effectIndex = -1;
                    propRow.maskIndex = -1;
                    propRow.propertyIndex = propIdx;
                    propRow.y = y;
                    propRow.height = kPropertyRowHeight;
                    _visibleRows.append(propRow);
                    y += propRow.height;
                }
            }

            // Layer-level masks (effectIndex < 0)
            const QList<FluxMask>& masks = _layers[i].masks;
            for (int m = 0; m < masks.size(); ++m) {
                if (masks[m].effectIndex >= 0) {
                    continue;
                }
                FluxVisibleRow maskRow;
                maskRow.type = eFluxVisibleRowMask;
                maskRow.layerIndex = i;
                maskRow.childIndex = m;
                maskRow.effectIndex = -1;
                maskRow.maskIndex = m;
                maskRow.y = y;
                maskRow.height = kMaskRowHeight;
                _visibleRows.append(maskRow);
                y += maskRow.height;

                // Mask property rows
                {
                    QList<FluxKeyframeProperty> props = buildMaskProperties(masks[m]);
                    for (int p = 0; p < props.size(); ++p) {
                        int propIdx = _propertyRows.size();
                        _propertyRows.append(props[p]);
                        FluxVisibleRow propRow;
                        propRow.type = eFluxVisibleRowProperty;
                        propRow.layerIndex = i;
                        propRow.childIndex = -1;
                        propRow.effectIndex = -1;
                        propRow.maskIndex = m;
                        propRow.propertyIndex = propIdx;
                        propRow.y = y;
                        propRow.height = kPropertyRowHeight;
                        _visibleRows.append(propRow);
                        y += propRow.height;
                    }
                }
            }

            // Effects and their masks
            const QList<FluxEffect>& effects = _layers[i].effects;
            for (int e = 0; e < effects.size(); ++e) {
                FluxVisibleRow effectRow;
                effectRow.type = eFluxVisibleRowEffect;
                effectRow.layerIndex = i;
                effectRow.childIndex = e;
                effectRow.effectIndex = e;
                effectRow.maskIndex = -1;
                effectRow.y = y;
                effectRow.height = kEffectRowHeight;
                _visibleRows.append(effectRow);
                y += effectRow.height;

                // Effect property rows
                {
                    QList<FluxKeyframeProperty> props = buildEffectProperties(effects[e], isAdjustmentRow(i), _ungroupedKeyframeProperties);
                    for (int p = 0; p < props.size(); ++p) {
                        int propIdx = _propertyRows.size();
                        _propertyRows.append(props[p]);
                        FluxVisibleRow propRow;
                        propRow.type = eFluxVisibleRowProperty;
                        propRow.layerIndex = i;
                        propRow.childIndex = -1;
                        propRow.effectIndex = e;
                        propRow.maskIndex = -1;
                        propRow.propertyIndex = propIdx;
                        propRow.y = y;
                        propRow.height = kPropertyRowHeight;
                        _visibleRows.append(propRow);
                        y += propRow.height;
                    }
                }

                // Effect-level masks
                for (int m = 0; m < masks.size(); ++m) {
                    if (masks[m].effectIndex != e) {
                        continue;
                    }
                    FluxVisibleRow maskRow;
                    maskRow.type = eFluxVisibleRowMask;
                    maskRow.layerIndex = i;
                    maskRow.childIndex = m;
                    maskRow.effectIndex = e;
                    maskRow.maskIndex = m;
                    maskRow.y = y;
                    maskRow.height = kMaskRowHeight;
                    _visibleRows.append(maskRow);
                    y += maskRow.height;

                    // Effect-mask property rows
                    {
                        QList<FluxKeyframeProperty> props = buildMaskProperties(masks[m]);
                        for (int p = 0; p < props.size(); ++p) {
                            int propIdx = _propertyRows.size();
                            _propertyRows.append(props[p]);
                            FluxVisibleRow propRow;
                            propRow.type = eFluxVisibleRowProperty;
                            propRow.layerIndex = i;
                            propRow.childIndex = -1;
                            propRow.effectIndex = e;
                            propRow.maskIndex = m;
                            propRow.propertyIndex = propIdx;
                            propRow.y = y;
                            propRow.height = kPropertyRowHeight;
                            _visibleRows.append(propRow);
                            y += propRow.height;
                        }
                    }
                }
            }
        }
    }
    _totalContentHeight = y;

    // After rebuilding rows, refresh native signal connections so external
    // keyframe edits (from DopeSheet, CurveEditor, property panel) invalidate rows.
    refreshKeyframeSignalConnections();
}

void
FluxTimeline::refreshKeyframeSignalConnections()
{
    // Collect all knobs from Flux-owned nodes (layer gizmos + effects) that
    // could potentially appear as keyframe property rows. We connect to their
    // native KnobSignalSlotHandler signals so external edits (DopeSheet,
    // CurveEditor, property panel) invalidate our cached property rows.
    //
    // We use a set of raw KnobSignalSlotHandler* pointers to avoid duplicate
    // connections across multiple rebuildVisibleRows() calls. Qt::UniqueConnection
    // would also work but the set lets us quickly skip known handlers.

    std::set<KnobSignalSlotHandler*> neededHandlers;

    for (int i = 0; i < _layers.size(); ++i) {
        const FluxLayer& layer = _layers[i];

        // Layer gizmo knobs
        if (layer.gizmoNode) {
            const std::vector<KnobIPtr>& knobs = layer.gizmoNode->getKnobs();
            for (const KnobIPtr& knob : knobs) {
                if (!knob || !knob->canAnimate()) {
                    continue;
                }
                if (knob->getName() == std::string("animatorTimeDependency")) {
                    continue;
                }
                KnobSignalSlotHandlerPtr handler = knob->getSignalSlotHandler();
                if (handler) {
                    neededHandlers.insert(handler.get());
                }
            }
        }

        // Effect knobs
        for (int e = 0; e < layer.effects.size(); ++e) {
            if (!layer.effects[e].node) {
                continue;
            }
            const std::vector<KnobIPtr>& knobs = layer.effects[e].node->getKnobs();
            for (const KnobIPtr& knob : knobs) {
                if (!knob || !knob->canAnimate()) {
                    continue;
                }
                KnobSignalSlotHandlerPtr handler = knob->getSignalSlotHandler();
                if (handler) {
                    neededHandlers.insert(handler.get());
                }
            }
        }

        // Mask node knobs (Roto/RotoPaint nodes may have animatable knobs)
        for (int m = 0; m < layer.masks.size(); ++m) {
            if (!layer.masks[m].maskNode) {
                continue;
            }
            const std::vector<KnobIPtr>& knobs = layer.masks[m].maskNode->getKnobs();
            for (const KnobIPtr& knob : knobs) {
                if (!knob || !knob->canAnimate()) {
                    continue;
                }
                KnobSignalSlotHandlerPtr handler = knob->getSignalSlotHandler();
                if (handler) {
                    neededHandlers.insert(handler.get());
                }
            }
        }
    }

    // Connect new knob handlers that we haven't seen before
    for (KnobSignalSlotHandler* handler : neededHandlers) {
        if (_connectedKnobHandlers.find(handler) != _connectedKnobHandlers.end()) {
            continue; // already connected
        }
        // Connect keyframe mutation signals. All route to the same handler:
        // mark rows dirty and schedule a repaint. The actual rebuild is deferred
        // to the next paintEvent via _rowsDirty.
        QObject::connect(handler, SIGNAL(keyFrameSet(double,ViewSpec,int,int,bool)),
                         this, SLOT(onNativeKeyframeChanged()),
                         Qt::UniqueConnection);
        QObject::connect(handler, SIGNAL(keyFrameRemoved(double,ViewSpec,int,int)),
                         this, SLOT(onNativeKeyframeChanged()),
                         Qt::UniqueConnection);
        QObject::connect(handler, SIGNAL(keyFrameMoved(ViewSpec,int,double,double)),
                         this, SLOT(onNativeKeyframeChanged()),
                         Qt::UniqueConnection);
        QObject::connect(handler, SIGNAL(multipleKeyFramesSet(std::list<double>,ViewSpec,int,int)),
                         this, SLOT(onNativeKeyframeChanged()),
                         Qt::UniqueConnection);
        QObject::connect(handler, SIGNAL(multipleKeyFramesRemoved(std::list<double>,ViewSpec,int,int)),
                         this, SLOT(onNativeKeyframeChanged()),
                         Qt::UniqueConnection);
        QObject::connect(handler, SIGNAL(animationRemoved(ViewSpec,int)),
                         this, SLOT(onNativeKeyframeChanged()),
                         Qt::UniqueConnection);
        QObject::connect(handler, SIGNAL(animationAboutToBeRemoved(ViewSpec,int)),
                         this, SLOT(onNativeKeyframeChanged()),
                         Qt::UniqueConnection);

        _connectedKnobHandlers.insert(handler);
    }

    // --- Bezier shape keyframe signals for roto aggregate rows ---
    // Aggregate mask rows are driven by RotoContext::getBeziersKeyframeTimes().
    // Bezier emits keyframeSet/Removed/animationRemoved when shape keys change
    // (e.g. via roto overlay in the viewer).
    std::set<Bezier*> neededBeziers;

    for (int i = 0; i < _layers.size(); ++i) {
        const FluxLayer& layer = _layers[i];
        for (int m = 0; m < layer.masks.size(); ++m) {
            if (!layer.masks[m].maskNode) {
                continue;
            }
            RotoContextPtr rotoCtx = layer.masks[m].maskNode->getRotoContext();
            if (!rotoCtx) {
                continue;
            }
            std::list<RotoDrawableItemPtr> items = rotoCtx->getCurvesByRenderOrder(true);
            for (const RotoDrawableItemPtr& item : items) {
                BezierPtr bez = std::dynamic_pointer_cast<Bezier>(item);
                if (bez) {
                    neededBeziers.insert(bez.get());
                }
            }
        }
    }

    // Connect new Bezier signals not yet tracked
    for (Bezier* bez : neededBeziers) {
        if (_connectedBeziers.find(bez) != _connectedBeziers.end()) {
            continue; // already connected
        }
        QObject::connect(bez, SIGNAL(keyframeSet(double)),
                         this, SLOT(onNativeKeyframeChanged()),
                         Qt::UniqueConnection);
        QObject::connect(bez, SIGNAL(keyframeRemoved(double)),
                         this, SLOT(onNativeKeyframeChanged()),
                         Qt::UniqueConnection);
        QObject::connect(bez, SIGNAL(animationRemoved()),
                         this, SLOT(onNativeKeyframeChanged()),
                         Qt::UniqueConnection);

        _connectedBeziers.insert(bez);
    }

    // --- RotoContext lifecycle signals ---
    // When shapes are inserted/removed or their activation state changes,
    // the set of active Bezier shapes changes. We need to invalidate rows
    // so the next rebuild discovers new Beziers and connects their key signals.
    std::set<RotoContext*> neededRotoContexts;

    for (int i = 0; i < _layers.size(); ++i) {
        const FluxLayer& layer = _layers[i];
        for (int m = 0; m < layer.masks.size(); ++m) {
            if (!layer.masks[m].maskNode) {
                continue;
            }
            RotoContextPtr rotoCtx = layer.masks[m].maskNode->getRotoContext();
            if (rotoCtx) {
                neededRotoContexts.insert(rotoCtx.get());
            }
        }
    }

    for (RotoContext* ctx : neededRotoContexts) {
        if (_connectedRotoContexts.find(ctx) != _connectedRotoContexts.end()) {
            continue; // already connected
        }
        // itemInserted(int,int) — a new shape was added
        QObject::connect(ctx, &RotoContext::itemInserted,
                         this, [this]() { onNativeKeyframeChanged(); },
                         Qt::UniqueConnection);
        // itemRemoved(const RotoItemPtr&,int) — a shape was removed
        QObject::connect(ctx, &RotoContext::itemRemoved,
                         this, [this]() { onNativeKeyframeChanged(); },
                         Qt::UniqueConnection);
        // itemGloballyActivatedChanged — shape visibility toggled, changes active set
        QObject::connect(ctx, &RotoContext::itemGloballyActivatedChanged,
                         this, [this]() { onNativeKeyframeChanged(); },
                         Qt::UniqueConnection);

        _connectedRotoContexts.insert(ctx);
    }

    // Note: we do NOT disconnect stale knob handlers, Bezier pointers, or
    // RotoContext pointers when nodes/shapes are removed. Stale connections
    // fire into onNativeKeyframeChanged() which just sets _rowsDirty and calls
    // update() — harmless and cheap. The sets are never pruned to keep the
    // logic simple; they grow to at most O(knobs+shapes+contexts).
}

void
FluxTimeline::onNativeKeyframeChanged()
{
    for (const FluxLayer& layer : _layers) {
        if (layer.type == QString::fromUtf8("text") && layer.gizmoNode) {
            FluxTextAnimatorModel::syncAnimatorStackToRenderer(layer.gizmoNode);
        }
    }
    // A native knob keyframe was added/removed/moved outside FluxTimeline
    // (e.g. via DopeSheet, CurveEditor, or property panel). Mark rows dirty
    // so the next paint rebuilds property rows from current animation state.
    _rowsDirty = true;
    update();
}

bool
FluxTimeline::isKeyframeSelected(int propertyIndex, double keyTime) const
{
    for (const SelectedKeyframe& sk : _selectedKeys) {
        if (sk.propertyIndex == propertyIndex && std::abs(sk.keyTime - keyTime) < 0.5) {
            return true;
        }
    }
    return false;
}

void
FluxTimeline::toggleKeyframeSelection(int propertyIndex, double keyTime)
{
    for (int i = 0; i < _selectedKeys.size(); ++i) {
        if (_selectedKeys[i].propertyIndex == propertyIndex &&
            std::abs(_selectedKeys[i].keyTime - keyTime) < 0.5) {
            _selectedKeys.removeAt(i);
            return;
        }
    }
    _selectedKeys.append(SelectedKeyframe(propertyIndex, keyTime));
}

void
FluxTimeline::clearKeyframeSelection()
{
    _selectedKeys.clear();
    _selectedKeyPropertyIndex = -1;
    _selectedKeyTime = 0.0;
}

void
FluxTimeline::copySelectedKeyframes()
{
    _keyframeClipboard.clear();

    if (_selectedKeys.isEmpty()) {
        return;
    }

    // Determine earliest source time across all selected keys
    double earliest = 1e30;

    // Phase 1: collect ClipboardKeyEntry for each selected non-roto key
    for (const SelectedKeyframe& sk : _selectedKeys) {
        if (sk.propertyIndex < 0 || sk.propertyIndex >= _propertyRows.size()) {
            continue;
        }
        const FluxKeyframeProperty& prop = _propertyRows[sk.propertyIndex];
        if (prop.isRotoAggregate || !prop.knob) {
            continue;
        }

        double roundedTime = std::round(sk.keyTime);

        for (int d : prop.dims) {
            std::shared_ptr<Curve> curve = prop.knob->getCurve(ViewSpec::current(), d);
            if (!curve) {
                continue;
            }
            KeyFrame kf;
            if (!curve->getKeyFrameWithTime(roundedTime, &kf)) {
                continue;
            }

            ClipboardKeyEntry entry;
            entry.ownerNode = prop.ownerNode;
            entry.knob = prop.knob;
            entry.dim = d;
            entry.keyFrame = kf; // preserves value, interpolation, tangents
            entry.sourceTime = roundedTime;
            _keyframeClipboard.entries.append(entry);

            if (roundedTime < earliest) {
                earliest = roundedTime;
            }
        }
    }

    if (_keyframeClipboard.isEmpty()) {
        return;
    }

    _keyframeClipboard.earliestSourceTime = earliest;
}

void
FluxTimeline::pasteKeyframes()
{
    if (_keyframeClipboard.isEmpty()) {
        return;
    }

    // basePasteFrame is the current playhead position
    double basePasteFrame = static_cast<double>(_currentFrame);
    double earliest = _keyframeClipboard.earliestSourceTime;

    // Batch all knob changes to avoid per-key re-evaluations
    // Group by knob for begin/end changes
    std::set<KnobI*> touchedKnobs;
    for (const ClipboardKeyEntry& e : _keyframeClipboard.entries) {
        if (e.knob) {
            touchedKnobs.insert(e.knob.get());
        }
    }
    for (KnobI* knob : touchedKnobs) {
        knob->beginChanges();
    }

    bool anyPasted = false;

    for (const ClipboardKeyEntry& e : _keyframeClipboard.entries) {
        if (!e.knob) {
            continue;
        }

        double relativeOffset = e.sourceTime - earliest;
        double targetTime = basePasteFrame + relativeOffset;

        // Create a copy of the stored keyframe with time adjusted to target
        KeyFrame pastedKey = e.keyFrame;
        pastedKey.setTime(targetTime);

        // Use the exact KeyFrame overload to preserve value/interpolation/tangents
        e.knob->onKeyFrameSet(targetTime, ViewIdx(0), pastedKey, e.dim);
        anyPasted = true;
    }

    for (KnobI* knob : touchedKnobs) {
        knob->endChanges();
    }

    if (!anyPasted) {
        return;
    }

    // Sync text animators for affected knobs
    std::set<Node*> syncedNodes;
    for (const ClipboardKeyEntry& e : _keyframeClipboard.entries) {
        if (e.ownerNode && e.knob && !syncedNodes.count(e.ownerNode.get())) {
            if (FluxTextAnimatorModel::isAnimatorKnobName(e.knob->getName())) {
                FluxTextAnimatorModel::syncAnimatorStackToRenderer(e.ownerNode);
            }
            syncedNodes.insert(e.ownerNode.get());
        }
    }

    // Save clipboard info for selection remap after rebuild
    struct PastedKeyIdentity {
        NodePtr ownerNode;
        KnobIPtr knob;
        double targetTime;
    };
    QList<PastedKeyIdentity> pastedIdentities;
    for (const ClipboardKeyEntry& e : _keyframeClipboard.entries) {
        double relativeOffset = e.sourceTime - earliest;
        double targetTime = basePasteFrame + relativeOffset;
        PastedKeyIdentity id;
        id.ownerNode = e.ownerNode;
        id.knob = e.knob;
        id.targetTime = targetTime;
        pastedIdentities.append(id);
    }

    rebuildVisibleRows();

    // Select the pasted keys by mapping ownerNode+knob+targetTime to new property indices
    _selectedKeys.clear();
    for (const PastedKeyIdentity& pid : pastedIdentities) {
        for (int pi = 0; pi < _propertyRows.size(); ++pi) {
            const FluxKeyframeProperty& prop = _propertyRows[pi];
            if (prop.ownerNode == pid.ownerNode && prop.knob == pid.knob) {
                // Check if this property actually has a key at the target time
                // (avoid duplicates in selection from multiple dims)
                bool alreadySelected = false;
                for (const SelectedKeyframe& sk : _selectedKeys) {
                    if (sk.propertyIndex == pi && std::abs(sk.keyTime - pid.targetTime) < 0.5) {
                        alreadySelected = true;
                        break;
                    }
                }
                if (!alreadySelected) {
                    _selectedKeys.append(SelectedKeyframe(pi, pid.targetTime));
                }
                break;
            }
        }
    }

    // Update backward-compat single selection
    if (!_selectedKeys.isEmpty()) {
        _selectedKeyPropertyIndex = _selectedKeys[0].propertyIndex;
        _selectedKeyTime = _selectedKeys[0].keyTime;
    }

    Q_EMIT compositingChanged();
    Q_EMIT frameChanged(_currentFrame);
    update();
}

void
FluxTimeline::selectKeyframesInRect(const QRect& screenRect)
{
    // Normalize the rect (handle drag in any direction)
    QRect normalized = screenRect.normalized();

    int leftFrame = xToFrame(normalized.left());
    int rightFrame = xToFrame(normalized.right());

    for (int pi = 0; pi < _propertyRows.size(); ++pi) {
        // Find the visible row for this property
        int rowY = -1;
        int rowH = kPropertyRowHeight;
        for (const FluxVisibleRow& vr : _visibleRows) {
            if (vr.type == eFluxVisibleRowProperty && vr.propertyIndex == pi) {
                rowY = vr.y;
                rowH = vr.height;
                break;
            }
        }
        if (rowY < 0) {
            continue;
        }

        // Check vertical overlap (convert rowY to screen coordinates)
        int screenRowY = kTimeRulerHeight + rowY - _scrollOffsetY;
        if (screenRowY + rowH < normalized.top() || screenRowY > normalized.bottom()) {
            continue;
        }

        // Find keyframes within horizontal range
        const FluxKeyframeProperty& prop = _propertyRows[pi];
        QList<FluxKeyframeKey> keys = keysForProperty(prop);
        for (const FluxKeyframeKey& key : keys) {
            int kf = static_cast<int>(key.time + 0.5);
            if (kf >= leftFrame && kf <= rightFrame) {
                if (!isKeyframeSelected(pi, key.time)) {
                    _selectedKeys.append(SelectedKeyframe(pi, key.time));
                }
            }
        }
    }
}

bool
FluxTimeline::layerHasExpandableChildren(int layerIndex) const
{
    if (layerIndex < 0 || layerIndex >= _layers.size()) {
        return false;
    }
    const FluxLayer& layer = _layers[layerIndex];
    if (layer.type == QString::fromUtf8("text") &&
        !FluxTextAnimatorModel::animators(layer.gizmoNode).isEmpty()) {
        return true;
    }
    if (!layer.effects.isEmpty() || !layer.masks.isEmpty()) {
        return true;
    }
    return !buildLayerProperties(layer, _ungroupedKeyframeProperties).isEmpty();
}

const FluxVisibleRow*
FluxTimeline::yToRow(int y) const
{
    int contentY = y - kTimeRulerHeight + _scrollOffsetY;
    for (const FluxVisibleRow& row : _visibleRows) {
        if (contentY >= row.y && contentY < row.y + row.height) {
            return &row;
        }
    }
    return nullptr;
}

int
FluxTimeline::rowYForLayer(int layerIndex) const
{
    for (const FluxVisibleRow& row : _visibleRows) {
        if (row.type == eFluxVisibleRowLayer && row.layerIndex == layerIndex) {
            return kTimeRulerHeight + row.y - _scrollOffsetY;
        }
    }
    return kTimeRulerHeight + layerIndex * kLayerRowHeight - _scrollOffsetY;
}

int
FluxTimeline::insertionLayerIndexForY(int y) const
{
    int contentY = y - kTimeRulerHeight + _scrollOffsetY;
    for (int r = 0; r < _visibleRows.size(); ++r) {
        const FluxVisibleRow& row = _visibleRows[r];
        if (contentY >= row.y && contentY < row.y + row.height) {
            if (row.type == eFluxVisibleRowLayer) {
                return row.layerIndex;
            }
        }
        if (contentY < row.y) {
            if (row.type == eFluxVisibleRowLayer) {
                return row.layerIndex;
            }
        }
    }
    return _layers.size();
}

int
FluxTimeline::insertionYForLayerIndex(int index) const
{
    for (const FluxVisibleRow& row : _visibleRows) {
        if (row.type == eFluxVisibleRowLayer && row.layerIndex == index) {
            return kTimeRulerHeight + row.y - _scrollOffsetY;
        }
    }
    return kTimeRulerHeight + _totalContentHeight - _scrollOffsetY;
}

int
FluxTimeline::yToLayer(int y) const
{
    const FluxVisibleRow* row = yToRow(y);
    if (row && row->type == eFluxVisibleRowLayer &&
        row->layerIndex >= 0 && row->layerIndex < _layers.size()) {
        return row->layerIndex;
    }
    return -1;
}

void
FluxTimeline::updateZoom()
{
    int frameRange = _lastFrame - _firstFrame;
    if (frameRange <= 0) {
        return;
    }
    int availableWidth = width() - _layerLabelWidth;
    if (availableWidth > 0) {
        _zoom = (double)availableWidth / frameRange;
    }
}

void
FluxTimeline::clampScrollOffsets()
{
    // Clamp vertical scroll
    int visibleHeight = height() - kTimeRulerHeight;
    int totalLayerHeight = _totalContentHeight;
    int maxScrollY = qMax(0, totalLayerHeight - visibleHeight);
    _scrollOffsetY = qBound(0, _scrollOffsetY, maxScrollY);

    // Clamp horizontal scroll
    int visibleWidth = width() - _layerLabelWidth;
    int totalFrameWidth = (int)((_lastFrame - _firstFrame) * _zoom);
    int maxScrollX = qMax(0, totalFrameWidth - visibleWidth);
    _scrollOffsetX = qBound(0, _scrollOffsetX, maxScrollX);
}

void
FluxTimeline::fitToView()
{
    // Reset scroll offsets and zoom to fit the full project frame range
    _scrollOffsetX = 0;
    _scrollOffsetY = 0;
    updateZoom();
    update();
}

bool
FluxTimeline::addEffectByPluginId(const QString& pluginId, int major)
{
    // Same rejection logic as showNodeCreationDialog lambda
    if (pluginId.isEmpty()) {
        return false;
    }
    if (pluginId.contains(QString::fromUtf8("Read"), Qt::CaseInsensitive) ||
        pluginId.contains(QString::fromUtf8("Write"), Qt::CaseInsensitive) ||
        pluginId.contains(QString::fromUtf8("Merge"), Qt::CaseInsensitive) ||
        pluginId.contains(QString::fromUtf8("Viewer"), Qt::CaseInsensitive)) {
        return false;
    }

    int targetLayer = _selectedLayer;
    if (targetLayer < 0 || targetLayer >= _layers.size()) {
        return false;
    }
    if (!canAddEffectToRow(targetLayer)) {
        return false;
    }

    Gui* gui = getGui();
    if (!gui || !gui->getApp()) {
        return false;
    }
    NodeCollectionPtr collection = std::dynamic_pointer_cast<NodeCollection>(gui->getApp()->getProject());
    if (!collection) {
        return false;
    }

    CreateNodeArgs args(pluginId.toStdString(), collection);
    args.setProperty<bool>(kCreateNodeArgsPropAutoConnect, false);
    args.setProperty<bool>(kCreateNodeArgsPropAddUndoRedoCommand, false);
    args.setProperty<bool>(kCreateNodeArgsPropSettingsOpened, false);
    args.setProperty<bool>(kCreateNodeArgsPropSilent, true);
    if (major >= 0) {
        args.setProperty<int>(kCreateNodeArgsPropPluginVersion, major, 0);
    }

    NodePtr node = gui->getApp()->createNode(args);
    if (!node) {
        fprintf(stderr, "FLUX ERROR: Failed to create effect node '%s' from toolbar\n",
                pluginId.toStdString().c_str());
        return false;
    }
    if (node->getNInputs() < 1) {
        fprintf(stderr, "FLUX WARNING: plugin '%s' has no stack input; rejecting toolbar effect\n",
                pluginId.toStdString().c_str());
        node->deactivate(std::list<NodePtr>(), false, true);
        return false;
    }

    // Do not force-open the settings panel from the toolbar action path.
    // This path runs while Qt's tool-button menu activation stack is active;
    // opening the panel immediately can assert in ViewerTab::setPluginViewerInterface()
    // when it tries to insert the node viewer context. The node is still added to
    // the Flux layer/effect model; selecting it can open/show its controls through
    // the normal timeline/properties path.

    FluxEffect effect;
    effect.pluginId = pluginId;
    effect.label = QString::fromStdString(node->getLabel());
    effect.node = node;
    effect.enabled = true;

    _layers[targetLayer].effects.append(effect);
    _layers[targetLayer].expanded = true;
    refreshVisibleRows();
    _selectedType = eFluxSelectionLayer;
    _selectedLayer = targetLayer;
    _selectedEffectIndex = -1;
    _selectedMaskIndex = -1;
    _selectedPropertyIndex = -1;
    Q_EMIT layerSelected(targetLayer);
    Q_EMIT effectsChanged(targetLayer);
    Q_EMIT compositingChanged();
    update();

    fprintf(stderr, "FLUX: Added effect '%s' to layer %d '%s' from toolbar\n",
            effect.label.toStdString().c_str(), targetLayer,
            _layers[targetLayer].name.toStdString().c_str());
    return true;
}

void
FluxTimeline::showNodeCreationDialog()
{
    NodeCreationDialog* dialog = new NodeCreationDialog(QString(), this);
    QObject::connect(dialog, &NodeCreationDialog::dialogFinished, this,
                     [this, dialog](bool accepted) {
                         int major = -1;
                         QString pluginId = dialog->getNodeName(&major);
                         dialog->close();
                         dialog->deleteLater();

                         if (!accepted || pluginId.isEmpty()) {
                             return;
                         }
                         if (pluginId.contains(QString::fromUtf8("Read"), Qt::CaseInsensitive) ||
                             pluginId.contains(QString::fromUtf8("Write"), Qt::CaseInsensitive) ||
                             pluginId.contains(QString::fromUtf8("Merge"), Qt::CaseInsensitive) ||
                             pluginId.contains(QString::fromUtf8("Viewer"), Qt::CaseInsensitive)) {
                             fprintf(stderr, "FLUX WARNING: plugin '%s' is not a safe timeline-stack effect\n",
                                     pluginId.toStdString().c_str());
                             return;
                         }

                          int targetLayer = _selectedLayer;
                          if (targetLayer >= 0 && targetLayer < _layers.size()) {
                              if (!canAddEffectToRow(targetLayer)) {
                                  return;
                              }
                          }

                         Gui* gui = getGui();
                         if (!gui || !gui->getApp()) {
                             return;
                         }
                         NodeCollectionPtr collection = std::dynamic_pointer_cast<NodeCollection>(gui->getApp()->getProject());
                         if (!collection) {
                             return;
                         }

                         CreateNodeArgs args(pluginId.toStdString(), collection);
                         args.setProperty<bool>(kCreateNodeArgsPropAutoConnect, false);
                         args.setProperty<bool>(kCreateNodeArgsPropAddUndoRedoCommand, false);
                         args.setProperty<bool>(kCreateNodeArgsPropSettingsOpened, false);
                         args.setProperty<bool>(kCreateNodeArgsPropSilent, true);
                         if (major >= 0) {
                             args.setProperty<int>(kCreateNodeArgsPropPluginVersion, major, 0);
                         }

                         NodePtr node = gui->getApp()->createNode(args);
                         if (!node) {
                             fprintf(stderr, "FLUX ERROR: Failed to create effect node '%s' from timeline Tab\n",
                                     pluginId.toStdString().c_str());
                             return;
                         }
                         if (node->getNInputs() < 1) {
                             fprintf(stderr, "FLUX WARNING: plugin '%s' has no stack input; rejecting timeline effect\n",
                                     pluginId.toStdString().c_str());
                             node->deactivate(std::list<NodePtr>(), false, true);
                             return;
                         }

                         NodeGuiPtr nodeGui = std::dynamic_pointer_cast<NodeGui>(node->getNodeGui());
                         if (nodeGui) {
                             nodeGui->setVisibleSettingsPanel(true);
                             NodeSettingsPanel* settingsPanel = nodeGui->getSettingPanel();
                             if (settingsPanel) {
                                 DockablePanel* dockPanel = static_cast<DockablePanel*>(settingsPanel);
                                 gui->putSettingsPanelFirst(dockPanel);
                             }
                         }

                         FluxEffect effect;
                         effect.pluginId = pluginId;
                         effect.label = QString::fromStdString(node->getLabel());
                         effect.node = node;
                         effect.enabled = true;

                          if (targetLayer >= 0 && targetLayer < _layers.size()) {
                              _layers[targetLayer].effects.append(effect);
                              _layers[targetLayer].expanded = true;
                              refreshVisibleRows();
                              _selectedType = eFluxSelectionLayer;
                              _selectedLayer = targetLayer;
                              _selectedEffectIndex = -1;
                              _selectedMaskIndex = -1;
                              _selectedPropertyIndex = -1;
                              Q_EMIT layerSelected(targetLayer);
                              Q_EMIT effectsChanged(targetLayer);
                              fprintf(stderr, "FLUX: Added effect '%s' to layer %d '%s'\n",
                                      effect.label.toStdString().c_str(), targetLayer,
                                      _layers[targetLayer].name.toStdString().c_str());
                         } else {
                             FluxLayer adjustment;
                             adjustment.name = effect.label;
                             adjustment.type = QString::fromUtf8("adjustment");
                             adjustment.inPoint = _firstFrame;
                             adjustment.outPoint = _lastFrame;
                             adjustment.originalInPoint = _firstFrame;
                             adjustment.originalOutPoint = _lastFrame;
                             adjustment.originalFirstFrame = _firstFrame;
                             adjustment.originalLastFrame = _lastFrame;
                             adjustment.color = QColor(200, 130, 80);
                             adjustment.effects.append(effect);
                              adjustment.nodeInitialized = true;
                               _layers.insert(0, adjustment);
                               rebuildVisibleRows();
                                _selectedType = eFluxSelectionLayer;
                                _selectedLayer = 0;
                                _selectedEffectIndex = -1;
                                _selectedMaskIndex = -1;
                                _selectedPropertyIndex = -1;
                               Q_EMIT layerSelected(0);
                             fprintf(stderr, "FLUX: Created adjustment effect row '%s' after final merge\n",
                                     effect.label.toStdString().c_str());
                         }

                         Q_EMIT compositingChanged();
                         update();
                     });
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

bool
FluxTimeline::event(QEvent* event)
{
    if (event->type() == QEvent::KeyPress) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent && keyEvent->key() == Qt::Key_Tab && keyEvent->modifiers() == Qt::NoModifier) {
            // Tab is normally consumed by Qt focus traversal before keyPressEvent().
            // Catch it here so Timeline gets the same Natron node-search UX as NodeGraph.
            showNodeCreationDialog();
            keyEvent->accept();
            return true;
        }
    }
    return QWidget::event(event);
}

// ─── Hit Testing ────────────────────────────────────────────────────────────

FluxTimeline::HitZone
FluxTimeline::hitTest(int x,
                      int y,
                      int* outLayerIndex) const
{
    if (outLayerIndex) {
        *outLayerIndex = -1;
    }

    // Must be in the bar area (right of labels, below ruler)
    if (x <= _layerLabelWidth || y <= kTimeRulerHeight) {
        return eHitNone;
    }

    // Effect sub-rows are not hit-targets for bar interactions
    const FluxVisibleRow* row = yToRow(y);
    if (!row || row->type != eFluxVisibleRowLayer) {
        return eHitNone;
    }

    int layerIdx = row->layerIndex;
    if (layerIdx < 0 || layerIdx >= _layers.size()) {
        return eHitNone;
    }

    const FluxLayer& layer = _layers[layerIdx];
    int barX1 = frameToX(layer.inPoint + layer.timeOffset);
    int barX2 = frameToX(layer.outPoint + layer.timeOffset);

    // Check if within the bar's horizontal extent
    if (x < barX1 || x > barX2) {
        return eHitNone;
    }

    if (outLayerIndex) {
        *outLayerIndex = layerIdx;
    }

    // Null rows: body only, never trim handles
    if (isNullRow(layerIdx)) {
        return eHitBarBody;
    }

    // Check trim handles (only if bar is wide enough for both handles)
    if (barX2 - barX1 > kTrimHandleWidth * 2) {
        if (x - barX1 < kTrimHandleWidth) {
            return eHitTrimLeft;
        }
        if (barX2 - x < kTrimHandleWidth) {
            return eHitTrimRight;
        }
    }

    return eHitBarBody;
}

// ─── Mouse Interaction ──────────────────────────────────────────────────────

void
FluxTimeline::mousePressEvent(QMouseEvent* event)
{
    // ── Dirty-banner click detection (before all other handlers) ──
    if (event->button() == Qt::LeftButton) {
        const int mx = event->pos().x();
        const int my = event->pos().y();

        // Check dirty banner (same geometry as painted in paintEvent: y=2, h=22, full width)
        Gui* gui = getGui();
        if (gui && gui->isFluxNodeGraphDirty()) {
            if (my >= 2 && my < 24 && mx >= 0) {
                Q_EMIT nodegraphSyncRequested();
                return;
            }

            // Check Sync button in ruler area (syncBtnX=layerLabelWidth+4, syncBtnY=2, W=48, H=rulerHeight-4)
            const int syncBtnW = 48;
            const int syncBtnH = kTimeRulerHeight - 4;
            const int syncBtnX = _layerLabelWidth + 4;
            const int syncBtnY = 2;
            if (mx >= syncBtnX && mx < syncBtnX + syncBtnW &&
                my >= syncBtnY && my < syncBtnY + syncBtnH) {
                Q_EMIT nodegraphSyncRequested();
                return;
            }
        }
    }

    // ── Middle button or Alt+Left: start pan ──
    if (event->button() == Qt::MiddleButton ||
        (event->button() == Qt::LeftButton && (event->modifiers() & Qt::AltModifier))) {
        _interactionMode = eModePan;
        _interactionStartX = event->pos().x();
        _interactionStartY = event->pos().y();
        _panStartScrollX = _scrollOffsetX;
        _panStartScrollY = _scrollOffsetY;
        setCursor(Qt::ClosedHandCursor);
        clearSnapState();
        _snapShiftHeld = false;
        return;
    }

    if (event->button() != Qt::LeftButton) {
        return;
    }

    int x = event->pos().x();
    int y = event->pos().y();

    // Keyframe/Curve mode toggle button (name-area header, in ruler row)
    {
        int toggleX = kControlColumnWidth + 4;
        int toggleW = qMin(80, _layerLabelWidth - kControlColumnWidth - 8);
        if (toggleW > 20 && x >= toggleX && x < toggleX + toggleW && y >= 2 && y < kTimeRulerHeight - 2) {
            _showKeyframeCurves = !_showKeyframeCurves;
            update();
            return;
        }
    }

    // Fit button (top-left corner, control column header in ruler row)
    if (x >= 2 && x < kControlColumnWidth - 2 && y >= 2 && y < kTimeRulerHeight - 2) {
        fitToView();
        return;
    }

    // Resize handle (right edge of label panel)
    if (x >= _layerLabelWidth - 4 && x < _layerLabelWidth && y > kTimeRulerHeight) {
        _interactionMode = eModeResizePanel;
        _resizeStartX = x;
        _resizeStartWidth = _layerLabelWidth;
        setCursor(Qt::SplitHCursor);
        clearSnapState();
        _snapShiftHeld = false;
        return;
    }

    // Click on time ruler area → drag playhead
    if (y < kTimeRulerHeight && x > _layerLabelWidth) {
        _interactionMode = eModeDragPlayhead;
        _currentFrame = xToFrame(x);
        // T019-C: Sync with shared timeline
        if (_timeline) {
            _timeline->seekFrame(SequenceTime(_currentFrame), false, nullptr, eTimelineChangeReasonUserSeek);
        }
        Q_EMIT frameChanged(_currentFrame);
        clearSnapState();
        _snapShiftHeld = false;
        update();
        return;
    }

    // Click on layer label area → check L/V/S buttons, disclosure, then select layer
    if (x < _layerLabelWidth && y > kTimeRulerHeight) {
        const FluxVisibleRow* clickRow = yToRow(y);
        if (!clickRow) {
            // Clicked below all rows
            _selectedType = eFluxSelectionNone;
            _selectedLayer = -1;
            _selectedEffectIndex = -1;
            _selectedMaskIndex = -1;
            _selectedPropertyIndex = -1;
            Q_EMIT layerSelected(-1);
            update();
            return;
        }

        if (clickRow->type == eFluxVisibleRowEffect) {
            int li = clickRow->layerIndex;
            int ei = clickRow->childIndex;
            QRect enableRect(20, kTimeRulerHeight + clickRow->y - _scrollOffsetY + 4, 16, qMax(10, clickRow->height - 8));
            if (enableRect.contains(x, y) && li >= 0 && li < _layers.size() && ei >= 0 && ei < _layers[li].effects.size()) {
                _layers[li].effects[ei].enabled = !_layers[li].effects[ei].enabled;
                Q_EMIT effectsChanged(li);
                Q_EMIT compositingChanged();
                update();
                return;
            }

            // Select effect sub-row in label area and prepare for potential reorder drag
            _selectedType = eFluxSelectionEffect;
            _selectedLayer = li;
            _selectedEffectIndex = ei;
            _selectedMaskIndex = -1;
            _selectedPropertyIndex = -1;
            Q_EMIT effectSelected(_selectedLayer, _selectedEffectIndex);

            // Start potential effect reorder (only if layer not locked and layer has >1 effect)
            if (!_layers[li].locked && _layers[li].effects.size() > 1) {
                _interactionMode = eModeReorderEffect;
                _interactionLayerIndex = li;
                _interactionStartX = x;
                _interactionStartY = y;
                _reorderEffectLayerIndex = li;
                _reorderEffectFromIndex = ei;
                _reorderEffectTargetIndex = ei;
            }
            update();
            return;
        }

        if (clickRow->type == eFluxVisibleRowTextAnimator) {
            _selectedType = eFluxSelectionTextAnimator;
            _selectedLayer = clickRow->layerIndex;
            _selectedEffectIndex = clickRow->childIndex;
            _selectedMaskIndex = -1;
            _selectedPropertyIndex = -1;
            Q_EMIT textAnimatorSelected(_selectedLayer, _selectedEffectIndex);
            update();
            return;
        }

        if (clickRow->type == eFluxVisibleRowMask) {
            int li = clickRow->layerIndex;
            int mi = clickRow->maskIndex;
            QRect enableRect(20, kTimeRulerHeight + clickRow->y - _scrollOffsetY + 4, 16, qMax(10, clickRow->height - 8));
            if (enableRect.contains(x, y) && li >= 0 && li < _layers.size() && mi >= 0 && mi < _layers[li].masks.size()) {
                _layers[li].masks[mi].enabled = !_layers[li].masks[mi].enabled;
                Q_EMIT masksChanged(li);
                Q_EMIT compositingChanged();
                update();
                return;
            }

            // Select mask sub-row in label area
            _selectedType = eFluxSelectionMask;
            _selectedLayer = li;
            _selectedEffectIndex = clickRow->effectIndex;
            _selectedMaskIndex = mi;
            _selectedPropertyIndex = -1;
            Q_EMIT maskSelected(_selectedLayer, _selectedMaskIndex);
            update();
            return;
        }

        if (clickRow->type == eFluxVisibleRowProperty) {
            // Select property row in label area
            _selectedType = eFluxSelectionProperty;
            _selectedLayer = clickRow->layerIndex;
            _selectedEffectIndex = -1;
            _selectedMaskIndex = -1;
            _selectedPropertyIndex = clickRow->propertyIndex;
            _selectedKeyPropertyIndex = -1;
            _selectedKeyTime = 0.0;
            _selectedKeys.clear();
            update();
            return;
        }

        int layerIdx = clickRow->layerIndex;
        if (layerIdx >= 0 && layerIdx < _layers.size()) {
            // Check disclosure arrow
            const bool hasChildren = layerHasExpandableChildren(layerIdx);
            if (hasChildren) {
                int arrowX = kControlColumnWidth + 4;
                int arrowY = rowYForLayer(layerIdx) + (kLayerRowHeight - kDisclosureSize) / 2;
                QRect disclosureRect(arrowX, arrowY, kDisclosureSize, kDisclosureSize);
                if (disclosureRect.contains(x, y)) {
                    _layers[layerIdx].expanded = !_layers[layerIdx].expanded;
                    rebuildVisibleRows();
                    clampScrollOffsets();
                    update();
                    return;
                }
            }

            int btnY = rowYForLayer(layerIdx);

            // L button at x=4
            QRect lockRect(4, btnY + 6, 16, kLayerRowHeight - 12);
            // V button at x=20
            QRect visRect(20, btnY + 6, 16, kLayerRowHeight - 12);
            // S button at x=36
            QRect soloRect(36, btnY + 6, 16, kLayerRowHeight - 12);

            if (soloRect.contains(x, y)) {
                // Solo is disabled for adjustment rows
                if (isAdjustmentRow(layerIdx)) {
                    _layers[layerIdx].solo = false;
                    update();
                    return;
                }
                // Toggle solo
                _layers[layerIdx].solo = !_layers[layerIdx].solo;
                Q_EMIT compositingChanged();
                update();
                return;
            } else if (visRect.contains(x, y)) {
                // Toggle mute
                _layers[layerIdx].muted = !_layers[layerIdx].muted;
                if (isAdjustmentRow(layerIdx)) {
                    updateAdjustmentTrimKeyframes(layerIdx);
                }
                Q_EMIT compositingChanged();
                update();
                return;
            } else if (lockRect.contains(x, y)) {
                // Toggle lock
                _layers[layerIdx].locked = !_layers[layerIdx].locked;
                update();
                return;
            }

            // No button hit — select the layer
            _selectedType = eFluxSelectionLayer;
            _selectedLayer = layerIdx;
            _selectedEffectIndex = -1;
            _selectedMaskIndex = -1;
            _selectedPropertyIndex = -1;
            Q_EMIT layerSelected(layerIdx);
            update();
        }
        return;
    }

    // Click on layer bar area → hit-test to determine what was clicked
    if (x > _layerLabelWidth && y > kTimeRulerHeight) {
        // Check if an effect or mask sub-row was clicked (no bar interactions)
        const FluxVisibleRow* clickRow = yToRow(y);
        if (clickRow && clickRow->type == eFluxVisibleRowEffect) {
            int li = clickRow->layerIndex;
            int ei = clickRow->childIndex;
            _selectedType = eFluxSelectionEffect;
            _selectedLayer = li;
            _selectedEffectIndex = ei;
            _selectedMaskIndex = -1;
            _selectedPropertyIndex = -1;
            Q_EMIT effectSelected(_selectedLayer, _selectedEffectIndex);

            // Start potential effect reorder (only if layer not locked and layer has >1 effect)
            if (li >= 0 && li < _layers.size() && !_layers[li].locked && _layers[li].effects.size() > 1) {
                _interactionMode = eModeReorderEffect;
                _interactionLayerIndex = li;
                _interactionStartX = x;
                _interactionStartY = y;
                _reorderEffectLayerIndex = li;
                _reorderEffectFromIndex = ei;
                _reorderEffectTargetIndex = ei;
            }
            update();
            return;
        }
        if (clickRow && clickRow->type == eFluxVisibleRowTextAnimator) {
            _selectedType = eFluxSelectionTextAnimator;
            _selectedLayer = clickRow->layerIndex;
            _selectedEffectIndex = clickRow->childIndex;
            _selectedMaskIndex = -1;
            _selectedPropertyIndex = -1;
            Q_EMIT textAnimatorSelected(_selectedLayer, _selectedEffectIndex);
            update();
            return;
        }
        if (clickRow && clickRow->type == eFluxVisibleRowMask) {
            _selectedType = eFluxSelectionMask;
            _selectedLayer = clickRow->layerIndex;
            _selectedEffectIndex = clickRow->effectIndex;
            _selectedMaskIndex = clickRow->maskIndex;
            _selectedPropertyIndex = -1;
            Q_EMIT maskSelected(_selectedLayer, _selectedMaskIndex);
            update();
            return;
        }

        if (clickRow && clickRow->type == eFluxVisibleRowProperty) {
            // Property row click in timeline area
            int propIdx = clickRow->propertyIndex;
            if (propIdx < 0 || propIdx >= _propertyRows.size()) {
                return;
            }
            const FluxKeyframeProperty& prop = _propertyRows[propIdx];
            bool ctrlHeld = (QApplication::keyboardModifiers() & Qt::ControlModifier) != 0;

            // Select this property row
            _selectedType = eFluxSelectionProperty;
            _selectedLayer = clickRow->layerIndex;
            _selectedEffectIndex = -1;
            _selectedMaskIndex = -1;
            _selectedPropertyIndex = propIdx;

            // Key hit-test: check if mouse is on a keyframe diamond
            double toleranceFrames = qMax(1.0, 8.0 / _zoom);
            double nearestTime = 0.0;
            if (nearestKeyTimeForProperty(prop, xToFrame(x), toleranceFrames, &nearestTime)) {
                if (ctrlHeld) {
                    // Ctrl+click: toggle this keyframe in/out of multi-select
                    toggleKeyframeSelection(propIdx, nearestTime);
                } else if (isKeyframeSelected(propIdx, nearestTime)) {
                    // Already selected — drag all selected keys
                    _interactionMode = eModeDragKeyframe;
                    _interactionStartX = x;
                    _interactionStartY = y;
                    _dragPropertyIndex = propIdx;
                    _dragOrigKeyTime = nearestTime;
                    _dragCurrentKeyTime = nearestTime;
                    clearSnapState();
                    _snapShiftHeld = (QApplication::keyboardModifiers() & Qt::ShiftModifier) != 0;
                    setCursor(Qt::SplitHCursor);
                } else {
                    // Regular click: select only this keyframe
                    _selectedKeys.clear();
                    _selectedKeys.append(SelectedKeyframe(propIdx, nearestTime));
                    _selectedKeyPropertyIndex = propIdx;
                    _selectedKeyTime = nearestTime;
                    // Start dragging the keyframe
                    _interactionMode = eModeDragKeyframe;
                    _interactionStartX = x;
                    _interactionStartY = y;
                    _dragPropertyIndex = propIdx;
                    _dragOrigKeyTime = nearestTime;
                    _dragCurrentKeyTime = nearestTime;
                    clearSnapState();
                    _snapShiftHeld = (QApplication::keyboardModifiers() & Qt::ShiftModifier) != 0;
                    setCursor(Qt::SplitHCursor);
                }
            } else {
                // Click on empty space in property row
                if (!ctrlHeld) {
                    // Clear keyframe selection and start rubber-band select
                    clearKeyframeSelection();
                    _interactionMode = eModeRubberBandSelect;
                    _interactionStartX = x;
                    _interactionStartY = y;
                    _rubberBandStart = QPoint(x, y);
                    _rubberBandCurrent = QPoint(x, y);
                }
            }
            update();
            return;
        }

        int layerIdx = -1;
        HitZone zone = hitTest(x, y, &layerIdx);

        if (zone == eHitNone) {
            // Clicked empty space in bar area — start rubber-band select
            _selectedType = eFluxSelectionNone;
            _selectedLayer = -1;
            _selectedEffectIndex = -1;
            _selectedMaskIndex = -1;
            _selectedPropertyIndex = -1;
            clearKeyframeSelection();
            Q_EMIT layerSelected(-1);
            _interactionMode = eModeRubberBandSelect;
            _interactionStartX = x;
            _interactionStartY = y;
            _rubberBandStart = QPoint(x, y);
            _rubberBandCurrent = QPoint(x, y);
            update();
            return;
        }

        // Select the layer
        _selectedType = eFluxSelectionLayer;
        _selectedLayer = layerIdx;
        _selectedEffectIndex = -1;
        _selectedMaskIndex = -1;
        _selectedPropertyIndex = -1;
        Q_EMIT layerSelected(layerIdx);

        // Store interaction start state
        _interactionLayerIndex = layerIdx;
        _interactionStartX = x;
        _interactionStartY = y;
        _interactionOrigInPoint = _layers[layerIdx].inPoint;
        _interactionOrigOutPoint = _layers[layerIdx].outPoint;
        _interactionOrigTimeOffset = _layers[layerIdx].timeOffset;
        _interactionOrigTrimStart = _layers[layerIdx].trimStart;
        _interactionOrigTrimEnd = _layers[layerIdx].trimEnd;
        _reorderTargetRow = layerIdx;

        if (_layers[layerIdx].locked) {
            // Locked layers: selection works, but no drag interactions
            _interactionMode = eModeNone;
        } else if (zone == eHitTrimLeft) {
            if (canTrimRow(layerIdx)) {
                _interactionMode = eModeTrimLeft;
                clearSnapState();
                _snapShiftHeld = (QApplication::keyboardModifiers() & Qt::ShiftModifier) != 0;
            } else {
                _interactionMode = eModeNone;
            }
        } else if (zone == eHitTrimRight) {
            if (canTrimRow(layerIdx)) {
                _interactionMode = eModeTrimRight;
                clearSnapState();
                _snapShiftHeld = (QApplication::keyboardModifiers() & Qt::ShiftModifier) != 0;
            } else {
                _interactionMode = eModeNone;
            }
        } else if (isAdjustmentRow(layerIdx) || isNullRow(layerIdx)) {
            // Adjustment/null: vertical reorder only, no horizontal move
            _interactionMode = eModeReorderLayer;
            _reorderTargetRow = layerIdx;
        } else {
            // Bar body: start as move, may transition to reorder on vertical drag
            _interactionMode = eModeMoveBar;
        }

        update();
    }
}

void
FluxTimeline::mouseMoveEvent(QMouseEvent* event)
{
    int x = event->pos().x();
    int y = event->pos().y();

    switch (_interactionMode) {

    case eModeDragPlayhead: {
        _currentFrame = qBound(_firstFrame, xToFrame(x), _lastFrame);
        // T019-C: Sync with shared timeline
        if (_timeline) {
            _timeline->seekFrame(SequenceTime(_currentFrame), false, nullptr, eTimelineChangeReasonUserSeek);
        }
        Q_EMIT frameChanged(_currentFrame);
        update();
        break;
    }

    case eModeMoveBar: {
        int deltaX = x - _interactionStartX;
        int deltaY = y - _interactionStartY;

        // If dragged vertically more than half a row height, switch to reorder mode
        if (qAbs(deltaY) > kLayerRowHeight / 2) {
            _interactionMode = eModeReorderLayer;
            _reorderTargetRow = _interactionLayerIndex;
            update();
            break;
        }

        // Move: only timeOffset changes. inPoint/outPoint stay the same.
        // Only allowed for rows that support horizontal move.
        if (canHorizontallyMoveRow(_interactionLayerIndex)) {
            int frameDelta = (int)( deltaX / _zoom );
            _layers[_interactionLayerIndex].timeOffset = _interactionOrigTimeOffset + frameDelta;
        }
        update();
        break;
    }

    case eModeTrimLeft: {
        if (_interactionLayerIndex < 0 || _interactionLayerIndex >= _layers.size()) {
            break;
        }
        if (!canTrimRow(_interactionLayerIndex)) {
            break;
        }
        int deltaX = x - _interactionStartX;
        int frameDelta = (int)( deltaX / _zoom );
        int candidateEdge = _interactionOrigInPoint + _interactionOrigTimeOffset + frameDelta;
        int newIn;

        // ── Shift-toggle snap ──
        bool shiftNow = (QApplication::keyboardModifiers() & Qt::ShiftModifier) != 0;
        if (shiftNow && !_snapShiftHeld) {
            _snapShiftHeld = true;
            auto targets = collectSnapTargets(eModeTrimLeft, _interactionLayerIndex, -1, 0.0);
            int snapped = candidateEdge;
            SnapTargetKind kind = eSnapNone;
            if (resolveSnapFrame(candidateEdge, targets, &snapped, &kind)) {
                setSnapState(snapped, kind);
                newIn = snapped - _layers[_interactionLayerIndex].timeOffset;
            } else {
                clearSnapState();
                newIn = candidateEdge - _layers[_interactionLayerIndex].timeOffset;
            }
        } else if (!shiftNow && _snapShiftHeld) {
            _snapShiftHeld = false;
            clearSnapState();
            newIn = candidateEdge - _layers[_interactionLayerIndex].timeOffset;
        } else if (_snapShiftHeld) {
            auto targets = collectSnapTargets(eModeTrimLeft, _interactionLayerIndex, -1, 0.0);
            int snapped = candidateEdge;
            SnapTargetKind kind = eSnapNone;
            if (resolveSnapFrame(candidateEdge, targets, &snapped, &kind)) {
                setSnapState(snapped, kind);
                newIn = snapped - _layers[_interactionLayerIndex].timeOffset;
            } else {
                clearSnapState();
                newIn = candidateEdge - _layers[_interactionLayerIndex].timeOffset;
            }
        } else {
            newIn = _interactionOrigInPoint + frameDelta;
        }

        // Clamp: inPoint must stay before outPoint
        newIn = qMin(newIn, _layers[_interactionLayerIndex].outPoint - 1);
        _layers[_interactionLayerIndex].inPoint = newIn;
        update();
        break;
    }

    case eModeTrimRight: {
        if (_interactionLayerIndex < 0 || _interactionLayerIndex >= _layers.size()) {
            break;
        }
        if (!canTrimRow(_interactionLayerIndex)) {
            break;
        }
        int deltaX = x - _interactionStartX;
        int frameDelta = (int)( deltaX / _zoom );
        int candidateEdge = _interactionOrigOutPoint + _interactionOrigTimeOffset + frameDelta;
        int newOut;

        // ── Shift-toggle snap ──
        bool shiftNow = (QApplication::keyboardModifiers() & Qt::ShiftModifier) != 0;
        if (shiftNow && !_snapShiftHeld) {
            _snapShiftHeld = true;
            auto targets = collectSnapTargets(eModeTrimRight, _interactionLayerIndex, -1, 0.0);
            int snapped = candidateEdge;
            SnapTargetKind kind = eSnapNone;
            if (resolveSnapFrame(candidateEdge, targets, &snapped, &kind)) {
                setSnapState(snapped, kind);
                newOut = snapped - _layers[_interactionLayerIndex].timeOffset;
            } else {
                clearSnapState();
                newOut = candidateEdge - _layers[_interactionLayerIndex].timeOffset;
            }
        } else if (!shiftNow && _snapShiftHeld) {
            _snapShiftHeld = false;
            clearSnapState();
            newOut = candidateEdge - _layers[_interactionLayerIndex].timeOffset;
        } else if (_snapShiftHeld) {
            auto targets = collectSnapTargets(eModeTrimRight, _interactionLayerIndex, -1, 0.0);
            int snapped = candidateEdge;
            SnapTargetKind kind = eSnapNone;
            if (resolveSnapFrame(candidateEdge, targets, &snapped, &kind)) {
                setSnapState(snapped, kind);
                newOut = snapped - _layers[_interactionLayerIndex].timeOffset;
            } else {
                clearSnapState();
                newOut = candidateEdge - _layers[_interactionLayerIndex].timeOffset;
            }
        } else {
            newOut = _interactionOrigOutPoint + frameDelta;
        }

        // Clamp: outPoint must stay after inPoint
        newOut = qMax(newOut, _layers[_interactionLayerIndex].inPoint + 1);
        _layers[_interactionLayerIndex].outPoint = newOut;
        // Update explicit trim state
        int trimDelta = _interactionOrigOutPoint - newOut;
        _layers[_interactionLayerIndex].trimEnd = qMax(0, _interactionOrigTrimEnd + trimDelta);
        update();
        break;
    }

    case eModeReorderLayer: {
        if (_interactionLayerIndex < 0 || _interactionLayerIndex >= _layers.size()) {
            break;
        }

        int currentRow = yToLayer(y);
        if (currentRow < 0 || currentRow >= _layers.size()) {
            break;
        }

        // Swap layers when the drag crosses the midpoint of an adjacent row
        if (currentRow != _reorderTargetRow) {
            _layers.move(_reorderTargetRow, currentRow);
            rebuildVisibleRows();

            // Update selected layer index
            if (_selectedLayer == _reorderTargetRow) {
                _selectedLayer = currentRow;
            }

            _interactionLayerIndex = currentRow;
            _reorderTargetRow = currentRow;

            // Reset start Y so we don't keep swapping on small movements
            _interactionStartY = y;

            Q_EMIT layersReordered();
            Q_EMIT compositingChanged();
        }
        update();
        break;
    }

    case eModeReorderEffect: {
        if (_reorderEffectLayerIndex < 0 || _reorderEffectLayerIndex >= _layers.size()) {
            break;
        }

        // Drag threshold: only activate visual feedback after moving a few pixels
        int deltaY = y - _interactionStartY;
        if (qAbs(deltaY) < 4 && _reorderEffectTargetIndex == _reorderEffectFromIndex) {
            break;
        }

        // Find which effect row the mouse is over (same layer only)
        int targetEffectIdx = -1;
        for (int ri = 0; ri < _visibleRows.size(); ++ri) {
            const FluxVisibleRow& vr = _visibleRows[ri];
            if (vr.type != eFluxVisibleRowEffect || vr.layerIndex != _reorderEffectLayerIndex) {
                continue;
            }
            int vrY = kTimeRulerHeight + vr.y - _scrollOffsetY;
            if (y >= vrY && y < vrY + vr.height) {
                targetEffectIdx = vr.childIndex;
                break;
            }
        }

        // If not directly over an effect row, find nearest based on Y position
        if (targetEffectIdx < 0) {
            int bestDist = 0x7fffffff;
            int bestIdx = _reorderEffectFromIndex;
            for (int ri = 0; ri < _visibleRows.size(); ++ri) {
                const FluxVisibleRow& vr = _visibleRows[ri];
                if (vr.type != eFluxVisibleRowEffect || vr.layerIndex != _reorderEffectLayerIndex) {
                    continue;
                }
                int vrY = kTimeRulerHeight + vr.y - _scrollOffsetY;
                int midY = vrY + vr.height / 2;
                int dist = qAbs(y - midY);
                if (dist < bestDist) {
                    bestDist = dist;
                    bestIdx = vr.childIndex;
                }
            }
            targetEffectIdx = bestIdx;
        }

        if (targetEffectIdx >= 0) {
            _reorderEffectTargetIndex = targetEffectIdx;
            setCursor(Qt::DragMoveCursor);
        }

        update();
        break;
    }

    case eModePan: {
        int deltaX = x - _interactionStartX;
        int deltaY = y - _interactionStartY;
        _scrollOffsetX = _panStartScrollX - deltaX;
        _scrollOffsetY = _panStartScrollY - deltaY;
        clampScrollOffsets();
        update();
        break;
    }

    case eModeResizePanel: {
        int deltaX = x - _resizeStartX;
        _layerLabelWidth = qBound(120, _resizeStartWidth + deltaX, 500);
        update();
        break;
    }

    case eModeRubberBandSelect: {
        _rubberBandCurrent = QPoint(x, y);
        update();
        break;
    }

    case eModeDragKeyframe: {
        if (_dragPropertyIndex < 0 || _dragPropertyIndex >= _propertyRows.size()) {
            break;
        }
        int targetFrame = xToFrame(x);
        double candidate = (double)targetFrame; // No project-edge clamp — preserve relative range

        // ── Shift-toggle snap ──
        bool shiftNow = (QApplication::keyboardModifiers() & Qt::ShiftModifier) != 0;
        if (shiftNow && !_snapShiftHeld) {
            _snapShiftHeld = true;
            auto targets = collectSnapTargets(eModeDragKeyframe, -1, _dragPropertyIndex, _dragOrigKeyTime);
            int snapped = static_cast<int>(candidate + 0.5);
            SnapTargetKind kind = eSnapNone;
            if (resolveSnapFrame(static_cast<int>(candidate + 0.5), targets, &snapped, &kind)) {
                setSnapState(snapped, kind);
                _dragCurrentKeyTime = snapped;
            } else {
                clearSnapState();
                _dragCurrentKeyTime = candidate;
            }
        } else if (!shiftNow && _snapShiftHeld) {
            _snapShiftHeld = false;
            clearSnapState();
            _dragCurrentKeyTime = candidate;
        } else if (_snapShiftHeld) {
            auto targets = collectSnapTargets(eModeDragKeyframe, -1, _dragPropertyIndex, _dragOrigKeyTime);
            int snapped = static_cast<int>(candidate + 0.5);
            SnapTargetKind kind = eSnapNone;
            if (resolveSnapFrame(static_cast<int>(candidate + 0.5), targets, &snapped, &kind)) {
                setSnapState(snapped, kind);
                _dragCurrentKeyTime = snapped;
            } else {
                clearSnapState();
                _dragCurrentKeyTime = candidate;
            }
        } else {
            _dragCurrentKeyTime = candidate;
        }

        setCursor(Qt::SplitHCursor);
        update();
        break;
    }

    case eModeNone:
    default: {
        // No active interaction — update cursor based on hover position
        // Resize handle cursor (right edge of label panel)
        if (x >= _layerLabelWidth - 4 && x < _layerLabelWidth && y > kTimeRulerHeight) {
            setCursor(Qt::SplitHCursor);
        } else {
            int layerIdx = -1;
            HitZone zone = hitTest(x, y, &layerIdx);
            if (zone == eHitTrimLeft || zone == eHitTrimRight) {
                if (layerIdx >= 0 && layerIdx < _layers.size() && _layers[layerIdx].locked) {
                    setCursor(Qt::ForbiddenCursor);
                } else {
                    setCursor(Qt::SplitHCursor);
                }
            } else if (zone == eHitBarBody) {
                if (layerIdx >= 0 && layerIdx < _layers.size() && _layers[layerIdx].locked) {
                    setCursor(Qt::ForbiddenCursor);
                } else {
                    setCursor(Qt::OpenHandCursor);
                }
            } else if (y < kTimeRulerHeight && x > _layerLabelWidth) {
                setCursor(Qt::PointingHandCursor);
            } else {
                unsetCursor();
            }
        }
        break;
    }

    }
}

void
FluxTimeline::mouseReleaseEvent(QMouseEvent* /*event*/)
{
    if (_interactionMode == eModeMoveBar) {
        // MOVE: only timeOffset changes
        if (isAdjustmentRow(_interactionLayerIndex)) {
            // Move shifts the bar — update disable-knob keyframes for new position
            updateAdjustmentTrimKeyframes(_interactionLayerIndex);
        } else {
            updateLayerMoveKnob(_interactionLayerIndex);
        }

        // Shift all keyframes owned by this layer (user properties, effects, masks)
        // by the same movement delta as the layer bar.
        if (_interactionLayerIndex >= 0 && _interactionLayerIndex < _layers.size()) {
            const FluxLayer& layer = _layers[_interactionLayerIndex];
            int frameDelta = layer.timeOffset - _interactionOrigTimeOffset;
            if (frameDelta != 0) {
                // System/timing knobs handled by updateLayerMoveKnob already — skip them.
                static const std::set<std::string> kSkipKnobNames = {
                    "timeOffset", "frameRange", "before", "after"
                };

                // Collect all animatable properties for this layer
                QList<FluxKeyframeProperty> allProps;

                // Layer-level user properties (excluding timing knobs)
                QList<FluxKeyframeProperty> layerProps = buildLayerProperties(layer, _ungroupedKeyframeProperties);
                for (const FluxKeyframeProperty& prop : layerProps) {
                    if (prop.knob && kSkipKnobNames.count(prop.knob->getName())) {
                        continue;
                    }
                    allProps.append(prop);
                }

                // All effect properties
                bool isAdj = isAdjustmentRow(_interactionLayerIndex);
                for (int ei = 0; ei < layer.effects.size(); ++ei) {
                    QList<FluxKeyframeProperty> effectProps =
                        buildEffectProperties(layer.effects[ei], isAdj, _ungroupedKeyframeProperties);
                    allProps.append(effectProps);
                }

                // All mask properties (layer masks and effect masks)
                for (int mi = 0; mi < layer.masks.size(); ++mi) {
                    QList<FluxKeyframeProperty> maskProps = buildMaskProperties(layer.masks[mi]);
                    allProps.append(maskProps);
                }

                // Build pending moves for every key across all properties
                struct LayerPendingMove {
                    const FluxKeyframeProperty* prop;
                    double srcTime;
                    double dstTime;
                };
                QList<LayerPendingMove> pendingMoves;

                for (const FluxKeyframeProperty& prop : allProps) {
                    QList<FluxKeyframeKey> keys = keysForProperty(prop);
                    for (const FluxKeyframeKey& k : keys) {
                        pendingMoves.append({&prop, k.time, k.time + frameDelta});
                    }
                }

                // Sort in safe order to avoid self-collisions
                if (frameDelta > 0) {
                    std::sort(pendingMoves.begin(), pendingMoves.end(),
                              [](const LayerPendingMove& a, const LayerPendingMove& b) { return a.srcTime > b.srcTime; });
                } else {
                    std::sort(pendingMoves.begin(), pendingMoves.end(),
                              [](const LayerPendingMove& a, const LayerPendingMove& b) { return a.srcTime < b.srcTime; });
                }

                // Apply moves
                bool anyMoved = false;
                for (const LayerPendingMove& pm : pendingMoves) {
                    if (moveKeysAtTime(*pm.prop, pm.srcTime, pm.dstTime)) {
                        anyMoved = true;
                    }
                }

                if (anyMoved) {
                    // Sync text animators if any affected property belongs to a text layer
                    for (const FluxKeyframeProperty& prop : allProps) {
                        syncTextAnimatorPropertyIfNeeded(prop);
                    }

                    // Build set of owner nodes for the moved layer so we can
                    // identify which selected keys belong to it.
                    std::set<Node*> movedOwnerNodes;
                    if (layer.gizmoNode) {
                        movedOwnerNodes.insert(layer.gizmoNode.get());
                    }
                    for (int ei = 0; ei < layer.effects.size(); ++ei) {
                        if (layer.effects[ei].node) {
                            movedOwnerNodes.insert(layer.effects[ei].node.get());
                        }
                    }
                    for (int mi = 0; mi < layer.masks.size(); ++mi) {
                        if (layer.masks[mi].maskNode) {
                            movedOwnerNodes.insert(layer.masks[mi].maskNode.get());
                        }
                    }

                    // Identify property indices belonging to the moved layer
                    // (using _propertyRows BEFORE rebuild clears them).
                    std::set<int> movedPropIndices;
                    for (int pi = 0; pi < _propertyRows.size(); ++pi) {
                        if (_propertyRows[pi].ownerNode &&
                            movedOwnerNodes.count(_propertyRows[pi].ownerNode.get())) {
                            movedPropIndices.insert(pi);
                        }
                    }

                    // Preserve selection across row rebuild
                    QList<SelectedKeyframe> savedSelection = _selectedKeys;
                    int savedKeyPropIdx = _selectedKeyPropertyIndex;
                    double savedKeyTime = _selectedKeyTime;

                    rebuildVisibleRows();

                    // Shift keyTimes for selected keys that belong to the moved layer
                    for (int i = 0; i < savedSelection.size(); ++i) {
                        if (movedPropIndices.count(savedSelection[i].propertyIndex)) {
                            savedSelection[i].keyTime += frameDelta;
                        }
                    }
                    if (savedKeyPropIdx >= 0 && movedPropIndices.count(savedKeyPropIdx)) {
                        savedKeyTime += frameDelta;
                    }

                    _selectedKeys = savedSelection;
                    _selectedKeyPropertyIndex = savedKeyPropIdx;
                    _selectedKeyTime = savedKeyTime;
                }
            }
        }
    } else if (_interactionMode == eModeTrimLeft || _interactionMode == eModeTrimRight) {
        // TRIM: only inPoint/outPoint changes
        if (isAdjustmentRow(_interactionLayerIndex)) {
            // Adjustment trim: update disable-knob keyframes
            updateAdjustmentTrimKeyframes(_interactionLayerIndex);
        } else {
            // Footage/solid trim: update FrameRange knob
            updateLayerTrimKnobs(_interactionLayerIndex);
        }
    }

    if (_interactionMode == eModeReorderLayer) {
        // Reorder requires Merge node reconnection — full rebuild
        Q_EMIT compositingChanged();
    }

    if (_interactionMode == eModeReorderEffect) {
        if (_reorderEffectLayerIndex >= 0 && _reorderEffectFromIndex >= 0 &&
            _reorderEffectTargetIndex >= 0 &&
            _reorderEffectFromIndex != _reorderEffectTargetIndex &&
            _reorderEffectLayerIndex < _layers.size()) {
            moveEffectInLayer(_reorderEffectLayerIndex, _reorderEffectFromIndex, _reorderEffectTargetIndex);
        }
        _reorderEffectLayerIndex = -1;
        _reorderEffectFromIndex = -1;
        _reorderEffectTargetIndex = -1;
    }

    if (_interactionMode == eModeDragKeyframe) {
        if (_dragPropertyIndex >= 0 && _dragPropertyIndex < _propertyRows.size()) {
            double oldTime = _dragOrigKeyTime;
            double newTime = _dragCurrentKeyTime;
            double roundedOld = std::round(oldTime);
            double roundedNew = std::round(newTime);
            if (std::abs(roundedNew - roundedOld) > 0.5) {
                double delta = roundedNew - roundedOld;

                // Build a sorted move list to avoid selected-key self-collisions:
                // Move later source times first when dragging right, earlier first when dragging left.
                struct PendingMove {
                    int listIndex;       ///< index into _selectedKeys
                    double srcTime;
                    double dstTime;
                };
                QList<PendingMove> moveList;
                for (int i = 0; i < _selectedKeys.size(); ++i) {
                    const SelectedKeyframe& sk = _selectedKeys[i];
                    if (sk.propertyIndex < 0 || sk.propertyIndex >= _propertyRows.size()) {
                        continue;
                    }
                    double srcTime = std::round(sk.keyTime);
                    double dstTime = srcTime + delta; // No project-edge clamp — preserve relative range
                    if (std::abs(dstTime - srcTime) > 0.5) {
                        moveList.append({i, srcTime, dstTime});
                    }
                }
                if (delta > 0.0) {
                    std::sort(moveList.begin(), moveList.end(),
                              [](const PendingMove& a, const PendingMove& b) { return a.srcTime > b.srcTime; });
                } else if (delta < 0.0) {
                    std::sort(moveList.begin(), moveList.end(),
                              [](const PendingMove& a, const PendingMove& b) { return a.srcTime < b.srcTime; });
                }

                // Apply moves in safe order
                bool anyMoved = false;
                QList<int> movedIndices;
                for (const PendingMove& pm : moveList) {
                    const SelectedKeyframe& sk = _selectedKeys[pm.listIndex];
                    const FluxKeyframeProperty& prop = _propertyRows[sk.propertyIndex];
                    if (moveKeysAtTime(prop, pm.srcTime, pm.dstTime)) {
                        anyMoved = true;
                        movedIndices.append(pm.listIndex);
                    }
                }

                if (anyMoved) {
                    // Update selection times for keys that actually moved
                    for (const PendingMove& pm : moveList) {
                        if (movedIndices.contains(pm.listIndex)) {
                            _selectedKeys[pm.listIndex].keyTime = pm.dstTime;
                        }
                    }

                    // Sync primary selection for backward compat
                    if (!_selectedKeys.isEmpty()) {
                        _selectedKeyPropertyIndex = _selectedKeys[0].propertyIndex;
                        _selectedKeyTime = _selectedKeys[0].keyTime;
                    }
                    // Sync text animators if any affected property belongs to a text layer
                    for (const SelectedKeyframe& sk : _selectedKeys) {
                        if (sk.propertyIndex >= 0 && sk.propertyIndex < _propertyRows.size()) {
                            syncTextAnimatorPropertyIfNeeded(_propertyRows[sk.propertyIndex]);
                        }
                    }

                    // Save selection before rebuildVisibleRows() clears it
                    QList<SelectedKeyframe> savedSelection = _selectedKeys;
                    int savedKeyPropIdx = _selectedKeyPropertyIndex;
                    double savedKeyTime = _selectedKeyTime;

                    rebuildVisibleRows();

                    // Restore selection so keyframes remain selected after drag
                    _selectedKeys = savedSelection;
                    _selectedKeyPropertyIndex = savedKeyPropIdx;
                    _selectedKeyTime = savedKeyTime;

                    Q_EMIT compositingChanged();
                    Q_EMIT frameChanged(_currentFrame);
                }
            }
        }
        _dragPropertyIndex = -1;
        _dragOrigKeyTime = 0.0;
        _dragCurrentKeyTime = 0.0;
    }

    if (_interactionMode == eModeRubberBandSelect) {
        // Finalize rubber-band selection
        QRect rubberRect = QRect(_rubberBandStart, _rubberBandCurrent).normalized();
        selectKeyframesInRect(rubberRect);
        // Update backward-compat single selection from first selected key
        if (!_selectedKeys.isEmpty()) {
            _selectedKeyPropertyIndex = _selectedKeys[0].propertyIndex;
            _selectedKeyTime = _selectedKeys[0].keyTime;
        }
    }

    if (_interactionMode == eModePan || _interactionMode == eModeResizePanel || _interactionMode == eModeRubberBandSelect) {
        unsetCursor();
    }

    clearSnapState();
    _snapShiftHeld = false;
    _interactionMode = eModeNone;
    _interactionLayerIndex = -1;
    _reorderTargetRow = -1;
    unsetCursor();

    update();
}

void
FluxTimeline::mouseDoubleClickEvent(QMouseEvent* event)
{
    int y = event->pos().y();
    if (y > kTimeRulerHeight) {
        const FluxVisibleRow* row = yToRow(y);
        if (row) {
            if (row->type == eFluxVisibleRowLayer) {
                Q_EMIT layerDoubleClicked(row->layerIndex);
            } else if (row->type == eFluxVisibleRowEffect) {
                // Double-click effect row: select and open properties
                _selectedType = eFluxSelectionEffect;
                _selectedLayer = row->layerIndex;
                _selectedEffectIndex = row->childIndex;
                _selectedMaskIndex = -1;
                _selectedPropertyIndex = -1;
                Q_EMIT effectSelected(_selectedLayer, _selectedEffectIndex);
                update();
            } else if (row->type == eFluxVisibleRowMask) {
                // Double-click mask row: select and open properties
                _selectedType = eFluxSelectionMask;
                _selectedLayer = row->layerIndex;
                _selectedEffectIndex = row->effectIndex;
                _selectedMaskIndex = row->maskIndex;
                _selectedPropertyIndex = -1;
                Q_EMIT maskSelected(_selectedLayer, _selectedMaskIndex);
                update();
            }
        }
    }
}

void
FluxTimeline::contextMenuEvent(QContextMenuEvent* event)
{
    QMenu menu(this);

    int x = event->pos().x();
    int y = event->pos().y();

    int layerIdx = -1;
    const FluxVisibleRow* contextRow = nullptr;
    if (y > kTimeRulerHeight) {
        contextRow = yToRow(y);
        if (contextRow) {
            if (contextRow->type == eFluxVisibleRowLayer) {
                layerIdx = contextRow->layerIndex;
            } else if (contextRow->type == eFluxVisibleRowEffect) {
                // Effect row context menu
                int ei = contextRow->childIndex;
                int li = contextRow->layerIndex;
                if (li < 0 || li >= _layers.size() || ei < 0 || ei >= _layers[li].effects.size()) {
                    return;
                }

                // Select the effect row
                _selectedType = eFluxSelectionEffect;
                _selectedLayer = li;
                _selectedEffectIndex = ei;
                _selectedMaskIndex = -1;
                _selectedPropertyIndex = -1;
                Q_EMIT effectSelected(li, ei);
                update();

                const FluxLayer& parentLayer = _layers[li];
                bool parentLocked = parentLayer.locked;

                QAction* openAction = menu.addAction(QString::fromUtf8("Open Effect Properties"));
                openAction->setEnabled(true);
                connect(openAction, &QAction::triggered, this, [this, li, ei]() {
                    Q_EMIT effectSelected(li, ei);
                });

                menu.addSeparator();

                QAction* moveUpAction = menu.addAction(QString::fromUtf8("Move Effect Up"));
                moveUpAction->setEnabled(!parentLocked && ei > 0);
                connect(moveUpAction, &QAction::triggered, this, [this, li, ei]() {
                    moveEffectInLayer(li, ei, ei - 1);
                });

                QAction* moveDownAction = menu.addAction(QString::fromUtf8("Move Effect Down"));
                moveDownAction->setEnabled(!parentLocked && ei < parentLayer.effects.size() - 1);
                connect(moveDownAction, &QAction::triggered, this, [this, li, ei]() {
                    moveEffectInLayer(li, ei, ei + 1);
                });

                menu.addSeparator();

                QAction* addMaskAction = menu.addAction(QString::fromUtf8("Add Effect Mask"));
                addMaskAction->setEnabled(!parentLocked && canAddEffectMask(li, ei));
                connect(addMaskAction, &QAction::triggered, this, [this, li, ei]() {
                    addEffectMask(li, ei);
                });

                menu.addSeparator();

                QAction* addToLayerAction = menu.addAction(QString::fromUtf8("Add Effect to Layer..."));
                addToLayerAction->setEnabled(canAddEffectToRow(li));
                connect(addToLayerAction, &QAction::triggered, this, [this, li]() {
                    _selectedType = eFluxSelectionLayer;
                    _selectedLayer = li;
                    _selectedEffectIndex = -1;
                    _selectedMaskIndex = -1;
                    _selectedPropertyIndex = -1;
                    Q_EMIT layerSelected(li);
                    update();
                    showNodeCreationDialog();
                });

                menu.addSeparator();

                QAction* removeAction = menu.addAction(QString::fromUtf8("Remove Effect"));
                removeAction->setEnabled(!parentLocked);
                connect(removeAction, &QAction::triggered, this, [this, li, ei]() {
                    removeEffectFromLayer(li, ei);
                });

                if (!menu.actions().isEmpty()) {
                    menu.exec(event->globalPos());
                }
                return;
            } else if (contextRow->type == eFluxVisibleRowMask) {
                // Mask row context menu
                int mi = contextRow->maskIndex;
                int li = contextRow->layerIndex;
                if (li < 0 || li >= _layers.size() || mi < 0 || mi >= _layers[li].masks.size()) {
                    return;
                }

                // Select the mask row
                _selectedType = eFluxSelectionMask;
                _selectedLayer = li;
                _selectedEffectIndex = contextRow->effectIndex;
                _selectedMaskIndex = mi;
                _selectedPropertyIndex = -1;
                Q_EMIT maskSelected(li, mi);
                update();

                const FluxLayer& parentLayer = _layers[li];
                const FluxMask& mask = parentLayer.masks[mi];
                bool parentLocked = parentLayer.locked;

                QAction* openAction = menu.addAction(QString::fromUtf8("Open Mask Properties"));
                openAction->setEnabled(mask.maskNode != nullptr);
                connect(openAction, &QAction::triggered, this, [this, li, mi]() {
                    Q_EMIT maskSelected(li, mi);
                });

                menu.addSeparator();

                QAction* removeAction = menu.addAction(QString::fromUtf8("Remove Mask"));
                removeAction->setEnabled(!parentLocked);
                connect(removeAction, &QAction::triggered, this, [this, li, mi]() {
                    removeMaskFromLayer(li, mi);
                });

                if (!menu.actions().isEmpty()) {
                    menu.exec(event->globalPos());
                }
                return;
            } else if (contextRow->type == eFluxVisibleRowProperty) {
                // Property row context menu
                int propIdx = contextRow->propertyIndex;
                if (propIdx < 0 || propIdx >= _propertyRows.size()) {
                    return;
                }
                const FluxKeyframeProperty& prop = _propertyRows[propIdx];

                // Select the property row
                _selectedType = eFluxSelectionProperty;
                _selectedLayer = contextRow->layerIndex;
                _selectedEffectIndex = -1;
                _selectedMaskIndex = -1;
                _selectedPropertyIndex = propIdx;
                _selectedKeyPropertyIndex = -1;
                _selectedKeyTime = 0.0;
                _selectedKeys.clear();
                update();;

                // Toggle mode action
                QString modeLabel = _showKeyframeCurves
                    ? QString::fromUtf8("Show Keyframes")
                    : QString::fromUtf8("Show Inline Curves");
                QAction* toggleModeAction = menu.addAction(modeLabel);
                connect(toggleModeAction, &QAction::triggered, this, [this]() {
                    _showKeyframeCurves = !_showKeyframeCurves;
                    update();
                });

                if (prop.groupDimCount > 1 && !prop.groupKey.empty()) {
                    QAction* groupAction = menu.addAction(prop.isGrouped
                        ? QString::fromUtf8("Ungroup Dimensions")
                        : QString::fromUtf8("Group Dimensions"));
                    connect(groupAction, &QAction::triggered, this, [this, propIdx]() {
                        if (propIdx < 0 || propIdx >= _propertyRows.size()) {
                            return;
                        }
                        const FluxKeyframeProperty& prop = _propertyRows[propIdx];
                        if (prop.groupKey.empty()) {
                            return;
                        }
                        if (prop.isGrouped) {
                            _ungroupedKeyframeProperties.insert(prop.groupKey);
                        } else {
                            _ungroupedKeyframeProperties.erase(prop.groupKey);
                        }
                        _rowsDirty = true;
                        rebuildVisibleRows();
                        update();
                    });
                }

                menu.addSeparator();

                QAction* addKeyAction = menu.addAction(QString::fromUtf8("Add Key at Playhead"));
                connect(addKeyAction, &QAction::triggered, this, [this, propIdx]() {
                    if (propIdx < 0 || propIdx >= _propertyRows.size()) {
                        return;
                    }
                    const FluxKeyframeProperty& prop = _propertyRows[propIdx];
                    if (addKeyAtTime(prop, _currentFrame)) {
                        syncTextAnimatorPropertyIfNeeded(prop);
                        rebuildVisibleRows();
                        Q_EMIT compositingChanged();
                        Q_EMIT frameChanged(_currentFrame);
                        update();
                    }
                });

                QAction* deleteAtPlayheadAction = menu.addAction(QString::fromUtf8("Delete Key at Playhead"));
                deleteAtPlayheadAction->setEnabled(propertyHasKeysAtTime(prop, _currentFrame));
                connect(deleteAtPlayheadAction, &QAction::triggered, this, [this, propIdx]() {
                    if (propIdx < 0 || propIdx >= _propertyRows.size()) {
                        return;
                    }
                    const FluxKeyframeProperty& prop = _propertyRows[propIdx];
                    if (deleteKeysAtTime(prop, _currentFrame)) {
                        syncTextAnimatorPropertyIfNeeded(prop);
                        rebuildVisibleRows();
                        Q_EMIT compositingChanged();
                        Q_EMIT frameChanged(_currentFrame);
                        update();
                    }
                });

                double toleranceFrames = qMax(1.0, 8.0 / _zoom);
                double nearestTime = 0.0;
                bool hasKeyUnderCursor = nearestKeyTimeForProperty(prop, xToFrame(x), toleranceFrames, &nearestTime);
                if (hasKeyUnderCursor) {
                    _selectedKeyPropertyIndex = propIdx;
                    _selectedKeyTime = nearestTime;
                }

                QAction* deleteSelectedAction = menu.addAction(QString::fromUtf8("Delete Selected Key"));
                deleteSelectedAction->setEnabled(_selectedKeyPropertyIndex == propIdx);
                connect(deleteSelectedAction, &QAction::triggered, this, [this, propIdx]() {
                    if (propIdx < 0 || propIdx >= _propertyRows.size() || _selectedKeyPropertyIndex != propIdx) {
                        return;
                    }
                    const FluxKeyframeProperty& prop = _propertyRows[propIdx];
                    if (deleteKeysAtTime(prop, _selectedKeyTime)) {
                        syncTextAnimatorPropertyIfNeeded(prop);
                        rebuildVisibleRows();
                        Q_EMIT compositingChanged();
                        Q_EMIT frameChanged(_currentFrame);
                        update();
                    }
                });

                QAction* deleteUnderCursorAction = menu.addAction(QString::fromUtf8("Delete Key Under Cursor"));
                deleteUnderCursorAction->setEnabled(hasKeyUnderCursor);
                if (hasKeyUnderCursor) {
                    connect(deleteUnderCursorAction, &QAction::triggered, this, [this, propIdx, nearestTime]() {
                        if (propIdx < 0 || propIdx >= _propertyRows.size()) {
                            return;
                        }
                        const FluxKeyframeProperty& prop = _propertyRows[propIdx];
                        if (deleteKeysAtTime(prop, nearestTime)) {
                            syncTextAnimatorPropertyIfNeeded(prop);
                            rebuildVisibleRows();
                            Q_EMIT compositingChanged();
                            Q_EMIT frameChanged(_currentFrame);
                            update();
                        }
                    });
                }

                if (!menu.actions().isEmpty()) {
                    menu.exec(event->globalPos());
                }
                return;
            }
        }
        if (layerIdx < 0) {
            hitTest(x, y, &layerIdx);
            if (layerIdx < 0 && x < _layerLabelWidth) {
                layerIdx = yToLayer(y);
            }
        }
    }

    // -- "Add Layer" submenu (shown when right-clicking empty space or in bar area) --
    if (y > kTimeRulerHeight) {
        QMenu* addMenu = menu.addMenu(QString::fromUtf8("Add Layer"));

        QAction* solidAction = addMenu->addAction(QString::fromUtf8("Solid"));
        connect(solidAction, &QAction::triggered, this, [this]() {
            addSolidLayer();
        });

        QAction* textAction = addMenu->addAction(QString::fromUtf8("Text"));
        bool motionTextProviderAvailable = false;
        bool textRenderProviderAvailable = false;
        if (appPTR) {
            const std::list<std::string> pluginIDs = appPTR->getPluginIDs();
            for (std::list<std::string>::const_iterator it = pluginIDs.begin(); it != pluginIDs.end(); ++it) {
                if (*it == std::string("net.sf.openfx.FluxMotionText")) {
                    motionTextProviderAvailable = true;
                } else if (*it == std::string("net.flux.openfx.TextRender")) {
                    textRenderProviderAvailable = true;
                }
            }
        }
        const bool textProviderAvailable = motionTextProviderAvailable && textRenderProviderAvailable;
        textAction->setEnabled(textProviderAvailable);
        if (!textProviderAvailable) {
            QStringList missingProviders;
            if (!motionTextProviderAvailable) {
                missingProviders << QString::fromUtf8("net.sf.openfx.FluxMotionText");
            }
            if (!textRenderProviderAvailable) {
                missingProviders << QString::fromUtf8("net.flux.openfx.TextRender");
            }
            textAction->setToolTip(QString::fromUtf8("Text layer unavailable. Missing provider(s): %1.").arg(missingProviders.join(QString::fromUtf8(", "))));
        }
        connect(textAction, &QAction::triggered, this, [this]() {
            addTextLayer();
        });

        QAction* nullAction = addMenu->addAction(QString::fromUtf8("Null"));
        connect(nullAction, &QAction::triggered, this, [this]() {
            addNullLayer();
        });

        if (layerIdx < 0) {
            QAction* addAdjustmentEffectAction = menu.addAction(QString::fromUtf8("Add Effect as Adjustment..."));
            connect(addAdjustmentEffectAction, &QAction::triggered, this, [this]() {
                _selectedLayer = -1;
                Q_EMIT layerSelected(-1);
                showNodeCreationDialog();
                update();
            });
        }

        // -- Per-layer actions (shown when right-clicking on a layer bar or label) --
        if (layerIdx >= 0 && layerIdx < _layers.size()) {
            menu.addSeparator();
            QAction* duplicateAction = menu.addAction(QString::fromUtf8("Duplicate Layer"));
            duplicateAction->setEnabled(canDuplicateRow(layerIdx));
            connect(duplicateAction, &QAction::triggered, this, [this, layerIdx]() {
                duplicateLayer(layerIdx);
            });
            QAction* splitAction = menu.addAction(QString::fromUtf8("Split Layer"));
            splitAction->setEnabled(canSplitRow(layerIdx));
            connect(splitAction, &QAction::triggered, this, [this, layerIdx]() {
                splitLayer(layerIdx, _currentFrame);
            });
            QAction* deleteAction = menu.addAction(QString::fromUtf8("Delete Layer"));
            deleteAction->setEnabled(!_layers[layerIdx].locked);
            connect(deleteAction, &QAction::triggered, this, [this, layerIdx]() {
                removeLayer(layerIdx);
            });

            menu.addSeparator();

            QAction* resetInOutAction = menu.addAction(QString::fromUtf8("Reset In/Out Points"));
            resetInOutAction->setEnabled(canTrimRow(layerIdx));
            connect(resetInOutAction, &QAction::triggered, this, [this, layerIdx]() {
                if (layerIdx < 0 || layerIdx >= _layers.size()) {
                    return;
                }
                FluxLayer& layer = _layers[layerIdx];
                if (layer.locked) {
                    return;
                }
                layer.inPoint = layer.originalInPoint;
                layer.outPoint = layer.originalOutPoint;
                layer.trimStart = 0;
                layer.trimEnd = 0;
                if (isAdjustmentRow(layerIdx)) {
                    // Adjustment rows: update disable-knob keyframes (respects mute/enabled)
                    updateAdjustmentTrimKeyframes(layerIdx);
                } else if (layer.gizmoNode) {
                    KnobIPtr frameRangeKnob = layer.gizmoNode->getKnobByName(std::string("frameRange"));
                    if (frameRangeKnob) {
                        KnobIntBasePtr int2D = std::dynamic_pointer_cast<KnobIntBase>(frameRangeKnob);
                        if (int2D) {
                            int2D->setValue(layer.inPoint, ViewSpec::all(), 0);
                            int2D->setValue(layer.outPoint, ViewSpec::all(), 1);
                        }
                    }
                }
                update();
            });

            // Open Read Node / source viewer (footage layers only)
            if (_layers[layerIdx].type == QString::fromUtf8("footage") && _layers[layerIdx].readerNode) {
                QAction* openReadAction = menu.addAction(QString::fromUtf8("Open Read Node"));
                connect(openReadAction, &QAction::triggered, this, [this, layerIdx]() {
                    if (layerIdx < 0 || layerIdx >= _layers.size()) {
                        return;
                    }
                    const FluxLayer& layer = _layers[layerIdx];
                    if (!layer.readerNode) {
                        return;
                    }
                    // Open the Read node's settings panel in the properties bin
                    NodeGuiIPtr nodeGui_i = layer.readerNode->getNodeGui();
                    if (nodeGui_i) {
                        NodeGuiPtr nodeGui = std::dynamic_pointer_cast<NodeGui>(nodeGui_i);
                        if (nodeGui) {
                            nodeGui->setVisibleSettingsPanel(true);
                        }
                    }
                });

                QAction* sourceViewerAction = menu.addAction(QString::fromUtf8("Open Source in AI Viewer"));
                connect(sourceViewerAction, &QAction::triggered, this, [this, layerIdx]() {
                    _selectedType = eFluxSelectionLayer;
                    _selectedLayer = layerIdx;
                    _selectedEffectIndex = -1;
                    _selectedMaskIndex = -1;
                    _selectedPropertyIndex = -1;
                    Q_EMIT layerSelected(layerIdx);
                    Q_EMIT sourceViewerRequested(layerIdx);
                    update();
                });
            }

            QAction* addEffectAction = menu.addAction(QString::fromUtf8("Add Effect..."));
            addEffectAction->setEnabled(canAddEffectToRow(layerIdx));
            connect(addEffectAction, &QAction::triggered, this, [this, layerIdx]() {
                // Select this layer and focus the effects panel
                _selectedType = eFluxSelectionLayer;
                _selectedLayer = layerIdx;
                _selectedEffectIndex = -1;
                _selectedMaskIndex = -1;
                _selectedPropertyIndex = -1;
                Q_EMIT layerSelected(layerIdx);
                update();
                showNodeCreationDialog();
            });

            if (_layers[layerIdx].type == QString::fromUtf8("text")) {
                QAction* addTextAnimatorAction = menu.addAction(QString::fromUtf8("Add Text Animator"));
                addTextAnimatorAction->setEnabled(!_layers[layerIdx].locked && _layers[layerIdx].gizmoNode);
                connect(addTextAnimatorAction, &QAction::triggered, this, [this, layerIdx]() {
                    if (layerIdx < 0 || layerIdx >= _layers.size() || !_layers[layerIdx].gizmoNode) {
                        return;
                    }
                    FluxTextAnimatorModel::addAnimator(_layers[layerIdx].gizmoNode, QString());
                    _layers[layerIdx].expanded = true;
                    rebuildVisibleRows();
                    Q_EMIT compositingChanged();
                    Q_EMIT frameChanged(_currentFrame);
                    update();
                });
            }

            QAction* addLayerMaskAction = menu.addAction(QString::fromUtf8("Add Layer Mask"));
            addLayerMaskAction->setEnabled(!_layers[layerIdx].locked);
            connect(addLayerMaskAction, &QAction::triggered, this, [this, layerIdx]() {
                addLayerMask(layerIdx);
            });
        }
    }

    if (!menu.actions().isEmpty()) {
        menu.exec(event->globalPos());
    }
}

void
FluxTimeline::wheelEvent(QWheelEvent* event)
{
    const QPoint angleDelta = event->angleDelta();
    const QPoint pixelDelta = event->pixelDelta();
    const QPoint delta = pixelDelta.isNull() ? angleDelta : pixelDelta;
    Qt::KeyboardModifiers mods = event->modifiers();

    const bool ctrlHeld = (mods & Qt::ControlModifier);

    // Horizontal component from trackpad or tilt wheel
    int hDelta = delta.x();

    if (!ctrlHeld && delta.y() != 0 && hDelta == 0) {
        // ── Plain Wheel: horizontal zoom anchored at mouse frame ──
        double oldZoom = _zoom;
        double factor = delta.y() > 0 ? 1.15 : 1.0 / 1.15;
        _zoom *= factor;
        _zoom = qBound(1.0, _zoom, 100.0);

        // Adjust scroll so the frame under the mouse stays in place
        int mouseX = event->position().x();
        int d = mouseX - _layerLabelWidth;
        if (d > 0 && oldZoom > 0) {
            _scrollOffsetX = (int)( (d + _scrollOffsetX) * (_zoom / oldZoom) - d );
        }

        clampScrollOffsets();
        update();
        event->accept();
    } else if (ctrlHeld && delta.y() != 0) {
        // ── Ctrl+Wheel: horizontal scroll ──
        _scrollOffsetX -= delta.y();
        clampScrollOffsets();
        update();
        event->accept();
    } else if (hDelta != 0) {
        // ── Horizontal wheel/trackpad: scroll horizontally ──
        _scrollOffsetX -= hDelta;
        clampScrollOffsets();
        update();
        event->accept();
    }
}

void
FluxTimeline::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Delete) {
        if (!_selectedKeys.isEmpty()) {
            // Delete all selected keyframes
            bool anyDeleted = false;
            for (const SelectedKeyframe& sk : _selectedKeys) {
                if (sk.propertyIndex >= 0 && sk.propertyIndex < _propertyRows.size()) {
                    const FluxKeyframeProperty& prop = _propertyRows[sk.propertyIndex];
                    if (deleteKeysAtTime(prop, sk.keyTime)) {
                        anyDeleted = true;
                        syncTextAnimatorPropertyIfNeeded(prop);
                    }
                }
            }
            if (anyDeleted) {
                clearKeyframeSelection();
                rebuildVisibleRows();
                Q_EMIT compositingChanged();
                Q_EMIT frameChanged(_currentFrame);
                update();
            }
        } else if (_selectedType == eFluxSelectionProperty &&
            _selectedKeyPropertyIndex >= 0 &&
            _selectedKeyPropertyIndex < _propertyRows.size()) {
            const FluxKeyframeProperty& prop = _propertyRows[_selectedKeyPropertyIndex];
            if (deleteKeysAtTime(prop, _selectedKeyTime)) {
                syncTextAnimatorPropertyIfNeeded(prop);
                rebuildVisibleRows();
                Q_EMIT compositingChanged();
                Q_EMIT frameChanged(_currentFrame);
                update();
            }
        } else if (_selectedType == eFluxSelectionMask &&
            _selectedLayer >= 0 && _selectedLayer < _layers.size() &&
            _selectedMaskIndex >= 0) {
            removeMaskFromLayer(_selectedLayer, _selectedMaskIndex);
        } else if (_selectedType == eFluxSelectionLayer &&
                   _selectedLayer >= 0 && _selectedLayer < _layers.size()) {
            if (!_layers[_selectedLayer].locked) {
                removeLayer(_selectedLayer);
            }
        }
        event->accept();
    } else if (event->key() == Qt::Key_D && (event->modifiers() & Qt::ControlModifier) && (event->modifiers() & Qt::ShiftModifier)) {
        // Ctrl+Shift+D = Split at playhead
        if (_selectedType == eFluxSelectionLayer && canSplitRow(_selectedLayer)) {
            splitLayer(_selectedLayer, _currentFrame);
        }
        event->accept();
    } else if (event->key() == Qt::Key_D && (event->modifiers() & Qt::ControlModifier) && !(event->modifiers() & Qt::ShiftModifier)) {
        // Ctrl+D = Duplicate selected layer
        if (_selectedType == eFluxSelectionLayer && canDuplicateRow(_selectedLayer)) {
            duplicateLayer(_selectedLayer);
        }
        event->accept();
    } else if (event->key() == Qt::Key_C && (event->modifiers() & Qt::ControlModifier) && !(event->modifiers() & Qt::ShiftModifier)) {
        // Ctrl+C = Copy selected keyframes
        copySelectedKeyframes();
        event->accept();
    } else if (event->key() == Qt::Key_V && (event->modifiers() & Qt::ControlModifier) && !(event->modifiers() & Qt::ShiftModifier)) {
        // Ctrl+V = Paste keyframes at playhead
        pasteKeyframes();
        event->accept();
    } else if (event->key() == Qt::Key_F && event->modifiers() == Qt::NoModifier) {
        // F = Fit to view
        fitToView();
        event->accept();
    } else if (event->key() == Qt::Key_Tab && event->modifiers() == Qt::NoModifier) {
        // Tab = Natron node search, routed into the Flux timeline stack.
        showNodeCreationDialog();
        event->accept();
    } else if (event->key() == Qt::Key_Escape && event->modifiers() == Qt::NoModifier) {
        _selectedType = eFluxSelectionNone;
        _selectedLayer = -1;
        _selectedEffectIndex = -1;
        _selectedMaskIndex = -1;
        _selectedPropertyIndex = -1;
        Q_EMIT layerSelected(-1);
        update();
        event->accept();
    } else if (event->modifiers() == Qt::NoModifier) {
        // Handle number keys 1-9 for viewer input switching via native-key normalization
        // (supports AZERTY, QWERTZ, and other layouts)
        Qt::Key normalizedKey = (Qt::Key)Gui::handleNativeKeys(event->key(), event->nativeScanCode(), event->nativeVirtualKey());
        if (normalizedKey >= Qt::Key_1 && normalizedKey <= Qt::Key_9) {
            int viewerInputIndex = normalizedKey - Qt::Key_1; // 0-based: Key_1→0, Key_9→8
            Q_EMIT viewerInputSwitchRequested(viewerInputIndex);
            event->accept();
        } else {
            QWidget::keyPressEvent(event);
        }
    } else {
        QWidget::keyPressEvent(event);
    }
}

void
FluxTimeline::resizeEvent(QResizeEvent* event)
{
    Q_UNUSED(event);
    updateZoom();
    update();
}

// T019-B: Drag and drop handlers

void
FluxTimeline::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasFormat(QString::fromUtf8("application/x-flux-asset")) ||
        event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
        _isDragOver = true;
        _dragPreviewPos = event->position().toPoint();
        update();
    }
}

void
FluxTimeline::dragMoveEvent(QDragMoveEvent* event)
{
    if (_isDragOver) {
        _dragPreviewPos = event->position().toPoint();
        update();
        event->accept();
    }
}

void
FluxTimeline::dropEvent(QDropEvent* event)
{
    _isDragOver = false;

    QString filePath;

    // Try custom mime type first
    if (event->mimeData()->hasFormat(QString::fromUtf8("application/x-flux-asset"))) {
        filePath = QString::fromUtf8(event->mimeData()->data(QString::fromUtf8("application/x-flux-asset")));
    } else if (event->mimeData()->hasUrls()) {
        const QList<QUrl> urls = event->mimeData()->urls();
        for (const QUrl& url : urls) {
            if (url.isLocalFile()) {
                filePath = url.toLocalFile();
                break;
            }
        }
    }

    if (filePath.isEmpty()) {
        update();
        return;
    }

    QPoint dropPos = event->position().toPoint();
    int x = dropPos.x();
    int y = dropPos.y();
    // Determine inPoint from X position
    int inFrame = _firstFrame;
    if (x > _layerLabelWidth) {
        inFrame = xToFrame(x);
    }

    // Determine row from Y position
    int row = _layers.size(); // Default: append at end
    if (y > kTimeRulerHeight) {
        row = insertionLayerIndexForY(y);
        if (row < 0) {
            row = 0;
        }
        if (row > _layers.size()) {
            row = _layers.size();
        }
    }

    // Create the layer
    QFileInfo fi(filePath);
    QString layerName = fi.fileName();
    if (layerName.isEmpty()) {
        layerName = filePath;
    }

    FluxLayer layer;
    layer.name = layerName;
    layer.filePath = filePath;
    layer.type = QString::fromUtf8("footage");
    layer.inPoint = inFrame;
    layer.outPoint = inFrame + 50; // Default 50-frame duration
    layer.color = QColor(80, 130, 200);

    // Insert at the determined row
    if (row >= _layers.size()) {
        _layers.append(layer);
        row = _layers.size() - 1;
    } else {
        _layers.insert(row, layer);
    }

    rebuildVisibleRows();

    // Select the new layer
    _selectedType = eFluxSelectionLayer;
    _selectedLayer = row;
    _selectedEffectIndex = -1;
    _selectedMaskIndex = -1;
    _selectedPropertyIndex = -1;
    Q_EMIT layerSelected(row);

    // Emit signal so Gui can create the gizmo + rebuild compositing graph
    Q_EMIT layerAddedFromDrop(filePath, row, inFrame);
    // Note: layerAddedFromDrop triggers rebuildCompositingGraph in Gui05.cpp.
    // Do NOT also emit compositingChanged here — that would cause a double rebuild.

    update();
    event->acceptProposedAction();
}

void
FluxTimeline::setLayerReaderNode(int layerIndex,
                                 const NodePtr& node)
{
    if (layerIndex >= 0 && layerIndex < _layers.size()) {
        _layers[layerIndex].readerNode = node;

        // Query actual frame range from the reader node
        if (node) {
            EffectInstancePtr effect = node->getEffectInstance();
            if (effect) {
                double first = 0, last = 0;
                effect->getFrameRange_public(0, &first, &last);
                if (last > first) {
                    _layers[layerIndex].outPoint = _layers[layerIndex].inPoint + (int)(last - first);
                    update();
                }
            }
        }
    }
}

void
FluxTimeline::updateLayerMoveKnob(int layerIndex)
{
    if (layerIndex < 0 || layerIndex >= _layers.size()) {
        return;
    }
    FluxLayer& layer = _layers[layerIndex];
    if (!layer.gizmoNode) {
        return;
    }

    // MOVE: only timeOffset changes. NEVER touch frameRange/inPoint/outPoint.
    // timeOffset is already updated in mouseMoveEvent. Just write it to the knob.

    fprintf(stderr, "FLUX MOVE: layer=%d timeOffset=%d\n",
            layerIndex, layer.timeOffset);

    KnobIPtr offsetKnob = layer.gizmoNode->getKnobByName(std::string("timeOffset"));
    if (offsetKnob) {
        KnobIntBasePtr intKnob = std::dynamic_pointer_cast<KnobIntBase>(offsetKnob);
        if (intKnob) {
            intKnob->setValue(layer.timeOffset, ViewSpec::all(), 0);
        }
    }

    for (int e = 0; e < layer.effects.size(); ++e) {
        FluxEffect& effect = layer.effects[e];
        if (!effect.aiMaskTimeOffsetNode) {
            continue;
        }
        fluxConfigureAIMaskTimeOffsetNode(effect.aiMaskTimeOffsetNode, layer.timeOffset - effect.aiMaskBaseTimeOffset);
    }
}

void
FluxTimeline::updateLayerTrimKnobs(int layerIndex)
{
    if (layerIndex < 0 || layerIndex >= _layers.size()) {
        return;
    }
    FluxLayer& layer = _layers[layerIndex];
    if (!layer.gizmoNode) {
        return;
    }

    // TRIM: only frameRange changes. timeOffset untouched.
    // inPoint = frameRange first. outPoint = frameRange last.
    // mouseMoveEvent already updated inPoint/outPoint. Just write them to the knob.

    fprintf(stderr, "FLUX TRIM: layer=%d inPoint=%d outPoint=%d (originalInPoint=%d originalOutPoint=%d)\n",
            layerIndex, layer.inPoint, layer.outPoint, layer.originalInPoint, layer.originalOutPoint);

    // Set FrameRange
    KnobIPtr frameRangeKnob = layer.gizmoNode->getKnobByName(std::string("frameRange"));
    if (frameRangeKnob) {
        KnobIntBasePtr int2D = std::dynamic_pointer_cast<KnobIntBase>(frameRangeKnob);
        if (int2D) {
            int2D->setValue(layer.inPoint, ViewSpec::all(), 0);
            int2D->setValue(layer.outPoint, ViewSpec::all(), 1);
        }
    }

    // Set before/after to black
    KnobIPtr beforeKnob = layer.gizmoNode->getKnobByName(std::string("before"));
    if (beforeKnob) {
        KnobIntBasePtr choice = std::dynamic_pointer_cast<KnobIntBase>(beforeKnob);
        if (choice) {
            choice->setValue(2, ViewSpec::all(), 0); // black
        }
    }
    KnobIPtr afterKnob = layer.gizmoNode->getKnobByName(std::string("after"));
    if (afterKnob) {
        KnobIntBasePtr choice = std::dynamic_pointer_cast<KnobIntBase>(afterKnob);
        if (choice) {
            choice->setValue(2, ViewSpec::all(), 0); // black
        }
    }

    for (int e = 0; e < layer.effects.size(); ++e) {
        FluxEffect& effect = layer.effects[e];
        if (!effect.aiMaskTimeOffsetNode) {
            continue;
        }
        fluxConfigureAIMaskTimeOffsetNode(effect.aiMaskTimeOffsetNode, layer.timeOffset - effect.aiMaskBaseTimeOffset);
    }
}

void
FluxTimeline::updateAdjustmentTrimKeyframes(int layerIndex)
{
    if (layerIndex < 0 || layerIndex >= _layers.size()) {
        return;
    }
    const FluxLayer& layer = _layers[layerIndex];
    if (layer.type != QString::fromUtf8("adjustment")) {
        return;
    }

    int startFrame = layer.inPoint + layer.timeOffset;
    int endFrame   = layer.outPoint + layer.timeOffset;

    for (int e = 0; e < layer.effects.size(); ++e) {
        NodePtr effectNode = layer.effects[e].node;
        if (!effectNode || !effectNode->isActivated()) {
            continue;
        }

        KnobIPtr disableKnobI = effectNode->getKnobByName(kDisableNodeKnobName);
        if (!disableKnobI) {
            continue;
        }
        KnobBoolBasePtr disableKnob = std::dynamic_pointer_cast<KnobBoolBase>(disableKnobI);
        if (!disableKnob) {
            continue;
        }

        // Enable animation (safe: KnobBool::canAnimate() == true)
        disableKnob->setAnimationEnabled(true);

        // Remove old keyframes
        disableKnob->removeAnimation(ViewSpec::all(), 0);

        // If the layer is muted or this effect is disabled, keep it statically off
        bool keepDisabled = !layer.effects[e].enabled || layer.muted;
        if (keepDisabled) {
            disableKnob->setValue(true, ViewSpec::all(), 0);
            continue;
        }

        // Set trim keyframes using 4-arg overload (no nullptr dereference)
        if (startFrame > 0) {
            disableKnob->setValueAtTime((double)(startFrame - 1), true,
                                         ViewSpec::all(), 0);
        }
        disableKnob->setValueAtTime((double)startFrame, false,
                                     ViewSpec::all(), 0);
        disableKnob->setValueAtTime((double)(endFrame + 1), true,
                                     ViewSpec::all(), 0);

        fprintf(stderr, "FLUX ADJ TRIM: effect '%s' keyframed: disabled@%d, enabled@%d, disabled@%d\n",
                effectNode->getLabel().c_str(), startFrame - 1, startFrame, endFrame + 1);
    }
}

void
FluxTimeline::applyLayerVisibility()
{
    // Determine if any non-adjustment layer is soloed
    bool anySoloed = false;
    int soloIdx = -1;
    for (int i = 0; i < _layers.size(); ++i) {
        if (_layers[i].solo && !isAdjustmentRow(i)) {
            anySoloed = true;
            soloIdx = i;
            break;
        }
    }

    // Apply mute/solo visibility to each row
    for (int i = 0; i < _layers.size(); ++i) {
        FluxLayer& layer = _layers[i];

        if (layer.type == QString::fromUtf8("adjustment")) {
            // Adjustment rows: mute/visibility only, solo ignored
            bool visible = !layer.muted;
            for (int e = 0; e < layer.effects.size(); ++e) {
                if (layer.effects[e].node && layer.effects[e].node->isActivated()) {
                    layer.effects[e].node->setNodeDisabled(!visible || !layer.effects[e].enabled);
                }
            }
        } else if (layer.type != QString::fromUtf8("null") && layer.mergeNode) {
            bool visible = !layer.muted && (!anySoloed || layer.solo);
            layer.mergeNode->setNodeDisabled(!visible);
        }
    }

    // Solo: reconnect viewer to show only the soloed layer's output
    NodePtr soloOutput;
    if (anySoloed && soloIdx >= 0 && soloIdx < _layers.size()) {
        FluxLayer& soloLayer = _layers[soloIdx];
        // Solo only applies to non-adjustment layers (adjustment rows excluded above)
        soloOutput = soloLayer.gizmoNode;
        for (int e = 0; e < soloLayer.effects.size(); ++e) {
            if (soloLayer.effects[e].node && soloLayer.effects[e].node->isActivated()) {
                soloOutput = soloLayer.effects[e].node;
            }
        }
    }

    if (anySoloed && soloOutput) {
        Gui* gui = getGui();
        if (gui) {
            const std::list<ViewerTab*>& viewerTabs = gui->getViewersList();
            if (!viewerTabs.empty()) {
                ViewerTab* viewerTab = viewerTabs.front();
                NodePtr viewerNode = viewerTab->getInternalNode()->getNode();
                if (viewerNode) {
                    viewerNode->disconnectInput(0);
                    viewerNode->connectInput(soloOutput, 0);
                }
            }
        }
    } else if (!anySoloed) {
        // No solo active — reconnect viewer to the final merge node in the chain.
        // The chain is built bottom-to-top, so the topmost layer's merge is the final output.
        NodePtr lastMerge;
        for (int i = 0; i < _layers.size(); ++i) {
            if (_layers[i].mergeNode) {
                lastMerge = _layers[i].mergeNode;
                break; // first hit = topmost layer = final merge in chain
            }
        }
        if (lastMerge) {
            Gui* gui = getGui();
            if (gui) {
                const std::list<ViewerTab*>& viewerTabs = gui->getViewersList();
                if (!viewerTabs.empty()) {
                    ViewerTab* viewerTab = viewerTabs.front();
                    NodePtr viewerNode = viewerTab->getInternalNode()->getNode();
                    if (viewerNode) {
                        viewerNode->disconnectInput(0);
                        viewerNode->connectInput(lastMerge, 0);
                    }
                }
            }
        }
    }
}

void
FluxTimeline::reconnectMergeChain()
{
    // Merge chain reconnection is now handled by rebuildCompositingGraph in Gui05.cpp
    // which creates Merge nodes outside the gizmos
}

FluxTimelineSerialization
FluxTimeline::serializeForProject() const
{
    FluxTimelineSerialization ser;
    ser.selectedLayer = _selectedLayer;
    ser.showKeyframeCurves = _showKeyframeCurves;
    ser.ungroupedKeyframeProperties.assign(_ungroupedKeyframeProperties.begin(),
                                           _ungroupedKeyframeProperties.end());

    for (int i = 0; i < _layers.size(); ++i) {
        const FluxLayer& layer = _layers[i];
        FluxLayerSerialization layerSer;

        // Identity
        layerSer.name = layer.name.toStdString();
        layerSer.filePath = layer.filePath.toStdString();
        layerSer.type = layer.type.toStdString();

        // State
        layerSer.muted = layer.muted;
        layerSer.locked = layer.locked;
        layerSer.solo = layer.solo;
        layerSer.expanded = layer.expanded;

        // Timing
        layerSer.inPoint = layer.inPoint;
        layerSer.outPoint = layer.outPoint;
        layerSer.originalInPoint = layer.originalInPoint;
        layerSer.originalOutPoint = layer.originalOutPoint;
        layerSer.originalFirstFrame = layer.originalFirstFrame;
        layerSer.originalLastFrame = layer.originalLastFrame;
        layerSer.timeOffset = layer.timeOffset;
        layerSer.trimStart = layer.trimStart;
        layerSer.trimEnd = layer.trimEnd;
        layerSer.nodeInitialized = layer.nodeInitialized;

        // Source FPS
        layerSer.sourceFrameRate = layer.sourceFrameRate;

        // Solid color
        layerSer.solidColorR = layer.solidColor.red();
        layerSer.solidColorG = layer.solidColor.green();
        layerSer.solidColorB = layer.solidColor.blue();

        // Parenting
        layerSer.parentLayerIndex = layer.parentLayerIndex;

        // Bar color
        layerSer.colorR = layer.color.red();
        layerSer.colorG = layer.color.green();
        layerSer.colorB = layer.color.blue();

        // Node script names
        if (layer.readerNode) {
            layerSer.readerNodeScriptName = layer.readerNode->getFullyQualifiedName();
        }
        if (layer.gizmoNode) {
            layerSer.gizmoNodeScriptName = layer.gizmoNode->getFullyQualifiedName();
        }
        if (layer.mergeNode) {
            layerSer.mergeNodeScriptName = layer.mergeNode->getFullyQualifiedName();
        }

        // Effects
        for (int e = 0; e < layer.effects.size(); ++e) {
            const FluxEffect& effect = layer.effects[e];
            FluxEffectSerialization effectSer;
            effectSer.pluginId = effect.pluginId.toStdString();
            effectSer.label = effect.label.toStdString();
            effectSer.enabled = effect.enabled;
            effectSer.isAIMaskCopy = effect.isAIMaskCopy;
            effectSer.aiMaskUsage = effect.aiMaskUsage.toStdString();
            effectSer.aiMaskTargetPlane = effect.aiMaskTargetPlane.toStdString();
            effectSer.aiMaskSourceChannel = effect.aiMaskSourceChannel.toStdString();
            effectSer.aiMaskOperation = effect.aiMaskOperation.toStdString();
            effectSer.aiMaskSourceRelativePath = effect.aiMaskSourceRelativePath.toStdString();
            effectSer.aiMaskManifestRelativePath = effect.aiMaskManifestRelativePath.toStdString();
            effectSer.aiMaskBaseTimeOffset = effect.aiMaskBaseTimeOffset;
            if (effect.aiMaskReadNode) {
                effectSer.aiMaskReadNodeScriptName = effect.aiMaskReadNode->getFullyQualifiedName();
            }
            if (effect.aiMaskTimeOffsetNode) {
                effectSer.aiMaskTimeOffsetNodeScriptName = effect.aiMaskTimeOffsetNode->getFullyQualifiedName();
            }
            if (effect.aiMaskShuffleNode) {
                effectSer.aiMaskShuffleNodeScriptName = effect.aiMaskShuffleNode->getFullyQualifiedName();
            }
            if (effect.aiMaskChannelMergeNode) {
                effectSer.aiMaskChannelMergeNodeScriptName = effect.aiMaskChannelMergeNode->getFullyQualifiedName();
            }
            if (effect.node) {
                effectSer.nodeScriptName = effect.node->getFullyQualifiedName();
            }
            layerSer.effects.push_back(effectSer);
        }

        // Masks
        layerSer.hasPrecompBranch = layer.hasPrecompBranch;
        if (layer.maskApplyNode) {
            layerSer.maskApplyNodeScriptName = layer.maskApplyNode->getFullyQualifiedName();
        }
        for (int m = 0; m < layer.masks.size(); ++m) {
            const FluxMask& mask = layer.masks[m];
            FluxMaskSerialization maskSer;
            maskSer.name = mask.name.toStdString();
            maskSer.type = mask.type.toStdString();
            maskSer.pluginId = mask.pluginId.toStdString();
            maskSer.enabled = mask.enabled;
            maskSer.inverted = mask.inverted;
            maskSer.effectIndex = mask.effectIndex;
            if (mask.maskNode) {
                maskSer.maskNodeScriptName = mask.maskNode->getFullyQualifiedName();
            }
            if (mask.reformatNode) {
                maskSer.reformatNodeScriptName = mask.reformatNode->getFullyQualifiedName();
            }
            layerSer.masks.push_back(maskSer);
        }

        // Text animator data
        if (layer.type == QString::fromUtf8("text") && layer.gizmoNode) {
            layerSer.animators = FluxTextAnimatorModel::captureAnimators(layer.gizmoNode);
        }

        ser.layers.push_back(layerSer);
    }

    return ser;
}

void
FluxTimeline::restoreFromProjectSerialization(const FluxTimelineSerialization& ser,
                                                Gui* gui)
{
    _layers.clear();
    _ungroupedKeyframeProperties.clear();
    _showKeyframeCurves = ser.showKeyframeCurves;
    _ungroupedKeyframeProperties.insert(ser.ungroupedKeyframeProperties.begin(),
                                        ser.ungroupedKeyframeProperties.end());
    _rowsDirty = true;

    ProjectPtr project = gui->getApp()->getProject();

    for (size_t i = 0; i < ser.layers.size(); ++i) {
        const FluxLayerSerialization& layerSer = ser.layers[i];
        FluxLayer layer;

        // Identity
        layer.name = QString::fromStdString(layerSer.name);
        layer.filePath = QString::fromStdString(layerSer.filePath);
        layer.type = QString::fromStdString(layerSer.type);

        // State
        layer.muted = layerSer.muted;
        layer.locked = layerSer.locked;
        layer.solo = layerSer.solo;
        layer.expanded = layerSer.expanded;

        // Timing
        layer.inPoint = layerSer.inPoint;
        layer.outPoint = layerSer.outPoint;
        layer.originalInPoint = layerSer.originalInPoint;
        layer.originalOutPoint = layerSer.originalOutPoint;
        layer.originalFirstFrame = layerSer.originalFirstFrame;
        layer.originalLastFrame = layerSer.originalLastFrame;
        layer.timeOffset = layerSer.timeOffset;
        layer.trimStart = layerSer.trimStart;
        layer.trimEnd = layerSer.trimEnd;
        layer.nodeInitialized = layerSer.nodeInitialized;

        // Source FPS (sanitize non-finite / non-positive to 0.0)
        layer.sourceFrameRate = (std::isfinite(layerSer.sourceFrameRate) && layerSer.sourceFrameRate > 0.0)
            ? layerSer.sourceFrameRate : 0.0;

        // Solid color
        layer.solidColor = QColor(layerSer.solidColorR, layerSer.solidColorG, layerSer.solidColorB);

        // Parenting
        layer.parentLayerIndex = layerSer.parentLayerIndex;

        // Bar color
        layer.color = QColor(layerSer.colorR, layerSer.colorG, layerSer.colorB);

        // Resolve node script names to NodePtr
        if (!layerSer.readerNodeScriptName.empty()) {
            layer.readerNode = project->getNodeByFullySpecifiedName(layerSer.readerNodeScriptName);
            if (!layer.readerNode) {
                qDebug() << "FluxTimeline::restore: reader node not found:" << QString::fromStdString(layerSer.readerNodeScriptName);
            }
        }

        if (!layerSer.gizmoNodeScriptName.empty()) {
            layer.gizmoNode = project->getNodeByFullySpecifiedName(layerSer.gizmoNodeScriptName);
            if (!layer.gizmoNode) {
                qDebug() << "FluxTimeline::restore: gizmo node not found:" << QString::fromStdString(layerSer.gizmoNodeScriptName);
            }
        }

        if (!layerSer.mergeNodeScriptName.empty()) {
            layer.mergeNode = project->getNodeByFullySpecifiedName(layerSer.mergeNodeScriptName);
            if (!layer.mergeNode) {
                qDebug() << "FluxTimeline::restore: merge node not found:" << QString::fromStdString(layerSer.mergeNodeScriptName);
            }
        }

        // Restore effects
        for (size_t e = 0; e < layerSer.effects.size(); ++e) {
            const FluxEffectSerialization& effectSer = layerSer.effects[e];
            FluxEffect effect;
            effect.pluginId = QString::fromStdString(effectSer.pluginId);
            effect.label = QString::fromStdString(effectSer.label);
            effect.enabled = effectSer.enabled;
            effect.isAIMaskCopy = effectSer.isAIMaskCopy;
            effect.aiMaskUsage = QString::fromStdString(effectSer.aiMaskUsage);
            if (effect.isAIMaskCopy && effect.aiMaskUsage.isEmpty()) {
                effect.aiMaskUsage = QString::fromUtf8("legacy-custom-plane");
            }
            effect.aiMaskTargetPlane = QString::fromStdString(effectSer.aiMaskTargetPlane);
            effect.aiMaskSourceChannel = QString::fromStdString(effectSer.aiMaskSourceChannel);
            if (effect.aiMaskSourceChannel.isEmpty()) {
                effect.aiMaskSourceChannel = QString::fromUtf8("red");
            }
            effect.aiMaskOperation = QString::fromStdString(effectSer.aiMaskOperation);
            if (effect.aiMaskUsage == QString::fromUtf8("layer-alpha") && effect.aiMaskOperation.isEmpty()) {
                effect.aiMaskOperation = QString::fromUtf8("max");
            }
            effect.aiMaskSourceRelativePath = QString::fromStdString(effectSer.aiMaskSourceRelativePath);
            effect.aiMaskManifestRelativePath = QString::fromStdString(effectSer.aiMaskManifestRelativePath);
            effect.aiMaskBaseTimeOffset = effectSer.aiMaskBaseTimeOffset;
            if (!effectSer.aiMaskReadNodeScriptName.empty()) {
                effect.aiMaskReadNode = project->getNodeByFullySpecifiedName(effectSer.aiMaskReadNodeScriptName);
            }
            if (!effectSer.aiMaskTimeOffsetNodeScriptName.empty()) {
                effect.aiMaskTimeOffsetNode = project->getNodeByFullySpecifiedName(effectSer.aiMaskTimeOffsetNodeScriptName);
            }
            if (!effectSer.aiMaskShuffleNodeScriptName.empty()) {
                effect.aiMaskShuffleNode = project->getNodeByFullySpecifiedName(effectSer.aiMaskShuffleNodeScriptName);
            }
            if (!effectSer.aiMaskChannelMergeNodeScriptName.empty()) {
                effect.aiMaskChannelMergeNode = project->getNodeByFullySpecifiedName(effectSer.aiMaskChannelMergeNodeScriptName);
            }

            if (!effectSer.nodeScriptName.empty()) {
                effect.node = project->getNodeByFullySpecifiedName(effectSer.nodeScriptName);
                if (!effect.node) {
                    qDebug() << "FluxTimeline::restore: effect node not found:" << QString::fromStdString(effectSer.nodeScriptName);
                    if (effect.aiMaskUsage != QString::fromUtf8("layer-alpha")) {
                        // Skip normal effects if their node is gone. AI layer-alpha
                        // ChannelMerge rows can be recreated from persisted media metadata.
                        continue;
                    }
                }
            }

            layer.effects.append(effect);
        }

        // Restore masks
        // Read hasPrecompBranch from serialization for compatibility, but do not
        // trust the restored value — it will be recomputed by the classifier on
        // the next rebuild. Prevents stale precomp icons after load.
        layer.hasPrecompBranch = false;
        if (!layerSer.maskApplyNodeScriptName.empty()) {
            layer.maskApplyNode = project->getNodeByFullySpecifiedName(layerSer.maskApplyNodeScriptName);
            if (!layer.maskApplyNode) {
                qDebug() << "FluxTimeline::restore: mask apply node not found:" << QString::fromStdString(layerSer.maskApplyNodeScriptName);
            }
        }
        for (size_t m = 0; m < layerSer.masks.size(); ++m) {
            const FluxMaskSerialization& maskSer = layerSer.masks[m];
            FluxMask mask;
            mask.name = QString::fromStdString(maskSer.name);
            mask.type = QString::fromStdString(maskSer.type);
            mask.pluginId = QString::fromStdString(maskSer.pluginId);
            mask.enabled = maskSer.enabled;
            mask.inverted = maskSer.inverted;
            mask.effectIndex = maskSer.effectIndex;

            if (!maskSer.maskNodeScriptName.empty()) {
                mask.maskNode = project->getNodeByFullySpecifiedName(maskSer.maskNodeScriptName);
                if (!mask.maskNode) {
                    qDebug() << "FluxTimeline::restore: mask node not found:" << QString::fromStdString(maskSer.maskNodeScriptName);
                    continue;
                }
            }

            if (!maskSer.reformatNodeScriptName.empty()) {
                mask.reformatNode = project->getNodeByFullySpecifiedName(maskSer.reformatNodeScriptName);
                if (!mask.reformatNode) {
                    qDebug() << "FluxTimeline::restore: mask reformat node not found:" << QString::fromStdString(maskSer.reformatNodeScriptName);
                }
            }
            layer.masks.append(mask);
        }

        // Restore text animator data (version 3+)
        if (layer.type == QString::fromUtf8("text") && layer.gizmoNode && !layerSer.animators.empty()) {
            FluxTextAnimatorModel::restoreAnimators(layer.gizmoNode, layerSer.animators);
        }

        _layers.append(layer);
    }

    _selectedLayer = ser.selectedLayer;
    _selectedKeyPropertyIndex = -1;
    _selectedKeyTime = 0.0;
    _selectedKeys.clear();

    rebuildVisibleRows();
    update();

    Q_EMIT projectLayersRestored();
}
// ── Snap helpers ──

QList<FluxTimeline::SnapTarget>
FluxTimeline::collectSnapTargets(InteractionMode mode,
                                 int sourceLayerIndex,
                                 int sourcePropertyIndex,
                                 double sourceKeyTime) const
{
    QList<SnapTarget> targets;

    // ── Layer edge targets ──
    for (int i = 0; i < _layers.size(); ++i) {
        const FluxLayer& layer = _layers[i];
        int startEdge = layer.inPoint + layer.timeOffset;
        int endEdge   = layer.outPoint + layer.timeOffset;

        // Exclude self-edges for the dragged element
        bool excludeStart = false;
        bool excludeEnd   = false;

        if (mode == eModeTrimLeft && i == sourceLayerIndex) {
            excludeStart = true;
        }
        if (mode == eModeTrimRight && i == sourceLayerIndex) {
            excludeEnd = true;
        }

        if (!excludeStart) {
            SnapTarget t;
            t.frame = startEdge;
            t.kind = eSnapLayerEdge;
            t.layerIndex = i;
            t.propertyIndex = -1;
            targets.append(t);
        }
        if (!excludeEnd) {
            SnapTarget t;
            t.frame = endEdge;
            t.kind = eSnapLayerEdge;
            t.layerIndex = i;
            t.propertyIndex = -1;
            targets.append(t);
        }
    }

    // ── Keyframe targets ──
    // We iterate all layers and all their properties (layer, effect, mask)
    // using the same builders as rebuildVisibleRows.
    for (int i = 0; i < _layers.size(); ++i) {
        const FluxLayer& layer = _layers[i];

        // Layer properties (includes text animator properties for text layers)
        if (layer.gizmoNode) {
            QList<FluxKeyframeProperty> layerProps = buildLayerProperties(layer, _ungroupedKeyframeProperties);
            for (int p = 0; p < layerProps.size(); ++p) {
                // Skip the dragged property's key at sourceKeyTime
                if (mode == eModeDragKeyframe &&
                    i == sourceLayerIndex &&
                    sourcePropertyIndex >= 0 &&
                    sourcePropertyIndex < _propertyRows.size()) {
                    // Compare by knob identity
                    const FluxKeyframeProperty& dragProp = _propertyRows[sourcePropertyIndex];
                    if (layerProps[p].knob == dragProp.knob && layerProps[p].ownerNode == dragProp.ownerNode) {
                        // Add all keys except the one at sourceKeyTime
                        QList<FluxKeyframeKey> keys = keysForProperty(layerProps[p]);
                        for (const auto& k : keys) {
                            int rounded = static_cast<int>(k.time + 0.5);
                            if (mode == eModeDragKeyframe && std::abs(k.time - sourceKeyTime) < 0.5) {
                                continue;
                            }
                            SnapTarget t;
                            t.frame = rounded;
                            t.kind = eSnapKeyframe;
                            t.layerIndex = i;
                            t.propertyIndex = -1; // not referencing _propertyRows
                            targets.append(t);
                        }
                        continue;
                    }
                }
                QList<FluxKeyframeKey> keys = keysForProperty(layerProps[p]);
                for (const auto& k : keys) {
                    SnapTarget t;
                    t.frame = static_cast<int>(k.time + 0.5);
                    t.kind = eSnapKeyframe;
                    t.layerIndex = i;
                    t.propertyIndex = -1;
                    targets.append(t);
                }
            }
        }

        // Effect properties
        for (int ei = 0; ei < layer.effects.size(); ++ei) {
            const FluxEffect& effect = layer.effects[ei];
            if (!effect.node) continue;
            QList<FluxKeyframeProperty> effectProps =
                buildEffectProperties(effect, isAdjustmentRow(i), _ungroupedKeyframeProperties);
            for (int p = 0; p < effectProps.size(); ++p) {
                QList<FluxKeyframeKey> keys = keysForProperty(effectProps[p]);
                for (const auto& k : keys) {
                    SnapTarget t;
                    t.frame = static_cast<int>(k.time + 0.5);
                    t.kind = eSnapKeyframe;
                    t.layerIndex = i;
                    t.propertyIndex = -1;
                    targets.append(t);
                }
            }
        }

        // Mask properties
        for (int mi = 0; mi < layer.masks.size(); ++mi) {
            const FluxMask& mask = layer.masks[mi];
            QList<FluxKeyframeProperty> maskProps = buildMaskProperties(mask);
            for (int p = 0; p < maskProps.size(); ++p) {
                QList<FluxKeyframeKey> keys = keysForProperty(maskProps[p]);
                for (const auto& k : keys) {
                    SnapTarget t;
                    t.frame = static_cast<int>(k.time + 0.5);
                    t.kind = eSnapKeyframe;
                    t.layerIndex = i;
                    t.propertyIndex = -1;
                    targets.append(t);
                }
            }
        }
    }

    return targets;
}

bool
FluxTimeline::resolveSnapFrame(int candidateFrame,
                                const QList<SnapTarget>& targets,
                                int* snappedFrame,
                                SnapTargetKind* snappedKind) const
{
    if (targets.isEmpty()) {
        return false;
    }

    int candX = frameToX(candidateFrame);
    double bestDist = _snapTolerancePixels + 1.0; // +1 so equal-tolerance fails
    int bestFrame = candidateFrame;
    SnapTargetKind bestKind = eSnapNone;

    for (const SnapTarget& t : targets) {
        int tX = frameToX(t.frame);
        double dist = qAbs(tX - candX);
        if (dist < bestDist ||
            (dist == bestDist && t.kind < bestKind) ||
            (dist == bestDist && t.kind == bestKind && t.frame < bestFrame)) {
            bestDist = dist;
            bestFrame = t.frame;
            bestKind = t.kind;
        }
    }

    if (bestDist <= _snapTolerancePixels) {
        *snappedFrame = bestFrame;
        *snappedKind = bestKind;
        return true;
    }
    return false;
}

void
FluxTimeline::clearSnapState()
{
    _snapActive = false;
    _snapFrame = 0;
    _snapKind = eSnapNone;
}

void
FluxTimeline::setSnapState(int snappedFrame, SnapTargetKind kind)
{
    _snapActive = true;
    _snapFrame = snappedFrame;
    _snapKind = kind;
}

void
FluxTimeline::drawSnapIndicator(QPainter& painter, const QRect& rect)
{
    if (!_snapActive || !_snapShiftHeld) {
        return;
    }

    int x = frameToX(_snapFrame);
    if (x < _layerLabelWidth) {
        return;
    }

    // Vertical snap guide line — cyan/blue, distinct from red playhead
    QColor snapColor(0, 200, 220, 180);
    painter.setPen(QPen(snapColor, 1, Qt::DashLine));
    painter.drawLine(x, kTimeRulerHeight, x, rect.height());

    // Small label near the ruler
    QFont font;
    font.setPointSize(7);
    painter.setFont(font);
    painter.setPen(snapColor);
    QRect labelRect(x - 30, kTimeRulerHeight - 14, 60, 14);
    painter.drawText(labelRect, Qt::AlignCenter, QString::fromUtf8("%1").arg(_snapFrame));
}

NATRON_NAMESPACE_EXIT

NATRON_NAMESPACE_USING
#include "moc_FluxTimeline.cpp"
