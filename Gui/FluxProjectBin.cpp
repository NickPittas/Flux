/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of Flux <https://github.com/NickPittas/Flux>,
 * based on Natron <https://natrongithub.github.io/>
 * (C) 2018-2023 The Natron developers
 * (C) 2025 Nick Pittas — Flux modifications
 *
 * Flux is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Flux is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Flux.  If not, see <http://www.gnu.org/licenses/gpl-2.0.html>
 * ***** END LICENSE BLOCK ***** */

#include "Gui/FluxProjectBin.h"

#include <QMimeData>
#include <QUrl>
#include <QFileDialog>
#include <QPixmap>
#include <QImage>
#include <QFileInfo>
#include <QPainter>
#include <QMessageBox>
#include <QDrag>
#include <QProcess>
#include <QApplication>
#include <QMouseEvent>

#include "Gui/Gui.h"
#include "Gui/GuiAppInstance.h"

NATRON_NAMESPACE_ENTER

FluxProjectBin::FluxProjectBin(Gui* gui,
                               QWidget* parent)
    : QWidget(parent)
      , PanelWidget(this, gui)
      , _thumbnailViewMode(true)
{
    setupUI();
    setAcceptDrops(true);
    setObjectName( QString::fromUtf8("FluxProjectBin") );
}

FluxProjectBin::~FluxProjectBin()
{
}

void
FluxProjectBin::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(4);

    // Header
    _headerLabel = new QLabel(QString::fromUtf8("Project Bin"));
    _headerLabel->setObjectName( QString::fromUtf8("fluxPanelHeader") );
    QFont headerFont;
    headerFont.setBold(true);
    headerFont.setPointSize(11);
    _headerLabel->setFont(headerFont);
    mainLayout->addWidget(_headerLabel);

    // Search bar + view mode toggle
    QHBoxLayout* searchLayout = new QHBoxLayout();
    _searchField = new QLineEdit();
    _searchField->setPlaceholderText(QString::fromUtf8("Search files..."));
    _searchField->setClearButtonEnabled(true);
    QObject::connect(_searchField, SIGNAL(textChanged(QString)), this, SLOT(onSearchTextChanged(QString)));
    searchLayout->addWidget(_searchField);

    _viewModeButton = new QToolButton();
    _viewModeButton->setText(QString::fromUtf8("List"));
    _viewModeButton->setToolTip(QString::fromUtf8("Toggle between thumbnail and list view"));
    _viewModeButton->setCheckable(true);
    _viewModeButton->setChecked(false);
    QObject::connect(_viewModeButton, SIGNAL(clicked(bool)), this, SLOT(onViewModeToggled()));
    searchLayout->addWidget(_viewModeButton);

    mainLayout->addLayout(searchLayout);

    // File list
    _fileList = new FluxProjectBinListWidget();
    // Drag is handled manually via mousePressEvent/mouseMoveEvent/performDrag
    // Do NOT enable QListWidget's built-in drag (it bypasses our custom MIME data)
    _fileList->setSelectionMode(QListWidget::SingleSelection);
    _fileList->setWordWrap(true);
    _fileList->setSpacing(4);
    applyViewMode();

    QObject::connect(_fileList, SIGNAL(itemDoubleClicked(QListWidgetItem*)), this, SLOT(onItemDoubleClicked(QListWidgetItem*)));
    QObject::connect(_fileList, SIGNAL(emptySpaceDoubleClicked()), this, SLOT(onImportButtonClicked()));
    mainLayout->addWidget(_fileList);

    // Buttons
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    _importButton = new QPushButton(QString::fromUtf8("Import..."));
    _importButton->setToolTip(QString::fromUtf8("Import files into the project bin"));
    QObject::connect(_importButton, SIGNAL(clicked(bool)), this, SLOT(onImportButtonClicked()));

    _clearButton = new QPushButton(QString::fromUtf8("Clear"));
    _clearButton->setToolTip(QString::fromUtf8("Remove all files from the bin"));
    QObject::connect(_clearButton, SIGNAL(clicked(bool)), this, SLOT(clearBin()));

    buttonLayout->addWidget(_importButton);
    buttonLayout->addWidget(_clearButton);
    mainLayout->addLayout(buttonLayout);

    setLayout(mainLayout);
}

void
FluxProjectBin::applyViewMode()
{
    if (_thumbnailViewMode) {
        _fileList->setViewMode(QListWidget::IconMode);
        _fileList->setIconSize(QSize(80, 60));
        _fileList->setGridSize(QSize(90, 80));
        _fileList->setResizeMode(QListWidget::Adjust);
        _fileList->setMovement(QListWidget::Static);
    } else {
        _fileList->setViewMode(QListWidget::ListMode);
        _fileList->setIconSize(QSize(32, 24));
        _fileList->setGridSize(QSize());
        _fileList->setResizeMode(QListWidget::Fixed);
        _fileList->setMovement(QListWidget::Static);
    }
}

void
FluxProjectBin::onViewModeToggled()
{
    _thumbnailViewMode = !_viewModeButton->isChecked();
    _viewModeButton->setText(_thumbnailViewMode ? QString::fromUtf8("List") : QString::fromUtf8("Grid"));
    applyViewMode();

    // Rebuild items to update display text
    for (int i = 0; i < _fileList->count(); ++i) {
        QListWidgetItem* item = _fileList->item(i);
        QString filePath = item->data(Qt::UserRole).toString();
        QFileInfo fi(filePath);

        if (!_thumbnailViewMode) {
            // List mode: show filename, file type suffix
            QString ext = fi.suffix().toUpper();
            item->setText(fi.fileName() + QString::fromUtf8("  [") + ext + QString::fromUtf8("]"));
        } else {
            // Thumbnail mode: just filename
            item->setText(fi.fileName());
        }

        // Restore icon from cache
        QMap<QString, QPixmap>::iterator cacheIt = _thumbnailCache.find(filePath);
        if (cacheIt != _thumbnailCache.end()) {
            if (_thumbnailViewMode) {
                item->setIcon(QIcon(cacheIt.value()));
            } else {
                QPixmap smallThumb = cacheIt.value().scaled(32, 24, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                item->setIcon(QIcon(smallThumb));
            }
        }
    }
}

void
FluxProjectBin::addFile(const QString& filePath)
{
    if (_files.contains(filePath)) {
        return;
    }
    _files.append(filePath);

    // Generate and cache thumbnail (80x60)
    QPixmap thumb = generateThumbnail(filePath);
    if (!thumb.isNull()) {
        _thumbnailCache[filePath] = thumb;
    }

    QListWidgetItem* item = createItem(filePath);
    _fileList->addItem(item);
}

QStringList
FluxProjectBin::getFiles() const
{
    return _files;
}

void
FluxProjectBin::clearBin()
{
    _fileList->clear();
    _files.clear();
    _thumbnailCache.clear();
}

QListWidgetItem*
FluxProjectBin::createItem(const QString& filePath)
{
    QFileInfo fi(filePath);
    QListWidgetItem* item = new QListWidgetItem();

    // Use cached thumbnail if available
    QMap<QString, QPixmap>::iterator cacheIt = _thumbnailCache.find(filePath);
    if (cacheIt != _thumbnailCache.end()) {
        if (_thumbnailViewMode) {
            item->setIcon(QIcon(cacheIt.value()));
        } else {
            QPixmap smallThumb = cacheIt.value().scaled(32, 24, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            item->setIcon(QIcon(smallThumb));
        }
    } else if (!_thumbnailCache.contains(filePath)) {
        // No cached thumbnail, try to generate
        QPixmap thumb = generateThumbnail(filePath);
        if (!thumb.isNull()) {
            _thumbnailCache[filePath] = thumb;
            item->setIcon(QIcon(thumb));
        } else {
            item->setIcon(QIcon::fromTheme(QString::fromUtf8("document-open"), QIcon()));
        }
    }

    // Show just the filename in thumbnail mode, or detailed info in list mode
    if (!_thumbnailViewMode) {
        QString ext = fi.suffix().toUpper();
        item->setText(fi.fileName() + QString::fromUtf8("  [") + ext + QString::fromUtf8("]"));
    } else {
        item->setText(fi.fileName());
    }

    item->setToolTip(filePath);
    item->setData(Qt::UserRole, filePath);

    return item;
}

void
FluxProjectBin::onImportButtonClicked()
{
    QStringList files = QFileDialog::getOpenFileNames(
        this,
        QString::fromUtf8("Import Files"),
        QString(),
        QString::fromUtf8("Media Files (*.mov *.mp4 *.mxf *.avi *.exr *.tif *.tiff *.png *.jpg *.jpeg *.psd *.dpx);;All Files (*)")
    );

    for (const QString& file : files) {
        addFile(file);
    }
}

void
FluxProjectBin::onItemDoubleClicked(QListWidgetItem* item)
{
    if (item) {
        QString filePath = item->data(Qt::UserRole).toString();
        Q_EMIT fileRequested(filePath);
    }
}

void
FluxProjectBin::onSearchTextChanged(const QString& text)
{
    for (int i = 0; i < _fileList->count(); ++i) {
        QListWidgetItem* item = _fileList->item(i);
        QString name = item->text();
        QString path = item->data(Qt::UserRole).toString();
        bool match = text.isEmpty() ||
                     name.contains(text, Qt::CaseInsensitive) ||
                     path.contains(text, Qt::CaseInsensitive);
        item->setHidden(!match);
    }
}

void
FluxProjectBin::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void
FluxProjectBin::dropEvent(QDropEvent* event)
{
    QStringList droppedFiles;
    const QList<QUrl> urls = event->mimeData()->urls();
    for (const QUrl& url : urls) {
        if (url.isLocalFile()) {
            QString filePath = url.toLocalFile();
            addFile(filePath);
            droppedFiles.append(filePath);
        }
    }
    if (!droppedFiles.isEmpty()) {
        Q_EMIT filesDropped(droppedFiles);
    }
    event->acceptProposedAction();
}

void
FluxProjectBinListWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        _dragStartPos = event->pos();
        _isDragging = false;
    }
    QListWidget::mousePressEvent(event);
}

void
FluxProjectBinListWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (event->buttons() & Qt::LeftButton && !_isDragging) {
        int distance = (event->pos() - _dragStartPos).manhattanLength();
        if (distance > QApplication::startDragDistance()) {
            _isDragging = true;
            performDrag();
            _isDragging = false;
            return;
        }
    }
    QListWidget::mouseMoveEvent(event);
}

void
FluxProjectBinListWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (!itemAt(event->pos())) {
        // Double-click on empty space: emit our signal and consume the event
        // so itemDoubleClicked is NOT fired and no import dialog opens
        Q_EMIT emptySpaceDoubleClicked();
        event->accept();
        return;
    }
    // Double-click on an item: let the base class handle it so
    // itemDoubleClicked(QListWidgetItem*) fires and fileRequested is emitted
    QListWidget::mouseDoubleClickEvent(event);
}

void
FluxProjectBinListWidget::performDrag()
{
    QListWidgetItem* item = currentItem();
    if (!item) {
        return;
    }

    QString filePath = item->data(Qt::UserRole).toString();
    if (filePath.isEmpty()) {
        return;
    }

    QMimeData* mimeData = new QMimeData;

    // text/uri-list with the file path
    QList<QUrl> urls;
    urls << QUrl::fromLocalFile(filePath);
    mimeData->setUrls(urls);

    // Custom mime type with the file path as plain text
    mimeData->setData(QString::fromUtf8("application/x-flux-asset"), filePath.toUtf8());

    // Also set plain text for broader compatibility
    mimeData->setText(filePath);

    QDrag* drag = new QDrag(this);
    drag->setMimeData(mimeData);

    // Create a drag pixmap from the item icon or a default
    QPixmap dragPixmap;
    if (!item->icon().isNull()) {
        dragPixmap = item->icon().pixmap(64, 48);
    }
    if (dragPixmap.isNull()) {
        // Create a default drag pixmap
        dragPixmap = QPixmap(64, 48);
        dragPixmap.fill(QColor(80, 130, 200, 180));
        QPainter painter(&dragPixmap);
        painter.setPen(Qt::white);
        QFont font;
        font.setPointSize(8);
        painter.setFont(font);
        QFileInfo fi(filePath);
        QString name = fi.fileName();
        if (name.length() > 10) {
            name = name.left(9) + QString::fromUtf8("...");
        }
        painter.drawText(dragPixmap.rect(), Qt::AlignCenter, name);
        painter.end();
    }
    drag->setPixmap(dragPixmap);
    drag->setHotSpot(QPoint(dragPixmap.width() / 2, dragPixmap.height() / 2));

    // Execute drag — must use Qt::CopyAction for cross-widget drops
    Qt::DropAction result = drag->exec(Qt::CopyAction | Qt::MoveAction, Qt::CopyAction);
    Q_UNUSED(result);
}

QPixmap
FluxProjectBin::generateThumbnail(const QString& filePath)
{
    static const QStringList videoExts = QStringList()
        << QString::fromUtf8("mov") << QString::fromUtf8("mp4")
        << QString::fromUtf8("mxf") << QString::fromUtf8("avi")
        << QString::fromUtf8("mkv") << QString::fromUtf8("webm")
        << QString::fromUtf8("m4v") << QString::fromUtf8("mpg")
        << QString::fromUtf8("mpeg") << QString::fromUtf8("wmv");

    QFileInfo fi(filePath);
    QString ext = fi.suffix().toLower();

    QPixmap result;

    if (videoExts.contains(ext)) {
        // For video files, use ffmpeg to extract the first frame as JPEG
        // Use QProcess to run: ffmpeg -i input.mov -vframes 1 -f image2pipe -vcodec mjpeg - 2>/dev/null
        QProcess ffmpeg;
        QStringList args;
        args << QString::fromUtf8("-i") << filePath
             << QString::fromUtf8("-vframes") << QString::fromUtf8("1")
             << QString::fromUtf8("-f") << QString::fromUtf8("image2pipe")
             << QString::fromUtf8("-vcodec") << QString::fromUtf8("mjpeg")
             << QString::fromUtf8("-");
        ffmpeg.start(QString::fromUtf8("ffmpeg"), args);
        if (ffmpeg.waitForStarted(2000)) {
            if (ffmpeg.waitForFinished(5000)) {
                QByteArray jpegData = ffmpeg.readAllStandardOutput();
                if (!jpegData.isEmpty()) {
                    QImage frameImage;
                    if (frameImage.loadFromData(jpegData, "JPEG")) {
                        result = QPixmap::fromImage(frameImage);
                    }
                }
            }
        }
        ffmpeg.kill();
        ffmpeg.waitForFinished(1000);

        // If ffmpeg failed, create a video placeholder thumbnail
        if (result.isNull()) {
            result = QPixmap(80, 60);
            result.fill(QColor(40, 40, 50));
            QPainter painter(&result);
            painter.setPen(QColor(200, 200, 200));
            QFont font;
            font.setPointSize(14);
            font.setBold(true);
            painter.setFont(font);
            painter.drawText(result.rect(), Qt::AlignCenter, QString::fromUtf8("🎬"));
            painter.setPen(QColor(150, 150, 160));
            font.setPointSize(7);
            font.setBold(false);
            painter.setFont(font);
            painter.drawText(result.rect(), Qt::AlignBottom | Qt::AlignHCenter, ext.toUpper());
            painter.end();
        }
    } else {
        // For image files, use QPixmap::load
        if (!result.load(filePath)) {
            // Create a placeholder for unsupported formats
            result = QPixmap(80, 60);
            result.fill(QColor(40, 40, 50));
            QPainter painter(&result);
            painter.setPen(QColor(150, 150, 160));
            QFont font;
            font.setPointSize(7);
            painter.setFont(font);
            painter.drawText(result.rect(), Qt::AlignCenter, ext.toUpper());
            painter.end();
        }
    }

    // Scale to thumbnail size
    if (!result.isNull()) {
        QPixmap scaled = result.scaled(80, 60, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        QPixmap centered(80, 60);
        centered.fill(QColor(30, 30, 38));
        QPainter painter(&centered);
        int x = (80 - scaled.width()) / 2;
        int y = (60 - scaled.height()) / 2;
        painter.drawPixmap(x, y, scaled);
        painter.end();
        result = centered;
    }

    return result;
}

NATRON_NAMESPACE_EXIT
