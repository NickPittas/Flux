/* ***** BEGIN LICENSE BLOCK *****
 * Flux — Export/Render Panel
 * (C) 2025 Nick Pittas
 * GPL2 — see LICENSE.txt
 * ***** END LICENSE BLOCK ***** */

#ifndef FLUXEXPORTPANEL_H
#define FLUXEXPORTPANEL_H

// ***** BEGIN PYTHON BLOCK *****
#include <Python.h>
// ***** END PYTHON BLOCK *****

#include "Global/Macros.h"

CLANG_DIAG_OFF(deprecated)
CLANG_DIAG_OFF(uninitialized)
#include <QWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFileDialog>
#include <QGroupBox>
CLANG_DIAG_ON(deprecated)
CLANG_DIAG_ON(uninitialized)

#include "Gui/PanelWidget.h"

NATRON_NAMESPACE_ENTER

class DockablePanel;

class FluxExportPanel
    : public QWidget
      , public PanelWidget
{
    GCC_DIAG_SUGGEST_OVERRIDE_OFF
    Q_OBJECT
    GCC_DIAG_SUGGEST_OVERRIDE_ON

public:

    explicit FluxExportPanel(Gui* gui, QWidget* parent = nullptr);
    virtual ~FluxExportPanel();

    void setExportNodes(const NodePtr& reformatNode, const NodePtr& writeNode);
    void syncFrameRangeFromProject();

    NodePtr getWriteNode() const;
    NodePtr getReformatNode() const;

Q_SIGNALS:

    void renderRequested();
    void outputPathChanged(const QString& path);

private Q_SLOTS:

    void onBrowseClicked();
    void onOutputPathChanged();
    void onReformatToggleChanged(int state);
    void onRenderClicked();

private:

    NodePtr _reformatNode;
    NodePtr _writeNode;

    // Output path (convenience shortcut at top)
    QLineEdit* _outputPathEdit;
    QPushButton* _browseButton;

    // Embedded Write node knobs
    QGroupBox* _writeGroup;
    QVBoxLayout* _writeGroupLayout;
    DockablePanel* _writeKnobsPanel;

    // Embedded Reformat node knobs
    QGroupBox* _reformatGroup;
    QVBoxLayout* _reformatGroupLayout;
    DockablePanel* _reformatKnobsPanel;
    QCheckBox* _reformatToggle;

    // Render button
    QPushButton* _renderButton;

    friend class Gui;
};

NATRON_NAMESPACE_EXIT

#endif // FLUXEXPORTPANEL_H
