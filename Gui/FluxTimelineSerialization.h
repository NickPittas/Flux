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

    FluxEffectSerialization()
        : pluginId()
        , label()
        , nodeScriptName()
        , enabled(true)
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

    FluxMaskSerialization()
        : type("layer")
        , enabled(true)
        , inverted(false)
        , effectIndex(-1)
    {}

    friend class ::boost::serialization::access;
    template<class Archive>
    void serialize(Archive & ar, const unsigned int /*version*/)
    {
        ar & ::boost::serialization::make_nvp("Name", name);
        ar & ::boost::serialization::make_nvp("Type", type);
        ar & ::boost::serialization::make_nvp("PluginId", pluginId);
        ar & ::boost::serialization::make_nvp("Enabled", enabled);
        ar & ::boost::serialization::make_nvp("Inverted", inverted);
        ar & ::boost::serialization::make_nvp("EffectIndex", effectIndex);
        ar & ::boost::serialization::make_nvp("MaskNode", maskNodeScriptName);
        ar & ::boost::serialization::make_nvp("ReformatNode", reformatNodeScriptName);
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
BOOST_CLASS_VERSION(NATRON_NAMESPACE::FluxLayerSerialization, 2)
BOOST_CLASS_VERSION(NATRON_NAMESPACE::FluxMaskSerialization, 1)

#endif // FLUXTIMELINESERIALIZATION_H
