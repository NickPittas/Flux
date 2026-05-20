# Natron Engine Node System — API Reference for Flux UI

> Auto-generated from header analysis of the Natron codebase.
> Focus: public API surface needed to build a new UI layer.

---

## Table of Contents

1. [Architecture Overview](#architecture-overview)
2. [AppManager](#1-appmanager-enginappmanagerh)
3. [AppInstance](#2-appinstance-engineappinstanceh)
4. [Project](#3-project-engineprojecth)
5. [NodeCollection / NodeGroup](#4-nodecollection--nodegroup-enginenodegrouph)
6. [Node](#5-node-enginenodeh)
7. [EffectInstance](#6-effectinstance-engineeffectinstanceh)
8. [CreateNodeArgs](#7-createnodeargs-enginecreatenodeargsh)
9. [Plugin](#8-plugin-enginepluginh)
10. [Signal/Slot Quick Reference](#signalslot-quick-reference)
11. [Node Lifecycle](#node-lifecycle)
12. [Qt Coupling Summary](#qt-coupling-summary)
13. [Key Type Aliases](#key-type-aliases)

---

## Architecture Overview

```
AppManager (singleton)
 └── AppInstance (per project/window)
      └── Project (top-level NodeCollection, KnobHolder)
           └── NodeCollection (owns nodes, graph operations)
                └── Node (QObject, owns EffectInstance)
                     └── EffectInstance (rendering logic, KnobHolder)
                          ├── OutputEffectInstance (writers, viewers)
                          ├── ViewerInstance
                          └── OfxEffectInstance (OpenFX plugins)
```

- **AppManager** is a process-wide singleton (`appPTR` macro). Owns all `AppInstance`s.
- **AppInstance** is one open project. Owns the `Project`.
- **Project** extends `NodeCollection` (which holds all nodes) and `KnobHolder` (project-level parameters).
- **Node** is a `QObject` with signals. Wraps one `EffectInstance`.
- **EffectInstance** is NOT a QObject. It is a `NamedKnobHolder` with rendering logic.
- **NodeGroup** extends both `OutputEffectInstance` AND `NodeCollection`, enabling nested sub-graphs.

---

## 1. AppManager (`Engine/AppManager.h`)

**Singleton.** Access via `appPTR` macro. Inherits `QObject`.

### Key Public Methods

| Method | Description |
|--------|-------------|
| `static AppManager* instance()` | Get the singleton. |
| `bool load(int argc, char** argv, const CLArgs& cl)` | Initialize application, load plugins. Call right after constructor. |
| `int exec()` | Start Qt event loop. Blocks until quit. |
| `AppTypeEnum getAppType() const` | `eAppTypeGui`, `eAppTypeBackground`, `eAppTypeBackgroundAutoRun`, `eAppTypeInterpreter` |
| `bool isLoaded() const` | Whether initialization completed. |
| `AppInstancePtr newAppInstance(const CLArgs&, bool makeEmpty)` | Create a new GUI project instance. |
| `AppInstancePtr newBackgroundInstance(const CLArgs&, bool makeEmpty)` | Create a background/render instance. |
| `AppInstancePtr getAppInstance(int appID) const` | Get instance by ID. |
| `int getNumInstances() const` | Count of open instances. |
| `void removeInstance(int appID)` | Remove an instance. |
| `AppInstancePtr getTopLevelInstance() const` | The front-most instance. |
| `const AppInstanceVec& getAppInstances() const` | All instances. |
| `const PluginsMap& getPluginsList() const` | All loaded plugins. |
| `Plugin* getPluginBinary(const QString& id, int major, int minor, bool lowerCase) const` | Lookup a plugin by ID+version. |
| `std::list<std::string> getPluginIDs() const` | All plugin IDs. |
| `std::list<std::string> getPluginIDs(const std::string& filter)` | Filtered plugin IDs. |
| `SettingsPtr getCurrentSettings() const` | Global application settings. |
| `void quit(const AppInstancePtr&)` / `void quitApplication()` | Shutdown. |
| `void setNumberOfThreads(int)` | Control render thread pool. |
| `int getMaxThreadCount()` | Thread pool size. |
| `void clearAllCaches()` | Clear all caches. |
| `bool getImage(...)` / `bool getImageOrCreate(...)` | Cache access. |
| `void getSupportedReaderFileFormats(...)` / `getSupportedWriterFileFormats(...)` | IO format discovery. |
| `std::string getReaderPluginIDForFileType(...)` / `getWriterPluginIDForFileType(...)` | Map extension to plugin. |
| `EffectInstancePtr createOFXEffect(NodePtr, const CreateNodeArgs&)` | Low-level OFX effect factory. |
| `bool isOpenGLLoaded() const` | OpenGL availability. |

### Key Signals

| Signal | When emitted |
|--------|-------------|
| `checkerboardSettingsChanged()` | Checkerboard display settings changed. |
| `s_requestOFXDialogOnMainThread(...)` | OFX plugin requests a dialog on main thread. |

### Dependencies

- `Plugin`, `KnobFactory`, `Settings`, `Image` cache system, `OfxHost`, Python interpreter.
- OFX host, GPU context pool, application TLS.

---

## 2. AppInstance (`Engine/AppInstance.h`)

**Per-project instance.** Inherits `QObject`, `std::enable_shared_from_this`, `TimeLineKeyFrames`.

### Key Public Methods

| Method | Description |
|--------|-------------|
| `void load(const CLArgs&, bool makeEmptyInstance)` | Load project from command-line args. |
| `int getAppID() const` | Unique integer ID. |
| `NodePtr createNode(CreateNodeArgs& args)` | **Primary node creation API.** |
| `NodePtr createReader(const string& filename, CreateNodeArgs&)` | Convenience: create a reader node for a file. |
| `NodePtr createWriter(const string& filename, CreateNodeArgs&, int first, int last)` | Convenience: create a writer node. |
| `NodePtr getNodeByFullySpecifiedName(const string&) const` | Lookup node by qualified name. |
| `ProjectPtr getProject() const` | Access the project. |
| `TimeLinePtr getTimeLine() const` | Access the timeline. |
| `double getProjectFrameRate() const` | Project FPS. |
| `void getFrameRange(double* first, double* last) const` | Project frame range. |
| `bool save(const string& filename)` / `saveAs(...)` | Save project. |
| `AppInstancePtr loadProject(const string& filename)` | Load project. |
| `bool resetProject()` / `closeProject()` | Close/reset. |
| `AppInstancePtr newProject()` | Open a new empty project. |
| `void startWritersRendering(...)` | Trigger render. |
| `void startWritersRenderingFromNames(...)` | Trigger render by writer names. |
| `bool isBackground() const` | Virtual; GUI subclass returns false. |

### Virtual Methods for UI Override

These are no-ops in the base class. A GUI subclass (like the existing `GuiAppInstance`) overrides them:

| Method | Purpose |
|--------|---------|
| `virtual void errorDialog(...)` | Show error dialog. |
| `virtual void warningDialog(...)` | Show warning dialog. |
| `virtual void informationDialog(...)` | Show info dialog. |
| `virtual StandardButtonEnum questionDialog(...)` | Show question dialog. |
| `virtual void renderAllViewers(bool canAbort)` | Refresh all viewers. |
| `virtual void abortAllViewers()` | Abort viewer renders. |
| `virtual void refreshAllPreviews()` | Refresh node thumbnails. |
| `virtual void queueRedrawForAllViewers()` | Schedule viewer redraws. |
| `virtual void redrawAllViewers()` | Immediate viewer redraw. |
| `virtual void clearViewersLastRenderedTexture()` | Clear viewer cache. |
| `virtual void progressStart/Update/End(...)` | Progress bar for plugins. |
| `virtual void appendToScriptEditor(const string&)` | Script console output. |
| `virtual void createLoadProjectSplashScreen(...)` | Loading UI. |
| `virtual void updateProjectLoadStatus(...)` | Loading progress. |
| `virtual void notifyRenderStarted(...)` | Render started notification. |

### Key Signals

| Signal | When emitted |
|--------|-------------|
| `pluginsPopulated()` | All plugins finished loading. |

### Key Slots

| Slot | Description |
|------|-------------|
| `quit()` | Graceful quit (async). |
| `quitNow()` | Blocking quit. |
| `triggerAutoSave()` | Force an auto-save. |
| `clearOpenFXPluginsCaches()` | Clear OFX caches. |
| `clearAllLastRenderedImages()` | Clear rendered image cache. |

---

## 3. Project (`Engine/Project.h`)

**Top-level node container.** Inherits `KnobHolder`, `NodeCollection`, `AfterQuitProcessingI`, `std::enable_shared_from_this`.

### Key Public Methods

| Method | Description |
|--------|-------------|
| `static ProjectPtr create(const AppInstancePtr&)` | Factory. |
| `bool loadProject(const QString& path, const QString& name, ...)` | Load from disk. |
| `bool saveProject(const QString& path, const QString& name, ...)` | Save to disk. |
| `void autoSave()` / `triggerAutoSave()` | Auto-save mechanism. |
| `bool isLoadingProject() const` | Loading state. |
| `QString getProjectPath() const` | Project directory. |
| `QString getProjectFilename() const` | Project filename. |
| `bool hasProjectBeenSavedByUser() const` | Ever saved? |
| `bool isSaveUpToDate() const` | Unsaved changes? |
| `void getProjectDefaultFormat(Format*) const` | Default resolution. |
| `bool getProjectFormatAtIndex(int, Format*) const` | Format by index. |

### Inherited from NodeCollection

See [NodeCollection section](#4-nodecollection--nodegroup-enginenodegrouph). The Project IS the root NodeCollection.

---

## 4. NodeCollection / NodeGroup (`Engine/NodeGroup.h`)

**Node container with graph operations.** `NodeCollection` is a plain C++ class (NOT QObject). `NodeGroup` extends both `OutputEffectInstance` and `NodeCollection`.

### NodeCollection — Key Public Methods

| Method | Description |
|--------|-------------|
| `NodesList getNodes() const` | **MT-safe.** Copy of all nodes. |
| `void getNodes_recursive(NodesList&, bool onlyActive) const` | Recursive into sub-groups. |
| `void addNode(const NodePtr&)` | Add node. MT-safe. |
| `void removeNode(const Node*)` | Remove node. MT-safe. |
| `NodePtr getLastNode(const string& pluginID) const` | Most recent node of given type. |
| `void clearNodesBlocking()` / `clearNodesNonBlocking()` | Remove all nodes. |
| `void initNodeName(const string& pluginLabel, string* nodeName)` | Generate unique name. |
| `bool hasNodes() const` | Any nodes? |
| `bool hasNodeRendering() const` | Any node currently rendering? |
| `static bool connectNodes(int inputNumber, const NodePtr& input, const NodePtr& output, bool force)` | **Connect two nodes.** |
| `bool connectNodes(int inputNumber, const string& inputName, const NodePtr& output)` | Connect by name. |
| `static bool disconnectNodes(const NodePtr& input, const NodePtr& output, bool autoReconnect)` | **Disconnect two nodes.** |
| `bool autoConnectNodes(const NodePtr& selected, const NodePtr& created)` | Auto-connect based on role. |
| `NodePtr getNodeByName(const string&) const` | Lookup by script-name. |
| `NodePtr getNodeByFullySpecifiedName(const string&) const` | Lookup across sub-groups. |
| `void getActiveNodes(NodesList*) const` | Active (non-deleted) nodes. |
| `void getViewers(list<ViewerInstance*>*) const` | All viewers. |
| `void getWriters(list<OutputEffectInstance*>*) const` | All writers. |
| `void refreshViewersAndPreviews()` | Force refresh. |
| `void quitAnyProcessingForAllNodes_blocking()` | Stop all renders. |
| `NodeGraphI* getNodeGraph() const` | Pointer to the GUI graph object (may be null). |
| `AppInstancePtr getApplication() const` | Parent app instance. |

### NodeGroup — Key Public Methods

| Method | Description |
|--------|-------------|
| `NodePtr getOutputNode(bool useGuiConnexions) const` | The Output node inside the group. |
| `NodePtr getOutputNodeInput(bool useGuiConnexions) const` | What feeds the Output node. |
| `void getInputs(vector<NodePtr>*, bool useGuiConnexions) const` | All Input nodes in the group. |
| `void setSubGraphEditable(bool)` | Lock/unlock editing. |
| `bool isSubGraphEditable() const` | Editable state. |

### NodeGroup — Key Signals

| Signal | When |
|--------|------|
| `graphEditableChanged(bool)` | Edit lock changed. |

---

## 5. Node (`Engine/Node.h`)

**Central graph node.** Inherits `QObject`, `std::enable_shared_from_this<Node>`, `CacheEntryHolder`. Has `Q_OBJECT` macro — full signal/slot support.

### Lifecycle

```
Constructor(app, group, plugin)
  → load(CreateNodeArgs)          // creates EffectInstance, loads knobs
  → initializeInputs()            // set up input slots
  → [active in graph]
  → deactivate(...)               // soft remove (undo-able)
  → activate(...)                 // restore from deactivate
  → destroyNode(blocking, reconnect)  // permanent removal
```

### Identity & Metadata

| Method | Description |
|--------|-------------|
| `const string& getScriptName() const` | Unique script-name (e.g. "Blur1"). |
| `string getScriptName_mt_safe() const` | Thread-safe version. |
| `string getFullyQualifiedName() const` | Qualified: `<g>group</g>Blur1`. |
| `void setScriptName(const string&)` | Rename (throws on conflict). |
| `const string& getLabel() const` | User-visible label. |
| `void setLabel(const string&)` | Set label. |
| `string getPluginID() const` | Plugin identifier. |
| `string getPluginLabel() const` | Human-readable plugin name. |
| `string getPluginResourcesPath() const` | Path to plugin resources. |
| `void getPluginGrouping(list<string>*) const` | Plugin menu grouping. |
| `bool isInputNode() const` | Has no inputs (generator/reader). |
| `bool isOutputNode() const` | Has no outputs (viewer/writer). |
| `bool isOpenFXNode() const` | OpenFX plugin? |
| `bool isBackdropNode() const` | Backdrop node? |
| `bool isRotoNode() const` / `isRotoPaintingNode()` | Roto/RotoPaint? |
| `bool isTrackerNodePlugin() const` | Tracker? |
| `bool isMultiInstance() const` | Multi-instance (e.g. Tracker)? |
| `bool isPartOfProject() const` | User-visible node (vs internal). |
| `bool isActivated() const` | Currently in the graph? |
| `bool isNodeDisabled() const` | Disabled by user? |
| `bool isNodeCreated() const` | Fully initialized? |
| `ViewerInstance* isEffectViewer() const` | Cast to viewer (or null). |
| `NodeGroup* isEffectGroup() const` | Cast to group (or null). |
| `const Plugin* getPlugin() const` | Plugin descriptor. |
| `AppInstancePtr getApp() const` | Owning app instance. |
| `NodeCollectionPtr getGroup() const` | Owning collection (project or group). |

### Input/Output Connections

| Method | Description |
|--------|-------------|
| `int getNInputs() const` | Number of input slots. |
| `NodePtr getInput(int index) const` | **MT-safe.** Input node at index (may be null). |
| `NodePtr getGuiInput(int index) const` | Input as shown on GUI (may differ during render). |
| `const vector<NodeWPtr>& getInputs() const` | All inputs (main-thread only). |
| `const NodesWList& getOutputs() const` | All output nodes. |
| `void getOutputsConnectedToThisNode(map<NodePtr, int>*)` | Outputs with their input-index. |
| `void getOutputsWithGroupRedirection(NodesList&)` | Outputs traversing groups. |
| `bool isInputConnected(int inputNb) const` | Is specific input connected? |
| `bool hasOutputConnected() const` | Any output? |
| `bool hasInputConnected() const` | Any input? |
| `bool hasAllInputsConnected() const` | All non-optional inputs filled? |
| `int inputIndex(const NodePtr&) const` | Which input index is this node on? (-1 if not). |
| `int getPreferredInputForConnection() const` | Best input for auto-connection. |
| `int getPreferredInput() const` | Preferred input for display. |
| `NodePtr getPreferredInputNode() const` | Node on preferred input. |
| `const vector<string>& getInputLabels() const` | Labels for all inputs. |
| `string getInputLabel(int) const` | Label for one input. |
| `bool isInputOptional(int) const` | Is input optional? |
| `bool isInputVisible(int) const` | Is input visible? |
| `CanConnectInputReturnValue canConnectInput(const NodePtr&, int)` | Pre-check connection validity. |
| `bool connectInput(const NodePtr& input, int inputNumber)` | **Connect input.** Returns success. |
| `int disconnectInput(int inputNumber)` | **Disconnect by index.** Returns index or -1. |
| `int disconnectInput(Node* input)` | **Disconnect by node.** Returns index or -1. |
| `bool replaceInput(const NodePtr&, int)` | Atomic disconnect+connect. |
| `bool checkIfConnectingInputIsOk(Node*) const` | **Cycle detection.** |

### EffectInstance Access

| Method | Description |
|--------|-------------|
| `EffectInstancePtr getEffectInstance() const` | **The live effect.** Primary way to access rendering. |
| `void setEffect(const EffectInstancePtr&)` | Set effect (internal use). |

### Knob (Parameter) Access

| Method | Description |
|--------|-------------|
| `const vector<KnobIPtr>& getKnobs() const` | All parameters. |
| `KnobIPtr getKnobByName(const string&) const` | Lookup by name. |
| `void beginEditKnobs()` | Start editing session. |
| `void setKnobsFrozen(bool)` | Make read-only. |
| `U64 getKnobsAge() const` | Hash age (incremented on change). |
| `bool hasAnimatedKnob() const` | Any animated parameters? |
| `void getAllKnobsKeyframes(list<SequenceTime>*)` | All keyframe times. |

### Node State & Position

| Method | Description |
|--------|-------------|
| `void setPosition(double x, double y)` | Position in node graph. |
| `void getPosition(double* x, double* y) const` | Get position. |
| `void setSize(double w, double h)` | Node size. |
| `void getSize(double* w, double* h) const` | Get size. |
| `bool getColor(double* r, double* g, double* b) const` | Node color. |
| `void setColor(double r, double g, double b)` | Set color. |
| `bool isSettingsPanelVisible() const` | Is settings panel open? |
| `bool isUserSelected() const` | Selected in graph? |
| `bool makePreviewByDefault() const` | Should show preview? |
| `bool isPreviewEnabled() const` | Preview currently on? |
| `void togglePreview()` | Toggle preview. |

### Activation / Deactivation / Destruction

| Method | Description |
|--------|-------------|
| `void deactivate(outputs, disconnectAll, reconnect, hideGui, triggerRender, unslaveKnobs)` | Soft-remove from graph (undo-able). |
| `void activate(outputs, restoreAll, triggerRender)` | Restore from deactivate. |
| `void destroyNode(bool blocking, bool autoReconnect)` | **Permanent removal.** |

### Messages

| Method | Description |
|--------|-------------|
| `bool message(MessageTypeEnum, const string&)` | Transient message (dialog). |
| `void setPersistentMessage(MessageTypeEnum, const string&)` | Persistent warning/error on node. |
| `void clearPersistentMessage(bool recurse)` | Clear persistent message. |
| `bool hasPersistentMessage() const` | Is there a message? |
| `void getPersistentMessage(QString*, int*, bool) const` | Get current message. |

### Preview

| Method | Description |
|--------|-------------|
| `bool makePreviewImage(SequenceTime, int* w, int* h, unsigned int* buf)` | Render preview thumbnail. |
| `bool isRenderingPreview() const` | Currently rendering preview? |

### Rendering Notifications

| Method | Description |
|--------|-------------|
| `bool notifyRenderingStarted()` | Returns true if this is the first render call. |
| `void notifyRenderingEnded()` | Paired with above. |
| `bool notifyInputNIsRendering(int)` | Input N started rendering. |
| `void notifyInputNIsFinishedRendering(int)` | Input N finished. |
| `bool isNodeRendering() const` | Currently rendering? |
| `void quitAnyProcessing_non_blocking()` | Abort renders. |
| `void abortAnyProcessing_blocking()` | Blocking abort. |

### Key Signals (for UI connection)

| Signal | Signature | When emitted |
|--------|-----------|-------------|
| `activated(bool triggerRender)` | `void` | Node activated in graph. |
| `deactivated(bool triggerRender)` | `void` | Node deactivated. |
| `inputChanged(int)` | `int` inputIndex | An input connection changed (GUI-side). |
| `outputsChanged()` | `void` | Outputs changed. |
| `inputsInitialized()` | `void` | Inputs are set up. |
| `knobsInitialized()` | `void` | All knobs created. |
| `labelChanged(QString)` | `QString` | Node label changed. |
| `scriptNameChanged(QString)` | `QString` | Script name changed. |
| `inputLabelChanged(int, QString)` | `int, QString` | Input label changed. |
| `inputEdgeLabelChanged(int, QString)` | `int, QString` | Edge label changed. |
| `inputVisibilityChanged(int)` | `int` | Input visibility toggled. |
| `refreshEdgesGUI()` | `void` | Edges need redraw. |
| `previewImageChanged(double)` | `double` time | Preview should update (auto-preview mode). |
| `previewRefreshRequested(double)` | `double` time | Preview should update (forced). |
| `renderingStarted()` | `void` | Node started rendering. |
| `renderingEnded()` | `void` | Node finished rendering. |
| `inputNIsRendering(int)` | `int` inputNb | Input N started rendering. |
| `inputNIsFinishedRendering(int)` | `int` inputNb | Input N finished rendering. |
| `persistentMessageChanged()` | `void` | Error/warning message changed. |
| `knobsAgeChanged(U64)` | `U64` age | Parameter hash changed. |
| `settingsPanelClosed(bool)` | `bool` closed | Settings panel closed. |
| `disabledKnobToggled(bool)` | `bool` disabled | Node enabled/disabled. |
| `previewKnobToggled()` | `void` | Preview knob toggled. |
| `canUndoChanged(bool)` / `canRedoChanged(bool)` | `bool` | Undo/redo availability. |
| `pluginMemoryUsageChanged(qint64)` | `qint64` delta | Plugin memory changed. |
| `allKnobsSlaved(bool)` | `bool` | All knobs slaved/unslaved. |
| `knobsLinksChanged()` | `void` | Knob links changed. |
| `knobSlaved()` | `void` | A knob was slaved/unslaved. |
| `nodeExtraLabelChanged(QString)` | `QString` | Extra label text changed. |
| `streamWarningsChanged()` | `void` | Bit depth/PAR/FPS warnings. |
| `outputLayerChanged()` | `void` | Output layer changed. |
| `availableViewsChanged()` | `void` | Views changed. |
| `hideInputsKnobChanged(bool)` | `bool` | Hide-inputs knob changed. |
| `mustDequeueActions()` | `void` | Actions need processing. |

### Key Slots

| Slot | Description |
|------|-------------|
| `doRefreshEdgesGUI()` | Emit `refreshEdgesGUI()`. |
| `computePreviewImage(double time)` | Force preview re-computation. |
| `refreshPreviewImage(double time)` | Refresh if auto-preview enabled. |
| `notifySettingsPanelClosed(bool)` | Emit `settingsPanelClosed()`. |
| `dequeueActions()` | Process queued actions. |
| `doComputeHashOnMainThread()` | Recompute hash. |

### InspectorNode (Subclass)

`InspectorNode` extends `Node` for nodes with dynamic inputs (e.g., Viewer with A/B wipe).

| Method | Description |
|--------|-------------|
| `void setInputA(int inputNb)` | Set A-side input. |
| `void setInputB(int inputNb)` | Set B-side input. |
| `void getActiveInputs(int& a, int& b) const` | Get current A/B. |
| `void setActiveInputAndRefresh(int, bool isASide)` | Switch active input. |

Additional signals: `refreshOptionalState()`, `activeInputsChanged()`.

---

## 6. EffectInstance (`Engine/EffectInstance.h`)

**Base class for all effects.** Inherits `NamedKnobHolder`, `LockManagerI<Image>`, `std::enable_shared_from_this`. **NOT a QObject** — no signals/slots.

### Core Identity

| Method | Description |
|--------|-------------|
| `NodePtr getNode() const` | The owning Node. |
| `U64 getHash() const` | Current hash (synchronized with GUI). |
| `U64 getKnobsAge() const` | Parameter age. |
| `const string& getScriptName() const` | Node script-name. |
| `string getFullyQualifiedName() const` | Qualified name. |
| `RectI getOutputFormat() const` | Output resolution. |
| `int getRenderViewsCount() const` | Number of views. |

### Input Access

| Method | Description |
|--------|-------------|
| `EffectInstancePtr getInput(int n) const` | **MT-safe.** Input effect (may be null). |
| `int getNInputs() const` | Number of inputs (pure virtual). |
| `bool isInputOptional(int) const` | Is input optional? (pure virtual). |
| `bool isInputMask(int) const` | Is input a mask? |
| `string getInputLabel(int) const` | Input label. |
| `bool hasOutputConnected() const` | Any output? |

### Plugin Info (Pure Virtuals)

| Method | Description |
|--------|-------------|
| `string getPluginID() const` | Plugin identifier. |
| `string getPluginLabel() const` | Human-readable name. |
| `void getPluginGrouping(list<string>*) const` | Menu grouping. |
| `string getPluginDescription() const` | Description. |
| `int getMajorVersion() const` / `getMinorVersion()` | Version. |
| `RenderSafetyEnum renderThreadSafety() const` | Thread safety level. |
| `bool isGenerator() const` | Has no inputs? |
| `bool isReader() const` / `isWriter()` | IO node? |
| `bool isOutput() const` | Has no outputs? |
| `bool isOpenFX() const` | OpenFX plugin? |
| `bool isFilter() const` | Processes input? |

### Rendering

| Method | Description |
|--------|-------------|
| `RenderRoIRetCode renderRoI(const RenderRoIArgs&, map<ImagePlaneDesc, ImagePtr>*)` | **Main render entry point.** Renders given RoI at time/scale/view. |
| `ImagePtr getImage(int inputNb, double time, RenderScale, ViewIdx, RectD* optionalBounds, ...)` | Get input image (calls renderRoI recursively). |
| `StatusEnum getRegionOfDefinition_public(U64 hash, double time, RenderScale, ViewIdx, RectD* rod, bool* isProjectFormat)` | Get output RoD. |
| `void getRegionsOfInterest_public(...)` | Get input RoIs for a given output RoI. |
| `FramesNeededMap getFramesNeeded_public(...)` | Frames needed from inputs. |
| `void getFrameRange_public(...)` | Frame range. |
| `bool isIdentity_public(...)` | Is this an identity (pass-through)? |
| `StatusEnum render_public(const RenderActionArgs&)` | Execute render action. |
| `bool aborted() const` | Should abort? |

### RenderRoIArgs Structure

```cpp
struct RenderRoIArgs {
    double time;
    RenderScale scale;
    unsigned int mipmapLevel;
    ViewIdx view;
    RectI roi;                          // pixel coordinates
    RectD preComputedRoD;               // canonical coordinates
    list<ImagePlaneDesc> components;    // requested layers
    InputImagesMap inputImagesList;     // pre-computed input images
    const EffectInstance* caller;
    ImageBitDepthEnum bitdepth;
    bool byPassCache;
    bool calledFromGetImage;
    StorageModeEnum returnStorage;      // RAM vs GPU
    bool allowGPURendering;
    double callerRenderTime;
};
```

### Capabilities (Virtuals to Override)

| Method | Description |
|--------|-------------|
| `bool supportsTiles() const` | Can render sub-regions? |
| `bool supportsMultiResolution() const` | Different input/output sizes? |
| `bool supportsRenderScale() const` | Can render at reduced resolution? |
| `bool supportsMultipleClipPARs() const` | Different pixel aspect ratios? |
| `bool supportsMultipleClipDepths() const` | Different bit depths? |
| `PluginOpenGLRenderSupport supportsOpenGLRender() const` | GPU rendering support. |
| `SequentialPreferenceEnum getSequentialPreference() const` | Sequential render preference. |
| `bool canRenderContinuously() const` | Non-integer frame times? |

### Metadata / Clip Preferences

| Method | Description |
|--------|-------------|
| `double getFrameRate() const` | Output frame rate. |
| `ImagePremultiplicationEnum getPremult() const` | Output premultiplication. |
| `double getAspectRatio(int inputNb) const` | Pixel aspect ratio. |
| `ImageBitDepthEnum getBitDepth(int inputNb) const` | Bit depth. |
| `void getMetadataComponents(int inputNb, ...)` | Components for input/output. |
| `bool isFrameVarying() const` | Frame-varying (e.g. reader)? |
| `bool isFrameVaryingOrAnimated_Recursive() const` | Tree is animated? |

### Messages

| Method | Description |
|--------|-------------|
| `bool message(MessageTypeEnum, const string&)` | Transient dialog. |
| `void setPersistentMessage(...)` | Persistent node error/warning. |
| `void clearPersistentMessage(bool recurse)` | Clear message. |

### Overlay / Interact

| Method | Description |
|--------|-------------|
| `bool hasOverlay() const` | Has overlay interact? |
| `void drawOverlay_public(...)` | Draw overlay. |
| `bool onOverlayPenDown/Motion/Up_public(...)` | Pen events. |
| `bool onOverlayKeyDown/Up/Repeat_public(...)` | Key events. |
| `void setCurrentViewportForOverlays_public(OverlaySupport*)` | Set viewport. |

---

## 7. CreateNodeArgs (`Engine/CreateNodeArgs.h`)

**Property bag for node creation.** Not a QObject. Uses typed property system.

### Key Properties

| Property Constant | Type | Default | Description |
|-------------------|------|---------|-------------|
| `kCreateNodeArgsPropPluginID` | `string` | **(required)** | Plugin ID to instantiate. |
| `kCreateNodeArgsPropPluginVersion` | `int x2` | `(-1, -1)` | Major/minor version (-1 = latest). |
| `kCreateNodeArgsPropNodeInitialPosition` | `double x2` | `(INT_MIN, INT_MIN)` | X,Y position in graph. |
| `kCreateNodeArgsPropNodeInitialName` | `string` | `""` | Initial script-name. |
| `kCreateNodeArgsPropNodeSerialization` | `shared_ptr<NodeSerialization>` | `null` | Restore from serialization. |
| `kCreateNodeArgsPropOutOfProject` | `bool` | `false` | Internal node (not serialized). |
| `kCreateNodeArgsPropNoNodeGUI` | `bool` | `false` | Skip GUI creation. |
| `kCreateNodeArgsPropSettingsOpened` | `bool` | `true` | Open settings panel on creation. |
| `kCreateNodeArgsPropAutoConnect` | `bool` | `true` | Auto-connect to selection. |
| `kCreateNodeArgsPropAddUndoRedoCommand` | `bool` | `true` | Push undo command. |
| `kCreateNodeArgsPropSilent` | `bool` | `false` | Suppress dialogs. |
| `kCreateNodeArgsPropTrustPluginID` | `bool` | `false` | Don't remap plugin IDs. |
| `kCreateNodeArgsPropGroupContainer` | `NodeCollectionPtr` | `null` | Group to create in (null = project root). |
| `kCreateNodeArgsPropMetaNodeContainer` | `NodePtr` | `null` | Parent meta-node (for Read/Write bundles). |

### Usage Pattern

```cpp
CreateNodeArgs args("net.sf.openfx.MergePlugin", project->getProject());
args.setProperty<double>(kCreateNodeArgsPropNodeInitialPosition, 100.0, 0);
args.setProperty<double>(kCreateNodeArgsPropNodeInitialPosition, 200.0, 1);
args.setProperty<bool>(kCreateNodeArgsPropAutoConnect, true);
args.addParamDefaultValue("operation", 0); // set default for "operation" knob
NodePtr node = appInstance->createNode(args);
```

---

## 8. Plugin (`Engine/Plugin.h`)

**Plugin descriptor.** Not a QObject. Plain C++ class.

### Key Methods

| Method | Description |
|--------|-------------|
| `const QString& getPluginID() const` | Plugin identifier string. |
| `const QString& getPluginLabel() const` | Human-readable name. |
| `const QString& getIconFilePath() const` | Icon path. |
| `const QStringList& getGrouping() const` | Menu hierarchy (e.g. ["Filter", "Blur"]). |
| `int getMajorVersion() const` / `getMinorVersion()` | Version numbers. |
| `bool isReader() const` / `isWriter() const` | IO type. |
| `bool getIsUserCreatable() const` | Can user create this? |
| `bool getIsDeprecated() const` | Deprecated? |
| `bool getIsForInternalUseOnly() const` | Internal only? |
| `bool isActivated() const` | Currently active? |
| `bool isRenderScaleEnabled() const` | Supports render scale? |
| `PluginOpenGLRenderSupport getPluginOpenGLRenderSupport() const` | GPU support level. |
| `const QString& getResourcesPath() const` | Plugin resource directory. |

### PluginsMap Structure

```cpp
// Maps pluginID -> set of Plugin* sorted by version
typedef std::map<std::string, std::set<Plugin*, Plugin_compare_version>> PluginsMap;
```

---

## Signal/Slot Quick Reference

### Signals to Connect for a Node Graph UI

| Source | Signal | UI Action |
|--------|--------|-----------|
| `Node` | `activated(bool)` | Show node in graph |
| `Node` | `deactivated(bool)` | Hide node from graph |
| `Node` | `inputChanged(int)` | Redraw edge |
| `Node` | `outputsChanged()` | Redraw all output edges |
| `Node` | `refreshEdgesGUI()` | Redraw edges |
| `Node` | `labelChanged(QString)` | Update node label |
| `Node` | `scriptNameChanged(QString)` | Update node name |
| `Node` | `renderingStarted()` | Show render indicator |
| `Node` | `renderingEnded()` | Hide render indicator |
| `Node` | `inputNIsRendering(int)` | Show input render indicator |
| `Node` | `inputNIsFinishedRendering(int)` | Hide input render indicator |
| `Node` | `previewImageChanged(double)` | Refresh thumbnail |
| `Node` | `persistentMessageChanged()` | Show/hide error badge |
| `Node` | `disabledKnobToggled(bool)` | Update enabled visual |
| `Node` | `nodeExtraLabelChanged(QString)` | Update subtitle |
| `Node` | `streamWarningsChanged()` | Show warning badge |
| `Node` | `knobsInitialized()` | Build settings panel |
| `Node` | `settingsPanelClosed(bool)` | Close settings panel |
| `Node` | `inputVisibilityChanged(int)` | Show/hide input edge |
| `Node` | `outputLayerChanged()` | Update layer info |
| `Node` | `hideInputsKnobChanged(bool)` | Toggle input visibility |
| `NodeGroup` | `graphEditableChanged(bool)` | Lock/unlock group editing |
| `AppInstance` | `pluginsPopulated()` | Populate node creation menu |

---

## Node Lifecycle

```
1. CREATION
   AppInstance::createNode(CreateNodeArgs)
     → new Node(app, group, plugin)
     → Node::load(args)                    // creates EffectInstance, loads knobs
     → Node::initializeInputs()            // set up input slots
     → AppInstance::createNodeGui(node)     // virtual: UI creates NodeGui
     → NodeCollection::autoConnectNodes()   // optional auto-connect

2. ACTIVE STATE
   - Node responds to: connectInput, disconnectInput, knob changes
   - Emits signals on state changes
   - EffectInstance::renderRoI() for rendering

3. DEACTIVATION (undo-able)
   Node::deactivate(outputs, disconnectAll, reconnect, hideGui)
     → disconnects inputs/outputs
     → emits deactivated()
     → node remains alive for undo

4. ACTIVATION (undo-able, pairs with deactivate)
   Node::activate(outputs, restoreAll)
     → reconnects
     → emits activated()

5. DESTRUCTION (permanent)
   Node::destroyNode(blocking, autoReconnect)
     → deactivate + remove from NodeCollection
     → object destroyed when last shared_ptr released
```

---

## Qt Coupling Summary

| Class | QObject? | Q_OBJECT? | Signals | Slots | Notes |
|-------|----------|-----------|---------|-------|-------|
| `AppManager` | Yes | Yes | 2 | Several | Singleton, manages QCoreApplication |
| `AppInstance` | Yes | Yes | 1 | Several | Per-project, has timeline integration |
| `Project` | Yes | Yes | (inherited) | — | KnobHolder + NodeCollection |
| `Node` | Yes | Yes | **~35** | Several | **Primary signal source for UI** |
| `NodeGroup` | Yes | Yes | 1 | — | Sub-graph container |
| `InspectorNode` | Yes | Yes | 2 | — | Viewer-style A/B inputs |
| `EffectInstance` | **No** | **No** | 0 | 0 | Pure rendering logic |
| `NodeCollection` | **No** | **No** | 0 | 0 | Graph operations |
| `CreateNodeArgs` | **No** | **No** | 0 | 0 | Property bag |
| `Plugin` | **No** | **No** | 0 | 0 | Descriptor |

**Key takeaway:** `Node` is the primary signal emitter. `EffectInstance` is pure engine with no Qt dependency beyond `QObject` base for `Q_OBJECT` macro (it does have `Q_OBJECT` but no custom signals). All UI state changes flow through `Node` signals.

---

## Key Type Aliases

From `Engine/EngineFwd.h`:

| Alias | Type |
|-------|------|
| `NodePtr` | `shared_ptr<Node>` |
| `NodeWPtr` | `weak_ptr<Node>` |
| `NodesList` | `list<NodePtr>` |
| `NodesWList` | `list<NodeWPtr>` |
| `NodeCollectionPtr` | `shared_ptr<NodeCollection>` |
| `NodeGroupPtr` | `shared_ptr<NodeGroup>` |
| `EffectInstancePtr` | `shared_ptr<EffectInstance>` |
| `AppInstancePtr` | `shared_ptr<AppInstance>` |
| `ProjectPtr` | `shared_ptr<Project>` |
| `PluginPtr` | (raw `Plugin*`) |
| `KnobIPtr` | `shared_ptr<KnobI>` |
| `KnobsVec` | `vector<KnobIPtr>` |
| `ImagePtr` | `shared_ptr<Image>` |
| `ViewerInstancePtr` | `shared_ptr<ViewerInstance>` |
| `OutputEffectInstancePtr` | `shared_ptr<OutputEffectInstance>` |
| `PluginsMap` | `map<string, set<Plugin*>>` |

---

## NodeGraphI (`Engine/NodeGraphI.h`)

Minimal interface for engine→GUI communication:

```cpp
class NodeGraphI {
public:
    virtual ~NodeGraphI() {}
    virtual void onNodesCleared() = 0;
};
```

The GUI's `NodeGraph` (in `Gui/NodeGraph.h`) implements this. Set on `NodeCollection` via `setNodeGraphPointer()`. This is how the engine notifies the GUI that nodes were cleared.

The `Gui::NodeGraph` class itself is a `QGraphicsView` with full scene management, node selection, edge drawing, copy/paste, undo/redo, etc. For Flux, this is the primary class to replace.
