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

CLANG_DIAG_OFF(deprecated)
CLANG_DIAG_OFF(uninitialized)
#include <QWidget>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QTimer>
#include <QStringList>
#include <QList>
CLANG_DIAG_ON(deprecated)
CLANG_DIAG_ON(uninitialized)

#include "Gui/PanelWidget.h"

NATRON_NAMESPACE_ENTER

struct FluxLayer {
    QString name;
    QString filePath;    // empty for solid/adjustment/null layers
    QString type;        // "footage", "solid", "adjustment", "null"
    bool visible;
    bool locked;
    bool solo;
    int inPoint;         // frame number
    int outPoint;        // frame number
    QColor color;        // layer bar color

    FluxLayer()
        : type(QString::fromUtf8("footage"))
        , visible(true)
        , locked(false)
        , solo(false)
        , inPoint(0)
        , outPoint(100)
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

    /** @brief Start playback. */
    void play();

    /** @brief Stop playback. */
    void stop();

Q_SIGNALS:

    /** @brief Emitted when the playhead moves. */
    void frameChanged(int frame);

    /** @brief Emitted when a layer is selected. */
    void layerSelected(int index);

    /** @brief Emitted when a layer is double-clicked (open properties). */
    void layerDoubleClicked(int index);

    /** @brief Emitted when layers are reordered. */
    void layersReordered();

public Q_SLOTS:

    void onPlayTimeout();

protected:

    virtual void paintEvent(QPaintEvent* event) OVERRIDE;
    virtual void mousePressEvent(QMouseEvent* event) OVERRIDE;
    virtual void mouseMoveEvent(QMouseEvent* event) OVERRIDE;
    virtual void mouseDoubleClickEvent(QMouseEvent* event) OVERRIDE;
    virtual void wheelEvent(QWheelEvent* event) OVERRIDE;
    virtual void resizeEvent(QResizeEvent* event) OVERRIDE;

private:

    void drawTimeRuler(QPainter& painter, const QRect& rect);
    void drawLayerBars(QPainter& painter, const QRect& rect);
    void drawPlayhead(QPainter& painter, const QRect& rect);
    int frameToX(int frame) const;
    int xToFrame(int x) const;
    int yToLayer(int y) const;
    void updateZoom();

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

    // Playback
    QTimer* _playTimer;
    bool _playing;

    // Interaction
    bool _draggingPlayhead;
    bool _draggingLayer;
    int _dragLayerStartY;
    int _dragLayerIndex;
};

NATRON_NAMESPACE_EXIT

#endif // FLUX_TIMELINE_H
