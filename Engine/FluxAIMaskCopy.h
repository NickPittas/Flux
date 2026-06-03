/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of Natron <https://natrongithub.github.io/>
 * ***** END LICENSE BLOCK ***** */

#ifndef FLUXAIMASKCOPY_H
#define FLUXAIMASKCOPY_H

#include <Python.h>

#include "Global/Macros.h"
#include "Engine/EffectInstance.h"
#include "Engine/ViewIdx.h"
#include "Engine/EngineFwd.h"

NATRON_NAMESPACE_ENTER

struct FluxAIMaskCopyPrivate;
class FluxAIMaskCopy : public EffectInstance
{
public:
    static EffectInstance* BuildEffect(NodePtr n) { return new FluxAIMaskCopy(n); }
    FluxAIMaskCopy(NodePtr node);
    virtual ~FluxAIMaskCopy();
    virtual int getMajorVersion() const OVERRIDE FINAL WARN_UNUSED_RETURN { return 1; }
    virtual int getMinorVersion() const OVERRIDE FINAL WARN_UNUSED_RETURN { return 0; }
    virtual int getNInputs() const OVERRIDE FINAL WARN_UNUSED_RETURN { return 2; }
    virtual bool getCanTransform() const OVERRIDE FINAL WARN_UNUSED_RETURN { return false; }
    virtual bool getCreateChannelSelectorKnob() const OVERRIDE FINAL WARN_UNUSED_RETURN { return true; }
    virtual std::string getPluginID() const OVERRIDE FINAL WARN_UNUSED_RETURN { return PLUGINID_FLUX_AI_MASK_COPY; }
    virtual std::string getPluginLabel() const OVERRIDE FINAL WARN_UNUSED_RETURN { return "Flux Custom Plane Copy"; }
    virtual std::string getPluginDescription() const OVERRIDE FINAL WARN_UNUSED_RETURN { return "Copies the second input RGBA into a safe custom RGBA plane while passing the layer stream through unchanged. Defaults to ai_mask1 for Flux-managed masks."; }
    virtual void getPluginGrouping(std::list<std::string>* grouping) const OVERRIDE FINAL { grouping->push_back("Flux"); }
    virtual std::string getInputLabel(int inputNb) const OVERRIDE FINAL WARN_UNUSED_RETURN { return inputNb == 1 ? "AI Mask" : "Layer"; }
    virtual bool isInputOptional(int inputNb) const OVERRIDE FINAL WARN_UNUSED_RETURN { return inputNb == 1; }
    virtual void addAcceptedComponents(int inputNb, std::list<ImagePlaneDesc>* comps) OVERRIDE FINAL;
    virtual void addSupportedBitDepth(std::list<ImageBitDepthEnum>* depths) const OVERRIDE FINAL;
    virtual RenderSafetyEnum renderThreadSafety() const OVERRIDE FINAL WARN_UNUSED_RETURN { return eRenderSafetyFullySafe; }
    virtual bool supportsTiles() const OVERRIDE FINAL WARN_UNUSED_RETURN { return true; }
    virtual bool supportsMultiResolution() const OVERRIDE FINAL WARN_UNUSED_RETURN { return true; }
    virtual bool isOutput() const OVERRIDE FINAL WARN_UNUSED_RETURN { return false; }
    virtual void initializeKnobs() OVERRIDE FINAL;
private:
    virtual StatusEnum render(const RenderActionArgs& args) OVERRIDE WARN_UNUSED_RETURN;
    std::unique_ptr<FluxAIMaskCopyPrivate> _imp;
};

NATRON_NAMESPACE_EXIT

#endif
