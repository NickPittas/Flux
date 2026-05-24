/* ***** BEGIN LICENSE BLOCK *****
 * Flux — Keyframe Property Edit Helpers
 * (C) 2025 Nick Pittas
 * GPL2 — see LICENSE.txt
 * ***** END LICENSE BLOCK ***** */

#include "Gui/FluxKeyframeModel.h"

#include "Engine/Knob.h"
#include "Engine/Curve.h"
#include "Engine/Node.h"
#include "Engine/Bezier.h"
#include "Engine/RotoContext.h"

#include <algorithm>
#include <cmath>
#include <set>

NATRON_NAMESPACE_ENTER

// ---------------------------------------------------------------------------
// nearestKeyTimeForProperty
// ---------------------------------------------------------------------------

bool
nearestKeyTimeForProperty(const FluxKeyframeProperty& property,
                          double frame,
                          double toleranceFrames,
                          double* outTime)
{
    if (!outTime) {
        return false;
    }

    if (property.isRotoAggregate) {
        if (!property.ownerNode) {
            return false;
        }
        RotoContextPtr rotoCtx = property.ownerNode->getRotoContext();
        if (!rotoCtx) {
            return false;
        }
        std::list<double> shapeTimes;
        rotoCtx->getBeziersKeyframeTimes(&shapeTimes);

        double bestDist = toleranceFrames + 1.0;
        double bestTime = 0.0;
        for (double t : shapeTimes) {
            double dist = std::abs(t - frame);
            if (dist < bestDist) {
                bestDist = dist;
                bestTime = t;
            }
        }
        if (bestDist <= toleranceFrames) {
            *outTime = bestTime;
            return true;
        }
        return false;
    }

    // Knob-backed property
    if (!property.knob) {
        return false;
    }

    double bestDist = toleranceFrames + 1.0;
    double bestTime = 0.0;

    for (int d : property.dims) {
        std::shared_ptr<Curve> curve = property.knob->getCurve(ViewSpec::current(), d);
        if (!curve) {
            continue;
        }
        KeyFrameSet keyFrames = curve->getKeyFrames_mt_safe();
        for (const KeyFrame& kf : keyFrames) {
            double dist = std::abs(kf.getTime() - frame);
            if (dist < bestDist) {
                bestDist = dist;
                bestTime = kf.getTime();
            }
        }
    }

    if (bestDist <= toleranceFrames) {
        *outTime = bestTime;
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Knob-backed helpers (anonymous namespace)
// ---------------------------------------------------------------------------

namespace {

bool
addKnobKeyAtTime(const FluxKeyframeProperty& property, double time)
{
    // Batch knob changes so evaluation/render is deferred until all dims are set.
    // This avoids per-dim re-renders and gives Natron a single evaluation point.
    // Limitation: this is NOT a full Natron undo command — the user sees one visual
    // change but undo granularity depends on Natron's internal knob change batching.
    property.knob->beginChanges();
    bool anySet = false;
    for (int d : property.dims) {
        if (property.knob->onKeyFrameSet(time, ViewIdx(0), d)) {
            anySet = true;
        }
    }
    property.knob->endChanges();
    return anySet;
}

bool
deleteKnobKeysAtTime(const FluxKeyframeProperty& property, double time)
{
    property.knob->beginChanges();
    bool anyRemoved = false;
    for (int d : property.dims) {
        std::shared_ptr<Curve> curve = property.knob->getCurve(ViewSpec::current(), d);
        if (!curve) {
            continue;
        }
        KeyFrame kf;
        if (!curve->getKeyFrameWithTime(time, &kf)) {
            continue; // no key at this time for this dim
        }
        bool isLastKey = (curve->getKeyFramesCount() == 1);
        property.knob->onKeyFrameRemoved(time, ViewIdx(0), d, isLastKey);
        anyRemoved = true;
    }
    property.knob->endChanges();
    return anyRemoved;
}

bool
moveKnobKeysAtTime(const FluxKeyframeProperty& property,
                   double oldTime,
                   double newTime)
{
    // Phase 1: Determine which dims have keys at oldTime and verify no
    // collisions at newTime.
    std::vector<int> dimsToMove;
    for (int d : property.dims) {
        std::shared_ptr<Curve> curve = property.knob->getCurve(ViewSpec::current(), d);
        if (!curve) {
            continue;
        }
        KeyFrame kf;
        if (!curve->getKeyFrameWithTime(oldTime, &kf)) {
            continue; // no key at oldTime
        }
        // Check collision: does this dim already have a key at newTime?
        KeyFrame kfNew;
        if (curve->getKeyFrameWithTime(newTime, &kfNew)) {
            return false; // collision — abort without moving anything
        }
        dimsToMove.push_back(d);
    }

    if (dimsToMove.empty()) {
        return false;
    }

    // Phase 2: Move keys (batched so evaluation is deferred)
    property.knob->beginChanges();
    double dt = newTime - oldTime;
    bool anyMoved = false;
    for (int d : dimsToMove) {
        KeyFrame newKey;
        if (property.knob->moveValueAtTime(eCurveChangeReasonDopeSheet,
                                           oldTime, ViewIdx(0), d,
                                           dt, 0, &newKey)) {
            anyMoved = true;
        }
    }
    property.knob->endChanges();
    return anyMoved;
}

// Collect all active Bezier shapes from a roto context.
std::vector<BezierPtr>
getActiveBeziers(const RotoContextPtr& rotoCtx)
{
    std::vector<BezierPtr> result;
    if (!rotoCtx) {
        return result;
    }
    std::list<RotoDrawableItemPtr> items = rotoCtx->getCurvesByRenderOrder(true);
    for (const RotoDrawableItemPtr& item : items) {
        BezierPtr bez = std::dynamic_pointer_cast<Bezier>(item);
        if (bez) {
            result.push_back(bez);
        }
    }
    return result;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Roto aggregate helpers (anonymous namespace)
// ---------------------------------------------------------------------------

namespace {

bool
addRotoKeyAtTime(const FluxKeyframeProperty& property, double time)
{
    if (!property.ownerNode) {
        return false;
    }
    RotoContextPtr rotoCtx = property.ownerNode->getRotoContext();
    if (!rotoCtx) {
        return false;
    }

    std::vector<BezierPtr> beziers = getActiveBeziers(rotoCtx);
    if (beziers.empty()) {
        return false;
    }

    for (const BezierPtr& bez : beziers) {
        bez->setKeyframe(time);
    }

    rotoCtx->evaluateChange();
    rotoCtx->emitRefreshViewerOverlays();
    return true;
}

bool
deleteRotoKeysAtTime(const FluxKeyframeProperty& property, double time)
{
    if (!property.ownerNode) {
        return false;
    }
    RotoContextPtr rotoCtx = property.ownerNode->getRotoContext();
    if (!rotoCtx) {
        return false;
    }

    std::vector<BezierPtr> beziers = getActiveBeziers(rotoCtx);
    bool anyRemoved = false;

    for (const BezierPtr& bez : beziers) {
        std::set<double> times;
        bez->getKeyframeTimes(&times);
        if (times.find(time) != times.end()) {
            bez->removeKeyframe(time);
            anyRemoved = true;
        }
    }

    if (anyRemoved) {
        rotoCtx->evaluateChange();
        rotoCtx->emitRefreshViewerOverlays();
    }
    return anyRemoved;
}

bool
moveRotoKeysAtTime(const FluxKeyframeProperty& property,
                   double oldTime,
                   double newTime)
{
    if (!property.ownerNode) {
        return false;
    }
    RotoContextPtr rotoCtx = property.ownerNode->getRotoContext();
    if (!rotoCtx) {
        return false;
    }

    std::vector<BezierPtr> beziers = getActiveBeziers(rotoCtx);

    // Phase 1: Identify beziers with oldTime and check collision at newTime.
    std::vector<BezierPtr> toMove;
    for (const BezierPtr& bez : beziers) {
        std::set<double> times;
        bez->getKeyframeTimes(&times);
        bool hasOld = (times.find(oldTime) != times.end());
        if (!hasOld) {
            continue;
        }
        // Collision: this Bezier already has a key at newTime.
        if (times.find(newTime) != times.end()) {
            return false; // abort without moving anything
        }
        toMove.push_back(bez);
    }

    if (toMove.empty()) {
        return false;
    }

    // Phase 2: Move keys
    for (const BezierPtr& bez : toMove) {
        bez->moveKeyframe(oldTime, newTime);
    }

    rotoCtx->evaluateChange();
    rotoCtx->emitRefreshViewerOverlays();
    return true;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Public dispatch functions
// ---------------------------------------------------------------------------

bool
addKeyAtTime(const FluxKeyframeProperty& property, double time)
{
    if (property.isRotoAggregate) {
        return addRotoKeyAtTime(property, time);
    }
    if (!property.knob) {
        return false;
    }
    return addKnobKeyAtTime(property, time);
}

bool
deleteKeysAtTime(const FluxKeyframeProperty& property, double time)
{
    if (property.isRotoAggregate) {
        return deleteRotoKeysAtTime(property, time);
    }
    if (!property.knob) {
        return false;
    }
    return deleteKnobKeysAtTime(property, time);
}

bool
moveKeysAtTime(const FluxKeyframeProperty& property,
               double oldTime,
               double newTime)
{
    if (property.isRotoAggregate) {
        return moveRotoKeysAtTime(property, oldTime, newTime);
    }
    if (!property.knob) {
        return false;
    }
    return moveKnobKeysAtTime(property, oldTime, newTime);
}

NATRON_NAMESPACE_EXIT
