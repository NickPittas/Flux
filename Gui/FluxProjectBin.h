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
 * ***** END LICENSE BLOCK ***** */

#ifndef FLUX_PROJECTBIN_H
#define FLUX_PROJECTBIN_H

// ***** BEGIN PYTHON BLOCK *****
#include <Python.h>
// ***** END PYTHON BLOCK *****

#include "Global/Macros.h"

CLANG_DIAG_OFF(deprecated)
CLANG_DIAG_OFF(uninitialized)
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QPixmap>
#include <QPushButton>
#include <QStringList>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWidget>
CLANG_DIAG_ON(deprecated)
CLANG_DIAG_ON(uninitialized)

#include "Gui/PanelWidget.h"

NATRON_NAMESPACE_ENTER

struct FluxProjectBinMetadata
{
    QString resolution;
    QString duration;
    QString startFrame;
    QString endFrame;
    QString fps;
    QString filePath;
};

class FluxProjectBinTreeWidget
    : public QTreeWidget
{
    GCC_DIAG_SUGGEST_OVERRIDE_OFF
    Q_OBJECT
    GCC_DIAG_SUGGEST_OVERRIDE_ON

public:
    explicit FluxProjectBinTreeWidget(QWidget* parent = nullptr);

Q_SIGNALS:
    void emptySpaceDoubleClicked();

protected:
    virtual void mousePressEvent(QMouseEvent* event) OVERRIDE;
    virtual void mouseMoveEvent(QMouseEvent* event) OVERRIDE;
    virtual void mouseDoubleClickEvent(QMouseEvent* event) OVERRIDE;

private:
    QPoint _dragStartPos;
    bool _isDragging;
    void performDrag();
};

class FluxProjectBin
    : public QWidget
      , public PanelWidget
{
    GCC_DIAG_SUGGEST_OVERRIDE_OFF
    Q_OBJECT
    GCC_DIAG_SUGGEST_OVERRIDE_ON

public:
    explicit FluxProjectBin(Gui* gui, QWidget* parent = nullptr);
    virtual ~FluxProjectBin();

    void addFile(const QString& filePath);
    QStringList getFiles() const;

Q_SIGNALS:
    void fileRequested(const QString& filePath);
    void filesDropped(const QStringList& filePaths);

public Q_SLOTS:
    void clearBin();
    void onImportButtonClicked();
    void onItemDoubleClicked(QTreeWidgetItem* item, int column);
    void onSearchTextChanged(const QString& text);
    void onViewModeToggled();
    void onContextMenuRequested(const QPoint& pos);

protected:
    virtual void dragEnterEvent(QDragEnterEvent* event) OVERRIDE;
    virtual void dropEvent(QDropEvent* event) OVERRIDE;

private:
    enum Column {
        eColumnName = 0,
        eColumnResolution,
        eColumnDuration,
        eColumnStart,
        eColumnEnd,
        eColumnFps,
        eColumnPath,
        eColumnCount
    };

    void setupUI();
    QTreeWidgetItem* createItem(const QString& filePath);
    void applyViewMode();
    void removeItem(QTreeWidgetItem* item);
    void renameItem(QTreeWidgetItem* item);
    void openItemInFileManager(QTreeWidgetItem* item) const;
    QString itemPath(const QTreeWidgetItem* item) const;
    QPixmap generateThumbnail(const QString& filePath);
    FluxProjectBinMetadata probeMetadata(const QString& filePath) const;
    void updateStatus();

    QLineEdit* _searchField;
    FluxProjectBinTreeWidget* _fileList;
    QLabel* _headerLabel;
    QToolButton* _importButton;
    QToolButton* _clearButton;
    QToolButton* _gridViewButton;
    QToolButton* _listViewButton;
    QLabel* _statusLabel;
    QStringList _files;
    QMap<QString, QPixmap> _thumbnailCache;
    QMap<QString, FluxProjectBinMetadata> _metadataCache;
    bool _thumbnailViewMode;
};

NATRON_NAMESPACE_EXIT

#endif // FLUX_PROJECTBIN_H
