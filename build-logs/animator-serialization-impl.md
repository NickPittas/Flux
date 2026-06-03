# Animator Serialization Implementation Report

## Summary
Implemented FluxAnimatorSerialization for text animator save/load persistence across 4 files with a clean build and no whitespace issues.

## Build Result
**SUCCESS** — Full build completed without errors. All object files compiled, linking succeeded, `Natron` binary built.

## git diff --check
**CLEAN** — No whitespace errors, no trailing spaces, no blank-at-EOF issues.

## Files Changed (4 files, +302 / -5 lines)

### 1. `Gui/FluxTimelineSerialization.h` (+137 lines)
- Added `FluxAnimatorKeyframeSerialization` struct (time, value, leftDerivative, rightDerivative, interpolation)
- Added `FluxAnimatorPropertySerialization` struct (value + vector of keyframes, with is_loading guard)
- Added `FluxAnimatorSerialization` struct (full animator state: id, name, enabled, basedOn, shape, anchor, all range selector props, all animated props, position/scale/fillColor as vectors, scaleSeparated)
- Added `animators` member (`std::vector<FluxAnimatorSerialization>`) to `FluxLayerSerialization`
- Added `version >= 3` guard block in `FluxLayerSerialization::serialize()` for animators
- Bumped `BOOST_CLASS_VERSION(FluxLayerSerialization, 3)` from 2

### 2. `Gui/FluxTextAnimatorModel.h` (+10 lines)
- Added `#include <vector>` 
- Added forward declaration `struct FluxAnimatorSerialization` in NATRON_NAMESPACE scope
- Declared `captureAnimators(const NodePtr&)` returning `std::vector<FluxAnimatorSerialization>`
- Declared `restoreAnimators(const NodePtr&, const std::vector<FluxAnimatorSerialization>&)`

### 3. `Gui/FluxTextAnimatorModel.cpp` (+142 lines)
- Added `#include "Gui/FluxTimelineSerialization.h"` for struct definitions
- Added anonymous-namespace helpers: `captureProperty()`, `captureVecProperty()`, `restoreProperty()`, `restoreVecProperty()`
- Implemented `captureAnimators()` — iterates `animatorIds()`, reads all fta_* knobs, captures values + keyframe curves with derivatives and interpolation
- Implemented `restoreAnimators()` — calls `ensureAnimatorKnobs()` per animator, restores scalar values (enabled, name, basedOn, shape, anchor, scaleSeparated), restores animated properties with `removeAnimation` + `setValue` + `setValueAtTime` for each keyframe, calls `syncAnimatorStackToRenderer()` at end

### 4. `Gui/FluxTimeline.cpp` (+8 lines in 2 sites)
- **serializeForProject()** (line ~4424): Added animator capture after masks loop — captures `layerSer.animators` for text layers with gizmoNode
- **restoreFromProjectSerialization()** (line ~4566): Added animator restore after masks restore and before `_layers.append(layer)` — restores animators for text layers with gizmoNode and non-empty serialized animators

## Key Design Decisions
- **Forward declaration** of `FluxAnimatorSerialization` in `FluxTextAnimatorModel.h` avoids circular include dependency. The full type is available at call sites via `FluxTimelineSerialization.h` include.
- **Version 3 guard** ensures backward compatibility: older .ntp files (version 0-2) load without animator data, empty animators vector → no restore call → no regression.
- **Anonymous namespace helpers** in `.cpp` reuse existing `knob()`, `stringValue()`, `boolValue()`, `intValue()` helpers from the file's existing anonymous namespace.

## Validation Targets
- **Build**: ✅ PASSED
- **Whitespace**: ✅ PASSED (`git diff --check` clean)
- **Runtime tests** (not run yet — requires GUI):
  1. Create text layer, add 2+ animators with keyframes, save, reload, verify all animators/keys restored
  2. Open legacy .ntp (version 2) with text layers — no crashes, no data loss
  3. Text layer with no animators — save/reload produces no spurious animators
