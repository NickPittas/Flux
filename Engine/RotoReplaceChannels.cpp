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

// ***** BEGIN PYTHON BLOCK *****
// from <https://docs.python.org/3/c-api/intro.html#include-files>:
// "Since Python may define some pre-processor definitions which affect the standard headers on some systems, you must include Python.h before any standard headers are included."
#include <Python.h>
// ***** END PYTHON BLOCK *****

#include "Global/Macros.h"

#include "Engine/RotoReplaceChannels.h"

#include <list>

#include "Engine/AppManager.h"
#include "Engine/AppInstance.h"
#include "Engine/Image.h"
#include "Engine/ImagePlaneDesc.h"
#include "Engine/KnobTypes.h"
#include "Engine/Node.h"
#include "Engine/Plugin.h"

NATRON_NAMESPACE_ENTER

struct RotoReplaceChannelsPrivate
{
    KnobBoolWPtr replaceEnabledKnob;
    KnobBoolWPtr processRKnob;
    KnobBoolWPtr processGKnob;
    KnobBoolWPtr processBKnob;
    KnobBoolWPtr processAKnob;

    RotoReplaceChannelsPrivate()
        : replaceEnabledKnob()
        , processRKnob()
        , processGKnob()
        , processBKnob()
        , processAKnob()
    {
    }
};

RotoReplaceChannels::RotoReplaceChannels(NodePtr node)
    : EffectInstance(node)
    , _imp(new RotoReplaceChannelsPrivate())
{
}

RotoReplaceChannels::~RotoReplaceChannels()
{
}

void
RotoReplaceChannels::addAcceptedComponents(int inputNb, std::list<ImagePlaneDesc>* comps)
{
    comps->push_back( ImagePlaneDesc::getRGBAComponents() );
    comps->push_back( ImagePlaneDesc::getRGBComponents() );
    comps->push_back( ImagePlaneDesc::getAlphaComponents() );
    comps->push_back( ImagePlaneDesc::getXYComponents() );
}

void
RotoReplaceChannels::addSupportedBitDepth(std::list<ImageBitDepthEnum>* depths) const
{
    depths->push_back(eImageBitDepthFloat);
}

void
RotoReplaceChannels::initializeKnobs()
{
    KnobBoolPtr replaceKnob = AppManager::createKnob<KnobBool>(this, tr("Replace"), 1, false);
    replaceKnob->setName("replaceSelectedChannels");
    replaceKnob->setDefaultValue(false);
    replaceKnob->setAnimationEnabled(false);
    replaceKnob->setHintToolTip( tr("When enabled, selected channels are zeroed from the input image.") );
    _imp->replaceEnabledKnob = replaceKnob;

    KnobBoolPtr rKnob = AppManager::createKnob<KnobBool>(this, tr("R"), 1, false);
    rKnob->setName("processR");
    rKnob->setAnimationEnabled(false);
    rKnob->setAddNewLine(false);
    rKnob->setDefaultValue(false);
    _imp->processRKnob = rKnob;

    KnobBoolPtr gKnob = AppManager::createKnob<KnobBool>(this, tr("G"), 1, false);
    gKnob->setName("processG");
    gKnob->setAnimationEnabled(false);
    gKnob->setAddNewLine(false);
    gKnob->setDefaultValue(false);
    _imp->processGKnob = gKnob;

    KnobBoolPtr bKnob = AppManager::createKnob<KnobBool>(this, tr("B"), 1, false);
    bKnob->setName("processB");
    bKnob->setAnimationEnabled(false);
    bKnob->setAddNewLine(false);
    bKnob->setDefaultValue(false);
    _imp->processBKnob = bKnob;

    KnobBoolPtr aKnob = AppManager::createKnob<KnobBool>(this, tr("A"), 1, false);
    aKnob->setName("processA");
    aKnob->setAnimationEnabled(false);
    aKnob->setAddNewLine(true);
    aKnob->setDefaultValue(false);
    _imp->processAKnob = aKnob;
}

bool
RotoReplaceChannels::isIdentity(double time,
                                const RenderScale & /*scale*/,
                                const RectI & /*roi*/,
                                ViewIdx view,
                                double* inputTime,
                                ViewIdx* inputView,
                                int* inputNb)
{
    KnobBoolPtr replaceKnob = _imp->replaceEnabledKnob.lock();
    if (!replaceKnob || !replaceKnob->getValue()) {
        *inputTime = time;
        *inputView = view;
        *inputNb = 0;
        return true;
    }

    bool processR = _imp->processRKnob.lock() && _imp->processRKnob.lock()->getValue();
    bool processG = _imp->processGKnob.lock() && _imp->processGKnob.lock()->getValue();
    bool processB = _imp->processBKnob.lock() && _imp->processBKnob.lock()->getValue();
    bool processA = _imp->processAKnob.lock() && _imp->processAKnob.lock()->getValue();

    if (!processR && !processG && !processB && !processA) {
        *inputTime = time;
        *inputView = view;
        *inputNb = 0;
        return true;
    }

    return false;
}

StatusEnum
RotoReplaceChannels::render(const RenderActionArgs& args)
{
    const RectI& roi = args.roi;

    for (std::list<std::pair<ImagePlaneDesc, ImagePtr> >::const_iterator it = args.outputPlanes.begin();
         it != args.outputPlanes.end(); ++it) {
        const ImagePtr& outputImg = it->second;

        ImagePtr inputImg;
        if (!args.inputImages.empty()) {
            EffectInstance::InputImagesMap::const_iterator inputIt = args.inputImages.find(0);
            if (inputIt != args.inputImages.end() && !inputIt->second.empty()) {
                inputImg = inputIt->second.front();
            }
        }

        if (!inputImg) {
            outputImg->fillZero(roi);
            continue;
        }

        if ( inputImg->getComponents() != outputImg->getComponents() ) {
            inputImg->convertToFormat(roi,
                                      getApp()->getDefaultColorSpaceForBitDepth( inputImg->getBitDepth() ),
                                      getApp()->getDefaultColorSpaceForBitDepth( outputImg->getBitDepth() ), 3,
                                      false, false, outputImg.get() );
        } else {
            outputImg->pasteFrom(*inputImg, roi, false);
        }

        KnobBoolPtr replaceKnob = _imp->replaceEnabledKnob.lock();
        if (replaceKnob && replaceKnob->getValue()) {
            bool processR = _imp->processRKnob.lock() && _imp->processRKnob.lock()->getValue();
            bool processG = _imp->processGKnob.lock() && _imp->processGKnob.lock()->getValue();
            bool processB = _imp->processBKnob.lock() && _imp->processBKnob.lock()->getValue();
            bool processA = _imp->processAKnob.lock() && _imp->processAKnob.lock()->getValue();

            if (processR || processG || processB || processA) {
                const ImagePlaneDesc& comps = outputImg->getComponents();
                int nComps = outputImg->getComponentsCount();

                RectI intersection = roi.intersect(outputImg->getBounds());
                if (!intersection.isNull()) {
                    Image::WriteAccess writeAccess(outputImg.get());
                    for (int y = intersection.y1; y < intersection.y2; ++y) {
                        float* pix = (float*)writeAccess.pixelAt(intersection.x1, y);
                        for (int x = intersection.x1; x < intersection.x2; ++x) {
                            if (comps == ImagePlaneDesc::getAlphaComponents()) {
                                if (processA) { pix[0] = 0.f; }
                            } else if (comps == ImagePlaneDesc::getXYComponents()) {
                                if (processR) { pix[0] = 0.f; }
                                if (processG && nComps > 1) { pix[1] = 0.f; }
                            } else {
                                if (processR && nComps > 0) { pix[0] = 0.f; }
                                if (processG && nComps > 1) { pix[1] = 0.f; }
                                if (processB && nComps > 2) { pix[2] = 0.f; }
                                if (processA && nComps > 3) { pix[3] = 0.f; }
                            }
                            pix += nComps;
                        }
                    }
                }
            }
        }
    }

    return eStatusOK;
}

NATRON_NAMESPACE_EXIT
