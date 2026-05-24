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
#include <QMimeData>
#include <QUrl>
#include <QMenu>
#include <QDrag>
#include <QKeyEvent>

#include "Gui/Gui.h"
#include "Gui/GuiAppInstance.h"
#include "Gui/FluxEffectsPanel.h"
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
#include "Engine/ViewIdx.h"
#include "Engine/CreateNodeArgs.h"
#include "Gui/ViewerTab.h"
#include "Gui/GuiApplicationManager.h"
#include "Engine/ViewerInstance.h"
#include "Gui/FluxTimelineSerialization.h"
#include "Gui/FluxMaskUtils.h"

NATRON_NAMESPACE_ENTER

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

    // Assign color based on type
    if (type == QString::fromUtf8("footage")) {
        layer.color = QColor(80, 130, 200);
    } else if (type == QString::fromUtf8("solid")) {
        layer.color = QColor(130, 180, 80);
    } else if (type == QString::fromUtf8("text")) {
        layer.color = QColor(200, 130, 80);
    } else if (type == QString::fromUtf8("adjustment")) {
        layer.color = QColor(180, 130, 80);
    } else {
        layer.color = QColor(120, 120, 120);
    }

    _layers.append(layer);
    rebuildVisibleRows();
    Q_EMIT compositingChanged();
    update();
}

void
FluxTimeline::addSolidLayer(const QColor& color)
{
    FluxLayer layer;
    layer.name = QString::fromUtf8("Solid");
    layer.type = QString::fromUtf8("solid");
    layer.inPoint = _firstFrame;
    layer.outPoint = _lastFrame;
    layer.color = QColor(130, 180, 80);
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
    layer.color = QColor(200, 130, 80);

    _layers.append(layer);
    rebuildVisibleRows();
    Q_EMIT compositingChanged();
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
    layer.color = QColor(180, 180, 180);
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
    Q_EMIT layerSelected(-1);
    showNodeCreationDialog();
    update();
}

void
FluxTimeline::removeLayer(int index)
{
    if (index >= 0 && index < _layers.size() && !_layers[index].locked) {
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
            layer.mergeNode->deactivate(std::list<NodePtr>(), false, true);
            layer.mergeNode.reset();
        }
        for (int e = 0; e < layer.effects.size(); ++e) {
            if (layer.effects[e].node) {
                layer.effects[e].node->deactivate(std::list<NodePtr>(), false, true);
                layer.effects[e].node.reset();
            }
        }
        if (layer.gizmoNode) {
            layer.gizmoNode->deactivate(std::list<NodePtr>(), false, true);
            layer.gizmoNode.reset();
        }
        if (layer.readerNode) {
            layer.readerNode->deactivate(std::list<NodePtr>(), false, true);
            layer.readerNode.reset();
        }
        _layers.removeAt(index);
        rebuildVisibleRows();
        if (_selectedLayer == index) {
            _selectedLayer = -1;
            _selectedType = eFluxSelectionNone;
            _selectedEffectIndex = -1;
            _selectedMaskIndex = -1;
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

        fprintf(stderr, "FLUX DUPLICATE: adjustment row '%s' duplicated with %d effects at index %d\n",
                layer.name.toStdString().c_str(), newEffects.size(), index);

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

    fprintf(stderr, "FLUX DUPLICATE: layer '%s' branch-aware duplicated with %d effects, %d masks at index %d\n",
            layer.name.toStdString().c_str(), newEffects.size(), newMasks.size(), index);

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

    FluxEffect effect = layer.effects.takeAt(effectIndex);
    if (effect.node && effect.node->isActivated()) {
        effect.node->deactivate(std::list<NodePtr>(), false, true);
    }

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

    refreshVisibleRows();
    Q_EMIT masksChanged(layerIndex);
    Q_EMIT compositingChanged();
    Q_EMIT maskSelected(layerIndex, newMaskIndex);
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
    // Layer masks no longer use reformatNode (T064). Deactivate if present (old artifact).
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
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);

    // Safety: rebuild visible rows if out of sync
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

    QRect totalRect = rect();

    // Background — subtle violet tint to differentiate the timeline panel.
    painter.fillRect(totalRect, QColor(31, 29, 36));

    // Time ruler area
    QRect rulerRect(_layerLabelWidth, 0, totalRect.width() - _layerLabelWidth, kTimeRulerHeight);
    drawTimeRuler(painter, rulerRect);

    // Control column background (L/V/S buttons) — darker tint
    painter.fillRect(0, kTimeRulerHeight, kControlColumnWidth, totalRect.height() - kTimeRulerHeight, QColor(38, 34, 44));

    // Name column background
    painter.fillRect(kControlColumnWidth, kTimeRulerHeight, _layerLabelWidth - kControlColumnWidth, totalRect.height() - kTimeRulerHeight, QColor(40, 36, 47));

    // Panel accent: small, non-invasive color cue.
    painter.fillRect(0, 0, totalRect.width(), 2, QColor(120, 92, 155));

    // Layer bars area
    QRect barsRect(_layerLabelWidth, kTimeRulerHeight, totalRect.width() - _layerLabelWidth, totalRect.height() - kTimeRulerHeight);
    drawLayerBars(painter, barsRect);

    // Playhead
    drawPlayhead(painter, totalRect);

    // T019-B: Drag preview ghost bar
    if (_isDragOver) {
        drawDragPreview(painter, totalRect);
    }

    // Separator lines
    painter.setPen(QColor(60, 60, 65));
    painter.drawLine(0, kTimeRulerHeight, totalRect.width(), kTimeRulerHeight);
    painter.drawLine(_layerLabelWidth, 0, _layerLabelWidth, totalRect.height());
    painter.drawLine(kControlColumnWidth, kTimeRulerHeight, kControlColumnWidth, totalRect.height());

    // Resize handle (subtle grip dots at the right edge of the label panel)
    {
        int handleX = _layerLabelWidth;
        int cy = totalRect.height() / 2;
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(90, 90, 95));
        for (int dy = -12; dy <= 12; dy += 6) {
            painter.drawRect(handleX - 2, cy + dy - 1, 4, 2);
        }
        painter.setBrush(Qt::NoBrush);
    }

    // Fit button in top-left corner (control column header, in ruler row)
    {
        QRect fitBtnRect(2, 2, kControlColumnWidth - 4, kTimeRulerHeight - 4);
        painter.fillRect(fitBtnRect, QColor(55, 55, 60));
        painter.setPen(QColor(160, 160, 170));
        QFont fitFont;
        fitFont.setPointSize(8);
        painter.setFont(fitFont);
        painter.drawText(fitBtnRect, Qt::AlignCenter, QString::fromUtf8("Fit"));
        painter.setPen(QColor(70, 70, 75));
        painter.drawRect(fitBtnRect);
    }
}

void
FluxTimeline::drawTimeRuler(QPainter& painter,
                            const QRect& rect)
{
    painter.fillRect(rect, QColor(45, 40, 54));

    painter.setPen(QColor(140, 140, 150));
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
        painter.setPen(QColor(80, 80, 85));
        painter.drawLine(x, rect.bottom() - 4, x, rect.bottom());
        painter.setPen(QColor(140, 140, 150));
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

            // ── Control column background (L/V/S) ──
            QRect ctrlRect(0, y, kControlColumnWidth, rowHeight);
            if (isSelected) {
                painter.fillRect(ctrlRect, QColor(56, 113, 204));
            } else {
                painter.fillRect(ctrlRect, QColor(45, 45, 50));
            }

            // ── Name column background ──
            int nameColX = kControlColumnWidth;
            int nameColW = _layerLabelWidth - kControlColumnWidth;
            QRect nameRect(nameColX, y, nameColW, rowHeight);
            if (isSelected) {
                painter.fillRect(nameRect, QColor(56, 113, 204));
                painter.setPen(Qt::white);
            } else {
                painter.fillRect(nameRect, QColor(45, 45, 50));
                painter.setPen(QColor(200, 200, 210));
            }

            // Disclosure arrow if layer has effects or masks
            const bool hasChildren = !layer.effects.isEmpty() || !layer.masks.isEmpty();
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
            int availableWidth = nameRect.width() - textLeftPad - 4;
            QString displayName = fm.elidedText(layer.name, Qt::ElideRight, qMax(0, availableWidth));
            painter.drawText(nameRect.adjusted(textLeftPad, 0, -4, 0), Qt::AlignVCenter | Qt::AlignLeft, displayName);

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
                painter.setPen(Qt::NoPen);
                painter.fillRect(lockBtnX, btnY + btnInsetY, 16, btnHeight, QColor(220, 160, 50));
                painter.setPen(QColor(40, 40, 40));
            } else {
                painter.setPen(isSelected ? QColor(180, 180, 200) : QColor(100, 100, 110));
            }
            painter.drawText(QRect(lockBtnX, btnY, 16, btnH), Qt::AlignCenter, QString::fromUtf8("L"));

            // V button (Visibility) at x=20
            int visBtnX = 20;
            if (layer.muted) {
                painter.setPen(Qt::NoPen);
                painter.fillRect(visBtnX, btnY + btnInsetY, 16, btnHeight, QColor(80, 180, 120));
                painter.setPen(QColor(40, 40, 40));
            } else {
                painter.setPen(isSelected ? QColor(180, 180, 200) : QColor(100, 100, 110));
            }
            painter.drawText(QRect(visBtnX, btnY, 16, btnH), Qt::AlignCenter, QString::fromUtf8("V"));

            // S button (Solo) at x=36
            int soloBtnX = 36;
            if (isAdjustmentRow(i)) {
                // Solo is disabled for adjustment rows — always draw dim
                painter.setPen(QColor(60, 60, 65));
            } else if (layer.solo) {
                painter.setPen(Qt::NoPen);
                painter.fillRect(soloBtnX, btnY + btnInsetY, 16, btnHeight, QColor(255, 200, 50));
                painter.setPen(QColor(40, 40, 40));
            } else {
                painter.setPen(isSelected ? QColor(180, 180, 200) : QColor(100, 100, 110));
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

                // Fill entire bar with desaturated color
                QColor desatColor = layer.color.darker(170);
                if (effectivelyMuted) {
                    desatColor = desatColor.darker(200);
                }
                if (layer.locked) {
                    desatColor = desatColor.darker(150);
                }
                painter.fillRect(barRect, desatColor);

                activeX1 = qMax(activeX1, clipLeft);
                activeX2 = qMax(activeX2, clipLeft);

                // Fill active zone with normal color (overwrites desaturated)
                if (activeX2 > activeX1) {
                    QRect activeRect(activeX1, barRect.top(), activeX2 - activeX1, barRect.height());
                    QColor activeColor = effectivelyMuted ? layer.color.darker(200) : layer.color;
                    if (layer.locked) {
                        activeColor = activeColor.darker(150);
                    }
                    painter.fillRect(activeRect, activeColor);
                }

                // Bar border
                painter.setPen(layer.color.darker(130));
                painter.drawRect(barRect);

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
            painter.setPen(QColor(55, 55, 60));
            painter.drawLine(0, y + rowHeight, width(), y + rowHeight);

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

            // Dark subtle row background across full width
            QColor rowBg = isEffectSelected ? QColor(46, 93, 174) : QColor(33, 33, 37);
            painter.fillRect(0, y, width(), rowHeight, rowBg);

            // Separator across timeline area
            painter.setPen(QColor(48, 48, 52));
            painter.drawLine(_layerLabelWidth, y + rowHeight, width(), y + rowHeight);

            // Indented effect label in name column
            int labelX = kControlColumnWidth + kEffectIndent;
            QRect effectLabelRect(labelX, y, _layerLabelWidth - labelX - 4, rowHeight);
            QFont effectFont;
            effectFont.setPointSize(8);
            painter.setFont(effectFont);
            painter.setPen(isEffectSelected ? Qt::white : QColor(170, 170, 180));
            QFontMetrics efm(effectFont);
            int effectAvailW = effectLabelRect.width();
            QString effectLabel = efm.elidedText(effect.label, Qt::ElideRight, qMax(0, effectAvailW));

            // Small "fx" glyph prefix
            painter.setPen(isEffectSelected ? QColor(200, 200, 255) : QColor(120, 140, 180));
            painter.drawText(effectLabelRect, Qt::AlignVCenter | Qt::AlignLeft,
                             QString::fromUtf8("\xE2\x97\x8F ") + effectLabel);

            // Separator in name column
            painter.setPen(QColor(48, 48, 52));
            painter.drawLine(kControlColumnWidth, y, kControlColumnWidth, y + rowHeight);

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
            painter.fillRect(0, y, width(), rowHeight, rowBg);

            // Separator across timeline area
            painter.setPen(QColor(44, 44, 48));
            painter.drawLine(_layerLabelWidth, y + rowHeight, width(), y + rowHeight);

            // Deeper indent than effect rows
            int labelX = kControlColumnWidth + kEffectIndent + 12;
            QRect maskLabelRect(labelX, y, _layerLabelWidth - labelX - 4, rowHeight);
            QFont maskFont;
            maskFont.setPointSize(8);
            painter.setFont(maskFont);
            QFontMetrics mfm(maskFont);
            int maskAvailW = maskLabelRect.width();
            QString maskLabel = mask.name.isEmpty() ? QString::fromUtf8("Mask") : mask.name;
            maskLabel = mfm.elidedText(maskLabel, Qt::ElideRight, qMax(0, maskAvailW));

            painter.setPen(isMaskSelected ? QColor(200, 200, 255) : QColor(100, 160, 140));
            painter.drawText(maskLabelRect, Qt::AlignVCenter | Qt::AlignLeft,
                             QString::fromUtf8("M ") + maskLabel);

            // Separator in name column
            painter.setPen(QColor(44, 44, 48));
            painter.drawLine(kControlColumnWidth, y, kControlColumnWidth, y + rowHeight);
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

    // Playhead line
    painter.setPen(QPen(QColor(220, 50, 50), 2));
    painter.drawLine(x, 0, x, rect.height());

    // Playhead triangle at top
    QPainterPath triangle;
    triangle.moveTo(x - 6, 0);
    triangle.lineTo(x + 6, 0);
    triangle.lineTo(x, 10);
    triangle.closeSubpath();
    painter.fillPath(triangle, QColor(220, 50, 50));

    // Frame number on playhead
    painter.setPen(Qt::white);
    QFont font;
    font.setPointSize(7);
    font.setBold(true);
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

    // Only draw if in the layer bars area
    if (x <= _layerLabelWidth || y <= kTimeRulerHeight) {
        return;
    }

    int inFrame = xToFrame(x);
    int outFrame = inFrame + 50; // Default 50-frame duration for preview

    // Determine which row the drop would land on
    int row = insertionLayerIndexForY(y);
    if (row < 0) {
        row = 0;
    }
    // Allow appending beyond existing layers
    if (row > _layers.size()) {
        row = _layers.size();
    }

    int barY = insertionYForLayerIndex(row) + 4;
    int barX1 = frameToX(inFrame);
    int barX2 = frameToX(outFrame);

    // Semi-transparent ghost bar
    QColor ghostColor(100, 160, 240, 100);
    QRect ghostRect(barX1, barY, barX2 - barX1, kLayerRowHeight - 8);
    painter.fillRect(ghostRect, ghostColor);

    // Ghost border
    painter.setPen(QPen(QColor(100, 160, 240, 180), 1, Qt::DashLine));
    painter.drawRect(ghostRect);

    // Ghost label
    painter.setPen(QColor(200, 220, 255, 200));
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
                }
            }
        }
    }
    _totalContentHeight = y;
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
    // ── Middle button or Alt+Left: start pan ──
    if (event->button() == Qt::MiddleButton ||
        (event->button() == Qt::LeftButton && (event->modifiers() & Qt::AltModifier))) {
        _interactionMode = eModePan;
        _interactionStartX = event->pos().x();
        _interactionStartY = event->pos().y();
        _panStartScrollX = _scrollOffsetX;
        _panStartScrollY = _scrollOffsetY;
        setCursor(Qt::ClosedHandCursor);
        return;
    }

    if (event->button() != Qt::LeftButton) {
        return;
    }

    int x = event->pos().x();
    int y = event->pos().y();

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
            Q_EMIT layerSelected(-1);
            update();
            return;
        }

        if (clickRow->type == eFluxVisibleRowEffect) {
            // Select effect sub-row in label area
            _selectedType = eFluxSelectionEffect;
            _selectedLayer = clickRow->layerIndex;
            _selectedEffectIndex = clickRow->childIndex;
            _selectedMaskIndex = -1;
            Q_EMIT effectSelected(_selectedLayer, _selectedEffectIndex);
            update();
            return;
        }

        if (clickRow->type == eFluxVisibleRowMask) {
            // Select mask sub-row in label area
            _selectedType = eFluxSelectionMask;
            _selectedLayer = clickRow->layerIndex;
            _selectedEffectIndex = clickRow->effectIndex;
            _selectedMaskIndex = clickRow->maskIndex;
            Q_EMIT maskSelected(_selectedLayer, _selectedMaskIndex);
            update();
            return;
        }

        int layerIdx = clickRow->layerIndex;
        if (layerIdx >= 0 && layerIdx < _layers.size()) {
            // Check disclosure arrow
            const FluxLayer& layer = _layers[layerIdx];
            const bool hasChildren = !layer.effects.isEmpty() || !layer.masks.isEmpty();
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
            _selectedType = eFluxSelectionEffect;
            _selectedLayer = clickRow->layerIndex;
            _selectedEffectIndex = clickRow->childIndex;
            _selectedMaskIndex = -1;
            Q_EMIT effectSelected(_selectedLayer, _selectedEffectIndex);
            update();
            return;
        }
        if (clickRow && clickRow->type == eFluxVisibleRowMask) {
            _selectedType = eFluxSelectionMask;
            _selectedLayer = clickRow->layerIndex;
            _selectedEffectIndex = clickRow->effectIndex;
            _selectedMaskIndex = clickRow->maskIndex;
            Q_EMIT maskSelected(_selectedLayer, _selectedMaskIndex);
            update();
            return;
        }

        int layerIdx = -1;
        HitZone zone = hitTest(x, y, &layerIdx);

        if (zone == eHitNone) {
            // Clicked empty space in bar area — move playhead only
            _selectedType = eFluxSelectionNone;
            _selectedLayer = -1;
            _selectedEffectIndex = -1;
            _selectedMaskIndex = -1;
            Q_EMIT layerSelected(-1);
            _interactionMode = eModeDragPlayhead;
            _currentFrame = xToFrame(x);
            if (_timeline) {
                _timeline->seekFrame(SequenceTime(_currentFrame), false, nullptr, eTimelineChangeReasonUserSeek);
            }
            Q_EMIT frameChanged(_currentFrame);
            update();
            return;
        }

        // Select the layer
        _selectedType = eFluxSelectionLayer;
        _selectedLayer = layerIdx;
        _selectedEffectIndex = -1;
        _selectedMaskIndex = -1;
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
            } else {
                _interactionMode = eModeNone;
            }
        } else if (zone == eHitTrimRight) {
            if (canTrimRow(layerIdx)) {
                _interactionMode = eModeTrimRight;
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
        int newIn = _interactionOrigInPoint + frameDelta;
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
        int newOut = _interactionOrigOutPoint + frameDelta;
        // Clamp: outPoint must stay after inPoint
        newOut = qMax(newOut, _interactionOrigInPoint + 1);
        _layers[_interactionLayerIndex].outPoint = newOut;
        // Update explicit trim state:
        // trimEnd = how many frames we've moved outPoint left from initial position
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

    if (_interactionMode == eModePan || _interactionMode == eModeResizePanel) {
        unsetCursor();
    }

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
                Q_EMIT effectSelected(_selectedLayer, _selectedEffectIndex);
                update();
            } else if (row->type == eFluxVisibleRowMask) {
                // Double-click mask row: select and open properties
                _selectedType = eFluxSelectionMask;
                _selectedLayer = row->layerIndex;
                _selectedEffectIndex = row->effectIndex;
                _selectedMaskIndex = row->maskIndex;
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
        bool textProviderAvailable = false;
        if (appPTR) {
            const std::list<std::string> textPlugins = appPTR->getPluginIDs("Text");
            for (std::list<std::string>::const_iterator it = textPlugins.begin(); it != textPlugins.end(); ++it) {
                if (*it == std::string("net.fxarena.openfx.Text")) {
                    textProviderAvailable = true;
                    break;
                }
            }
        }
        textAction->setEnabled(textProviderAvailable);
        if (!textProviderAvailable) {
            textAction->setToolTip(QString::fromUtf8("Text.ofx provider is not available."));
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

            // Open Read Node (footage layers only)
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
            }

            QAction* addEffectAction = menu.addAction(QString::fromUtf8("Add Effect..."));
            addEffectAction->setEnabled(canAddEffectToRow(layerIdx));
            connect(addEffectAction, &QAction::triggered, this, [this, layerIdx]() {
                // Select this layer and focus the effects panel
                _selectedType = eFluxSelectionLayer;
                _selectedLayer = layerIdx;
                _selectedEffectIndex = -1;
                _selectedMaskIndex = -1;
                Q_EMIT layerSelected(layerIdx);
                update();
                showNodeCreationDialog();
            });

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
        if (_selectedType == eFluxSelectionMask &&
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
        Q_EMIT layerSelected(-1);
        update();
        event->accept();
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

    // Text OFX exposes its own frame range plus the host node lifetime range.
    // Keep both in sync so timeline trim gates the native Text node itself.
    if (layer.type == QString::fromUtf8("text")) {
        KnobIPtr enableLifeKnob = layer.gizmoNode->getKnobByName(std::string("enableNodeLifeTime"));
        if (enableLifeKnob) {
            KnobBoolPtr boolKnob = std::dynamic_pointer_cast<KnobBool>(enableLifeKnob);
            if (boolKnob) {
                boolKnob->setValue(true, ViewSpec::all(), 0);
            }
        }
        KnobIPtr lifeRangeKnob = layer.gizmoNode->getKnobByName(std::string("nodeLifeTime"));
        if (lifeRangeKnob) {
            KnobIntBasePtr int2D = std::dynamic_pointer_cast<KnobIntBase>(lifeRangeKnob);
            if (int2D) {
                int2D->setValue(layer.inPoint, ViewSpec::all(), 0);
                int2D->setValue(layer.outPoint, ViewSpec::all(), 1);
            }
        }
        return;
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

        ser.layers.push_back(layerSer);
    }

    return ser;
}

void
FluxTimeline::restoreFromProjectSerialization(const FluxTimelineSerialization& ser,
                                                Gui* gui)
{
    _layers.clear();

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

            if (!effectSer.nodeScriptName.empty()) {
                effect.node = project->getNodeByFullySpecifiedName(effectSer.nodeScriptName);
                if (!effect.node) {
                    qDebug() << "FluxTimeline::restore: effect node not found:" << QString::fromStdString(effectSer.nodeScriptName);
                    // Skip this effect — node is gone
                    continue;
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

        _layers.append(layer);
    }

    _selectedLayer = ser.selectedLayer;

    rebuildVisibleRows();
    update();
}
NATRON_NAMESPACE_EXIT

NATRON_NAMESPACE_USING
#include "moc_FluxTimeline.cpp"
