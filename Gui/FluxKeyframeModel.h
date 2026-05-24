/* ***** BEGIN LICENSE BLOCK *****
 * Flux — Keyframe Property Row Model
 * (C) 2025 Nick Pittas
 * GPL2 — see LICENSE.txt
 * ***** END LICENSE BLOCK ***** */

#ifndef FLUXKEYFRAMEMODEL_H
#define FLUXKEYFRAMEMODEL_H

#include "Global/Macros.h"

#include "Engine/EngineFwd.h"
#include "Engine/ViewIdx.h"

#include <QList>
#include <QString>

#include <set>
#include <string>
#include <vector>

NATRON_NAMESPACE_ENTER

// Forward declarations — avoid circular include with FluxTimeline.h
struct FluxLayer;
struct FluxEffect;
struct FluxMask;

// ---------------------------------------------------------------------------
// Enums
// ---------------------------------------------------------------------------

enum FluxKeyframeOwnerKind {
    eFluxKeyframeOwnerLayer,
    eFluxKeyframeOwnerEffect,
    eFluxKeyframeOwnerMask
};

enum FluxKeyframePropertyFlags {
    eFluxKeyframePropNone       = 0,
    eFluxKeyframePropReadOnly   = 1 << 0,
    eFluxKeyframePropSystem     = 1 << 1
};

// ---------------------------------------------------------------------------
// Structs
// ---------------------------------------------------------------------------

/** @brief A lightweight keyframe handle returned by key queries. */
struct FluxKeyframeKey {
    double time;
    bool isPartial;  // true if some grouped dims lack a key at this time
    int animatedDimCount;      // number of animated dims with a key at this time
    int totalAnimatedDimCount; // total animated dims in the group

    FluxKeyframeKey()
        : time(0.0)
        , isPartial(false)
        , animatedDimCount(0)
        , totalAnimatedDimCount(0)
    {}
};

/** @brief Describes one animated property row shown in the keyframe view. */
struct FluxKeyframeProperty {
    FluxKeyframeOwnerKind ownerKind;
    QString label;

    // Backing store (at least one is valid)
    NodePtr ownerNode;       // node that owns the knob or mask
    KnobIPtr knob;           // null for roto aggregate rows
    std::vector<int> dims;   // animated dimensions included in this row

    // Multidim grouping UI state. groupKey is stable node-name + knob-name.
    std::string groupKey;
    int groupDimCount;
    bool isGrouped;

    // Roto aggregate
    bool isRotoAggregate;

    // Flags
    int flags;

    FluxKeyframeProperty()
        : ownerKind(eFluxKeyframeOwnerLayer)
        , groupDimCount(0)
        , isGrouped(false)
        , isRotoAggregate(false)
        , flags(eFluxKeyframePropNone)
    {}
};

// ---------------------------------------------------------------------------
// Query functions
// ---------------------------------------------------------------------------

/** @brief Build animated property rows for a layer's gizmo node. */
QList<FluxKeyframeProperty> buildLayerProperties(const FluxLayer& layer,
                                                 const std::set<std::string>& ungroupedKeys = std::set<std::string>());

/** @brief Build animated property rows for an effect node. */
QList<FluxKeyframeProperty> buildEffectProperties(const FluxEffect& effect,
                                                   bool ownerIsAdjustment,
                                                   const std::set<std::string>& ungroupedKeys = std::set<std::string>());

/** @brief Build animated property rows for a mask (roto aggregate). */
QList<FluxKeyframeProperty> buildMaskProperties(const FluxMask& mask);

/** @brief Stable grouping key for a node/knob pair. */
std::string keyframeGroupingKey(const NodePtr& node,
                                const KnobIPtr& knob);

/** @brief Return keyframe times for a property row, with grouped/partial info. */
QList<FluxKeyframeKey> keysForProperty(const FluxKeyframeProperty& property);

/** @brief Return true if the property has a key at exactly the given time. */
bool propertyHasKeysAtTime(const FluxKeyframeProperty& property,
                           double time);

// ---------------------------------------------------------------------------
// Key edit helper functions
// ---------------------------------------------------------------------------

/**
 * @brief Find the nearest keyframe time to `frame` within `toleranceFrames`.
 *
 * Searches all curves/dimensions represented by `property`. Returns true and
 * writes the nearest time into `outTime` if a key was found within tolerance.
 * For roto aggregate properties, searches all Bezier shape keyframe times.
 */
bool nearestKeyTimeForProperty(const FluxKeyframeProperty& property,
                               double frame,
                               double toleranceFrames,
                               double* outTime);

/**
 * @brief Add a keyframe at `time` for every dimension represented by the property.
 *
 * For knob-backed properties, calls knob->onKeyFrameSet() for each dim in
 * property.dims. Does NOT add keys to dimensions not in property.dims.
 * For roto aggregate properties, calls Bezier::setKeyframe(time) for every
 * active Bezier, then refreshes the roto context.
 *
 * @return true if at least one key was added or set.
 */
bool addKeyAtTime(const FluxKeyframeProperty& property, double time);

/**
 * @brief Delete keyframes at `time` for every represented dimension that has one.
 *
 * For knob-backed properties, removes keys only for dimensions that actually
 * have a key at `time`. Uses copyCurveValueAtTimeToInternalValue=true when a
 * dimension's curve has exactly one key left.
 * For roto aggregate properties, calls removeKeyframe(time) on every Bezier
 * that has a key at `time`, then refreshes the roto context.
 *
 * @return true if at least one key was removed.
 */
bool deleteKeysAtTime(const FluxKeyframeProperty& property, double time);

/**
 * @brief Move keyframes from `oldTime` to `newTime` for represented dimensions.
 *
 * For knob-backed properties, moves only dimensions that have an existing key
 * at `oldTime`. Refuses to move if the target `newTime` is already occupied
 * by a key on the same dimension.
 * For roto aggregate properties, moves only Beziers that have `oldTime`.
 * Refuses to move any Bezier if that same Bezier already has `newTime`;
 * returns false in the collision case without moving anything.
 *
 * @return true if at least one key was moved successfully.
 */
bool moveKeysAtTime(const FluxKeyframeProperty& property,
                    double oldTime,
                    double newTime);

NATRON_NAMESPACE_EXIT

#endif // FLUXKEYFRAMEMODEL_H
