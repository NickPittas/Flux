/* ***** BEGIN LICENSE BLOCK *****
 * Flux — Export/Render Panel
 * (C) 2025 Nick Pittas
 * GPL2 — see LICENSE.txt
 * ***** END LICENSE BLOCK ***** */

#include "Gui/FluxExportPanel.h"

#include "Gui/Gui.h"
#include "Gui/GuiAppInstance.h"
#include "Gui/NodeGui.h"
#include "Gui/NodeSettingsPanel.h"
#include "Gui/DockablePanel.h"
#include "Engine/Node.h"
#include "Engine/EffectInstance.h"
#include "Engine/Project.h"
#include "Engine/Knob.h"
#include "Engine/KnobTypes.h"
#include "Engine/AppInstance.h"

NATRON_NAMESPACE_ENTER

FluxExportPanel::FluxExportPanel(Gui* gui, QWidget* parent)
    : QWidget(parent)
    , PanelWidget(this, gui)
    , _reformatNode()
    , _writeNode()
    , _outputPathEdit(nullptr)
    , _browseButton(nullptr)
    , _writeSettingsContainer(nullptr)
    , _writeScrollArea(nullptr)
    , _reformatGroup(nullptr)
    , _reformatToggle(nullptr)
    , _reformatSettingsContainer(nullptr)
    , _renderButton(nullptr)
{
    setObjectName(QString::fromUtf8("FluxExportPanel"));
    setMinimumWidth(200);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(4);

    // --- Output File ---
    QGroupBox* outputGroup = new QGroupBox(QString::fromUtf8("Output File"));
    QHBoxLayout* pathLayout = new QHBoxLayout(outputGroup);

    _outputPathEdit = new QLineEdit();
    _outputPathEdit->setPlaceholderText(QString::fromUtf8("Click browse to set output file..."));
    pathLayout->addWidget(_outputPathEdit);

    _browseButton = new QPushButton(QString::fromUtf8("Browse"));
    pathLayout->addWidget(_browseButton);

    mainLayout->addWidget(outputGroup);

    // --- Write Node Settings Container ---
    // This will hold the NodeGui's existing settings panel, reparented here
    _writeScrollArea = new QScrollArea();
    _writeScrollArea->setWidgetResizable(true);
    _writeScrollArea->setFrameShape(QFrame::NoFrame);
    _writeSettingsContainer = new QWidget();
    QVBoxLayout* writeContainerLayout = new QVBoxLayout(_writeSettingsContainer);
    writeContainerLayout->setContentsMargins(0, 0, 0, 0);
    writeContainerLayout->setSpacing(0);
    _writeScrollArea->setWidget(_writeSettingsContainer);
    mainLayout->addWidget(_writeScrollArea);

    // --- Format Override ---
    _reformatGroup = new QGroupBox(QString::fromUtf8("Format Override"));
    QVBoxLayout* reformatLayout = new QVBoxLayout(_reformatGroup);

    _reformatToggle = new QCheckBox(QString::fromUtf8("Enable Reformat"));
    reformatLayout->addWidget(_reformatToggle);

    // Reformat settings container — holds the Reformat NodeGui's settings panel
    _reformatSettingsContainer = new QWidget();
    QVBoxLayout* reformatContainerLayout = new QVBoxLayout(_reformatSettingsContainer);
    reformatContainerLayout->setContentsMargins(0, 0, 0, 0);
    reformatContainerLayout->setSpacing(0);
    reformatLayout->addWidget(_reformatSettingsContainer);

    mainLayout->addWidget(_reformatGroup);

    // --- Render Button ---
    _renderButton = new QPushButton(QString::fromUtf8("▶ Render"));
    _renderButton->setMinimumHeight(40);
    _renderButton->setStyleSheet(QString::fromUtf8(
        "QPushButton { background-color: #2d7d46; color: white; font-weight: bold; font-size: 14px; border-radius: 4px; }"
        "QPushButton:hover { background-color: #3a9957; }"
        "QPushButton:pressed { background-color: #1f5e33; }"));
    mainLayout->addWidget(_renderButton);

    // --- Connections ---
    QObject::connect(_browseButton, &QPushButton::clicked, this, &FluxExportPanel::onBrowseClicked);
    QObject::connect(_outputPathEdit, &QLineEdit::editingFinished, this, &FluxExportPanel::onOutputPathChanged);
    QObject::connect(_reformatToggle, &QCheckBox::stateChanged, this, &FluxExportPanel::onReformatToggleChanged);
    QObject::connect(_renderButton, &QPushButton::clicked, this, &FluxExportPanel::onRenderClicked);
}

FluxExportPanel::~FluxExportPanel()
{
    // Reparent panels back so Qt doesn't delete them with us.
    // NodeGui owns the panels — we just borrowed them.
    auto reparentOut = [](const NodePtr& node, QWidget* container) {
        if (!node || !container) {
            return;
        }
        QLayout* layout = container->layout();
        if (!layout) {
            return;
        }

        QLayoutItem* child;
        while ((child = layout->takeAt(0)) != nullptr) {
            if (child->widget()) {
                // Restore header visibility
                NodeSettingsPanel* panel = qobject_cast<NodeSettingsPanel*>(child->widget());
                if (panel) {
                    QWidget* header = panel->getHeaderWidget();
                    if (header) {
                        header->setVisible(true);
                    }
                }
                child->widget()->setParent(nullptr);
                child->widget()->hide();
            }
            delete child;
        }
    };

    reparentOut(_writeNode, _writeSettingsContainer);
    reparentOut(_reformatNode, _reformatSettingsContainer);
}

void
FluxExportPanel::setExportNodes(const NodePtr& reformatNode, const NodePtr& writeNode)
{
    // Fix 3: Clean up previous panels — reparent them out so Qt doesn't delete them
    {
        QLayout* writeLayout = _writeSettingsContainer->layout();
        if (writeLayout) {
            QLayoutItem* child;
            while ((child = writeLayout->takeAt(0)) != nullptr) {
                if (child->widget()) {
                    child->widget()->setParent(nullptr);
                    child->widget()->hide();
                }
                delete child;
            }
        }
        QLayout* reformatLayout = _reformatSettingsContainer->layout();
        if (reformatLayout) {
            QLayoutItem* child;
            while ((child = reformatLayout->takeAt(0)) != nullptr) {
                if (child->widget()) {
                    child->widget()->setParent(nullptr);
                    child->widget()->hide();
                }
                delete child;
            }
        }
    }

    _reformatNode = reformatNode;
    _writeNode = writeNode;

    // --- Embed Write node's existing settings panel ---
    if (_writeNode) {
        // Initialize path field from Write node's filename knob
        KnobIPtr filenameKnob = _writeNode->getKnobByName(std::string("filename"));
        if (filenameKnob) {
            KnobStringBasePtr strKnob = std::dynamic_pointer_cast<KnobStringBase>(filenameKnob);
            if (strKnob) {
                std::string val = strKnob->getValue();
                if (!val.empty()) {
                    _outputPathEdit->setText(QString::fromStdString(val));
                }

                // Fix 5: Two-way sync — knob → edit field
                KnobSignalSlotHandlerPtr handler = filenameKnob->getSignalSlotHandler();
                if (handler) {
                    QObject::connect(
                        handler.get(),
                        &KnobSignalSlotHandler::valueChanged,
                        this,
                        [this, strKnob](ViewSpec, int, int) {
                            if (_outputPathEdit->hasFocus()) {
                                return;
                            }
                            _outputPathEdit->setText(QString::fromStdString(strKnob->getValue()));
                        }
                    );
                }
            }
        }

        // Get the NodeGui and ensure its settings panel is created
        NodeGuiIPtr nodeGui_i = _writeNode->getNodeGui();
        if (nodeGui_i) {
            NodeGuiPtr nodeGui = std::dynamic_pointer_cast<NodeGui>(nodeGui_i);
            if (nodeGui) {
                nodeGui->ensurePanelCreated();

                NodeSettingsPanel* settingsPanel = nodeGui->getSettingPanel();
                if (settingsPanel) {
                    // Fix 1: Use setVisible/show instead of setClosed(false)
                    settingsPanel->setVisible(true);
                    settingsPanel->show();

                    // Fix 2: Hide the DockablePanel header while embedded
                    QWidget* header = settingsPanel->getHeaderWidget();
                    if (header) {
                        header->hide();
                    }

                    // Reparent the panel into our container
                    QVBoxLayout* containerLayout = qobject_cast<QVBoxLayout*>(_writeSettingsContainer->layout());
                    if (containerLayout) {
                        containerLayout->addWidget(settingsPanel);
                    }
                }
            }
        }

        // Sync frame range from project
        syncFrameRangeFromProject();
    }

    // --- Embed Reformat node's existing settings panel ---
    if (_reformatNode) {
        _reformatToggle->setChecked(!_reformatNode->isNodeDisabled());

        // Fix 6: Sync reformat toggle from external disabled state
        KnobIPtr disableKnob = _reformatNode->getKnobByName(kDisableNodeKnobName);
        if (disableKnob) {
            KnobSignalSlotHandlerPtr handler = disableKnob->getSignalSlotHandler();
            if (handler) {
                QObject::connect(
                    handler.get(),
                    &KnobSignalSlotHandler::valueChanged,
                    this,
                    [this](ViewSpec, int, int) {
                        if (!_reformatNode) {
                            return;
                        }
                        _reformatToggle->setChecked(!_reformatNode->isNodeDisabled());
                    }
                );
            }
        }

        NodeGuiIPtr nodeGui_i = _reformatNode->getNodeGui();
        if (nodeGui_i) {
            NodeGuiPtr nodeGui = std::dynamic_pointer_cast<NodeGui>(nodeGui_i);
            if (nodeGui) {
                nodeGui->ensurePanelCreated();

                NodeSettingsPanel* settingsPanel = nodeGui->getSettingPanel();
                if (settingsPanel) {
                    // Fix 1: Use setVisible/show instead of setClosed(false)
                    settingsPanel->setVisible(true);
                    settingsPanel->show();

                    // Fix 2: Hide the DockablePanel header while embedded
                    QWidget* header = settingsPanel->getHeaderWidget();
                    if (header) {
                        header->hide();
                    }

                    QVBoxLayout* containerLayout = qobject_cast<QVBoxLayout*>(_reformatSettingsContainer->layout());
                    if (containerLayout) {
                        containerLayout->addWidget(settingsPanel);
                    }
                }
            }
        }
    }

    // Fix 7: Keep frame range in sync with project changes
    Gui* g = getGui();
    if (g && g->getApp()) {
        ProjectPtr project = g->getApp()->getProject();
        if (project) {
            QObject::connect(
                project.get(),
                SIGNAL(frameRangeChanged(int, int)),
                this,
                SLOT(syncFrameRangeFromProject())
            );
        }
    }
}

void
FluxExportPanel::syncFrameRangeFromProject()
{
    Gui* g = getGui();
    if (!g || !g->getApp() || !_writeNode) {
        return;
    }

    double projFirst, projLast;
    g->getApp()->getProject()->getFrameRange(&projFirst, &projLast);

    KnobIPtr firstKnob = _writeNode->getKnobByName(std::string("firstFrame"));
    KnobIPtr lastKnob = _writeNode->getKnobByName(std::string("lastFrame"));

    if (firstKnob) {
        KnobIntBasePtr k = std::dynamic_pointer_cast<KnobIntBase>(firstKnob);
        if (k && !k->hasModifications()) {
            k->setValue((int)projFirst, ViewSpec::all(), 0);
        }
    }
    if (lastKnob) {
        KnobIntBasePtr k = std::dynamic_pointer_cast<KnobIntBase>(lastKnob);
        if (k && !k->hasModifications()) {
            k->setValue((int)projLast, ViewSpec::all(), 0);
        }
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
    KnobIPtr filenameKnob = _writeNode->getKnobByName(std::string("filename"));
    if (filenameKnob) {
        KnobStringBasePtr strKnob = std::dynamic_pointer_cast<KnobStringBase>(filenameKnob);
        if (strKnob) {
            strKnob->setValue(_outputPathEdit->text().toStdString(), ViewSpec::all(), 0);
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
}

void
FluxExportPanel::onRenderClicked()
{
    Q_EMIT renderRequested();
}

NATRON_NAMESPACE_EXIT

NATRON_NAMESPACE_USING
#include "moc_FluxExportPanel.cpp"
