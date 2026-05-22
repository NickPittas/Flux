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
#include <stdexcept>

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
#include "Gui/DopeSheetEditor.h"
#include "Gui/PropertiesBinWrapper.h"

#include "Engine/EffectInstance.h"
#include "Engine/Knob.h"
#include "Engine/KnobTypes.h"
#include "Engine/KnobFile.h"


NATRON_NAMESPACE_ENTER


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
    //   Top row:    Project Bin (20%) | Viewport (50%) | Effects+Properties (30%)
    //   Bottom row: Timeline (with Node Graph / Curve Editor / Dope Sheet as tabs)
    // ====================================================================

    // ====================================================================
    // TOP-CENTER: Main pane (will hold viewers)
    // ====================================================================
    TabWidget* mainPane = new TabWidget(this, _imp->_leftRightSplitter);
    {
        QMutexLocker l(&_imp->_panesMutex);
        _imp->_panes.push_back(mainPane);
    }
    mainPane->setObjectName_mt_safe( QString::fromUtf8("fluxMainPane") );
    mainPane->setAsAnchor(true);

    // ====================================================================
    // TOP-RIGHT: Properties pane (split horizontally from main)
    // ====================================================================
    TabWidget* propertiesPane = mainPane->splitHorizontally(false);
    propertiesPane->setObjectName_mt_safe( QString::fromUtf8("fluxPropertiesPane") );

    // ====================================================================
    // BOTTOM: Workshop pane (split vertically from main)
    // ====================================================================
    TabWidget* workshopPane = mainPane->splitVertically(false);
    workshopPane->setObjectName_mt_safe( QString::fromUtf8("fluxWorkshopPane") );

    // ====================================================================
    // TOP-LEFT: Project Bin (split horizontally from main, left side)
    // ====================================================================
    TabWidget* projectBinPane = mainPane->splitHorizontally(false);
    projectBinPane->setObjectName_mt_safe( QString::fromUtf8("fluxProjectBinPane") );

    // Create and add FluxProjectBin widget
    FluxProjectBin* projectBin = new FluxProjectBin(this);
    projectBin->setScriptName("fluxProjectBin");
    projectBin->setLabel( tr("Project Bin").toStdString() );
    TabWidget::moveTab(projectBin, projectBin, projectBinPane);

    // ====================================================================
    // Populate workshop pane (bottom)
    // ====================================================================
    // Flux Timeline as the primary tab
    FluxTimeline* timeline = new FluxTimeline(this);
    timeline->setScriptName("fluxTimeline");
    timeline->setLabel( tr("Timeline").toStdString() );
    TabWidget::moveTab(timeline, timeline, workshopPane);

    // Node Graph, Curve Editor, Dope Sheet as additional tabs (power users)
    if (_imp->_nodeGraphArea) {
        TabWidget::moveTab(_imp->_nodeGraphArea, _imp->_nodeGraphArea, workshopPane);
    }
    if (_imp->_curveEditor) {
        TabWidget::moveTab(_imp->_curveEditor, _imp->_curveEditor, workshopPane);
    }
    if (_imp->_dopeSheetEditor) {
        TabWidget::moveTab(_imp->_dopeSheetEditor, _imp->_dopeSheetEditor, workshopPane);
    }

    // ====================================================================
    // Populate properties pane (top-right)
    // ====================================================================
    // Flux Effects Stack
    FluxEffectsPanel* effectsPanel = new FluxEffectsPanel(this);
    effectsPanel->setScriptName("fluxEffectsPanel");
    effectsPanel->setLabel( tr("Effects").toStdString() );
    TabWidget::moveTab(effectsPanel, effectsPanel, propertiesPane);

    // Natron's properties bin
    if (_imp->_propertiesBin) {
        TabWidget::moveTab(_imp->_propertiesBin, _imp->_propertiesBin, propertiesPane);
    }

    // ====================================================================
    // Move viewers and histograms to main pane (top-center)
    // ====================================================================
    {
        QMutexLocker l(&_imp->_viewerTabsMutex);
        for (std::list<ViewerTab*>::iterator it2 = _imp->_viewerTabs.begin(); it2 != _imp->_viewerTabs.end(); ++it2) {
            TabWidget::moveTab(*it2, *it2, mainPane);
        }
    }
    {
        QMutexLocker l(&_imp->_histogramsMutex);
        for (std::list<Histogram*>::iterator it2 = _imp->_histograms.begin(); it2 != _imp->_histograms.end(); ++it2) {
            TabWidget::moveTab(*it2, *it2, mainPane);
        }
    }

    // ====================================================================
    // Set pane sizes
    // ====================================================================
    // Top row: Project Bin (20%) | Main/Viewport (50%) | Properties (30%)
    Splitter* topSplitter = dynamic_cast<Splitter*>(mainPane->parentWidget());
    if (topSplitter) {
        QList<int> topSizes;
        topSizes << (width() * 0.20) << (width() * 0.50) << (width() * 0.30);
        topSplitter->setSizes_mt_safe(topSizes);
    }

    // Top/bottom: 70% / 30%
    Splitter* vertSplitter = dynamic_cast<Splitter*>(workshopPane->parentWidget());
    if (vertSplitter) {
        QList<int> vertSizes;
        vertSizes << (height() * 0.70) << (height() * 0.30);
        vertSplitter->setSizes_mt_safe(vertSizes);
    }

    // Default to Timeline displayed in workshop pane
    workshopPane->makeCurrentTab(0);

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

                          // Open newly selected layer's properties panel
                          const QList<FluxLayer>& layers = timeline->getLayers();
                          if (index >= 0 && index < layers.size()) {
                              effectsPanel->setActiveLayer(index, layers[index].name);

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

    // 4. Effects Panel: layerEffectAddRequested → create effect node
    QObject::connect(effectsPanel, &FluxEffectsPanel::layerEffectAddRequested, this,
                     [this](int /*layerIndex*/, QString pluginId) {
                         if (!getApp() || pluginId.isEmpty()) {
                             return;
                         }
                         NodeCollectionPtr collection = std::dynamic_pointer_cast<NodeCollection>(getApp()->getProject());
                         CreateNodeArgs args(pluginId.toStdString(), collection);
                         getApp()->createNode(args);
                     });

    // 5. Timeline: compositingChanged → rebuild Merge node chain + connect viewer
    QObject::connect(timeline, &FluxTimeline::compositingChanged, this,
                     [this, timeline]() {
                         rebuildCompositingGraph(timeline);
                     });

    // Store references to Flux widgets for later access
    _imp->_fluxProjectBin = projectBin;
    _imp->_fluxTimeline = timeline;
    _imp->_fluxEffectsPanel = effectsPanel;

    fprintf(stderr, "FLUX: Layout created successfully\n");
} // Gui::setupFluxUi

void
Gui::rebuildCompositingGraph(FluxTimeline* timeline)
{
    if (!getApp() || !timeline) {
        return;
    }

    const QList<FluxLayer>& constLayers = timeline->getLayers();
    if (constLayers.isEmpty()) {
        return;
    }
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

    // Clean up old Merge nodes from previous build (gizmos are preserved per-layer)
    for (NodePtr node : _imp->_fluxMergeNodes) {
        if (node) {
            // Only deactivate merge nodes, not gizmo nodes
            if (!node->isEffectGroup()) {
                node->deactivate(std::list<NodePtr>(), false, true);
            }
        }
    }
    _imp->_fluxMergeNodes.clear();

    // ====================================================================
    // Node Graph Layout — Strict Vertical Pipeline
    //
    //   X:  centerX (main pipe)    gizmoX (layer branches, LEFT of pipe)
    //
    //        ┌──────────┐
    //        │ Reformat │  ← top anchor, project format canvas
    //        └────┬─────┘
    //             │  (vertical main pipe, all B inputs)
    //        ┌────┴─────┐   ┌──────────┐
    //        │ Merge 0  │───│ Gizmo 0  │  ← bottom timeline layer (background)
    //        └────┬─────┘   └──────────┘
    //             │
    //        ┌────┴─────┐   ┌──────────┐
    //        │ Merge N  │───│ Gizmo N  │  ← top timeline layer (foreground) → Viewer
    //        └────┬─────┘   └──────────┘
    //             │
    //           Viewer
    //
    //   Rules:
    //   - Reformat at top center (anchor point)
    //   - All Merge nodes stacked vertically at centerX (main pipe)
    //   - Each layer gizmo placed to the LEFT of its Merge
    //   - Effects placed below gizmo, above merge (same X as gizmo) — future
    //   - No overlaps, uniform vertical spacing
    //   - Chain built bottom-to-top so top timeline layer = foreground
    // ====================================================================

    // Layout constants
    const double kCenterX = 0.0;       // X position for Reformat + all Merge nodes (main pipe)
    const double kGizmoOffsetX = -200.0; // gizmos go LEFT of the main pipe
    const double kYStart = 0.0;        // Reformat Y position (top of tree)
    const double kYSpacing = 120.0;    // vertical gap between each node row

    // -- Ensure background Reformat node exists --
    if (!_imp->_fluxBgReformatNode) {
        CreateNodeArgs bgArgs("net.sf.openfx.Reformat", collection);
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
    // Reposition Reformat anchor on every rebuild
    if (_imp->_fluxBgReformatNode) {
        _imp->_fluxBgReformatNode->setPosition(kCenterX, kYStart);
    }

    NodePtr lastOutput = _imp->_fluxBgReformatNode; // Start with the background canvas

    // Build Merge chain bottom-to-top so that top timeline layers composite on top.
    // i=0 in the loop processes the bottom-most pixel-producing layer first (onto the Reformat bg),
    // then each subsequent layer goes on top, ending with the top timeline layer as foreground.
    QList<int> pixelLayers; // indices of layers that produce pixels, bottom-to-top
    for (int i = layers.size() - 1; i >= 0; --i) {
        const FluxLayer& l = layers[i];
        if (l.type == QString::fromUtf8("null")) continue;
        if (l.type == QString::fromUtf8("footage") && l.filePath.isEmpty()) continue;
        pixelLayers.push_back(i);
    }

    for (int pi = 0; pi < pixelLayers.size(); ++pi) {
        int i = pixelLayers[pi];
        FluxLayer& layer = layers[i];

        // -- Create new gizmo ONLY if this layer doesn't have one yet --
        if (!layer.gizmoNode) {
            QString pluginId;
            if (layer.type == QString::fromUtf8("solid")) {
                pluginId = QString::fromUtf8("net.sf.openfx.FluxSolid");
            } else {
                pluginId = QString::fromUtf8("net.sf.openfx.FluxLayer");
            }

            CreateNodeArgs gizmoArgs(pluginId.toStdString(), collection);
            NodePtr gizmoNode = getApp()->createNode(gizmoArgs);

            if (!gizmoNode) {
                fprintf(stderr, "FLUX ERROR: Failed to create gizmo for layer %d '%s' (type=%s)\n",
                        i, layer.name.toStdString().c_str(), layer.type.toStdString().c_str());
                continue;
            }

            // -- Footage-specific: set filename on internal Read node --
            if (layer.type == QString::fromUtf8("footage")) {
                NodeGroup* gizmoGroup = gizmoNode->isEffectGroup();
                NodePtr internalRead;
                if (gizmoGroup) {
                    internalRead = gizmoGroup->getNodeByName("Read1");
                }
                if (internalRead) {
                    KnobIPtr filenameKnob = internalRead->getKnobByName("filename");
                    if (filenameKnob) {
                        KnobStringBasePtr strKnob = std::dynamic_pointer_cast<KnobStringBase>(filenameKnob);
                        if (strKnob) {
                            strKnob->setValue(layer.filePath.toStdString(), ViewSpec::all(), 0);
                        }
                    }
                }
                // Lock the original anchor points immediately — these NEVER change.
                // outPoint will be corrected by deferredInit once we know the media duration,
                // but originalInPoint must be set NOW so moves/trims before the timer don't break.
                layer.originalInPoint = layer.inPoint;
                // Set a reasonable default outPoint; deferred init will correct it.
                // Use project range as a guess; it gets overwritten once the file is probed.
                if (!layer.nodeInitialized) {
                    layer.originalOutPoint = layer.outPoint;
                }
            }

            // -- Solid-specific: initialize immediately (no file probe needed) --
            if (layer.type == QString::fromUtf8("solid")) {
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

            // -- Register transform overlay handles on the gizmo node --
            // This makes translate/rotate/scale/center/skew handles appear in the viewer
            // when the gizmo's properties panel is open.
            {
                KnobIPtr translateKnob = gizmoNode->getKnobByName("translate");
                KnobIPtr scaleKnob = gizmoNode->getKnobByName("scale");
                KnobIPtr rotateKnob = gizmoNode->getKnobByName("rotate");
                KnobIPtr centerKnob = gizmoNode->getKnobByName("center");
                KnobIPtr uniformKnob = gizmoNode->getKnobByName("uniform");
                KnobIPtr skewXKnob = gizmoNode->getKnobByName("skewX");
                KnobIPtr skewYKnob = gizmoNode->getKnobByName("skewY");
                KnobIPtr skewOrderKnob = gizmoNode->getKnobByName("skewOrder");

                KnobDoublePtr translateDbl = std::dynamic_pointer_cast<KnobDouble>(translateKnob);
                KnobDoublePtr scaleDbl = std::dynamic_pointer_cast<KnobDouble>(scaleKnob);
                KnobDoublePtr rotateDbl = std::dynamic_pointer_cast<KnobDouble>(rotateKnob);
                KnobDoublePtr centerDbl = std::dynamic_pointer_cast<KnobDouble>(centerKnob);
                KnobBoolPtr uniformBool = std::dynamic_pointer_cast<KnobBool>(uniformKnob);
                KnobDoublePtr skewXDbl = std::dynamic_pointer_cast<KnobDouble>(skewXKnob);
                KnobDoublePtr skewYDbl = std::dynamic_pointer_cast<KnobDouble>(skewYKnob);
                KnobChoicePtr skewOrderChoice = std::dynamic_pointer_cast<KnobChoice>(skewOrderKnob);

                if (translateDbl && scaleDbl && rotateDbl && centerDbl) {
                    gizmoNode->addTransformInteract(
                        translateDbl,
                        scaleDbl,
                        uniformBool,
                        rotateDbl,
                        skewXDbl,
                        skewYDbl,
                        skewOrderChoice,
                        centerDbl,
                        KnobBoolPtr(),  // invert (null)
                        KnobBoolPtr()   // interactive (null — let overlay use default)
                    );
                    fprintf(stderr, "FLUX: Registered transform overlay on gizmo for layer %d\n", i);
                } else {
                    fprintf(stderr, "FLUX WARNING: Could not find all transform knobs on gizmo for overlay registration (layer %d)\n", i);
                }
            }

            layer.gizmoNode = gizmoNode;
        }

        // -- Reposition gizmo (runs every rebuild, not just on creation) --
        {
            double gizmoY = kYStart + (pi + 1) * kYSpacing;
            layer.gizmoNode->setPosition(kCenterX + kGizmoOffsetX, gizmoY);
        }

        // Track this gizmo in our node list
        _imp->_fluxMergeNodes.push_back(layer.gizmoNode);

        NodePtr layerOutput = layer.gizmoNode;

        // -- Create Merge node for every layer (including first) --
        // Merge(A=this gizmo, B=previous output / background)
        CreateNodeArgs mgArgs(PLUGINID_OFX_MERGE, collection);
        NodePtr mergeNode = getApp()->createNode(mgArgs);
        if (!mergeNode) {
            CreateNodeArgs mgArgs2("net.sf.openfx.MergePlugin", collection);
            mergeNode = getApp()->createNode(mgArgs2);
        }
        if (!mergeNode) {
            fprintf(stderr, "FLUX ERROR: Failed to create Merge node for layer %d (chain pos %d)\n", i, pi);
            lastOutput = layerOutput;
            continue;
        }

        // Position merge on the main pipe, at same Y as its gizmo
        double mergeY = kYStart + (pi + 1) * kYSpacing;
        mergeNode->setPosition(kCenterX, mergeY);

        // Connect: input 0 (B) = previous output / background (pass-through when disabled)
        //          input 1 (A) = this layer's gizmo (foreground)
        if (lastOutput) {
            mergeNode->connectInput(lastOutput, 0);
        }
        mergeNode->connectInput(layerOutput, 1);

        // -- Sync blending mode from gizmo to Merge --
        // The gizmo's "blendingMode" Choice param persists across rebuilds.
        // 1) Set initial value on Merge's "operation" knob
        // 2) Connect knob signal for live updates when user changes the dropdown
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
        _imp->_fluxMergeNodes.push_back(mergeNode);
        lastOutput = mergeNode;
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

    fprintf(stderr, "FLUX: Compositing graph rebuilt with %d layers, %d nodes (gizmos + merges)\n",
            (int)layers.size(), (int)_imp->_fluxMergeNodes.size());
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

        // Find the internal Read node inside the gizmo group
        NodeGroup* gizmoGroup = layer.gizmoNode->isEffectGroup();
        NodePtr internalRead;
        if (gizmoGroup) {
            internalRead = gizmoGroup->getNodeByName("Read1");
        }
        if (!internalRead) {
            continue;
        }

        // Step 1: Trigger "reload" on the file knob — this probes the file,
        // creates the embedded decoder, and populates firstFrame/lastFrame/etc.
        KnobIPtr filenameKnob = internalRead->getKnobByName("filename");
        if (filenameKnob) {
            KnobFilePtr fileKnob = std::dynamic_pointer_cast<KnobFile>(filenameKnob);
            if (fileKnob) {
                fileKnob->reloadFile();
            }
        }

        // Force output components to RGBA so the Multiply node (opacity) works on all channels.
        // Without this, MP4/JPG/etc without alpha output RGB only and Multiply ignores them.
        {
            KnobIPtr outCompKnob = internalRead->getKnobByName("outputComponents");
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
            EffectInstancePtr readEffect = internalRead->getEffectInstance();
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
            EffectInstancePtr readEffect = internalRead->getEffectInstance();
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
