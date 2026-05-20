/* ***** BEGIN LICENSE BLOCK *****
 * Flux — Effects Stack Panel
 * (C) 2025 Nick Pittas
 * GPL2 — see LICENSE.txt
 * ***** END LICENSE BLOCK ***** */

#ifndef FLUX_EFFECTSPANEL_H
#define FLUX_EFFECTSPANEL_H

// ***** BEGIN PYTHON BLOCK *****
#include <Python.h>
// ***** END PYTHON BLOCK *****

#include "Global/Macros.h"

CLANG_DIAG_OFF(deprecated)
CLANG_DIAG_OFF(uninitialized)
#include <QWidget>
#include <QVBoxLayout>
#include <QListWidget>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QSplitter>
CLANG_DIAG_ON(deprecated)
CLANG_DIAG_ON(uninitialized)

#include "Gui/PanelWidget.h"

NATRON_NAMESPACE_ENTER

class FluxEffectsPanel
    : public QWidget
      , public PanelWidget
{
    GCC_DIAG_SUGGEST_OVERRIDE_OFF
    Q_OBJECT
    GCC_DIAG_SUGGEST_OVERRIDE_ON

public:

    explicit FluxEffectsPanel(Gui* gui,
                              QWidget* parent = nullptr);

    virtual ~FluxEffectsPanel();

    /** @brief Set the layer whose effects are displayed. -1 for no selection. */
    void setActiveLayer(int layerIndex, const QString& layerName);

    /** @brief Add an effect to the current layer's stack. */
    void addEffect(const QString& pluginId, const QString& name);

    /** @brief Remove an effect from the stack by index. */
    void removeEffect(int index);

Q_SIGNALS:

    /** @brief Emitted when the user wants to add an effect. */
    void effectAddRequested(const QString& pluginId);

    /** @brief Emitted when an effect is selected. */
    void effectSelected(int index);

    /** @brief Emitted when an effect is removed. */
    void effectRemoved(int index);

    /** @brief Emitted when effects are reordered. */
    void effectsReordered();

public Q_SLOTS:

    void onAddButtonClicked();
    void onRemoveButtonClicked();
    void onEffectClicked(QListWidgetItem* item);
    void onEffectDoubleClicked(QListWidgetItem* item);

private:

    void setupUI();

    QLabel* _layerLabel;
    QListWidget* _effectList;
    QComboBox* _effectCombo;
    QPushButton* _addButton;
    QPushButton* _removeButton;
    int _activeLayerIndex;
};

NATRON_NAMESPACE_EXIT

#endif // FLUX_EFFECTSPANEL_H
