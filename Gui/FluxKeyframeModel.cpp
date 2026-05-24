/* ***** BEGIN LICENSE BLOCK *****
 * Flux — Keyframe Property Row Model
 * (C) 2025 Nick Pittas
 * GPL2 — see LICENSE.txt
 * ***** END LICENSE BLOCK ***** */

#include "Gui/FluxKeyframeModel.h"

#include "Gui/FluxTimeline.h"
#include "Engine/Knob.h"
#include "Engine/KnobTypes.h"
#include "Engine/Node.h"
#include "Engine/EffectInstance.h"
#include "Engine/Curve.h"
#include "Engine/RotoContext.h"

#include <algorithm>
#include <set>
#include <string>

NATRON_NAMESPACE_ENTER

namespace {

// Knob type names that are never user-animatable property rows.
static const char* kHiddenTypeNames[] = {
    "Page",
    "Separator",
    "Group",
    nullptr
};

// Knob script names that are always hidden from the keyframe view.
// NOTE: disableNode is NOT hidden globally — it is hidden only for
// adjustment-row effect nodes (see shouldShowKnob ownerIsAdjustment check).
static const char* kHiddenScriptNames[] = {
    " NatronOfFile",            // internal file knobs
    "NatronNodeInfoPanel",
    "userTextArea",
    nullptr
};

// Prefixes of knobs that are internal Text node duplicates (not promoted).
static const char* kInternalTextPrefixes[] = {
    "text_font",
    "text_size",
    "text_color",
    "text_text",
    nullptr
};

// Promoted Text1 knob name prefix (the only ones we expose for text layers).
static const char* kPromotedTextPrefix = "Text1";

// Public FluxText gizmo controls that are not prefixed with Text1 but should
// still appear as keyframe property rows for text layers.
static const char* kFluxTextPublicControls[] = {
    "opacity",
    nullptr
};

bool
isHiddenTypeName(const std::string& typeName)
{
    for (int i = 0; kHiddenTypeNames[i]; ++i) {
        if (typeName == kHiddenTypeNames[i]) {
            return true;
        }
    }
    return false;
}

bool
isHiddenScriptName(const std::string& name)
{
    for (int i = 0; kHiddenScriptNames[i]; ++i) {
        if (name == kHiddenScriptNames[i]) {
            return true;
        }
    }
    return false;
}

bool
isInternalTextKnob(const std::string& name)
{
    for (int i = 0; kInternalTextPrefixes[i]; ++i) {
        if (name.find(kInternalTextPrefixes[i]) == 0) {
            return true;
        }
    }
    return false;
}

bool
isPromotedTextKnob(const std::string& name)
{
    return name.find(kPromotedTextPrefix) == 0;
}

bool
isFluxTextPublicControl(const std::string& name)
{
    for (int i = 0; kFluxTextPublicControls[i]; ++i) {
        if (name == kFluxTextPublicControls[i]) {
            return true;
        }
    }
    return false;
}

// Return true if this knob should be visible in the keyframe property list.
bool
shouldShowKnob(const KnobIPtr& knob,
               bool isTextLayer,
               bool ownerIsAdjustment)
{
    if (!knob) {
        return false;
    }

    const std::string& scriptName = knob->getName();

    // Hide pages, separators, groups
    if (isHiddenTypeName(knob->typeName())) {
        return false;
    }

    // Hide secret knobs (including recursive parent-secret)
    if (knob->getIsSecretRecursive()) {
        return false;
    }

    // Hide system/internal knob names
    if (isHiddenScriptName(scriptName)) {
        return false;
    }

    // For adjustment layers, always hide the disable knob
    if (ownerIsAdjustment && scriptName == kDisableNodeKnobName) {
        return false;
    }

    // Text layer filtering
    if (isTextLayer) {
        // T074 v1 exposes promoted Text1... controls and whitelisted public
        // FluxText gizmo controls (e.g. opacity) for text layers.
        // Internal native Text node duplicates and generic implementation knobs
        // stay hidden from the FluxTimeline keyframe rows.
        if (isFluxTextPublicControl(scriptName)) {
            return true;
        }
        return isPromotedTextKnob(scriptName) && !isInternalTextKnob(scriptName);
    }

    // Must be able to animate
    if (!knob->canAnimate()) {
        return false;
    }

    // Must have animation enabled
    if (!knob->isAnimationEnabled()) {
        return false;
    }

    return true;
}

// Return the list of animated dimensions for a knob.
std::vector<int>
getAnimatedDimensions(const KnobIPtr& knob)
{
    std::vector<int> dims;
    if (!knob) {
        return dims;
    }
    int nDims = knob->getDimension();
    for (int d = 0; d < nDims; ++d) {
        if (knob->isAnimated(d, ViewSpec::current())) {
            dims.push_back(d);
        }
    }
    return dims;
}

// Build a user-visible label for a single-dimension property row.
QString
singleDimLabel(const KnobIPtr& knob, int dim)
{
    QString knobLabel = QString::fromStdString(knob->getLabel());
    std::string dimName = knob->getDimensionName(dim);
    // If the dimension name is non-trivial, append it
    if (!dimName.empty() && dimName != " ") {
        return knobLabel + QString::fromUtf8(".%1").arg(QString::fromStdString(dimName));
    }
    return knobLabel;
}

// Build a user-visible label for a grouped multi-dim property row.
QString
groupedDimLabel(const KnobIPtr& knob)
{
    return QString::fromStdString(knob->getLabel());
}

// Collect animated property rows from a node's knobs.
void
collectKnobProperties(const NodePtr& node,
                      FluxKeyframeOwnerKind ownerKind,
                      bool isTextLayer,
                      bool ownerIsAdjustment,
                      const std::set<std::string>& ungroupedKeys,
                      QList<FluxKeyframeProperty>* out)
{
    if (!node) {
        return;
    }
    const std::vector<KnobIPtr>& knobs = node->getKnobs();
    for (const KnobIPtr& knob : knobs) {
        if (!shouldShowKnob(knob, isTextLayer, ownerIsAdjustment)) {
            continue;
        }

        std::vector<int> animatedDims = getAnimatedDimensions(knob);
        if (animatedDims.empty()) {
            continue;
        }

        // Skip knobs that are slaved (aliases show through the master)
        bool anyDimSlaved = false;
        for (int d : animatedDims) {
            if (knob->isSlave(d)) {
                anyDimSlaved = true;
                break;
            }
        }
        if (anyDimSlaved) {
            // The master knob will be shown instead. Skip the slave.
            // Exception: if ALL dims are slaves, skip entirely.
            // If some dims are slaves and some not, we still skip
            // to avoid confusing partial rows.
            continue;
        }

        std::string groupingKey = keyframeGroupingKey(node, knob);
        int groupDimCount = static_cast<int>(animatedDims.size());
        bool forceUngrouped = (groupDimCount > 1 && ungroupedKeys.find(groupingKey) != ungroupedKeys.end());

        if (animatedDims.size() == 1 || forceUngrouped) {
            // Single animated dimension → separate row
            for (int dim : animatedDims) {
                FluxKeyframeProperty prop;
                prop.ownerKind = ownerKind;
                prop.ownerNode = node;
                prop.knob = knob;
                prop.dims.push_back(dim);
                prop.groupKey = groupingKey;
                prop.groupDimCount = groupDimCount;
                prop.isGrouped = false;
                prop.isRotoAggregate = false;
                prop.flags = eFluxKeyframePropNone;
                prop.label = singleDimLabel(knob, dim);
                out->append(prop);
            }
        } else {
            // Multiple animated dimensions → grouped row
            FluxKeyframeProperty prop;
            prop.ownerKind = ownerKind;
            prop.ownerNode = node;
            prop.knob = knob;
            prop.dims = animatedDims;
            prop.groupKey = groupingKey;
            prop.groupDimCount = groupDimCount;
            prop.isGrouped = true;
            prop.isRotoAggregate = false;
            prop.flags = eFluxKeyframePropNone;
            prop.label = groupedDimLabel(knob);
            out->append(prop);
        }
    }
}

} // anonymous namespace

// ===========================================================================
// Public API
// ===========================================================================

QList<FluxKeyframeProperty>
buildLayerProperties(const FluxLayer& layer,
                     const std::set<std::string>& ungroupedKeys)
{
    QList<FluxKeyframeProperty> result;
    bool isTextLayer = (layer.type == QString::fromUtf8("text"));
    collectKnobProperties(layer.gizmoNode, eFluxKeyframeOwnerLayer,
                          isTextLayer, false, ungroupedKeys, &result);
    return result;
}

QList<FluxKeyframeProperty>
buildEffectProperties(const FluxEffect& effect,
                      bool ownerIsAdjustment,
                      const std::set<std::string>& ungroupedKeys)
{
    QList<FluxKeyframeProperty> result;
    if (!effect.node) {
        return result;
    }
    // Effects are not text layers — pass false for isTextLayer.
    collectKnobProperties(effect.node, eFluxKeyframeOwnerEffect,
                          false, ownerIsAdjustment, ungroupedKeys, &result);
    return result;
}

std::string
keyframeGroupingKey(const NodePtr& node,
                    const KnobIPtr& knob)
{
    if (!node || !knob) {
        return std::string();
    }
    return node->getFullyQualifiedName() + ":" + knob->getName();
}

QList<FluxKeyframeProperty>
buildMaskProperties(const FluxMask& mask)
{
    QList<FluxKeyframeProperty> result;
    if (!mask.maskNode) {
        return result;
    }

    RotoContextPtr rotoCtx = mask.maskNode->getRotoContext();
    if (!rotoCtx) {
        return result;
    }

    // Check if there are any bezier keyframes
    std::list<double> shapeTimes;
    rotoCtx->getBeziersKeyframeTimes(&shapeTimes);
    if (shapeTimes.empty()) {
        return result;
    }

    FluxKeyframeProperty prop;
    prop.ownerKind = eFluxKeyframeOwnerMask;
    prop.ownerNode = mask.maskNode;
    prop.isRotoAggregate = true;
    prop.groupDimCount = 1;
    prop.label = QObject::tr("Mask Shapes");
    result.append(prop);

    return result;
}

QList<FluxKeyframeKey>
keysForProperty(const FluxKeyframeProperty& property)
{
    QList<FluxKeyframeKey> result;

    if (property.isRotoAggregate) {
        // Roto aggregate: gather all bezier shape keyframe times
        if (!property.ownerNode) {
            return result;
        }
        RotoContextPtr rotoCtx = property.ownerNode->getRotoContext();
        if (!rotoCtx) {
            return result;
        }
        std::list<double> shapeTimes;
        rotoCtx->getBeziersKeyframeTimes(&shapeTimes);
        // Deduplicate
        std::set<double> uniqueTimes;
        for (double t : shapeTimes) {
            uniqueTimes.insert(t);
        }
        for (double t : uniqueTimes) {
            FluxKeyframeKey key;
            key.time = t;
            key.isPartial = false;
            key.animatedDimCount = 1;
            key.totalAnimatedDimCount = 1;
            result.append(key);
        }
        return result;
    }

    // Knob-backed property
    if (!property.knob) {
        return result;
    }

    const std::vector<int>& dims = property.dims;
    if (dims.empty()) {
        return result;
    }

    // Gather keyframe times from each animated dimension
    std::set<double> allTimes;
    for (int d : dims) {
        std::shared_ptr<Curve> curve = property.knob->getCurve(ViewSpec::current(), d);
        if (!curve) {
            continue;
        }
        KeyFrameSet keyFrames = curve->getKeyFrames_mt_safe();
        for (const KeyFrame& kf : keyFrames) {
            allTimes.insert(kf.getTime());
        }
    }

    int totalAnimatedDims = static_cast<int>(dims.size());

    // Build key entries with partial/full info
    for (double t : allTimes) {
        FluxKeyframeKey key;
        key.time = t;
        key.totalAnimatedDimCount = totalAnimatedDims;

        int dimsWithKey = 0;
        for (int d : dims) {
            std::shared_ptr<Curve> curve = property.knob->getCurve(ViewSpec::current(), d);
            if (!curve) {
                continue;
            }
            KeyFrame kf;
            if (curve->getKeyFrameWithTime(t, &kf)) {
                ++dimsWithKey;
            }
        }
        key.animatedDimCount = dimsWithKey;
        key.isPartial = (dimsWithKey < totalAnimatedDims);
        result.append(key);
    }

    return result;
}

bool
propertyHasKeysAtTime(const FluxKeyframeProperty& property,
                      double time)
{
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
        for (double t : shapeTimes) {
            if (std::abs(t - time) < 0.5) {
                return true;
            }
        }
        return false;
    }

    if (!property.knob) {
        return false;
    }

    // For a grouped property, check if any animated dim has a key at this time
    for (int d : property.dims) {
        std::shared_ptr<Curve> curve = property.knob->getCurve(ViewSpec::current(), d);
        if (!curve) {
            continue;
        }
        KeyFrame kf;
        if (curve->getKeyFrameWithTime(time, &kf)) {
            return true;
        }
    }
    return false;
}

NATRON_NAMESPACE_EXIT
