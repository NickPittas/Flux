# Locator Report: Animator Knob Capture Surface

**Date**: 2026-05-26
**Task**: Locate exact knob capture surface for FluxTextAnimator serialization
**Root**: /home/npittas/Flux

---

## 1. Animator Knob Creation — `ensureAnimatorKnobs()`

### File: `Gui/FluxTextAnimatorModel.cpp` lines ~197–247

The function creates all `fta_*` knobs for a single animator ID. Each knob is created via `KnobHolder::createXxxKnob()` on the nodegroup's `EffectInstance`.

**Knob naming convention**: `fta_{animatorId}_{suffix}` (prefix `fta_`)

Helper `knobName()` at line ~175:
```cpp
QString knobName(int animatorId, const QString& suffix)
{
    return QString::fromUtf8("%1%2_%3").arg(QString::fromUtf8(kPrefix)).arg(animatorId).arg(suffix);
}
```

**Complete knob set per animator** (from `ensureAnimatorKnobs`, lines ~197–247):

| Knob Name (`fta_{id}_`) | Type | C++ Creator | Dimensions | Animatable | Default |
|---|---|---|---|---|---|
| `enabled` | `KnobBool` | `createBoolKnob` | 1 | No | `true` |
| `name` | `KnobString` | `createStringKnob` | 1 | No | label param |
| `basedOn` | `KnobChoice` | `createChoiceKnob` | 1 | No | `0` (chars) |
| `shape` | `KnobChoice` | `createChoiceKnob` | 1 | No | `1` (linear) |
| `anchor` | `KnobChoice` | `createChoiceKnob` | 1 | No | `1` (center) |
| `start` | `KnobDouble` | `createDoubleKnob` | 1 | Yes | `0.0` |
| `end` | `KnobDouble` | `createDoubleKnob` | 1 | Yes | `100.0` |
| `offset` | `KnobDouble` | `createDoubleKnob` | 1 | Yes | `0.0` |
| `amount` | `KnobDouble` | `createDoubleKnob` | 1 | Yes | `100.0` |
| `rotation` | `KnobDouble` | `createDoubleKnob` | 1 | Yes | `0.0` |
| `opacity` | `KnobDouble` | `createDoubleKnob` | 1 | Yes | `100.0` |
| `tracking` | `KnobDouble` | `createDoubleKnob` | 1 | Yes | `0.0` |
| `position` | `KnobDouble` | `createDoubleKnob` | 2 | Yes | `(0.0, 0.0)` |
| `scale` | `KnobDouble` | `createDoubleKnob` | 2 | Yes | `(100.0, 100.0)` |
| `scaleSeparated` | `KnobBool` | `createBoolKnob` (via `ensureBoolKnob`) | 1 | No | `false` |
| `fillColor` | `KnobColor` | `createColorKnob` | 4 | Yes | `(0.1, 0.65, 1.0, 1.0)` |

All knobs are created as **user knobs** (default `isUserKnob = true` for `createXxxKnob()`). After creation, `h->recreateUserKnobs(true)` is called (line ~247).

**Critical**: These knobs are created with `setupKnob()` which calls:
```cpp
k->setIsPersistent(true);   // marks for serialization (but doesn't help—see §6)
k->setAnimationEnabled(animated);
k->setSecret(false);
```

---

## 2. Reading Knob Values and Keyframe Curves

### NodePtr → Knob lookup

**File**: `Engine/Node.h` line 760
```cpp
KnobIPtr getKnobByName(const std::string & name) const;
```

### Helper in `FluxTextAnimatorModel.cpp` (anonymous namespace, line ~24):
```cpp
KnobIPtr knob(const NodePtr& node, const QString& name)
{
    return node ? node->getKnobByName(name.toStdString()) : KnobIPtr();
}
```

### Reading current value

**For `KnobDoubleBase`** (covers `KnobDouble` and `KnobColor`):
```cpp
KnobDoubleBasePtr dk = std::dynamic_pointer_cast<KnobDoubleBase>(knob(node, name));
double val = dk->getValue(dim, ViewSpec::current());
```

**For `KnobIntBase`**:
```cpp
KnobIntBasePtr ik = std::dynamic_pointer_cast<KnobIntBase>(knob(node, name));
int val = ik->getValue(dim, ViewSpec::current());
```

**For `KnobBool`**:
```cpp
KnobBoolPtr k = std::dynamic_pointer_cast<KnobBool>(knob(node, name));
bool val = k->getValue(0, ViewSpec::current());
```

**For `KnobStringBase`**:
```cpp
KnobStringBasePtr k = std::dynamic_pointer_cast<KnobStringBase>(knob(node, name));
std::string val = k->getValue(0, ViewSpec::current());
```

### Reading keyframe curves

**File**: `Engine/Knob.h` line 771 (virtual) / line 1473 (KnobHelper override)
```cpp
virtual std::shared_ptr<Curve> getCurve(ViewSpec view, int dimension, bool byPassMaster = false) const;
```

**File**: `Engine/Curve.h` line 258
```cpp
KeyFrameSet getKeyFrames_mt_safe() const;   // returns std::set<KeyFrame, KeyFrame_compare_time>
```

**File**: `Engine/Curve.h` line 50 — `KeyFrame` class:
```cpp
class KeyFrame {
    double getTime() const;
    double getValue() const;
    double getLeftDerivative() const;
    double getRightDerivative() const;
    KeyframeTypeEnum getInterpolation() const;
};
```

### Existing helper: `keysForKnob()` (anonymous namespace, line ~105):
```cpp
QJsonArray keysForKnob(const KnobIPtr& k, int dim)
{
    QJsonArray keys;
    if (!k) return keys;
    CurvePtr curve = k->getCurve(ViewSpec::current(), dim);
    if (!curve) return keys;
    KeyFrameSet keyFrames = curve->getKeyFrames_mt_safe();
    for (const KeyFrame& key : keyFrames) {
        QJsonObject obj;
        obj["t"] = key.getTime();
        obj["v"] = key.getValue();
        keys.append(obj);
    }
    return keys;
}
```

**Note**: This existing helper only captures `t` and `v`. For full restoration, also capture `getLeftDerivative()`, `getRightDerivative()`, and `getInterpolation()`.

### Existing helper: `numericParam()` (anonymous namespace, line ~121):
```cpp
QJsonObject numericParam(const NodePtr& node, const QString& name, int dim, double fallback)
{
    QJsonObject obj;
    KnobDoubleBasePtr dk = std::dynamic_pointer_cast<KnobDoubleBase>(knob(node, name));
    KnobIntBasePtr ik = std::dynamic_pointer_cast<KnobIntBase>(knob(node, name));
    if (dk) {
        obj["v"] = dk->getValue(dim, ViewSpec::current());
        obj["keys"] = keysForKnob(dk, dim);
    } else if (ik) {
        obj["v"] = ik->getValue(dim, ViewSpec::current());
        obj["keys"] = keysForKnob(ik, dim);
    } else {
        obj["v"] = fallback;
        obj["keys"] = QJsonArray();
    }
    return obj;
}
```

---

## 3. Writing/Restoring Knob Values and Keyframe Curves

### Setting current value

**For `KnobDoubleBase`**:
```cpp
k->setValue(v, ViewSpec::all(), dim, eValueChangedReasonPluginEdited, 0);
```
See `setDoubleDefault()` at line ~47 and `setColorDefault()` at line ~55.

**For `KnobBool`**:
```cpp
k->setValue(fallback, ViewSpec::all(), 0, eValueChangedReasonPluginEdited, 0);
```

**For `KnobStringBase`**:
```cpp
k->setValue(value.toStdString(), ViewSpec::all(), 0, eValueChangedReasonNatronGuiEdited, 0);
```

**For `KnobIntBase`**:
```cpp
k->setValue(value, ViewSpec::all(), 0, eValueChangedReasonNatronGuiEdited, 0);
```

### Setting keyframes

**File**: `Engine/Knob.h` lines 1856–1863:
```cpp
ValueChangedReturnCodeEnum setValueAtTime(double time,
                                          const T & v,
                                          ViewSpec view,
                                          int dimension,
                                          Natron::ValueChangedReasonEnum reason,
                                          KeyFrame* newKey,
                                          bool hasChanged = false);
```

**Example usage** (existing in `syncAnimatorTimeDependency()`, line ~225):
```cpp
dependency->setValueAtTime(time, static_cast<double>(index++), ViewSpec::all(), 0,
                           eValueChangedReasonNatronGuiEdited, &key);
```

### Restoring full keyframe curve (pattern to implement):
```cpp
// 1. Clear existing animation
k->removeAnimation(ViewSpec::all(), dim);   // Knob.h line 618

// 2. Set the static default value
k->setValue(defaultVal, ViewSpec::all(), dim, eValueChangedReasonPluginEdited, 0);

// 3. Add each keyframe
KeyFrame kf;
for (const auto& savedKey : savedKeys) {
    k->setValueAtTime(savedKey.time, savedKey.value, ViewSpec::all(), dim,
                      eValueChangedReasonNatronInternalEdited, &kf);
    // Optionally restore derivatives/interpolation:
    // kf.setInterpolation(savedKey.interpolation);  // but need to set on curve
}
```

For setting interpolation on restored keyframes, use `Curve::setKeyFrameInterpolation()` or reconstruct the `KeyFrame` with full constructor:
```cpp
KeyFrame(double time, double initialValue, double leftDerivative, double rightDerivative,
         KeyframeTypeEnum interpolation);
```

---

## 4. `animatorOrder` and `animatorStackJson` — PyPlug Knobs (DO Survive)

### File: `plugins/FluxMotionText.py` lines 217–263

```python
def _add_animator_storage_params(group, page):
    next_id_param = group.createIntParam("animatorNextId", "Next Animator ID")
    next_id_param.setDefaultValue(1, 0)
    next_id_param.restoreDefaultValue(0)
    _finalize(page, group, next_id_param, "animatorNextId", hidden=True)

    order_param = group.createStringParam("animatorOrder", "Animator Order")
    _finalize(page, group, order_param, "animatorOrder", hidden=True)

    stack_param = group.createStringParam("animatorStackJson", "Animator Stack JSON")
    _finalize(page, group, stack_param, "animatorStackJson", hidden=True)

    time_dependency_param = group.createDoubleParam("animatorTimeDependency", "Animator Time Dependency")
    _finalize(page, group, time_dependency_param, "animatorTimeDependency", anim=True, hidden=True)
```

These are **PyPlug-declared knobs** (not user knobs). They are serialized by Natron's native node serialization because:
- They are created by the PyPlug `createInstance()` → `isUserKnob() = false`
- They have `setPersistent(True)` (via `_finalize`)
- Natron's `NodeSerialization::initialize()` (line 85) only skips `isUserKnob() == true`

### How they're read by C++:

**`animatorOrder`**: Read in `animatorIds()` (line ~189):
```cpp
QString order = stringValue(node, "animatorOrder", QString());
// Parses "1,2,3" format
```

Written in `addAnimator()`, `removeAnimator()`, `moveAnimator()`:
```cpp
setString(node, "animatorOrder", orderString(ids));   // "1,2,3"
```

**`animatorStackJson`**: Written in `syncAnimatorStackToRenderer()` (line ~299):
```cpp
setString(node, "animatorStackJson", json);
```

This JSON **already contains all values** (and keys!) via `numericParam()`/`vecParam()` helpers. BUT on reload, the `fta_*` knobs are missing, so `syncAnimatorStackToRenderer` reads defaults and overwrites the JSON with garbage.

**`animatorNextId`**: Read/written as int:
```cpp
int nextId = intValue(node, "animatorNextId", 1);
setInt(node, "animatorNextId", nextId + 1);
```

### Alias chain:
```python
# FluxMotionText.py line 280
group.getParam("animatorStackJson").setAsAlias(text_node.getParam("animatorStackJson"))
group.getParam("animatorTimeDependency").setAsAlias(text_node.getParam("animatorTimeDependency"))
```

---

## 5. Serialization Structs in `FluxTimelineSerialization.h`

### File: `Gui/FluxTimelineSerialization.h`

#### `FluxEffectSerialization` (lines 26–48)
```cpp
struct FluxEffectSerialization {
    std::string pluginId;
    std::string label;
    std::string nodeScriptName;
    bool enabled;
    // Boost serialize() with make_nvp()
};
```

#### `FluxMaskSerialization` (lines 50–72)
```cpp
struct FluxMaskSerialization {
    std::string name, type, pluginId;
    bool enabled, inverted;
    int effectIndex;
    std::string maskNodeScriptName, reformatNodeScriptName;
    // Boost serialize()
};
```

#### `FluxLayerSerialization` (lines 74–195)
Contains identity, timing, solid color, parenting, node script names, child `effects[]`, `masks[]`. **No animator data.**

#### `FluxTimelineSerialization` (lines 197–240)
Contains `layers[]`, `bgReformatNodeScriptName`, `selectedLayer`, `showKeyframeCurves`, `ungroupedKeyframeProperties`.

#### Version macros (bottom of file):
```cpp
BOOST_CLASS_VERSION(NATRON_NAMESPACE::FluxTimelineSerialization, 2)
BOOST_CLASS_VERSION(NATRON_NAMESPACE::FluxLayerSerialization, 2)
BOOST_CLASS_VERSION(NATRON_NAMESPACE::FluxMaskSerialization, 1)
```

### Pattern for adding animator serialization:

Add a new struct `FluxAnimatorSerialization` following the `FluxEffectSerialization` pattern, with:
- `int animatorId`
- `std::string name`
- `bool enabled`
- `int basedOn, shape, anchor`
- Per-property data: value per dim + keyframe set (time, value, interpolation, derivatives)

Then add `std::vector<FluxAnimatorSerialization> animators` to `FluxLayerSerialization` and bump version to 3.

---

## 6. How Effects Are Serialized/Deserialized — Reference Pattern

### File: `Gui/FluxTimeline.cpp`

**Serialize** (`serializeForProject()`, lines ~4390–4405):
```cpp
for (int e = 0; e < layer.effects.size(); ++e) {
    const FluxEffect& effect = layer.effects[e];
    FluxEffectSerialization effectSer;
    effectSer.pluginId = effect.pluginId.toStdString();
    effectSer.label = effect.label.toStdString();
    effectSer.enabled = effect.enabled;
    if (effect.node) {
        effectSer.nodeScriptName = effect.node->getFullyQualifiedName();
    }
    layerSer.effects.push_back(effectSer);
}
```

**Deserialize** (`restoreFromProjectSerialization()`, lines ~4502–4518):
```cpp
for (size_t e = 0; e < layerSer.effects.size(); ++e) {
    const FluxEffectSerialization& effectSer = layerSer.effects[e];
    FluxEffect effect;
    effect.pluginId = QString::fromStdString(effectSer.pluginId);
    effect.label = QString::fromStdString(effectSer.label);
    effect.enabled = effectSer.enabled;
    if (!effectSer.nodeScriptName.empty()) {
        effect.node = project->getNodeByFullySpecifiedName(effectSer.nodeScriptName);
        if (!effect.node) continue;  // skip if node gone
    }
    layer.effects.append(effect);
}
```

### Why effects "just work":
Effects reference **Natron nodes by script name**. The nodes themselves are serialized by Natron's engine-level `NodeSerialization` which captures all non-user knobs. So Flux only needs to store the node reference and Natron handles the knob data.

### Why animators DON'T work:
The `fta_*` knobs are **user knobs** created at runtime. `NodeSerialization::initialize()` at line 85 explicitly skips user knobs:
```cpp
if (knobs[i]->isUserKnob()) {
    continue;  // fta_* knobs land here
}
```
User knobs are only captured if they're children of a user page (lines 81–83 + 124):
```cpp
if (isPage && knobs[i]->isUserKnob()) {
    userPages.push_back(knobs[i]);  // only pages, not individual knobs
}
```
The `fta_*` knobs are never parented under a page → never captured → lost.

---

## 7. The NodePtr API for Knob Access

### Lookup
```cpp
// Engine/Node.h:760
KnobIPtr getKnobByName(const std::string & name) const;

// Engine/Node.h (inherited)
const std::vector<KnobIPtr>& getKnobs() const;  // all knobs on node
```

### Value read
```cpp
// Engine/Knob.h:1826 (template on Knob<T>)
T getValue(int dimension = 0, ViewSpec view = ViewSpec::current(), bool clampToMinMax = true);
```

### Value write
```cpp
// Engine/Knob.h:1871
ValueChangedReturnCodeEnum setValue(const T & v, ViewSpec view, int dimension,
                                    Natron::ValueChangedReasonEnum reason, KeyFrame* newKey,
                                    bool hasChanged = false);
```

### Keyframe write
```cpp
// Engine/Knob.h:1856
ValueChangedReturnCodeEnum setValueAtTime(double time, const T & v, ViewSpec view, int dimension,
                                          Natron::ValueChangedReasonEnum reason, KeyFrame* newKey,
                                          bool hasChanged = false);
```

### Curve/keyframe read
```cpp
// Engine/Knob.h:771
std::shared_ptr<Curve> getCurve(ViewSpec view, int dimension, bool byPassMaster = false) const;

// Engine/Curve.h:258
KeyFrameSet getKeyFrames_mt_safe() const;

// Engine/Knob.h:448
virtual bool canAnimate() const = 0;

// Engine/Knob.h:830
virtual int getDimension() const = 0;
```

### Animation clear
```cpp
// Engine/Knob.h:618
void removeAnimation(ViewSpec view, int dimension);
```

### KeyFrame properties (Engine/Curve.h:50–100)
```cpp
double getTime() const;
double getValue() const;
double getLeftDerivative() const;
double getRightDerivative() const;
KeyframeTypeEnum getInterpolation() const;

void setValue(double v);
void setTime(double time);
void setInterpolation(KeyframeTypeEnum interp);
void setLeftDerivative(double v);
void setRightDerivative(double v);
```

### Knob type casting
```cpp
// All from Engine/KnobTypes.h
std::dynamic_pointer_cast<KnobDoubleBase>(knob)   // covers KnobDouble + KnobColor
std::dynamic_pointer_cast<KnobDouble>(knob)
std::dynamic_pointer_cast<KnobColor>(knob)
std::dynamic_pointer_cast<KnobBool>(knob)
std::dynamic_pointer_cast<KnobIntBase>(knob)       // covers KnobInt
std::dynamic_pointer_cast<KnobStringBase>(knob)    // covers KnobString
std::dynamic_pointer_cast<KnobChoice>(knob)
```

---

## 8. Serialization Invocation Points

### Save path:
```
Gui/ProjectGuiSerialization.cpp:187
  → timeline->serializeForProject()    // returns FluxTimelineSerialization
  → stored in _fluxTimeline member
  → serialized to .ntp via Boost XML
```

### Load path:
```
Gui/ProjectGui.cpp:587
  → timeline->restoreFromProjectSerialization(fluxSer, _gui)
  → resolves node script names to NodePtr via project->getNodeByFullySpecifiedName()
  → calls gui->rebuildCompositingGraph(timeline)
```

---

## 9. Summary of Exact Modification Points

| File | Lines | What to Change |
|---|---|---|
| `Gui/FluxTimelineSerialization.h` | After L72 | Add `struct FluxAnimatorSerialization` with Boost serialize |
| `Gui/FluxTimelineSerialization.h` | ~L161 | Add `std::vector<FluxAnimatorSerialization> animators` to `FluxLayerSerialization` |
| `Gui/FluxTimelineSerialization.h` | ~L195 | In `FluxLayerSerialization::serialize()`, serialize animators for `version >= 3` |
| `Gui/FluxTimelineSerialization.h` | Bottom | `BOOST_CLASS_VERSION(FluxLayerSerialization, 3)` |
| `Gui/FluxTimeline.cpp` | ~L4405 (after masks) | In `serializeForProject()`: for text layers, call `FluxTextAnimatorModel::captureAnimators()` |
| `Gui/FluxTimeline.cpp` | ~L4560 (after mask restore) | In `restoreFromProjectSerialization()`: restore animator knobs from serialized data |
| `Gui/FluxTextAnimatorModel.h` | After L43 | Add `captureAnimators()` and `restoreAnimators()` function declarations |
| `Gui/FluxTextAnimatorModel.cpp` | After L475 | Implement capture (read fta_* knob values + curves) and restore (recreate knobs, set values, add keyframes) |

---

## Confidence: HIGH (95%)

Root cause confirmed by direct source tracing:
- `fta_*` knobs are user knobs → skipped by `NodeSerialization::initialize()` L85
- Not parented under a page → not captured by `GroupKnobSerialization`
- `animatorOrder`/`animatorStackJson` survive (PyPlug knobs, `isUserKnob()=false`)
- `syncAnimatorStackToRenderer` runs with missing knobs on reload → overwrites JSON with defaults
