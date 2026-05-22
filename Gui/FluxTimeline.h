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

NATRON_NAMESPACE_ENTER

struct FluxLayer {
    QString name;
    QString filePath;    // empty for solid/adjustment/null layers
    QString type;        // "footage", "solid", "adjustment", "null"
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
    QColor color;        // layer bar color

    QString readerNodeId; // Natron node ID for the reader (set after node creation)
    NodePtr readerNode;   // Actual Natron node pointer (set after node creation)

    // Gizmo: FluxLayer PyPlug wrapping FrameRange -> TimeOffset -> Transform
    NodePtr gizmoNode;       // flux.layer gizmo (one per layer)
    NodePtr mergeNode;       // net.sf.openfx.MergePlugin (compositing, outside gizmo)

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
        , color(QColor(80, 130, 200))
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

    /** @brief Remove a layer by index. */
    void removeLayer(int index);

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

    /** @brief Set the reader NodePtr for a layer (called after node creation). */
    void setLayerReaderNode(int index, const NodePtr& node);

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

public Q_SLOTS:

    /** @brief Responds to external TimeLine frame changes (from Viewer playback, etc.). */
    void onExternalFrameChanged(SequenceTime time, int reason);

protected:

    virtual void paintEvent(QPaintEvent* event) OVERRIDE;
    virtual void mousePressEvent(QMouseEvent* event) OVERRIDE;
    virtual void mouseMoveEvent(QMouseEvent* event) OVERRIDE;
    virtual void mouseReleaseEvent(QMouseEvent* event) OVERRIDE;
    virtual void mouseDoubleClickEvent(QMouseEvent* event) OVERRIDE;
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

    /** @brief Determine what's under the mouse: nothing, bar body, left trim handle, right trim handle */
    enum HitZone { eHitNone, eHitBarBody, eHitTrimLeft, eHitTrimRight };
    HitZone hitTest(int x, int y, int* outLayerIndex = nullptr) const;

    QList<FluxLayer> _layers;
    int _firstFrame;
    int _lastFrame;
    int _currentFrame;
    int _selectedLayer;
    double _zoom;         // pixels per frame
    int _scrollOffsetX;
    int _scrollOffsetY;

    // Layout constants
    static const int kTimeRulerHeight = 28;
    static const int kLayerLabelWidth = 140;
    static const int kLayerRowHeight = 30;
    static const int kPlayheadWidth = 2;

    static const int kTrimHandleWidth = 6; // pixels for trim handles at bar edges

    // Interaction modes
    enum InteractionMode {
        eModeNone,           // no interaction
        eModeDragPlayhead,   // dragging the playhead
        eModeMoveBar,        // dragging a layer bar horizontally
        eModeTrimLeft,       // trimming the left (in) edge of a bar
        eModeTrimRight,      // trimming the right (out) edge of a bar
        eModeReorderLayer    // dragging a layer up/down to reorder
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
