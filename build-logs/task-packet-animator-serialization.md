# Task Packet: FluxAnimatorSerialization — Save/Load for Text Animator Keyframes

**Date**: 2026-05-26
**Mode**: general-coding
**Confidence**: HIGH (95%)
**Root Cause**: Confirmed — `fta_*` user knobs are orphans (no page parent), so Natron's `NodeSerialization::initialize()` skips them at line 85 (`if (knobs[i]->isUserKnob()) { continue; }`). The `animatorOrder`/`animatorStackJson` PyPlug knobs DO survive, but on reload `syncAnimatorStackToRenderer` reads missing `fta_*` knobs → writes defaults → overwrites the JSON with garbage.

---

## User Goal

Text animator keyframes and settings (from FluxMotionText / T080/T081 work) must survive save and reload of the `.ntp` project file.

---

## Relevant Locations

### Serialization structs and version macros
| File | Lines | Symbol | Purpose |
|---|---|---|---|
| `Gui/FluxTimelineSerialization.h` | L26–L48 | `struct FluxEffectSerialization` | Reference pattern (simple struct + Boost serialize) |
| `Gui/FluxTimelineSerialization.h` | L50–L72 | `struct FluxMaskSerialization` | Reference pattern |
| `Gui/FluxTimelineSerialization.h` | L74–L195 | `struct FluxLayerSerialization` | **TARGET**: add `animators` vector, bump version |
| `Gui/FluxTimelineSerialization.h` | L243 (bottom) | `BOOST_CLASS_VERSION(FluxLayerSerialization, 2)` | **TARGET**: bump to 3 |

### Serialize/restore entry points
| File | Lines | Symbol | Purpose |
|---|---|---|---|
| `Gui/FluxTimeline.cpp` | L4328–L4430 | `FluxTimeline::serializeForProject()` | **TARGET**: add animator capture for text layers |
| `Gui/FluxTimeline.cpp` | L4430–L4570 | `FluxTimeline::restoreFromProjectSerialization()` | **TARGET**: add animator restoration for text layers |

### Animator model and helpers
| File | Lines | Symbol | Purpose |
|---|---|---|---|
| `Gui/FluxTextAnimatorModel.h` | L25–L28 | `struct FluxTextAnimatorSummary` | In-memory animator summary |
| `Gui/FluxTextAnimatorModel.h` | L30–L43 | `namespace FluxTextAnimatorModel` | Public API declarations |
| `Gui/FluxTextAnimatorModel.cpp` | L55–L56 | anonymous `knob()` | NodePtr→KnobIPtr lookup |
| `Gui/FluxTextAnimatorModel.cpp` | L105–L119 | `keysForKnob()` | Reads keyframes from a knob dim (captures t, v) |
| `Gui/FluxTextAnimatorModel.cpp` | L121–L136 | `numericParam()` | Reads value + keys for a single dim |
| `Gui/FluxTextAnimatorModel.cpp` | L138–L143 | `vecParam()` | Reads multi-dim values + keys |
| `Gui/FluxTextAnimatorModel.cpp` | L310–L345 | `ensureAnimatorKnobs()` | Creates all `fta_*` knobs for one animator |
| `Gui/FluxTextAnimatorModel.cpp` | L347–L375 | `ensureAnimatorCompatibility()` | Post-reload knob repair |
| `Gui/FluxTextAnimatorModel.cpp` | L399–L480 | `syncAnimatorStackToRenderer()` | Reads knobs → writes `animatorStackJson` |
| `Gui/FluxTextAnimatorModel.cpp` | L188–L202 | `animatorIds()` | Parses `animatorOrder` knob → QList<int> |

### Knob/value APIs (read-only reference, no edits needed)
| File | Symbol | Purpose |
|---|---|---|
| `Engine/Knob.h:618` | `removeAnimation(ViewSpec, int dim)` | Clear animation before restore |
| `Engine/Knob.h:1856` | `setValueAtTime(time, v, view, dim, reason, &key)` | Add keyframe |
| `Engine/Knob.h:1871` | `setValue(v, view, dim, reason, key)` | Set static value |
| `Engine/Curve.h:258` | `getKeyFrames_mt_safe()` | Read keyframe set |
| `Engine/Curve.h:50` | `KeyFrame` class | `getTime()`, `getValue()`, `getLeftDerivative()`, `getRightDerivative()`, `getInterpolation()` |
| `Engine/Node.h:760` | `getKnobByName(name)` | Lookup knob by name |

---

## Allowed Edit Files

1. **`Gui/FluxTimelineSerialization.h`** — Add `FluxAnimatorSerialization` struct + `FluxAnimatorKeyframeSerialization` struct, add `animators` member to `FluxLayerSerialization`, bump version.
2. **`Gui/FluxTextAnimatorModel.h`** — Add `captureAnimators()` and `restoreAnimators()` declarations.
3. **`Gui/FluxTextAnimatorModel.cpp`** — Implement `captureAnimators()` and `restoreAnimators()`.
4. **`Gui/FluxTimeline.cpp`** — Call `captureAnimators()`/`restoreAnimators()` at the right points.

## Read-Only Context Files

- `Gui/ProjectGuiSerialization.h` / `.cpp` — Embeds `FluxTimelineSerialization` in .ntp (no changes needed)
- `Engine/Curve.h` — KeyFrame API reference
- `Engine/Knob.h` / `Engine/KnobTypes.h` — Knob value/keyframe API reference
- `Engine/Node.h` — NodePtr API reference
- `plugins/FluxMotionText.py` — PyPlug knob declarations (animatorOrder etc.)

---

## Required Change

### 1. New struct `FluxAnimatorKeyframeSerialization` in `Gui/FluxTimelineSerialization.h`

Insert **after `FluxMaskSerialization`** (after line 72), before `FluxLayerSerialization`:

```cpp
struct FluxAnimatorKeyframeSerialization
{
    double time;
    double value;
    double leftDerivative;
    double rightDerivative;
    int interpolation; // KeyframeTypeEnum as int

    FluxAnimatorKeyframeSerialization()
        : time(0), value(0), leftDerivative(0), rightDerivative(0), interpolation(0)
    {}

    friend class ::boost::serialization::access;
    template<class Archive>
    void serialize(Archive & ar, const unsigned int /*version*/)
    {
        ar & ::boost::serialization::make_nvp("Time", time);
        ar & ::boost::serialization::make_nvp("Value", value);
        ar & ::boost::serialization::make_nvp("LeftDerivative", leftDerivative);
        ar & ::boost::serialization::make_nvp("RightDerivative", rightDerivative);
        ar & ::boost::serialization::make_nvp("Interpolation", interpolation);
    }
};
```

### 2. New struct `FluxAnimatorPropertySerialization` in `Gui/FluxTimelineSerialization.h`

One per knob dimension (for simplicity, or one per knob with N values/keys arrays):

```cpp
struct FluxAnimatorPropertySerialization
{
    double value;                              // current/static value
    std::vector<FluxAnimatorKeyframeSerialization> keyframes;  // empty if not animated

    FluxAnimatorPropertySerialization()
        : value(0)
    {}

    friend class ::boost::serialization::access;
    template<class Archive>
    void serialize(Archive & ar, const unsigned int /*version*/)
    {
        ar & ::boost::serialization::make_nvp("Value", value);
        int numKeys = (int)keyframes.size();
        ar & ::boost::serialization::make_nvp("NumKeys", numKeys);
        if (Archive::is_loading::value) {
            keyframes.resize(numKeys);
        }
        for (int i = 0; i < numKeys; ++i) {
            ar & ::boost::serialization::make_nvp("Key", keyframes[i]);
        }
    }
};
```

### 3. New struct `FluxAnimatorSerialization` in `Gui/FluxTimelineSerialization.h`

Analogous to `FluxEffectSerialization`. Captures all data for one animator:

```cpp
struct FluxAnimatorSerialization
{
    int animatorId;
    std::string name;
    bool enabled;
    int basedOn;
    int shape;
    int anchor;

    // Range selector (1-dim each)
    FluxAnimatorPropertySerialization start;
    FluxAnimatorPropertySerialization end;
    FluxAnimatorPropertySerialization offset;
    FluxAnimatorPropertySerialization amount;

    // Animated properties
    FluxAnimatorPropertySerialization rotation;       // 1-dim
    FluxAnimatorPropertySerialization opacity;         // 1-dim
    FluxAnimatorPropertySerialization tracking;        // 1-dim
    std::vector<FluxAnimatorPropertySerialization> position; // 2 dims
    std::vector<FluxAnimatorPropertySerialization> scale;    // 2 dims
    std::vector<FluxAnimatorPropertySerialization> fillColor; // 4 dims

    bool scaleSeparated;

    FluxAnimatorSerialization()
        : animatorId(0), enabled(true), basedOn(0), shape(1), anchor(1)
        , scaleSeparated(false)
    {}

    friend class ::boost::serialization::access;
    template<class Archive>
    void serialize(Archive & ar, const unsigned int /*version*/)
    {
        ar & ::boost::serialization::make_nvp("AnimatorId", animatorId);
        ar & ::boost::serialization::make_nvp("Name", name);
        ar & ::boost::serialization::make_nvp("Enabled", enabled);
        ar & ::boost::serialization::make_nvp("BasedOn", basedOn);
        ar & ::boost::serialization::make_nvp("Shape", shape);
        ar & ::boost::serialization::make_nvp("Anchor", anchor);
        ar & ::boost::serialization::make_nvp("Start", start);
        ar & ::boost::serialization::make_nvp("End", end);
        ar & ::boost::serialization::make_nvp("Offset", offset);
        ar & ::boost::serialization::make_nvp("Amount", amount);
        ar & ::boost::serialization::make_nvp("Rotation", rotation);
        ar & ::boost::serialization::make_nvp("Opacity", opacity);
        ar & ::boost::serialization::make_nvp("Tracking", tracking);

        // position: 2 dims
        int numPos = (int)position.size();
        ar & ::boost::serialization::make_nvp("NumPosition", numPos);
        if (Archive::is_loading::value) { position.resize(numPos); }
        for (int i = 0; i < numPos; ++i) {
            ar & ::boost::serialization::make_nvp("Pos", position[i]);
        }

        // scale: 2 dims
        int numScale = (int)scale.size();
        ar & ::boost::serialization::make_nvp("NumScale", numScale);
        if (Archive::is_loading::value) { scale.resize(numScale); }
        for (int i = 0; i < numScale; ++i) {
            ar & ::boost::serialization::make_nvp("Scl", scale[i]);
        }

        // fillColor: 4 dims
        int numFill = (int)fillColor.size();
        ar & ::boost::serialization::make_nvp("NumFillColor", numFill);
        if (Archive::is_loading::value) { fillColor.resize(numFill); }
        for (int i = 0; i < numFill; ++i) {
            ar & ::boost::serialization::make_nvp("Fill", fillColor[i]);
        }

        ar & ::boost::serialization::make_nvp("ScaleSeparated", scaleSeparated);
    }
};
```

### 4. Add `animators` member to `FluxLayerSerialization`

In `Gui/FluxTimelineSerialization.h`, inside `struct FluxLayerSerialization` (after `masks` member, ~line 161):

```cpp
// Text animator data (version 3+)
std::vector<FluxAnimatorSerialization> animators;
```

### 5. Add `animators` to `FluxLayerSerialization::serialize()`

Inside the `serialize()` template, after the `version >= 2` block (after line ~193):

```cpp
if (version >= 3) {
    int numAnimators = (int)animators.size();
    ar & ::boost::serialization::make_nvp("NumAnimators", numAnimators);
    if (Archive::is_loading::value) {
        animators.resize(numAnimators);
    }
    for (int i = 0; i < numAnimators; ++i) {
        ar & ::boost::serialization::make_nvp("Animator", animators[i]);
    }
}
```

### 6. Bump version macro

Change bottom of `Gui/FluxTimelineSerialization.h`:
```cpp
// From:
BOOST_CLASS_VERSION(NATRON_NAMESPACE::FluxLayerSerialization, 2)
// To:
BOOST_CLASS_VERSION(NATRON_NAMESPACE::FluxLayerSerialization, 3)
```

### 7. Add `captureAnimators()` / `restoreAnimators()` to `Gui/FluxTextAnimatorModel.h`

Inside `namespace FluxTextAnimatorModel` (after existing declarations):

```cpp
// Forward declarations needed:
struct FluxAnimatorSerialization;       // from FluxTimelineSerialization.h
struct FluxAnimatorPropertySerialization;

// Capture all animator data from a text layer's gizmoNode into serializable form
std::vector<FluxAnimatorSerialization> captureAnimators(const NodePtr& gizmoNode);

// Restore animator data from serialization onto a text layer's gizmoNode
void restoreAnimators(const NodePtr& gizmoNode, const std::vector<FluxAnimatorSerialization>& serialized);
```

### 8. Implement `captureAnimators()` in `Gui/FluxTextAnimatorModel.cpp`

The implementation reads the `fta_*` knobs for each animator and populates `FluxAnimatorSerialization`:

```cpp
// Helper: capture one dim of a numeric knob into FluxAnimatorPropertySerialization
namespace {
FluxAnimatorPropertySerialization captureProperty(const NodePtr& node, const QString& knobFullName, int dim)
{
    FluxAnimatorPropertySerialization prop;
    KnobDoubleBasePtr dk = std::dynamic_pointer_cast<KnobDoubleBase>(knob(node, knobFullName));
    if (dk) {
        prop.value = dk->getValue(dim, ViewSpec::current());
        CurvePtr curve = dk->getCurve(ViewSpec::current(), dim);
        if (curve) {
            KeyFrameSet kfSet = curve->getKeyFrames_mt_safe();
            for (const KeyFrame& kf : kfSet) {
                FluxAnimatorKeyframeSerialization ks;
                ks.time = kf.getTime();
                ks.value = kf.getValue();
                ks.leftDerivative = kf.getLeftDerivative();
                ks.rightDerivative = kf.getRightDerivative();
                ks.interpolation = static_cast<int>(kf.getInterpolation());
                prop.keyframes.push_back(ks);
            }
        }
    }
    return prop;
}

std::vector<FluxAnimatorPropertySerialization> captureVecProperty(const NodePtr& node, const QString& knobFullName, int dims)
{
    std::vector<FluxAnimatorPropertySerialization> result;
    for (int d = 0; d < dims; ++d) {
        result.push_back(captureProperty(node, knobFullName, d));
    }
    return result;
}
} // anonymous namespace

namespace FluxTextAnimatorModel {

std::vector<FluxAnimatorSerialization> captureAnimators(const NodePtr& node)
{
    std::vector<FluxAnimatorSerialization> result;
    if (!node) return result;

    QList<int> ids = animatorIds(node);
    for (int id : ids) {
        FluxAnimatorSerialization as;
        as.animatorId = id;
        as.name = stringValue(node, knobName(id, QString::fromUtf8("name")), QString::fromUtf8("Animator %1").arg(id)).toStdString();
        as.enabled = boolValue(node, knobName(id, QString::fromUtf8("enabled")), true);
        as.basedOn = intValue(node, knobName(id, QString::fromUtf8("basedOn")), 0);
        as.shape = intValue(node, knobName(id, QString::fromUtf8("shape")), 1);
        as.anchor = intValue(node, knobName(id, QString::fromUtf8("anchor")), 1);

        // Range selector (1-dim each)
        as.start = captureProperty(node, knobName(id, QString::fromUtf8("start")), 0);
        as.end = captureProperty(node, knobName(id, QString::fromUtf8("end")), 0);
        as.offset = captureProperty(node, knobName(id, QString::fromUtf8("offset")), 0);
        as.amount = captureProperty(node, knobName(id, QString::fromUtf8("amount")), 0);

        // Animated properties
        as.rotation = captureProperty(node, knobName(id, QString::fromUtf8("rotation")), 0);
        as.opacity = captureProperty(node, knobName(id, QString::fromUtf8("opacity")), 0);
        as.tracking = captureProperty(node, knobName(id, QString::fromUtf8("tracking")), 0);
        as.position = captureVecProperty(node, knobName(id, QString::fromUtf8("position")), 2);
        as.scale = captureVecProperty(node, knobName(id, QString::fromUtf8("scale")), 2);
        as.fillColor = captureVecProperty(node, knobName(id, QString::fromUtf8("fillColor")), 4);
        as.scaleSeparated = boolValue(node, knobName(id, QString::fromUtf8("scaleSeparated")), false);

        result.push_back(as);
    }
    return result;
}
```

### 9. Implement `restoreAnimators()` in `Gui/FluxTextAnimatorModel.cpp`

```cpp
namespace {
void restoreProperty(const NodePtr& node, const QString& knobFullName,
                     int dim, const FluxAnimatorPropertySerialization& prop)
{
    KnobDoubleBasePtr dk = std::dynamic_pointer_cast<KnobDoubleBase>(knob(node, knobFullName));
    if (!dk) return;

    // Clear any existing animation
    dk->removeAnimation(ViewSpec::all(), dim);

    // Set static value
    dk->setValue(prop.value, ViewSpec::all(), dim, eValueChangedReasonPluginEdited, 0);

    // Restore keyframes
    for (const FluxAnimatorKeyframeSerialization& ks : prop.keyframes) {
        KeyFrame kf;
        dk->setValueAtTime(ks.time, ks.value, ViewSpec::all(), dim,
                           eValueChangedReasonNatronInternalEdited, &kf);
        // Restore derivatives and interpolation after insertion
        kf.setLeftDerivative(ks.leftDerivative);
        kf.setRightDerivative(ks.rightDerivative);
        kf.setInterpolation(static_cast<KeyframeTypeEnum>(ks.interpolation));
    }
}

void restoreVecProperty(const NodePtr& node, const QString& knobFullName,
                        const std::vector<FluxAnimatorPropertySerialization>& props)
{
    for (size_t d = 0; d < props.size(); ++d) {
        restoreProperty(node, knobFullName, static_cast<int>(d), props[d]);
    }
}
} // anonymous namespace

namespace FluxTextAnimatorModel {

void restoreAnimators(const NodePtr& node, const std::vector<FluxAnimatorSerialization>& serialized)
{
    if (!node) return;

    for (const FluxAnimatorSerialization& as : serialized) {
        // Ensure knobs exist (creates the full knob set if missing)
        ensureAnimatorKnobs(node, as.animatorId, QString::fromStdString(as.name));

        // Set scalar values
        auto setBool = [&](const QString& suffix, bool val) {
            KnobBoolPtr k = std::dynamic_pointer_cast<KnobBool>(knob(node, knobName(as.animatorId, suffix)));
            if (k) k->setValue(val, ViewSpec::all(), 0, eValueChangedReasonPluginEdited, 0);
        };
        auto setInt = [&](const QString& suffix, int val) {
            KnobIntBasePtr k = std::dynamic_pointer_cast<KnobIntBase>(knob(node, knobName(as.animatorId, suffix)));
            if (k) k->setValue(val, ViewSpec::all(), 0, eValueChangedReasonNatronGuiEdited, 0);
        };

        setBool("enabled", as.enabled);
        setInt("basedOn", as.basedOn);
        setInt("shape", as.shape);
        setInt("anchor", as.anchor);
        setBool("scaleSeparated", as.scaleSeparated);

        // Restore name
        KnobStringBasePtr nameK = std::dynamic_pointer_cast<KnobStringBase>(
            knob(node, knobName(as.animatorId, QString::fromUtf8("name"))));
        if (nameK) nameK->setValue(as.name, ViewSpec::all(), 0, eValueChangedReasonPluginEdited, 0);

        // Restore range selector properties
        restoreProperty(node, knobName(as.animatorId, QString::fromUtf8("start")), 0, as.start);
        restoreProperty(node, knobName(as.animatorId, QString::fromUtf8("end")), 0, as.end);
        restoreProperty(node, knobName(as.animatorId, QString::fromUtf8("offset")), 0, as.offset);
        restoreProperty(node, knobName(as.animatorId, QString::fromUtf8("amount")), 0, as.amount);

        // Restore animated properties
        restoreProperty(node, knobName(as.animatorId, QString::fromUtf8("rotation")), 0, as.rotation);
        restoreProperty(node, knobName(as.animatorId, QString::fromUtf8("opacity")), 0, as.opacity);
        restoreProperty(node, knobName(as.animatorId, QString::fromUtf8("tracking")), 0, as.tracking);
        restoreVecProperty(node, knobName(as.animatorId, QString::fromUtf8("position")), as.position);
        restoreVecProperty(node, knobName(as.animatorId, QString::fromUtf8("scale")), as.scale);
        restoreVecProperty(node, knobName(as.animatorId, QString::fromUtf8("fillColor")), as.fillColor);
    }

    // Finally, rebuild the renderer JSON from the restored knobs
    syncAnimatorStackToRenderer(node);
}
} // namespace FluxTextAnimatorModel
```

### 10. Call from `serializeForProject()` in `Gui/FluxTimeline.cpp`

In `FluxTimeline::serializeForProject()`, after the masks loop (~line 4422), before `ser.layers.push_back(layerSer)`:

```cpp
// Text animator data
if (layer.type == QString::fromUtf8("text") && layer.gizmoNode) {
    layerSer.animators = FluxTextAnimatorModel::captureAnimators(layer.gizmoNode);
}
```

### 11. Call from `restoreFromProjectSerialization()` in `Gui/FluxTimeline.cpp`

In `FluxTimeline::restoreFromProjectSerialization()`, after the masks restore loop and before `_layers.append(layer)` (~line 4565), but **only after** `layer.gizmoNode` is resolved:

```cpp
// Restore text animator data (version 3+)
if (layer.type == QString::fromUtf8("text") && layer.gizmoNode && !layerSer.animators.empty()) {
    FluxTextAnimatorModel::restoreAnimators(layer.gizmoNode, layerSer.animators);
}
```

---

## Serialize Flow (what to capture for each animator)

For each animator ID (from `animatorOrder`):

| Field | Source | Type |
|---|---|---|
| `animatorId` | From `animatorIds()` | int |
| `name` | `fta_{id}_name` knob | string |
| `enabled` | `fta_{id}_enabled` knob | bool |
| `basedOn` | `fta_{id}_basedOn` knob (choice index) | int |
| `shape` | `fta_{id}_shape` knob (choice index) | int |
| `anchor` | `fta_{id}_anchor` knob (choice index) | int |
| `start` | `fta_{id}_start` dim 0 | double + keyframes |
| `end` | `fta_{id}_end` dim 0 | double + keyframes |
| `offset` | `fta_{id}_offset` dim 0 | double + keyframes |
| `amount` | `fta_{id}_amount` dim 0 | double + keyframes |
| `rotation` | `fta_{id}_rotation` dim 0 | double + keyframes |
| `opacity` | `fta_{id}_opacity` dim 0 | double + keyframes |
| `tracking` | `fta_{id}_tracking` dim 0 | double + keyframes |
| `position` | `fta_{id}_position` dims 0,1 | 2× (double + keyframes) |
| `scale` | `fta_{id}_scale` dims 0,1 | 2× (double + keyframes) |
| `fillColor` | `fta_{id}_fillColor` dims 0–3 | 4× (double + keyframes) |
| `scaleSeparated` | `fta_{id}_scaleSeparated` knob | bool |

Each keyframe captures: `time`, `value`, `leftDerivative`, `rightDerivative`, `interpolation` (as int).

**NOT captured** (no need — these are PyPlug knobs that Natron serializes natively):
- `animatorOrder`, `animatorNextId`, `animatorStackJson`, `animatorTimeDependency`

---

## Restore Flow (how to recreate knobs and set values/keyframes)

1. **Resolve gizmoNode** — `project->getNodeByFullySpecifiedName(layerSer.gizmoNodeScriptName)` gives us the FluxMotionText nodegroup.
2. **For each serialized animator**:
   a. Call `ensureAnimatorKnobs(node, id, name)` — creates all `fta_*` knobs if they don't exist.
   b. Set scalar (non-animated) values: `enabled`, `name`, `basedOn`, `shape`, `anchor`, `scaleSeparated`.
   c. For each numeric property per dimension:
      - `removeAnimation(ViewSpec::all(), dim)` — clear any default animation
      - `setValue(staticValue, ...)` — set the static value
      - For each keyframe: `setValueAtTime(time, value, ...)` then set derivatives + interpolation
3. **After all animators restored**: Call `syncAnimatorStackToRenderer(node)` to rebuild the JSON from the now-populated knobs. This also calls `syncAnimatorTimeDependency()`.

**Timing**: The restore must happen **after** `layer.gizmoNode` is resolved (it is — the gizmoNode lookup happens at ~line 4535 in the restore function).

**Note on `ensureAnimatorKnobs`**: The existing implementation calls `recreateUserKnobs(true)` internally. Calling it once per animator is acceptable but slightly redundant. An optimization would be to call `recreateUserKnobs` once after all knobs are created, but the existing pattern works correctly.

---

## Version Bump Details

| Struct | Current Version | New Version | Rationale |
|---|---|---|---|
| `FluxLayerSerialization` | 2 | **3** | Adds `std::vector<FluxAnimatorSerialization> animators` |
| `FluxTimelineSerialization` | 2 | 2 (no change) | No new top-level fields |
| `FluxAnimatorSerialization` | (new) | 0 | New struct, starts at 0 |
| `FluxAnimatorPropertySerialization` | (new) | 0 | New struct |
| `FluxAnimatorKeyframeSerialization` | (new) | 0 | New struct |

**Backward compatibility**: `version >= 3` guard means older .ntp files (version 0, 1, 2) load without animator data. `animators` vector will be empty → `restoreAnimators` simply not called → text layers load without animator data (same behavior as today). No data loss for legacy files.

**Forward compatibility**: If a future version bumps again, the `version >= 3` check naturally extends to `version >= 3 && version < 4` or additional guards.

---

## Validation Steps

1. **Build**: Full rebuild after changes. Must compile without errors.
2. **New project test**:
   - Create a text layer.
   - Add 2+ animators with different properties (position, opacity, rotation, scale).
   - Add keyframes at multiple times (e.g., frame 0, 30, 60) with varying interpolation types.
   - Save project → close → reopen.
   - Verify: All animators appear in the panel with correct names, enabled state, basedOn, shape, anchor.
   - Verify: All keyframe curves match the original (values, timing, interpolation).
   - Verify: `animatorStackJson` reflects the restored data (check via console or debug).
3. **Legacy project test**:
   - Open a pre-existing .ntp file (version 2) that contains text layers.
   - Verify: No crashes, no warnings about missing serialization data.
   - Verify: Text layers load without animators (same as before — no regression).
4. **Empty animator test**:
   - Create text layer with no animators.
   - Save → reload.
   - Verify: No spurious animators appear.
5. **Deleted animator test**:
   - Create text layer, add animator, remove it.
   - Save → reload.
   - Verify: Removed animator stays removed.
6. **Multi-dim keyframe test**:
   - Create animator with position keyframes (2D) and fillColor keyframes (4D).
   - Save → reload.
   - Verify: All dimensions have correct keyframes with correct derivatives.

---

## Non-Goals

1. **NOT fixing the root cause in Natron's NodeSerialization** — The `isUserKnob()` skip in `NodeSerialization::initialize()` is Natron engine code. We don't modify it; we work around it with explicit Flux-level serialization.
2. **NOT changing `animatorOrder`/`animatorStackJson` serialization** — These PyPlug knobs already survive save/load correctly via Natron's native path.
3. **NOT parenting `fta_*` knobs under a user page** (Option B) — That approach relies on Natron's implicit user-knob serialization behavior and is fragile. Option A (explicit serialization) is more robust.
4. **NOT adding undo/redo support** — This task is purely about save/load persistence.
5. **NOT adding a migration path for pre-existing `animatorStackJson`** — Since `animatorStackJson` is overwritten on reload anyway (by `syncAnimatorStackToRenderer` reading defaults), there's no recoverable data to migrate from legacy files.
6. **NOT modifying the FluxMotionText.py PyPlug** — No changes needed to the Python plugin.

---

## Notes for Reviewer

- The key architectural insight is that effects/masks "just work" because they reference Natron nodes (serialized natively). Animators are different — their knobs are runtime-created user knobs that Natron explicitly skips. This is why we need explicit Flux-level serialization.
- The `syncAnimatorStackToRenderer()` call at the end of `restoreAnimators()` is critical. Without it, the renderer JSON would be stale/empty.
- The `animatorIds()` function already parses `animatorOrder` (which IS saved by Natron). On reload, `animatorOrder` contains the right IDs, but the `fta_*` knobs are missing. `captureAnimators()` uses `animatorIds()` during save. `restoreAnimators()` calls `ensureAnimatorKnobs()` which recreates the knobs, then populates them.
- `KeyframeTypeEnum` is serialized as `int` for simplicity. This is safe because it's an enum with fixed values in Natron's `Curve.h`.
- The anonymous namespace helpers `captureProperty`/`restoreProperty`/`captureVecProperty`/`restoreVecProperty` are implementation details local to `FluxTextAnimatorModel.cpp` and do not pollute the public API.
