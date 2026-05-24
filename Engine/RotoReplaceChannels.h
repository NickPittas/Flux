/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of Natron <https://natrongithub.github.io/>,
 * (C) 2018-2023 The Natron developers
 * (C) 2013-2018 INRIA and Alexandre Gauthier-Foichat
 *
 * Natron is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Natron is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Natron.  If not, see <http://www.gnu.org/licenses/gpl-2.0.html>
 * ***** END LICENSE BLOCK ***** */

#ifndef ROTOREPLACECHANNELS_H
#define ROTOREPLACECHANNELS_H

// ***** BEGIN PYTHON BLOCK *****
// from <https://docs.python.org/3/c-api/intro.html>:
// "Since Python may define some pre-processor definitions which affect the standard headers on some systems, you must include Python.h before any standard headers are included."
#include <Python.h>
// ***** END PYTHON BLOCK *****

#include "Global/Macros.h"

#include "Engine/EffectInstance.h"
#include "Engine/ViewIdx.h"
#include "Engine/EngineFwd.h"

NATRON_NAMESPACE_ENTER

struct RotoReplaceChannelsPrivate;
class RotoReplaceChannels
    : public EffectInstance
{
public:

    static EffectInstance* BuildEffect(NodePtr n)
    {
        return new RotoReplaceChannels(n);
    }

    RotoReplaceChannels(NodePtr node);

    virtual ~RotoReplaceChannels();

    virtual int getMajorVersion() const OVERRIDE FINAL WARN_UNUSED_RETURN
    {
        return 1;
    }

    virtual int getMinorVersion() const OVERRIDE FINAL WARN_UNUSED_RETURN
    {
        return 0;
    }

    virtual int getNInputs() const OVERRIDE FINAL WARN_UNUSED_RETURN
    {
        return 1;
    }

    virtual bool getCanTransform() const OVERRIDE FINAL WARN_UNUSED_RETURN { return false; }

    virtual std::string getPluginID() const OVERRIDE FINAL WARN_UNUSED_RETURN
    {
        return PLUGINID_NATRON_ROTO_REPLACE_CHANNELS;
    }

    virtual std::string getPluginLabel() const OVERRIDE FINAL WARN_UNUSED_RETURN
    {
        return "RotoReplaceChannels";
    }

    virtual std::string getPluginDescription() const OVERRIDE FINAL WARN_UNUSED_RETURN { return std::string(); }

    virtual void getPluginGrouping(std::list<std::string>* grouping) const OVERRIDE FINAL
    {
        grouping->push_back(PLUGIN_GROUP_PAINT);
    }

    virtual std::string getInputLabel (int /*inputNb*/) const OVERRIDE FINAL WARN_UNUSED_RETURN
    {
        return "Source";
    }

    virtual bool isInputOptional(int /*inputNb*/) const OVERRIDE FINAL WARN_UNUSED_RETURN
    {
        return true;
    }

    virtual void addAcceptedComponents(int inputNb, std::list<ImagePlaneDesc>* comps) OVERRIDE FINAL;
    virtual void addSupportedBitDepth(std::list<ImageBitDepthEnum>* depths) const OVERRIDE FINAL;

    virtual RenderSafetyEnum renderThreadSafety() const OVERRIDE FINAL WARN_UNUSED_RETURN
    {
        return eRenderSafetyFullySafe;
    }

    virtual bool supportsTiles() const OVERRIDE FINAL WARN_UNUSED_RETURN
    {
        return true;
    }

    virtual bool supportsMultiResolution() const OVERRIDE FINAL WARN_UNUSED_RETURN
    {
        return true;
    }

    virtual bool isOutput() const OVERRIDE FINAL WARN_UNUSED_RETURN
    {
        return false;
    }

    virtual void initializeKnobs() OVERRIDE FINAL;

private:

    virtual bool isIdentity(double time,
                            const RenderScale & scale,
                            const RectI & roi,
                            ViewIdx view,
                            double* inputTime,
                            ViewIdx* inputView,
                            int* inputNb) OVERRIDE FINAL WARN_UNUSED_RETURN;
    virtual StatusEnum render(const RenderActionArgs& args) OVERRIDE WARN_UNUSED_RETURN;
    std::unique_ptr<RotoReplaceChannelsPrivate> _imp;
};

NATRON_NAMESPACE_EXIT

#endif // ROTOREPLACECHANNELS_H
