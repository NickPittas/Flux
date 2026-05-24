/* ***** BEGIN LICENSE BLOCK *****
 * Flux — Layer-based Timeline Widget
 * (C) 2025 Nick Pittas
 * GPL2 — see LICENSE.txt
 * ***** END LICENSE BLOCK ***** */

#ifndef FLUX_TIMELINE_H
#define FLUX_TIMELINE_H

// ***** BEGIN PYTHON BLOCK *****
#include <Python.h>
// ***** END PYTHON BLOCK *****

#include "Global/Macros.h"

#include "Engine/Node.h"
#include "Engine/NodeGroup.h"
#include "Engine/Plugin.h"
#include "Engine/Project.h"
#include "Engine/AppInstance.h"

CLANG_DIAG_OFF(deprecated)
CLANG_DIAG_OFF(uninitialized)
#include <QWidget>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QStringList>
#include <QList>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QPoint>
#include <QContextMenuEvent>
CLANG_DIAG_ON(deprecated)
CLANG_DIAG_ON(uninitialized)

#include "Gui/PanelWidget.h"

#include "Engine/TimeLine.h"

#include "Gui/FluxTimelineSerialization.h"

NATRON_NAMESPACE_ENTER

enum FluxVisibleRowType {
    eFluxVisibleRowLayer,
    eFluxVisibleRowEffect,
    eFluxVisibleRowMask
};

enum FluxSelectionType {
    eFluxSelectionNone,
    eFluxSelectionLayer,
    eFluxSelectionEffect,
    eFluxSelectionMask
};

struct FluxVisibleRow {
    FluxVisibleRowType type;
    int layerIndex;
    int childIndex;
    int effectIndex;
    int maskIndex;
    int y;
    int height;

    FluxVisibleRow()
        : type(eFluxVisibleRowLayer)
        , layerIndex(-1)
        , childIndex(-1)
        , effectIndex(-1)
        , maskIndex(-1)
        , y(0)
        , height(0)
    {}
};

struct FluxEffect {
    QString pluginId;
    QString label;
    NodePtr node;
    bool enabled;

    FluxEffect()
        : enabled(true)
    {}
};

struct FluxMask {
    QString name;
    QString type;        // "layer" for T057; future: "effect"
    QString pluginId;    // Roto/RotoPaint plugin id, informational/persistence aid
    bool enabled;
    bool inverted;
    int effectIndex;     // -1 = layer mask; >=0 future effect-mask owner

    NodePtr maskNode;    // Roto/RotoPaint node
    NodePtr reformatNode;// future mask branch source

    FluxMask()
        : type(QString::fromUtf8("layer"))
        , enabled(true)
        , inverted(false)
        , effectIndex(-1)
    {}
};

struct FluxLayer {
    QString name;
    QString filePath;    // empty for solid/adjustment/null layers
    QString type;        // "footage", "solid", "text", "adjustment", "null"
    bool muted;          // true = merge node disabled, V button highlighted
    bool locked;
    bool solo;
    int inPoint;         // current frameRange first (source frame start, changes on trim only)
    int outPoint;        // current frameRange last (source frame end, changes on trim only)
    int originalInPoint;  // first frame of source media. NEVER changes.
    int originalOutPoint; // last frame of source media. NEVER changes.
    int originalFirstFrame;  // first frame of the source media (e.g. 1)
    int originalLastFrame;   // last frame of the source media (e.g. 225)
    int timeOffset;      // how many frames the bar moved. Changes on move only.
    int trimStart;       // frames trimmed from start of media (0 = no trim)
    int trimEnd;         // frames trimmed from end of media (0 = no trim)
    bool nodeInitialized;// true after deferredInit has successfully set frameRange/timeOffset
    QColor solidColor;   // color for solid layers (used when creating Constant node)
    int parentLayerIndex;// index of parent layer (-1 = no parent), for null/parenting
    bool expanded;       // true = child/effect rows visible below this layer
    QColor color;        // layer bar color

    QString readerNodeId; // Natron node ID for the reader (set after node creation)
    NodePtr readerNode;   // Actual Natron node pointer (set after node creation)

    // Gizmo: FluxLayer PyPlug wrapping FrameRange -> TimeOffset -> Transform
    NodePtr gizmoNode;       // flux.layer gizmo (one per layer)
    NodePtr mergeNode;       // net.sf.openfx.MergePlugin (compositing, outside gizmo)
    QList<FluxEffect> effects; // ordered child effects; adjustment rows contain only effects
    QList<FluxMask> masks;    // masks attached to this layer
    NodePtr maskApplyNode;    // node that receives mask input (future use)
    bool hasPrecompBranch;    // true if layer has a precomp branch (future use)

    FluxLayer()
        : type(QString::fromUtf8("footage"))
        , muted(false)
        , locked(false)
        , solo(false)
        , inPoint(0)
        , outPoint(100)
        , originalInPoint(0)
        , originalOutPoint(100)
        , originalFirstFrame(0)
        , originalLastFrame(100)
        , timeOffset(0)
        , trimStart(0)
        , trimEnd(0)
        , nodeInitialized(false)
        , solidColor(128, 128, 128)
        , parentLayerIndex(-1)
        , expanded(false)
        , color(QColor(80, 130, 200))
        , hasPrecompBranch(false)
    {}
};

class FluxTimeline
    : public QWidget
      , public PanelWidget
{
    GCC_DIAG_SUGGEST_OVERRIDE_OFF
    Q_OBJECT
    GCC_DIAG_SUGGEST_OVERRIDE_ON

public:

    explicit FluxTimeline(Gui* gui,
                          QWidget* parent = nullptr);

    virtual ~FluxTimeline();

    /** @brief Add a new layer from a file path. */
    void addLayer(const QString& name, const QString& filePath, const QString& type = QString::fromUtf8("footage"));

    /** @brief Add a solid color layer. */
    void addSolidLayer(const QColor& color = QColor(128, 128, 128));

    /** @brief Add a text layer. */
    void addTextLayer();

    /** @brief Remove a layer by index. */
    void removeLayer(int index);

    /** @brief Duplicate a layer using Natron's native copy/paste. */
    bool duplicateLayer(int index);

    /** @brief Split a layer at the playhead: duplicate + trim. */
    void splitLayer(int index, int frame);

    /** @brief Move a layer from one index to another. */
    void moveLayer(int from, int to);

    /** @brief Get the current list of layers. */
    const QList<FluxLayer>& getLayers() const;

    /** @brief Set the frame range for the timeline. */
    void setFrameRange(int first, int last);

    /** @brief Set the current frame (playhead position). */
    void setCurrentFrame(int frame);

    /** @brief Get the current frame. */
    int getCurrentFrame() const;

    /** @brief Get selected timeline row, or -1. */
    int getSelectedLayerIndex() const;

    /** @brief Set the reader NodePtr for a layer (called after node creation). */
    void setLayerReaderNode(int index, const NodePtr& node);

    /** @brief Export timeline state for project serialization. */
    FluxTimelineSerialization serializeForProject() const;

    /** @brief Restore timeline state from project serialization. */
    void restoreFromProjectSerialization(const FluxTimelineSerialization& ser, Gui* gui);

    /** @brief Rebuild visible rows cache, clamp scroll, and repaint. */
    void refreshVisibleRows();

    /** @brief Remove an effect from a layer. Returns true on success. */
    bool removeEffectFromLayer(int layerIndex, int effectIndex);

    /** @brief Move an effect within a layer's effect stack. Returns true on success. */
    bool moveEffectInLayer(int layerIndex, int fromEffectIndex, int toEffectIndex);

    /** @brief Add a layer-level mask model entry. Returns true on success. */
    bool addLayerMask(int layerIndex);

    /** @brief Add an effect-level mask model entry. Returns true on success. */
    bool addEffectMask(int layerIndex, int effectIndex);

    /** @brief Remove a mask from a layer. Returns true on success. */
    bool removeMaskFromLayer(int layerIndex, int maskIndex);

Q_SIGNALS:

    /** @brief Emitted when the playhead moves. */
    void frameChanged(int frame);

    /** @brief Emitted when a layer is selected. */
    void layerSelected(int index);

    /** @brief Emitted when a layer is double-clicked (open properties). */
    void layerDoubleClicked(int index);

    /** @brief Emitted when layers are reordered. */
    void layersReordered();

    /** @brief Emitted when a layer is added from a drag-drop from the Project Bin. */
    void layerAddedFromDrop(QString filePath, int row, int inFrame);

    /** @brief Emitted when the compositing graph needs rebuilding (layer added/removed/reordered/trimmed). */
    void compositingChanged();

    /** @brief Emitted when an effect sub-row is selected. */
    void effectSelected(int layerIndex, int effectIndex);

    /** @brief Emitted when effects are added/removed/reordered on a layer. */
    void effectsChanged(int layerIndex);

    /** @brief Emitted when a mask sub-row is selected. */
    void maskSelected(int layerIndex, int maskIndex);

    /** @brief Emitted when masks are added/removed on a layer. */
    void masksChanged(int layerIndex);

public Q_SLOTS:

    /** @brief Responds to external TimeLine frame changes (from Viewer playback, etc.). */
    void onExternalFrameChanged(SequenceTime time, int reason);

    /** @brief Menu/shortcut entry points for Flux layer operations. */
    void addNullLayer();
    void duplicateSelectedLayer();
    void splitSelectedLayer();
    void deleteSelectedLayer();
    void addEffectToSelectedLayer();
    void addMaskToSelectedRow();
    void addAdjustmentEffectRow();

protected:

    virtual bool event(QEvent* event) OVERRIDE;
    virtual void paintEvent(QPaintEvent* event) OVERRIDE;
    virtual void mousePressEvent(QMouseEvent* event) OVERRIDE;
    virtual void mouseMoveEvent(QMouseEvent* event) OVERRIDE;
    virtual void mouseReleaseEvent(QMouseEvent* event) OVERRIDE;
    virtual void mouseDoubleClickEvent(QMouseEvent* event) OVERRIDE;
    virtual void keyPressEvent(QKeyEvent* event) OVERRIDE;
    virtual void wheelEvent(QWheelEvent* event) OVERRIDE;
    virtual void resizeEvent(QResizeEvent* event) OVERRIDE;
    virtual void contextMenuEvent(QContextMenuEvent* event) OVERRIDE;

    // Drag and drop support
    virtual void dragEnterEvent(QDragEnterEvent* event) OVERRIDE;
    virtual void dragMoveEvent(QDragMoveEvent* event) OVERRIDE;
    virtual void dropEvent(QDropEvent* event) OVERRIDE;

    /** @brief Rebuild the compositing graph: create Merge chain and connect viewer. */
    void rebuildCompositingGraph();

    /** @brief Create the full node chain for a layer: Read -> FrameRange -> TimeOffset -> Transform -> Merge */
    void createLayerNodeChain(int layerIndex);

    /** @brief Update the FrameRange node knobs for a layer (trim). */
    void updateLayerTrimKnobs(int layerIndex);

    /** @brief Update the TimeOffset node knob for a layer (move). */
    void updateLayerMoveKnob(int layerIndex);

    /** @brief Update disable-knob keyframes for adjustment row trim. */
    void updateAdjustmentTrimKeyframes(int layerIndex);

    /** @brief Reconnect Merge.B inputs after layer reorder (no node create/destroy). */
    void reconnectMergeChain();

    /** @brief Apply solo/mute state: mute disables merge, solo reconnects viewer. */
    void applyLayerVisibility();

    // getGui() inherited from PanelWidget

private:

    void drawTimeRuler(QPainter& painter, const QRect& rect);
    void drawLayerBars(QPainter& painter, const QRect& rect);
    void drawPlayhead(QPainter& painter, const QRect& rect);
    void drawDragPreview(QPainter& painter, const QRect& rect);
    int frameToX(int frame) const;
    int xToFrame(int x) const;
    int yToLayer(int y) const;
    void updateZoom();
    void clampScrollOffsets();
    void fitToView();
    void showNodeCreationDialog();

    void rebuildVisibleRows();
    const FluxVisibleRow* yToRow(int y) const;
    int rowYForLayer(int layerIndex) const;
    int insertionLayerIndexForY(int y) const;
    int insertionYForLayerIndex(int index) const;

    /** @brief Row capability helpers for T046 */
    bool isAdjustmentRow(int index) const;
    bool isNullRow(int index) const;
    bool canTrimRow(int index) const;
    bool canHorizontallyMoveRow(int index) const;
    bool canDuplicateRow(int index) const;
    bool canSplitRow(int index) const;
    bool canAddEffectToRow(int index) const;

    /** @brief Determine what's under the mouse: nothing, bar body, left trim handle, right trim handle */
    enum HitZone { eHitNone, eHitBarBody, eHitTrimLeft, eHitTrimRight };
    HitZone hitTest(int x, int y, int* outLayerIndex = nullptr) const;

    QString makeUniqueMaskName(int layerIndex, int effectIndex) const;
    bool canAddEffectMask(int layerIndex, int effectIndex) const;

    QList<FluxLayer> _layers;
    QList<FluxVisibleRow> _visibleRows;
    int _totalContentHeight;
    int _firstFrame;
    int _lastFrame;
    int _currentFrame;
    int _selectedLayer;
    FluxSelectionType _selectedType;
    int _selectedEffectIndex;
    int _selectedMaskIndex;
    double _zoom;         // pixels per frame
    int _scrollOffsetX;
    int _scrollOffsetY;

    // Layout constants
    static const int kTimeRulerHeight = 28;
    static const int kControlColumnWidth = 56; // fixed width for L/V/S controls
    static const int kLayerRowHeight = 30;
    static const int kPlayheadWidth = 2;

    static const int kTrimHandleWidth = 6; // pixels for trim handles at bar edges
    static const int kEffectRowHeight = 24;
    static const int kMaskRowHeight = 22;
    static const int kDisclosureSize = 12;
    static const int kEffectIndent = 18;

    int _layerLabelWidth; // runtime total left-panel width (default 180)

    // Interaction modes
    enum InteractionMode {
        eModeNone,           // no interaction
        eModeDragPlayhead,   // dragging the playhead
        eModeMoveBar,        // dragging a layer bar horizontally
        eModeTrimLeft,       // trimming the left (in) edge of a bar
        eModeTrimRight,      // trimming the right (out) edge of a bar
        eModeReorderLayer,   // dragging a layer up/down to reorder
        eModePan,            // middle-mouse or Alt+Left pan (both axes)
        eModeResizePanel     // resizing the left label panel
    };

    InteractionMode _interactionMode;
    int _interactionLayerIndex; // which layer is being interacted with
    int _interactionStartX;    // mouse X at interaction start
    int _interactionStartY;    // mouse Y at interaction start
    int _interactionOrigInPoint;  // original inPoint at drag start
    int _interactionOrigOutPoint; // original outPoint at drag start
    int _interactionOrigTimeOffset; // original timeOffset at drag start
    int _interactionOrigTrimStart; // original trimStart at drag start
    int _interactionOrigTrimEnd;   // original trimEnd at drag start
    int _reorderTargetRow;     // target row for layer reordering

    // Middle-mouse pan state
    int _panStartScrollX;      // _scrollOffsetX at pan start
    int _panStartScrollY;      // _scrollOffsetY at pan start

    // Panel resize state
    int _resizeStartX;         // mouse X at resize start
    int _resizeStartWidth;     // _layerLabelWidth at resize start

    // Drag and drop state
    bool _isDragOver;
    QPoint _dragPreviewPos;

    // Shared app timeline for playhead sync
    TimeLinePtr _timeline;

    // Compositing graph
    QList<NodePtr> _mergeNodes; // Merge nodes in the chain
};

NATRON_NAMESPACE_EXIT

#endif // FLUX_TIMELINE_H
