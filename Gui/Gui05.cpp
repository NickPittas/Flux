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
#include <cctype>

#include <stdexcept>
#include <limits>

#include <QCoreApplication>
#include <QThread>
#include <QTimer>

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

#include "Gui/FluxProjectBin.h"
#include "Gui/FluxTimeline.h"
#include "Gui/FluxEffectsPanel.h"
#include "Gui/FluxExportPanel.h"
#include "Gui/FluxT074Harness.h"
#include "Gui/FluxMaskUtils.h"
#include "Gui/DopeSheetEditor.h"
#include "Gui/PropertiesBinWrapper.h"

#include "Engine/EffectInstance.h"
#include "Engine/OutputEffectInstance.h"
#include "Engine/Knob.h"
#include "Engine/KnobTypes.h"
#include "Engine/KnobFile.h"


NATRON_NAMESPACE_ENTER

namespace {

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
    topLeftPane->setAsAnchor(true);

    // ====================================================================
    // 2. Split vertically to create full-width bottom workshop pane
    // ====================================================================
    TabWidget* workshopPane = topLeftPane->splitVertically(false);
    workshopPane->setObjectName_mt_safe( QString::fromUtf8("fluxWorkshopPane") );

    // ====================================================================
    // 3. Split top-left horizontally to create top-center pane
    // ====================================================================
    TabWidget* topCenterPane = topLeftPane->splitHorizontally(false);
    topCenterPane->setObjectName_mt_safe( QString::fromUtf8("fluxTopCenterPane") );
    _imp->_fluxViewerPane = topCenterPane;

    // ====================================================================
    // 4. Split top-center horizontally to create top-right pane
    // ====================================================================
    TabWidget* topRightPane = topCenterPane->splitHorizontally(false);
    topRightPane->setObjectName_mt_safe( QString::fromUtf8("fluxTopRightPane") );

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

    // ====================================================================
    // Populate workshop pane (bottom): Timeline + Dope Sheet + Curve Editor
    // ====================================================================
    FluxTimeline* timeline = new FluxTimeline(this);
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
                      [this, timeline, effectsPanel](int index) {
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
                              effectsPanel->setActiveLayer(index, layers[index].name);
                              for (int e = 0; e < layers[index].effects.size(); ++e) {
                                  const FluxEffect& effect = layers[index].effects[e];
                                  if (effect.node && effect.node->isActivated()) {
                                      effectsPanel->addEffect(effect.pluginId, effect.label);
                                  }
                              }

                              if (layers[index].gizmoNode) {
                                  NodeGuiPtr nodeGui = std::dynamic_pointer_cast<NodeGui>(layers[index].gizmoNode->getNodeGui());
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
                          }

                          // Redraw viewers to update overlay handles
                          redrawAllViewers();
                      });

    // 4. Effects Panel: select/remove actual effect nodes owned by the timeline model.
    QObject::connect(effectsPanel, &FluxEffectsPanel::effectSelected, this,
                     [this, timeline](int effectIndex) {
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

    // 6. Timeline: effectSelected → open effect node properties panel
    QObject::connect(timeline, &FluxTimeline::effectSelected, this,
                     [this, timeline](int layerIndex, int effectIndex) {
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

    // Store references to Flux widgets for later access
    _imp->_fluxProjectBin = projectBin;
    _imp->_fluxTimeline = timeline;
    _imp->_fluxEffectsPanel = effectsPanel;
    _imp->_fluxExportPanel = exportPanel;

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
} // Gui::setupFluxUi

// ====================================================================
// Mask graph helpers (T060/T061)
// ====================================================================

static const char* kFluxLayerMaskApplyLabel = "Flux Layer Mask Apply";
static const char* kFluxMaskReformatLabel = "Flux Mask Reformat";
static const char* kFluxMaskRotoLabel = "Flux Mask";
static const char* kFluxLayerMaskPremultLabel = "Flux Layer Mask Premult";
static const char* kFluxLayerMaskUnpremultLabel = "Flux Layer Mask Unpremult";

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
    // Chain: expectedSource -> [Unpremult] -> Roto -> Premult -> ... -> mergeA
    // Detect Flux-owned nodes by label.
    NodePtr stalePremult;
    NodePtr staleRoto;
    NodePtr staleUnpremult;

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
        }

        int nInputs = cur->getNInputs();
        for (int i = 0; i < nInputs; ++i) {
            NodePtr inp = cur->getInput(i);
            if (inp) {
                queue.push_back(inp);
            }
        }
    }

    // Only clean up if we found at least a Premult or Roto that is Flux-owned
    // AND the chain reaches expectedSource (not some unrelated user subgraph)
    if (!stalePremult && !staleRoto) {
        return false;
    }

    // Verify the chain traces back to expectedSource
    NodePtr chainRoot = stalePremult ? stalePremult : staleRoto;
    if (!upstreamContainsNode(chainRoot, expectedSource, nullptr)) {
        return false;
    }

    // Found a stale Flux-owned inline layer-mask chain — clean it up
    mergeNode->disconnectInput(1);
    mergeNode->connectInput(expectedSource, 1);

    // Discover Unpremult from Roto input 0 if not already found
    if (staleRoto && staleRoto->isActivated() && !staleUnpremult) {
        NodePtr rotoIn0 = staleRoto->getInput(0);
        if (rotoIn0 && rotoIn0->isActivated() && rotoIn0->getLabel() == kFluxLayerMaskUnpremultLabel) {
            staleUnpremult = rotoIn0;
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

    // Clear model refs if they point to the deactivated nodes
    if (layer.maskApplyNode && !layer.maskApplyNode->isActivated()) {
        layer.maskApplyNode.reset();
    }
    for (int m = 0; m < layer.masks.size(); ++m) {
        if (layer.masks[m].effectIndex < 0) {
            if (layer.masks[m].maskNode && !layer.masks[m].maskNode->isActivated()) {
                layer.masks[m].maskNode.reset();
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
    // If the current input eventually reaches expectedSource upstream,
    // the user inserted manual nodes in between — preserve them.
    if (upstreamContainsNode(currentInput, expectedSource)) {
        return currentInput;
    }
    // No connection to expected source — use the default.
    return expectedSource;
}

void
Gui::rebuildCompositingGraph(FluxTimeline* timeline)
{
    if (!getApp() || !timeline) {
        return;
    }

    const QList<FluxLayer>& constLayers = timeline->getLayers();
    // We need mutable access to set gizmoNode/mergeNode on each layer
    QList<FluxLayer>& layers = const_cast<QList<FluxLayer>&>(constLayers);

    // Find the first viewer
    ViewerTab* viewerTab = nullptr;
    {
        QMutexLocker l(&_imp->_viewerTabsMutex);
        if (!_imp->_viewerTabs.empty()) {
            viewerTab = _imp->_viewerTabs.front();
        }
    }

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
        if (l.type == QString::fromUtf8("footage") && l.filePath.isEmpty()) continue;
        if (l.type == QString::fromUtf8("adjustment") && l.effects.isEmpty()) continue;
        pixelLayers.push_back(i);
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
                    if (adjustmentOutput && !effect.node->connectInput(adjustmentOutput, 0)) {
                        fprintf(stderr, "FLUX ERROR: adjustment effect connectInput(%s <- %s) failed for row %d\n",
                                effect.node->getLabel().c_str(), adjustmentOutput->getLabel().c_str(), i);
                    }
                }
                // Disable state is controlled by updateAdjustmentTrimKeyframes(),
                // not by setNodeDisabled() which would overwrite the trim keyframes.
                _imp->_fluxMergeNodes.push_back(effect.node);
                adjustmentOutput = effect.node;
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
                pluginId = QString::fromUtf8("net.sf.openfx.FluxText");
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
        {
            int sourceRows = (layer.type == QString::fromUtf8("footage") && layer.readerNode) ? 2 : 1;
            double effectY = kYStart + (graphRow + sourceRows) * kYSpacing;
            for (int e = layer.effects.size() - 1; e >= 0; --e) {
                if (!layer.effects[e].node || !layer.effects[e].node->isActivated()) {
                    fprintf(stderr, "FLUX: Dropping stale effect reference from layer %d '%s'\n",
                            i, layer.name.toStdString().c_str());
                    layer.effects.removeAt(e);
                    effectsPruned = true;
                }
            }
            for (int e = 0; e < layer.effects.size(); ++e) {
                FluxEffect& effect = layer.effects[e];
                effect.node->setPosition(kCenterX + kGizmoOffsetX, effectY + e * kYSpacing);
                if (effect.node->getNInputs() > 0) {
                    // Preserve any manual inline chain the user inserted
                    NodePtr sourceInput = preserveInlineChainInput(effect.node, 0, layerOutput);
                    effect.node->disconnectInput(0);
                    if (sourceInput && !effect.node->connectInput(sourceInput, 0)) {
                        fprintf(stderr, "FLUX ERROR: effect connectInput(%s <- %s) failed for layer %d\n",
                                effect.node->getLabel().c_str(), sourceInput->getLabel().c_str(), i);
                    }
                }
                effect.node->setNodeDisabled(!effect.enabled);

                // -- Effect mask wiring (T061) --
                int maskInputIdx = discoverMaskInput(effect.node);
                if (maskInputIdx >= 0) {
                    FluxMask* emask = firstEnabledEffectMask(layer, e);
                    if (emask) {
                        double effectPosX, effectPosY;
                        effect.node->getPosition(&effectPosX, &effectPosY);
                        if (ensureMaskSourceNodes(this, *emask, collection,
                                                  effectPosX, effectPosY)) {
                            effect.node->disconnectInput(maskInputIdx);
                            if (!effect.node->connectInput(emask->maskNode, maskInputIdx)) {
                                fprintf(stderr, "FLUX WARNING: effect mask connectInput(%s mask %d) failed for layer %d effect %d\n",
                                        effect.node->getLabel().c_str(), maskInputIdx, i, e);
                            }
                        }
                    } else {
                        // No mask for this effect — clear the mask input if it was previously connected
                        effect.node->disconnectInput(maskInputIdx);
                    }
                }

                // V1: warn about multiple enabled masks for the same effect
                if (maskInputIdx >= 0) {
                    int effectMaskCount = 0;
                    for (int m = 0; m < layer.masks.size(); ++m) {
                        if (layer.masks[m].enabled && layer.masks[m].effectIndex == e) {
                            ++effectMaskCount;
                        }
                    }
                    if (effectMaskCount > 1) {
                        fprintf(stderr, "FLUX: Multiple enabled effect masks on effect %s; V1 uses first only\n",
                                effect.node->getLabel().c_str());
                    }
                }

                _imp->_fluxMergeNodes.push_back(effect.node);
                layerOutput = effect.node;
            }
        }

        // -- Layer mask wiring (T064) --
        // V1: single enabled layer mask per layer, inline in the source pipe.
        // Chain: source -> [Unpremult] -> Roto -> Premult -> Merge A
        // No Merge(in), no side branch. maskApplyNode = inline Premult.
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
            double maskBaseY = kYStart + (graphRow + sourceRows + layer.effects.size()) * kYSpacing;

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
                // source -> [Unpremult input 0] -> Roto input 0 -> Premult input 0
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

                // Wire: Roto -> Premult input 0
                NodePtr premultInput = preserveInlineChainInput(premultNode, 0, lmask->maskNode);
                premultNode->disconnectInput(0);
                premultNode->connectInput(premultInput, 0);

                // layerOutput is now the Premult
                layerOutput = premultNode;

                _imp->_fluxMergeNodes.push_back(lmask->maskNode);
                _imp->_fluxMergeNodes.push_back(premultNode);
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
            // Inline layer mask rows: Roto + Premult + optional Unpremult
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
            double mergeY = kYStart + (graphRow + sourceRows + layer.effects.size() + inlineMaskRows) * kYSpacing;
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
                if (layer.type == QString::fromUtf8("footage") || !layer.effects.isEmpty()) {
                    inlineMaskRows = 3;
                }
            }
            graphRow += qMax(kNodesPerLayer, sourceRows + layer.effects.size() + inlineMaskRows + 1);
        }

        // Classify branches to detect precomp — derived UI state, not serialized
        {
            FluxLayerBranchClassification cls = classifyLayerBranches(layer);
            layer.hasPrecompBranch = cls.hasPrecompBranch;
        }
    }

    // Connect the final output to the viewer
    if (lastOutput && viewerTab) {
        NodePtr viewerNode = viewerTab->getInternalNode()->getNode();
        if (viewerNode) {
            viewerNode->disconnectInput(0);
            viewerNode->connectInput(lastOutput, 0);
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

    fprintf(stderr, "FLUX: Compositing graph rebuilt with %d layers, %d nodes (gizmos + merges)\n",
            (int)layers.size(), (int)_imp->_fluxMergeNodes.size());

    // Store final output node for export panel
    _imp->_fluxFinalOutputNode = lastOutput;
}

void
Gui::deferredInitGizmoParams(FluxTimeline* timeline)
{
    if (!getApp() || !timeline) {
        return;
    }

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
        int mediaDuration = mediaLast - mediaFirst;
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

NATRON_NAMESPACE_EXIT
