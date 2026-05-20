# Natron Engine Parameter (Knob) System & Serialization

## Objective

Comprehensive documentation of the Natron Engine's parameter system, animation framework, project serialization, application settings, and Python bindings. This document serves as the authoritative reference for Flux development, particularly for the Layer-to-Node bridge and JSON serialization format design.

---

## 1. Knob (Parameter) System

### 1.1 Class Hierarchy

The Knob system uses a type-parameterized, multi-dimensional, animatable parameter framework built on Qt signals/slots and thread-safe value storage.

```
OverlaySupport (OpenGL viewport interface)
  └── KnobI  (abstract interface, std::enable_shared_from_this)
        └── KnobHelper  (non-templated implementation base)
              └── Knob<T>  (templated on data type)
                    ├── KnobIntBase      = Knob<int>
                    ├── KnobDoubleBase   = Knob<double>
                    ├── KnobBoolBase     = Knob<bool>
                    └── KnobStringBase   = Knob<std::string>

// Concrete Knob Types (KnobTypes.h, KnobFile.h):
QObject + KnobIntBase    → KnobInt       (integer parameter)
QObject + KnobBoolBase   → KnobBool      (boolean/checkbox)
QObject + KnobBoolBase   → KnobButton    (button action)
QObject + KnobIntBase    → KnobChoice    (dropdown menu)
         KnobBoolBase   → KnobSeparator  (visual separator)
QObject + KnobDoubleBase → KnobDouble    (floating-point)
QObject + KnobDoubleBase → KnobColor     (RGBA color)
QObject + KnobBoolBase   → KnobGroup     (collapsible group)
QObject + KnobBoolBase   → KnobPage      (tab page)

KnobStringBase → AnimatingKnobStringHelper (string animation support)
  ├── KnobString     (text parameter)
  └── QObject + AnimatingKnobStringHelper → KnobFile       (file input)
                       KnobStringBase → QObject + KnobStringBase → KnobOutputFile (file output)

QObject + KnobStringBase → KnobTable (string table)
  └── KnobPath     (file path list)
  └── KnobLayers   (layer table)

QObject + KnobDoubleBase → KnobParametric (parametric curve)
```

**Key source files:**
- `Engine/Knob.h:54-2892` — Base classes: `KnobSignalSlotHandler`, `KnobI`, `KnobHelper`, `Knob<T>`
- `Engine/KnobTypes.h:1-1215` — Concrete types: `KnobInt`, `KnobBool`, `KnobDouble`, `KnobChoice`, `KnobColor`, `KnobString`, `KnobButton`, `KnobGroup`, `KnobPage`, `KnobSeparator`, `KnobParametric`, `KnobTable`, `KnobLayers`
- `Engine/KnobFile.h` — File-related types: `KnobFile`, `KnobOutputFile`, `KnobPath`
- `Engine/KnobImpl.h` — Template implementations for `Knob<T>` methods
- `Engine/KnobFactory.h/cpp` — Factory for creating knobs by type name

**Holder hierarchy:**
```
QObject
  └── KnobHolder  (owns/manages knobs, calls evaluate on change)
        └── NamedKnobHolder  (adds script name)
              └── EffectInstance  (the visual effect node)
```

### 1.2 All Knob Types and Their APIs

#### KnobInt (`Engine/KnobTypes.h:64-143`)
- **Base**: `QObject, KnobIntBase` (i.e. `Knob<int>`)
- **canAnimate**: Yes
- **Dimensions**: Variable (1 for scalar, 2 for 2D, 4 for rectangle)
- **Key methods**:
  - `setIncrement(int incr, int index=0)` / `getIncrements()` — UI step size
  - `disableSlider()` / `isSliderDisabled()` — hide slider, show spinbox only
  - `setAsRectangle()` — when dim=4, treat as rectangle (x1,y1,x2,y2)
  - `setValueFromPlugin(int, view, dim)` — set from plugin code
  - `getValue(dim=0, view, clamp)` / `getValueAtTime(time, dim, view, clamp)`
  - `setDefaultValue(int v, dim=0)` / `getDefaultValue(dim)`
  - `setMinimum(int, dim)` / `setMaximum(int, dim)`

#### KnobBool (`Engine/KnobTypes.h:147-191`)
- **Base**: `KnobBoolBase` (i.e. `Knob<bool>`)
- **canAnimate**: Yes (via curve interpolation, rounded to 0/1)
- **Dimensions**: 1
- Simple checkbox parameter

#### KnobDouble (`Engine/KnobTypes.h:195-383`)
- **Base**: `QObject, KnobDoubleBase` (i.e. `Knob<double>`)
- **canAnimate**: Yes
- **Dimensions**: Variable (1-4)
- **Key methods**:
  - `setIncrement(double, index)` / `setDecimals(int, index)` — UI precision
  - `disableSlider()` / `isSliderDisabled()`
  - `setSpatial(bool)` / `getIsSpatial()` — coordinate system params
  - `normalize(dim, time, value)` / `denormalize(dim, time, value)` — OFX normalized coords
  - `setHasHostOverlayHandle(bool)` — interactive overlay handle
  - `setAsRectangle()` — when dim=4
  - `setCanAutoFoldDimensions(bool)` — collapse multi-dim into single widget
  - Signals: `incrementChanged(double, int)`, `decimalsChanged(int, int)`

#### KnobButton (`Engine/KnobTypes.h:387-461`)
- **Base**: `KnobBoolBase` (uses bool internally for checked state)
- **canAnimate**: No
- **Key methods**:
  - `trigger()` — programmatically fire the button
  - `setAsRenderButton()` / `isRenderButton()` — special render trigger
  - `setCheckable(bool)` / `getIsCheckable()` — toggle button mode

#### KnobChoice (`Engine/KnobTypes.h:465-600`)
- **Base**: `QObject, KnobIntBase` (stores selected index as int)
- **canAnimate**: Yes (constant interpolation only between indices)
- **Key methods**:
  - `populateChoices(vector<ChoiceOption>)` — set menu entries
  - `resetChoices()` / `appendChoice(ChoiceOption)`
  - `getEntries_mt_safe()` / `getEntry(int)` / `getActiveEntry()`
  - `setValueFromID(string, dim)` — set by option ID string
  - `setCascading(bool)` — hierarchical menu
  - Signals: `populated()`, `entriesReset()`, `entryAppended()`

#### KnobColor (`Engine/KnobTypes.h:650-725`)
- **Base**: `QObject, KnobDoubleBase` (each channel is a double in [0,1])
- **canAnimate**: Yes
- **Dimensions**: 1 (grayscale), 3 (RGB), or 4 (RGBA)
- **Key methods**:
  - `areAllDimensionsEnabled()` / `activateAllDimensions()`
  - `setPickingEnabled(bool)` — color picker mode
  - `setSimplified(bool)` — minimal UI
  - Signals: `pickingEnabled(bool)`, `minMaxChanged(...)`

#### KnobString (`Engine/KnobTypes.h:730-837`)
- **Base**: `AnimatingKnobStringHelper` (extends `KnobStringBase`)
- **canAnimate**: Yes (constant string per keyframe)
- **Key methods**:
  - `setAsMultiLine()` / `isMultiLine()` — multi-line text
  - `setUsesRichText(bool)` — HTML rich text
  - `setAsLabel()` / `isLabel()` — read-only display label
  - `setAsCustom()` / `isCustomKnob()` — custom rendering

#### KnobFile (`Engine/KnobFile.h:49-113`)
- **Base**: `QObject, AnimatingKnobStringHelper`
- **canAnimate**: No (but handles file sequences via pattern matching)
- **Key methods**:
  - `setAsInputImage()` / `isInputImageFile()` — marks as image input
  - `open_file()` — trigger file dialog
  - `reloadFile()` — reload current file
  - `getFileName(int time, ViewSpec view)` — get filename (handles sequences)
  - Signal: `openFile()`

#### KnobOutputFile (`Engine/KnobFile.h:117-184`)
- **Base**: `QObject, KnobStringBase`
- **canAnimate**: No
- **Key methods**:
  - `setAsOutputImageFile()` / `isOutputImageFile()`
  - `generateFileNameAtTime(time, view)` — generate filename for specific frame

#### KnobPath (`Engine/KnobFile.h:194-264`)
- **Base**: `KnobTable`
- **canAnimate**: No
- **Key methods**:
  - `setMultiPath(bool)` / `isMultiPath()` — multiple paths
  - `getPaths(list<string>)` / `prependPath(string)` / `appendPath(string)`

#### KnobParametric (`Engine/KnobTypes.h:970-1074`)
- **Base**: `QObject, KnobDoubleBase`
- **canAnimate**: No (has its own internal curve system)
- **Dimensions**: Each dimension is a separate parametric curve
- **Key methods**:
  - `setParametricRange(min, max)` / `getParametricRange()`
  - `addControlPoint(reason, dim, key, value, interpolation)` — add point
  - `getValue(dim, parametricPosition, *returnValue)` — evaluate curve
  - `getNControlPoints(dim, *count)` / `getNthControlPoint(dim, n, *key, *value)`
  - `saveParametricCurves(list<Curve>)` / `loadParametricCurves(list<Curve>)`
  - Signals: `curveChanged(int)`, `curveColorChanged(int)`

#### KnobGroup (`Engine/KnobTypes.h:840-902`)
- **Base**: `QObject, KnobBoolBase` (value = expanded/collapsed state)
- **canAnimate**: No
- **Key methods**:
  - `addKnob(KnobIPtr)` / `removeKnob(KnobI*)` — add child knobs
  - `getChildren()` — get all child knobs
  - `setAsTab()` / `isTab()` — render as tab instead of collapsible group

#### KnobPage (`Engine/KnobTypes.h:907-965`)
- **Base**: `QObject, KnobBoolBase`
- **canAnimate**: No
- **Key methods**:
  - `addKnob(KnobIPtr)` / `removeKnob(KnobI*)`
  - `getChildren()` — child knobs
  - `setAsToolBar(bool)` — toolbar page

#### KnobSeparator (`Engine/KnobTypes.h:604-640`)
- **Base**: `KnobBoolBase`
- **canAnimate**: No
- Visual separator line in the parameter panel

#### KnobTable (`Engine/KnobTypes.h:1079-1140`)
- **Base**: `QObject, KnobStringBase`
- **canAnimate**: No
- Abstract base for table-like parameters
- **Key virtual methods**: `getColumnsCount()`, `getColumnLabel(int col)`, `isCellEnabled(int row, int col)`
- **Key methods**: `getTable()`, `setTable()`, `appendRow()`, `removeRow()`

#### KnobLayers (`Engine/KnobTypes.h:1142-1211`)
- **Base**: `KnobTable`
- Fixed 2-column table: Name + Channels

### 1.3 How Dimensions Work

Multi-dimensional knobs store separate values per dimension. The dimension count is set at construction and never changes.

**Internal storage** (`Engine/Knob.h:2238-2252`, `Engine/KnobImpl.h:91-112`):
```
_values          = vector<T>        // current values (one per dimension)
_guiValues       = vector<T>        // GUI-thread copy
_defaultValues   = vector<DefaultValue>  // defaults per dimension
_exprRes         = ExprResults      // expression results cache per dimension
_minimums/_maximums  = vector<T>    // range per dimension
```

**Examples:**
- `KnobColor` dim=1: single grayscale channel
- `KnobColor` dim=3: R (dim 0), G (dim 1), B (dim 2)
- `KnobColor` dim=4: R (dim 0), G (dim 1), B (dim 2), A (dim 3)
- `KnobDouble` dim=2: X/Y position
- `KnobDouble` dim=4: rectangle (x1, y1, x2, y2)

**Multi-value setters** (`Engine/Knob.h:1889-1992`):
- `setValues(v0, v1, view, reason)` — 2D
- `setValues(v0, v1, v2, view, reason)` — 3D
- `setValues(v0, v1, v2, v3, view, reason)` — 4D
- Same pattern for `setValuesAtTime()`

These bracket calls with `beginChanges()`/`endChanges()` and `blockValueChanges()`/`unblockValueChanges()` to batch evaluations.

### 1.4 Signal/Slot Mechanism for Change Notification

Every knob has a `KnobSignalSlotHandler` (Qt QObject) accessible via `getSignalSlotHandler()`. It bridges the engine (any thread) to the GUI (main thread).

**Key signals** (`Engine/Knob.h:54-352`):

| Signal | When emitted |
|--------|-------------|
| `valueChanged(view, dim, reason)` | Any value change |
| `keyFrameSet(time, view, dim, reason, added)` | Keyframe added |
| `keyFrameRemoved(time, view, dim, reason)` | Keyframe removed |
| `keyFrameMoved(view, dim, oldTime, newTime)` | Keyframe dragged |
| `animationRemoved(view, dim)` | All keyframes cleared |
| `secretChanged()` | Visibility toggled |
| `enabledChanged()` | Dimension enabled/disabled |
| `expressionChanged(dim)` | Python expression changed |
| `dirty(bool)` | Multi-value dirty state |

**ValueChangedReasonEnum** (the `reason` parameter):
- `eValueChangedReasonUserEdited` — direct user interaction
- `eValueChangedReasonPluginEdited` — plugin code called setValue
- `eValueChangedReasonNatronInternalEdited` — Natron internal
- `eValueChangedReasonNatronGuiEdited` — Natron GUI internal
- `eValueChangedReasonTimeChanged` — timeline scrub

### 1.5 How to Create Parameters for New Node Types

A node (subclass of `EffectInstance`) must override `initializeKnobs()`:

**Example from Backdrop** (`Engine/Backdrop.cpp:64-76`):
```cpp
void Backdrop::initializeKnobs()
{
    // Create a page (tab)
    KnobPagePtr page = AppManager::createKnob<KnobPage>(this, tr("Controls"));
    
    // Create a string knob
    KnobStringPtr knobLabel = AppManager::createKnob<KnobString>(this, tr("Label"));
    knobLabel->setAnimationEnabled(false);
    knobLabel->setAsMultiLine();
    knobLabel->setUsesRichText(true);
    knobLabel->setHintToolTip(tr("Text to display on the backdrop."));
    knobLabel->setEvaluateOnChange(false);
    
    // Add to page
    page->addKnob(knobLabel);
    
    // Store reference for later access
    _imp->knobLabel = knobLabel;
}
```

**Responding to knob changes** — Override `knobChanged()`:
```cpp
bool Backdrop::knobChanged(KnobI* k, ValueChangedReasonEnum reason,
                           ViewSpec view, double time,
                           bool originatedFromMainThread)
{
    if (k == _imp->knobLabel.lock().get()) {
        // handle change
        return true;  // return true if handled
    }
    return false;  // unhandled
}
```

**KnobHolder convenience methods** (`Engine/Knob.h:2517-2532`) for runtime user knobs:
```
createIntKnob(name, label, dimension)
createDoubleKnob(name, label, dimension)
createColorKnob(name, label, dimension)
createBoolKnob(name, label)
createChoiceKnob(name, label)
createButtonKnob(name, label)
createSeparatorKnob(name, label)
createStringKnob(name, label)
createFileKnob(name, label)
createOuptutFileKnob(name, label)
createPathKnob(name, label)
createPageKnob(name, label)
createGroupKnob(name, label)
createParametricKnob(name, label, nbCurves)
```

### 1.6 Thread Safety Model

- **Value storage**: Protected by `QRecursiveMutex _valueMutex`
- **Min/max ranges**: Protected by `QReadWriteLock _minMaxMutex`
- **GUI vs render thread**: `_guiValues` vs `_values` — GUI thread reads `_guiValues`, render threads read `_values`
- **Choice entries**: Protected by `QMutex _entriesMutex`
- **Parametric curves**: Protected by `QMutex _curvesMutex`
- **Deferred value setting**: When `setValue` is called during rendering (when `isSetValueCurrentlyPossible()` returns false), values are queued in `_setValuesQueue` and dequeued later when safe

### 1.7 Value Resolution Priority

When `getValue()` or `getValueAtTime()` is called (`Engine/KnobImpl.h:684-854`):

1. **Expression** — If expression is set for this dimension, evaluate Python expression (with caching)
2. **Master/slave** — If knob is slaved to another, delegate to master knob
3. **Animation** — If curve has keyframes, interpolate via `Curve::getValueAt()`
4. **Stored value** — Return `_values[dim]` (or `_guiValues[dim]` on GUI thread)

---

## 2. Animation System

### 2.1 Keyframe Data Structure

**File:** `Engine/Curve.h:50-114`

```cpp
class KeyFrame {
    double _time;              // X position (time/frame)
    double _value;             // Y position (parameter value)
    double _leftDerivative;    // Tangent slope arriving at this key
    double _rightDerivative;   // Tangent slope leaving this key
    KeyframeTypeEnum _interpolation; // Interpolation method
};
```

- All constructors assert against NaN/Inf values
- Default interpolation is `eKeyframeTypeSmooth`
- Comparison is by time only for ordering: `KeyFrame_compare_time` uses `lhs.getTime() < rhs.getTime()`
- Keyframes are stored in a `std::set<KeyFrame, KeyFrame_compare_time>` — a time-ordered set

### 2.2 Interpolation Types

**Defined in** `Global/Enums.h:102-112`:

| Enum | Value | Description |
|---|---|---|
| `eKeyframeTypeConstant` | 0 | Hold/step — no interpolation |
| `eKeyframeTypeLinear` | 1 | Straight line between keys |
| `eKeyframeTypeSmooth` | 2 | Auto-clamped Catmull-Rom (default) |
| `eKeyframeTypeCatmullRom` | 3 | Standard Catmull-Rom spline |
| `eKeyframeTypeCubic` | 4 | C2-continuous cubic (equal 2nd derivatives) |
| `eKeyframeTypeHorizontal` | 5 | Zero tangent (flat at keyframe) |
| `eKeyframeTypeFree` | 6 | User-controlled, both handles independent |
| `eKeyframeTypeBroken` | 7 | User-controlled, handles not linked |
| `eKeyframeTypeNone` | 8 | Virtual keyframe (before first / after last) |

### 2.3 Curve Class

**File:** `Engine/Curve.h`, `Engine/CurvePrivate.h`, `Engine/Curve.cpp`

```cpp
struct CurvePrivate {
    KeyFrameSet keyFrames;          // std::set<KeyFrame, KeyFrame_compare_time>
    KnobI* owner;                   // owning Knob
    int dimensionInOwner;           // which dimension of the Knob
    CurveTypeEnum type;             // determines value clamping behavior
    double xMin, xMax;              // X range (for parametric curves)
    double yMin, yMax;              // Y range (for clamping)
    mutable QRecursiveMutex _lock;  // thread safety
    bool isParametric;              // if true, X values are not clamped to integers
    bool isPeriodic;                // periodic curve (first key == last key)
};
```

**Curve type enum** (`Engine/CurvePrivate.h:46-54`):

| Type | Description |
|---|---|
| `eCurveTypeDouble` | Free real values |
| `eCurveTypeInt` | Values rounded to integers |
| `eCurveTypeIntConstantInterp` | Integers with constant-only interpolation (choice params) |
| `eCurveTypeBool` | Values thresholded to 0 or 1 (>= 0.5 → 1) |
| `eCurveTypeString` | Integer-indexed for string lookup |

**Key Curve methods:**

| Method | Purpose |
|---|---|
| `addKeyFrame(KeyFrame)` | Add/replace keyframe; auto-forces constant interpolation for bool/string/int/choice types |
| `removeKeyFrameWithTime(double)` | Remove by exact time |
| `getValueAt(double t, bool clamp)` | Interpolated value at time t |
| `getDerivativeAt(double t)` | First derivative at time t |
| `getIntegrateFromTo(t1, t2)` | Definite integral between two times |
| `setKeyFrameValueAndTime(time, value, index)` | Move keyframe position and value |
| `setKeyFrameInterpolation(interp, index)` | Change interpolation type |
| `setCurveInterpolation(interp)` | Set interpolation for ALL keyframes |
| `smooth(range)` | Laplacian smoothing over a range |
| `clone(other)` | Deep-copy keyframes from another curve |

### 2.4 Interpolation Algorithms

**File:** `Engine/Interpolation.cpp`

All interpolation uses **cubic Hermite splines**. The interpolation type only affects how tangent derivatives are computed at each keyframe.

1. **Hermite → Cubic polynomial conversion** (`Engine/Interpolation.cpp:48-63`):
   ```
   c0 = P0
   c1 = P0' (right derivative, normalized)
   c2 = 3(P3 - P0) - 2P0' - P3'
   c3 = -2(P3 - P0) + P0' + P3'
   ```

2. **Evaluation**: `cubicEval(c0,c1,c2,c3,t) = c0 + c1*t + c2*t^2 + c3*t^3`

3. **Auto-derivative computation** (`Engine/Interpolation.cpp:941-1106`):
   - **Linear**: Sets 2nd derivative to zero
   - **Catmull-Rom**: `deriv = (vnext - vprev) / (tnext - tprev)`
   - **Smooth**: Catmull-Rom with clamping — if `vcur` is outside `[vprev, vnext]`, uses horizontal (zero derivative)
   - **Cubic**: Enforces C2 continuity (equal 2nd derivatives)
   - **Horizontal/Constant**: Both derivatives set to 0

### 2.5 Expression System

**Expression storage** (`Engine/Knob.cpp:363-377`):
```cpp
struct Expr {
    std::string expression;         // Modified by Natron (with Python wrapper)
    std::string originalExpression; // Raw user input
    std::string exprInvalid;        // Error message if invalid
    bool hasRet;                    // Whether expression uses 'ret' variable
    std::list<std::pair<KnobIWPtr, int>> dependencies; // Referenced knobs
};
```

**Expression evaluation flow:**

1. `Knob<T>::getValueAtTime()` checks if expression exists for the dimension
2. Checks `_exprRes[dimension]` (a `std::map<double, T>`) for cached result at this time
3. If not cached, calls `evaluateExpression()` which wraps expression as `expr(time, view)` and executes via Python C API
4. Expression is expected to set a `ret` variable in the Python module scope
5. Result is cached: `_exprRes[dimension].insert(make_pair(time, ret))`
6. Cache is cleared when curves change via `Curve::onCurveChanged()`

**Expression API on Knob:**
- `setExpression(dim, expr, hasRetVariable, failIfInvalid)` — set expression
- `getExpression(dim)` — get expression string
- `clearExpression(dim, clearResults)` — remove expression
- `isExpressionValid(dim, *error)` — check validity

### 2.6 Knob ↔ Curve ↔ Keyframe Relationship

```
Knob<T> (e.g. KnobDouble)
  ├── KnobHelperPrivate
  │     ├── CurvesMap curves;               // vector<shared_ptr<Curve>>, one per dimension
  │     ├── vector<Expr> expressions;        // one per dimension
  │     └── MastersMap masters;              // slave/master links
  │
  ├── vector<T> _values;                     // current static values per dimension
  ├── ExprResults _exprRes;                  // expression result cache per dimension
  │
  └── Each Curve:
        ├── KeyFrameSet keyFrames;           // std::set<KeyFrame> (time-ordered)
        ├── KnobI* owner;                    // back-pointer to Knob
        ├── int dimensionInOwner;            // which dimension
        └── CurveTypeEnum type;              // determines value clamping behavior
```

---

## 3. Serialization System

### 3.1 File Format

- Project files use the `.ntp` extension
- Auto-save files append `.autosave` to the filename
- Backup files use the `.~N~` suffix pattern with rotation
- Uses **Boost.Serialization** with **XML archive** format

### 3.2 Top-Level XML Structure

Written in `Project::saveProjectInternal()` (`Engine/Project.cpp:549-708`):

```xml
<Background_project>0</Background_project>
<Project class_id="..." version="6">
  <VersionMajor>2</VersionMajor>
  <VersionMinor>4</VersionMinor>
  <VersionRev>0</VersionRev>
  <GitBranch>RB-2.6</GitBranch>
  <GitCommit>abc123</GitCommit>
  <OS>Linux</OS>
  <Bits>64</Bits>
  <NodesCollection>
    <NodesCount>3</NodesCount>
    <item><!-- NodeSerialization --></item>
    <item><!-- NodeSerialization --></item>
    <item><!-- NodeSerialization --></item>
  </NodesCollection>
  <ProjectKnobsCount>12</ProjectKnobsCount>
  <item><!-- KnobSerialization --></item>
  <AdditionalFormats>...</AdditionalFormats>
  <Timeline_current_time>0</Timeline_current_time>
  <CreationDate>...</CreationDate>
</Project>
<!-- GUI state follows via saveProjectGui() if not background mode -->
```

### 3.3 Node Serialization

**Defined in** `Engine/NodeSerialization.h:77-437`, current version is **15**.

```xml
<item>
  <Plugin_label>Blur1</Plugin_label>
  <Plugin_script_name>Blur1</Plugin_script_name>
  <Plugin_id>net.sf.openfx.BlurPlugin</Plugin_id>
  <PythonModule>/path/to/plugin.py</PythonModule>
  <PythonModuleVersion>1</PythonModuleVersion>
  <Plugin_major_version>1</Plugin_major_version>
  <Plugin_minor_version>0</Plugin_minor_version>
  <KnobsCount>5</KnobsCount>
  <item><!-- KnobSerialization --></item>
  ...
  <Inputs_map>
    <count>2</count>
    <item>
      <first>Source</first>
      <second>Read1</second>
    </item>
  </Inputs_map>
  <KnobsAge>123456789</KnobsAge>
  <MasterNode></MasterNode>
  <HasRotoContext>0</HasRotoContext>
  <HasTrackerContext>0</HasTrackerContext>
  <Children>0</Children>
  <UserComponents>...</UserComponents>
  <CacheID>abc123</CacheID>
</item>
```

**Key node fields:**
- `Plugin_id` — identifies the OpenFX plugin or built-in effect
- `Plugin_script_name` — the unique script name used for connections
- `Inputs_map` — maps input label names to connected node script names
- `KnobsAge` — hash used for cache invalidation
- `MasterNode` — fully qualified name for master/slave knob links
- `HasRotoContext` / `RotoContext` — rotoscoping data
- `HasTrackerContext` / `TrackerContext` — tracking data
- `Children` — for groups and multi-instance nodes
- `PythonModule` / `PythonModuleVersion` — for PyPlug group nodes

### 3.4 Knob Serialization

**Defined in** `Engine/KnobSerialization.h:505-1004`, current version is **14**.

```xml
<item>
  <Name>size</Name>
  <Type>Double</Type>
  <Dimension>1</Dimension>
  <Secret>0</Secret>
  <MasterIsAlias>0</MasterIsAlias>
  <!-- Per-dimension ValueSerialization: -->
  <item>
    <Enabled>1</Enabled>
    <HasAnimation>1</HasAnimation>
    <Curve>                          <!-- Only if HasAnimation -->
      <KeyFrameSet>
        <count>3</count>
        <item>
          <Time>0</Time>
          <Value>5</Value>
          <InterpolationMethod>2</InterpolationMethod>
          <LeftDerivative>0</LeftDerivative>
          <RightDerivative>0</RightDerivative>
        </item>
      </KeyFrameSet>
    </Curve>
    <Value>5</Value>
    <Default>5</Default>
    <HasMaster>0</HasMaster>
    <Expression></Expression>
    <ExprHasRet>0</ExprHasRet>
  </item>
  <!-- Type-specific extra data -->
  <StringsAnimation>...</StringsAnimation>  <!-- For string knobs -->
  <ParametricCurves>...</ParametricCurves>  <!-- For parametric knobs -->
  <ChoiceLabel>option_id</ChoiceLabel>       <!-- For choice knobs -->
  <UserKnob>0</UserKnob>
</item>
```

### 3.5 Per-Type Serialization Details

The `ValueSerialization::save()` method (`Engine/KnobSerialization.h:281-344`) uses `dynamic_cast` to determine the knob type:

| Knob Type | Value Type | XML Tag | Notes |
|---|---|---|---|
| `KnobIntBase` (not Choice) | `int` | `<Value>` | Integer parameter |
| `KnobBoolBase` (not Page/Group/Sep/Button) | `bool` | `<Value>` | Boolean parameter |
| `KnobDoubleBase` (not Parametric) | `double` | `<Value>` | Floating-point parameter |
| `KnobChoice` | `int` + label | `<Value>` | Dropdown; stores index + `ChoiceExtraData` |
| `KnobStringBase` | `string` | `<Value>` | String parameter |
| `KnobFile` | `string` | `<Value>` | File path; special handling for sequence patterns |
| `KnobParametric` | - | `<ParametricCurves>` | Stores list of Curve objects |

**Extra data per type** (when `UserKnob=1`, `Engine/KnobSerialization.h:566-608`):

| Type | Extra Fields |
|---|---|
| Choice | `ChoiceLabel`, `Entries`, `Helps` |
| Double/Int/Color | `Min`, `Max`, `DMin`, `DMax` |
| File/OutputFile | `Sequences` |
| Path | `MultiPath` |
| String (Text) | `IsLabel`, `IsMultiLine`, `UseRichText` |
| Double (2D) | `HasOverlayHandle` |

### 3.6 Animation Data Serialization

**Curve/KeyFrame** serialization (`Engine/CurveSerialization.h:54-73`):

```xml
<item>
  <Time>0</Time>              <!-- x position (frame number) -->
  <Value>5</Value>            <!-- y position -->
  <InterpolationMethod>2</InterpolationMethod>  <!-- KeyframeTypeEnum -->
  <LeftDerivative>0</LeftDerivative>
  <RightDerivative>0</RightDerivative>
</item>
```

**String animation** uses `StringAnimationManager` — a `std::map<int, std::string>` of frame numbers to string values (since strings can't be interpolated via curves).

**Parametric curves** are serialized as a list of `Curve` objects.

### 3.7 Connection Serialization

Node connections are serialized as a map of **input label** to **connected node script name**:

```xml
<Inputs_map>
  <count>2</count>
  <item>
    <first>Source</first>
    <second>Read1</second>
  </item>
</Inputs_map>
```

Connections are restored in a second pass after all nodes are created (handles forward references).

### 3.8 External Resource References

- **File paths**: Stored as knob values; environment variable expansion with `[Project]`, `[OCIO]`, and user-defined variables
- **Plugin references**: `Plugin_id` + `Plugin_major_version` / `Plugin_minor_version`
- **Python modules**: `PythonModule` — absolute path to `.py` file for PyPlug groups
- **Path resolution**: `Project::simplifyPath()` replaces absolute paths with variable references; `Project::canonicalizePath()` expands them back

### 3.9 Version Handling

Multi-layered versioning scheme:

| Level | Current Version | Constant |
|---|---|---|
| Project | 6 | `PROJECT_SERIALIZATION_VERSION` |
| Node | 15 | `NODE_SERIALIZATION_CURRENT_VERSION` |
| Knob | 14 | `KNOB_SERIALIZATION_VERSION` |
| Value | 7 | `VALUE_SERIALIZATION_VERSION` |
| Master | 3 | `MASTER_SERIALIZATION_VERSION` |
| Format | 3 | `FORMAT_SERIALIZATION_VERSION` |

**Forward compatibility check**: Throws if project version > current version.
**Backward compatibility**: `filterKnobNameCompat()` remaps old knob names; `filterKnobChoiceOptionCompat()` remaps old choice option IDs.

### 3.10 Save/Load Workflow

**Save** (`Engine/Project.cpp:549-708`):
1. Write to temp file first (crash safety)
2. Create `ProjectSerialization` and call `initialize()`
3. Write via `boost::archive::xml_oarchive`
4. Rotate backups (up to `saveVersions` count)
5. Atomically rename temp file to final path

**Load** (`Engine/Project.cpp:202-405`):
1. Reset project state
2. Check for auto-save files
3. Deserialize `ProjectSerialization` (version-aware)
4. Three-pass restoration:
   - Pass 1: Create all nodes, restore knob values
   - Pass 2: Reconnect nodes using `Inputs_map`
   - Pass 3: Restore master/slave links and expressions
5. Compute clip preferences on all trees
6. Load GUI state (if not background mode)
7. Run `onProjectLoad` Python callback

### 3.11 Implications for Flux JSON Format

Key takeaways for designing Flux's JSON serialization:

1. **Knob type mapping**: Each knob has a `typeName()` string (`"Double"`, `"Int"`, `"Bool"`, `"String"`, `"Choice"`, `"Color"`, `"Parametric"`, `"File"`, `"OutputFile"`, `"Path"`, `"Group"`, `"Page"`, `"Separator"`, `"Button"`, `"Table"`, `"Layers"`)
2. **Per-dimension values**: Animation, expressions, and master links are per-dimension
3. **Three-pass restoration**: Nodes → connections → links/expressions
4. **Version field**: Essential for backward compatibility
5. **Environment variable expansion**: Needed for portable file paths
6. **Extra type data**: Choice entries, min/max ranges, file sequence flags must be serialized alongside values

---

## 4. Settings System

### 4.1 Architecture

The `Settings` class (`Engine/Settings.h:48-651`) extends `KnobHolder` and is process-wide:

```cpp
Settings::Settings()
    : KnobHolder( AppInstancePtr() ) // Settings are process wide
```

Every setting is a **Knob** — the same parameter abstraction used for node parameters.

### 4.2 Settings Categories (14 Pages)

Initialized via `initializeKnobs()` at `Engine/Settings.cpp:113-136`:

| Page | Key Parameters |
|---|---|
| **General** | checkForUpdates, autoSaveDelay, saveVersions, hostName |
| **Threading** | numberOfThreads, parallelRenders, useThreadPool |
| **Rendering** | convertNaN, copyInputImage, rgbSupport, transformConcatenation |
| **GPU** | openglRenderer, nOpenGLContexts, enableOpenGL |
| **Project Setup** | autoProjectFormat, autoPreview, fixPaths |
| **Documentation** | webserverPort, documentationSource |
| **User Interface** | notifyOnFileChange, renderOnEditingFinished, linearPickers, maxPanels |
| **Color Management** | ocioConfig, customOcioConfigFile, warnOCIOChanged |
| **Caching** | aggressiveCaching, maxRAMPercent, unreachableRAMPercent, diskCachePath |
| **Viewers** | texturesBitDepth, tiling, checkerboard, autoWipe, autoProxy |
| **Nodegraph** | autoScroll, autoTurbo, snapToNode, maxUndoRedo |
| **Plugins** | extraPluginPaths, useBundledPlugins, pyPlugsSearchPath |
| **Python** | afterProjectCreated, afterProjectLoaded, beforeProjectSave, beforeProjectClose, afterNodeCreated, beforeNodeRemoval callbacks |
| **Appearance** | Font, fontSize, stylesheet, all UI colors |

### 4.3 Persistence

Settings are persisted via Qt's `QSettings` (`Engine/Settings.cpp:1927-2064`):
- Each knob is dynamically cast to its type for serialization
- For `KnobChoice`, the **choice name string** is serialized (not the index), making settings resilient to option reordering
- Multi-dimensional knobs store each dimension as `name.dimensionName`

### 4.4 Change Notification

`onKnobValueChanged()` at `Engine/Settings.cpp:2270-2423`:
- Emits `settingChanged(KnobI*)` signal
- Updates runtime state (thread counts, cache sizes, OCIO config, thread pool)
- Shows warnings for settings requiring restart
- Reloads stylesheets when colors change

### 4.5 OCIO Integration

The color management settings (`Engine/Settings.cpp:644-701`) handle OpenColorIO config selection with a custom config file option. `tryLoadOpenColorIOConfig()` (`Engine/Settings.cpp:2169-2254`) sets the `OCIO` environment variable.

---

## 5. Python Bindings

### 5.1 Architecture

All Python-exposed types live in `NATRON_PYTHON_NAMESPACE` (separate from `NATRON_NAMESPACE`). The Python API uses a **thin wrapper pattern** — C++ classes that hold weak pointers to internal engine objects.

### 5.2 Wrapper Class Hierarchy

```
Group                          (PyNodeGroup.h)
  └── App : Group              (PyAppInstance.h)

UserParamHolder                (PyNode.h)
  └── Effect : Group, UserParamHolder   (PyNode.h)

Param                          (PyParameter.h)
  ├── AnimatedParam : Param
  │   ├── IntParam, Int2DParam, Int3DParam
  │   ├── DoubleParam, Double2DParam, Double3DParam
  │   ├── ColorParam
  │   ├── ChoiceParam
  │   ├── BooleanParam
  │   └── StringParamBase → StringParam, FileParam, OutputFileParam, PathParam
  ├── ButtonParam
  ├── SeparatorParam
  ├── GroupParam
  ├── PageParam
  └── ParametricParam

PyCoreApplication              (PyGlobalFunctions.h)
AppSettings                    (PyAppInstance.h)
```

### 5.3 PyCoreApplication (`Engine/PyGlobalFunctions.h:45-233`)

Top-level Python entry point:

| Method | Returns | Description |
|---|---|---|
| `getPluginIDs()` | `QStringList` | All available plugin IDs |
| `getPluginIDs(filter)` | `QStringList` | Filtered plugin IDs |
| `getNumInstances()` | `int` | Number of open project instances |
| `getNatronPath()` | `QStringList` | Natron search paths |
| `appendToNatronPath(path)` | `void` | Add to search paths |
| `isLinux/isWindows/isMacOSX/isUnix()` | `bool` | Platform detection |
| `getNatronVersionString()` | `QString` | e.g. "2.6.0" |
| `isBackground()` | `bool` | Running in render mode |
| `getNumCpus()` | `int` | Hardware thread count |
| `getInstance(idx)` | `App*` | Get project instance by index |
| `getActiveInstance()` | `App*` | Get top-level instance |
| `getSettings()` | `AppSettings*` | Access application settings |

### 5.4 AppSettings (`Engine/PyAppInstance.h:51-67`)

| Method | Description |
|---|---|
| `getParam(scriptName)` | Get a specific setting parameter by name |
| `getParams()` | Get all setting parameters |
| `saveSettings()` | Persist settings to disk |
| `restoreDefaultSettings()` | Reset all settings to defaults |

### 5.5 App (`Engine/PyAppInstance.h:258-334`)

Per-project Python API:

| Method | Description |
|---|---|
| `createNode(pluginID, ...)` | Create a new node by plugin ID |
| `createReader(filename)` | Create a reader node |
| `createWriter(filename)` | Create a writer node |
| `timelineGetTime()` | Current timeline position |
| `timelineGetLeftBound/RightBound()` | Timeline bounds |
| `render(writeNode, first, last)` | Render frames |
| `getProjectParam(name)` | Get project-level parameter |
| `saveProject/saveProjectAs/loadProject` | Project I/O |
| `writeToScriptEditor(msg)` | Output to script editor |

### 5.6 Effect (`Engine/PyNode.h:218-390`)

Full node interaction API:

| Category | Methods |
|---|---|
| **Connections** | `connectInput`, `disconnectInput`, `getInput`, `getMaxInputCount` |
| **Identity** | `getScriptName`, `setScriptName`, `getLabel`, `setLabel`, `getPluginID` |
| **Parameters** | `getParams()`, `getParam(name)` |
| **Evaluation** | `beginChanges()`, `endChanges()` |
| **Positioning** | `setPosition`, `getPosition`, `setSize`, `getSize` |
| **User Params** | `getUserPageParam()`, full param creation via `UserParamHolder` |

### 5.7 Param System (`Engine/PyParameter.h`)

**Base `Param`** (lines 43-199): Common operations:
- `getScriptName()`, `getLabel()`, `getTypeName()`, `getHelp()`
- `getIsVisible()`, `setVisible()`, `getIsEnabled()`, `setEnabled()`
- `copy()`, `slaveTo()`, `unslave()`, `setAsAlias()`

**AnimatedParam** (lines 202-268): Animation support:
- `getIsAnimated()`, `getNumKeys()`, `getKeyIndex()`, `getKeyTime()`
- `deleteValueAtTime()`, `removeAnimation()`
- `setExpression()`, `getExpression()`

**Typed Params**: Each provides `get()`, `set()`, `getValue()`, `setValue()`, `getValueAtTime()`, `setValueAtTime()`, `setDefaultValue()`, `getDefaultValue()`, `restoreDefaultValue()`, `setMinimum()`, `setMaximum()`:

| Param Type | Dimensions | Return Type |
|---|---|---|
| `IntParam` | 1 | `int` |
| `Int2DParam` | 2 | `Int2DTuple {x,y}` |
| `Int3DParam` | 3 | `Int3DTuple {x,y,z}` |
| `DoubleParam` | 1 | `double` |
| `Double2DParam` | 2 | `Double2DTuple {x,y}` |
| `Double3DParam` | 3 | `Double3DTuple {x,y,z}` |
| `ColorParam` | 4 | `ColorTuple {r,g,b,a}` |
| `BooleanParam` | 1 | `bool` |
| `ChoiceParam` | 1 | `int` (index) + `getOptions()` |
| `StringParamBase` | 1 | `QString` |
| `ButtonParam` | 0 | `trigger()` |
| `ParametricParam` | N | Curve control points |

### 5.8 Python → Settings Access Path

```
Python script
  → natron.getSettings()           # returns AppSettings
    → AppSettings.getParam(name)   # returns Param subclass
      → Param.get/setValue()       # reads/writes the underlying Knob
        → Settings.onKnobValueChanged()  # triggers side effects
          → QSettings persistence          # auto-saves if enabled
```

Every setting knob is accessible from Python by its `setName()` identifier (e.g., `"noRenderThreads"`, `"maxRAMPercent"`, `"ocioConfig"`).

---

## 6. Key Source File Index

| File | Lines | Purpose |
|---|---|---|
| `Engine/Knob.h` | 2892 | Base classes: `KnobI`, `KnobHelper`, `Knob<T>`, `KnobHolder`, signal handler |
| `Engine/Knob.cpp` | — | Knob base implementation |
| `Engine/KnobImpl.h` | — | Template implementations for `Knob<T>` |
| `Engine/KnobTypes.h` | 1215 | All concrete knob types |
| `Engine/KnobTypes.cpp` | — | Concrete knob type implementations |
| `Engine/KnobFile.h/cpp` | — | `KnobFile`, `KnobOutputFile`, `KnobPath` |
| `Engine/KnobFactory.h/cpp` | — | Factory for creating knobs by type name |
| `Engine/Curve.h/cpp` | 403 | `KeyFrame`, `Curve` classes |
| `Engine/CurvePrivate.h` | — | Curve internal data |
| `Engine/CurveSerialization.h` | — | Curve XML serialization |
| `Engine/Interpolation.cpp` | — | Hermite spline interpolation algorithms |
| `Engine/StringAnimationManager.h` | — | String keyframe animation |
| `Engine/KnobSerialization.h` | 1226 | Knob/value/master serialization |
| `Engine/Project.h/cpp` | — | Project save/load workflow |
| `Engine/ProjectSerialization.h` | — | Project-level serialization |
| `Engine/NodeSerialization.h/cpp` | — | Node-level serialization |
| `Engine/NodeGroupSerialization.cpp` | — | Node group restoration |
| `Engine/Settings.h/cpp` | 655 | Application settings (knob-based) |
| `Engine/PyGlobalFunctions.h` | 238 | Top-level Python API |
| `Engine/PyAppInstance.h` | — | `App`, `AppSettings` Python wrappers |
| `Engine/PyNode.h` | — | `Effect` Python wrapper |
| `Engine/PyParameter.h` | — | All `Param` Python wrappers |

---

## 7. Design Patterns Summary

1. **Unified Parameter Model**: The same `Knob` abstraction is used for node parameters, project settings, and application preferences. This means animation, expressions, and slaving work everywhere.

2. **Type-Parameterized Base**: `Knob<T>` is templated on `int`, `double`, `bool`, `std::string`. Concrete types add QObject for signals and type-specific behavior.

3. **Factory Pattern**: `KnobFactory` registers 15 built-in types and creates knobs by type name string, enabling plugin-driven parameter creation.

4. **Dual Curve System**: "Internal curves" (rendering) and "GUI curves" (interactive editing) are kept separate and synced after rendering completes.

5. **Three-Pass Restoration**: Nodes are created first, connections restored second, master/slave links and expressions restored third — all deferred until all nodes exist.

6. **Atomic Saves**: Write to temp file + rename prevents corruption on crash.

7. **Expression Caching**: Python expression results are cached per-frame and only cleared when curves change, avoiding redundant Python evaluations during rendering.

8. **Thread Safety**: GUI values and render values are stored separately, with mutex protection for cross-thread access.
