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

#include "Gui/Gui.h"

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
      , _draggingPlayhead(false)
      , _draggingLayer(false)
      , _dragLayerStartY(0)
      , _dragLayerIndex(-1)
{
    setObjectName( QString::fromUtf8("FluxTimeline") );
    setMinimumHeight(120);
    setMouseTracking(true);
    setFocusPolicy(Qt::ClickFocus);

    QObject::connect(_playTimer, SIGNAL(timeout()), this, SLOT(onPlayTimeout()));

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
    Q_EMIT frameChanged(_currentFrame);
    update();
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

void
FluxTimeline::mousePressEvent(QMouseEvent* event)
{
    int x = event->pos().x();
    int y = event->pos().y();

    // Click on time ruler or playhead area → drag playhead
    if (y < kTimeRulerHeight && x > kLayerLabelWidth) {
        _draggingPlayhead = true;
        _currentFrame = xToFrame(x);
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

    // Click on layer bar area → select layer + move playhead
    if (x > kLayerLabelWidth && y > kTimeRulerHeight) {
        int layerIdx = yToLayer(y);
        if (layerIdx >= 0) {
            _selectedLayer = layerIdx;
            Q_EMIT layerSelected(layerIdx);
        }
        _currentFrame = xToFrame(x);
        Q_EMIT frameChanged(_currentFrame);
        update();
    }
}

void
FluxTimeline::mouseMoveEvent(QMouseEvent* event)
{
    if (_draggingPlayhead) {
        int x = event->pos().x();
        _currentFrame = qBound(_firstFrame, xToFrame(x), _lastFrame);
        Q_EMIT frameChanged(_currentFrame);
        update();
    }
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

NATRON_NAMESPACE_EXIT
