/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of Natron <https://natrongithub.github.io/>,
 * (C) 2018-2023 The Natron developers
 * (C) 2013-2018 INRIA and Alexandre Gauthier-Foichat
 *
 * Natron is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Natron is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Natron.  If not, see <http://www.gnu.org/licenses/gpl-2.0.html>
 * ***** END LICENSE BLOCK ***** */

// ***** BEGIN PYTHON BLOCK *****
// from <https://docs.python.org/3/c-api/intro.html#include-files>:
// "Since Python may define some pre-processor definitions which affect the standard headers on some systems, you must include Python.h before any standard headers are included."
#include <Python.h>
// ***** END PYTHON BLOCK *****

#include "Gui.h"

#include <cassert>
#include <algorithm>
#include <cmath>
#include <cctype>

#include <stdexcept>
#include <limits>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QtGlobal>
#include <QThread>
#include <QTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcess>
#include <QPointer>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QUuid>

#include <QHBoxLayout>
#include <QDebug>
#include <QGraphicsScene>
#include <QUndoGroup>

#ifdef DEBUG
#include "Global/FloatingPointExceptions.h"
#endif
#include "Engine/Node.h"
#include "Engine/NodeGroup.h"
#include "Engine/NodeSerialization.h"
#include "Engine/Project.h"
#include "Engine/TimeLine.h"
#include "Engine/CreateNodeArgs.h"
#include "Engine/ViewerInstance.h"
#include "Engine/OutputEffectInstance.h"

#include "Gui/AboutWindow.h"
#include "Gui/AutoHideToolBar.h"
#include "Gui/DockablePanel.h"
#include "Gui/CurveEditor.h"
#include "Gui/CurveWidget.h"
#include "Gui/FloatingWidget.h"
#include "Gui/GuiAppInstance.h"
#include "Gui/GuiPrivate.h"
#include "Gui/Histogram.h"
#include "Gui/NodeGraph.h"
#include "Gui/NodeGui.h"
#include "Gui/NodeSettingsPanel.h"
#include "Gui/ProgressPanel.h"
#include "Gui/ProjectGui.h"
#include "Gui/GuiApplicationManager.h"
#include "Gui/Splitter.h"
#include "Gui/TabWidget.h"
#include "Gui/ViewerTab.h"
#include "Gui/ViewerGL.h"

#include "Gui/FluxProjectBin.h"
#include "Gui/FluxTimeline.h"
#include "Gui/FluxEffectsPanel.h"
#include "Gui/FluxExportPanel.h"
#include "Gui/FluxAiPanel.h"
#include "Gui/FluxTextAnimatorModel.h"
#include "Gui/FluxTextAnimatorPanel.h"
#include "Gui/FluxTextPanel.h"
#include "Gui/FluxT074Harness.h"
#include "Gui/FluxT079Harness.h"
#include "Gui/FluxMaskUtils.h"
#include "Gui/FluxNodegraphTimelineSync.h"
#include "Gui/DopeSheetEditor.h"
#include "Gui/PropertiesBinWrapper.h"

#include "Engine/EffectInstance.h"
#include "Engine/OutputEffectInstance.h"
#include "Engine/Knob.h"
#include "Engine/KnobTypes.h"
#include "Engine/KnobFile.h"
#include "Engine/ImagePlaneDesc.h"


NATRON_NAMESPACE_ENTER

namespace {

static const char* kFluxAiWorkViewerLabel = "Flux AI Work Viewer";
static const char* kFluxAiWorkViewerScriptName = "fluxAiWorkViewer";

std::string
fluxTextFontFamilyFromChoice(const ChoiceOption& entry)
{
    std::string family = entry.label.empty() ? entry.id : entry.label;
    std::string::size_type slash = family.rfind('/');
    if (slash != std::string::npos && slash + 1 < family.size()) {
        family = family.substr(slash + 1);
    }
    return family;
}

void
syncFluxTextFontChoiceToFont(const NodePtr& gizmoNode)
{
    if (!gizmoNode) {
        return;
    }

    KnobChoicePtr fontChoice = std::dynamic_pointer_cast<KnobChoice>(gizmoNode->getKnobByName("Text1name"));
    KnobStringBasePtr fontString = std::dynamic_pointer_cast<KnobStringBase>(gizmoNode->getKnobByName("Text1font"));
    if (!fontChoice || !fontString) {
        return;
    }

    std::string family = fluxTextFontFamilyFromChoice(fontChoice->getActiveEntry());
    if (family.empty()) {
        return;
    }

    if (fontString->getValue(0, ViewSpec::current()) == family) {
        return;
    }

    fontString->setValue(family, ViewSpec::all(), 0);
}

void
connectAIPaintPromptStoreRefresh(const NodePtr& aiPaintNode, FluxAiPanel* aiPanel)
{
    static std::vector<KnobSignalSlotHandler*> connectedAIPaintControls;

    if (!aiPanel) {
        return;
    }

    for (KnobSignalSlotHandler* previousHandler : connectedAIPaintControls) {
        if (previousHandler) {
            QObject::disconnect(previousHandler, &KnobSignalSlotHandler::valueChanged, aiPanel, nullptr);
        }
    }
    connectedAIPaintControls.clear();

    if (!aiPaintNode) {
        return;
    }

    KnobIPtr promptStore = aiPaintNode->getKnobByName("aiPaintPromptStore");
    if (promptStore) {
        KnobSignalSlotHandler* handler = promptStore->getSignalSlotHandler().get();
        if (handler) {
            QObject::connect(
                handler,
                &KnobSignalSlotHandler::valueChanged,
                aiPanel,
                [aiPanel](ViewSpec, int, int) {
                    QMetaObject::invokeMethod(aiPanel, "refreshAIPaintPromptState", Qt::QueuedConnection);
                }
            );
            connectedAIPaintControls.push_back(handler);
        }
    }

    KnobIPtr loadSam3 = aiPaintNode->getKnobByName("aiPaintLoadSam3");
    if (loadSam3) {
        KnobSignalSlotHandler* handler = loadSam3->getSignalSlotHandler().get();
        if (handler) {
            QObject::connect(handler, &KnobSignalSlotHandler::valueChanged, aiPanel, [aiPanel](ViewSpec, int, int) {
                QMetaObject::invokeMethod(aiPanel, "requestAIPaintSam3Load", Qt::QueuedConnection);
            });
            connectedAIPaintControls.push_back(handler);
        }
    }

    KnobIPtr unloadSam3 = aiPaintNode->getKnobByName("aiPaintUnloadSam3");
    if (unloadSam3) {
        KnobSignalSlotHandler* handler = unloadSam3->getSignalSlotHandler().get();
        if (handler) {
            QObject::connect(handler, &KnobSignalSlotHandler::valueChanged, aiPanel, [aiPanel](ViewSpec, int, int) {
                QMetaObject::invokeMethod(aiPanel, "requestAIPaintSam3Unload", Qt::QueuedConnection);
            });
            connectedAIPaintControls.push_back(handler);
        }
    }

    KnobIPtr livePreview = aiPaintNode->getKnobByName("aiPaintLivePreview");
    if (livePreview) {
        KnobSignalSlotHandler* handler = livePreview->getSignalSlotHandler().get();
        if (handler) {
            QObject::connect(handler, &KnobSignalSlotHandler::valueChanged, aiPanel, [aiPanel, livePreview](ViewSpec, int, int) {
                KnobButton* liveButton = dynamic_cast<KnobButton*>(livePreview.get());
                const bool enabled = liveButton && liveButton->getValue();
                QMetaObject::invokeMethod(aiPanel, "setAIPaintLivePreviewEnabled", Qt::QueuedConnection, Q_ARG(bool, enabled));
            });
            connectedAIPaintControls.push_back(handler);
        }
    }
}

void
installFluxTextFontSync(const NodePtr& gizmoNode, QObject* receiver)
{
    if (!gizmoNode || !receiver) {
        return;
    }

    KnobChoicePtr fontChoice = std::dynamic_pointer_cast<KnobChoice>(gizmoNode->getKnobByName("Text1name"));
    if (!fontChoice) {
        return;
    }

    KnobSignalSlotHandler* handler = fontChoice->getSignalSlotHandler().get();
    if (!handler) {
        return;
    }

    QObject::disconnect(handler, &KnobSignalSlotHandler::valueChanged, receiver, nullptr);
    QObject::connect(
        handler,
        &KnobSignalSlotHandler::valueChanged,
        receiver,
        [gizmoNode](ViewSpec, int, int) {
            syncFluxTextFontChoiceToFont(gizmoNode);
        }
    );

    syncFluxTextFontChoiceToFont(gizmoNode);
}

void
ensureFluxNodeRegisteredWithAnimationEditors(Gui* gui, const NodePtr& node)
{
    if (!gui || !node) {
        return;
    }

    NodeGraph* graph = gui->getNodeGraph();
    if (!graph) {
        return;
    }

    const NodesGuiList& nodeGuis = graph->getAllActiveNodes();
    for (NodesGuiList::const_iterator it = nodeGuis.begin(); it != nodeGuis.end(); ++it) {
        const NodeGuiPtr& nodeGui = *it;
        if (nodeGui && nodeGui->getNode() == node) {
            // Create the panel hidden/minimized so Natron registers KnobGui state
            // with CurveEditor/DopeSheet, but do not filter out unmodified knobs:
            // the visible Flux properties panel must still show the full layer UI.
            nodeGui->ensurePanelCreated(true, false);
            return;
        }
    }
}

} // namespace

bool
Gui::isFluxAiWorkViewerNode(const NodePtr& node) const
{
    if (!node || !node->isActivated()) {
        return false;
    }
    if (node->getPluginID() != PLUGINID_NATRON_VIEWER) {
        return false;
    }
    return node->getLabel() == kFluxAiWorkViewerLabel ||
           node->getScriptName() == kFluxAiWorkViewerScriptName;
}

ViewerTab*
Gui::getFluxMainCompositingViewerTab() const
{
    QMutexLocker l(&_imp->_viewerTabsMutex);
    for (std::list<ViewerTab*>::const_iterator it = _imp->_viewerTabs.begin(); it != _imp->_viewerTabs.end(); ++it) {
        ViewerTab* tab = *it;
        if (!tab) {
            continue;
        }
        ViewerInstance* internalViewer = tab->getInternalNode();
        NodePtr viewerNode = internalViewer ? internalViewer->getNode() : NodePtr();
        if (viewerNode && viewerNode->isActivated() && !isFluxAiWorkViewerNode(viewerNode)) {
            return tab;
        }
    }
    return 0;
}

void
Gui::showFluxViewerTab(ViewerTab* viewerTab)
{
    if (!viewerTab) {
        return;
    }
    setActiveViewer(viewerTab);
    TabWidget* viewerPane = viewerTab->getParentPane();
    if (!viewerPane) {
        return;
    }
    const int count = viewerPane->count();
    for (int t = 0; t < count; ++t) {
        if (viewerPane->tabAt(t) == viewerTab) {
            viewerPane->makeCurrentTab(t);
            break;
        }
    }
}

ViewerTab*
Gui::ensureFluxAiWorkViewerTab(NodePtr* viewerNodeOut)
{
    if (viewerNodeOut) {
        *viewerNodeOut = NodePtr();
    }

    NodePtr matchedNode;
    {
        QMutexLocker l(&_imp->_viewerTabsMutex);
        for (std::list<ViewerTab*>::iterator it = _imp->_viewerTabs.begin(); it != _imp->_viewerTabs.end(); ++it) {
            ViewerTab* tab = *it;
            ViewerInstance* internalViewer = tab ? tab->getInternalNode() : 0;
            NodePtr viewerNode = internalViewer ? internalViewer->getNode() : NodePtr();
            if (isFluxAiWorkViewerNode(viewerNode)) {
                if (viewerNodeOut) {
                    *viewerNodeOut = viewerNode;
                }
                return tab;
            }
        }
    }

    NodeCollectionPtr collection;
    if (getApp()) {
        collection = std::dynamic_pointer_cast<NodeCollection>(getApp()->getProject());
    }
    if (collection) {
        NodesList nodes;
        collection->getActiveNodes(&nodes);
        for (NodesList::const_iterator it = nodes.begin(); it != nodes.end(); ++it) {
            if (isFluxAiWorkViewerNode(*it)) {
                matchedNode = *it;
                break;
            }
        }
    }

    if (!matchedNode) {
        NodeGraph* graph = _imp->_lastFocusedGraph ? _imp->_lastFocusedGraph : _imp->_nodeGraphArea;
        NodeCollectionPtr group = graph ? graph->getGroup() : collection;
        if (!group || !getApp()) {
            return 0;
        }
        CreateNodeArgs args(PLUGINID_NATRON_VIEWER, group);
        args.setProperty<bool>(kCreateNodeArgsPropAutoConnect, false);
        args.setProperty<bool>(kCreateNodeArgsPropAddUndoRedoCommand, false);
        args.setProperty<bool>(kCreateNodeArgsPropSettingsOpened, false);
        matchedNode = getApp()->createNode(args);
        if (!matchedNode || !matchedNode->isActivated()) {
            return 0;
        }
        matchedNode->setLabel(kFluxAiWorkViewerLabel);
        try {
            matchedNode->setScriptName(kFluxAiWorkViewerScriptName);
        } catch (...) {
            // Label remains the stable Flux-owned identity if the script name is already taken.
        }
    }

    {
        QMutexLocker l(&_imp->_viewerTabsMutex);
        for (std::list<ViewerTab*>::iterator it = _imp->_viewerTabs.begin(); it != _imp->_viewerTabs.end(); ++it) {
            ViewerTab* tab = *it;
            ViewerInstance* internalViewer = tab ? tab->getInternalNode() : 0;
            NodePtr viewerNode = internalViewer ? internalViewer->getNode() : NodePtr();
            if (viewerNode == matchedNode) {
                if (viewerNodeOut) {
                    *viewerNodeOut = viewerNode;
                }
                return tab;
            }
        }
    }

    if (viewerNodeOut) {
        *viewerNodeOut = matchedNode;
    }
    return 0;
}


void
Gui::setupUi()
{
    onProjectNameChanged(QString(), false);

    setMouseTracking(true);
    installEventFilter(this);
    assert( !isFullScreen() );

    //Gui::loadStyleSheet();

    ///Restores position, size of the main window as well as whether it was fullscreen or not.
    _imp->restoreGuiGeometry();


    _imp->_undoStacksGroup = new QUndoGroup;
    QObject::connect( _imp->_undoStacksGroup, SIGNAL(activeStackChanged(QUndoStack*)), this, SLOT(onCurrentUndoStackChanged(QUndoStack*)) );

    createMenuActions();

    /*CENTRAL AREA*/
    //======================
    _imp->_centralWidget = new QWidget(this);
    setCentralWidget(_imp->_centralWidget);
    _imp->_mainLayout = new QHBoxLayout(_imp->_centralWidget);
    _imp->_mainLayout->setContentsMargins(0, 0, 0, 0);
    _imp->_centralWidget->setLayout(_imp->_mainLayout);

    _imp->_leftRightSplitter = new Splitter(_imp->_centralWidget);
    _imp->_leftRightSplitter->setChildrenCollapsible(false);
    _imp->_leftRightSplitter->setObjectName( QString::fromUtf8(kMainSplitterObjectName) );
    _imp->_splitters.push_back(_imp->_leftRightSplitter);
    _imp->_leftRightSplitter->setOrientation(Qt::Horizontal);
    _imp->_leftRightSplitter->setContentsMargins(0, 0, 0, 0);


    _imp->_toolBox = new AutoHideToolBar(this, _imp->_leftRightSplitter);
    _imp->_toolBox->setToolButtonStyle(Qt::ToolButtonIconOnly);
    _imp->_toolBox->setOrientation(Qt::Vertical);
    _imp->_toolBox->setMaximumWidth( TO_DPIX(NATRON_TOOL_BUTTON_SIZE) );

    if (_imp->leftToolBarDisplayedOnHoverOnly) {
        _imp->refreshLeftToolBarVisibility( mapFromGlobal( QCursor::pos() ) );
    }

    _imp->_leftRightSplitter->addWidget(_imp->_toolBox);

    _imp->_mainLayout->addWidget(_imp->_leftRightSplitter);

    _imp->createNodeGraphGui();
    _imp->createCurveEditorGui();
    _imp->createDopeSheetGui();
    _imp->createScriptEditorGui();
    _imp->createProgressPanelGui();
    ///Must be absolutely called once _nodeGraphArea has been initialized.
    _imp->createPropertiesBinGui();

    ProjectPtr project = getApp()->getProject();

    _imp->_projectGui = new ProjectGui(this);
    _imp->_projectGui->create(project,
                              _imp->_layoutPropertiesBin,
                              this);

    _imp->_errorLog = new LogWindow(0);
    _imp->_errorLog->hide();

    if (sFluxMode) {
        setupFluxUi();
    } else {
        createDefaultLayoutInternal(false);
    }


    initProjectGuiKnobs();


    setVisibleProjectSettingsPanel();

    _imp->_aboutWindow = new AboutWindow(this);
    _imp->_aboutWindow->hide();


    //the same action also clears the ofx plugins caches, they are not the same cache but are used to the same end

    QObject::connect( project.get(), SIGNAL(projectNameChanged(QString,bool)), this, SLOT(onProjectNameChanged(QString,bool)) );
    TimeLinePtr timeline = project->getTimeLine();
    QObject::connect( timeline.get(), SIGNAL(frameChanged(SequenceTime,int)), this, SLOT(renderViewersAndRefreshKnobsAfterTimelineTimeChange(SequenceTime,int)) );
    QObject::connect( timeline.get(), SIGNAL(frameAboutToChange()), this, SLOT(onTimelineTimeAboutToChange()) );

    /*Searches recursively for all child objects of the given object,
       and connects matching signals from them to slots of object that follow the following form:

        void on_<object name>_<signal name>(<signal parameters>);

       Let's assume our object has a child object of type QPushButton with the object name button1.
       The slot to catch the button's clicked() signal would be:

       void on_button1_clicked();

       If object itself has a properly set object name, its own signals are also connected to its respective slots.
     */
    QMetaObject::connectSlotsByName(this);

    {
#ifdef DEBUG
        boost_adaptbx::floating_point::exception_trapping trap(0);
#endif
        appPTR->setOFXHostHandle( getApp()->getOfxHostOSHandle() );
    }
} // setupUi

void
Gui::onPropertiesScrolled()
{
#ifdef __NATRON_WIN32__
    //On Windows Qt 4.8.6 has a bug where the viewport of the scrollarea gets scrolled outside the bounding rect of the QScrollArea and overlaps all widgets inheriting QGLWidget.
    //The only thing I could think of was to repaint all GL widgets manually...

    {
        QMutexLocker k(&_imp->_viewerTabsMutex);
        for (std::list<ViewerTab*>::iterator it = _imp->_viewerTabs.begin(); it != _imp->_viewerTabs.end(); ++it) {
            (*it)->redrawGLWidgets();
        }
    }
    _imp->_curveEditor->getCurveWidget()->update();

    {
        QMutexLocker k (&_imp->_histogramsMutex);
        for (std::list<Histogram*>::iterator it = _imp->_histograms.begin(); it != _imp->_histograms.end(); ++it) {
            (*it)->update();
        }
    }
#endif
}

void
Gui::updateAboutWindowLibrariesVersion()
{
    if (_imp->_aboutWindow) {
        _imp->_aboutWindow->updateLibrariesVersions();
    }
}

void
Gui::createGroupGui(const NodePtr & group,
                    const CreateNodeArgs& args)
{
    NodeGroupPtr isGrp = std::dynamic_pointer_cast<NodeGroup>( group->getEffectInstance()->shared_from_this() );

    assert(isGrp);
    NodeCollectionPtr collection = std::dynamic_pointer_cast<NodeCollection>(isGrp);
    assert(collection);

    TabWidget* where = 0;
    if (_imp->_lastFocusedGraph) {
        TabWidget* isTab = dynamic_cast<TabWidget*>( _imp->_lastFocusedGraph->parentWidget() );
        if (isTab) {
            where = isTab;
        } else {
            QMutexLocker k(&_imp->_panesMutex);
            assert( !_imp->_panes.empty() );
            where = _imp->_panes.front();
        }
    }

    QGraphicsScene* scene = new QGraphicsScene(this);
    scene->setItemIndexMethod(QGraphicsScene::NoIndex);
    NodeGraph* nodeGraph = new NodeGraph(this, collection, scene, this);
    nodeGraph->setObjectName( QString::fromUtf8( group->getLabel().c_str() ) );
    _imp->_groups.push_back(nodeGraph);
    
    NodeSerializationPtr serialization = args.getProperty<NodeSerializationPtr>(kCreateNodeArgsPropNodeSerialization);

    if ( where && !serialization && !getApp()->isCreatingPythonGroup() ) {
        where->appendTab(nodeGraph, nodeGraph);
        QTimer::singleShot( 25, nodeGraph, SLOT(centerOnAllNodes()) );
    } else {
        nodeGraph->setVisible(false);
    }
}

void
Gui::addGroupGui(NodeGraph* tab,
                 TabWidget* where)
{
    assert(tab);
    assert(where);
    {
        std::list<NodeGraph*>::iterator it = std::find(_imp->_groups.begin(), _imp->_groups.end(), tab);
        if ( it == _imp->_groups.end() ) {
            _imp->_groups.push_back(tab);
        }
    }
    where->appendTab(tab, tab);
}

void
Gui::removeGroupGui(NodeGraph* tab,
                    bool deleteData)
{
    tab->hide();

    if (_imp->_lastFocusedGraph == tab) {
        _imp->_lastFocusedGraph = 0;
    }
    TabWidget* container = dynamic_cast<TabWidget*>( tab->parentWidget() );
    if (container) {
        container->removeTab(tab, true);
    }

    if (deleteData) {
        std::list<NodeGraph*>::iterator it = std::find(_imp->_groups.begin(), _imp->_groups.end(), tab);
        if ( it != _imp->_groups.end() ) {
            _imp->_groups.erase(it);
        }

        unregisterTab(tab);
        tab->deleteLater();
    }
}

void
Gui::setLastSelectedGraph(NodeGraph* graph)
{
    assert( QThread::currentThread() == qApp->thread() );
    _imp->_lastFocusedGraph = graph;
}

NodeGraph*
Gui::getLastSelectedGraph() const
{
    assert( QThread::currentThread() == qApp->thread() );

    return _imp->_lastFocusedGraph;
}

void
Gui::setActiveViewer(ViewerTab* viewer)
{
    assert( QThread::currentThread() == qApp->thread() );
    _imp->_activeViewer = viewer;
}

ViewerTab*
Gui::getActiveViewer() const
{
    assert( QThread::currentThread() == qApp->thread() );

    return _imp->_activeViewer;
}

static QString
fluxSam3A1SanitizedName(const QString& name)
{
    QString out = name;
    out.replace(QRegularExpression(QString::fromUtf8("[^A-Za-z0-9_.-]+")), QString::fromUtf8("_"));
    if (out.isEmpty()) {
        out = QString::fromUtf8("source");
    }
    return out.left(80);
}

static QString
fluxSam3A1BoundedProcessText(const QByteArray& text)
{
    QString out = QString::fromLocal8Bit(text).trimmed();
    out.replace(QLatin1Char('\n'), QLatin1Char(' '));
    out.replace(QLatin1Char('\r'), QLatin1Char(' '));
    return out.left(500);
}

static bool
fluxSam3A1IsVideoExtension(const QString& extension)
{
    const QString ext = extension.toLower();
    return ext == QString::fromUtf8("mov") || ext == QString::fromUtf8("mp4") ||
           ext == QString::fromUtf8("mxf") || ext == QString::fromUtf8("avi") ||
           ext == QString::fromUtf8("mkv") || ext == QString::fromUtf8("webm") ||
           ext == QString::fromUtf8("m4v");
}

static bool
fluxSam3A1RunFfmpeg(const QString& ffmpegPath, const QStringList& arguments, int* exitCode, QString* diagnostics)
{
    QProcess process;
    process.setProgram(ffmpegPath);
    process.setArguments(arguments);
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start();
    if (!process.waitForStarted()) {
        if (diagnostics) {
            *diagnostics = process.errorString();
        }
        if (exitCode) {
            *exitCode = -1;
        }
        return false;
    }
    process.waitForFinished(-1);
    if (exitCode) {
        *exitCode = process.exitCode();
    }
    if (diagnostics) {
        *diagnostics = fluxSam3A1BoundedProcessText(process.readAllStandardError());
        const QString stdoutText = fluxSam3A1BoundedProcessText(process.readAllStandardOutput());
        if (diagnostics->isEmpty()) {
            *diagnostics = stdoutText;
        } else if (!stdoutText.isEmpty()) {
            *diagnostics += QString::fromUtf8(" stdout=") + stdoutText;
        }
    }
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

QString
Gui::exportFluxSam3SourceFrameForSelectedLayer(QJsonObject* sourceMetadata)
{
    if (!sFluxMode || !_imp || !_imp->_fluxTimeline || !getApp()) {
        fprintf(stderr, "FLUX-SAM3-A1 capture blocked: Flux mode/timeline/app unavailable\n");
        return QString();
    }

    FluxTimeline* timeline = _imp->_fluxTimeline;
    const int layerIndex = timeline->getSelectedLayerIndex();
    const QList<FluxLayer>& layers = timeline->getLayers();
    if (layerIndex < 0 || layerIndex >= layers.size()) {
        fprintf(stderr, "FLUX-SAM3-A1 capture blocked: invalid selected layer index %d\n", layerIndex);
        return QString();
    }

    const FluxLayer& layer = layers[layerIndex];
    const int timelineFrame = timeline->getCurrentFrame();
    const int sourceFrame = qBound(layer.originalFirstFrame, timelineFrame - layer.timeOffset, layer.originalLastFrame);
    QString diagnostics;
    const QString result = exportFluxSam3SourceFrameForSourceContext(layerIndex, layer.name, layer.filePath, layer.readerNode,
                                                                    layer.readerNode ? QString::fromStdString(layer.readerNode->getLabel()) : QString(),
                                                                    timelineFrame, sourceFrame, sourceMetadata, &diagnostics);
    if (result.isEmpty() && !diagnostics.isEmpty()) {
        fprintf(stderr, "FLUX-SAM3-A1 capture blocked: %s\n", diagnostics.toStdString().c_str());
    }
    return result;
}

QString
Gui::exportFluxSam3SourceFrameForSourceContext(int layerIndex, const QString& layerName, const QString& filePath, const NodePtr& readerNode, const QString& readerLabel, int timelineFrame, int sourceFrame, QJsonObject* sourceMetadata, QString* diagnostics)
{
    auto fail = [diagnostics](const QString& text) -> QString {
        if (diagnostics) {
            *diagnostics = text;
        }
        fprintf(stderr, "FLUX-SAM3-A1 capture blocked: %s\n", text.toStdString().c_str());
        return QString();
    };

    if (!sFluxMode || !_imp || !_imp->_fluxTimeline || !getApp()) {
        return fail(QString::fromUtf8("source-frame export unavailable: Flux mode, timeline, or app is unavailable"));
    }
    if (layerIndex < 0) {
        return fail(QString::fromUtf8("source-frame export unavailable: stored source layer index is invalid"));
    }
    const QList<FluxLayer>& layers = _imp->_fluxTimeline->getLayers();
    if (layerIndex >= layers.size()) {
        return fail(QString::fromUtf8("source-frame export unavailable: stored source layer index no longer exists"));
    }
    const FluxLayer& layer = layers[layerIndex];
    if (layer.type != QString::fromUtf8("footage")) {
        return fail(QString::fromUtf8("source-frame export unavailable: stored source layer is not footage"));
    }
    if (!readerNode || !readerNode->isActivated()) {
        return fail(QString::fromUtf8("source-frame export unavailable: stored reader node is missing or deactivated"));
    }
    if (layer.readerNode && layer.readerNode != readerNode) {
        return fail(QString::fromUtf8("source-frame export unavailable: stored reader no longer matches the layer reader"));
    }

    const QString effectiveLayerName = layerName.isEmpty() ? layer.name : layerName;
    const QString effectiveFilePath = filePath.isEmpty() ? layer.filePath : filePath;
    const QString effectiveReaderLabel = readerLabel.isEmpty() ? QString::fromStdString(readerNode->getLabel()) : readerLabel;
    const int originalFirstFrame = layer.originalFirstFrame;
    const int originalLastFrame = layer.originalLastFrame;
    int exportSourceFrame = sourceFrame;
    int exportTimelineFrame = timelineFrame;
    if (exportSourceFrame < originalFirstFrame || exportSourceFrame > originalLastFrame) {
        const int clamped = qBound(originalFirstFrame, exportSourceFrame, originalLastFrame);
        fprintf(stderr,
                "FLUX-SAM3-A1 source-frame context clamped: requested=%d clamped=%d originalRange=[%d,%d]\n",
                exportSourceFrame, clamped, originalFirstFrame, originalLastFrame);
        exportSourceFrame = clamped;
        exportTimelineFrame = exportSourceFrame + layer.timeOffset;
    }
    const int timeOffset = exportTimelineFrame - exportSourceFrame;

    const QFileInfo sourceInfo(effectiveFilePath);
    if (effectiveFilePath.isEmpty()) {
        return fail(QString::fromUtf8("source-frame export unavailable: stored source file path is empty"));
    }
    if (!sourceInfo.exists() || !sourceInfo.isFile()) {
        return fail(QString::fromUtf8("source-frame export unavailable: stored source file is missing: %1").arg(effectiveFilePath));
    }

    const QString previewDirPath = QDir::temp().filePath(QString::fromUtf8("Flux/sam3_preview"));
    QDir previewDir;
    if (!previewDir.mkpath(previewDirPath)) {
        return fail(QString::fromUtf8("source-frame export unavailable: could not create temporary directory %1").arg(previewDirPath));
    }

    const QString safeName = fluxSam3A1SanitizedName(effectiveLayerName.isEmpty() ? effectiveReaderLabel : effectiveLayerName);
    const QString unique = QUuid::createUuid().toString(QUuid::Id128).left(12);
    const QString exportTarget = QString::fromUtf8("reader_source");
    const QString outputPath = QDir(previewDirPath).filePath(QString::fromUtf8("source_%1_%2_%3_%4.png").arg(safeName).arg(exportSourceFrame).arg(exportTarget).arg(unique));
    const QString extension = sourceInfo.suffix().toLower();
    const bool isPng = extension == QString::fromUtf8("png");
    const bool isVideo = fluxSam3A1IsVideoExtension(extension);
    const int zeroBasedFrame = exportSourceFrame - originalFirstFrame;
    if (zeroBasedFrame < 0) {
        return fail(QString::fromUtf8("source-frame export unavailable: computed negative zero-based source frame"));
    }

    QString renderMode;
    if (isPng && !isVideo) {
        QFile::remove(outputPath);
        renderMode = QString::fromUtf8("source-png-copy");
        if (!QFile::copy(effectiveFilePath, outputPath)) {
            return fail(QString::fromUtf8("source-frame export failed: could not copy PNG source to %1").arg(outputPath));
        }
    } else {
        const QString ffmpegPath = QStandardPaths::findExecutable(QString::fromUtf8("ffmpeg"));
        if (ffmpegPath.isEmpty()) {
            return fail(QString::fromUtf8("source-frame export failed: ffmpeg executable was not found"));
        }
        QFile::remove(outputPath);
        QStringList arguments;
        arguments << QString::fromUtf8("-y") << QString::fromUtf8("-hide_banner") << QString::fromUtf8("-loglevel") << QString::fromUtf8("error")
                  << QString::fromUtf8("-i") << effectiveFilePath;
        if (isVideo) {
            arguments << QString::fromUtf8("-vf") << QString::fromUtf8("select=eq(n\\,%1)").arg(zeroBasedFrame);
        }
        arguments << QString::fromUtf8("-frames:v") << QString::fromUtf8("1") << outputPath;
        int ffmpegExitCode = -1;
        QString ffmpegDiagnostics;
        const bool processOk = fluxSam3A1RunFfmpeg(ffmpegPath, arguments, &ffmpegExitCode, &ffmpegDiagnostics);
        const QFileInfo outputInfo(outputPath);
        renderMode = QString::fromUtf8("external-source-ffmpeg");
        if (!processOk || !outputInfo.exists() || outputInfo.size() <= 0) {
            return fail(QString::fromUtf8("source-frame export failed: ffmpeg produced no usable PNG (exit %1, %2)").arg(ffmpegExitCode).arg(ffmpegDiagnostics));
        }
    }

    const QFileInfo outputInfo(outputPath);
    if (!outputInfo.exists() || outputInfo.size() <= 0) {
        return fail(QString::fromUtf8("source-frame export failed: generated PNG is missing or empty: %1").arg(outputPath));
    }
    QImageReader reader(outputPath);
    const QSize imageSize = reader.size();
    if (!imageSize.isValid() || imageSize.width() <= 0 || imageSize.height() <= 0) {
        return fail(QString::fromUtf8("source-frame export failed: could not read exported PNG dimensions for %1 (%2)").arg(outputPath, reader.errorString()));
    }

    if (sourceMetadata) {
        QJsonObject metadata;
        metadata.insert(QString::fromUtf8("selected_layer_index"), layerIndex);
        metadata.insert(QString::fromUtf8("selected_layer_name"), effectiveLayerName);
        metadata.insert(QString::fromUtf8("reader_label"), effectiveReaderLabel);
        metadata.insert(QString::fromUtf8("timeline_frame"), exportTimelineFrame);
        metadata.insert(QString::fromUtf8("source_frame"), exportSourceFrame);
        metadata.insert(QString::fromUtf8("original_first_frame"), originalFirstFrame);
        metadata.insert(QString::fromUtf8("original_last_frame"), originalLastFrame);
        metadata.insert(QString::fromUtf8("trim_source_start"), qBound(originalFirstFrame, layer.inPoint, originalLastFrame));
        metadata.insert(QString::fromUtf8("trim_source_end"), qBound(originalFirstFrame, layer.outPoint, originalLastFrame));
        metadata.insert(QString::fromUtf8("time_offset"), timeOffset);
        metadata.insert(QString::fromUtf8("source_file_basename"), sourceInfo.completeBaseName());
        metadata.insert(QString::fromUtf8("source_file_suffix"), sourceInfo.suffix());
        metadata.insert(QString::fromUtf8("export_target"), exportTarget);
        metadata.insert(QString::fromUtf8("render_mode"), renderMode);
        metadata.insert(QString::fromUtf8("width"), imageSize.width());
        metadata.insert(QString::fromUtf8("height"), imageSize.height());
        metadata.insert(QString::fromUtf8("exported_png_width"), imageSize.width());
        metadata.insert(QString::fromUtf8("exported_png_height"), imageSize.height());
        metadata.insert(QString::fromUtf8("temporary_source_png_basename"), outputInfo.fileName());
        metadata.insert(QString::fromUtf8("temporary_source_png_note"), QString::fromUtf8("Temporary source PNG path is intentionally not persisted."));
        if (std::isfinite(layer.sourceFrameRate) && layer.sourceFrameRate > 0.0) {
            metadata.insert(QString::fromUtf8("source_frame_rate"), layer.sourceFrameRate);
        }
        *sourceMetadata = metadata;
    }
    if (diagnostics) {
        *diagnostics = QString::fromUtf8("source frame exported for stored AI Work Viewer context (%1x%2, target=%3, mode=%4)").arg(imageSize.width()).arg(imageSize.height()).arg(exportTarget, renderMode);
    }
    fprintf(stderr,
            "FLUX-SAM3-A1 capture complete: layer=%d name='%s' reader='%s' timelineFrame=%d sourceFrame=%d originalFirstFrame=%d zeroBasedFrame=%d source='%s' output='%s' exportTarget=%s renderMode=%s dimensions=%dx%d\n",
            layerIndex, effectiveLayerName.toStdString().c_str(), effectiveReaderLabel.toStdString().c_str(), exportTimelineFrame,
            exportSourceFrame, originalFirstFrame, zeroBasedFrame, effectiveFilePath.toStdString().c_str(),
            outputPath.toStdString().c_str(), exportTarget.toStdString().c_str(), renderMode.toStdString().c_str(), imageSize.width(), imageSize.height());
    return outputPath;
}

QString
Gui::exportFluxSam3SourceSequenceForSourceContext(int layerIndex, const QString& layerName, const QString& filePath, const NodePtr& readerNode, const QString& readerLabel, int firstSourceFrame, int lastSourceFrame, QJsonObject* sourceMetadata, QString* diagnostics, std::function<bool(int completed, int total)> progressCallback)
{
    auto fail = [diagnostics](const QString& text) -> QString {
        if (diagnostics) {
            *diagnostics = text;
        }
        fprintf(stderr, "FLUX-SAM3-A1 source-sequence capture blocked: %s\n", text.toStdString().c_str());
        return QString();
    };

    if (!getFluxTimeline()) {
        return fail(QString::fromUtf8("source-sequence export unavailable: missing Flux timeline"));
    }
    const QList<FluxLayer>& layers = getFluxTimeline()->getLayers();
    if (layerIndex < 0 || layerIndex >= layers.size()) {
        return fail(QString::fromUtf8("source-sequence export unavailable: selected source layer index is invalid"));
    }
    const FluxLayer& layer = layers[layerIndex];
    if (layer.type != QString::fromUtf8("footage")) {
        return fail(QString::fromUtf8("source-sequence export unavailable: stored source layer is not footage"));
    }

    const int rangeStart = qMin(firstSourceFrame, lastSourceFrame);
    const int rangeEnd = qMax(firstSourceFrame, lastSourceFrame);
    const int clampedStart = qBound(layer.originalFirstFrame, rangeStart, layer.originalLastFrame);
    const int clampedEnd = qBound(layer.originalFirstFrame, rangeEnd, layer.originalLastFrame);
    if (clampedStart > clampedEnd) {
        return fail(QString::fromUtf8("source-sequence export unavailable: requested range is outside original media range"));
    }

    const QString projectDir = getApp()->getProject()->getProjectPath();
    if (projectDir.isEmpty()) {
        return fail(QString::fromUtf8("source-sequence export unavailable: project must be saved first to determine persistent output path"));
    }
    const QString fileHash = QString::fromLatin1(QCryptographicHash::hash(filePath.toUtf8(), QCryptographicHash::Md5).toHex().left(12));
    const QString fileBasename = QFileInfo(filePath).fileName();
    const QString sequenceDirPath = QDir(projectDir).filePath(QString::fromUtf8("FluxGenerated/AI/.source_sequences/%1/%2").arg(fileHash, fileBasename));
    QDir sequenceDir(sequenceDirPath);
    if (!sequenceDir.mkpath(QStringLiteral("."))) {
        return fail(QString::fromUtf8("source-sequence export unavailable: could not create persistent directory %1").arg(sequenceDirPath));
    }

    QJsonArray frames;
    int width = 0;
    int height = 0;
    const int totalFrames = clampedEnd - clampedStart + 1;
    int completedFrames = 0;
    for (int sourceFrame = clampedStart; sourceFrame <= clampedEnd; ++sourceFrame) {
        const int timelineFrame = sourceFrame + layer.timeOffset;
        QJsonObject frameMetadata;
        QString frameDiagnostics;
        const QString sourcePng = exportFluxSam3SourceFrameForSourceContext(layerIndex, layerName, filePath, readerNode, readerLabel, timelineFrame, sourceFrame, &frameMetadata, &frameDiagnostics);
        QFileInfo sourceInfo(sourcePng);
        if (sourcePng.isEmpty() || !sourceInfo.isFile() || sourceInfo.size() <= 0) {
            return fail(QString::fromUtf8("source-sequence export failed at source frame %1: %2").arg(sourceFrame).arg(frameDiagnostics));
        }
        const QString outputName = QString::fromUtf8("source_%1.png").arg(timelineFrame, 6, 10, QLatin1Char('0'));
        const QString outputPath = sequenceDir.filePath(outputName);
        QFile::remove(outputPath);
        if (!QFile::copy(sourcePng, outputPath)) {
            return fail(QString::fromUtf8("source-sequence export failed: could not copy frame %1 to %2").arg(sourceFrame).arg(outputPath));
        }
        QFileInfo outputInfo(outputPath);
        if (!outputInfo.isFile() || outputInfo.size() <= 0) {
            return fail(QString::fromUtf8("source-sequence export failed: copied frame is missing or empty: %1").arg(outputPath));
        }
        const int frameWidth = frameMetadata.value(QString::fromUtf8("width")).toInt(frameMetadata.value(QString::fromUtf8("exported_png_width")).toInt(0));
        const int frameHeight = frameMetadata.value(QString::fromUtf8("height")).toInt(frameMetadata.value(QString::fromUtf8("exported_png_height")).toInt(0));
        if (frameWidth <= 0 || frameHeight <= 0) {
            return fail(QString::fromUtf8("source-sequence export failed: frame %1 metadata has invalid dimensions").arg(sourceFrame));
        }
        if (width == 0 && height == 0) {
            width = frameWidth;
            height = frameHeight;
        } else if (width != frameWidth || height != frameHeight) {
            return fail(QString::fromUtf8("source-sequence export failed: frame dimensions changed at source frame %1").arg(sourceFrame));
        }

        QJsonObject frame;
        frame.insert(QString::fromUtf8("path"), outputPath);
        frame.insert(QString::fromUtf8("basename"), outputInfo.fileName());
        frame.insert(QString::fromUtf8("source_frame"), sourceFrame);
        frame.insert(QString::fromUtf8("timeline_frame"), timelineFrame);
        frame.insert(QString::fromUtf8("zero_based_source_frame"), sourceFrame - layer.originalFirstFrame);
        frames.append(frame);
        ++completedFrames;
        if (progressCallback) {
            if (!progressCallback(completedFrames, totalFrames)) {
                return fail(QString::fromUtf8("source-sequence export canceled"));
            }
        }
    }

    if (frames.isEmpty()) {
        return fail(QString::fromUtf8("source-sequence export failed: no frames were exported"));
    }

    if (sourceMetadata) {
        QJsonObject metadata;
        metadata.insert(QString::fromUtf8("selected_layer_index"), layerIndex);
        metadata.insert(QString::fromUtf8("selected_layer_name"), layerName.isEmpty() ? layer.name : layerName);
        metadata.insert(QString::fromUtf8("reader_label"), readerLabel.isEmpty() && readerNode ? QString::fromStdString(readerNode->getLabel()) : readerLabel);
        metadata.insert(QString::fromUtf8("source_frame_start"), clampedStart);
        metadata.insert(QString::fromUtf8("source_frame_end"), clampedEnd);
        metadata.insert(QString::fromUtf8("timeline_frame_start"), clampedStart + layer.timeOffset);
        metadata.insert(QString::fromUtf8("timeline_frame_end"), clampedEnd + layer.timeOffset);
        metadata.insert(QString::fromUtf8("duration_frames"), frames.size());
        metadata.insert(QString::fromUtf8("original_first_frame"), layer.originalFirstFrame);
        metadata.insert(QString::fromUtf8("original_last_frame"), layer.originalLastFrame);
        metadata.insert(QString::fromUtf8("trim_source_start"), qBound(layer.originalFirstFrame, layer.inPoint, layer.originalLastFrame));
        metadata.insert(QString::fromUtf8("trim_source_end"), qBound(layer.originalFirstFrame, layer.outPoint, layer.originalLastFrame));
        metadata.insert(QString::fromUtf8("time_offset"), layer.timeOffset);
        metadata.insert(QString::fromUtf8("width"), width);
        metadata.insert(QString::fromUtf8("height"), height);
        metadata.insert(QString::fromUtf8("temporary_source_sequence_dir"), sequenceDirPath);
        metadata.insert(QString::fromUtf8("temporary_source_sequence_note"), QString::fromUtf8("Temporary source PNG sequence path is intentionally not persisted."));
        if (std::isfinite(layer.sourceFrameRate) && layer.sourceFrameRate > 0.0) {
            metadata.insert(QString::fromUtf8("source_frame_rate"), layer.sourceFrameRate);
        }
        metadata.insert(QString::fromUtf8("frames"), frames);
        *sourceMetadata = metadata;
    }
    if (diagnostics) {
        *diagnostics = QString::fromUtf8("source sequence exported for stored AI Work Viewer context (%1 frames, %2x%3)").arg(frames.size()).arg(width).arg(height);
    }
    fprintf(stderr,
            "FLUX-SAM3-A1 sequence capture complete: layer=%d sourceRange=[%d,%d] timelineRange=[%d,%d] frames=%d outputDir='%s'\n",
            layerIndex, clampedStart, clampedEnd, clampedStart + layer.timeOffset, clampedEnd + layer.timeOffset, static_cast<int>(frames.size()), sequenceDirPath.toStdString().c_str());
    return sequenceDirPath;
}

bool
Gui::previewFluxAiResultPngInWorkViewer(const QString& absolutePngPath, QString* diagnostics)
{
    auto fail = [diagnostics](const QString& text) -> bool {
        if (diagnostics) {
            *diagnostics = text;
        }
        fprintf(stderr, "FLUX-SAM3-A1 preview blocked: %s\n", text.toStdString().c_str());
        return false;
    };
    if (!getApp()) {
        return fail(QString::fromUtf8("AI Work Viewer preview unavailable: app is missing"));
    }
    const QFileInfo pngInfo(absolutePngPath);
    if (!pngInfo.isFile() || pngInfo.size() <= 0) {
        return fail(QString::fromUtf8("AI Work Viewer preview unavailable: result PNG is missing or empty: %1").arg(absolutePngPath));
    }
    NodePtr viewerNode;
    ViewerTab* viewerTab = ensureFluxAiWorkViewerTab(&viewerNode);
    if (!viewerTab || !viewerNode || !viewerNode->isActivated()) {
        return fail(QString::fromUtf8("AI Work Viewer preview unavailable: could not create or find Flux AI Work Viewer"));
    }
    NodeCollectionPtr collection;
    if (getApp()->getProject()) {
        collection = std::dynamic_pointer_cast<NodeCollection>(getApp()->getProject());
    }
    if (!collection) {
        return fail(QString::fromUtf8("AI Work Viewer preview unavailable: project collection is missing"));
    }
    std::string filePath = absolutePngPath.toStdString();
    getApp()->getProject()->canonicalizePath(filePath);
    NodePtr readNode = collection->getNodeByName("FluxSAM3LiveResultPreview");
    if (readNode && readNode->isActivated()) {
        KnobIPtr filenameKnob = readNode->getKnobByName("filename");
        KnobFilePtr fileKnob = std::dynamic_pointer_cast<KnobFile>(filenameKnob);
        if (!fileKnob) {
            return fail(QString::fromUtf8("AI Work Viewer preview unavailable: existing result Read has no filename knob"));
        }
        fileKnob->setValue(filePath);
        fileKnob->reloadFile();
    } else {
        CreateNodeArgs readArgs(PLUGINID_NATRON_READ, collection);
        readArgs.setProperty<bool>(kCreateNodeArgsPropAutoConnect, false);
        readArgs.setProperty<bool>(kCreateNodeArgsPropAddUndoRedoCommand, false);
        readArgs.setProperty<bool>(kCreateNodeArgsPropSettingsOpened, false);
        readNode = getApp()->createReader(filePath, readArgs);
        if (!readNode || !readNode->isActivated()) {
            return fail(QString::fromUtf8("AI Work Viewer preview unavailable: could not create result Read node"));
        }
        readNode->setLabel(QString::fromUtf8("Flux SAM3 Result Preview").toStdString());
        try {
            readNode->setScriptName(QString::fromUtf8("FluxSAM3LiveResultPreview").toStdString());
        } catch (...) {
        }
    }
    viewerNode->disconnectInput(0);
    viewerNode->connectInput(readNode, 0);
    if (viewerNode->getInput(0) != readNode) {
        return fail(QString::fromUtf8("AI Work Viewer preview unavailable: could not connect result Read to viewer input 0"));
    }
    showFluxViewerTab(viewerTab);
    redrawAllViewers();
    if (diagnostics) {
        *diagnostics = QString::fromUtf8("SAM3 result preview loaded in Flux AI Work Viewer: %1").arg(pngInfo.fileName());
    }
    return true;
}

NodeCollectionPtr
Gui::getLastSelectedNodeCollection() const
{
    NodeGraph* graph = 0;

    if (_imp->_lastFocusedGraph) {
        graph = _imp->_lastFocusedGraph;
    } else {
        graph = _imp->_nodeGraphArea;
    }
    NodeCollectionPtr group = graph->getGroup();
    assert(group);

    return group;
}

void
Gui::wipeLayout()
{
    std::list<TabWidget*> panesCpy;
    {
        QMutexLocker l(&_imp->_panesMutex);
        panesCpy = _imp->_panes;
        _imp->_panes.clear();
    }
    std::list<FloatingWidget*> floatingWidgets = getFloatingWindows();

    FloatingWidget* projectFW = _imp->_projectGui->getPanel()->getFloatingWindow();
    for (std::list<FloatingWidget*>::const_iterator it = floatingWidgets.begin(); it != floatingWidgets.end(); ++it) {
        if (!projectFW || (*it) != projectFW) {
            (*it)->deleteLater();
        }
    }
    {
        QMutexLocker k(&_imp->_floatingWindowMutex);
        _imp->_floatingWindows.clear();

        // Re-add the project window
        if (projectFW) {
            _imp->_floatingWindows.push_back(projectFW);
        }
    }


    for (std::list<TabWidget*>::iterator it = panesCpy.begin(); it != panesCpy.end(); ++it) {
        ///Conserve tabs by removing them from the tab widgets. This way they will not be deleted.
        while ( (*it)->count() > 0 ) {
            (*it)->removeTab(0, false);
        }
        //(*it)->setParent(NULL);
        (*it)->deleteLater();
    }

    std::list<Splitter*> splittersCpy;
    {
        QMutexLocker l(&_imp->_splittersMutex);
        splittersCpy = _imp->_splitters;
        _imp->_splitters.clear();
    }
    for (std::list<Splitter*>::iterator it = splittersCpy.begin(); it != splittersCpy.end(); ++it) {
        if (_imp->_leftRightSplitter != *it) {
            while ( (*it)->count() > 0 ) {
                (*it)->widget(0)->setParent(NULL);
            }
            //(*it)->setParent(NULL);
            (*it)->deleteLater();
        }
    }


    Splitter *newSplitter = new Splitter(_imp->_centralWidget);
    newSplitter->addWidget(_imp->_toolBox);
    newSplitter->setObjectName_mt_safe( _imp->_leftRightSplitter->objectName_mt_safe() );
    _imp->_mainLayout->removeWidget(_imp->_leftRightSplitter);
    unregisterSplitter(_imp->_leftRightSplitter);
    _imp->_leftRightSplitter->deleteLater();
    _imp->_leftRightSplitter = newSplitter;
    _imp->_leftRightSplitter->setChildrenCollapsible(false);
    _imp->_mainLayout->addWidget(newSplitter);

    {
        QMutexLocker l(&_imp->_splittersMutex);
        _imp->_splitters.push_back(newSplitter);
    }
} // Gui::wipeLayout

void
Gui::setupFluxUi()
{
    // ====================================================================
    // Flux Layout:
    //   Top row:    Project Bin/NodeGraph (22%) | Viewer (53%) | Properties/Export (25%)
    //   Bottom row: Timeline/Dope Sheet/Curve Editor (full width, ~33% height)
    // ====================================================================

    // ====================================================================
    // 1. Create top-left pane as the initial root TabWidget
    // ====================================================================
    TabWidget* topLeftPane = new TabWidget(this, _imp->_leftRightSplitter);
    {
        QMutexLocker l(&_imp->_panesMutex);
        _imp->_panes.push_back(topLeftPane);
    }
    topLeftPane->setObjectName_mt_safe( QString::fromUtf8("fluxTopLeftPane") );
    topLeftPane->setProperty("fluxPaneRole", QString::fromUtf8("projectTabs"));
    topLeftPane->setAsAnchor(true);

    // ====================================================================
    // 2. Split vertically to create full-width bottom workshop pane
    // ====================================================================
    TabWidget* workshopPane = topLeftPane->splitVertically(false);
    workshopPane->setObjectName_mt_safe( QString::fromUtf8("fluxWorkshopPane") );
    workshopPane->setProperty("fluxPaneRole", QString::fromUtf8("timelineTabs"));

    // ====================================================================
    // 3. Split top-left horizontally to create top-center pane
    // ====================================================================
    TabWidget* topCenterPane = topLeftPane->splitHorizontally(false);
    topCenterPane->setObjectName_mt_safe( QString::fromUtf8("fluxTopCenterPane") );
    topCenterPane->setProperty("fluxPaneRole", QString::fromUtf8("viewerTabs"));
    _imp->_fluxViewerPane = topCenterPane;

    // ====================================================================
    // 4. Split top-center horizontally to create top-right pane
    // ====================================================================
    TabWidget* topRightPane = topCenterPane->splitHorizontally(false);
    topRightPane->setObjectName_mt_safe( QString::fromUtf8("fluxTopRightPane") );
    topRightPane->setProperty("fluxPaneRole", QString::fromUtf8("inspectorTabs"));
    _imp->_fluxTopLeftPane = topLeftPane;
    _imp->_fluxTopRightPane = topRightPane;
    _imp->_fluxWorkshopPane = workshopPane;

    // ====================================================================
    // Populate top-left pane: Project Bin + Node Graph (tabs)
    // ====================================================================
    FluxProjectBin* projectBin = new FluxProjectBin(this);
    projectBin->setScriptName("fluxProjectBin");
    projectBin->setLabel( tr("Project Bin").toStdString() );
    TabWidget::moveTab(projectBin, projectBin, topLeftPane);

    if (_imp->_nodeGraphArea) {
        TabWidget::moveTab(_imp->_nodeGraphArea, _imp->_nodeGraphArea, topLeftPane);
    }

    // ====================================================================
    // Populate top-center pane: Viewers + Histograms
    // ====================================================================
    {
        QMutexLocker l(&_imp->_viewerTabsMutex);
        for (std::list<ViewerTab*>::iterator it2 = _imp->_viewerTabs.begin(); it2 != _imp->_viewerTabs.end(); ++it2) {
            TabWidget::moveTab(*it2, *it2, topCenterPane);
        }
    }
    {
        QMutexLocker l(&_imp->_histogramsMutex);
        for (std::list<Histogram*>::iterator it2 = _imp->_histograms.begin(); it2 != _imp->_histograms.end(); ++it2) {
            TabWidget::moveTab(*it2, *it2, topCenterPane);
        }
    }

    // ====================================================================
    // Populate top-right pane: Properties + Export (tabs)
    // ====================================================================
    if (_imp->_propertiesBin) {
        TabWidget::moveTab(_imp->_propertiesBin, _imp->_propertiesBin, topRightPane);
    }

    FluxExportPanel* exportPanel = new FluxExportPanel(this);
    exportPanel->setScriptName("fluxExportPanel");
    exportPanel->setLabel( tr("Export").toStdString() );
    TabWidget::moveTab(exportPanel, exportPanel, topRightPane);

    FluxAiPanel* aiPanel = new FluxAiPanel(this);
    aiPanel->setScriptName("fluxAiPanel");
    aiPanel->setLabel( tr("AI").toStdString() );
    TabWidget::moveTab(aiPanel, aiPanel, topRightPane);

    FluxTextPanel* textPanel = new FluxTextPanel(this);
    textPanel->setScriptName("fluxTextPanel");
    textPanel->setLabel( tr("Text").toStdString() );
    TabWidget::moveTab(textPanel, textPanel, topRightPane);

    FluxTextAnimatorPanel* textAnimatorPanel = new FluxTextAnimatorPanel(this);
    textAnimatorPanel->setScriptName("fluxTextAnimatorPanel");
    textAnimatorPanel->setLabel( tr("Text Animators").toStdString() );
    TabWidget::moveTab(textAnimatorPanel, textAnimatorPanel, topRightPane);

    // ====================================================================
    // Populate workshop pane (bottom): Timeline + Dope Sheet + Curve Editor
    // ====================================================================
    FluxTimeline* timeline = new FluxTimeline(this);
    timeline->setObjectName(QString::fromUtf8("fluxTimeline"));
    timeline->setScriptName("fluxTimeline");
    timeline->setLabel( tr("Timeline").toStdString() );
    TabWidget::moveTab(timeline, timeline, workshopPane);

    // Sync timeline frame range from project
    {
        double pf = 0, pl = 100;
        getApp()->getProject()->getFrameRange(&pf, &pl);
        timeline->setFrameRange((int)pf, (int)pl);

        // Keep in sync when project frame range changes
        QObject::connect(getApp()->getProject().get(), &Project::frameRangeChanged,
                         timeline, &FluxTimeline::setFrameRange);
    }

    if (_imp->_dopeSheetEditor) {
        TabWidget::moveTab(_imp->_dopeSheetEditor, _imp->_dopeSheetEditor, workshopPane);
    }
    if (_imp->_curveEditor) {
        TabWidget::moveTab(_imp->_curveEditor, _imp->_curveEditor, workshopPane);
    }

    // ====================================================================
    // Effects panel: keep internal for signal wiring, not visible in right pane
    // ====================================================================
    FluxEffectsPanel* effectsPanel = new FluxEffectsPanel(this);
    effectsPanel->setScriptName("fluxEffectsPanel");
    effectsPanel->setLabel( tr("Effects").toStdString() );
    effectsPanel->hide(); // not tabbed — timeline now owns effect UX

    // ====================================================================
    // Set pane sizes
    // ====================================================================
    // Top row: Left (22%) | Center (53%) | Right (25%)
    // Nested splitters: topLeftPane parent controls left vs (center+right)
    //                   topCenterPane parent controls center vs right
    const int totalW = width();
    const int leftW = static_cast<int>(totalW * 0.22);
    const int centerW = static_cast<int>(totalW * 0.53);
    const int rightW = static_cast<int>(totalW * 0.25);

    Splitter* topSplitter = dynamic_cast<Splitter*>(topLeftPane->parentWidget());
    if (topSplitter) {
        QList<int> topSizes;
        topSizes << leftW << (centerW + rightW);
        topSplitter->setSizes_mt_safe(topSizes);
    }

    Splitter* topCenterSplitter = dynamic_cast<Splitter*>(topCenterPane->parentWidget());
    if (topCenterSplitter) {
        QList<int> centerSizes;
        centerSizes << centerW << rightW;
        topCenterSplitter->setSizes_mt_safe(centerSizes);
    }

    // Top/bottom: ~67% / ~33%
    const int totalH = height();
    const int topH = static_cast<int>(totalH * 0.67);
    const int bottomH = static_cast<int>(totalH * 0.33);

    Splitter* vertSplitter = dynamic_cast<Splitter*>(workshopPane->parentWidget());
    if (vertSplitter) {
        QList<int> vertSizes;
        vertSizes << topH << bottomH;
        vertSplitter->setSizes_mt_safe(vertSizes);
    }

    // Default current tabs
    topLeftPane->makeCurrentTab(0);      // Project Bin
    topRightPane->makeCurrentTab(0);     // Properties (or Export if no properties)
    workshopPane->makeCurrentTab(0);     // Timeline

    // ====================================================================
    // Signal wiring for Flux widgets
    // ====================================================================

    auto selectedLayerAIPaintNode = [](const FluxLayer& layer) -> NodePtr {
        for (const FluxEffect& effect : layer.effects) {
            if (!effect.enabled || !effect.node || !effect.node->isActivated()) {
                continue;
            }
            EffectInstancePtr effectInstance = effect.node->getEffectInstance();
            if (effectInstance && effectInstance->getPluginID() == PLUGINID_NATRON_AIPAINT) {
                return effect.node;
            }
        }
        return NodePtr();
    };

    auto refreshSelectedLayerAiPaintBinding = [this, timeline, aiPanel, selectedLayerAIPaintNode](bool foregroundAiPane) {
        if (!timeline || !aiPanel) {
            return;
        }
        const QList<FluxLayer>& layers = timeline->getLayers();
        const int layerIndex = timeline->getSelectedLayerIndex();
        if (layerIndex < 0 || layerIndex >= layers.size()) {
            aiPanel->setViewerForCapture(nullptr);
            connectAIPaintPromptStoreRefresh(NodePtr(), aiPanel);
            return;
        }
        const FluxLayer& layer = layers[layerIndex];
        if (layer.type != QString::fromUtf8("footage") || !layer.readerNode || !layer.readerNode->isActivated()) {
            aiPanel->setViewerForCapture(nullptr);
            connectAIPaintPromptStoreRefresh(NodePtr(), aiPanel);
            return;
        }

        const NodePtr aiPaintNode = selectedLayerAIPaintNode(layer);
        NodePtr viewerNode;
        ViewerTab* viewerTab = ensureFluxAiWorkViewerTab(&viewerNode);
        if (!viewerTab || !viewerNode || !viewerNode->isActivated()) {
            aiPanel->setViewerForCapture(nullptr);
            return;
        }

        NodePtr sourceViewNode = aiPaintNode && aiPaintNode->isActivated() ? aiPaintNode : layer.readerNode;
        if (viewerNode->getInput(0) != sourceViewNode) {
            viewerNode->disconnectInput(0);
            viewerNode->connectInput(sourceViewNode, 0);
        }

        const int timelineFrame = timeline->getCurrentFrame();
        const int sourceFrame = qBound(layer.originalFirstFrame, timelineFrame - layer.timeOffset, layer.originalLastFrame);
        const int rangeFirstFrame = qBound(layer.originalFirstFrame, layer.inPoint, layer.originalLastFrame);
        const int rangeLastFrame = qBound(layer.originalFirstFrame, layer.outPoint, layer.originalLastFrame);
        aiPanel->setSourceCaptureContext(viewerTab->getViewer(), viewerNode, layerIndex, layer.name, layer.filePath, layer.readerNode, layer.readerNode ? QString::fromStdString(layer.readerNode->getLabel()) : QString(), aiPaintNode, timelineFrame, sourceFrame, layer.timeOffset, rangeFirstFrame, rangeLastFrame);
        connectAIPaintPromptStoreRefresh(aiPaintNode, aiPanel);

        if (foregroundAiPane) {
            showFluxViewerTab(viewerTab);
            TabWidget* aiPane = aiPanel->getParentPane();
            if (aiPane) {
                const int count = aiPane->count();
                for (int t = 0; t < count; ++t) {
                    if (aiPane->tabAt(t) == aiPanel) {
                        aiPane->makeCurrentTab(t);
                        break;
                    }
                }
            }
        }
        redrawAllViewers();
    };

    // 1. Project Bin: fileRequested → add layer to timeline + rebuild graph
    //    (fires on double-click — import button/drag-to-bin only adds to bin)
    //    The gizmo (with internal Read) is created in rebuildCompositingGraph.
    QObject::connect(projectBin, &FluxProjectBin::fileRequested, this,
                     [this, timeline](const QString& filePath) {
                         if (!getApp()) {
                             return;
                         }
                         QFileInfo fi(filePath);
                         QString name = fi.fileName();
                         timeline->addLayer(name, filePath, QString::fromUtf8("footage"));
                         // addLayer emits compositingChanged → rebuildCompositingGraph
                     });

    // 2. Timeline: layerAddedFromDrop → rebuild compositing graph
    //    The gizmo is created inside rebuildCompositingGraph.
    QObject::connect(timeline, &FluxTimeline::layerAddedFromDrop, this,
                     [this, timeline](QString filePath, int row, int inFrame) {
                         Q_UNUSED(filePath);
                         Q_UNUSED(row);
                         Q_UNUSED(inFrame);
                         // The layer is already added; rebuild creates the gizmo.
                         rebuildCompositingGraph(timeline);
                     });

    // 3. Timeline: layerSelected → open gizmo properties panel + update Effects Panel
    QObject::connect(timeline, &FluxTimeline::layerSelected, this,
                      [this, timeline, effectsPanel, textPanel, textAnimatorPanel, refreshSelectedLayerAiPaintBinding](int index) {
                          // Close previously selected layer's properties panel
                          const QList<FluxLayer>& prevLayers = timeline->getLayers();
                          for (int i = 0; i < prevLayers.size(); ++i) {
                              if (i != index && prevLayers[i].gizmoNode) {
                                  NodeGuiPtr prevNodeGui = std::dynamic_pointer_cast<NodeGui>(prevLayers[i].gizmoNode->getNodeGui());
                                  if (prevNodeGui && prevNodeGui->isSettingsPanelVisible()) {
                                      prevNodeGui->setVisibleSettingsPanel(false);
                                  }
                              }
                          }

                          // Open newly selected layer's properties panel.
                          const QList<FluxLayer>& layers = timeline->getLayers();
                          if (index >= 0 && index < layers.size()) {
                              const FluxLayer& selLayer = layers[index];
                              effectsPanel->setActiveLayer(index, selLayer.name);

                              // Bind text panel for FluxMotionText layers
                              const bool isTextLayer = (selLayer.type == QString::fromUtf8("text"));
                              if (textPanel) {
                                  if (isTextLayer && selLayer.gizmoNode) {
                                      textPanel->setActiveNode(selLayer.gizmoNode);
                                      if (textAnimatorPanel) {
                                          textAnimatorPanel->setActiveNode(selLayer.gizmoNode);
                                      }
                                      // Foreground the Text tab in the top-right pane
                                      TabWidget* pane = textPanel->getParentPane();
                                      if (pane) {
                                          const int count = pane->count();
                                          for (int t = 0; t < count; ++t) {
                                              if (pane->tabAt(t) == textPanel) {
                                                  pane->makeCurrentTab(t);
                                                  break;
                                              }
                                          }
                                      }
                                  } else {
                                      textPanel->setActiveNode(NodePtr());
                                      if (textAnimatorPanel) {
                                          textAnimatorPanel->setActiveNode(NodePtr());
                                      }
                                  }
                              }

                              for (int e = 0; e < selLayer.effects.size(); ++e) {
                                  const FluxEffect& effect = selLayer.effects[e];
                                  if (effect.node && effect.node->isActivated()) {
                                      effectsPanel->addEffect(effect.pluginId, effect.label);
                                  }
                              }

                              if (selLayer.gizmoNode) {
                                  NodeGuiPtr nodeGui = std::dynamic_pointer_cast<NodeGui>(selLayer.gizmoNode->getNodeGui());
                                  if (nodeGui) {
                                      nodeGui->setVisibleSettingsPanel(true);
                                  NodeSettingsPanel* settingsPanel = nodeGui->getSettingPanel();
                                  if (settingsPanel) {
                                      DockablePanel* dockPanel = static_cast<DockablePanel*>(settingsPanel);
                                      putSettingsPanelFirst(dockPanel);
                                  }
                                  }
                              }
                          } else {
                              effectsPanel->setActiveLayer(-1, QString());
                              if (textPanel) {
                                  textPanel->setActiveNode(NodePtr());
                              }
                              if (textAnimatorPanel) {
                                  textAnimatorPanel->setActiveNode(NodePtr());
                              }
                          }

                          refreshSelectedLayerAiPaintBinding(false);
                          // Redraw viewers to update overlay handles
                          redrawAllViewers();
                      });


    QObject::connect(timeline, &FluxTimeline::sourceFrameCaptureRequested, this,
                     [this]() {
                         exportFluxSam3SourceFrameForSelectedLayer();
                     });

    QObject::connect(timeline, &FluxTimeline::frameChanged, this,
                     [this, refreshSelectedLayerAiPaintBinding](int) {
                         refreshSelectedLayerAiPaintBinding(false);
                     });

    QObject::connect(timeline, &FluxTimeline::sourceViewerRequested, this,
                     [this, timeline, aiPanel](int layerIndex) {
                         const QList<FluxLayer>& layers = timeline->getLayers();
                         if (layerIndex < 0 || layerIndex >= layers.size()) {
                             return;
                         }
                         const FluxLayer& layer = layers[layerIndex];
                         if (layer.type != QString::fromUtf8("footage") || !layer.readerNode || !layer.readerNode->isActivated()) {
                             return;
                         }

                         NodePtr aiPaintNode;
                         for (const FluxEffect& effect : layer.effects) {
                             if (!effect.enabled || !effect.node || !effect.node->isActivated()) {
                                 continue;
                             }
                             EffectInstancePtr effectInstance = effect.node->getEffectInstance();
                             if (effectInstance && effectInstance->getPluginID() == PLUGINID_NATRON_AIPAINT) {
                                 aiPaintNode = effect.node;
                                 break;
                             }
                         }

                         NodePtr viewerNode;
                         ViewerTab* viewerTab = ensureFluxAiWorkViewerTab(&viewerNode);
                         bool sourceViewerArmed = false;
                         if (viewerTab && viewerNode && viewerNode->isActivated()) {
                             NodePtr sourceViewNode = aiPaintNode && aiPaintNode->isActivated() ? aiPaintNode : layer.readerNode;
                             viewerNode->disconnectInput(0);
                             viewerNode->connectInput(sourceViewNode, 0);
                             if (viewerNode->getInput(0) == sourceViewNode) {
                                 sourceViewerArmed = true;
                                 if (sourceViewNode == aiPaintNode) {
                                     NodeGuiPtr nodeGui = std::dynamic_pointer_cast<NodeGui>(aiPaintNode->getNodeGui());
                                     if (nodeGui) {
                                         nodeGui->setVisibleSettingsPanel(true);
                                         NodeSettingsPanel* settingsPanel = nodeGui->getSettingPanel();
                                         if (settingsPanel) {
                                             putSettingsPanelFirst(static_cast<DockablePanel*>(settingsPanel));
                                         }
                                     }
                                 }
                             } else {
                                 fprintf(stderr, "FLUX-SAM3-A1 source viewer blocked: AI Work Viewer input 0 does not match selected AI Paint/source node for layer=%d\n", layerIndex);
                             }
                             showFluxViewerTab(viewerTab);
                         }

                         if (aiPanel) {
                             if (sourceViewerArmed && viewerTab) {
                                 const int timelineFrame = timeline->getCurrentFrame();
                                 const int sourceFrame = qBound(layer.originalFirstFrame, timelineFrame - layer.timeOffset, layer.originalLastFrame);
                                 const int rangeFirstFrame = qBound(layer.originalFirstFrame, layer.inPoint, layer.originalLastFrame);
                                 const int rangeLastFrame = qBound(layer.originalFirstFrame, layer.outPoint, layer.originalLastFrame);
                                 aiPanel->setSourceCaptureContext(viewerTab->getViewer(), viewerNode, layerIndex, layer.name, layer.filePath, layer.readerNode, layer.readerNode ? QString::fromStdString(layer.readerNode->getLabel()) : QString(), aiPaintNode, timelineFrame, sourceFrame, layer.timeOffset, rangeFirstFrame, rangeLastFrame);
                                 connectAIPaintPromptStoreRefresh(aiPaintNode, aiPanel);
                             } else {
                                 aiPanel->setViewerForCapture(nullptr);
                             }
                             TabWidget* aiPane = aiPanel->getParentPane();
                             if (aiPane) {
                                 const int count = aiPane->count();
                                 for (int t = 0; t < count; ++t) {
                                     if (aiPane->tabAt(t) == aiPanel) {
                                         aiPane->makeCurrentTab(t);
                                         break;
                                     }
                                 }
                             }
                         }
                         redrawAllViewers();
                     });


    QObject::connect(timeline, &FluxTimeline::textAnimatorSelected, this,
                     [this, timeline, textPanel, textAnimatorPanel](int layerIndex, int animatorId) {
                         const QList<FluxLayer>& layers = timeline->getLayers();
                         if (layerIndex < 0 || layerIndex >= layers.size() || layers[layerIndex].type != QString::fromUtf8("text")) {
                             return;
                         }
                         NodePtr textNode = layers[layerIndex].gizmoNode;
                         if (textPanel) {
                             textPanel->setActiveNode(textNode);
                         }
                         if (textAnimatorPanel) {
                             textAnimatorPanel->setActiveNode(textNode);
                             textAnimatorPanel->setSelectedAnimatorId(animatorId);
                             TabWidget* pane = textAnimatorPanel->getParentPane();
                             if (pane) {
                                 const int count = pane->count();
                                 for (int t = 0; t < count; ++t) {
                                     if (pane->tabAt(t) == textAnimatorPanel) {
                                         pane->makeCurrentTab(t);
                                         break;
                                     }
                                 }
                             }
                         }
                         redrawAllViewers();
                     });

    // 4. Effects Panel: select/remove actual effect nodes owned by the timeline model.
    QObject::connect(effectsPanel, &FluxEffectsPanel::effectSelected, this,
                     [this, timeline, aiPanel, refreshSelectedLayerAiPaintBinding](int effectIndex) {
                         int layerIndex = timeline->getSelectedLayerIndex();
                         const QList<FluxLayer>& layers = timeline->getLayers();
                         if (layerIndex < 0 || layerIndex >= layers.size() ||
                             effectIndex < 0 || effectIndex >= layers[layerIndex].effects.size()) {
                             return;
                         }
                         NodePtr node = layers[layerIndex].effects[effectIndex].node;
                         if (!node || !node->isActivated()) {
                             return;
                         }
                         NodeGuiPtr nodeGui = std::dynamic_pointer_cast<NodeGui>(node->getNodeGui());
                         if (nodeGui) {
                             nodeGui->setVisibleSettingsPanel(true);
                             NodeSettingsPanel* settingsPanel = nodeGui->getSettingPanel();
                             if (settingsPanel) {
                                 DockablePanel* dockPanel = static_cast<DockablePanel*>(settingsPanel);
                                 putSettingsPanelFirst(dockPanel);
                             }
                         }
                         EffectInstancePtr effectInstance = node->getEffectInstance();
                         if (effectInstance && (effectInstance->getPluginID() == PLUGINID_NATRON_AIPAINT || effectInstance->getPluginID() == PLUGINID_FLUX_CORRIDOR_KEY)) {
                             NodePtr viewerNode;
                             ViewerTab* viewerTab = ensureFluxAiWorkViewerTab(&viewerNode);
                             if (viewerTab && viewerNode && viewerNode->isActivated()) {
                                 viewerNode->disconnectInput(0);
                                 viewerNode->connectInput(node, 0);
                                 showFluxViewerTab(viewerTab);
                                 if (aiPanel && effectInstance->getPluginID() == PLUGINID_NATRON_AIPAINT) {
                                     const int timelineFrame = timeline->getCurrentFrame();
                                     const FluxLayer& layer = layers[layerIndex];
                                     const int sourceFrame = qBound(layer.originalFirstFrame, timelineFrame - layer.timeOffset, layer.originalLastFrame);
                                     const int rangeFirstFrame = qBound(layer.originalFirstFrame, layer.inPoint, layer.originalLastFrame);
                                     const int rangeLastFrame = qBound(layer.originalFirstFrame, layer.outPoint, layer.originalLastFrame);
                                     aiPanel->setSourceCaptureContext(viewerTab->getViewer(), viewerNode, layerIndex, layer.name, layer.filePath, layer.readerNode, layer.readerNode ? QString::fromStdString(layer.readerNode->getLabel()) : QString(), node, timelineFrame, sourceFrame, layer.timeOffset, rangeFirstFrame, rangeLastFrame);
                                     connectAIPaintPromptStoreRefresh(node, aiPanel);
                                 }
                             }
                         }
                         refreshSelectedLayerAiPaintBinding(false);
                     });

    QObject::connect(effectsPanel, &FluxEffectsPanel::effectRemoved, this,
                     [this, timeline, effectsPanel](int effectIndex) {
                         int layerIndex = timeline->getSelectedLayerIndex();
                         if (layerIndex < 0 || layerIndex >= timeline->getLayers().size()) {
                             return;
                         }
                         timeline->removeEffectFromLayer(layerIndex, effectIndex);
                     });

    // 5. Timeline: compositingChanged → rebuild Merge node chain + connect viewer
    QObject::connect(timeline, &FluxTimeline::compositingChanged, this,
                     [this, timeline]() {
                         rebuildCompositingGraph(timeline);
                     });

    // 5b. Timeline: projectLayersRestored → repopulate Project Bin from restored footage layers
    QObject::connect(timeline, &FluxTimeline::projectLayersRestored, this,
                     [projectBin, timeline]() {
                         projectBin->clearBin();
                         QSet<QString> seen;
                         const QList<FluxLayer>& layers = timeline->getLayers();
                         for (int i = 0; i < layers.size(); ++i) {
                             const FluxLayer& layer = layers[i];
                             if (layer.type == QString::fromUtf8("footage") && !layer.filePath.isEmpty()) {
                                 if (!seen.contains(layer.filePath)) {
                                     seen.insert(layer.filePath);
                                     projectBin->addFile(layer.filePath);
                                 }
                             }
                         }
                     });

    // 6. Timeline: effectSelected → open effect node properties panel
    QObject::connect(timeline, &FluxTimeline::effectSelected, this,
                     [this, timeline, aiPanel, refreshSelectedLayerAiPaintBinding](int layerIndex, int effectIndex) {
                         const QList<FluxLayer>& layers = timeline->getLayers();
                         if (layerIndex < 0 || layerIndex >= layers.size()) {
                             return;
                         }
                         const FluxLayer& layer = layers[layerIndex];
                         if (effectIndex < 0 || effectIndex >= layer.effects.size()) {
                             return;
                         }
                         NodePtr node = layer.effects[effectIndex].node;
                         if (!node || !node->isActivated()) {
                             return;
                         }

                          // Close all layer gizmo panels before opening effect panel
                          const QList<FluxLayer>& allLayers = timeline->getLayers();
                          for (const FluxLayer& l : allLayers) {
                              if (!l.gizmoNode) continue;
                              NodeGuiIPtr gizmoGuiI = l.gizmoNode->getNodeGui();
                              if (!gizmoGuiI) continue;
                              NodeGuiPtr gizmoGui = std::dynamic_pointer_cast<NodeGui>(gizmoGuiI);
                              if (gizmoGui) {
                                  gizmoGui->setVisibleSettingsPanel(false);
                              }
                          }

                         NodeGuiIPtr nodeGuiI = node->getNodeGui();
                         if (nodeGuiI) {
                             NodeGuiPtr nodeGui = std::dynamic_pointer_cast<NodeGui>(nodeGuiI);
                             if (nodeGui) {
                                 nodeGui->setVisibleSettingsPanel(true);
                                 NodeSettingsPanel* settingsPanel = nodeGui->getSettingPanel();
                                 if (settingsPanel) {
                                     DockablePanel* dockPanel = static_cast<DockablePanel*>(settingsPanel);
                                     putSettingsPanelFirst(dockPanel);
                                 }
                             }
                         }
                         EffectInstancePtr effectInstance = node->getEffectInstance();
                         if (effectInstance && (effectInstance->getPluginID() == PLUGINID_NATRON_AIPAINT || effectInstance->getPluginID() == PLUGINID_FLUX_CORRIDOR_KEY)) {
                             NodePtr viewerNode;
                             ViewerTab* viewerTab = ensureFluxAiWorkViewerTab(&viewerNode);
                             if (viewerTab && viewerNode && viewerNode->isActivated()) {
                                 viewerNode->disconnectInput(0);
                                 viewerNode->connectInput(node, 0);
                                 showFluxViewerTab(viewerTab);
                                 if (aiPanel && effectInstance->getPluginID() == PLUGINID_NATRON_AIPAINT) {
                                     const int timelineFrame = timeline->getCurrentFrame();
                                     const int sourceFrame = qBound(layer.originalFirstFrame, timelineFrame - layer.timeOffset, layer.originalLastFrame);
                                     const int rangeFirstFrame = qBound(layer.originalFirstFrame, layer.inPoint, layer.originalLastFrame);
                                     const int rangeLastFrame = qBound(layer.originalFirstFrame, layer.outPoint, layer.originalLastFrame);
                                     aiPanel->setSourceCaptureContext(viewerTab->getViewer(), viewerNode, layerIndex, layer.name, layer.filePath, layer.readerNode, layer.readerNode ? QString::fromStdString(layer.readerNode->getLabel()) : QString(), node, timelineFrame, sourceFrame, layer.timeOffset, rangeFirstFrame, rangeLastFrame);
                                     connectAIPaintPromptStoreRefresh(node, aiPanel);
                                 }
                             }
                         }
                         refreshSelectedLayerAiPaintBinding(false);
                     });

    // 7. Timeline: effectsChanged → sync effects panel with model
    QObject::connect(timeline, &FluxTimeline::effectsChanged, this,
                     [effectsPanel, timeline](int layerIndex) {
                         if (!effectsPanel || !timeline) {
                             return;
                         }
                         const QList<FluxLayer>& layers = timeline->getLayers();
                         if (layerIndex >= 0 && layerIndex < layers.size()) {
                             effectsPanel->setActiveLayer(layerIndex, layers[layerIndex].name);
                             for (int e = 0; e < layers[layerIndex].effects.size(); ++e) {
                                 const FluxEffect& fx = layers[layerIndex].effects[e];
                                 if (fx.node && fx.node->isActivated()) {
                                     effectsPanel->addEffect(fx.pluginId, fx.label);
                                 }
                             }
                         } else {
                             effectsPanel->setActiveLayer(-1, QString());
                         }
                     });

    // 7b. Timeline: viewerInputSwitchRequested → connect viewer to selected node
    QObject::connect(timeline, &FluxTimeline::viewerInputSwitchRequested, this,
                     [this, timeline](int viewerInputIndex) {
                         if (!getApp() || !timeline) {
                             return;
                         }
                         if (viewerInputIndex < 0 || viewerInputIndex >= 9) {
                             return;
                         }

                         ViewerTab* viewerTab = getFluxMainCompositingViewerTab();
                         if (!viewerTab) {
                             return;
                         }
                         ViewerInstance* internalViewer = viewerTab->getInternalNode();
                         NodePtr viewerNode = internalViewer ? internalViewer->getNode() : NodePtr();
                         if (!viewerNode || !viewerNode->isActivated()) {
                             return;
                         }

                         // Resolve target node
                         NodePtr targetNode;
                         if (viewerInputIndex == 0) {
                             // Full comp: use the stored final output node
                             targetNode = _imp->_fluxFinalOutputNode;
                             if (!targetNode || !targetNode->isActivated()) {
                                 // Fall back to whatever is currently on input 0
                                 targetNode = viewerNode->getInput(0);
                                 if (!targetNode || !targetNode->isActivated()) {
                                     fprintf(stderr, "FLUX VIEWER SWITCH: No full-comp output available for input 1\n");
                                     return;
                                 }
                             }
                         } else {
                             targetNode = timeline->selectedViewerSwitchTargetNode(viewerInputIndex);
                             if (!targetNode || !targetNode->isActivated()) {
                                 return;
                             }
                         }

                         // Already connected? Just refresh badges/viewer.
                         NodePtr previousInput = viewerNode->getInput(viewerInputIndex);
                         if (previousInput == targetNode) {
                             timeline->setViewerInputBadgeForNode(viewerInputIndex, targetNode);
                             showFluxViewerTab(viewerTab);
                             redrawAllViewers();
                             return;
                         }

                         // Disconnect existing input, validate, connect
                         viewerNode->disconnectInput(viewerInputIndex);

                         Node::CanConnectInputReturnValue canConnect = viewerNode->canConnectInput(targetNode, viewerInputIndex);
                         if (canConnect != Node::eCanConnectInput_ok) {
                             // Attempt restore previous
                             if (previousInput && previousInput->isActivated()) {
                                 Node::CanConnectInputReturnValue restoreCheck = viewerNode->canConnectInput(previousInput, viewerInputIndex);
                                 if (restoreCheck == Node::eCanConnectInput_ok) {
                                     viewerNode->connectInput(previousInput, viewerInputIndex);
                                 }
                             }
                             timeline->clearViewerInputBadge(viewerInputIndex);
                             fprintf(stderr, "FLUX VIEWER SWITCH: canConnectInput failed for input %d (code %d)\n",
                                     viewerInputIndex + 1, (int)canConnect);
                             return;
                         }

                         bool connected = viewerNode->connectInput(targetNode, viewerInputIndex);
                         if (!connected) {
                             // Attempt restore
                             if (previousInput && previousInput->isActivated()) {
                                 viewerNode->connectInput(previousInput, viewerInputIndex);
                             }
                             timeline->clearViewerInputBadge(viewerInputIndex);
                             fprintf(stderr, "FLUX VIEWER SWITCH: connectInput failed for input %d\n",
                                     viewerInputIndex + 1);
                             return;
                         }

                         // Success: update badge, show viewer, redraw
                         // For input 0 (full comp), don't badge layer/effect rows
                         if (viewerInputIndex > 0) {
                             timeline->setViewerInputBadgeForNode(viewerInputIndex, targetNode);
                         } else {
                             timeline->clearViewerInputBadge(viewerInputIndex);
                         }
                         showFluxViewerTab(viewerTab);
                         redrawAllViewers();
                     });

    // 8. Timeline: maskSelected → open mask node properties panel (if backing node exists)
    QObject::connect(timeline, &FluxTimeline::maskSelected, this,
                     [this, timeline](int layerIndex, int maskIndex) {
                         const QList<FluxLayer>& layers = timeline->getLayers();
                         if (layerIndex < 0 || layerIndex >= layers.size()) {
                             return;
                         }
                         const FluxLayer& layer = layers[layerIndex];
                         if (maskIndex < 0 || maskIndex >= layer.masks.size()) {
                             return;
                         }
                         const FluxMask& mask = layer.masks[maskIndex];

                         // Close all layer gizmo panels
                         for (const FluxLayer& l : layers) {
                             if (!l.gizmoNode) continue;
                             NodeGuiIPtr gizmoGuiI = l.gizmoNode->getNodeGui();
                             if (!gizmoGuiI) continue;
                             NodeGuiPtr gizmoGui = std::dynamic_pointer_cast<NodeGui>(gizmoGuiI);
                             if (gizmoGui) {
                                 gizmoGui->setVisibleSettingsPanel(false);
                             }
                         }

                         if (!mask.maskNode) {
                             return;
                         }

                         NodeGuiIPtr nodeGuiI = mask.maskNode->getNodeGui();
                         if (nodeGuiI) {
                             NodeGuiPtr nodeGui = std::dynamic_pointer_cast<NodeGui>(nodeGuiI);
                             if (nodeGui) {
                                 nodeGui->setVisibleSettingsPanel(true);
                                 NodeSettingsPanel* settingsPanel = nodeGui->getSettingPanel();
                                 if (settingsPanel) {
                                     DockablePanel* dockPanel = static_cast<DockablePanel*>(settingsPanel);
                                     putSettingsPanelFirst(dockPanel);
                                 }
                             }
                         }
                     });

    // 9. Timeline: nodegraphSyncRequested → sync timeline from node graph
    QObject::connect(timeline, &FluxTimeline::nodegraphSyncRequested, this,
                     &Gui::syncFluxTimelineFromNodeGraph);

    // Store references to Flux widgets for later access
    _imp->_fluxProjectBin = projectBin;
    _imp->_fluxTimeline = timeline;
    _imp->_fluxEffectsPanel = effectsPanel;
    _imp->_fluxExportPanel = exportPanel;
    _imp->_fluxAiPanel = aiPanel;
    _imp->_fluxTextPanel = textPanel;
    _imp->_fluxTextAnimatorPanel = textAnimatorPanel;

    // Create Flux export nodes (disabled Reformat + Write)
    {
        NodeCollectionPtr collection = std::dynamic_pointer_cast<NodeCollection>(getApp()->getProject());
        
        // Reformat node (disabled by default — user can enable to override format)
        CreateNodeArgs reformatArgs("net.sf.openfx.Reformat", collection);
        reformatArgs.setProperty<bool>(kCreateNodeArgsPropAutoConnect, false);
        reformatArgs.setProperty<bool>(kCreateNodeArgsPropAddUndoRedoCommand, false);
        reformatArgs.setProperty<bool>(kCreateNodeArgsPropSettingsOpened, false);
        reformatArgs.setProperty<bool>(kCreateNodeArgsPropSilent, true);
        _imp->_fluxExportReformatNode = getApp()->createNode(reformatArgs);
        if (_imp->_fluxExportReformatNode) {
            _imp->_fluxExportReformatNode->setNodeDisabled(true);
            _imp->_fluxExportReformatNode->setPosition(0, -400); // off-screen
        }

        // Write node
        CreateNodeArgs writeArgs(PLUGINID_NATRON_WRITE, collection);
        writeArgs.setProperty<bool>(kCreateNodeArgsPropAutoConnect, false);
        writeArgs.setProperty<bool>(kCreateNodeArgsPropAddUndoRedoCommand, false);
        writeArgs.setProperty<bool>(kCreateNodeArgsPropSettingsOpened, false);
        writeArgs.setProperty<bool>(kCreateNodeArgsPropSilent, true);
        _imp->_fluxExportWriteNode = getApp()->createNode(writeArgs);
        if (_imp->_fluxExportWriteNode) {
            _imp->_fluxExportWriteNode->setPosition(0, -450); // off-screen
        }

        // Connect Reformat -> Write
        if (_imp->_fluxExportReformatNode && _imp->_fluxExportWriteNode) {
            _imp->_fluxExportWriteNode->disconnectInput(0);
            _imp->_fluxExportWriteNode->connectInput(_imp->_fluxExportReformatNode, 0);
        }

        // Pass nodes to the export panel
        exportPanel->setExportNodes(_imp->_fluxExportReformatNode, _imp->_fluxExportWriteNode);

        // Wire render signal
        QObject::connect(exportPanel, &FluxExportPanel::renderRequested, this, [this]() {
            if (!_imp->_fluxExportWriteNode || !_imp->_fluxExportWriteNode->isActivated()) {
                return;
            }
            
            if (!_imp->_fluxTimeline) {
                return;
            }

            // Connect export chain to the true final output (includes adjustment effects)
            NodePtr finalOutput = _imp->_fluxFinalOutputNode;
            if (!finalOutput) {
                Dialogs::warningDialog(tr("Render").toStdString(),
                                       tr("No layers to render. Add at least one layer to the timeline.").toStdString());
                return;
            }
            if (_imp->_fluxExportReformatNode) {
                _imp->_fluxExportReformatNode->disconnectInput(0);
                _imp->_fluxExportReformatNode->connectInput(finalOutput, 0);
            }

            // Fire render using Natron's standard sentinel values.
            // validateRenderOptions() will resolve frame range from the Write node's own knobs.
            // This is identical to how the Write node's own Render button works.
            EffectInstancePtr effect = _imp->_fluxExportWriteNode->getEffectInstance();
            if (effect) {
                AppInstance::RenderWork w;
                w.writer = dynamic_cast<OutputEffectInstance*>( effect.get() );
                if (w.writer) {
                    w.firstFrame = std::numeric_limits<int>::min();
                    w.lastFrame = std::numeric_limits<int>::max();
                    w.frameStep = std::numeric_limits<int>::min();
                    w.useRenderStats = false;
                    std::list<AppInstance::RenderWork> workList;
                    workList.push_back(w);
                    getApp()->startWritersRendering(false, workList);
                }
            }
        });
    }

    fprintf(stderr, "FLUX: Layout created successfully\n");
    FluxT074Harness::maybeStart(this);
    FluxT079Harness::maybeStart(this);
} // Gui::setupFluxUi

// ====================================================================
// Mask graph helpers (T060/T061)
// ====================================================================

static const char* kFluxLayerMaskApplyLabel = "Flux Layer Mask Apply";
static const char* kFluxMaskReformatLabel = "Flux Mask Reformat";
static const char* kFluxMaskRotoLabel = "Flux Mask";
static const char* kFluxLayerMaskPremultLabel = "Flux Layer Mask Premult";
static const char* kFluxLayerMaskUnpremultLabel = "Flux Layer Mask Unpremult";
static const char* kFluxExternalMaskReadLabel = "Flux External Mask Read";
static const char* kFluxExternalMaskReformatLabel = "Flux External Mask Reformat";
static const char* kFluxLayerExternalMaskShuffleLabel = "Flux Layer External Mask Shuffle";

static int fluxAIChannelIndex(const QString& channel)
{
    const QString c = channel.toLower();
    if (c == QLatin1String("green")) return 1;
    if (c == QLatin1String("blue")) return 2;
    if (c == QLatin1String("alpha")) return 3;
    return 0;
}

static int fluxAIOperationIndex(const QString& operation)
{
    const QString op = operation.toLower();
    if (op == QLatin1String("copy")) return 0;
    if (op == QLatin1String("plus")) return 1;
    if (op == QLatin1String("multiply")) return 2;
    if (op == QLatin1String("screen")) return 3;
    if (op == QLatin1String("max")) return 4;
    if (op == QLatin1String("min")) return 5;
    if (op == QLatin1String("subtract")) return 6;
    if (op == QLatin1String("overlay")) return 7;
    return 4;
}

static QString fluxAIShuffleChannelExpression(const QString& channel)
{
    const QString c = channel.toLower();
    if (c == QLatin1String("green")) {
        return QString::fromUtf8("B.uk.co.thefoundry.OfxImagePlaneColour.G");
    }
    if (c == QLatin1String("blue")) {
        return QString::fromUtf8("B.uk.co.thefoundry.OfxImagePlaneColour.B");
    }
    if (c == QLatin1String("alpha")) {
        return QString::fromUtf8("B.uk.co.thefoundry.OfxImagePlaneColour.A");
    }
    return QString::fromUtf8("B.uk.co.thefoundry.OfxImagePlaneColour.R");
}

static QString fluxAIShuffleChannelChoiceId(const QString& channel)
{
    const QString c = channel.toLower();
    if (c == QLatin1String("green")) {
        return QString::fromUtf8("B.g");
    }
    if (c == QLatin1String("blue")) {
        return QString::fromUtf8("B.b");
    }
    if (c == QLatin1String("alpha")) {
        return QString::fromUtf8("B.a");
    }
    return QString::fromUtf8("B.r");
}

static void fluxSetShuffleChannelKnob(const NodePtr& node,
                                      const char* knobName,
                                      const QString& expression,
                                      const QString& choiceId)
{
    KnobStringPtr stringKnob = std::dynamic_pointer_cast<KnobString>(node->getKnobByName(knobName));
    if (stringKnob) {
        stringKnob->setValue(expression.toStdString(), ViewSpec::all(), 0, true);
    }

    KnobChoicePtr choiceKnob = std::dynamic_pointer_cast<KnobChoice>(node->getKnobByName(knobName));
    if (choiceKnob) {
        choiceKnob->setValueFromID(expression.toStdString(), 0, true);
        choiceKnob->setValueFromID(choiceId.toStdString(), 0, true);
    }
}

static void fluxConfigureAIChannelMergeNode(const NodePtr& node,
                                            const QString& sourceChannel,
                                            const QString& operation)
{
    if (!node) {
        return;
    }

    KnobChoicePtr aChannel = std::dynamic_pointer_cast<KnobChoice>(node->getKnobByName("aChannel"));
    if (aChannel) {
        aChannel->setValue(fluxAIChannelIndex(sourceChannel), ViewSpec::all(), 0);
    }
    KnobChoicePtr bChannel = std::dynamic_pointer_cast<KnobChoice>(node->getKnobByName("bChannel"));
    if (bChannel) {
        bChannel->setValue(3, ViewSpec::all(), 0);
    }
    KnobChoicePtr op = std::dynamic_pointer_cast<KnobChoice>(node->getKnobByName("operation"));
    if (op) {
        op->setValue(fluxAIOperationIndex(operation), ViewSpec::all(), 0);
    }
    KnobChoicePtr out = std::dynamic_pointer_cast<KnobChoice>(node->getKnobByName("outputChannel"));
    if (out) {
        out->setValue(3, ViewSpec::all(), 0);
    }
    KnobChoicePtr bbox = std::dynamic_pointer_cast<KnobChoice>(node->getKnobByName("bbox"));
    if (bbox) {
        bbox->setValue(0, ViewSpec::all(), 0);
    }
    KnobDoublePtr mix = std::dynamic_pointer_cast<KnobDouble>(node->getKnobByName("mix"));
    if (mix) {
        mix->setValue(1.0, ViewSpec::all(), 0);
    }
}

static void fluxConfigureAIShuffleAlphaNode(const NodePtr& node,
                                            const QString& sourceChannel)
{
    if (!node) {
        return;
    }

    const QString expression = fluxAIShuffleChannelExpression(sourceChannel);
    const QString choiceId = fluxAIShuffleChannelChoiceId(sourceChannel);
    fluxSetShuffleChannelKnob(node, "outputA", expression, choiceId);
    fluxSetShuffleChannelKnob(node, "outputAChoice", choiceId, choiceId);
}

static bool
isFluxOwnedLayerMaskLabel(const std::string& label)
{
    return label == kFluxLayerMaskPremultLabel ||
           label == kFluxMaskRotoLabel ||
           label == kFluxLayerMaskUnpremultLabel ||
           label == kFluxLayerExternalMaskShuffleLabel ||
           label == kFluxExternalMaskReformatLabel ||
           label == kFluxExternalMaskReadLabel;
}

// Layout offsets for effect-mask nodes (relative to their anchor).
// Reformat sits above Roto in the mask side column, both at the same X.
// This keeps the mask subtree between the layer branch and the main pipe
// without crossing the main tree.
static const double kMaskColumnOffsetX = 325.0;  // X offset from branch anchor to mask side column
static const double kMaskReformatOffsetY = -120.0; // Reformat sits one row above Roto

// Forward declarations for mask helpers

// Return the first enabled layer-level mask (effectIndex < 0), or nullptr.
static FluxMask*
firstEnabledLayerMask(FluxLayer& layer)
{
    for (int m = 0; m < layer.masks.size(); ++m) {
        if (layer.masks[m].enabled && layer.masks[m].effectIndex < 0) {
            return &layer.masks[m];
        }
    }
    return nullptr;
}

// Return the first enabled mask for the given effect index, or nullptr.
static FluxMask*
firstEnabledEffectMask(FluxLayer& layer, int effectIndex)
{
    for (int m = 0; m < layer.masks.size(); ++m) {
        if (layer.masks[m].enabled && layer.masks[m].effectIndex == effectIndex) {
            return &layer.masks[m];
        }
    }
    return nullptr;
}

// Ensure Reformat + Roto nodes exist for the given mask. Creates them if needed.
// x,y = position anchor (typically the target effect/apply node position).
static bool
ensureMaskSourceNodes(Gui* gui, FluxMask& mask, const NodeCollectionPtr& collection,
                      double anchorX, double anchorY)
{
    if (!gui || !collection) {
        return false;
    }

    // -- Reformat node (feeds Roto so the mask matches the project format) --
    if (!mask.reformatNode || !mask.reformatNode->isActivated()) {
        CreateNodeArgs rfArgs("net.sf.openfx.Reformat", collection);
        rfArgs.setProperty<bool>(kCreateNodeArgsPropAutoConnect, false);
        rfArgs.setProperty<bool>(kCreateNodeArgsPropAddUndoRedoCommand, false);
        rfArgs.setProperty<bool>(kCreateNodeArgsPropSettingsOpened, false);
        mask.reformatNode = gui->getApp()->createNode(rfArgs);
        if (mask.reformatNode) {
            mask.reformatNode->setLabel(kFluxMaskReformatLabel);
            // Set type to "To Project Format" (index 3)
            KnobIPtr typeKnob = mask.reformatNode->getKnobByName("reformatType");
            if (typeKnob) {
                KnobChoicePtr choice = std::dynamic_pointer_cast<KnobChoice>(typeKnob);
                if (choice) {
                    choice->setValue(3, ViewSpec::all(), 0);
                }
            }
        } else {
            fprintf(stderr, "FLUX WARNING: Failed to create mask Reformat node\n");
            return false;
        }
    }
    mask.reformatNode->setPosition(anchorX + kMaskColumnOffsetX, anchorY + kMaskReformatOffsetY);

    // -- Roto node (the actual mask shape source) --
    if (!mask.maskNode || !mask.maskNode->isActivated()) {
        CreateNodeArgs rotoArgs(PLUGINID_NATRON_ROTO, collection);
        rotoArgs.setProperty<bool>(kCreateNodeArgsPropAutoConnect, false);
        rotoArgs.setProperty<bool>(kCreateNodeArgsPropAddUndoRedoCommand, false);
        rotoArgs.setProperty<bool>(kCreateNodeArgsPropSettingsOpened, false);
        mask.maskNode = gui->getApp()->createNode(rotoArgs);
        if (mask.maskNode) {
            mask.maskNode->setLabel(kFluxMaskRotoLabel);
            mask.pluginId = QString::fromUtf8(PLUGINID_NATRON_ROTO);
        } else {
            fprintf(stderr, "FLUX WARNING: Failed to create mask Roto node\n");
            return false;
        }
    }
    mask.maskNode->setPosition(anchorX + kMaskColumnOffsetX, anchorY);

    // Wire: Reformat → Roto input 0
    mask.maskNode->disconnectInput(0);
    if (!mask.maskNode->connectInput(mask.reformatNode, 0)) {
        fprintf(stderr, "FLUX WARNING: Reformat → Roto connect failed\n");
    }

    return mask.reformatNode && mask.maskNode;
}

// Ensure the inline Premult node for layer masks exists. Creates if needed.
// Stored in layer.maskApplyNode (repurposed from old Merge(in) to inline Premult).
static NodePtr
ensureInlineLayerMaskPremultNode(Gui* gui, FluxLayer& layer, const NodeCollectionPtr& collection,
                                  double x, double y)
{
    if (!gui || !collection) {
        return nullptr;
    }

    // Migrate old Merge(in) maskApplyNode if it exists
    if (layer.maskApplyNode && layer.maskApplyNode->isActivated()) {
        std::string label = layer.maskApplyNode->getLabel();
        if (label == kFluxLayerMaskApplyLabel) {
            // Old Flux-owned Merge(in) — deactivate and clear
            layer.maskApplyNode->deactivate(std::list<NodePtr>(), false, true);
            layer.maskApplyNode.reset();
        }
    }

    if (!layer.maskApplyNode || !layer.maskApplyNode->isActivated()) {
        CreateNodeArgs pmArgs("net.sf.openfx.Premult", collection);
        pmArgs.setProperty<bool>(kCreateNodeArgsPropAutoConnect, false);
        pmArgs.setProperty<bool>(kCreateNodeArgsPropAddUndoRedoCommand, false);
        pmArgs.setProperty<bool>(kCreateNodeArgsPropSettingsOpened, false);
        layer.maskApplyNode = gui->getApp()->createNode(pmArgs);
        if (!layer.maskApplyNode) {
            fprintf(stderr, "FLUX WARNING: Failed to create inline layer mask Premult node\n");
            return nullptr;
        }
        layer.maskApplyNode->setLabel(kFluxLayerMaskPremultLabel);
    }

    layer.maskApplyNode->setPosition(x, y);
    return layer.maskApplyNode;
}

// Ensure the inline Unpremult node for layer masks exists. Creates if needed.
// Discovers existing Flux-owned Unpremult from Roto input 0 to avoid duplicates.
// Returns nullptr if unpremult is not needed (caller decides).
static NodePtr
ensureInlineLayerMaskUnpremultNode(Gui* gui, FluxMask& lmask, const NodeCollectionPtr& collection,
                                    double x, double y)
{
    if (!gui || !collection || !lmask.maskNode) {
        return nullptr;
    }

    // Try to discover an existing Unpremult feeding Roto input 0
    if (lmask.maskNode->isActivated()) {
        NodePtr rotoInput = lmask.maskNode->getInput(0);
        if (rotoInput && isUnpremultNode(rotoInput) && rotoInput->isActivated()) {
            // Reuse existing Unpremult (may or may not be Flux-owned)
            rotoInput->setPosition(x, y);
            return rotoInput;
        }
    }

    // Create new Unpremult
    CreateNodeArgs upArgs("net.sf.openfx.Unpremult", collection);
    upArgs.setProperty<bool>(kCreateNodeArgsPropAutoConnect, false);
    upArgs.setProperty<bool>(kCreateNodeArgsPropAddUndoRedoCommand, false);
    upArgs.setProperty<bool>(kCreateNodeArgsPropSettingsOpened, false);
    NodePtr unpremultNode = gui->getApp()->createNode(upArgs);
    if (!unpremultNode) {
        fprintf(stderr, "FLUX WARNING: Failed to create inline layer mask Unpremult node\n");
        return nullptr;
    }
    unpremultNode->setLabel(kFluxLayerMaskUnpremultLabel);
    unpremultNode->setPosition(x, y);
    return unpremultNode;
}

// Deactivate old Flux-owned layer-mask artifacts (Merge(in) and Reformat).
// Narrow: only matches exact Flux-owned labels.
static void
cleanupOldLayerMaskArtifacts(FluxLayer& layer, FluxMask& lmask)
{
    // Old Merge(in) maskApplyNode
    if (layer.maskApplyNode && layer.maskApplyNode->isActivated()) {
        if (layer.maskApplyNode->getLabel() == kFluxLayerMaskApplyLabel) {
            layer.maskApplyNode->deactivate(std::list<NodePtr>(), false, true);
            layer.maskApplyNode.reset();
        }
    }
    // Old layer-mask Reformat
    if (lmask.reformatNode && lmask.reformatNode->isActivated()) {
        if (lmask.reformatNode->getLabel() == kFluxMaskReformatLabel) {
            lmask.reformatNode->deactivate(std::list<NodePtr>(), false, true);
            lmask.reformatNode.reset();
        }
    }
}

// Forward declaration — defined later in this file (inline chain preservation section)
static bool upstreamContainsNode(const NodePtr& start, const NodePtr& target, const QSet<Node*>* stop);

// Discover and deactivate a stale Flux-owned inline layer-mask chain feeding
// mergeNode input 1, provided the chain reaches expectedSource upstream.
// Used when no enabled layer mask exists but old Flux-owned nodes may still
// be connected. Cleanup is narrow: only deactivates nodes with Flux-owned labels.
// Returns true if a stale chain was found and cleaned.
static bool
cleanupStaleInlineLayerMaskChain(FluxLayer& layer, const NodePtr& mergeNode, const NodePtr& expectedSource)
{
    if (!mergeNode || !expectedSource) {
        return false;
    }

    NodePtr mergeA = mergeNode->getInput(1);
    if (!mergeA || mergeA == expectedSource) {
        return false;
    }

    // Walk upstream from Merge A looking for Flux-owned layer-mask nodes
    // that trace back to expectedSource.
    // Chain: expectedSource -> [Unpremult] -> Roto/Shuffle -> [Premult] -> ... -> mergeA
    // Detect Flux-owned nodes by exact label only.
    NodePtr stalePremult;
    NodePtr staleRoto;
    NodePtr staleUnpremult;
    NodePtr staleExternalShuffle;
    NodePtr staleExternalReformat;
    NodePtr staleExternalRead;

    // Walk from mergeA upstream, collecting Flux-owned mask chain nodes
    QSet<Node*> visited;
    QList<NodePtr> queue;
    queue.push_back(mergeA);
    while (!queue.isEmpty()) {
        NodePtr cur = queue.takeLast();
        if (!cur) continue;
        Node* raw = cur.get();
        if (visited.contains(raw)) continue;
        visited.insert(raw);

        std::string lbl = cur->getLabel();
        if (lbl == kFluxLayerMaskPremultLabel) {
            stalePremult = cur;
        } else if (lbl == kFluxMaskRotoLabel) {
            staleRoto = cur;
        } else if (lbl == kFluxLayerMaskUnpremultLabel) {
            staleUnpremult = cur;
        } else if (lbl == kFluxLayerExternalMaskShuffleLabel) {
            staleExternalShuffle = cur;
        } else if (lbl == kFluxExternalMaskReformatLabel) {
            staleExternalReformat = cur;
        } else if (lbl == kFluxExternalMaskReadLabel) {
            staleExternalRead = cur;
        }

        int nInputs = cur->getNInputs();
        for (int i = 0; i < nInputs; ++i) {
            NodePtr inp = cur->getInput(i);
            if (inp) {
                queue.push_back(inp);
            }
        }
    }

    // Only clean up if we found Flux-owned layer-mask evidence AND the chain
    // reaches expectedSource (not some unrelated user subgraph).
    if (!stalePremult && !staleRoto && !staleExternalShuffle) {
        return false;
    }

    NodePtr chainRoot = stalePremult ? stalePremult : (staleExternalShuffle ? staleExternalShuffle : staleRoto);
    if (!upstreamContainsNode(chainRoot, expectedSource, nullptr)) {
        return false;
    }

    // Found a stale Flux-owned inline/external layer-mask chain — clean it up
    mergeNode->disconnectInput(1);
    mergeNode->connectInput(expectedSource, 1);

    // Discover adjacent Flux-owned nodes if not already found
    if (staleRoto && staleRoto->isActivated() && !staleUnpremult) {
        NodePtr rotoIn0 = staleRoto->getInput(0);
        if (rotoIn0 && rotoIn0->isActivated() && rotoIn0->getLabel() == kFluxLayerMaskUnpremultLabel) {
            staleUnpremult = rotoIn0;
        }
    }
    if (staleExternalShuffle && staleExternalShuffle->isActivated()) {
        if (!staleExternalReformat) {
            NodePtr shuffleIn0 = staleExternalShuffle->getInput(0);
            if (shuffleIn0 && shuffleIn0->isActivated() && shuffleIn0->getLabel() == kFluxExternalMaskReformatLabel) {
                staleExternalReformat = shuffleIn0;
            }
        }
        if (!staleUnpremult) {
            NodePtr shuffleIn1 = staleExternalShuffle->getInput(1);
            if (shuffleIn1 && shuffleIn1->isActivated() && shuffleIn1->getLabel() == kFluxLayerMaskUnpremultLabel) {
                staleUnpremult = shuffleIn1;
            }
        }
    }
    if (staleExternalReformat && staleExternalReformat->isActivated() && !staleExternalRead) {
        NodePtr reformatIn0 = staleExternalReformat->getInput(0);
        if (reformatIn0 && reformatIn0->isActivated() && reformatIn0->getLabel() == kFluxExternalMaskReadLabel) {
            staleExternalRead = reformatIn0;
        }
    }

    if (stalePremult && stalePremult->isActivated()) {
        stalePremult->deactivate(std::list<NodePtr>(), false, true);
    }
    if (staleRoto && staleRoto->isActivated()) {
        staleRoto->deactivate(std::list<NodePtr>(), false, true);
    }
    if (staleUnpremult && staleUnpremult->isActivated()) {
        staleUnpremult->deactivate(std::list<NodePtr>(), false, true);
    }
    if (staleExternalShuffle && staleExternalShuffle->isActivated()) {
        staleExternalShuffle->deactivate(std::list<NodePtr>(), false, true);
    }
    if (staleExternalReformat && staleExternalReformat->isActivated()) {
        staleExternalReformat->deactivate(std::list<NodePtr>(), false, true);
    }
    if (staleExternalRead && staleExternalRead->isActivated()) {
        staleExternalRead->deactivate(std::list<NodePtr>(), false, true);
    }

    // Clear model refs if they point to the deactivated nodes
    if (layer.maskApplyNode && !layer.maskApplyNode->isActivated()) {
        layer.maskApplyNode.reset();
    }
    for (int m = 0; m < layer.masks.size(); ++m) {
        if (layer.masks[m].effectIndex < 0) {
            if (layer.masks[m].maskNode && !layer.masks[m].maskNode->isActivated()) {
                layer.masks[m].maskNode.reset();
            }
            if (layer.masks[m].reformatNode && !layer.masks[m].reformatNode->isActivated()) {
                layer.masks[m].reformatNode.reset();
            }
        }
    }

    return true;
}

// ====================================================================
// Inline chain preservation helpers (T062)
//
// When the user manually inserts nodes between Flux-owned nodes
// (e.g. Gizmo -> manualBlur -> FluxEffect), we want to preserve that
// chain during rebuild rather than overwriting it.
// ====================================================================

// Walk upstream from `start`, return true if `target` is found (directly or
// transitively). Optional `stop` set prevents traversal past known boundaries.
static bool
upstreamContainsNode(const NodePtr& start,
                      const NodePtr& target,
                      const QSet<Node*>* stop = nullptr)
{
    if (!start || !target) {
        return false;
    }
    QSet<Node*> visited;
    QList<NodePtr> queue;
    queue.push_back(start);
    while (!queue.isEmpty()) {
        NodePtr current = queue.takeLast();
        if (!current) continue;
        Node* raw = current.get();
        if (visited.contains(raw)) continue;
        if (stop && stop->contains(raw)) continue;
        visited.insert(raw);
        if (raw == target.get()) {
            return true;
        }
        int nInputs = current->getNInputs();
        for (int i = 0; i < nInputs; ++i) {
            NodePtr inp = current->getInput(i);
            if (inp) {
                queue.push_back(inp);
            }
        }
    }
    return false;
}

static bool
upstreamContainsFluxOwnedLayerMaskNode(const NodePtr& start)
{
    if (!start) {
        return false;
    }
    QSet<Node*> visited;
    QList<NodePtr> queue;
    queue.push_back(start);
    while (!queue.isEmpty()) {
        NodePtr cur = queue.takeLast();
        if (!cur) continue;
        Node* raw = cur.get();
        if (visited.contains(raw)) continue;
        visited.insert(raw);
        if (isFluxOwnedLayerMaskLabel(cur->getLabel())) {
            return true;
        }
        int nInputs = cur->getNInputs();
        for (int i = 0; i < nInputs; ++i) {
            NodePtr inp = cur->getInput(i);
            if (inp) {
                queue.push_back(inp);
            }
        }
    }
    return false;
}

// Determine the correct input to connect to `target`'s `inputIndex`.
// If the current input is a manual chain that eventually reaches
// `expectedSource`, return the current input (preserving the chain).
// Otherwise return `expectedSource`.
static NodePtr
preserveInlineChainInput(const NodePtr& target,
                          int inputIndex,
                          const NodePtr& expectedSource)
{
    if (!target) {
        return expectedSource;
    }
    NodePtr currentInput = target->getInput(inputIndex);
    if (!currentInput || currentInput == expectedSource) {
        return expectedSource;
    }
    if (!currentInput->isActivated()) {
        return expectedSource;
    }
    // If the current input eventually reaches expectedSource upstream,
    // the user inserted manual nodes in between — preserve them.
    if (upstreamContainsNode(currentInput, expectedSource)) {
        if (upstreamContainsFluxOwnedLayerMaskNode(currentInput)) {
            return expectedSource;
        }
        return currentInput;
    }
    // No connection to expected source — use the default.
    return expectedSource;
}

static void
fluxDeactivateOwnedNodeAndOutputs(const NodePtr& node)
{
    if (node && node->isActivated()) {
        node->deactivate(std::list<NodePtr>(), true, false, true);
    }
}

static void
fluxCleanupAIMaskOwnedNodes(FluxEffect& effect,
                            bool includeEffectNode)
{
    if (effect.aiMaskShuffleNode && effect.aiMaskShuffleNode != effect.node) {
        fluxDeactivateOwnedNodeAndOutputs(effect.aiMaskShuffleNode);
        effect.aiMaskShuffleNode.reset();
    }
    if (effect.aiMaskReadNode && effect.aiMaskReadNode != effect.node) {
        fluxDeactivateOwnedNodeAndOutputs(effect.aiMaskReadNode);
        effect.aiMaskReadNode.reset();
    }
    if (effect.aiMaskChannelMergeNode && effect.aiMaskChannelMergeNode != effect.node) {
        fluxDeactivateOwnedNodeAndOutputs(effect.aiMaskChannelMergeNode);
        effect.aiMaskChannelMergeNode.reset();
    }
    if (includeEffectNode) {
        fluxDeactivateOwnedNodeAndOutputs(effect.node);
        effect.node.reset();
        effect.aiMaskChannelMergeNode.reset();
    }
}

static void
fluxConfigureAIMaskReadTiming(const NodePtr& readNode,
                              double sourceFrameRate)
{
    if (!readNode) {
        return;
    }

    if (std::isfinite(sourceFrameRate) && sourceFrameRate > 0.0) {
        KnobIPtr customFpsKnobI = readNode->getKnobByName("customFps");
        if (customFpsKnobI) {
            KnobBoolPtr customFpsKnob = std::dynamic_pointer_cast<KnobBool>(customFpsKnobI);
            if (customFpsKnob) {
                customFpsKnob->setValue(true, ViewSpec::all(), 0);
            }
        }
        KnobIPtr frameRateKnobI = readNode->getKnobByName("frameRate");
        if (frameRateKnobI) {
            KnobDoublePtr frameRateKnob = std::dynamic_pointer_cast<KnobDouble>(frameRateKnobI);
            if (frameRateKnob) {
                frameRateKnob->setValue(sourceFrameRate, ViewSpec::all(), 0);
            }
        }
    }
}

static void
fluxConfigureAIMaskTimeOffsetTiming(const NodePtr& timeOffsetNode,
                                    int timeOffset)
{
    if (!timeOffsetNode) {
        return;
    }

    KnobIPtr timeOffsetKnobI = timeOffsetNode->getKnobByName("timeOffset");
    if (timeOffsetKnobI) {
        KnobIntBasePtr timeOffsetKnob = std::dynamic_pointer_cast<KnobIntBase>(timeOffsetKnobI);
        if (timeOffsetKnob) {
            timeOffsetKnob->setValue(timeOffset, ViewSpec::all(), 0);
        }
    }
}
void
Gui::rebuildCompositingGraph(FluxTimeline* timeline)
{
    if (!getApp() || !timeline) {
        return;
    }

    // RAII guard: prevent Flux-owned rebuilds from marking timeline dirty
    struct SyncGuard {
        bool* flag;
        bool prev;
        SyncGuard(bool* ptr) : flag(ptr), prev(*ptr) { *flag = true; }
        ~SyncGuard() { *flag = prev; }
    };
    SyncGuard rebuildGuard(&_imp->_fluxSyncInProgress);
    Q_UNUSED(rebuildGuard);

    const QList<FluxLayer>& constLayers = timeline->getLayers();
    // We need mutable access to set gizmoNode/mergeNode on each layer
    QList<FluxLayer>& layers = const_cast<QList<FluxLayer>&>(constLayers);

    // Find the main composition viewer (never the Flux AI Work Viewer).
    ViewerTab* viewerTab = getFluxMainCompositingViewerTab();

    NodeCollectionPtr collection = std::dynamic_pointer_cast<NodeCollection>(getApp()->getProject());
    if (!collection) {
        return;
    }

    // Clear tracking list only — do NOT deactivate/destroy existing nodes
    _imp->_fluxMergeNodes.clear();

    // Track whether any stale effect references were pruned during this rebuild
    // so we can refresh the effects panel afterward.
    bool effectsPruned = false;

    // ====================================================================
    // Node Graph Layout — Per-Layer Vertical Branch Stack
    //
    //   Three-column layout (left to right):
    //     branchX  = centerX + kGizmoOffsetX   (far left)
    //     maskColX = branchX + kMaskColumnOffsetX (middle corridor)
    //     centerX  = 0.0                        (right — main pipe)
    //
    //   branchX         maskColX       centerX
    //     │                 │              │
    //     │                 │         ┌────┴─────┐
    //     │                 │         │ Reformat │  ← project format canvas
    //     │                 │         └────┬─────┘
    //     │                 │              │  (vertical main pipe, all B inputs)
    //     │                 │              │
    //   ┌──┴──────┐    ┌────┴────┐   ┌────┴───────┐
    //   │ Read 0  │    │  Refmt  │   │            │  ← branch top
    //   ├─────────┤    │  (mask) │   │            │
    //   │ Gizmo 0 │    ├────┬────┤   │            │
    //   │         │    │ Roto│    │   │            │
    //   ├─────────┤    │(mask)│    │   │            │
    //   │ Efx...  │    └─────┘    │   │            │  ← effect stack
    //   │         ├───────────────┘   │  Merge 0   │
    //   └─────────┘                   └─────┬──────┘
    //                                        │
    //   ┌──┴──────┐    ┌────┴────┐   ┌──────┴─────┐
    //   │ Read N  │    │  Refmt  │   │            │
    //   ├─────────┤    │  (mask) │   │            │
    //   │ Gizmo N │    ├────┬────┤   │            │
    //   │ Efx...  │    │ Roto│    │   │  Merge N   │  → Viewer
    //   └─────────┘    └─────┘    │   └────────────┘
    //
    //   Rules:
    //   - Reformat at top center (anchor point)
    //   - Each layer occupies kNodesPerLayer vertical rows:
    //       Row 0: Read node (footage) or Solid gizmo (solid)
    //       Row 1: FluxLayer gizmo (footage) — empty for solid
    //       Row 2: Merge on main pipe (room for future effects between gizmo & merge)
    //   - Merge nodes stacked at centerX (main pipe, right column)
    //   - Read + Gizmo at branchX (far left column)
    //   - Effect-mask Reformat/Roto at maskColX (middle column, vertically stacked)
    //   - No overlaps, uniform vertical spacing
    //   - Chain built bottom-to-top so top timeline layer = foreground
    //   - NEVER destroy existing nodes. Only create missing ones.
    //     Reconnect + reposition on every rebuild.
    // ====================================================================

    // Layout constants
    const double kCenterX = 0.0;         // X position for Reformat + all Merge nodes (main pipe)
    const double kGizmoOffsetX = -650.0; // branch column: Read + Gizmo go far LEFT of main pipe
    const double kYStart = 0.0;          // Reformat Y position (top of tree)
    const double kYSpacing = 120.0;      // vertical gap between each node row
    const int kNodesPerLayer = 3;        // minimum rows per layer: [Read|Solid] / Gizmo / Merge

    // -- Ensure background Reformat node exists --
    if (!_imp->_fluxBgReformatNode) {
        CreateNodeArgs bgArgs("net.sf.openfx.Reformat", collection);
        bgArgs.setProperty<bool>(kCreateNodeArgsPropAutoConnect, false);
        bgArgs.setProperty<bool>(kCreateNodeArgsPropAddUndoRedoCommand, false);
        bgArgs.setProperty<bool>(kCreateNodeArgsPropSettingsOpened, false);
        _imp->_fluxBgReformatNode = getApp()->createNode(bgArgs);
        if (_imp->_fluxBgReformatNode) {
            _imp->_fluxBgReformatNode->setLabel("Flux Background");
            // Set type to "To Project Format" (index 3 for v2 plugin)
            KnobIPtr typeKnob = _imp->_fluxBgReformatNode->getKnobByName("reformatType");
            if (typeKnob) {
                KnobChoicePtr choice = std::dynamic_pointer_cast<KnobChoice>(typeKnob);
                if (choice) {
                    choice->setValue(3, ViewSpec::all(), 0); // eReformatTypeToProjectFormat
                }
            }
        } else {
            fprintf(stderr, "FLUX WARNING: Failed to create background Reformat node\n");
        }
    }
    // Reposition Reformat anchor on every rebuild and strip any auto-connected inputs
    if (_imp->_fluxBgReformatNode) {
        _imp->_fluxBgReformatNode->setPosition(kCenterX, kYStart);
        // Forcibly disconnect all inputs — Reformat is a pure source/background canvas
        for (int input = 0; input < _imp->_fluxBgReformatNode->getNInputs(); ++input) {
            _imp->_fluxBgReformatNode->disconnectInput(input);
        }
    }

    NodePtr lastOutput = _imp->_fluxBgReformatNode; // Start with the background canvas

    // Compute solo state for disabled-logic during wiring (adjustment rows excluded)
    bool anySoloed = false;
    for (int si = 0; si < layers.size(); ++si) {
        if (layers[si].solo && layers[si].type != QString::fromUtf8("adjustment")) { anySoloed = true; break; }
    }

    // Build Merge chain bottom-to-top so that top timeline layers composite on top.
    // i=0 in the loop processes the bottom-most pixel-producing layer first (onto the Reformat bg),
    // then each subsequent layer goes on top, ending with the top timeline layer as foreground.
    QList<int> pixelLayers; // indices of layers that produce pixels, bottom-to-top
    for (int i = layers.size() - 1; i >= 0; --i) {
        const FluxLayer& l = layers[i];
        if (l.type == QString::fromUtf8("null")) continue;
        if (l.type == QString::fromUtf8("footage") && l.filePath.isEmpty() && !l.readerNode) continue;
        if (l.type == QString::fromUtf8("adjustment") && l.effects.isEmpty()) continue;
        pixelLayers.push_back(i);
    }

    // Disconnect all effect input 0s so preserveInlineChainInput doesn't see
    // stale connections after effect reorder. Without this, preserveInlineChainInput
    // preserves the old upstream (which still transitively contains the expected source)
    // creating cycles and orphaned nodes.
    for (int pi = 0; pi < pixelLayers.size(); ++pi) {
        FluxLayer& layer = layers[pixelLayers[pi]];
        for (int e = 0; e < layer.effects.size(); ++e) {
            if (layer.effects[e].node && layer.effects[e].node->getNInputs() > 0) {
                layer.effects[e].node->disconnectInput(0);
            }
        }
    }

    int graphRow = 1;
    for (int pi = 0; pi < pixelLayers.size(); ++pi) {
        int i = pixelLayers[pi];
        FluxLayer& layer = layers[i];

        if (layer.type == QString::fromUtf8("adjustment")) {
            NodePtr adjustmentOutput = lastOutput;
            double effectY = kYStart + graphRow * kYSpacing;
            for (int e = layer.effects.size() - 1; e >= 0; --e) {
                if (!layer.effects[e].node || !layer.effects[e].node->isActivated()) {
                    fprintf(stderr, "FLUX: Dropping stale adjustment effect reference from row %d\n", i);
                    layer.effects.removeAt(e);
                    effectsPruned = true;
                }
            }
            for (int e = 0; e < layer.effects.size(); ++e) {
                FluxEffect& effect = layer.effects[e];
                // Adjustment effects are main-pipe operations — position at kCenterX
                effect.node->setPosition(kCenterX, effectY + e * kYSpacing);
                if (effect.node->getNInputs() > 0) {
                    effect.node->disconnectInput(0);
                    if (effect.enabled && adjustmentOutput && !effect.node->connectInput(adjustmentOutput, 0)) {
                        fprintf(stderr, "FLUX ERROR: adjustment effect connectInput(%s <- %s) failed for row %d\n",
                                effect.node->getLabel().c_str(), adjustmentOutput->getLabel().c_str(), i);
                    }
                }
                // Trim state is controlled by updateAdjustmentTrimKeyframes(); do not call
                // setNodeDisabled() here because that would overwrite disable-key animation.
                // A manually disabled adjustment effect is bypassed by not advancing the pipe.
                _imp->_fluxMergeNodes.push_back(effect.node);
                if (effect.enabled) {
                    adjustmentOutput = effect.node;
                }
            }
            if (adjustmentOutput) {
                lastOutput = adjustmentOutput;
            }
            graphRow += qMax(1, layer.effects.size());
            continue;
        }

        fprintf(stderr, "FLUX REBUILD: layer[%d] '%s' inPoint=%d outPoint=%d timeOffset=%d hasGizmo=%d nodeInit=%d\n",
                i, layer.name.toStdString().c_str(), layer.inPoint, layer.outPoint, layer.timeOffset,
                layer.gizmoNode ? 1 : 0, layer.nodeInitialized ? 1 : 0);

        // -- Create new gizmo ONLY if this layer doesn't have one yet --
        if (!layer.gizmoNode) {
            NodePtr gizmoNode;

            // Normal creation — brand new layer
            QString pluginId;
            if (layer.type == QString::fromUtf8("solid")) {
                pluginId = QString::fromUtf8("net.sf.openfx.FluxSolid");
            } else if (layer.type == QString::fromUtf8("text")) {
                pluginId = QString::fromUtf8("net.sf.openfx.FluxMotionText");
            } else {
                pluginId = QString::fromUtf8("net.sf.openfx.FluxLayer");
            }

            CreateNodeArgs gizmoArgs(pluginId.toStdString(), collection);
            gizmoArgs.setProperty<bool>(kCreateNodeArgsPropAutoConnect, false);
            gizmoArgs.setProperty<bool>(kCreateNodeArgsPropAddUndoRedoCommand, false);
            gizmoArgs.setProperty<bool>(kCreateNodeArgsPropSettingsOpened, false);
            gizmoNode = getApp()->createNode(gizmoArgs);

            if (!gizmoNode) {
                fprintf(stderr, "FLUX ERROR: Failed to create gizmo for layer %d '%s' (type=%s)\n",
                        i, layer.name.toStdString().c_str(), layer.type.toStdString().c_str());
                continue;
            }

            // -- Footage-specific: create external Read node and connect to gizmo --
            if (layer.type == QString::fromUtf8("footage")) {
                std::string filePath = layer.filePath.toStdString();
                getApp()->getProject()->canonicalizePath(filePath);
                CreateNodeArgs readArgs(PLUGINID_NATRON_READ, collection);
                readArgs.setProperty<bool>(kCreateNodeArgsPropAutoConnect, false);
                readArgs.setProperty<bool>(kCreateNodeArgsPropAddUndoRedoCommand, false);
                readArgs.setProperty<bool>(kCreateNodeArgsPropSettingsOpened, false);
                NodePtr readNode = getApp()->createReader(filePath, readArgs);
                if (readNode) {
                    // Force output components to RGBA
                    KnobIPtr outCompKnob = readNode->getKnobByName("outputComponents");
                    if (outCompKnob) {
                        KnobIntBasePtr choiceKnob = std::dynamic_pointer_cast<KnobIntBase>(outCompKnob);
                        if (choiceKnob) {
                            choiceKnob->setValue(0, ViewSpec::all(), 0); // 0 = RGBA
                        }
                    }
                    // Connect Read → Gizmo input (deterministic: disconnect first)
                    gizmoNode->disconnectInput(0);
                    if (!gizmoNode->connectInput(readNode, 0)) {
                        fprintf(stderr, "FLUX ERROR: connectInput(Read→Gizmo) failed for layer %d '%s'\n",
                                i, layer.name.toStdString().c_str());
                    }
                    layer.readerNode = readNode;
                    fprintf(stderr, "FLUX: Created external Read node for layer %d '%s'\n",
                            i, layer.name.toStdString().c_str());
                } else {
                    fprintf(stderr, "FLUX ERROR: Failed to create Read node for layer %d '%s'\n",
                            i, layer.name.toStdString().c_str());
                }
                // Lock the original anchor points immediately — these NEVER change.
                layer.originalInPoint = layer.inPoint;
                if (!layer.nodeInitialized) {
                    layer.originalOutPoint = layer.outPoint;
                }
            }

            // -- Solid-specific: initialize immediately (no file probe needed) --
            // Only run for brand new solids. Split/duplicated solids are already initialized.
            if (layer.type == QString::fromUtf8("solid") && !layer.nodeInitialized) {
                // Use project frame range for the solid's source range
                int projectFirst = 0, projectLast = 100;
                {
                    double pf = 0, pl = 100;
                    getApp()->getProject()->getFrameRange(&pf, &pl);
                    projectFirst = (int)pf;
                    projectLast = (int)pl;
                }

                // Set frame range to project range
                KnobIPtr frameRangeKnob = gizmoNode->getKnobByName("frameRange");
                if (frameRangeKnob) {
                    KnobIntBasePtr int2D = std::dynamic_pointer_cast<KnobIntBase>(frameRangeKnob);
                    if (int2D) {
                        int2D->setValue(projectFirst, ViewSpec::all(), 0);
                        int2D->setValue(projectLast, ViewSpec::all(), 1);
                    }
                }

                // Set timeOffset: timeOffset = frameRangeFirst - inPoint
                KnobIPtr timeOffsetKnob = gizmoNode->getKnobByName("timeOffset");
                if (timeOffsetKnob) {
                    KnobIntBasePtr intKnob = std::dynamic_pointer_cast<KnobIntBase>(timeOffsetKnob);
                    if (intKnob) {
                        intKnob->setValue(0, ViewSpec::all(), 0);
                    }
                }

                // Set before/after to black
                {
                    KnobIPtr beforeKnob = gizmoNode->getKnobByName(std::string("before"));
                    if (beforeKnob) {
                        KnobIntBasePtr choice = std::dynamic_pointer_cast<KnobIntBase>(beforeKnob);
                        if (choice) choice->setValue(2, ViewSpec::all(), 0); // black
                    }
                    KnobIPtr afterKnob = gizmoNode->getKnobByName(std::string("after"));
                    if (afterKnob) {
                        KnobIntBasePtr choice = std::dynamic_pointer_cast<KnobIntBase>(afterKnob);
                        if (choice) choice->setValue(2, ViewSpec::all(), 0); // black
                    }
                }

                // Set the color on the Constant node
                KnobIPtr colorKnob = gizmoNode->getKnobByName("color");
                if (colorKnob) {
                    KnobDoubleBasePtr dblColor = std::dynamic_pointer_cast<KnobDoubleBase>(colorKnob);
                    if (dblColor) {
                        dblColor->setValue(layer.solidColor.redF(), ViewSpec::all(), 0);
                        dblColor->setValue(layer.solidColor.greenF(), ViewSpec::all(), 1);
                        dblColor->setValue(layer.solidColor.blueF(), ViewSpec::all(), 2);
                        dblColor->setValue(1.0, ViewSpec::all(), 3); // alpha = fully opaque
                    }
                }
                // Set center to project format center
                {
                    Format projectFormat;
                    getApp()->getProject()->getProjectDefaultFormat(&projectFormat);
                    double cx = projectFormat.x1 + projectFormat.width() / 2.0;
                    double cy = projectFormat.y1 + projectFormat.height() / 2.0;
                    KnobIPtr centerKnob = gizmoNode->getKnobByName("center");
                    if (centerKnob) {
                        KnobDoubleBasePtr dbl2D = std::dynamic_pointer_cast<KnobDoubleBase>(centerKnob);
                        if (dbl2D) {
                            dbl2D->setValue(cx, ViewSpec::all(), 0);
                            dbl2D->setValue(cy, ViewSpec::all(), 1);
                        }
                    }
                }
                layer.originalFirstFrame = projectFirst;
                layer.originalLastFrame = projectLast;
                layer.inPoint = projectFirst;
                layer.outPoint = projectLast;
                layer.originalInPoint = projectFirst;
                layer.originalOutPoint = projectLast;
                layer.timeOffset = 0;
                layer.nodeInitialized = true;

                fprintf(stderr, "FLUX: solidInit — layer '%s' inPoint=%d outPoint=%d timeOffset=%d\n",
                        layer.name.toStdString().c_str(), layer.inPoint, layer.outPoint, layer.timeOffset);
            }

            // -- Text-specific: initialize immediately (no file probe needed) --
            if (layer.type == QString::fromUtf8("text") && !layer.nodeInitialized) {
                int projectFirst = 0, projectLast = 100;
                {
                    double pf = 0, pl = 100;
                    getApp()->getProject()->getFrameRange(&pf, &pl);
                    projectFirst = (int)pf;
                    projectLast = (int)pl;
                }

                KnobIPtr frameRangeKnob = gizmoNode->getKnobByName("frameRange");
                if (frameRangeKnob) {
                    KnobIntBasePtr int2D = std::dynamic_pointer_cast<KnobIntBase>(frameRangeKnob);
                    if (int2D) {
                        int2D->setValue(projectFirst, ViewSpec::all(), 0);
                        int2D->setValue(projectLast, ViewSpec::all(), 1);
                    }
                }

                KnobIPtr timeOffsetKnob = gizmoNode->getKnobByName("timeOffset");
                if (timeOffsetKnob) {
                    KnobIntBasePtr intKnob = std::dynamic_pointer_cast<KnobIntBase>(timeOffsetKnob);
                    if (intKnob) {
                        intKnob->setValue(0, ViewSpec::all(), 0);
                    }
                }

                {
                    Format projectFormat;
                    getApp()->getProject()->getProjectDefaultFormat(&projectFormat);
                    double cx = projectFormat.x1 + projectFormat.width() / 2.0;
                    double cy = projectFormat.y1 + projectFormat.height() / 2.0;
                    KnobIPtr centerKnob = gizmoNode->getKnobByName("center");
                    if (centerKnob) {
                        KnobDoubleBasePtr dbl2D = std::dynamic_pointer_cast<KnobDoubleBase>(centerKnob);
                        if (dbl2D) {
                            dbl2D->setValue(cx, ViewSpec::all(), 0);
                            dbl2D->setValue(cy, ViewSpec::all(), 1);
                        }
                    }
                }

                layer.originalFirstFrame = projectFirst;
                layer.originalLastFrame = projectLast;
                layer.inPoint = projectFirst;
                layer.outPoint = projectLast;
                layer.originalInPoint = projectFirst;
                layer.originalOutPoint = projectLast;
                layer.timeOffset = 0;
                layer.nodeInitialized = true;

                fprintf(stderr, "FLUX: textInit — layer '%s' inPoint=%d outPoint=%d timeOffset=%d\n",
                        layer.name.toStdString().c_str(), layer.inPoint, layer.outPoint, layer.timeOffset);
            }

            layer.gizmoNode = gizmoNode;
            ensureFluxNodeRegisteredWithAnimationEditors(this, layer.gizmoNode);
        }

        if (layer.type == QString::fromUtf8("text")) {
            installFluxTextFontSync(layer.gizmoNode, this);
        }

        // -- Register transform overlay handles (runs every rebuild for every gizmo) --
        // This makes translate/rotate/scale/center/skew handles appear in the viewer.
        // Must run for duplicated/pasted gizmos too, not just newly created ones.
        {
            NodePtr transformNode = layer.gizmoNode;
            const bool isTextLayer = (layer.type == QString::fromUtf8("text"));

            KnobIPtr translateKnob = transformNode->getKnobByName(isTextLayer ? "Text1center" : "translate");
            KnobIPtr scaleKnob = transformNode->getKnobByName(isTextLayer ? "Text1scale" : "scale");
            KnobIPtr rotateKnob = transformNode->getKnobByName(isTextLayer ? "Text1rotate" : "rotate");
            KnobIPtr centerKnob = transformNode->getKnobByName(isTextLayer ? "textOverlayCenter" : "center");
            KnobIPtr uniformKnob = transformNode->getKnobByName(isTextLayer ? "Text1uniform" : "uniform");
            KnobIPtr skewXKnob = transformNode->getKnobByName(isTextLayer ? "Text1skewX" : "skewX");
            KnobIPtr skewYKnob = transformNode->getKnobByName(isTextLayer ? "Text1skewY" : "skewY");
            KnobIPtr skewOrderKnob = transformNode->getKnobByName(isTextLayer ? "Text1skewOrder" : "skewOrder");
            KnobIPtr interactiveKnob = transformNode->getKnobByName(isTextLayer ? "Text1interactive" : "interactive");
            if (isTextLayer && (!translateKnob || !scaleKnob || !rotateKnob || !centerKnob)) {
                translateKnob = transformNode->getKnobByName("translate");
                scaleKnob = transformNode->getKnobByName("scale");
                rotateKnob = transformNode->getKnobByName("rotate");
                centerKnob = transformNode->getKnobByName("center");
                uniformKnob = transformNode->getKnobByName("uniform");
                skewXKnob = transformNode->getKnobByName("skewX");
                skewYKnob = transformNode->getKnobByName("skewY");
                skewOrderKnob = transformNode->getKnobByName("skewOrder");
                interactiveKnob = transformNode->getKnobByName("interactive");
            }

            KnobDoublePtr translateDbl = std::dynamic_pointer_cast<KnobDouble>(translateKnob);
            KnobDoublePtr scaleDbl = std::dynamic_pointer_cast<KnobDouble>(scaleKnob);
            KnobDoublePtr rotateDbl = std::dynamic_pointer_cast<KnobDouble>(rotateKnob);
            KnobDoublePtr centerDbl = std::dynamic_pointer_cast<KnobDouble>(centerKnob);
            KnobBoolPtr uniformBool = std::dynamic_pointer_cast<KnobBool>(uniformKnob);
            KnobDoublePtr skewXDbl = std::dynamic_pointer_cast<KnobDouble>(skewXKnob);
            KnobDoublePtr skewYDbl = std::dynamic_pointer_cast<KnobDouble>(skewYKnob);
            KnobChoicePtr skewOrderChoice = std::dynamic_pointer_cast<KnobChoice>(skewOrderKnob);
            KnobBoolPtr interactiveBool = std::dynamic_pointer_cast<KnobBool>(interactiveKnob);

            if (translateDbl && scaleDbl && rotateDbl && centerDbl) {
                transformNode->addTransformInteract(
                    translateDbl,
                    scaleDbl,
                    uniformBool,
                    rotateDbl,
                    skewXDbl,
                    skewYDbl,
                    skewOrderChoice,
                    centerDbl,
                    KnobBoolPtr(),  // invert (null)
                    interactiveBool
                );
                fprintf(stderr, "FLUX: Registered transform overlay on gizmo for layer %d\n", i);
            } else {
                fprintf(stderr, "FLUX WARNING: Could not find all transform knobs on gizmo for overlay registration (layer %d)\n", i);
            }
        }

        // -- Reposition per-layer branch stack (runs every rebuild) --
        // Each layer uses kNodesPerLayer rows:
        //   Row 0: Read (footage) or Solid gizmo — branch top
        //   Row 1: FluxLayer gizmo (footage only)
        //   Row 2+: Merge (main pipe) — row index kNodesPerLayer-1
        {
            double branchTopY = kYStart + graphRow * kYSpacing;

            if (layer.type == QString::fromUtf8("footage") && layer.readerNode) {
                // Footage: Read at branch top, Gizmo one row below
                layer.readerNode->setPosition(kCenterX + kGizmoOffsetX, branchTopY);
                layer.gizmoNode->setPosition(kCenterX + kGizmoOffsetX, branchTopY + kYSpacing);
            } else {
                // Solid (or footage without reader): gizmo at branch top
                layer.gizmoNode->setPosition(kCenterX + kGizmoOffsetX, branchTopY);
            }
        }

        // -- Re-ensure Read → FluxLayer input 0 is intact (deterministic wiring) --
        if (layer.readerNode && layer.gizmoNode) {
            layer.gizmoNode->disconnectInput(0);
            if (!layer.gizmoNode->connectInput(layer.readerNode, 0)) {
                fprintf(stderr, "FLUX ERROR: reconnectInput(Read→Gizmo) failed for layer %d '%s' on rebuild\n",
                        i, layer.name.toStdString().c_str());
            }
        }

        // Track this gizmo in our node list
        _imp->_fluxMergeNodes.push_back(layer.gizmoNode);

        NodePtr layerOutput = layer.gizmoNode;
        const int sourceRows = (layer.type == QString::fromUtf8("footage") && layer.readerNode) ? 2 : 1;
        int totalEffectAndMaskRows = 0;
        // Will be set after pruning stale effects below.

        // Scope for effect processing: prune, plan layout, position, wire
        {
            // Backward pass: prune stale effects first
            for (int e = layer.effects.size() - 1; e >= 0; --e) {
                FluxEffect& effect = layer.effects[e];
                if (effect.aiMaskUsage == QString::fromUtf8("layer-alpha") &&
                    (!effect.node || !effect.node->isActivated())) {
                    CreateNodeArgs mergeArgs(PLUGINID_FLUX_CHANNEL_MERGE, collection);
                    mergeArgs.setProperty<bool>(kCreateNodeArgsPropAutoConnect, false);
                    mergeArgs.setProperty<bool>(kCreateNodeArgsPropAddUndoRedoCommand, false);
                    mergeArgs.setProperty<bool>(kCreateNodeArgsPropSettingsOpened, false);
                    effect.node = getApp()->createNode(mergeArgs);
                    effect.aiMaskChannelMergeNode = effect.node;
                    effect.pluginId = QString::fromUtf8(PLUGINID_FLUX_CHANNEL_MERGE);
                    if (effect.node) {
                        effect.node->setLabel(effect.label.isEmpty() ? "AI Mask Alpha" : effect.label.toStdString());
                        if (effect.aiMaskSourceChannel.isEmpty()) {
                            effect.aiMaskSourceChannel = QString::fromUtf8("red");
                        }
                        if (effect.aiMaskOperation.isEmpty()) {
                            effect.aiMaskOperation = QString::fromUtf8("max");
                        }
                        fluxConfigureAIChannelMergeNode(effect.node, effect.aiMaskSourceChannel, effect.aiMaskOperation);
                    }
                }
                if (!layer.effects[e].node || !layer.effects[e].node->isActivated()) {
                    fprintf(stderr, "FLUX: Dropping stale effect reference from layer %d '%s'\n",
                            i, layer.name.toStdString().c_str());
                    fluxCleanupAIMaskOwnedNodes(layer.effects[e], true);
                    layer.effects.removeAt(e);
                    effectsPruned = true;
                }
            }

            // -- Effect mask layout solving (T083) --
            // Constraint layout: each masked effect owns a horizontal mask lane.
            // The mask root (Roto) stays at the same Y as the effect it masks.
            // The mask Reformat sits 1 row above the effect.
            // If that row would collide with the previous layer-tree node,
            // push this effect and all subsequent effects down to create
            // clear visual separation. Shifts are cumulative.

            // Per-effect mask indices (which layer.masks entries belong to each effect)
            QVector<QVector<int>> effectMaskIndices(layer.effects.size());
            for (int e = 0; e < layer.effects.size(); ++e) {
                for (int m = 0; m < layer.masks.size(); ++m) {
                    if (layer.masks[m].enabled && layer.masks[m].effectIndex == e) {
                        effectMaskIndices[e].push_back(m);
                    }
                }
            }

            // Assign initial rows: 1 row per effect, consecutive
            QVector<int> effectRows(layer.effects.size());
            {
                int row = graphRow + sourceRows;
                for (int e = 0; e < layer.effects.size(); ++e) {
                    effectRows[e] = row++;
                }
            }

            // Constraint solve: for each masked effect, ensure the mask
            // Reformat (at effectRow - 1) does not visually collide with
            // the previous layer-tree node above it.
            for (int e = 0; e < layer.effects.size(); ++e) {
                if (!effectMaskIndices[e].isEmpty()) {
                    int maskReformatRow = effectRows[e] - 1;
                    int prevRow;
                    if (e > 0) {
                        prevRow = effectRows[e - 1];
                    } else {
                        prevRow = graphRow + sourceRows - 1;
                    }
                    if (maskReformatRow <= prevRow) {
                        int shift = prevRow - maskReformatRow + 1;
                        // Push this effect and all subsequent effects down
                        for (int j = e; j < layer.effects.size(); ++j) {
                            effectRows[j] += shift;
                        }
                    }
                }
            }

            // Compute total rows occupied by the effects section
            if (layer.effects.isEmpty()) {
                totalEffectAndMaskRows = 0;
            } else {
                totalEffectAndMaskRows = effectRows.last() - (graphRow + sourceRows) + 1;
            }
            // -- Forward pass: position and wire effects using planned layout --
            for (int e = 0; e < layer.effects.size(); ++e) {
                FluxEffect& effect = layer.effects[e];
                const bool isAILayerAlpha = effect.aiMaskUsage == QString::fromUtf8("layer-alpha");
                const bool isAIEffectMask = effect.aiMaskUsage == QString::fromUtf8("effect-mask");
                const double plannedEffectY = kYStart + effectRows[e] * kYSpacing;

                if (effect.isAIMaskCopy || isAILayerAlpha || isAIEffectMask) {
                    if (effect.aiMaskSourceChannel.isEmpty()) {
                        effect.aiMaskSourceChannel = QString::fromUtf8("red");
                    }
                    if ((!effect.aiMaskReadNode || !effect.aiMaskReadNode->isActivated()) && !effect.aiMaskSourceRelativePath.isEmpty()) {
                        std::string maskPath = QDir(getApp()->getProject()->getProjectPath()).filePath(effect.aiMaskSourceRelativePath).toStdString();
                        getApp()->getProject()->canonicalizePath(maskPath);
                        CreateNodeArgs readArgs(PLUGINID_NATRON_READ, collection);
                        readArgs.setProperty<bool>(kCreateNodeArgsPropAutoConnect, false);
                        readArgs.setProperty<bool>(kCreateNodeArgsPropAddUndoRedoCommand, false);
                        readArgs.setProperty<bool>(kCreateNodeArgsPropSettingsOpened, false);
                        effect.aiMaskReadNode = getApp()->createReader(maskPath, readArgs);
                    }
                    if (effect.aiMaskReadNode) {
                        fluxConfigureAIMaskReadTiming(effect.aiMaskReadNode, layer.sourceFrameRate);
                        effect.aiMaskReadNode->setPosition(kCenterX + kGizmoOffsetX - 260.0, plannedEffectY);
                    }
                    if ((!effect.aiMaskTimeOffsetNode || !effect.aiMaskTimeOffsetNode->isActivated()) && effect.aiMaskReadNode) {
                        CreateNodeArgs timeOffsetArgs(PLUGINID_OFX_TIMEOFFSET, collection);
                        timeOffsetArgs.setProperty<bool>(kCreateNodeArgsPropAutoConnect, false);
                        timeOffsetArgs.setProperty<bool>(kCreateNodeArgsPropAddUndoRedoCommand, false);
                        timeOffsetArgs.setProperty<bool>(kCreateNodeArgsPropSettingsOpened, false);
                        effect.aiMaskTimeOffsetNode = getApp()->createNode(timeOffsetArgs);
                    }
                    if (effect.aiMaskTimeOffsetNode) {
                        fluxConfigureAIMaskTimeOffsetTiming(effect.aiMaskTimeOffsetNode, layer.timeOffset - effect.aiMaskBaseTimeOffset);
                        effect.aiMaskTimeOffsetNode->setPosition(kCenterX + kGizmoOffsetX - 195.0, plannedEffectY);
                        effect.aiMaskTimeOffsetNode->disconnectInput(0);
                        if (effect.aiMaskReadNode && !effect.aiMaskTimeOffsetNode->connectInput(effect.aiMaskReadNode, 0)) {
                            fprintf(stderr, "FLUX ERROR: AI mask TimeOffset input connect failed for layer %d effect %d\n", i, e);
                        }
                    }
                    if (effect.isAIMaskCopy && !effect.aiMaskTargetPlane.isEmpty()) {
                        std::vector<std::string> channels;
                        channels.push_back("r"); channels.push_back("g"); channels.push_back("b"); channels.push_back("a");
                        effect.node->addUserComponents(ImagePlaneDesc(effect.aiMaskTargetPlane.toStdString(), effect.aiMaskTargetPlane.toStdString(), "RGBA", channels));
                        KnobStringPtr targetString = std::dynamic_pointer_cast<KnobString>(effect.node->getKnobByName("targetPlane"));
                        if (targetString) targetString->setValue(effect.aiMaskTargetPlane.toStdString(), ViewSpec::all(), 0, true);
                    }
                    if (isAILayerAlpha) {
                        if (effect.aiMaskOperation.isEmpty()) {
                            effect.aiMaskOperation = QString::fromUtf8("max");
                        }
                        effect.aiMaskChannelMergeNode = effect.node;
                    }
                    if (isAIEffectMask) {
                        if (!effect.aiMaskShuffleNode || !effect.aiMaskShuffleNode->isActivated()) {
                            CreateNodeArgs shuffleArgs(PLUGINID_OFX_SHUFFLE, collection);
                            shuffleArgs.setProperty<bool>(kCreateNodeArgsPropAutoConnect, false);
                            shuffleArgs.setProperty<bool>(kCreateNodeArgsPropAddUndoRedoCommand, false);
                            shuffleArgs.setProperty<bool>(kCreateNodeArgsPropSettingsOpened, false);
                            effect.aiMaskShuffleNode = getApp()->createNode(shuffleArgs);
                            if (effect.aiMaskShuffleNode) {
                                effect.aiMaskShuffleNode->setLabel("AI Effect Mask");
                            }
                        }
                        if (effect.aiMaskShuffleNode) {
                            fluxConfigureAIShuffleAlphaNode(effect.aiMaskShuffleNode, effect.aiMaskSourceChannel);
                            effect.aiMaskShuffleNode->setPosition(kCenterX + kGizmoOffsetX - 130.0, plannedEffectY);
                            effect.aiMaskShuffleNode->disconnectInput(0);
                            NodePtr maskSource = effect.aiMaskTimeOffsetNode ? effect.aiMaskTimeOffsetNode : effect.aiMaskReadNode;
                            if (maskSource && !effect.aiMaskShuffleNode->connectInput(maskSource, 0)) {
                                fprintf(stderr, "FLUX ERROR: AI effect mask Shuffle input connect failed for layer %d effect %d\n", i, e);
                            }
                        }
                    }
                }
                effect.node->setPosition(kCenterX + kGizmoOffsetX, plannedEffectY);
                if (effect.node->getNInputs() > 0) {
                    // Preserve any manual inline chain the user inserted
                    NodePtr sourceInput = preserveInlineChainInput(effect.node, 0, layerOutput);
                    effect.node->disconnectInput(0);
                    if (sourceInput && !effect.node->connectInput(sourceInput, 0)) {
                        fprintf(stderr, "FLUX ERROR: effect connectInput(%s <- %s) failed for layer %d\n",
                                effect.node->getLabel().c_str(), sourceInput->getLabel().c_str(), i);
                    }
                    if (effect.isAIMaskCopy && effect.node->getNInputs() > 1) {
                        effect.node->disconnectInput(1);
                        NodePtr maskSource = effect.aiMaskTimeOffsetNode ? effect.aiMaskTimeOffsetNode : effect.aiMaskReadNode;
                        if (maskSource && !effect.node->connectInput(maskSource, 1)) {
                            fprintf(stderr, "FLUX ERROR: AI mask input connect failed for layer %d effect %d\n", i, e);
                        }
                    }
                    if (isAILayerAlpha && effect.node->getNInputs() > 1) {
                        effect.node->disconnectInput(1);
                        NodePtr maskSource = effect.aiMaskTimeOffsetNode ? effect.aiMaskTimeOffsetNode : effect.aiMaskReadNode;
                        if (maskSource && !effect.node->connectInput(maskSource, 1)) {
                            fprintf(stderr, "FLUX ERROR: AI ChannelMerge mask input connect failed for layer %d effect %d\n", i, e);
                        }
                    }
                }
                effect.node->setNodeDisabled(!effect.enabled);

                // -- Effect mask wiring (T061) --
                int maskInputIdx = discoverMaskInput(effect.node);
                if (maskInputIdx >= 0) {
                    if (isAIEffectMask && effect.aiMaskShuffleNode) {
                        effect.node->disconnectInput(maskInputIdx);
                        if (!effect.node->connectInput(effect.aiMaskShuffleNode, maskInputIdx)) {
                            fprintf(stderr, "FLUX WARNING: AI effect mask connectInput(%s mask %d) failed for layer %d effect %d\n",
                                    effect.node->getLabel().c_str(), maskInputIdx, i, e);
                        }
                    } else if (!effectMaskIndices[e].isEmpty()) {
                        // Use the first planned mask for this effect
                        int mIdx = effectMaskIndices[e][0];
                        FluxMask& emask = layer.masks[mIdx];
                        // Mask root aligns with its effect (same Y)
                        const double plannedMaskY = kYStart + effectRows[e] * kYSpacing;
                        const double effectPosX = kCenterX + kGizmoOffsetX;
                        if (emask.reformatNode || !emask.maskNode) {
                            // Timeline-created mask: position using planned layout
                            if (ensureMaskSourceNodes(this, emask, collection,
                                                      effectPosX, plannedMaskY)) {
                                effect.node->disconnectInput(maskInputIdx);
                                if (!effect.node->connectInput(emask.maskNode, maskInputIdx)) {
                                    fprintf(stderr, "FLUX WARNING: effect mask connectInput(%s mask %d) failed for layer %d effect %d\n",
                                            effect.node->getLabel().c_str(), maskInputIdx, i, e);
                                }
                            }
                        } else if (emask.maskNode) {
                            // Nodegraph-discovered mask (no reformatNode): position only, do not rewire
                            emask.maskNode->setPosition(effectPosX + kMaskColumnOffsetX, plannedMaskY);
                            // Do NOT disconnect/reconnect — preserve existing nodegraph wiring
                        }
                    } else {
                        // No timeline mask for this effect. But don't disconnect if
                        // a nodegraph-created mask is already connected.
                        NodePtr existingMask = effect.node->getInput(maskInputIdx);
                        if (!existingMask) {
                            // No connection at all — safe to ensure it's clear
                            effect.node->disconnectInput(maskInputIdx);
                        }
                        // If existingMask is non-null, it was connected in the nodegraph — leave it
                    }
                }

                // V1: warn about multiple enabled masks for the same effect
                if (maskInputIdx >= 0) {
                    if (effectMaskIndices[e].size() > 1) {
                        fprintf(stderr, "FLUX: Multiple enabled effect masks on effect %s; V1 uses first only\n",
                                effect.node->getLabel().c_str());
                    }
                }

                _imp->_fluxMergeNodes.push_back(effect.node);
                layerOutput = effect.node;
            }

            // -- Position additional nodegraph-discovered mask nodes (beyond first) --
            // The loop above handles the first mask per effect for wiring.
            // Any additional enabled masks for the same effect get positioned
            // in their planned rows without rewiring.
            for (int e = 0; e < layer.effects.size(); ++e) {
                for (int mi = 1; mi < effectMaskIndices[e].size(); ++mi) {
                    int mIdx = effectMaskIndices[e][mi];
                    FluxMask& extraMask = layer.masks[mIdx];
                    const double plannedMaskY = kYStart + effectRows[e] * kYSpacing;
                    const double maskColX = kCenterX + kGizmoOffsetX + kMaskColumnOffsetX;
                    if (extraMask.reformatNode || !extraMask.maskNode) {
                        // Timeline-created: position both Reformat and Roto
                        // Use effect anchor X (no mask column offset) — ensureMaskSourceNodes
                        // adds kMaskColumnOffsetX internally to its anchor.
                        const double effectPosX = kCenterX + kGizmoOffsetX;
                        ensureMaskSourceNodes(this, extraMask, collection, effectPosX, plannedMaskY);
                    } else if (extraMask.maskNode) {
                        // Nodegraph-discovered: position only, no rewire
                        extraMask.maskNode->setPosition(maskColX, plannedMaskY);
                    }
                }
            }
        }

        // -- Layer mask wiring (T064) --
        // V1: single enabled layer mask per layer, inline in the source pipe.
        // Chain: source -> [Unpremult] -> Roto/Shuffle -> [Premult if enabled] -> Merge A
        // No Merge(in), no side branch. maskApplyNode = inline Premult when enabled.
        FluxMask* lmask = firstEnabledLayerMask(layer);
        if (lmask) {
            // Clean up old Merge(in)/Reformat artifacts from pre-T064
            cleanupOldLayerMaskArtifacts(layer, *lmask);

            // Determine if Unpremult is needed (conservative V1 rule):
            // - Footage layers: assume alpha may exist
            // - Layers with effects before mask: assume alpha may exist
            // - Text layers: generated alpha/antialiased edges
            // - Pure solid with no prior effects: skip
            bool needsUnpremult = false;
            if (layer.type == QString::fromUtf8("footage") ||
                layer.type == QString::fromUtf8("text")) {
                needsUnpremult = true;
            }
            if (!layer.effects.isEmpty()) {
                needsUnpremult = true;
            }

            // Compute positions for inline mask nodes
            int sourceRows = (layer.type == QString::fromUtf8("footage") && layer.readerNode) ? 2 : 1;
            double maskBaseY = kYStart + (graphRow + sourceRows + totalEffectAndMaskRows) * kYSpacing;

            // Ensure Roto node exists
            if (!lmask->maskNode || !lmask->maskNode->isActivated()) {
                CreateNodeArgs rotoArgs(PLUGINID_NATRON_ROTO, collection);
                rotoArgs.setProperty<bool>(kCreateNodeArgsPropAutoConnect, false);
                rotoArgs.setProperty<bool>(kCreateNodeArgsPropAddUndoRedoCommand, false);
                rotoArgs.setProperty<bool>(kCreateNodeArgsPropSettingsOpened, false);
                lmask->maskNode = getApp()->createNode(rotoArgs);
                if (lmask->maskNode) {
                    lmask->maskNode->setLabel(kFluxMaskRotoLabel);
                    lmask->pluginId = QString::fromUtf8(PLUGINID_NATRON_ROTO);
                }
            }
            // Position Roto in branch column
            if (lmask->maskNode) {
                double rotoY = needsUnpremult ? maskBaseY + kYSpacing : maskBaseY;
                lmask->maskNode->setPosition(kCenterX + kGizmoOffsetX, rotoY);
            }

            // Ensure inline Premult (stored in maskApplyNode)
            double premultY = needsUnpremult ? maskBaseY + 2 * kYSpacing : maskBaseY + kYSpacing;
            NodePtr premultNode = ensureInlineLayerMaskPremultNode(this, layer, collection,
                                                                   kCenterX + kGizmoOffsetX, premultY);

            // Optional Unpremult
            NodePtr unpremultNode;
            if (needsUnpremult) {
                unpremultNode = ensureInlineLayerMaskUnpremultNode(this, *lmask, collection,
                                                                    kCenterX + kGizmoOffsetX, maskBaseY);
            }

            if (lmask->maskNode && premultNode) {
                // Wire inline chain preserving manual nodes at each boundary
                // source -> [Unpremult input 0] -> Roto input 0 -> [Premult input 0 if enabled]
                NodePtr rotoExpectedSource = layerOutput;
                if (unpremultNode) {
                    // Wire: layerOutput -> Unpremult input 0
                    NodePtr unpremultInput = preserveInlineChainInput(unpremultNode, 0, layerOutput);
                    unpremultNode->disconnectInput(0);
                    unpremultNode->connectInput(unpremultInput, 0);
                    rotoExpectedSource = unpremultNode;
                }

                // Wire: [unpremult or layerOutput] -> Roto input 0
                NodePtr rotoInput = preserveInlineChainInput(lmask->maskNode, 0, rotoExpectedSource);
                lmask->maskNode->disconnectInput(0);
                lmask->maskNode->connectInput(rotoInput, 0);

                if (premultNode) {
                    // Wire: Roto -> Premult input 0
                    NodePtr premultInput = preserveInlineChainInput(premultNode, 0, lmask->maskNode);
                    premultNode->disconnectInput(0);
                    premultNode->connectInput(premultInput, 0);
                }

                layerOutput = premultNode;

                _imp->_fluxMergeNodes.push_back(lmask->maskNode);
                if (premultNode) {
                    _imp->_fluxMergeNodes.push_back(premultNode);
                }
                if (unpremultNode) {
                    _imp->_fluxMergeNodes.push_back(unpremultNode);
                }
            }

            // Log V1: warn about additional enabled layer masks
            int layerMaskCount = 0;
            for (int m = 0; m < layer.masks.size(); ++m) {
                if (layer.masks[m].enabled && layer.masks[m].effectIndex < 0) {
                    ++layerMaskCount;
                }
            }
            if (layerMaskCount > 1) {
                fprintf(stderr, "FLUX NOTE: Layer %d has %d enabled layer masks; V1 uses first only\n",
                        i, layerMaskCount);
            }
        }

        // -- Create Merge node ONLY if this layer doesn't have one yet --
        if (!layer.mergeNode) {
            CreateNodeArgs mgArgs(PLUGINID_OFX_MERGE, collection);
            mgArgs.setProperty<bool>(kCreateNodeArgsPropAutoConnect, false);
            mgArgs.setProperty<bool>(kCreateNodeArgsPropAddUndoRedoCommand, false);
            mgArgs.setProperty<bool>(kCreateNodeArgsPropSettingsOpened, false);
            NodePtr mergeNode = getApp()->createNode(mgArgs);
            if (!mergeNode) {
                CreateNodeArgs mgArgs2("net.sf.openfx.MergePlugin", collection);
                mgArgs2.setProperty<bool>(kCreateNodeArgsPropAutoConnect, false);
                mgArgs2.setProperty<bool>(kCreateNodeArgsPropAddUndoRedoCommand, false);
                mgArgs2.setProperty<bool>(kCreateNodeArgsPropSettingsOpened, false);
                mergeNode = getApp()->createNode(mgArgs2);
            }
            if (!mergeNode) {
                fprintf(stderr, "FLUX ERROR: Failed to create Merge node for layer %d (chain pos %d)\n", i, pi);
                lastOutput = layerOutput;
                continue;
            }

            // -- Sync blending mode from gizmo to NEW Merge --
            {
                KnobIPtr blendKnob = layer.gizmoNode->getKnobByName("blendingMode");
                KnobIPtr opKnob = mergeNode->getKnobByName("operation");
                if (blendKnob && opKnob) {
                    KnobIntBasePtr blendChoice = std::dynamic_pointer_cast<KnobIntBase>(blendKnob);
                    KnobIntBasePtr opChoice = std::dynamic_pointer_cast<KnobIntBase>(opKnob);
                    if (blendChoice && opChoice) {
                        int mode = blendChoice->getValue(0, ViewSpec::current());
                        opChoice->setValue(mode, ViewSpec::all(), 0);

                        // Live sync: when user changes blendingMode, update Merge operation
                        QObject::connect(
                            blendKnob->getSignalSlotHandler().get(),
                            &KnobSignalSlotHandler::valueChanged,
                            this,
                            [opChoice, blendChoice](ViewSpec, int, int) {
                                int newMode = blendChoice->getValue(0, ViewSpec::current());
                                opChoice->setValue(newMode, ViewSpec::all(), 0);
                            }
                        );
                    }
                }
            }

            layer.mergeNode = mergeNode;
        }

        // -- Stale inline layer-mask chain cleanup --
        // When no enabled layer mask exists, prevent preserveInlineChainInput from
        // keeping a stale Flux-owned mask chain alive on Merge A input.
        // This must work even if layer.maskApplyNode was already reset by
        // removeMaskFromLayer — detection is purely topology-based from Merge A.
        {
            if (!lmask && layer.mergeNode && layer.mergeNode->isActivated()) {
                cleanupStaleInlineLayerMaskChain(layer, layer.mergeNode, layerOutput);
            }
        }

        // -- Always reconnect + reposition (layer order may have changed) --
        {
            // Merge sits at the bottom of this layer's branch, on the main pipe.
            int sourceRows = (layer.type == QString::fromUtf8("footage") && layer.readerNode) ? 2 : 1;
            // Inline layer mask rows: Roto/Shuffle + optional Premult + optional Unpremult
            int inlineMaskRows = 0;
            if (lmask) {
                inlineMaskRows = 2; // Roto + Premult
                // Check if Unpremult is needed (same logic as above)
                if (layer.type == QString::fromUtf8("footage") ||
                    layer.type == QString::fromUtf8("text") ||
                    !layer.effects.isEmpty()) {
                    inlineMaskRows = 3; // Unpremult + Roto + Premult
                }
            }
            double mergeY = kYStart + (graphRow + sourceRows + totalEffectAndMaskRows + inlineMaskRows) * kYSpacing;
            layer.mergeNode->setPosition(kCenterX, mergeY);

            // Disconnect stale inputs before reconnecting (handles layer deletion/reorder)
            // But first capture the preserved inline chain before disconnecting
            NodePtr mergeAInput = preserveInlineChainInput(layer.mergeNode, 1, layerOutput);
            layer.mergeNode->disconnectInput(0);
            layer.mergeNode->disconnectInput(1);

            // Connect: input 0 (B) = previous output / background
            //          input 1 (A) = this layer's output
            if (lastOutput) {
                if (!layer.mergeNode->connectInput(lastOutput, 0)) {
                    fprintf(stderr, "FLUX ERROR: Merge connectInput(%d,B=%s) failed for layer %d\n",
                            0, lastOutput->getLabel().c_str(), i);
                }
            }
            // input 1 (A) = preserved inline chain or layerOutput
            if (!layer.mergeNode->connectInput(mergeAInput, 1)) {
                fprintf(stderr, "FLUX ERROR: Merge connectInput(%d,A=%s) failed for layer %d\n",
                        1, mergeAInput ? mergeAInput->getLabel().c_str() : "null", i);
            }

            // Set merge disabled state based on mute/solo
            layer.mergeNode->setNodeDisabled(layer.muted || (anySoloed && !layer.solo));
        }

        _imp->_fluxMergeNodes.push_back(layer.mergeNode);
        lastOutput = layer.mergeNode;
        {
            int sourceRows = (layer.type == QString::fromUtf8("footage") && layer.readerNode) ? 2 : 1;
            int inlineMaskRows = 0;
            if (lmask) {
                inlineMaskRows = 2;
                if (layer.type == QString::fromUtf8("footage") ||
                    layer.type == QString::fromUtf8("text") ||
                    !layer.effects.isEmpty()) {
                    inlineMaskRows = 3;
                }
            }
            graphRow += qMax(kNodesPerLayer, sourceRows + totalEffectAndMaskRows + inlineMaskRows + 1);
        }

        // Classify branches to detect precomp — derived UI state, not serialized
        {
            FluxLayerBranchClassification cls = classifyLayerBranches(layer);
            layer.hasPrecompBranch = cls.hasPrecompBranch;
        }
    }

    // Connect the final output to the main composition viewer only.
    if (lastOutput && viewerTab) {
        ViewerInstance* internalViewer = viewerTab->getInternalNode();
        NodePtr viewerNode = internalViewer ? internalViewer->getNode() : NodePtr();
        if (viewerNode && viewerNode->isActivated() && !isFluxAiWorkViewerNode(viewerNode)) {
            viewerNode->disconnectInput(0);
            bool connected = viewerNode->connectInput(lastOutput, 0);
            (void)connected;
        }
    } else if (!lastOutput) {
        fprintf(stderr, "FLUX WARNING: No compositing output produced for %d layers\n",
                (int)layers.size());
    }

    // -- Deferred initialization of frame range, time offset, center --
    // After the filename is set on the internal Read node, Natron needs time to
    // probe the file and populate its knobs (firstFrame, lastFrame, etc.).
    // We use a single-shot timer to defer the parameter initialization.
    FluxTimeline* timelinePtr = timeline;
    QTimer::singleShot(200, this, [this, timelinePtr]() {
        deferredInitGizmoParams(timelinePtr);
    });

    // Update the timeline display
    timeline->update();

    // If stale effect references were pruned, refresh the effects panel
    // so the UI matches the model (avoids ghost rows for manually-deleted nodes).
    if (effectsPruned && _imp->_fluxEffectsPanel) {
        int selected = timeline->getSelectedLayerIndex();
        if (selected >= 0 && selected < layers.size()) {
            _imp->_fluxEffectsPanel->setActiveLayer(selected, layers[selected].name);
            for (int e = 0; e < layers[selected].effects.size(); ++e) {
                const FluxEffect& fx = layers[selected].effects[e];
                if (fx.node && fx.node->isActivated()) {
                    _imp->_fluxEffectsPanel->addEffect(fx.pluginId, fx.label);
                }
            }
        }
        timeline->refreshVisibleRows();
    }

    // Rebind the Text panel for the selected text layer after rebuild,
    // in case the gizmoNode was just created and layerSelected already
    // fired with a null gizmoNode (e.g. from addTextLayer signal order).
    {
        int selected = timeline->getSelectedLayerIndex();
        if (selected >= 0 && selected < (int)layers.size() &&
            layers[selected].type == QString::fromUtf8("text") &&
            layers[selected].gizmoNode && _imp->_fluxTextPanel) {
            _imp->_fluxTextPanel->setActiveNode(layers[selected].gizmoNode);
            if (_imp->_fluxTextAnimatorPanel) {
                _imp->_fluxTextAnimatorPanel->setActiveNode(layers[selected].gizmoNode);
            }
        }
    }

    fprintf(stderr, "FLUX: Compositing graph rebuilt with %d layers, %d nodes (gizmos + merges)\n",
            (int)layers.size(), (int)_imp->_fluxMergeNodes.size());

    // Store final output node for export panel
    _imp->_fluxFinalOutputNode = lastOutput;

    // -- Update compositing-tree node tracking --
    // Walk upstream from lastOutput and collect all reachable nodes.
    std::set<NodeWPtr, std::owner_less<NodeWPtr>> newTreeNodes;
    if (lastOutput) {
        std::set<NodeWPtr, std::owner_less<NodeWPtr>> visited;
        std::vector<NodeWPtr> stack;
        stack.push_back(lastOutput);
        while (!stack.empty()) {
            NodeWPtr wp = stack.back();
            stack.pop_back();
            if (wp.expired()) continue;
            if (visited.count(wp)) continue;
            visited.insert(wp);
            newTreeNodes.insert(wp);
            NodePtr n = wp.lock();
            if (!n) continue;
            const std::vector<NodeWPtr>& inputs = n->getGuiInputs();
            for (size_t j = 0; j < inputs.size(); ++j) {
                if (!inputs[j].expired()) {
                    stack.push_back(inputs[j]);
                }
            }
        }
    }

    // Diff: disconnect nodes that left the tree
    for (const auto& oldWp : _imp->_fluxCompositingTreeNodes) {
        if (!newTreeNodes.count(oldWp)) {
            NodePtr n = oldWp.lock();
            if (n) {
                QObject::disconnect(n.get(), SIGNAL(inputChanged(int)), this, SLOT(onCompositingTreeNodeChanged(int)));
            }
        }
    }
    // Diff: connect nodes that entered the tree
    for (const auto& newWp : newTreeNodes) {
        if (!_imp->_fluxCompositingTreeNodes.count(newWp)) {
            NodePtr n = newWp.lock();
            if (n) {
                QObject::connect(n.get(), SIGNAL(inputChanged(int)), this, SLOT(onCompositingTreeNodeChanged(int)), Qt::UniqueConnection);
            }
        }
    }
    // Swap the tracked set
    _imp->_fluxCompositingTreeNodes.swap(newTreeNodes);
}

void
Gui::deferredInitGizmoParams(FluxTimeline* timeline)
{
    if (!getApp() || !timeline) {
        return;
    }

    // RAII guard: prevent deferred Flux reconnects from marking timeline dirty
    struct DeferredSyncGuard {
        bool* flag;
        bool prev;
        DeferredSyncGuard(bool* ptr) : flag(ptr), prev(*ptr) { *flag = true; }
        ~DeferredSyncGuard() { *flag = prev; }
    };
    DeferredSyncGuard deferredGuard(&_imp->_fluxSyncInProgress);
    Q_UNUSED(deferredGuard);

    const QList<FluxLayer>& constLayers = timeline->getLayers();
    QList<FluxLayer>& layers = const_cast<QList<FluxLayer>&>(constLayers);
    bool anyUpdated = false;

    for (int i = 0; i < layers.size(); ++i) {
        FluxLayer& layer = layers[i];
        if (!layer.gizmoNode || layer.filePath.isEmpty()) {
            continue;
        }

        // Skip layers that were already successfully initialized
        if (layer.nodeInitialized) {
            continue;
        }

        // Use the external Read node (not internal — Read is outside the gizmo now)
        NodePtr readNode = layer.readerNode;
        if (!readNode) {
            continue;
        }

        // -- Authoritative Read → FluxLayer wiring (after PyPlug setup) --
        // The immediate connection in rebuildCompositingGraph may fire before the
        // PyPlug group's external input / internal Input node is fully wired.
        // This deferred pass is the authoritative connection point.
        {
            layer.gizmoNode->disconnectInput(0);
            bool ok = layer.gizmoNode->connectInput(readNode, 0);
            if (!ok) {
                fprintf(stderr, "FLUX ERROR: deferred Read->FluxLayer connect failed for layer %d '%s' — will retry\n",
                        i, layer.name.toStdString().c_str());
                // Do NOT mark initialized — retry loop will pick this up again
                continue;
            }
            fprintf(stderr, "FLUX: deferred Read->FluxLayer connect OK for layer %d '%s'\n",
                    i, layer.name.toStdString().c_str());
        }

        // Step 1: Trigger "reload" on the file knob — this probes the file,
        // creates the embedded decoder, and populates firstFrame/lastFrame/etc.
        KnobIPtr filenameKnob = readNode->getKnobByName("filename");
        if (filenameKnob) {
            KnobFilePtr fileKnob = std::dynamic_pointer_cast<KnobFile>(filenameKnob);
            if (fileKnob) {
                fileKnob->reloadFile();
            }
        }

        // Force output components to RGBA so the Multiply node (opacity) works on all channels.
        {
            KnobIPtr outCompKnob = readNode->getKnobByName("outputComponents");
            if (outCompKnob) {
                KnobIntBasePtr choiceKnob = std::dynamic_pointer_cast<KnobIntBase>(outCompKnob);
                if (choiceKnob) {
                    choiceKnob->setValue(0, ViewSpec::all(), 0); // 0 = RGBA
                }
            }
        }

        // Step 2: Query the real frame range from the Read node (now populated)
        int mediaFirst = 0;
        int mediaLast = 100;
        {
            EffectInstancePtr readEffect = readNode->getEffectInstance();
            if (readEffect) {
                double first = 0, last = 100;
                readEffect->getFrameRange_public(0, &first, &last);
                mediaFirst = (int)first;
                mediaLast = (int)last;
                const double fps = readEffect->getFrameRate();
                if (std::isfinite(fps) && fps > 0.0) {
                    layer.sourceFrameRate = fps;
                } else {
                    layer.sourceFrameRate = 0.0;
                }
            }
        }

        // For single images (duration <= 1 frame), use the project frame range instead.
        // The source only has 1 frame, but we want the bar to span the full project.
        int projectFirst = 0;
        int projectLast = 100;
        {
            double pf = 0, pl = 100;
            getApp()->getProject()->getFrameRange(&pf, &pl);
            projectFirst = (int)pf;
            projectLast = (int)pl;
        }

        // Update the layer's original media range
        if (mediaLast - mediaFirst <= 0) {
            // Single frame image — use project range as the "virtual" source range.
            // FrameRange will hold the single frame for the entire duration.
            layer.originalFirstFrame = projectFirst;
            layer.originalLastFrame = projectLast;
            mediaFirst = projectFirst;
            mediaLast = projectLast;
        } else {
            layer.originalFirstFrame = mediaFirst;
            layer.originalLastFrame = mediaLast;
        }

        // Set the timeline bar and frameRange to match the actual media range.
        // inPoint = frameRange first, outPoint = frameRange last.
        // originalInPoint/originalOutPoint = same at creation (never change after this).
        layer.inPoint = mediaFirst;
        layer.outPoint = mediaLast;
        layer.originalInPoint = mediaFirst;
        layer.originalOutPoint = mediaLast;
        layer.timeOffset = 0;
        layer.trimStart = 0;
        layer.trimEnd = 0;

        // Step 3: Set FrameRange on the gizmo = inPoint/outPoint
        KnobIPtr frameRangeKnob = layer.gizmoNode->getKnobByName("frameRange");
        if (frameRangeKnob) {
            KnobIntBasePtr int2D = std::dynamic_pointer_cast<KnobIntBase>(frameRangeKnob);
            if (int2D) {
                int2D->setValue(layer.inPoint, ViewSpec::all(), 0);
                int2D->setValue(layer.outPoint, ViewSpec::all(), 1);
            }
        }

        // Set before/after to black (aliased on gizmo)
        {
            KnobIPtr beforeKnob = layer.gizmoNode->getKnobByName(std::string("before"));
            if (beforeKnob) {
                KnobIntBasePtr choice = std::dynamic_pointer_cast<KnobIntBase>(beforeKnob);
                if (choice) choice->setValue(2, ViewSpec::all(), 0); // black
            }
            KnobIPtr afterKnob = layer.gizmoNode->getKnobByName(std::string("after"));
            if (afterKnob) {
                KnobIntBasePtr choice = std::dynamic_pointer_cast<KnobIntBase>(afterKnob);
                if (choice) choice->setValue(2, ViewSpec::all(), 0); // black
            }
        }

        // Step 4: Set TimeOffset
        // timeOffset = how far clip moved from original position = 0 at init
        KnobIPtr timeOffsetKnob = layer.gizmoNode->getKnobByName("timeOffset");
        if (timeOffsetKnob) {
            KnobIntBasePtr intKnob = std::dynamic_pointer_cast<KnobIntBase>(timeOffsetKnob);
            if (intKnob) {
                intKnob->setValue(0, ViewSpec::all(), 0);
            }
        }

        // Step 5: Set Transform center from footage resolution
        double centerX = 960.0;
        double centerY = 540.0;
        {
            EffectInstancePtr readEffect = readNode->getEffectInstance();
            if (readEffect) {
                RectI format = readEffect->getOutputFormat();
                if (format.width() > 0 && format.height() > 0) {
                    centerX = format.x1 + format.width() / 2.0;
                    centerY = format.y1 + format.height() / 2.0;
                }
            }
        }
        KnobIPtr centerKnob = layer.gizmoNode->getKnobByName("center");
        if (centerKnob) {
            KnobDoubleBasePtr dbl2D = std::dynamic_pointer_cast<KnobDoubleBase>(centerKnob);
            if (dbl2D) {
                dbl2D->setValue(centerX, ViewSpec::all(), 0);
                dbl2D->setValue(centerY, ViewSpec::all(), 1);
            }
        }

        // Mark as initialized so we never overwrite these values again
        layer.nodeInitialized = true;

        fprintf(stderr, "FLUX: deferredInit — layer %d '%s' frameRange=%d..%d timeOffset=%d center=(%.0f,%.0f)\n",
                i, layer.filePath.toStdString().c_str(),
                mediaFirst, mediaLast, 0,
                centerX, centerY);
        anyUpdated = true;
    }

    if (anyUpdated) {
        timeline->update();
    }

    // Retry: if any layer with a gizmo is still not initialized, schedule another attempt
    bool needsRetry = false;
    for (int i = 0; i < layers.size(); ++i) {
        if (layers[i].gizmoNode && !layers[i].filePath.isEmpty() && !layers[i].nodeInitialized) {
            needsRetry = true;
            break;
        }
    }
    if (needsRetry) {
        FluxTimeline* timelinePtr = timeline;
        QTimer::singleShot(300, this, [this, timelinePtr]() {
            deferredInitGizmoParams(timelinePtr);
        });
    }
}

void
Gui::syncFluxTimelineFromNodeGraph()
{
    // 1. Guards
    if (!_imp || !getApp() || !_imp->_fluxTimeline) {
        return;
    }

    // RAII guard: prevent our own writes from re-dirtying
    struct SyncGuard {
        bool* flag;
        bool prev;
        SyncGuard(bool* ptr) : flag(ptr), prev(*ptr) { *flag = true; }
        ~SyncGuard() { *flag = prev; }
    };
    SyncGuard syncGuard(&_imp->_fluxSyncInProgress);
    Q_UNUSED(syncGuard);

    FluxTimeline* timeline = _imp->_fluxTimeline;
    NodePtr finalOutput = _imp->_fluxFinalOutputNode;
    NodePtr bgReformat  = _imp->_fluxBgReformatNode;
    if (!finalOutput || !bgReformat) {
        return;
    }

    // 2. Collect tracked nodes
    QList<FluxLayer>& layers = const_cast<QList<FluxLayer>&>(timeline->getLayers());
    QSet<Node*> tracked = collectTrackedNodes(layers);

    // 3. Walk main pipe
    QVector<MainPipeSegment> segments = walkMainPipe(finalOutput, bgReformat);
    if (segments.isEmpty()) {
        return;
    }

    // 4. For each existing layer, find and APPEND new effects
    for (int i = 0; i < layers.size(); ++i) {
        FluxLayer& layer = layers[i];
        if (!layer.mergeNode) {
            continue;
        }

        // Find this layer's Merge in the segments
        bool found = false;
        for (const auto& seg : segments) {
            if (seg.mergeNode == layer.mergeNode) {
                found = true;
                break;
            }
        }
        if (!found) {
            continue;
        }

        // Find new effects for this Merge
        QVector<FluxEffectWithMasks> newEffects = findNewEffects(layer.mergeNode, bgReformat, tracked);
        if (newEffects.isEmpty()) {
            continue;
        }

        // INSERT new effects at correct position (source→Merge order matters).
        // findNewEffects returns source→Merge order (index 0 closest to source).
        // layer.effects is also source→Merge order. New effects must be inserted
        // before the first tracked node downstream of them in the A-input chain.
        //
        // Strategy: walk Merge's A-input chain backward (Merge→source).
        // For each new effect node encountered, find the first tracked effect
        // downstream of it and insert before that effect's position.
        // If no tracked effect is downstream, append.

        // Build a map: Node* → index in layer.effects (for tracked effects only)
        QHash<Node*, int> trackedIndex;
        for (int ei = 0; ei < layer.effects.size(); ++ei) {
            if (layer.effects[ei].node) {
                trackedIndex[layer.effects[ei].node.get()] = ei;
            }
        }

        // Walk the A-input chain from Merge backward to find each new effect's
        // downstream tracked neighbor.
        // newEffects is in source→Merge order. We process in reverse (Merge→source)
        // so each insert adjusts the insertion point correctly.
        int insertBias = 0; // tracks cumulative insert offset
        for (int ni = newEffects.size() - 1; ni >= 0; --ni) {
            const FluxEffectWithMasks& entry = newEffects[ni];
            NodePtr newEffectNode = entry.effect.node;

            // Walk downstream from newEffectNode toward Merge to find the
            // first tracked effect.
            int insertPos = layer.effects.size(); // default: append
            {
                NodePtr downstream = layer.mergeNode->getInput(1); // A-input of Merge
                while (downstream) {
                    if (downstream == newEffectNode) {
                        break; // reached the new node itself, stop
                    }
                    auto it = trackedIndex.find(downstream.get());
                    if (it != trackedIndex.end()) {
                        insertPos = it.value() + insertBias;
                        break;
                    }
                    downstream = downstream->getInput(0);
                }
            }

            layer.effects.insert(insertPos, entry.effect);
            insertBias++;

            // Insert new masks, adjusting effectIndex for the inserted position
            for (const FluxMask& mask : entry.masks) {
                FluxMask m = mask;
                m.effectIndex = insertPos;
                layer.masks.append(m);
            }
        }

        fprintf(stderr, "FLUX SYNC: added %d new effects to layer %d '%s'\n",
                (int)newEffects.size(), i, layer.name.toStdString().c_str());
    }

    // 5. Find new main-pipe effects → adjustment layers
    QVector<NewAdjustmentEffect> newAdjEffects = findNewMainPipeEffects(segments, tracked);
    if (!newAdjEffects.isEmpty()) {
        // Find existing adjustment layer or create new one
        int adjIdx = -1;
        for (int i = 0; i < layers.size(); ++i) {
            if (layers[i].type == QString::fromUtf8("adjustment")) {
                adjIdx = i;
                break;
            }
        }

        if (adjIdx < 0) {
            // Create new adjustment layer
            FluxLayer adjLayer;
            adjLayer.type = QString::fromUtf8("adjustment");
            adjLayer.name = QString::fromUtf8("Adjustment");
            adjLayer.color = QColor(200, 130, 80);
            adjLayer.nodeInitialized = true;
            adjLayer.inPoint = 1;
            adjLayer.outPoint = 100;
            {
                double pf = 0, pl = 100;
                getApp()->getProject()->getFrameRange(&pf, &pl);
                adjLayer.inPoint = (int)pf;
                adjLayer.outPoint = (int)pl;
            }
            layers.prepend(adjLayer);
            adjIdx = 0;
        }

        // Append new effects to adjustment layer
        for (const NewAdjustmentEffect& nae : newAdjEffects) {
            layers[adjIdx].effects.append(nae.fx);
        }

        fprintf(stderr, "FLUX SYNC: added %d new adjustment effects\n", (int)newAdjEffects.size());
    }

    // 6. Check ALL existing effects for new mask connections
    // The user may have connected a Roto/Ramp/etc to an existing effect's mask input
    // in the nodegraph. We need to discover these and add them to the timeline.
    for (int i = 0; i < layers.size(); ++i) {
        FluxLayer& layer = layers[i];
        for (int e = 0; e < layer.effects.size(); ++e) {
            FluxEffect& effect = layer.effects[e];
            if (!effect.node) continue;

            int maskInputIdx = discoverMaskInput(effect.node);
            if (maskInputIdx < 0) continue;

            NodePtr maskStart = effect.node->getInput(maskInputIdx);
            if (!maskStart) continue;

            // Check if this mask is already tracked in the layer
            bool alreadyTracked = false;
            for (const FluxMask& m : layer.masks) {
                if (m.effectIndex == e && m.maskNode && m.maskNode == maskStart) {
                    alreadyTracked = true;
                    break;
                }
            }
            if (alreadyTracked) continue;

            // Walk the mask tree — collect all nodes not already tracked as masks
            QVector<NodePtr> maskTree;
            QSet<Node*> maskVisited;
            NodePtr walk = maskStart;
            while (walk) {
                if (maskVisited.contains(walk.get())) break;
                maskVisited.insert(walk.get());

                // Stop at Read nodes (source boundary)
                {
                    const std::string pid = walk->getPluginID();
                    if (pid == PLUGINID_NATRON_READ || pid == PLUGINID_NATRON_READQT) {
                        maskTree.push_back(walk);
                        break;
                    }
                }
                // Stop at Merge nodes
                if (walk->getPluginID() == PLUGINID_OFX_MERGE) {
                    maskTree.push_back(walk);
                    break;
                }
                // Stop at Flux gizmo nodes
                {
                    const std::string pid = walk->getPluginID();
                    if (pid == "net.sf.openfx.FluxLayer" ||
                        pid == "net.sf.openfx.FluxSolid" ||
                        pid == "net.sf.openfx.FluxMotionText") {
                        maskTree.push_back(walk);
                        break;
                    }
                }
                // Stop at gizmo-internal nodes
                if (dynamic_cast<NodeGroup*>(walk->getGroup().get())) break;
                // Stop at bgReformat
                if (walk == bgReformat) break;

                maskTree.push_back(walk);

                // Stop if this node is already tracked (boundary)
                if (tracked.contains(walk.get())) break;

                walk = walk->getInput(0);
            }

            // Create FluxMask entries only for nodes NOT already present in layer.masks
            // for this effectIndex (BUG-2: compare maskNode pointers, not just maskStart)
            int added = 0;
            for (int m = 0; m < maskTree.size(); ++m) {
                bool alreadyMasked = false;
                for (const FluxMask& existing : layer.masks) {
                    if (existing.effectIndex == e &&
                        existing.maskNode &&
                        existing.maskNode == maskTree[m]) {
                        alreadyMasked = true;
                        break;
                    }
                }
                if (alreadyMasked) continue;

                FluxMask mask;
                mask.type = QString::fromUtf8("effect");
                mask.effectIndex = e;
                mask.name = QString::fromUtf8("Flux Mask (") +
                            QString::fromStdString(maskTree[m]->getLabel()) +
                            QString::fromUtf8(")");
                mask.maskNode = maskTree[m];
                layer.masks.append(mask);
                ++added;
            }

            if (added > 0) {
                fprintf(stderr, "FLUX SYNC: added %d mask nodes to layer %d effect %d '%s'\n",
                        added, i, e, effect.label.toStdString().c_str());
            }
        }
    }

    // 7. Check for entirely new layers (Merge whose A-input has Read node not in tracked)
    // TODO: implement when needed — for now, user adds layers from timeline

    // 7. Refresh visible rows — do NOT call rebuildCompositingGraph.
    // The nodegraph connections are already correct (user made them).
    // rebuildCompositingGraph would disconnect and reconnect ALL effects
    // based on timeline order, destroying manual nodegraph connections.
    timeline->refreshVisibleRows();
    timeline->update();

    // 8. Clear dirty flag
    _imp->_fluxNodeGraphDirty = false;
    fprintf(stderr, "FLUX SYNC: done (additive).\n");
}

NATRON_NAMESPACE_EXIT
