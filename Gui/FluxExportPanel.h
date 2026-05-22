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
#include <QSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFileDialog>
#include <QGroupBox>
#include <QFormLayout>
CLANG_DIAG_ON(deprecated)
CLANG_DIAG_ON(uninitialized)

#include "Gui/PanelWidget.h"

NATRON_NAMESPACE_ENTER

class FluxExportPanel
    : public QWidget
      , public PanelWidget
{
    GCC_DIAG_SUGGEST_OVERRIDE_OFF
    Q_OBJECT
    GCC_DIAG_SUGGEST_OVERRIDE_ON

public:

    explicit FluxExportPanel(Gui* gui,
                              QWidget* parent = nullptr);

    virtual ~FluxExportPanel();

    /** @brief Set the export nodes (called from Gui after creation). */
    void setExportNodes(const NodePtr& reformatNode, const NodePtr& writeNode);

    /** @brief Get the Write node. */
    NodePtr getWriteNode() const;

    /** @brief Get the Reformat node. */
    NodePtr getReformatNode() const;

Q_SIGNALS:

    /** @brief Emitted when the user clicks Render. */
    void renderRequested();

    /** @brief Emitted when the user changes the output file path. */
    void outputPathChanged(const QString& path);

public Q_SLOTS:

    /** @brief Browse for output file. */
    void onBrowseClicked();

    /** @brief Update the Write node filename knob from the path field. */
    void onOutputPathChanged();

    /** @brief Toggle the Reformat node enabled/disabled. */
    void onReformatToggleChanged(int state);

    /** @brief Start rendering via Natron's render engine. */
    void onRenderClicked();

    /** @brief Open the Write node settings panel for advanced codec options. */
    void onAdvancedWriteSettingsClicked();

    /** @brief Open the Reformat node settings panel. */
    void onAdvancedReformatSettingsClicked();

private:

    NodePtr _reformatNode;
    NodePtr _writeNode;

    // Output section
    QLineEdit* _outputPathEdit;
    QPushButton* _browseButton;

    // Reformat section
    QCheckBox* _reformatToggle;
    QPushButton* _reformatSettingsButton;

    // Frame range section
    QSpinBox* _firstFrameSpin;
    QSpinBox* _lastFrameSpin;

    // Render section
    QPushButton* _renderButton;
    QPushButton* _writeSettingsButton;

    friend class Gui;
};

NATRON_NAMESPACE_EXIT

#endif // FLUXEXPORTPANEL_H
