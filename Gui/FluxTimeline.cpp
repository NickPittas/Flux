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
#include <QDrag>

#include "Gui/Gui.h"
#include "Gui/GuiAppInstance.h"
#include "Engine/Project.h"
#include "Engine/AppInstance.h"

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
      , _playTimer(new QTimer(this))
      , _playing(false)
      , _interactionMode(eModeNone)
      , _interactionLayerIndex(-1)
      , _interactionStartX(0)
      , _interactionStartY(0)
      , _interactionOrigInPoint(0)
      , _interactionOrigOutPoint(0)
      , _reorderTargetRow(-1)
      , _isDragOver(false)
      , _dragPreviewPos()
      , _timeline()
{
    setObjectName( QString::fromUtf8("FluxTimeline") );
    setMinimumHeight(120);
    setMouseTracking(true);
    setFocusPolicy(Qt::ClickFocus);
    setAcceptDrops(true);

    QObject::connect(_playTimer, SIGNAL(timeout()), this, SLOT(onPlayTimeout()));

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
FluxTimeline::removeLayer(int index)
{
    if (index >= 0 && index < _layers.size()) {
        _layers.removeAt(index);
        if (_selectedLayer == index) {
            _selectedLayer = -1;
        } else if (_selectedLayer > index) {
            --_selectedLayer;
        }
        Q_EMIT compositingChanged();
        update();
    }
}

void
FluxTimeline::moveLayer(int from,
                        int to)
{
    if (from >= 0 && from < _layers.size() &&
        to >= 0 && to < _layers.size()) {
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

void
FluxTimeline::play()
{
    if (!_playing) {
        _playing = true;
        _playTimer->start(41); // ~24fps
    }
}

void
FluxTimeline::stop()
{
    _playing = false;
    _playTimer->stop();
}

void
FluxTimeline::onPlayTimeout()
{
    ++_currentFrame;
    if (_currentFrame > _lastFrame) {
        _currentFrame = _firstFrame;
    }
    // T019-C: Sync with shared timeline
    if (_timeline) {
        _timeline->seekFrame(SequenceTime(_currentFrame), false, nullptr, eTimelineChangeReasonPlaybackSeek);
    }
    Q_EMIT frameChanged(_currentFrame);
    update();
}

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
    QRect rulerRect(kLayerLabelWidth, 0, totalRect.width() - kLayerLabelWidth, kTimeRulerHeight);
    drawTimeRuler(painter, rulerRect);

    // Layer label column background
    painter.fillRect(0, kTimeRulerHeight, kLayerLabelWidth, totalRect.height() - kTimeRulerHeight, QColor(38, 38, 42));

    // Layer bars area
    QRect barsRect(kLayerLabelWidth, kTimeRulerHeight, totalRect.width() - kLayerLabelWidth, totalRect.height() - kTimeRulerHeight);
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
    painter.drawLine(kLayerLabelWidth, 0, kLayerLabelWidth, totalRect.height());
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

        // Layer label (left column)
        QRect labelRect(0, y, kLayerLabelWidth - 4, kLayerRowHeight);
        bool isSelected = (i == _selectedLayer);

        if (isSelected) {
            painter.fillRect(labelRect, QColor(66, 133, 244));
            painter.setPen(Qt::white);
        } else {
            painter.fillRect(labelRect, QColor(45, 45, 50));
            painter.setPen(QColor(200, 200, 210));
        }

        QFont font;
        font.setPointSize(9);
        painter.setFont(font);
        QString displayName = layer.name;
        if (displayName.length() > 18) {
            displayName = displayName.left(15) + QString::fromUtf8("...");
        }
        painter.drawText(labelRect.adjusted(6, 0, -4, 0), Qt::AlignVCenter | Qt::AlignLeft, displayName);

        // Visibility indicator
        QString visStr = layer.visible ? QString::fromUtf8("V") : QString::fromUtf8("-");
        painter.drawText(labelRect.adjusted(0, 0, -6, 0), Qt::AlignVCenter | Qt::AlignRight, visStr);

        // Layer bar (right area)
        int barX1 = frameToX(layer.inPoint);
        int barX2 = frameToX(layer.outPoint);
        QRect barRect(barX1, y + 4, barX2 - barX1, kLayerRowHeight - 8);

        if (barRect.width() > 0) {
            // Bar fill
            QColor barColor = layer.color;
            if (!layer.visible) {
                barColor = barColor.darker(200);
            }
            painter.fillRect(barRect, barColor);

            // Bar border
            painter.setPen(barColor.darker(130));
            painter.drawRect(barRect);

            // Trim handle highlights
            if (barRect.width() > kTrimHandleWidth * 2) {
                // Left trim handle zone
                QRect leftHandle(barRect.left(), barRect.top(), kTrimHandleWidth, barRect.height());
                painter.fillRect(leftHandle, QColor(255, 255, 255, 30));

                // Right trim handle zone
                QRect rightHandle(barRect.right() - kTrimHandleWidth, barRect.top(), kTrimHandleWidth, barRect.height());
                painter.fillRect(rightHandle, QColor(255, 255, 255, 30));
            }

            // Bar text (filename)
            if (barRect.width() > 40) {
                painter.setPen(Qt::white);
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
    if (x < kLayerLabelWidth) {
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
    if (x <= kLayerLabelWidth || y <= kTimeRulerHeight) {
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
    return kLayerLabelWidth + (int)( (frame - _firstFrame) * _zoom ) - _scrollOffsetX;
}

int
FluxTimeline::xToFrame(int x) const
{
    int localX = x - kLayerLabelWidth + _scrollOffsetX;
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
    int availableWidth = width() - kLayerLabelWidth;
    if (availableWidth > 0) {
        _zoom = (double)availableWidth / frameRange;
    }
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
    if (x <= kLayerLabelWidth || y <= kTimeRulerHeight) {
        return eHitNone;
    }

    int layerIdx = yToLayer(y);
    if (layerIdx < 0 || layerIdx >= _layers.size()) {
        return eHitNone;
    }

    const FluxLayer& layer = _layers[layerIdx];
    int barX1 = frameToX(layer.inPoint);
    int barX2 = frameToX(layer.outPoint);

    // Check if within the bar's horizontal extent
    if (x < barX1 || x > barX2) {
        return eHitNone;
    }

    if (outLayerIndex) {
        *outLayerIndex = layerIdx;
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
    if (event->button() != Qt::LeftButton) {
        return;
    }

    int x = event->pos().x();
    int y = event->pos().y();

    // Click on time ruler area → drag playhead
    if (y < kTimeRulerHeight && x > kLayerLabelWidth) {
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

    // Click on layer label → select layer
    if (x < kLayerLabelWidth && y > kTimeRulerHeight) {
        int layerIdx = yToLayer(y);
        if (layerIdx >= 0) {
            _selectedLayer = layerIdx;
            Q_EMIT layerSelected(layerIdx);
            update();
        }
        return;
    }

    // Click on layer bar area → hit-test to determine what was clicked
    if (x > kLayerLabelWidth && y > kTimeRulerHeight) {
        int layerIdx = -1;
        HitZone zone = hitTest(x, y, &layerIdx);

        if (zone == eHitNone) {
            // Clicked empty space in bar area — move playhead only
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
        _reorderTargetRow = layerIdx;

        if (zone == eHitTrimLeft) {
            _interactionMode = eModeTrimLeft;
        } else if (zone == eHitTrimRight) {
            _interactionMode = eModeTrimRight;
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

        // Move bar horizontally: shift both inPoint and outPoint by the frame delta
        int frameDelta = (int)( deltaX / _zoom );
        int newIn = _interactionOrigInPoint + frameDelta;
        int newOut = _interactionOrigOutPoint + frameDelta;

        if (_interactionLayerIndex >= 0 && _interactionLayerIndex < _layers.size()) {
            _layers[_interactionLayerIndex].inPoint = newIn;
            _layers[_interactionLayerIndex].outPoint = newOut;
        }
        update();
        break;
    }

    case eModeTrimLeft: {
        if (_interactionLayerIndex < 0 || _interactionLayerIndex >= _layers.size()) {
            break;
        }
        int deltaX = x - _interactionStartX;
        int frameDelta = (int)( deltaX / _zoom );
        int newIn = _interactionOrigInPoint + frameDelta;
        // Clamp: inPoint must stay before outPoint and within range
        newIn = qMin(newIn, _interactionOrigOutPoint - 1);
        _layers[_interactionLayerIndex].inPoint = newIn;
        update();
        break;
    }

    case eModeTrimRight: {
        if (_interactionLayerIndex < 0 || _interactionLayerIndex >= _layers.size()) {
            break;
        }
        int deltaX = x - _interactionStartX;
        int frameDelta = (int)( deltaX / _zoom );
        int newOut = _interactionOrigOutPoint + frameDelta;
        // Clamp: outPoint must stay after inPoint
        newOut = qMax(newOut, _interactionOrigInPoint + 1);
        _layers[_interactionLayerIndex].outPoint = newOut;
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

    case eModeNone:
    default: {
        // No active interaction — update cursor based on hover position
        int layerIdx = -1;
        HitZone zone = hitTest(x, y, &layerIdx);
        if (zone == eHitTrimLeft || zone == eHitTrimRight) {
            setCursor(Qt::SplitHCursor);
        } else if (zone == eHitBarBody) {
            setCursor(Qt::OpenHandCursor);
        } else if (y < kTimeRulerHeight && x > kLayerLabelWidth) {
            setCursor(Qt::PointingHandCursor);
        } else {
            unsetCursor();
        }
        break;
    }

    }
}

void
FluxTimeline::mouseReleaseEvent(QMouseEvent* /*event*/)
{
    if (_interactionMode == eModeMoveBar || _interactionMode == eModeTrimLeft || _interactionMode == eModeTrimRight) {
        // Emit compositingChanged for bar moves and trims (reorder already emits during drag)
        Q_EMIT compositingChanged();
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
FluxTimeline::wheelEvent(QWheelEvent* event)
{
    QPoint delta = event->angleDelta();

    if (delta.y() != 0) {
        // Zoom horizontally
        double factor = delta.y() > 0 ? 1.15 : 1.0 / 1.15;
        _zoom *= factor;
        _zoom = qBound(1.0, _zoom, 100.0);
        update();
        event->accept();
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
    if (x > kLayerLabelWidth) {
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

    // Emit signal so Gui can create a reader node
    Q_EMIT layerAddedFromDrop(filePath, row, inFrame);

    Q_EMIT compositingChanged();

    update();
    event->acceptProposedAction();
}

void
FluxTimeline::setLayerReaderNode(int layerIndex,
                                 const NodePtr& node)
{
    if (layerIndex >= 0 && layerIndex < _layers.size()) {
        _layers[layerIndex].readerNode = node;
    }
}

NATRON_NAMESPACE_EXIT
