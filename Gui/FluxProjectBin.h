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

#ifndef FLUX_PROJECTBIN_H
#define FLUX_PROJECTBIN_H

// ***** BEGIN PYTHON BLOCK *****
#include <Python.h>
// ***** END PYTHON BLOCK *****

#include "Global/Macros.h"

CLANG_DIAG_OFF(deprecated)
CLANG_DIAG_OFF(uninitialized)
#include <QWidget>
#include <QListWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QLineEdit>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMap>
#include <QPixmap>
CLANG_DIAG_ON(deprecated)
CLANG_DIAG_ON(uninitialized)

#include "Gui/PanelWidget.h"

NATRON_NAMESPACE_ENTER

class FluxProjectBinListWidget
    : public QListWidget
{
    GCC_DIAG_SUGGEST_OVERRIDE_OFF
    Q_OBJECT
    GCC_DIAG_SUGGEST_OVERRIDE_ON

public:

    explicit FluxProjectBinListWidget(QWidget* parent = nullptr)
        : QListWidget(parent)
        , _dragStartPos()
        , _isDragging(false)
    {
    }

Q_SIGNALS:

    /** @brief Emitted when the user double-clicks on empty space (no item under cursor). */
    void emptySpaceDoubleClicked();

protected:

    // Manual drag implementation — bypasses QListWidget's broken startDrag
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

    explicit FluxProjectBin(Gui* gui,
                            QWidget* parent = nullptr);

    virtual ~FluxProjectBin();

    /** @brief Adds a file to the project bin. Creates a thumbnail and stores the path. */
    void addFile(const QString& filePath);

    /** @brief Returns the list of file paths currently in the bin. */
    QStringList getFiles() const;

    /** @brief Clears all files from the bin. */
    void clearBin();

Q_SIGNALS:

    /** @brief Emitted when the user double-clicks or drags a file to create a layer. */
    void fileRequested(const QString& filePath);

    /** @brief Emitted when files are dragged from outside the app into the bin. */
    void filesDropped(const QStringList& filePaths);

public Q_SLOTS:

    void onImportButtonClicked();
    void onItemDoubleClicked(QListWidgetItem* item);
    void onSearchTextChanged(const QString& text);
    void onViewModeToggled();

protected:

    virtual void dragEnterEvent(QDragEnterEvent* event) OVERRIDE;
    virtual void dropEvent(QDropEvent* event) OVERRIDE;

private:

    void setupUI();
    QListWidgetItem* createItem(const QString& filePath);
    void applyViewMode();
    QPixmap generateThumbnail(const QString& filePath);

    QLineEdit* _searchField;
    FluxProjectBinListWidget* _fileList;
    QLabel* _headerLabel;
    QPushButton* _importButton;
    QPushButton* _clearButton;
    QToolButton* _viewModeButton;
    QStringList _files;

    /** @brief Thumbnail cache: filePath -> QPixmap (80x60 thumbnail). */
    QMap<QString, QPixmap> _thumbnailCache;

    /** @brief true = thumbnail/icon mode, false = list mode. */
    bool _thumbnailViewMode;
};

NATRON_NAMESPACE_EXIT

#endif // FLUX_PROJECTBIN_H
