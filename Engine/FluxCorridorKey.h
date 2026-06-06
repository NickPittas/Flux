/* ***** BEGIN LICENSE BLOCK *****
 * Flux — Native CorridorKey node shell
 * (C) 2026 Nick Pittas
 * GPL2 — see LICENSE.txt
 * ***** END LICENSE BLOCK ***** */

#ifndef FLUXCORRIDORKEY_H
#define FLUXCORRIDORKEY_H

#include <Python.h>

#include "Global/Macros.h"

#include "Engine/EffectInstance.h"
#include "Engine/EngineFwd.h"
#include "Engine/ViewIdx.h"

NATRON_NAMESPACE_ENTER

struct FluxCorridorKeyPrivate;

class FluxCorridorKey
    : public EffectInstance
{
public:
    static EffectInstance* BuildEffect(NodePtr n) { return new FluxCorridorKey(n); }

    FluxCorridorKey(NodePtr node);
    virtual ~FluxCorridorKey();

    virtual int getMajorVersion() const OVERRIDE FINAL WARN_UNUSED_RETURN { return 1; }
    virtual int getMinorVersion() const OVERRIDE FINAL WARN_UNUSED_RETURN { return 0; }
    virtual int getNInputs() const OVERRIDE FINAL WARN_UNUSED_RETURN { return 2; }
    virtual bool getCanTransform() const OVERRIDE FINAL WARN_UNUSED_RETURN { return false; }
    virtual std::string getPluginID() const OVERRIDE FINAL WARN_UNUSED_RETURN { return PLUGINID_FLUX_CORRIDOR_KEY; }
    virtual std::string getPluginLabel() const OVERRIDE FINAL WARN_UNUSED_RETURN { return "CorridorKey"; }
    virtual std::string getPluginDescription() const OVERRIDE FINAL WARN_UNUSED_RETURN;
    virtual void getPluginGrouping(std::list<std::string>* grouping) const OVERRIDE FINAL { grouping->push_back("Flux"); }
    virtual std::string getInputLabel(int inputNb) const OVERRIDE FINAL WARN_UNUSED_RETURN { return inputNb == 1 ? "mask" : "plate"; }
    virtual bool isInputOptional(int inputNb) const OVERRIDE FINAL WARN_UNUSED_RETURN { return inputNb == 1; }
    virtual void addAcceptedComponents(int inputNb, std::list<ImagePlaneDesc>* comps) OVERRIDE FINAL;
    virtual void addSupportedBitDepth(std::list<ImageBitDepthEnum>* depths) const OVERRIDE FINAL;
    virtual RenderSafetyEnum renderThreadSafety() const OVERRIDE FINAL WARN_UNUSED_RETURN { return eRenderSafetyInstanceSafe; }
    virtual bool supportsTiles() const OVERRIDE FINAL WARN_UNUSED_RETURN { return false; }
    virtual bool supportsMultiResolution() const OVERRIDE FINAL WARN_UNUSED_RETURN { return false; }
    virtual bool isOutput() const OVERRIDE FINAL WARN_UNUSED_RETURN { return false; }
    virtual void initializeKnobs() OVERRIDE FINAL;

private:
    virtual StatusEnum getRegionOfDefinition(U64 hash,
                                             double time,
                                             const RenderScale& scale,
                                             ViewIdx view,
                                             RectD* rod) OVERRIDE WARN_UNUSED_RETURN;
    virtual StatusEnum render(const RenderActionArgs& args) OVERRIDE WARN_UNUSED_RETURN;

    std::unique_ptr<FluxCorridorKeyPrivate> _imp;
};

NATRON_NAMESPACE_EXIT

#endif // FLUXCORRIDORKEY_H
