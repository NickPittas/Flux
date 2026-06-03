# Locator Report: Text Animator Serialization Gap

**Date**: 2026-05-26  
**Task**: Locate serialization surface for Flux text animator keyframes and settings  
**Root Cause**: CONFIRMED — text animator keyframes and settings are NOT serialized to .ntp

---

## 1. In-Memory Animator Data Model

### Files and Symbols

| File | Symbol | Purpose |
|---|---|---|
| `Gui/FluxTextAnimatorModel.h` | `struct FluxTextAnimatorSummary` | Lightweight summary (id, name, enabled, basedOn, shape) |
| `Gui/FluxTextAnimatorModel.h` | `namespace FluxTextAnimatorModel` | Static helpers for animator CRUD |
| `Gui/FluxTextAnimatorModel.cpp` | `ensureAnimatorKnobs()` (L313) | Creates all `fta_*` knobs for one animator |
| `Gui/FluxTextAnimatorModel.cpp` | `syncAnimatorStackToRenderer()` (L445) | Reads knob values → writes `animatorStackJson` |
| `Gui/FluxTextAnimatorPanel.h` | `class FluxTextAnimatorPanel` | UI panel that reads/writes animator knobs |
| `Gui/FluxTextAnimatorPanel.cpp` | `setActiveNode()` (L119) | Calls `ensureAnimatorCompatibility` + `syncAnimatorStackToRenderer` |

### Knob Naming Convention

All animator data lives as **dynamic user knobs** on the FluxMotionText nodegroup (gizmo), named `fta_{id}_{suffix}`:

| Suffix | Type | Animatable | Purpose |
|---|---|---|---|
| `enabled` | Bool | No | Animator on/off |
| `name` | String | No | Display name |
| `basedOn` | Choice | No | chars/charsNoSpaces/words/lines |
| `shape` | Choice | No | square/linear/rampUp/rampDown |
| `anchor` | Choice | No | bottomLeft/center/bottomRight |
| `start` | Double | Yes | Range selector start % |
| `end` | Double | Yes | Range selector end % |
| `offset` | Double | Yes | Range selector offset % |
| `amount` | Double | Yes | Strength % |
| `position` | Double(2) | Yes | Position offset X/Y |
| `scale` | Double(2) | Yes | Scale X/Y % |
| `scaleSeparated` | Bool | No | Separate X/Y toggle |
| `rotation` | Double | Yes | Rotation degrees |
| `opacity` | Double | Yes | Opacity % |
| `fillColor` | Color(4) | Yes | RGBA fill color |
| `tracking` | Double | Yes | Tracking value |

### Metadata Knobs (PyPlug-declared, NOT user knobs)

| Knob | File | Serialized? |
|---|---|---|
| `animatorOrder` (string "1,2,3") | `plugins/FluxMotionText.py` L253 | ✅ Yes (PyPlug knob) |
| `animatorNextId` (int) | `plugins/FluxMotionText.py` L249 | ✅ Yes (PyPlug knob) |
| `animatorStackJson` (JSON string) | `plugins/FluxMotionText.py` L257 | ✅ Yes (PyPlug knob, alias to TextRender) |
| `animatorTimeDependency` (double) | `plugins/FluxMotionText.py` L261 | ✅ Yes (PyPlug knob, alias to TextRender) |

---

## 2. Timeline/Layer Serialization

### Files and Symbols

| File | Symbol | Lines | Purpose |
|---|---|---|---|
| `Gui/FluxTimelineSerialization.h` | `struct FluxLayerSerialization` | L74–L195 | Per-layer serialization (Boost XML) |
| `Gui/FluxTimelineSerialization.h` | `struct FluxEffectSerialization` | L26–L48 | Per-effect serialization |
| `Gui/FluxTimelineSerialization.h` | `struct FluxMaskSerialization` | L50–L72 | Per-mask serialization |
| `Gui/FluxTimelineSerialization.h` | `struct FluxTimelineSerialization` | L197–L240 | Top-level timeline serialization |
| `Gui/FluxTimeline.cpp` | `FluxTimeline::serializeForProject()` | L4328–L4430 | Collects FluxLayer → FluxLayerSerialization |
| `Gui/FluxTimeline.cpp` | `FluxTimeline::restoreFromProjectSerialization()` | L4430+ | Restores FluxLayerSerialization → FluxLayer |
| `Gui/ProjectGuiSerialization.h` | `class ProjectGuiSerialization` | L290–L390 | Embeds `FluxTimelineSerialization` in .ntp |
| `Gui/ProjectGuiSerialization.cpp` | `ProjectGuiSerialization::initialize()` | L147–L160 | Calls `timeline->serializeForProject()` |
| `Gui/ProjectGuiSerialization.cpp` | save/load methods | L318/L348 | Serializes `_fluxTimeline` to XML |

### Serialization Flow

```
Save:
  ProjectGuiSerialization::initialize()
    → FluxTimeline::serializeForProject()
      → For each FluxLayer: creates FluxLayerSerialization
        → For each FluxEffect: creates FluxEffectSerialization
        → For each FluxMask: creates FluxMaskSerialization
    → _fluxTimeline is embedded in .ntp via Boost XML

Load:
  ProjectGuiSerialization::load()
    → deserializes _fluxTimeline from XML
  ProjectGui::restore() calls:
    → FluxTimeline::restoreFromProjectSerialization()
      → resolves node script names to NodePtr
      → rebuilds FluxLayer in-memory structs
```

---

## 3. Does Text Animator Data Have Serialize/Deserialize Code?

**NO.** There is zero serialization code for text animator data in FluxTimelineSerialization.h or anywhere in the Flux serialization pipeline.

### What IS serialized for text layers (via FluxLayerSerialization):
- Layer `type = "text"`
- `gizmoNodeScriptName` (the FluxMotionText nodegroup's fully qualified name)
- Timing: inPoint, outPoint, timeOffset, etc.
- Effects, masks

### What IS serialized for text layers (via Natron native node serialization):
- The `animatorOrder`, `animatorNextId` knobs (PyPlug knobs, not user knobs)
- The `animatorStackJson` knob (PyPlug knob with alias)
- The `text`, `font`, `fontSize`, `fillColor`, etc. (PyPlug knobs with aliases)

### What is NOT serialized:
- All `fta_*` dynamic user knobs (keyframe curves, values, animation state)
- These knobs are lost on save/reload

---

## 4. How Other Layer-Level Data IS Serialized Successfully

### Effects — working pattern

| Step | Code Location |
|---|---|
| In-memory struct | `FluxTimeline.h` → `struct FluxEffect` (L98) |
| Serialization struct | `FluxTimelineSerialization.h` → `FluxEffectSerialization` (L26) |
| Serialize (save) | `FluxTimeline.cpp` L4395–L4404: loops `layer.effects`, creates `FluxEffectSerialization` |
| Deserialize (load) | `FluxTimeline.cpp` L4480–L4498: resolves script names to NodePtr |
| Persistence mechanism | Node script name → Natron node serialization handles knob values |

### Masks — working pattern

| Step | Code Location |
|---|---|
| In-memory struct | `FluxTimeline.h` → `struct FluxMask` (L106) |
| Serialization struct | `FluxTimelineSerialization.h` → `FluxMaskSerialization` (L50) |
| Serialize (save) | `FluxTimeline.cpp` L4407–L4422: loops `layer.masks` |
| Deserialize (load) | `FluxTimeline.cpp` L4500+: resolves mask/reformat node script names |

### Key insight about effects/masks vs. animators

Effects and masks reference **Natron nodes** (by script name). Natron's native serialization saves/restores all node knobs automatically. So Flux only needs to serialize the node reference (script name), and Natron handles the rest.

Animators are different: the `fta_*` knobs live on the FluxMotionText nodegroup itself. They are **user knobs** created at runtime via `KnobHolder::createDoubleKnob()`. Natron's `NodeSerialization::initialize()` skips user knobs unless they are parented under a user page (`GroupKnobSerialization` in `_userPages`). The `fta_*` knobs are orphans with no page/group parent → they fall through both serialization paths.

---

## 5. Where Serialization Would Need to Be Added

### Root Cause Summary

The `fta_*` knobs are created as orphan user knobs (no page/group parent). Natron's serialization:
1. Skips user knobs in `_knobsValues` (NodeSerialization.cpp L85: `if (knobs[i]->isUserKnob()) { continue; }`)
2. Only captures user knobs in `_userPages` if they're children of a user page/group (NodeSerialization.cpp L74-82)
3. The `fta_*` knobs are never added to any page → never captured

### Fix Options

**Option A: Add FluxAnimatorSerialization to FluxLayerSerialization** (recommended, explicit)

Files to modify:
1. **`Gui/FluxTimelineSerialization.h`** — Add `FluxAnimatorSerialization` struct (analogous to `FluxEffectSerialization`) and add `std::vector<FluxAnimatorSerialization> animators` to `FluxLayerSerialization`. Bump `BOOST_CLASS_VERSION(FluxLayerSerialization, 3)`.

2. **`Gui/FluxTimeline.cpp`** — In `serializeForProject()` (L4328), for text layers, iterate animator knobs and serialize their names + keyframe curves. In `restoreFromProjectSerialization()` (L4430), restore animator knobs.

3. **`Gui/FluxTextAnimatorModel.h/cpp`** — Add `serialize()` / `restore()` functions that extract/reinject animator data.

Serializer would need to capture for each animator:
- Animator ID, name, enabled, basedOn, shape, anchor
- Per-property: current value per dimension + keyframe curve (time, value, interpolation)

**Option B: Parent fta_* knobs under a user page** (leverages Natron's existing serialization)

Files to modify:
1. **`Gui/FluxTextAnimatorModel.cpp`** — In `ensureAnimatorKnobs()`, add each created knob to a user page via `page->addKnob(knob)`. This way Natron's `GroupKnobSerialization` in `_userPages` would capture them.

2. **`Gui/FluxTextAnimatorModel.cpp`** — In `ensureAnimatorCompatibility()` (called on reload), detect missing `fta_*` knobs from `animatorOrder` and call `ensureAnimatorKnobs()` to recreate them, so Natron's restore path can then set their values.

This option is simpler but relies on Natron's user-knob serialization which has implicit behavior around page/group hierarchy.

### Exact Anchors for Implementation

| File | Line Range | What to Change |
|---|---|---|
| `Gui/FluxTimelineSerialization.h` | L72 (after FluxMaskSerialization) | Add `FluxAnimatorSerialization` struct |
| `Gui/FluxTimelineSerialization.h` | L161 (FluxLayerSerialization members) | Add `std::vector<FluxAnimatorSerialization> animators` |
| `Gui/FluxTimelineSerialization.h` | L195 (FluxLayerSerialization::serialize) | Serialize animators in version >= 3 |
| `Gui/FluxTimelineSerialization.h` | L243 (bottom) | Bump `BOOST_CLASS_VERSION(FluxLayerSerialization, 3)` |
| `Gui/FluxTimeline.cpp` | L4395 (serializeForProject, after masks) | Serialize text layer animators |
| `Gui/FluxTimeline.cpp` | L4500 (restoreFromProject, after masks) | Restore text layer animators |
| `Gui/FluxTextAnimatorModel.h` | After L43 | Add `QJsonObject serializeAnimator(const NodePtr&, int id)` and restore functions |
| `Gui/FluxTextAnimatorModel.cpp` | After L475 | Implement serialize/restore logic |

---

## Confidence

**HIGH** (95%) — Root cause confirmed by code tracing:
- All `fta_*` knobs created as user knobs (default `userKnob = true` in `createDoubleKnob`)
- `NodeSerialization::initialize()` explicitly skips user knobs (L85)
- User knobs only captured if parented under user page → `fta_*` are orphans
- `animatorOrder`/`animatorStackJson` DO survive (PyPlug knobs), but the actual keyframe-bearing knobs don't
- On reload, `ensureAnimatorCompatibility` doesn't recreate the full knob set
- `syncAnimatorStackToRenderer` runs with missing knobs → writes default values → overwrites saved JSON
