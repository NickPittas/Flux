/* ***** BEGIN LICENSE BLOCK *****
 * Flux — Timeline Serialization
 * (C) 2025 Nick Pittas
 * GPL2 — see LICENSE.txt
 * ***** END LICENSE BLOCK ***** */

#ifndef FLUXTIMELINESERIALIZATION_H
#define FLUXTIMELINESERIALIZATION_H

// ***** BEGIN PYTHON BLOCK *****
#include <Python.h>
// ***** END PYTHON BLOCK *****

#include "Global/Macros.h"

#include <string>
#include <vector>

#if !defined(Q_MOC_RUN) && !defined(SBK_RUN)
GCC_DIAG_UNUSED_LOCAL_TYPEDEFS_OFF
// clang-format off
GCC_DIAG_OFF(unused-parameter)
#include <boost/archive/xml_iarchive.hpp>
#include <boost/archive/xml_oarchive.hpp>
#include <boost/serialization/vector.hpp>
#include <boost/serialization/string.hpp>
#include <boost/serialization/version.hpp>
GCC_DIAG_UNUSED_LOCAL_TYPEDEFS_ON
GCC_DIAG_ON(unused-parameter)
// clang-format on
#endif

NATRON_NAMESPACE_ENTER

struct FluxEffectSerialization
{
    std::string pluginId;
    std::string label;
    std::string nodeScriptName;
    bool enabled;
    bool isAIMaskCopy;
    std::string aiMaskUsage;
    std::string aiMaskTargetPlane;
    std::string aiMaskSourceChannel;
    std::string aiMaskOperation;
    std::string aiMaskSourceRelativePath;
    std::string aiMaskManifestRelativePath;
    int aiMaskBaseTimeOffset;
    std::string aiMaskReadNodeScriptName;
    std::string aiMaskTimeOffsetNodeScriptName;
    std::string aiMaskShuffleNodeScriptName;
    std::string aiMaskChannelMergeNodeScriptName;

    FluxEffectSerialization()
        : pluginId()
        , label()
        , nodeScriptName()
        , enabled(true)
        , isAIMaskCopy(false)
        , aiMaskSourceChannel("red")
        , aiMaskOperation("max")
        , aiMaskBaseTimeOffset(0)
    {}

    friend class ::boost::serialization::access;
    template<class Archive>
    void serialize(Archive & ar, const unsigned int /*version*/)
    {
        ar & ::boost::serialization::make_nvp("PluginId", pluginId);
        ar & ::boost::serialization::make_nvp("Label", label);
        ar & ::boost::serialization::make_nvp("NodeScriptName", nodeScriptName);
        ar & ::boost::serialization::make_nvp("Enabled", enabled);
    }
};

struct FluxEffectAIMaskSerialization
{
    int effectIndex;
    bool isAIMaskCopy;
    std::string aiMaskUsage;
    std::string aiMaskTargetPlane;
    std::string aiMaskSourceChannel;
    std::string aiMaskOperation;
    std::string aiMaskSourceRelativePath;
    std::string aiMaskManifestRelativePath;
    int aiMaskBaseTimeOffset;
    std::string aiMaskReadNodeScriptName;
    std::string aiMaskTimeOffsetNodeScriptName;
    std::string aiMaskShuffleNodeScriptName;
    std::string aiMaskChannelMergeNodeScriptName;

    FluxEffectAIMaskSerialization()
        : effectIndex(-1)
        , isAIMaskCopy(false)
        , aiMaskSourceChannel("red")
        , aiMaskOperation("max")
        , aiMaskBaseTimeOffset(0)
    {}

    friend class ::boost::serialization::access;
    template<class Archive>
    void serialize(Archive & ar, const unsigned int version)
    {
        ar & ::boost::serialization::make_nvp("EffectIndex", effectIndex);
        ar & ::boost::serialization::make_nvp("IsAIMaskCopy", isAIMaskCopy);
        ar & ::boost::serialization::make_nvp("AIMaskTargetPlane", aiMaskTargetPlane);
        ar & ::boost::serialization::make_nvp("AIMaskSourceRelativePath", aiMaskSourceRelativePath);
        ar & ::boost::serialization::make_nvp("AIMaskManifestRelativePath", aiMaskManifestRelativePath);
        ar & ::boost::serialization::make_nvp("AIMaskReadNode", aiMaskReadNodeScriptName);
        if (version >= 1) {
            ar & ::boost::serialization::make_nvp("AIMaskUsage", aiMaskUsage);
            ar & ::boost::serialization::make_nvp("AIMaskSourceChannel", aiMaskSourceChannel);
            ar & ::boost::serialization::make_nvp("AIMaskOperation", aiMaskOperation);
            ar & ::boost::serialization::make_nvp("AIMaskShuffleNode", aiMaskShuffleNodeScriptName);
            ar & ::boost::serialization::make_nvp("AIMaskChannelMergeNode", aiMaskChannelMergeNodeScriptName);
        }
        if (version >= 2) {
            ar & ::boost::serialization::make_nvp("AIMaskBaseTimeOffset", aiMaskBaseTimeOffset);
        }
        if (version >= 3) {
            ar & ::boost::serialization::make_nvp("AIMaskTimeOffsetNode", aiMaskTimeOffsetNodeScriptName);
        }
    }
};

struct FluxMaskSerialization
{
    std::string name;
    std::string type;
    std::string pluginId;
    bool enabled;
    bool inverted;
    int effectIndex;
    std::string maskNodeScriptName;
    std::string reformatNodeScriptName;
    // Legacy T083 external-AI-mask fields. Kept only so projects saved during
    // the removed layer-mask Apply experiment can be consumed without
    // resurrecting that graph path.
    std::string legacyExternalMaskPathProjectRelative;
    std::string legacyExternalReadNodeScriptName;
    std::string legacyExternalShuffleNodeScriptName;
    bool legacyPremultiplyAlpha;

    FluxMaskSerialization()
        : type("layer")
        , enabled(true)
        , inverted(false)
        , effectIndex(-1)
        , legacyPremultiplyAlpha(true)
    {}

    friend class ::boost::serialization::access;
    template<class Archive>
    void serialize(Archive & ar, const unsigned int version)
    {
        ar & ::boost::serialization::make_nvp("Name", name);
        ar & ::boost::serialization::make_nvp("Type", type);
        ar & ::boost::serialization::make_nvp("PluginId", pluginId);
        ar & ::boost::serialization::make_nvp("Enabled", enabled);
        ar & ::boost::serialization::make_nvp("Inverted", inverted);
        ar & ::boost::serialization::make_nvp("EffectIndex", effectIndex);
        ar & ::boost::serialization::make_nvp("MaskNode", maskNodeScriptName);
        ar & ::boost::serialization::make_nvp("ReformatNode", reformatNodeScriptName);
        if (version >= 2) {
            ar & ::boost::serialization::make_nvp("ExternalMaskPathProjectRelative", legacyExternalMaskPathProjectRelative);
            ar & ::boost::serialization::make_nvp("ExternalReadNode", legacyExternalReadNodeScriptName);
            ar & ::boost::serialization::make_nvp("ExternalShuffleNode", legacyExternalShuffleNodeScriptName);
        }
        if (version >= 3) {
            ar & ::boost::serialization::make_nvp("PremultiplyAlpha", legacyPremultiplyAlpha);
        }
    }
};

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

struct FluxAnimatorPropertySerialization
{
    double value;
    std::vector<FluxAnimatorKeyframeSerialization> keyframes;

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
    FluxAnimatorPropertySerialization rotation;
    FluxAnimatorPropertySerialization opacity;
    FluxAnimatorPropertySerialization tracking;
    std::vector<FluxAnimatorPropertySerialization> position;
    std::vector<FluxAnimatorPropertySerialization> scale;
    std::vector<FluxAnimatorPropertySerialization> fillColor;

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

        int numPos = (int)position.size();
        ar & ::boost::serialization::make_nvp("NumPosition", numPos);
        if (Archive::is_loading::value) { position.resize(numPos); }
        for (int i = 0; i < numPos; ++i) {
            ar & ::boost::serialization::make_nvp("Pos", position[i]);
        }

        int numScale = (int)scale.size();
        ar & ::boost::serialization::make_nvp("NumScale", numScale);
        if (Archive::is_loading::value) { scale.resize(numScale); }
        for (int i = 0; i < numScale; ++i) {
            ar & ::boost::serialization::make_nvp("Scl", scale[i]);
        }

        int numFill = (int)fillColor.size();
        ar & ::boost::serialization::make_nvp("NumFillColor", numFill);
        if (Archive::is_loading::value) { fillColor.resize(numFill); }
        for (int i = 0; i < numFill; ++i) {
            ar & ::boost::serialization::make_nvp("Fill", fillColor[i]);
        }

        ar & ::boost::serialization::make_nvp("ScaleSeparated", scaleSeparated);
    }
};

struct FluxLayerSerialization
{
    // Identity
    std::string name;
    std::string filePath;
    std::string type; // "footage", "solid", "text", "adjustment", "null"

    // State
    bool muted;
    bool locked;
    bool solo;

    // Timing
    int inPoint;
    int outPoint;
    int originalInPoint;
    int originalOutPoint;
    int originalFirstFrame;
    int originalLastFrame;
    int timeOffset;
    int trimStart;
    int trimEnd;
    bool nodeInitialized;
    double sourceFrameRate;

    // Solid color (int 0-255)
    int solidColorR, solidColorG, solidColorB;

    // Parenting
    int parentLayerIndex;

    // Bar color (int 0-255)
    int colorR, colorG, colorB;

    // Node references (script names)
    std::string readerNodeScriptName;
    std::string gizmoNodeScriptName;
    std::string mergeNodeScriptName;

    // Child effects
    std::vector<FluxEffectSerialization> effects;

    // Masks
    bool hasPrecompBranch;
    std::string maskApplyNodeScriptName;
    std::vector<FluxMaskSerialization> masks;

    // Text animator data (version 3+)
    std::vector<FluxAnimatorSerialization> animators;

    // Expanded state (version 2)
    bool expanded;

    FluxLayerSerialization()
        : name()
        , filePath()
        , type("footage")
        , muted(false)
        , locked(false)
        , solo(false)
        , inPoint(0)
        , outPoint(100)
        , originalInPoint(0)
        , originalOutPoint(100)
        , originalFirstFrame(0)
        , originalLastFrame(100)
        , timeOffset(0)
        , trimStart(0)
        , trimEnd(0)
        , nodeInitialized(true)
        , sourceFrameRate(0.0)
        , solidColorR(128)
        , solidColorG(128)
        , solidColorB(128)
        , parentLayerIndex(-1)
        , colorR(80)
        , colorG(130)
        , colorB(200)
        , readerNodeScriptName()
        , gizmoNodeScriptName()
        , mergeNodeScriptName()
        , effects()
        , hasPrecompBranch(false)
        , maskApplyNodeScriptName()
        , masks()
        , expanded(false)
    {}

    friend class ::boost::serialization::access;
    template<class Archive>
    void serialize(Archive & ar, const unsigned int version)
    {
        ar & ::boost::serialization::make_nvp("Name", name);
        ar & ::boost::serialization::make_nvp("FilePath", filePath);
        ar & ::boost::serialization::make_nvp("Type", type);
        ar & ::boost::serialization::make_nvp("Muted", muted);
        ar & ::boost::serialization::make_nvp("Locked", locked);
        ar & ::boost::serialization::make_nvp("Solo", solo);
        ar & ::boost::serialization::make_nvp("InPoint", inPoint);
        ar & ::boost::serialization::make_nvp("OutPoint", outPoint);
        ar & ::boost::serialization::make_nvp("OriginalInPoint", originalInPoint);
        ar & ::boost::serialization::make_nvp("OriginalOutPoint", originalOutPoint);
        ar & ::boost::serialization::make_nvp("OriginalFirstFrame", originalFirstFrame);
        ar & ::boost::serialization::make_nvp("OriginalLastFrame", originalLastFrame);
        ar & ::boost::serialization::make_nvp("TimeOffset", timeOffset);
        ar & ::boost::serialization::make_nvp("TrimStart", trimStart);
        ar & ::boost::serialization::make_nvp("TrimEnd", trimEnd);
        ar & ::boost::serialization::make_nvp("NodeInitialized", nodeInitialized);
        ar & ::boost::serialization::make_nvp("SolidColorR", solidColorR);
        ar & ::boost::serialization::make_nvp("SolidColorG", solidColorG);
        ar & ::boost::serialization::make_nvp("SolidColorB", solidColorB);
        ar & ::boost::serialization::make_nvp("ParentLayerIndex", parentLayerIndex);
        ar & ::boost::serialization::make_nvp("ColorR", colorR);
        ar & ::boost::serialization::make_nvp("ColorG", colorG);
        ar & ::boost::serialization::make_nvp("ColorB", colorB);
        ar & ::boost::serialization::make_nvp("ReaderNodeScriptName", readerNodeScriptName);
        ar & ::boost::serialization::make_nvp("GizmoNodeScriptName", gizmoNodeScriptName);
        ar & ::boost::serialization::make_nvp("MergeNodeScriptName", mergeNodeScriptName);

        int numEffects = (int)effects.size();
        ar & ::boost::serialization::make_nvp("NumEffects", numEffects);
        if (Archive::is_loading::value) {
            effects.resize(numEffects);
        }
        for (int i = 0; i < numEffects; ++i) {
            ar & ::boost::serialization::make_nvp("Effect", effects[i]);
        }

        if (version >= 4) {
            int numAIMaskMetadata = (int)effects.size();
            ar & ::boost::serialization::make_nvp("NumEffectAIMaskMetadata", numAIMaskMetadata);
            if (Archive::is_loading::value) {
                for (int i = 0; i < numAIMaskMetadata; ++i) {
                    FluxEffectAIMaskSerialization aiMeta;
                    ar & ::boost::serialization::make_nvp("EffectAIMask", aiMeta);
                    if (aiMeta.effectIndex >= 0 && aiMeta.effectIndex < (int)effects.size()) {
                        FluxEffectSerialization& effect = effects[aiMeta.effectIndex];
                        effect.isAIMaskCopy = aiMeta.isAIMaskCopy;
                        effect.aiMaskUsage = aiMeta.aiMaskUsage;
                        effect.aiMaskTargetPlane = aiMeta.aiMaskTargetPlane;
                        effect.aiMaskSourceChannel = aiMeta.aiMaskSourceChannel;
                        effect.aiMaskOperation = aiMeta.aiMaskOperation;
                        effect.aiMaskSourceRelativePath = aiMeta.aiMaskSourceRelativePath;
                        effect.aiMaskManifestRelativePath = aiMeta.aiMaskManifestRelativePath;
                        effect.aiMaskBaseTimeOffset = aiMeta.aiMaskBaseTimeOffset;
                        effect.aiMaskReadNodeScriptName = aiMeta.aiMaskReadNodeScriptName;
                        effect.aiMaskTimeOffsetNodeScriptName = aiMeta.aiMaskTimeOffsetNodeScriptName;
                        effect.aiMaskShuffleNodeScriptName = aiMeta.aiMaskShuffleNodeScriptName;
                        effect.aiMaskChannelMergeNodeScriptName = aiMeta.aiMaskChannelMergeNodeScriptName;
                    }
                }
            } else {
                for (int i = 0; i < numAIMaskMetadata; ++i) {
                    FluxEffectAIMaskSerialization aiMeta;
                    aiMeta.effectIndex = i;
                    aiMeta.isAIMaskCopy = effects[i].isAIMaskCopy;
                    aiMeta.aiMaskUsage = effects[i].aiMaskUsage;
                    aiMeta.aiMaskTargetPlane = effects[i].aiMaskTargetPlane;
                    aiMeta.aiMaskSourceChannel = effects[i].aiMaskSourceChannel;
                    aiMeta.aiMaskOperation = effects[i].aiMaskOperation;
                    aiMeta.aiMaskSourceRelativePath = effects[i].aiMaskSourceRelativePath;
                    aiMeta.aiMaskManifestRelativePath = effects[i].aiMaskManifestRelativePath;
                    aiMeta.aiMaskBaseTimeOffset = effects[i].aiMaskBaseTimeOffset;
                    aiMeta.aiMaskReadNodeScriptName = effects[i].aiMaskReadNodeScriptName;
                    aiMeta.aiMaskTimeOffsetNodeScriptName = effects[i].aiMaskTimeOffsetNodeScriptName;
                    aiMeta.aiMaskShuffleNodeScriptName = effects[i].aiMaskShuffleNodeScriptName;
                    aiMeta.aiMaskChannelMergeNodeScriptName = effects[i].aiMaskChannelMergeNodeScriptName;
                    ar & ::boost::serialization::make_nvp("EffectAIMask", aiMeta);
                }
            }
        }

        if (version >= 5) {
            ar & ::boost::serialization::make_nvp("SourceFrameRate", sourceFrameRate);
        }

        if (version >= 1) {
            ar & ::boost::serialization::make_nvp("HasPrecompBranch", hasPrecompBranch);
            ar & ::boost::serialization::make_nvp("MaskApplyNodeScriptName", maskApplyNodeScriptName);

            int numMasks = (int)masks.size();
            ar & ::boost::serialization::make_nvp("NumMasks", numMasks);
            if (Archive::is_loading::value) {
                masks.resize(numMasks);
            }
            for (int i = 0; i < numMasks; ++i) {
                ar & ::boost::serialization::make_nvp("Mask", masks[i]);
            }
        }
        if (version >= 2) {
            ar & ::boost::serialization::make_nvp("Expanded", expanded);
        }
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
    }
};

struct FluxTimelineSerialization
{
    std::vector<FluxLayerSerialization> layers;
    std::string bgReformatNodeScriptName;
    int selectedLayer;
    bool showKeyframeCurves;
    std::vector<std::string> ungroupedKeyframeProperties;

    FluxTimelineSerialization()
        : layers()
        , bgReformatNodeScriptName()
        , selectedLayer(-1)
        , showKeyframeCurves(false)
        , ungroupedKeyframeProperties()
    {}

    friend class ::boost::serialization::access;
    template<class Archive>
    void serialize(Archive & ar, const unsigned int version)
    {
        int numLayers = (int)layers.size();
        ar & ::boost::serialization::make_nvp("NumLayers", numLayers);
        if (Archive::is_loading::value) {
            layers.resize(numLayers);
        }
        for (int i = 0; i < numLayers; ++i) {
            ar & ::boost::serialization::make_nvp("Layer", layers[i]);
        }
        ar & ::boost::serialization::make_nvp("BgReformatNodeScriptName", bgReformatNodeScriptName);
        ar & ::boost::serialization::make_nvp("SelectedLayer", selectedLayer);
        if (version >= 2) {
            ar & ::boost::serialization::make_nvp("ShowKeyframeCurves", showKeyframeCurves);

            int numUngrouped = (int)ungroupedKeyframeProperties.size();
            ar & ::boost::serialization::make_nvp("NumUngroupedKeyframeProperties", numUngrouped);
            if (Archive::is_loading::value) {
                ungroupedKeyframeProperties.resize(numUngrouped);
            }
            for (int i = 0; i < numUngrouped; ++i) {
                ar & ::boost::serialization::make_nvp("UngroupedKeyframeProperty", ungroupedKeyframeProperties[i]);
            }
        }
    }
};

NATRON_NAMESPACE_EXIT

BOOST_CLASS_VERSION(NATRON_NAMESPACE::FluxTimelineSerialization, 2)
BOOST_CLASS_VERSION(NATRON_NAMESPACE::FluxLayerSerialization, 5)
BOOST_CLASS_VERSION(NATRON_NAMESPACE::FluxMaskSerialization, 3)
BOOST_CLASS_VERSION(NATRON_NAMESPACE::FluxEffectAIMaskSerialization, 3)

#endif // FLUXTIMELINESERIALIZATION_H
