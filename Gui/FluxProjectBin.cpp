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
#include <QFileInfo>
#include <QPainter>
#include <QMessageBox>
#include <QDrag>

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
    _fileList->setDragEnabled(true);
    _fileList->setSelectionMode(QListWidget::SingleSelection);
    _fileList->setWordWrap(true);
    _fileList->setSpacing(4);
    applyViewMode();

    QObject::connect(_fileList, SIGNAL(itemDoubleClicked(QListWidgetItem*)), this, SLOT(onItemDoubleClicked(QListWidgetItem*)));
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
    QPixmap thumb;
    if (thumb.load(filePath)) {
        QPixmap scaled = thumb.scaled(80, 60, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        QPixmap centered(80, 60);
        centered.fill(Qt::darkGray);
        QPainter painter(&centered);
        int x = (80 - scaled.width()) / 2;
        int y = (60 - scaled.height()) / 2;
        painter.drawPixmap(x, y, scaled);
        painter.end();
        _thumbnailCache[filePath] = centered;
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
        // No cached thumbnail, try to load
        QPixmap thumb;
        if (thumb.load(filePath)) {
            QPixmap scaled = thumb.scaled(80, 60, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            QPixmap centered(80, 60);
            centered.fill(Qt::darkGray);
            QPainter painter(&centered);
            int x = (80 - scaled.width()) / 2;
            int y = (60 - scaled.height()) / 2;
            painter.drawPixmap(x, y, scaled);
            painter.end();
            _thumbnailCache[filePath] = centered;
            item->setIcon(QIcon(centered));
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
        Q_EMIT fileRequested(file);
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
FluxProjectBinListWidget::startDrag(Qt::DropActions supportedActions)
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

    QDrag* drag = new QDrag(this);
    drag->setMimeData(mimeData);

    // Create a drag pixmap from the item icon
    QPixmap dragPixmap;
    if (!item->icon().isNull()) {
        dragPixmap = item->icon().pixmap(64, 48);
    }
    if (!dragPixmap.isNull()) {
        drag->setPixmap(dragPixmap);
    }

    drag->exec(supportedActions, Qt::CopyAction);
}

NATRON_NAMESPACE_EXIT
