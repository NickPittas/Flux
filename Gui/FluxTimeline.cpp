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
#include "Engine/ViewerInstance.h"
#include "Gui/FluxTimelineSerialization.h"

NATRON_NAMESPACE_ENTER

FluxTimeline::FluxTimeline(Gui* gui,
                           QWidget* parent)
    : QWidget(parent)
      , PanelWidget(this, gui)
      , _firstFrame(0)
      , _lastFrame(100)
      , _currentFrame(0)
      , _selectedLayer(-1)
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
    } else if (type == QString::fromUtf8("adjustment")) {
        layer.color = QColor(180, 130, 80);
    } else {
        layer.color = QColor(120, 120, 120);
    }

    _layers.append(layer);
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
    Q_EMIT compositingChanged();
    update();
}

void
FluxTimeline::removeLayer(int index)
{
    if (index >= 0 && index < _layers.size() && !_layers[index].locked) {
        FluxLayer& layer = _layers[index];
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
        if (_selectedLayer == index) {
            _selectedLayer = -1;
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

        Q_EMIT compositingChanged();
        update();
        return true;
    }

    // ---- Footage/solid row duplicate ----

    // 1. Collect nodes to copy: Read (if footage) + Gizmo + Effects + Merge
    NodesGuiList nodesToCopy;

    if (layer.readerNode) {
        NodeGuiIPtr readGuiI = layer.readerNode->getNodeGui();
        NodeGuiPtr readGui = std::dynamic_pointer_cast<NodeGui>(readGuiI);
        if (readGui) {
            nodesToCopy.push_back(readGui);
        }
    }

    {
        NodeGuiIPtr gizmoGuiI = layer.gizmoNode->getNodeGui();
        NodeGuiPtr gizmoGui = std::dynamic_pointer_cast<NodeGui>(gizmoGuiI);
        if (gizmoGui) {
            nodesToCopy.push_back(gizmoGui);
        }
    }

    // Add child effect nodes (between gizmo output and merge input)
    for (int e = 0; e < layer.effects.size(); ++e) {
        if (layer.effects[e].node) {
            NodeGuiIPtr eguiI = layer.effects[e].node->getNodeGui();
            NodeGuiPtr egui = std::dynamic_pointer_cast<NodeGui>(eguiI);
            if (egui) {
                nodesToCopy.push_back(egui);
            }
        }
    }

    {
        NodeGuiIPtr mergeGuiI = layer.mergeNode->getNodeGui();
        NodeGuiPtr mergeGui = std::dynamic_pointer_cast<NodeGui>(mergeGuiI);
        if (mergeGui) {
            nodesToCopy.push_back(mergeGui);
        }
    }

    if (nodesToCopy.size() < 2) {
        fprintf(stderr, "FLUX ERROR: duplicateLayer — could not find NodeGui for gizmo/merge\n");
        return false;
    }

    size_t expectedCount = nodesToCopy.size();

    // 2. Copy nodes into clipboard
    NodeClipBoard clipboard;
    nodeGraph->copyNodes(nodesToCopy, clipboard);

    if (clipboard.nodes.size() != expectedCount) {
        fprintf(stderr, "FLUX ERROR: duplicateLayer — clipboard has %zu nodes, expected %zu\n",
                clipboard.nodes.size(), expectedCount);
        return false;
    }

    // 3. Paste — creates new nodes
    std::list<std::pair<std::string, NodeGuiPtr>> newNodes;
    nodeGraph->pasteCliboard(clipboard, &newNodes);

    if (newNodes.size() != expectedCount) {
        fprintf(stderr, "FLUX ERROR: duplicateLayer — paste returned %zu nodes, expected %zu\n",
                newNodes.size(), expectedCount);
        return false;
    }

    // 4. Identify pasted nodes by plugin ID
    // Collect all pasted nodes and classify them
    NodePtr newRead;
    NodePtr newGizmo;
    NodePtr newMerge;
    QList<NodePtr> pastedEffectNodes; // effects in paste order

    for (auto& pair : newNodes) {
        NodePtr n = pair.second->getNode();
        if (!n) continue;
        const std::string& pluginId = n->getPluginID();
        if (pluginId == "net.sf.openfx.FluxSolid" ||
            pluginId == "net.sf.openfx.FluxLayer") {
            newGizmo = n;
        } else if (pluginId == "net.sf.openfx.MergePlugin" ||
                   pluginId.find("Merge") != std::string::npos) {
            newMerge = n;
        } else if (pluginId.find("Read") != std::string::npos) {
            newRead = n;
        } else {
            // Everything else that's not Read/Gizmo/Merge is a child effect
            pastedEffectNodes.append(n);
        }
    }

    if (!newGizmo || !newMerge) {
        fprintf(stderr, "FLUX ERROR: duplicateLayer — could not identify pasted gizmo (%p) or merge (%p)\n",
                newGizmo.get(), newMerge.get());
        return false;
    }

    // 5. Match pasted effect nodes to original effects by plugin ID (order preserved by clipboard)
    QList<FluxEffect> newEffects;
    if (!layer.effects.isEmpty() && !pastedEffectNodes.isEmpty()) {
        // The clipboard preserves the order we added nodes.
        // We added effects in order between gizmo and merge.
        // Match pasted effects to original effects by sequential plugin ID comparison.
        int peIdx = 0;
        for (int e = 0; e < layer.effects.size() && peIdx < pastedEffectNodes.size(); ++e) {
            // Find the next pasted node matching this effect's plugin
            for (int p = peIdx; p < pastedEffectNodes.size(); ++p) {
                if (pastedEffectNodes[p]->getPluginID() == layer.effects[e].pluginId.toStdString()) {
                    FluxEffect fe;
                    fe.pluginId = layer.effects[e].pluginId;
                    fe.label = QString::fromStdString(pastedEffectNodes[p]->getLabel());
                    fe.node = pastedEffectNodes[p];
                    fe.enabled = layer.effects[e].enabled;
                    newEffects.append(fe);
                    peIdx = p + 1;
                    break;
                }
            }
        }
    }

    // 6. Create new FluxLayer as a copy of the original
    FluxLayer duplicate = layer;
    duplicate.gizmoNode = newGizmo;
    duplicate.mergeNode = newMerge;
    duplicate.readerNode = newRead;
    duplicate.effects = newEffects;
    duplicate.nodeInitialized = true; // knobs already set by paste

    // 6b. For footage layers, re-set filename on the new external Read node.
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

    // 7. Insert duplicate above the original (at same index, pushing original down)
    _layers.insert(index, duplicate);

    // 8. Adjust selected layer
    if (_selectedLayer >= index) {
        ++_selectedLayer;
    }

    fprintf(stderr, "FLUX DUPLICATE: layer '%s' duplicated with %d effects above index %d\n",
            layer.name.toStdString().c_str(), newEffects.size(), index);

    // 9. Trigger rebuild to reconnect and reposition the node graph
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
    // Adjustment rows can duplicate (effects only, no gizmo/merge)
    // Footage/solid rows need gizmo + merge
    if (l.type != QString::fromUtf8("adjustment")) {
        if (!l.gizmoNode || !l.mergeNode) return false;
    }
    return true;
}

bool
FluxTimeline::canSplitRow(int index) const
{
    if (index < 0 || index >= _layers.size()) return false;
    const FluxLayer& l = _layers[index];
    if (l.locked) return false;
    if (l.type == QString::fromUtf8("null")) return false;
    // All non-null, non-locked rows can be split.
    // Adjustment row split: duplicates row + trims both halves via disable-knob keyframes.
    // Footage/solid split: duplicates row + trims both halves via FrameRange knob.
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

    QRect totalRect = rect();

    // Background
    painter.fillRect(totalRect, QColor(30, 30, 34));

    // Time ruler area
    QRect rulerRect(_layerLabelWidth, 0, totalRect.width() - _layerLabelWidth, kTimeRulerHeight);
    drawTimeRuler(painter, rulerRect);

    // Control column background (L/V/S buttons) — darker tint
    painter.fillRect(0, kTimeRulerHeight, kControlColumnWidth, totalRect.height() - kTimeRulerHeight, QColor(35, 35, 39));

    // Name column background
    painter.fillRect(kControlColumnWidth, kTimeRulerHeight, _layerLabelWidth - kControlColumnWidth, totalRect.height() - kTimeRulerHeight, QColor(38, 38, 42));

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
    painter.fillRect(rect, QColor(42, 42, 48));

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
    for (int i = 0; i < _layers.size(); ++i) {
        const FluxLayer& layer = _layers[i];
        int y = kTimeRulerHeight + i * kLayerRowHeight - _scrollOffsetY;

        if (y + kLayerRowHeight < rect.top() || y > rect.bottom()) {
            continue;
        }

        bool isSelected = (i == _selectedLayer);

        // ── Control column background (L/V/S) ──
        QRect ctrlRect(0, y, kControlColumnWidth, kLayerRowHeight);
        if (isSelected) {
            painter.fillRect(ctrlRect, QColor(56, 113, 204));
        } else {
            painter.fillRect(ctrlRect, QColor(45, 45, 50));
        }

        // ── Name column background ──
        int nameColX = kControlColumnWidth;
        int nameColW = _layerLabelWidth - kControlColumnWidth;
        QRect nameRect(nameColX, y, nameColW, kLayerRowHeight);
        if (isSelected) {
            painter.fillRect(nameRect, QColor(56, 113, 204));
            painter.setPen(Qt::white);
        } else {
            painter.fillRect(nameRect, QColor(45, 45, 50));
            painter.setPen(QColor(200, 200, 210));
        }

        // Layer name with proper elision
        QFont font;
        font.setPointSize(9);
        painter.setFont(font);
        QFontMetrics fm(font);
        int availableWidth = nameRect.width() - 8; // 4px padding each side
        QString displayName = fm.elidedText(layer.name, Qt::ElideRight, qMax(0, availableWidth));
        painter.drawText(nameRect.adjusted(4, 0, -4, 0), Qt::AlignVCenter | Qt::AlignLeft, displayName);

        // ── L/V/S buttons in control column at fixed positions ──
        QFont smallFont;
        smallFont.setPointSize(8);
        painter.setFont(smallFont);
        int btnY = y;
        int btnH = kLayerRowHeight;
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
        QRect barRect(barX1, y + 4, barX2 - barX1, kLayerRowHeight - 8);

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
        painter.drawLine(0, y + kLayerRowHeight, width(), y + kLayerRowHeight);
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
    int row = (y - kTimeRulerHeight + _scrollOffsetY) / kLayerRowHeight;
    if (row < 0) {
        row = 0;
    }
    // Allow appending beyond existing layers
    if (row > _layers.size()) {
        row = _layers.size();
    }

    int barY = kTimeRulerHeight + row * kLayerRowHeight - _scrollOffsetY + 4;
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

int
FluxTimeline::yToLayer(int y) const
{
    int adjustedY = y - kTimeRulerHeight + _scrollOffsetY;
    int index = adjustedY / kLayerRowHeight;
    if (index >= 0 && index < _layers.size()) {
        return index;
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
    int totalLayerHeight = _layers.size() * kLayerRowHeight;
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
                             Q_EMIT layerSelected(targetLayer);
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
                             _selectedLayer = 0;
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

    int layerIdx = yToLayer(y);
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

    // Click on layer label area → check L/V/S buttons, then select layer
    if (x < _layerLabelWidth && y > kTimeRulerHeight) {
        int layerIdx = yToLayer(y);
        if (layerIdx >= 0 && layerIdx < _layers.size()) {
            int btnY = kTimeRulerHeight + layerIdx * kLayerRowHeight - _scrollOffsetY;

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
            _selectedLayer = layerIdx;
            Q_EMIT layerSelected(layerIdx);
            update();
        }
        return;
    }

    // Click on layer bar area → hit-test to determine what was clicked
    if (x > _layerLabelWidth && y > kTimeRulerHeight) {
        int layerIdx = -1;
        HitZone zone = hitTest(x, y, &layerIdx);

        if (zone == eHitNone) {
            // Clicked empty space in bar area — move playhead only
            _selectedLayer = -1;
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
        _selectedLayer = layerIdx;
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
        int layerIdx = yToLayer(y);
        if (layerIdx >= 0) {
            Q_EMIT layerDoubleClicked(layerIdx);
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
    if (y > kTimeRulerHeight) {
        hitTest(x, y, &layerIdx);
        if (layerIdx < 0 && x < _layerLabelWidth) {
            layerIdx = yToLayer(y);
        }
    }

    // -- "Add Layer" submenu (shown when right-clicking empty space or in bar area) --
    if (y > kTimeRulerHeight) {
        QMenu* addMenu = menu.addMenu(QString::fromUtf8("Add Layer"));

        QAction* solidAction = addMenu->addAction(QString::fromUtf8("Solid"));
        connect(solidAction, &QAction::triggered, this, [this]() {
            addSolidLayer();
        });

        QAction* nullAction = addMenu->addAction(QString::fromUtf8("Null"));
        connect(nullAction, &QAction::triggered, this, [this]() {
            FluxLayer layer;
            layer.name = QString::fromUtf8("Null");
            layer.type = QString::fromUtf8("null");
            layer.inPoint = _firstFrame;
            layer.outPoint = _lastFrame;
            layer.color = QColor(180, 180, 180);
            _layers.append(layer);
            Q_EMIT compositingChanged();
            update();
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
                _selectedLayer = layerIdx;
                Q_EMIT layerSelected(layerIdx);
                update();
                showNodeCreationDialog();
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
        if (_selectedLayer >= 0 && _selectedLayer < _layers.size()) {
            if (!_layers[_selectedLayer].locked) {
                removeLayer(_selectedLayer);
            }
        }
        event->accept();
    } else if (event->key() == Qt::Key_D && (event->modifiers() & Qt::ControlModifier) && (event->modifiers() & Qt::ShiftModifier)) {
        // Ctrl+Shift+D = Split at playhead
        if (canSplitRow(_selectedLayer)) {
            splitLayer(_selectedLayer, _currentFrame);
        }
        event->accept();
    } else if (event->key() == Qt::Key_D && (event->modifiers() & Qt::ControlModifier) && !(event->modifiers() & Qt::ShiftModifier)) {
        // Ctrl+D = Duplicate selected layer
        if (canDuplicateRow(_selectedLayer)) {
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
        _selectedLayer = -1;
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
        int adjustedY = y - kTimeRulerHeight + _scrollOffsetY;
        row = adjustedY / kLayerRowHeight;
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

    // Select the new layer
    _selectedLayer = row;
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

        _layers.append(layer);
    }

    _selectedLayer = ser.selectedLayer;

    update();
}
NATRON_NAMESPACE_EXIT

NATRON_NAMESPACE_USING
#include "moc_FluxTimeline.cpp"
