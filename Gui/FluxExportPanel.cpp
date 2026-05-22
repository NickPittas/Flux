/* ***** BEGIN LICENSE BLOCK *****
 * Flux — Export/Render Panel
 * (C) 2025 Nick Pittas
 * GPL2 — see LICENSE.txt
 * ***** END LICENSE BLOCK ***** */

#include "Gui/FluxExportPanel.h"

#include "Gui/Gui.h"
#include "Gui/GuiAppInstance.h"
#include "Gui/DockablePanel.h"
#include "Gui/NodeGui.h"
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
    , _writeGroup(nullptr)
    , _writeGroupLayout(nullptr)
    , _writeKnobsPanel(nullptr)
    , _reformatGroup(nullptr)
    , _reformatGroupLayout(nullptr)
    , _reformatKnobsPanel(nullptr)
    , _reformatToggle(nullptr)
    , _renderButton(nullptr)
{
    setObjectName(QString::fromUtf8("FluxExportPanel"));
    setMinimumWidth(200);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(6);

    // --- Output File (convenience shortcut) ---
    QGroupBox* outputGroup = new QGroupBox(QString::fromUtf8("Output File"));
    QVBoxLayout* outputLayout = new QVBoxLayout(outputGroup);

    QHBoxLayout* pathLayout = new QHBoxLayout();
    _outputPathEdit = new QLineEdit();
    _outputPathEdit->setPlaceholderText(QString::fromUtf8("Click browse to set output file..."));
    pathLayout->addWidget(_outputPathEdit);
    _browseButton = new QPushButton(QString::fromUtf8("Browse"));
    pathLayout->addWidget(_browseButton);
    outputLayout->addLayout(pathLayout);

    mainLayout->addWidget(outputGroup);

    // --- Output Settings (embedded Write node DockablePanel) ---
    _writeGroup = new QGroupBox(QString::fromUtf8("Output Settings"));
    _writeGroup->setCheckable(true);
    _writeGroup->setChecked(true);
    _writeGroupLayout = new QVBoxLayout(_writeGroup);
    mainLayout->addWidget(_writeGroup);

    // --- Format Override (embedded Reformat DockablePanel) ---
    _reformatGroup = new QGroupBox(QString::fromUtf8("Format Override"));
    _reformatGroup->setCheckable(true);
    _reformatGroup->setChecked(false);
    _reformatGroupLayout = new QVBoxLayout(_reformatGroup);

    _reformatToggle = new QCheckBox(QString::fromUtf8("Enable Reformat"));
    _reformatGroupLayout->addWidget(_reformatToggle);

    mainLayout->addWidget(_reformatGroup);

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
}

FluxExportPanel::~FluxExportPanel()
{
}

void
FluxExportPanel::setExportNodes(const NodePtr& reformatNode, const NodePtr& writeNode)
{
    _reformatNode = reformatNode;
    _writeNode = writeNode;

    // --- Create embedded Write knobs panel ---
    if (_writeNode) {
        EffectInstancePtr effect = _writeNode->getEffectInstance();
        if (effect) {
            // Hide the internal "Node" page — its knobs (disableNode, previewEnabled, etc.)
            // are internal and shouldn't be in the export panel
            KnobIPtr nodePageKnob = _writeNode->getKnobByName(std::string("Node"));
            if (nodePageKnob) {
                KnobPagePtr nodePage = std::dynamic_pointer_cast<KnobPage>(nodePageKnob);
                if (nodePage) {
                    nodePage->setSecret(true);
                }
            }

            _writeKnobsPanel = new DockablePanel(
                getGui(),
                effect.get(),
                nullptr,
                DockablePanel::eHeaderModeNoHeader,
                false,
                QUndoStackPtr()
            );
            // Do NOT call turnOffPages() — we want pages as collapsible groups
            _writeKnobsPanel->initializeKnobs();

            // Extract each tab page into a collapsible QGroupBox
            QList<QTabWidget*> tabs = _writeKnobsPanel->findChildren<QTabWidget*>();
            if (!tabs.isEmpty()) {
                QTabWidget* tabWidget = tabs.first();
                for (int i = tabWidget->count() - 1; i >= 0; --i) {
                    QString pageTitle = tabWidget->tabText(i);

                    // Skip the "Node" page (should already be hidden, but double-check)
                    if (pageTitle == QString::fromUtf8("Node")) {
                        continue;
                    }

                    QWidget* pageWidget = tabWidget->widget(i);
                    tabWidget->removeTab(i);

                    QGroupBox* group = new QGroupBox(pageTitle, _writeGroup);
                    group->setCheckable(true);
                    group->setChecked(true);
                    QVBoxLayout* groupLayout = new QVBoxLayout(group);
                    groupLayout->setContentsMargins(4, 4, 4, 4);
                    groupLayout->addWidget(pageWidget);

                    // Make it collapsible — hide page widget when unchecked
                    QObject::connect(group, &QGroupBox::toggled, pageWidget, &QWidget::setVisible);

                    _writeGroupLayout->addWidget(group);
                }
                tabWidget->hide();
            }

            // Add the panel itself (now mostly empty) so it stays alive as a child
            _writeGroupLayout->addWidget(_writeKnobsPanel);
            _writeKnobsPanel->hide();

            // Sync frame range from project to Write node knobs
            syncFrameRangeFromProject();
        }
    }

    // --- Create embedded Reformat knobs panel ---
    if (_reformatNode) {
        EffectInstancePtr effect = _reformatNode->getEffectInstance();
        if (effect) {
            // Hide the internal "Node" page
            KnobIPtr nodePageKnob = _reformatNode->getKnobByName(std::string("Node"));
            if (nodePageKnob) {
                KnobPagePtr nodePage = std::dynamic_pointer_cast<KnobPage>(nodePageKnob);
                if (nodePage) {
                    nodePage->setSecret(true);
                }
            }

            _reformatKnobsPanel = new DockablePanel(
                getGui(),
                effect.get(),
                nullptr,
                DockablePanel::eHeaderModeNoHeader,
                false,
                QUndoStackPtr()
            );
            _reformatKnobsPanel->initializeKnobs();

            // Extract each tab page into a collapsible QGroupBox
            QList<QTabWidget*> tabs = _reformatKnobsPanel->findChildren<QTabWidget*>();
            if (!tabs.isEmpty()) {
                QTabWidget* tabWidget = tabs.first();
                for (int i = tabWidget->count() - 1; i >= 0; --i) {
                    QString pageTitle = tabWidget->tabText(i);

                    if (pageTitle == QString::fromUtf8("Node")) {
                        continue;
                    }

                    QWidget* pageWidget = tabWidget->widget(i);
                    tabWidget->removeTab(i);

                    QGroupBox* group = new QGroupBox(pageTitle, _reformatGroup);
                    group->setCheckable(true);
                    group->setChecked(true);
                    QVBoxLayout* groupLayout = new QVBoxLayout(group);
                    groupLayout->setContentsMargins(4, 4, 4, 4);
                    groupLayout->addWidget(pageWidget);

                    QObject::connect(group, &QGroupBox::toggled, pageWidget, &QWidget::setVisible);

                    _reformatGroupLayout->addWidget(group);
                }
                tabWidget->hide();
            }

            _reformatGroupLayout->addWidget(_reformatKnobsPanel);
            _reformatKnobsPanel->hide();
        }

        _reformatToggle->setChecked(!_reformatNode->isNodeDisabled());
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
