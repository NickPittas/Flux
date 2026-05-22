/* ***** BEGIN LICENSE BLOCK *****
 * Flux — Effects Stack Panel
 * (C) 2025 Nick Pittas
 * GPL2 — see LICENSE.txt
 * ***** END LICENSE BLOCK ***** */

#include "Gui/FluxEffectsPanel.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QApplication>

#include "Global/Macros.h"

NATRON_NAMESPACE_ENTER

FluxEffectsPanel::FluxEffectsPanel(Gui* gui,
                                   QWidget* parent)
    : QWidget(parent)
      , PanelWidget(this, gui)
      , _layerLabel(nullptr)
      , _effectList(nullptr)
      , _effectCombo(nullptr)
      , _addButton(nullptr)
      , _removeButton(nullptr)
      , _activeLayerIndex(-1)
{
    setupUI();
}

FluxEffectsPanel::~FluxEffectsPanel()
{
}

void
FluxEffectsPanel::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(4);

    // --- Layer label ---
    _layerLabel = new QLabel(QStringLiteral("No layer selected"));
    _layerLabel->setAlignment(Qt::AlignCenter);

    QString labelStyle = QStringLiteral(
        "QLabel {"
        "   font-size: 12px;"
        "   font-weight: bold;"
        "   padding: 6px;"
        "   background-color: #1a1a2e;"
        "   border-radius: 3px;"
        "}"
    );
    _layerLabel->setStyleSheet(labelStyle);

    mainLayout->addWidget(_layerLabel);

    // --- Effect list ---
    _effectList = new QListWidget();
    _effectList->setDragDropMode(QAbstractItemView::InternalMove);
    _effectList->setSelectionMode(QAbstractItemView::SingleSelection);
    _effectList->setAlternatingRowColors(true);

    QString listStyle = QStringLiteral(
        "QListWidget {"
        "   background-color: #16161a;"
        "   border: 1px solid #333;"
        "   border-radius: 3px;"
        "   font-size: 11px;"
        "}"
        "QListWidget::item {"
        "   padding: 6px 8px;"
        "   border-bottom: 1px solid #222;"
        "}"
        "QListWidget::item:selected {"
        "   background-color: #4285F4;"
        "   color: white;"
        "}"
        "QListWidget::item:alternate {"
        "   background-color: #1a1a20;"
        "}"
    );
    _effectList->setStyleSheet(listStyle);

    mainLayout->addWidget(_effectList, 1);

    // --- Add effect controls ---
    QHBoxLayout* addLayout = new QHBoxLayout();
    addLayout->setSpacing(4);

    _effectCombo = new QComboBox();

    // Populate with common effect categories
    _effectCombo->addItem(QStringLiteral("-- Add Effect --"));
    _effectCombo->addItem(QStringLiteral("Blur"), QStringLiteral("net.sf.openfx.BlurPlugin"));
    _effectCombo->addItem(QStringLiteral("Sharpen"), QStringLiteral("net.sf.openfx.SharpenPlugin"));
    _effectCombo->addItem(QStringLiteral("Color Correct"), QStringLiteral("net.sf.openfx.ColorCorrectPlugin"));
    _effectCombo->addItem(QStringLiteral("Grade"), QStringLiteral("net.sf.openfx.GradePlugin"));
    _effectCombo->addItem(QStringLiteral("Transform"), QStringLiteral("net.sf.openfx.TransformPlugin"));
    _effectCombo->addItem(QStringLiteral("Merge"), QStringLiteral("net.sf.openfx.MergePlugin"));
    _effectCombo->addItem(QStringLiteral("Shuffle"), QStringLiteral("net.sf.openfx.ShufflePlugin"));
    _effectCombo->addItem(QStringLiteral("Invert"), QStringLiteral("net.sf.openfx.InvertPlugin"));
    _effectCombo->addItem(QStringLiteral("Saturation"), QStringLiteral("net.sf.openfx.SaturationPlugin"));
    _effectCombo->addItem(QStringLiteral("OCIO ColorSpace"), QStringLiteral("fr.inria.openfx.OCIOColorSpace"));
    _effectCombo->addItem(QStringLiteral("OCIO File Transform"), QStringLiteral("fr.inria.openfx.OCIOFileTransform"));

    QString comboStyle = QStringLiteral(
        "QComboBox {"
        "   background-color: #202026;"
        "   border: 1px solid #444;"
        "   border-radius: 3px;"
        "   padding: 4px 8px;"
        "   font-size: 11px;"
        "   color: #d2d2d7;"
        "}"
        "QComboBox::drop-down {"
        "   border: none;"
        "}"
        "QComboBox QAbstractItemView {"
        "   background-color: #202026;"
        "   color: #d2d2d7;"
        "   selection-background-color: #4285F4;"
        "}"
    );
    _effectCombo->setStyleSheet(comboStyle);

    _addButton = new QPushButton(QStringLiteral("+"));
    _addButton->setFixedSize(28, 28);
    _addButton->setToolTip(QStringLiteral("Add selected effect"));
    _addButton->setStyleSheet(QStringLiteral(
        "QPushButton {"
        "   background-color: #4285F4;"
        "   color: white;"
        "   border: none;"
        "   border-radius: 4px;"
        "   font-weight: bold;"
        "   font-size: 14px;"
        "}"
        "QPushButton:hover {"
        "   background-color: #5a95f5;"
        "}"
        "QPushButton:pressed {"
        "   background-color: #3275e4;"
        "}"
    ));

    _removeButton = new QPushButton(QStringLiteral("-"));
    _removeButton->setFixedSize(28, 28);
    _removeButton->setToolTip(QStringLiteral("Remove selected effect"));
    _removeButton->setStyleSheet(QStringLiteral(
        "QPushButton {"
        "   background-color: #e53935;"
        "   color: white;"
        "   border: none;"
        "   border-radius: 4px;"
        "   font-weight: bold;"
        "   font-size: 14px;"
        "}"
        "QPushButton:hover {"
        "   background-color: #ef5350;"
        "}"
        "QPushButton:pressed {"
        "   background-color: #c62828;"
        "}"
    ));

    addLayout->addWidget(_effectCombo, 1);
    addLayout->addWidget(_addButton);
    addLayout->addWidget(_removeButton);

    mainLayout->addLayout(addLayout);

    // --- Connections ---
    QObject::connect(_addButton, SIGNAL(clicked()), this, SLOT(onAddButtonClicked()));
    QObject::connect(_removeButton, SIGNAL(clicked()), this, SLOT(onRemoveButtonClicked()));
    QObject::connect(_effectList, SIGNAL(itemClicked(QListWidgetItem*)), this, SLOT(onEffectClicked(QListWidgetItem*)));
    QObject::connect(_effectList, SIGNAL(itemDoubleClicked(QListWidgetItem*)), this, SLOT(onEffectDoubleClicked(QListWidgetItem*)));

    // T019-D: Start disabled since no layer is selected
    _effectCombo->setEnabled(false);
    _addButton->setEnabled(false);
    _removeButton->setEnabled(false);
}

void
FluxEffectsPanel::setActiveLayer(int layerIndex,
                                 const QString& layerName)
{
    _activeLayerIndex = layerIndex;
    _effectList->clear();

    if (layerIndex >= 0) {
        _layerLabel->setText(QStringLiteral("Layer: %1").arg(layerName));
        // T019-D: Enable controls when a layer is selected
        _effectCombo->setEnabled(true);
        _addButton->setEnabled(false);
        _removeButton->setEnabled(true);
    } else {
        _layerLabel->setText(QStringLiteral("No layer selected"));
        // T019-D: Disable controls when no layer is selected
        _effectCombo->setEnabled(false);
        _addButton->setEnabled(false);
        _removeButton->setEnabled(false);
    }
}

void
FluxEffectsPanel::addEffect(const QString& pluginId,
                            const QString& name)
{
    QListWidgetItem* item = new QListWidgetItem(name, _effectList);
    item->setData(Qt::UserRole, pluginId);
    _effectList->addItem(item);
    _effectList->setCurrentItem(item);
}

void
FluxEffectsPanel::removeEffect(int index)
{
    if (index >= 0 && index < _effectList->count()) {
        delete _effectList->takeItem(index);
    }
}

void
FluxEffectsPanel::onAddButtonClicked()
{
    // P4: effects are created through Natron's existing Tab node-search dialog
    // from the timeline. The old combo path is disabled to avoid orphan nodes.
    return;

    // T019-D: Safety check — no layer selected
    if (_activeLayerIndex < 0) {
        return;
    }

    int idx = _effectCombo->currentIndex();
    if (idx <= 0) {
        return; // "-- Add Effect --" selected
    }

    QString pluginId = _effectCombo->itemData(idx).toString();
    QString name = _effectCombo->itemText(idx);

    addEffect(pluginId, name);
    Q_EMIT effectAddRequested(pluginId);
    Q_EMIT layerEffectAddRequested(_activeLayerIndex, pluginId);

    // Reset combo
    _effectCombo->setCurrentIndex(0);
}

void
FluxEffectsPanel::onRemoveButtonClicked()
{
    int row = _effectList->currentRow();
    if (row >= 0) {
        Q_EMIT effectRemoved(row);
    }
}

void
FluxEffectsPanel::onEffectClicked(QListWidgetItem* item)
{
    if (item) {
        Q_EMIT effectSelected(_effectList->row(item));
    }
}

void
FluxEffectsPanel::onEffectDoubleClicked(QListWidgetItem* item)
{
    if (item) {
        Q_EMIT effectSelected(_effectList->row(item));
    }
}

void
FluxEffectsPanel::showAddEffectPopup()
{
    if (!_effectCombo) {
        return;
    }
    // Ensure the combo is enabled (a layer must be active)
    if (!_effectCombo->isEnabled()) {
        return;
    }
    _effectCombo->setFocus();
    _effectCombo->showPopup();
}

NATRON_NAMESPACE_EXIT
