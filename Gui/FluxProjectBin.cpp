/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of Flux <https://github.com/NickPittas/Flux>,
 * based on Natron <https://natrongithub.github.io/>
 * (C) 2018-2023 The Natron developers
 * (C) 2025 Nick Pittas — Flux modifications
 * ***** END LICENSE BLOCK ***** */

#include "Gui/FluxProjectBin.h"

#include <QApplication>
#include <QDesktopServices>
#include <QDrag>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QImage>
#include <QImageReader>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QProcess>
#include <QRegularExpression>
#include <QUrl>

#include "Gui/Gui.h"
#include "Gui/GuiAppInstance.h"

NATRON_NAMESPACE_ENTER

namespace {

const int kFluxProjectBinPathRole = Qt::UserRole;
const int kFluxProjectBinDisplayNameRole = Qt::UserRole + 1;

bool
fluxProjectBinIsVideoExtension(const QString& extension)
{
    const QString ext = extension.toLower();
    return ext == QString::fromUtf8("mov") || ext == QString::fromUtf8("mp4") ||
           ext == QString::fromUtf8("mxf") || ext == QString::fromUtf8("avi") ||
           ext == QString::fromUtf8("mkv") || ext == QString::fromUtf8("webm") ||
           ext == QString::fromUtf8("m4v") || ext == QString::fromUtf8("mpg") ||
           ext == QString::fromUtf8("mpeg") || ext == QString::fromUtf8("wmv");
}

QString
fluxProjectBinSecondsToTimecode(double seconds, double fps)
{
    if (seconds <= 0.) {
        return QString();
    }
    const int frame = fps > 0. ? qRound(seconds * fps) : 0;
    const int wholeSeconds = static_cast<int>(seconds);
    const int h = wholeSeconds / 3600;
    const int m = (wholeSeconds / 60) % 60;
    const int s = wholeSeconds % 60;
    if (frame > 0 && fps > 0.) {
        const int f = frame % qMax(1, qRound(fps));
        return QString::fromUtf8("%1:%2:%3:%4")
            .arg(h, 2, 10, QLatin1Char('0'))
            .arg(m, 2, 10, QLatin1Char('0'))
            .arg(s, 2, 10, QLatin1Char('0'))
            .arg(f, 2, 10, QLatin1Char('0'));
    }
    return QString::fromUtf8("%1:%2:%3")
        .arg(h, 2, 10, QLatin1Char('0'))
        .arg(m, 2, 10, QLatin1Char('0'))
        .arg(s, 2, 10, QLatin1Char('0'));
}

bool
fluxProjectBinParseFps(const QString& ratio, double* fps)
{
    if (!fps) {
        return false;
    }
    const QString trimmed = ratio.trimmed();
    if (trimmed.isEmpty() || trimmed == QString::fromUtf8("0/0")) {
        return false;
    }
    const QStringList parts = trimmed.split(QLatin1Char('/'));
    bool ok = false;
    if (parts.size() == 2) {
        const double num = parts[0].toDouble(&ok);
        if (!ok) {
            return false;
        }
        const double den = parts[1].toDouble(&ok);
        if (!ok || den == 0.) {
            return false;
        }
        *fps = num / den;
        return *fps > 0.;
    }
    const double value = trimmed.toDouble(&ok);
    if (ok && value > 0.) {
        *fps = value;
        return true;
    }
    return false;
}

QString
fluxProjectBinFormatFps(double fps)
{
    if (fps <= 0.) {
        return QString();
    }
    return QString::number(fps, 'f', 3).remove(QRegularExpression(QString::fromUtf8("0+$"))).remove(QRegularExpression(QString::fromUtf8("\\.$")));
}

} // namespace

FluxProjectBinTreeWidget::FluxProjectBinTreeWidget(QWidget* parent)
    : QTreeWidget(parent)
    , _dragStartPos()
    , _isDragging(false)
{
}

FluxProjectBin::FluxProjectBin(Gui* gui, QWidget* parent)
    : QWidget(parent)
      , PanelWidget(this, gui)
      , _thumbnailViewMode(true)
{
    setupUI();
    setAcceptDrops(true);
    setObjectName(QString::fromUtf8("FluxProjectBin"));
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

    QHBoxLayout* headerLayout = new QHBoxLayout();
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(4);

    _headerLabel = new QLabel(QString::fromUtf8("Project Bin"));
    _headerLabel->setObjectName(QString::fromUtf8("fluxPanelHeader"));
    QFont headerFont;
    headerFont.setBold(true);
    headerFont.setPointSize(11);
    _headerLabel->setFont(headerFont);
    headerLayout->addWidget(_headerLabel);

    headerLayout->addStretch();

    _gridViewButton = new QToolButton();
    _gridViewButton->setObjectName(QString::fromUtf8("FluxProjectBinGridViewButton"));
    _gridViewButton->setToolTip(QString::fromUtf8("Grid View"));
    _gridViewButton->setCheckable(true);
    _gridViewButton->setChecked(true);
    _gridViewButton->setIcon(QIcon::fromTheme(QString::fromUtf8("view-grid"), QIcon(QString::fromUtf8(":/Resources/Images/layout.png"))));
    _gridViewButton->setText(QString::fromUtf8("⊞"));
    _gridViewButton->setProperty("fluxPanelHeaderButton", true);
    _gridViewButton->setProperty("fluxHeaderMicroAction", true);
    QObject::connect(_gridViewButton, SIGNAL(clicked(bool)), this, SLOT(onViewModeToggled()));
    headerLayout->addWidget(_gridViewButton);

    _listViewButton = new QToolButton();
    _listViewButton->setObjectName(QString::fromUtf8("FluxProjectBinListViewButton"));
    _listViewButton->setToolTip(QString::fromUtf8("List View"));
    _listViewButton->setCheckable(true);
    _listViewButton->setChecked(false);
    _listViewButton->setIcon(QIcon::fromTheme(QString::fromUtf8("view-list"), QIcon(QString::fromUtf8(":/Resources/Images/treeview_more.png"))));
    _listViewButton->setText(QString::fromUtf8("☰"));
    _listViewButton->setProperty("fluxPanelHeaderButton", true);
    _listViewButton->setProperty("fluxHeaderMicroAction", true);
    QObject::connect(_listViewButton, SIGNAL(clicked(bool)), this, SLOT(onViewModeToggled()));
    headerLayout->addWidget(_listViewButton);

    mainLayout->addLayout(headerLayout);

    _searchField = new QLineEdit();
    _searchField->setObjectName(QString::fromUtf8("FluxProjectBinSearchField"));
    _searchField->setPlaceholderText(QString::fromUtf8("Search files..."));
    _searchField->setClearButtonEnabled(true);
    _searchField->setProperty("fluxRecessed", true);
    QObject::connect(_searchField, SIGNAL(textChanged(QString)), this, SLOT(onSearchTextChanged(QString)));
    mainLayout->addWidget(_searchField);

    _fileList = new FluxProjectBinTreeWidget();
    _fileList->setObjectName(QString::fromUtf8("FluxProjectBinList"));
    _fileList->setAlternatingRowColors(false);
    _fileList->setSelectionMode(QAbstractItemView::SingleSelection);
    _fileList->setRootIsDecorated(false);
    _fileList->setItemsExpandable(false);
    _fileList->setAllColumnsShowFocus(true);
    _fileList->setContextMenuPolicy(Qt::CustomContextMenu);
    _fileList->setColumnCount(eColumnCount);
    _fileList->setHeaderLabels(QStringList()
                               << QString::fromUtf8("Name")
                               << QString::fromUtf8("Resolution")
                               << QString::fromUtf8("Duration")
                               << QString::fromUtf8("Start")
                               << QString::fromUtf8("End")
                               << QString::fromUtf8("FPS")
                               << QString::fromUtf8("Path"));
    _fileList->header()->setStretchLastSection(true);
    _fileList->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    _fileList->setFrameShape(QFrame::NoFrame);
    _fileList->setLineWidth(0);
    _fileList->setMidLineWidth(0);
    _fileList->setHeaderHidden(true);

    applyViewMode();

    QObject::connect(_fileList, SIGNAL(itemDoubleClicked(QTreeWidgetItem*,int)), this, SLOT(onItemDoubleClicked(QTreeWidgetItem*,int)));
    QObject::connect(_fileList, SIGNAL(emptySpaceDoubleClicked()), this, SLOT(onImportButtonClicked()));
    QObject::connect(_fileList, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(onContextMenuRequested(QPoint)));
    mainLayout->addWidget(_fileList);

    QHBoxLayout* footerLayout = new QHBoxLayout();
    footerLayout->setContentsMargins(0, 0, 0, 0);
    footerLayout->setSpacing(4);

    _statusLabel = new QLabel();
    _statusLabel->setObjectName(QString::fromUtf8("FluxProjectBinStatusLabel"));
    _statusLabel->setProperty("fluxFooterStatus", true);
    QFont statusFont = _statusLabel->font();
    statusFont.setPointSize(9);
    _statusLabel->setFont(statusFont);
    footerLayout->addWidget(_statusLabel);

    footerLayout->addStretch();

    _importButton = new QToolButton();
    _importButton->setObjectName(QString::fromUtf8("FluxProjectBinImportButton"));
    _importButton->setToolTip(QString::fromUtf8("Import files into the project bin"));
    _importButton->setIcon(QIcon::fromTheme(QString::fromUtf8("document-open"), QIcon(QString::fromUtf8(":/Resources/Images/open-file.png"))));
    _importButton->setText(QString::fromUtf8("+"));
    _importButton->setProperty("fluxPanelHeaderButton", true);
    _importButton->setProperty("fluxFooterAction", true);
    QObject::connect(_importButton, SIGNAL(clicked(bool)), this, SLOT(onImportButtonClicked()));
    footerLayout->addWidget(_importButton);

    _clearButton = new QToolButton();
    _clearButton->setObjectName(QString::fromUtf8("FluxProjectBinClearButton"));
    _clearButton->setToolTip(QString::fromUtf8("Remove all files from the bin"));
    _clearButton->setIcon(QIcon::fromTheme(QString::fromUtf8("edit-clear"), QIcon(QString::fromUtf8(":/Resources/Images/close.png"))));
    _clearButton->setText(QString::fromUtf8("Clear"));
    _clearButton->setProperty("fluxPanelHeaderButton", true);
    _clearButton->setProperty("fluxFooterAction", true);
    QObject::connect(_clearButton, SIGNAL(clicked(bool)), this, SLOT(clearBin()));
    footerLayout->addWidget(_clearButton);

    mainLayout->addLayout(footerLayout);
    setLayout(mainLayout);

    updateStatus();
}

void
FluxProjectBin::applyViewMode()
{
    _fileList->setIconSize(_thumbnailViewMode ? QSize(80, 60) : QSize(32, 24));
    _fileList->setHeaderHidden(true);
    for (int c = eColumnResolution; c < eColumnCount; ++c) {
        _fileList->setColumnHidden(c, _thumbnailViewMode);
    }
    for (int i = 0; i < _fileList->topLevelItemCount(); ++i) {
        QTreeWidgetItem* item = _fileList->topLevelItem(i);
        const QString path = itemPath(item);
        const QString name = item->data(eColumnName, kFluxProjectBinDisplayNameRole).toString();
        if (_thumbnailViewMode) {
            const FluxProjectBinMetadata metadata = _metadataCache.value(path);
            QString metaStr;
            if (!metadata.resolution.isEmpty()) {
                metaStr += metadata.resolution;
            }
            if (!metadata.duration.isEmpty()) {
                if (!metaStr.isEmpty()) metaStr += QString::fromUtf8(" | ");
                metaStr += metadata.duration;
            }
            if (!metadata.fps.isEmpty()) {
                if (!metaStr.isEmpty()) metaStr += QString::fromUtf8(" | ");
                metaStr += metadata.fps + QString::fromUtf8(" fps");
            }
            if (metaStr.isEmpty()) {
                metaStr = QString::fromUtf8("No metadata");
            }
            item->setText(eColumnName, name + QString::fromUtf8("\n") + metaStr);
        } else {
            item->setText(eColumnName, name);
        }
        QMap<QString, QPixmap>::const_iterator cacheIt = _thumbnailCache.find(path);
        if (cacheIt != _thumbnailCache.end()) {
            item->setIcon(eColumnName, QIcon(_thumbnailViewMode ? cacheIt.value() : cacheIt.value().scaled(32, 24, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
        }
    }
}

void
FluxProjectBin::onViewModeToggled()
{
    QToolButton* clickedBtn = qobject_cast<QToolButton*>(sender());
    if (clickedBtn) {
        if (clickedBtn == _gridViewButton) {
            _gridViewButton->setChecked(true);
            _listViewButton->setChecked(false);
            _thumbnailViewMode = true;
        } else if (clickedBtn == _listViewButton) {
            _gridViewButton->setChecked(false);
            _listViewButton->setChecked(true);
            _thumbnailViewMode = false;
        }
    } else {
        _gridViewButton->setChecked(_thumbnailViewMode);
        _listViewButton->setChecked(!_thumbnailViewMode);
    }
    applyViewMode();
}


void
FluxProjectBin::addFile(const QString& filePath)
{
    if (_files.contains(filePath)) {
        return;
    }
    _files.append(filePath);
    const QPixmap thumb = generateThumbnail(filePath);
    if (!thumb.isNull()) {
        _thumbnailCache[filePath] = thumb;
    }
    _metadataCache[filePath] = probeMetadata(filePath);
    _fileList->addTopLevelItem(createItem(filePath));
    updateStatus();
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
    _metadataCache.clear();
    updateStatus();
}

QTreeWidgetItem*
FluxProjectBin::createItem(const QString& filePath)
{
    QFileInfo fi(filePath);
    QTreeWidgetItem* item = new QTreeWidgetItem();
    const QString displayName = fi.fileName();
    const FluxProjectBinMetadata metadata = _metadataCache.value(filePath);
    item->setData(eColumnName, kFluxProjectBinPathRole, filePath);
    item->setData(eColumnName, kFluxProjectBinDisplayNameRole, displayName);

    if (_thumbnailViewMode) {
        QString metaStr;
        if (!metadata.resolution.isEmpty()) {
            metaStr += metadata.resolution;
        }
        if (!metadata.duration.isEmpty()) {
            if (!metaStr.isEmpty()) metaStr += QString::fromUtf8(" | ");
            metaStr += metadata.duration;
        }
        if (!metadata.fps.isEmpty()) {
            if (!metaStr.isEmpty()) metaStr += QString::fromUtf8(" | ");
            metaStr += metadata.fps + QString::fromUtf8(" fps");
        }
        if (metaStr.isEmpty()) {
            metaStr = QString::fromUtf8("No metadata");
        }
        item->setText(eColumnName, displayName + QString::fromUtf8("\n") + metaStr);
    } else {
        item->setText(eColumnName, displayName);
    }

    item->setText(eColumnResolution, metadata.resolution);
    item->setText(eColumnDuration, metadata.duration);
    item->setText(eColumnStart, metadata.startFrame);
    item->setText(eColumnEnd, metadata.endFrame);
    item->setText(eColumnFps, metadata.fps);
    item->setText(eColumnPath, metadata.filePath);
    item->setToolTip(eColumnName, filePath);
    item->setToolTip(eColumnPath, filePath);
    QMap<QString, QPixmap>::const_iterator cacheIt = _thumbnailCache.find(filePath);
    if (cacheIt != _thumbnailCache.end()) {
        item->setIcon(eColumnName, QIcon(_thumbnailViewMode ? cacheIt.value() : cacheIt.value().scaled(32, 24, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
    } else {
        item->setIcon(eColumnName, QIcon::fromTheme(QString::fromUtf8("document-open"), QIcon()));
    }
    return item;
}

void
FluxProjectBin::updateStatus()
{
    if (!_statusLabel) {
        return;
    }
    const int count = _files.size();
    if (count == 0) {
        _statusLabel->setText(QString::fromUtf8("0 items"));
    } else if (count == 1) {
        _statusLabel->setText(QString::fromUtf8("1 item"));
    } else {
        _statusLabel->setText(QString::fromUtf8("%1 items").arg(count));
    }
}

void
FluxProjectBin::onImportButtonClicked()
{
    QStringList files = QFileDialog::getOpenFileNames(
        this,
        QString::fromUtf8("Import Files"),
        QString(),
        QString::fromUtf8("Media Files (*.mov *.mp4 *.mxf *.avi *.exr *.tif *.tiff *.png *.jpg *.jpeg *.psd *.dpx);;All Files (*)"));
    for (const QString& file : files) {
        addFile(file);
    }
}

void
FluxProjectBin::onItemDoubleClicked(QTreeWidgetItem* item, int column)
{
    Q_UNUSED(column);
    if (item) {
        Q_EMIT fileRequested(itemPath(item));
    }
}

void
FluxProjectBin::onSearchTextChanged(const QString& text)
{
    for (int i = 0; i < _fileList->topLevelItemCount(); ++i) {
        QTreeWidgetItem* item = _fileList->topLevelItem(i);
        const QString name = item->text(eColumnName);
        const QString path = itemPath(item);
        const bool match = text.isEmpty() || name.contains(text, Qt::CaseInsensitive) || path.contains(text, Qt::CaseInsensitive);
        item->setHidden(!match);
    }
}

void
FluxProjectBin::onContextMenuRequested(const QPoint& pos)
{
    QTreeWidgetItem* item = _fileList->itemAt(pos);
    if (!item) {
        return;
    }
    QMenu menu(this);
    QAction* renameAction = menu.addAction(QString::fromUtf8("Rename"));
    QAction* openAction = menu.addAction(QString::fromUtf8("Open in File Manager"));
    menu.addSeparator();
    QAction* removeAction = menu.addAction(QString::fromUtf8("Remove from Project"));
    QAction* chosen = menu.exec(_fileList->viewport()->mapToGlobal(pos));
    if (chosen == renameAction) {
        renameItem(item);
    } else if (chosen == openAction) {
        openItemInFileManager(item);
    } else if (chosen == removeAction) {
        removeItem(item);
    }
}

void
FluxProjectBin::removeItem(QTreeWidgetItem* item)
{
    if (!item) {
        return;
    }
    const QString path = itemPath(item);
    _files.removeAll(path);
    _thumbnailCache.remove(path);
    _metadataCache.remove(path);
    delete _fileList->takeTopLevelItem(_fileList->indexOfTopLevelItem(item));
    updateStatus();
}

void
FluxProjectBin::renameItem(QTreeWidgetItem* item)
{
    if (!item) {
        return;
    }
    bool ok = false;
    const QString current = item->data(eColumnName, kFluxProjectBinDisplayNameRole).toString();
    const QString renamed = QInputDialog::getText(this, QString::fromUtf8("Rename Project Item"), QString::fromUtf8("Name:"), QLineEdit::Normal, current, &ok).trimmed();
    if (!ok || renamed.isEmpty()) {
        return;
    }
    item->setText(eColumnName, renamed);
    item->setData(eColumnName, kFluxProjectBinDisplayNameRole, renamed);
}

void
FluxProjectBin::openItemInFileManager(QTreeWidgetItem* item) const
{
    const QString path = itemPath(item);
    if (path.isEmpty()) {
        return;
    }
    const QFileInfo fi(path);
    QDesktopServices::openUrl(QUrl::fromLocalFile(fi.absolutePath()));
}

QString
FluxProjectBin::itemPath(const QTreeWidgetItem* item) const
{
    return item ? item->data(eColumnName, kFluxProjectBinPathRole).toString() : QString();
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
            const QString filePath = url.toLocalFile();
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
FluxProjectBinTreeWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        _dragStartPos = event->pos();
        _isDragging = false;
    }
    QTreeWidget::mousePressEvent(event);
}

void
FluxProjectBinTreeWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (event->buttons() & Qt::LeftButton && !_isDragging) {
        const int distance = (event->pos() - _dragStartPos).manhattanLength();
        if (distance > QApplication::startDragDistance()) {
            _isDragging = true;
            performDrag();
            _isDragging = false;
            return;
        }
    }
    QTreeWidget::mouseMoveEvent(event);
}

void
FluxProjectBinTreeWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (!itemAt(event->pos())) {
        Q_EMIT emptySpaceDoubleClicked();
        event->accept();
        return;
    }
    QTreeWidget::mouseDoubleClickEvent(event);
}

void
FluxProjectBinTreeWidget::performDrag()
{
    QTreeWidgetItem* item = currentItem();
    if (!item) {
        return;
    }
    const QString filePath = item->data(0, kFluxProjectBinPathRole).toString();
    if (filePath.isEmpty()) {
        return;
    }
    QMimeData* mimeData = new QMimeData;
    mimeData->setUrls(QList<QUrl>() << QUrl::fromLocalFile(filePath));
    mimeData->setData(QString::fromUtf8("application/x-flux-asset"), filePath.toUtf8());
    mimeData->setText(filePath);
    QDrag* drag = new QDrag(this);
    drag->setMimeData(mimeData);
    QPixmap dragPixmap = item->icon(0).pixmap(64, 48);
    if (dragPixmap.isNull()) {
        dragPixmap = QPixmap(64, 48);
        dragPixmap.fill(QColor(80, 130, 200, 180));
        QPainter painter(&dragPixmap);
        painter.setPen(Qt::white);
        painter.drawText(dragPixmap.rect(), Qt::AlignCenter, QFileInfo(filePath).fileName().left(10));
    }
    drag->setPixmap(dragPixmap);
    drag->setHotSpot(QPoint(dragPixmap.width() / 2, dragPixmap.height() / 2));
    drag->exec(Qt::CopyAction | Qt::MoveAction, Qt::CopyAction);
}

FluxProjectBinMetadata
FluxProjectBin::probeMetadata(const QString& filePath) const
{
    FluxProjectBinMetadata metadata;
    metadata.filePath = filePath;
    const QFileInfo fi(filePath);
    const QString ext = fi.suffix().toLower();

    if (fluxProjectBinIsVideoExtension(ext)) {
        QProcess ffprobe;
        QStringList args;
        args << QString::fromUtf8("-v") << QString::fromUtf8("error")
             << QString::fromUtf8("-select_streams") << QString::fromUtf8("v:0")
             << QString::fromUtf8("-show_entries") << QString::fromUtf8("stream=width,height,avg_frame_rate,r_frame_rate,duration,nb_frames")
             << QString::fromUtf8("-of") << QString::fromUtf8("default=noprint_wrappers=1:nokey=1")
             << filePath;
        ffprobe.start(QString::fromUtf8("ffprobe"), args);
        if (ffprobe.waitForStarted(1000) && ffprobe.waitForFinished(3000)) {
            const QStringList lines = QString::fromLocal8Bit(ffprobe.readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            if (lines.size() >= 6) {
                metadata.resolution = lines[0] + QString::fromUtf8("x") + lines[1];
                double fps = 0.;
                if (!fluxProjectBinParseFps(lines[2], &fps)) {
                    fluxProjectBinParseFps(lines[3], &fps);
                }
                metadata.fps = fluxProjectBinFormatFps(fps);
                bool ok = false;
                const double seconds = lines[4].toDouble(&ok);
                if (ok) {
                    metadata.duration = fluxProjectBinSecondsToTimecode(seconds, fps);
                }
                const int frameCount = lines[5].toInt(&ok);
                if (ok && frameCount > 0) {
                    metadata.startFrame = QString::fromUtf8("1");
                    metadata.endFrame = QString::number(frameCount);
                }
            }
        }
        ffprobe.kill();
        ffprobe.waitForFinished(250);
        return metadata;
    }

    QImageReader reader(filePath);
    const QSize size = reader.size();
    if (size.isValid()) {
        metadata.resolution = QString::fromUtf8("%1x%2").arg(size.width()).arg(size.height());
    }

    QRegularExpression framePattern(QString::fromUtf8("^(.*?)(\\d+)(\\.[^.]+)$"));
    const QRegularExpressionMatch match = framePattern.match(fi.fileName());
    if (match.hasMatch()) {
        const QString prefix = match.captured(1);
        const QString suffix = match.captured(3);
        int first = match.captured(2).toInt();
        int last = first;
        const QStringList entries = fi.dir().entryList(QStringList() << (prefix + QString::fromUtf8("*") + suffix), QDir::Files, QDir::Name);
        for (const QString& entry : entries) {
            const QRegularExpressionMatch candidate = framePattern.match(entry);
            if (candidate.hasMatch() && candidate.captured(1) == prefix && candidate.captured(3) == suffix) {
                const int frame = candidate.captured(2).toInt();
                first = qMin(first, frame);
                last = qMax(last, frame);
            }
        }
        metadata.startFrame = QString::number(first);
        metadata.endFrame = QString::number(last);
        metadata.duration = QString::number(qMax(1, last - first + 1)) + QString::fromUtf8(" frame(s)");
    } else {
        metadata.startFrame = QString::fromUtf8("1");
        metadata.endFrame = QString::fromUtf8("1");
        metadata.duration = QString::fromUtf8("1 frame");
    }
    return metadata;
}

QPixmap
FluxProjectBin::generateThumbnail(const QString& filePath)
{
    QFileInfo fi(filePath);
    QString ext = fi.suffix().toLower();
    QPixmap result;

    if (fluxProjectBinIsVideoExtension(ext)) {
        QProcess ffmpeg;
        QStringList args;
        args << QString::fromUtf8("-i") << filePath
             << QString::fromUtf8("-vframes") << QString::fromUtf8("1")
             << QString::fromUtf8("-f") << QString::fromUtf8("image2pipe")
             << QString::fromUtf8("-vcodec") << QString::fromUtf8("mjpeg")
             << QString::fromUtf8("-");
        ffmpeg.start(QString::fromUtf8("ffmpeg"), args);
        if (ffmpeg.waitForStarted(2000) && ffmpeg.waitForFinished(5000)) {
            QImage frameImage;
            if (frameImage.loadFromData(ffmpeg.readAllStandardOutput(), "JPEG")) {
                result = QPixmap::fromImage(frameImage);
            }
        }
        ffmpeg.kill();
        ffmpeg.waitForFinished(1000);
    } else {
        result.load(filePath);
    }

    if (result.isNull()) {
        result = QPixmap(80, 60);
        result.fill(QColor(40, 40, 50));
        QPainter painter(&result);
        painter.setPen(QColor(150, 150, 160));
        QFont font;
        font.setPointSize(7);
        painter.setFont(font);
        painter.drawText(result.rect(), Qt::AlignCenter, ext.toUpper());
    }

    QPixmap scaled = result.scaled(80, 60, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QPixmap centered(80, 60);
    centered.fill(QColor(30, 30, 38));
    QPainter painter(&centered);
    painter.drawPixmap((80 - scaled.width()) / 2, (60 - scaled.height()) / 2, scaled);
    return centered;
}

NATRON_NAMESPACE_EXIT

NATRON_NAMESPACE_USING
#include "moc_FluxProjectBin.cpp"
