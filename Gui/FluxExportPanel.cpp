/* ***** BEGIN LICENSE BLOCK *****
 * Flux — Export/Render Panel
 * (C) 2025 Nick Pittas
 * GPL2 — see LICENSE.txt
 * ***** END LICENSE BLOCK ***** */

#include "Gui/FluxExportPanel.h"

#include "Gui/Gui.h"
#include "Gui/GuiAppInstance.h"
#include "Gui/NodeGui.h"
#include "Engine/Node.h"
#include "Engine/EffectInstance.h"
#include "Engine/Project.h"
#include "Engine/Knob.h"
#include "Engine/KnobTypes.h"
#include "Engine/AppInstance.h"

NATRON_NAMESPACE_ENTER

FluxExportPanel::FluxExportPanel(Gui* gui,
                                 QWidget* parent)
    : QWidget(parent)
      , PanelWidget(this, gui)
      , _reformatNode()
      , _writeNode()
      , _outputPathEdit(nullptr)
      , _browseButton(nullptr)
      , _reformatToggle(nullptr)
      , _reformatSettingsButton(nullptr)
      , _firstFrameSpin(nullptr)
      , _lastFrameSpin(nullptr)
      , _renderButton(nullptr)
      , _writeSettingsButton(nullptr)
{
    setObjectName(QString::fromUtf8("FluxExportPanel"));
    setMinimumWidth(200);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(6);

    // --- Output File ---
    QGroupBox* outputGroup = new QGroupBox(QString::fromUtf8("Output"));
    QVBoxLayout* outputLayout = new QVBoxLayout(outputGroup);

    QHBoxLayout* pathLayout = new QHBoxLayout();
    _outputPathEdit = new QLineEdit();
    _outputPathEdit->setPlaceholderText(QString::fromUtf8("Click browse to set output file..."));
    pathLayout->addWidget(_outputPathEdit);
    _browseButton = new QPushButton(QString::fromUtf8("Browse"));
    pathLayout->addWidget(_browseButton);
    outputLayout->addLayout(pathLayout);

    _writeSettingsButton = new QPushButton(QString::fromUtf8("Advanced Codec Settings..."));
    outputLayout->addWidget(_writeSettingsButton);

    mainLayout->addWidget(outputGroup);

    // --- Format Override ---
    QGroupBox* formatGroup = new QGroupBox(QString::fromUtf8("Format Override"));
    QVBoxLayout* formatLayout = new QVBoxLayout(formatGroup);

    _reformatToggle = new QCheckBox(QString::fromUtf8("Enable Reformat (override project format)"));
    formatLayout->addWidget(_reformatToggle);

    _reformatSettingsButton = new QPushButton(QString::fromUtf8("Reformat Settings..."));
    _reformatSettingsButton->setEnabled(false);
    formatLayout->addWidget(_reformatSettingsButton);

    mainLayout->addWidget(formatGroup);

    // --- Frame Range ---
    QGroupBox* rangeGroup = new QGroupBox(QString::fromUtf8("Frame Range"));
    QFormLayout* rangeLayout = new QFormLayout(rangeGroup);

    _firstFrameSpin = new QSpinBox();
    _firstFrameSpin->setRange(-100000, 100000);
    _firstFrameSpin->setValue(0);
    rangeLayout->addRow(QString::fromUtf8("First Frame:"), _firstFrameSpin);

    _lastFrameSpin = new QSpinBox();
    _lastFrameSpin->setRange(-100000, 100000);
    _lastFrameSpin->setValue(100);
    rangeLayout->addRow(QString::fromUtf8("Last Frame:"), _lastFrameSpin);

    mainLayout->addWidget(rangeGroup);

    // --- Render Button ---
    _renderButton = new QPushButton(QString::fromUtf8("▶ Render"));
    _renderButton->setMinimumHeight(40);
    _renderButton->setStyleSheet(QString::fromUtf8(
        "QPushButton { background-color: #2d7d46; color: white; font-weight: bold; font-size: 14px; border-radius: 4px; }"
        "QPushButton:hover { background-color: #3a9957; }"
        "QPushButton:pressed { background-color: #1f5e33; }"));
    mainLayout->addWidget(_renderButton);

    mainLayout->addStretch();

    // --- Connections ---
    QObject::connect(_browseButton, &QPushButton::clicked, this, &FluxExportPanel::onBrowseClicked);
    QObject::connect(_outputPathEdit, &QLineEdit::editingFinished, this, &FluxExportPanel::onOutputPathChanged);
    QObject::connect(_reformatToggle, &QCheckBox::stateChanged, this, &FluxExportPanel::onReformatToggleChanged);
    QObject::connect(_renderButton, &QPushButton::clicked, this, &FluxExportPanel::onRenderClicked);
    QObject::connect(_writeSettingsButton, &QPushButton::clicked, this, &FluxExportPanel::onAdvancedWriteSettingsClicked);
    QObject::connect(_reformatSettingsButton, &QPushButton::clicked, this, &FluxExportPanel::onAdvancedReformatSettingsClicked);

    // Initialize frame range from project
    Gui* g = getGui();
    if (g && g->getApp()) {
        double first, last;
        g->getApp()->getProject()->getFrameRange(&first, &last);
        _firstFrameSpin->setValue((int)first);
        _lastFrameSpin->setValue((int)last);
    }
}

FluxExportPanel::~FluxExportPanel()
{
}

void
FluxExportPanel::setExportNodes(const NodePtr& reformatNode, const NodePtr& writeNode)
{
    _reformatNode = reformatNode;
    _writeNode = writeNode;

    // Sync UI from node state
    if (_reformatNode) {
        _reformatToggle->setChecked(!_reformatNode->isNodeDisabled());
        _reformatSettingsButton->setEnabled(!_reformatNode->isNodeDisabled());
    }
}

NodePtr
FluxExportPanel::getWriteNode() const
{
    return _writeNode;
}

NodePtr
FluxExportPanel::getReformatNode() const
{
    return _reformatNode;
}

void
FluxExportPanel::onBrowseClicked()
{
    QString filePath = QFileDialog::getSaveFileName(this,
        QString::fromUtf8("Export File"),
        _outputPathEdit->text(),
        QString::fromUtf8("Video (*.mov *.mp4 *.avi *.mkv);;Image Sequence (*.exr *.png *.tiff *.jpg);;All Files (*)"));

    if (!filePath.isEmpty()) {
        _outputPathEdit->setText(filePath);
        onOutputPathChanged();
    }
}

void
FluxExportPanel::onOutputPathChanged()
{
    if (!_writeNode) {
        return;
    }
    std::string path = _outputPathEdit->text().toStdString();
    KnobIPtr filenameKnob = _writeNode->getKnobByName(std::string("filename"));
    if (filenameKnob) {
        KnobStringBasePtr strKnob = std::dynamic_pointer_cast<KnobStringBase>(filenameKnob);
        if (strKnob) {
            strKnob->setValue(path, ViewSpec::all(), 0);
        }
    }
    Q_EMIT outputPathChanged(_outputPathEdit->text());
}

void
FluxExportPanel::onReformatToggleChanged(int state)
{
    if (!_reformatNode) {
        return;
    }
    bool enabled = (state == Qt::Checked);
    _reformatNode->setNodeDisabled(!enabled);
    _reformatSettingsButton->setEnabled(enabled);
}

void
FluxExportPanel::onRenderClicked()
{
    Q_EMIT renderRequested();
}

void
FluxExportPanel::onAdvancedWriteSettingsClicked()
{
    if (!_writeNode) {
        return;
    }
    NodeGuiIPtr nodeGui_i = _writeNode->getNodeGui();
    if (nodeGui_i) {
        NodeGuiPtr nodeGui = std::dynamic_pointer_cast<NodeGui>(nodeGui_i);
        if (nodeGui) {
            nodeGui->setVisibleSettingsPanel(true);
        }
    }
}

void
FluxExportPanel::onAdvancedReformatSettingsClicked()
{
    if (!_reformatNode) {
        return;
    }
    NodeGuiIPtr nodeGui_i = _reformatNode->getNodeGui();
    if (nodeGui_i) {
        NodeGuiPtr nodeGui = std::dynamic_pointer_cast<NodeGui>(nodeGui_i);
        if (nodeGui) {
            nodeGui->setVisibleSettingsPanel(true);
        }
    }
}

NATRON_NAMESPACE_EXIT

NATRON_NAMESPACE_USING
#include "moc_FluxExportPanel.cpp"
