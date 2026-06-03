/* ***** BEGIN LICENSE BLOCK *****
 * Flux — Native channel merge node
 * (C) 2026 Nick Pittas
 * GPL2 — see LICENSE.txt
 * ***** END LICENSE BLOCK ***** */

// ***** BEGIN PYTHON BLOCK *****
#include <Python.h>
// ***** END PYTHON BLOCK *****

#include "Global/Macros.h"

#include "Engine/FluxChannelMerge.h"

#include <algorithm>
#include <cmath>
#include <list>
#include <memory>
#include <stdexcept>

#include "Engine/AppInstance.h"
#include "Engine/AppManager.h"
#include "Engine/ChoiceOption.h"
#include "Engine/Image.h"
#include "Engine/ImagePlaneDesc.h"
#include "Engine/KnobTypes.h"
#include "Engine/Node.h"
#include "Engine/RectD.h"

NATRON_NAMESPACE_ENTER

namespace {

enum FluxCMChannel {
    eFluxCMChannelRed = 0,
    eFluxCMChannelGreen,
    eFluxCMChannelBlue,
    eFluxCMChannelAlpha,
    eFluxCMChannelLuminance
};

enum FluxCMOperation {
    eFluxCMOperationCopy = 0,
    eFluxCMOperationPlus,
    eFluxCMOperationMultiply,
    eFluxCMOperationScreen,
    eFluxCMOperationMax,
    eFluxCMOperationMin,
    eFluxCMOperationSubtract,
    eFluxCMOperationOverlay
};

enum FluxCMBBox {
    eFluxCMBBoxB = 0,
    eFluxCMBBoxA,
    eFluxCMBBoxUnion,
    eFluxCMBBoxIntersection
};

static std::vector<ChoiceOption>
makeChannelChoices()
{
    std::vector<ChoiceOption> choices;
    choices.push_back(ChoiceOption("red", "Red", ""));
    choices.push_back(ChoiceOption("green", "Green", ""));
    choices.push_back(ChoiceOption("blue", "Blue", ""));
    choices.push_back(ChoiceOption("alpha", "Alpha", ""));
    choices.push_back(ChoiceOption("luminance", "Luminance", ""));
    return choices;
}

static std::vector<ChoiceOption>
makeOutputChannelChoices()
{
    std::vector<ChoiceOption> choices;
    choices.push_back(ChoiceOption("red", "Red", ""));
    choices.push_back(ChoiceOption("green", "Green", ""));
    choices.push_back(ChoiceOption("blue", "Blue", ""));
    choices.push_back(ChoiceOption("alpha", "Alpha", ""));
    return choices;
}

static int
clampChoiceIndex(int value, int maxExclusive)
{
    return std::max(0, std::min(value, maxExclusive - 1));
}

static bool
getInputRod(EffectInstance* effect,
            int inputIndex,
            U64 hash,
            double time,
            const RenderScale& scale,
            ViewIdx view,
            RectD* rod)
{
    EffectInstancePtr input = effect->getInput(inputIndex);
    if (!input) {
        return false;
    }

    bool isProjectFormat = false;
    StatusEnum st = input->getRegionOfDefinition_public(hash, time, scale, view, rod, &isProjectFormat);
    return st != eStatusFailed;
}

static int
outputChannelIndex(const ImagePlaneDesc& comps,
                   int nComps,
                   FluxCMChannel channel)
{
    if (comps == ImagePlaneDesc::getAlphaComponents()) {
        return channel == eFluxCMChannelAlpha ? 0 : -1;
    }
    if (nComps >= 4) {
        if (channel == eFluxCMChannelAlpha) {
            return 3;
        }
    }
    if (channel == eFluxCMChannelRed && nComps >= 1) {
        return 0;
    }
    if (channel == eFluxCMChannelGreen && nComps >= 2) {
        return 1;
    }
    if (channel == eFluxCMChannelBlue && nComps >= 3) {
        return 2;
    }
    return -1;
}

static float
sampleChannel(const ImagePtr& img,
              Image::ReadAccess* access,
              int x,
              int y,
              FluxCMChannel channel)
{
    if (!img || !access) {
        return 0.f;
    }
    const RectI bounds = img->getBounds();
    if (x < bounds.x1 || x >= bounds.x2 || y < bounds.y1 || y >= bounds.y2) {
        return 0.f;
    }

    const int nComps = (int)img->getComponentsCount();
    const float* pix = (const float*)access->pixelAt(x, y);
    if (!pix || nComps <= 0) {
        return 0.f;
    }

    if (nComps == 1) {
        return pix[0];
    }

    switch (channel) {
    case eFluxCMChannelRed:
        return pix[0];
    case eFluxCMChannelGreen:
        return nComps > 1 ? pix[1] : 0.f;
    case eFluxCMChannelBlue:
        return nComps > 2 ? pix[2] : 0.f;
    case eFluxCMChannelAlpha:
        return nComps > 3 ? pix[3] : 1.f;
    case eFluxCMChannelLuminance: {
        const float r = pix[0];
        const float g = nComps > 1 ? pix[1] : r;
        const float b = nComps > 2 ? pix[2] : r;
        return 0.2126f * r + 0.7152f * g + 0.0722f * b;
    }
    }
    return 0.f;
}

static float
overlay(float a,
        float b)
{
    return b <= 0.5f ? 2.f * a * b : 1.f - 2.f * (1.f - a) * (1.f - b);
}

static float
applyOperation(float a,
               float b,
               FluxCMOperation op)
{
    switch (op) {
    case eFluxCMOperationCopy:
        return a;
    case eFluxCMOperationPlus:
        return b + a;
    case eFluxCMOperationMultiply:
        return b * a;
    case eFluxCMOperationScreen:
        return 1.f - (1.f - b) * (1.f - a);
    case eFluxCMOperationMax:
        return std::max(b, a);
    case eFluxCMOperationMin:
        return std::min(b, a);
    case eFluxCMOperationSubtract:
        return b - a;
    case eFluxCMOperationOverlay:
        return overlay(a, b);
    }
    return a;
}

} // namespace

struct FluxChannelMergePrivate
{
    KnobChoiceWPtr aChannelKnob;
    KnobChoiceWPtr bChannelKnob;
    KnobChoiceWPtr operationKnob;
    KnobChoiceWPtr outputChannelKnob;
    KnobChoiceWPtr bboxKnob;
    KnobDoubleWPtr mixKnob;
    KnobBoolWPtr clampKnob;
};

FluxChannelMerge::FluxChannelMerge(NodePtr node)
    : EffectInstance(node)
    , _imp(new FluxChannelMergePrivate())
{
}

FluxChannelMerge::~FluxChannelMerge()
{
}

std::string
FluxChannelMerge::getPluginDescription() const
{
    return "Combines one scalar channel from input A with one scalar channel from input B, "
           "then writes the result into a selected output channel while preserving the other B channels.";
}

void
FluxChannelMerge::addAcceptedComponents(int /*inputNb*/, std::list<ImagePlaneDesc>* comps)
{
    comps->push_back(ImagePlaneDesc::getRGBAComponents());
    comps->push_back(ImagePlaneDesc::getRGBComponents());
    comps->push_back(ImagePlaneDesc::getAlphaComponents());
}

void
FluxChannelMerge::addSupportedBitDepth(std::list<ImageBitDepthEnum>* depths) const
{
    depths->push_back(eImageBitDepthFloat);
}

void
FluxChannelMerge::initializeKnobs()
{
    KnobPagePtr page = AppManager::createKnob<KnobPage>(this, tr("Controls"));

    KnobChoicePtr aChannel = AppManager::createKnob<KnobChoice>(this, tr("A Channel"), 1, false);
    aChannel->setName("aChannel");
    aChannel->setAnimationEnabled(false);
    aChannel->populateChoices(makeChannelChoices());
    aChannel->setDefaultValue(eFluxCMChannelRed);
    aChannel->setHintToolTip(tr("Channel sampled from input A."));
    page->addKnob(aChannel);
    _imp->aChannelKnob = aChannel;

    KnobChoicePtr bChannel = AppManager::createKnob<KnobChoice>(this, tr("B Channel"), 1, false);
    bChannel->setName("bChannel");
    bChannel->setAnimationEnabled(false);
    bChannel->populateChoices(makeChannelChoices());
    bChannel->setDefaultValue(eFluxCMChannelAlpha);
    bChannel->setHintToolTip(tr("Channel sampled from input B."));
    page->addKnob(bChannel);
    _imp->bChannelKnob = bChannel;

    KnobChoicePtr operation = AppManager::createKnob<KnobChoice>(this, tr("Operation"), 1, false);
    operation->setName("operation");
    operation->setAnimationEnabled(false);
    {
        std::vector<ChoiceOption> choices;
        choices.push_back(ChoiceOption("copy", "Copy", ""));
        choices.push_back(ChoiceOption("plus", "Plus", ""));
        choices.push_back(ChoiceOption("multiply", "Multiply", ""));
        choices.push_back(ChoiceOption("screen", "Screen", ""));
        choices.push_back(ChoiceOption("max", "Max", ""));
        choices.push_back(ChoiceOption("min", "Min", ""));
        choices.push_back(ChoiceOption("subtract", "Subtract", ""));
        choices.push_back(ChoiceOption("overlay", "Overlay", ""));
        operation->populateChoices(choices);
    }
    operation->setDefaultValue(eFluxCMOperationMax);
    operation->setHintToolTip(tr("Math used to combine A Channel and B Channel."));
    page->addKnob(operation);
    _imp->operationKnob = operation;

    KnobChoicePtr outputChannel = AppManager::createKnob<KnobChoice>(this, tr("Output Channel"), 1, false);
    outputChannel->setName("outputChannel");
    outputChannel->setAnimationEnabled(false);
    outputChannel->populateChoices(makeOutputChannelChoices());
    outputChannel->setDefaultValue(eFluxCMChannelAlpha);
    outputChannel->setHintToolTip(tr("Channel in the output image that receives the computed result."));
    page->addKnob(outputChannel);
    _imp->outputChannelKnob = outputChannel;

    KnobChoicePtr bbox = AppManager::createKnob<KnobChoice>(this, tr("BBox"), 1, false);
    bbox->setName("bbox");
    bbox->setAnimationEnabled(false);
    {
        std::vector<ChoiceOption> choices;
        choices.push_back(ChoiceOption("b", "B", ""));
        choices.push_back(ChoiceOption("a", "A", ""));
        choices.push_back(ChoiceOption("union", "Union", ""));
        choices.push_back(ChoiceOption("intersection", "Intersection", ""));
        bbox->populateChoices(choices);
    }
    bbox->setDefaultValue(eFluxCMBBoxB);
    bbox->setHintToolTip(tr("Region of definition used for the output."));
    page->addKnob(bbox);
    _imp->bboxKnob = bbox;

    KnobDoublePtr mix = AppManager::createKnob<KnobDouble>(this, tr("Mix"), 1, false);
    mix->setName("mix");
    mix->setDefaultValue(1.);
    mix->setMinimum(0.);
    mix->setMaximum(1.);
    mix->setHintToolTip(tr("Blends between the original B output channel and the computed result."));
    page->addKnob(mix);
    _imp->mixKnob = mix;

    KnobBoolPtr clamp = AppManager::createKnob<KnobBool>(this, tr("Clamp"), 1, false);
    clamp->setName("clamp");
    clamp->setAnimationEnabled(false);
    clamp->setDefaultValue(false);
    clamp->setHintToolTip(tr("Clamp the written result to the 0-1 range."));
    page->addKnob(clamp);
    _imp->clampKnob = clamp;
}

StatusEnum
FluxChannelMerge::getRegionOfDefinition(U64 hash,
                                        double time,
                                        const RenderScale& scale,
                                        ViewIdx view,
                                        RectD* rod)
{
    KnobChoicePtr bboxKnob = _imp->bboxKnob.lock();
    const int bbox = bboxKnob ? clampChoiceIndex(bboxKnob->getValue(), 4) : eFluxCMBBoxB;

    RectD bRod;
    RectD aRod;
    const bool hasB = getInputRod(this, 0, hash, time, scale, view, &bRod);
    const bool hasA = getInputRod(this, 1, hash, time, scale, view, &aRod);

    if (bbox == eFluxCMBBoxB) {
        if (!hasB) {
            return eStatusReplyDefault;
        }
        *rod = bRod;
        return eStatusOK;
    }
    if (bbox == eFluxCMBBoxA) {
        if (!hasA) {
            return eStatusReplyDefault;
        }
        *rod = aRod;
        return eStatusOK;
    }
    if (bbox == eFluxCMBBoxIntersection) {
        if (!hasA || !hasB) {
            return eStatusReplyDefault;
        }
        *rod = bRod.intersect(aRod);
        return rod->isNull() ? eStatusReplyDefault : eStatusOK;
    }

    if (hasB && hasA) {
        *rod = bRod;
        rod->merge(aRod);
        return eStatusOK;
    }
    if (hasB) {
        *rod = bRod;
        return eStatusOK;
    }
    if (hasA) {
        *rod = aRod;
        return eStatusOK;
    }
    return eStatusReplyDefault;
}

StatusEnum
FluxChannelMerge::render(const RenderActionArgs& args)
{
    ImagePtr bImg;
    EffectInstance::InputImagesMap::const_iterator bIt = args.inputImages.find(0);
    if (bIt != args.inputImages.end() && !bIt->second.empty()) {
        bImg = bIt->second.front();
    }

    ImagePtr aImg;
    EffectInstance::InputImagesMap::const_iterator aIt = args.inputImages.find(1);
    if (aIt != args.inputImages.end() && !aIt->second.empty()) {
        aImg = aIt->second.front();
    }

    KnobChoicePtr aChannelKnob = _imp->aChannelKnob.lock();
    KnobChoicePtr bChannelKnob = _imp->bChannelKnob.lock();
    KnobChoicePtr operationKnob = _imp->operationKnob.lock();
    KnobChoicePtr outputChannelKnob = _imp->outputChannelKnob.lock();
    KnobDoublePtr mixKnob = _imp->mixKnob.lock();
    KnobBoolPtr clampKnob = _imp->clampKnob.lock();

    const FluxCMChannel aChannel = (FluxCMChannel)(aChannelKnob ? clampChoiceIndex(aChannelKnob->getValue(), 5) : eFluxCMChannelRed);
    const FluxCMChannel bChannel = (FluxCMChannel)(bChannelKnob ? clampChoiceIndex(bChannelKnob->getValue(), 5) : eFluxCMChannelAlpha);
    const FluxCMOperation operation = (FluxCMOperation)(operationKnob ? clampChoiceIndex(operationKnob->getValue(), 8) : eFluxCMOperationMax);
    const FluxCMChannel outputChannel = (FluxCMChannel)(outputChannelKnob ? clampChoiceIndex(outputChannelKnob->getValue(), 4) : eFluxCMChannelAlpha);
    const float mix = (float)std::max(0., std::min(1., mixKnob ? mixKnob->getValue() : 1.));
    const bool doClamp = clampKnob && clampKnob->getValue();

    for (std::list<std::pair<ImagePlaneDesc, ImagePtr> >::const_iterator it = args.outputPlanes.begin();
         it != args.outputPlanes.end(); ++it) {
        const ImagePtr& outputImg = it->second;
        if (!outputImg) {
            continue;
        }

        if (bImg) {
            if (bImg->getMipmapLevel() != outputImg->getMipmapLevel()) {
                throw std::runtime_error("Host gave image with wrong scale");
            }
            if (bImg->getComponents() != outputImg->getComponents()) {
                bImg->convertToFormat(args.roi,
                                      getApp()->getDefaultColorSpaceForBitDepth(bImg->getBitDepth()),
                                      getApp()->getDefaultColorSpaceForBitDepth(outputImg->getBitDepth()),
                                      3, false, false, outputImg.get());
            } else {
                outputImg->pasteFrom(*bImg, args.roi, outputImg->usesBitMap() && bImg->usesBitMap());
            }
        } else {
            outputImg->fillZero(args.roi);
        }

        const int nComps = (int)outputImg->getComponentsCount();
        const int outChannel = outputChannelIndex(outputImg->getComponents(), nComps, outputChannel);
        if (outChannel < 0) {
            continue;
        }

        RectI intersection = args.roi.intersect(outputImg->getBounds());
        if (intersection.isNull()) {
            continue;
        }

        std::unique_ptr<Image::ReadAccess> aAccess;
        std::unique_ptr<Image::ReadAccess> bAccess;
        if (aImg) {
            aAccess.reset(new Image::ReadAccess(aImg.get()));
        }
        if (bImg) {
            bAccess.reset(new Image::ReadAccess(bImg.get()));
        }
        Image::WriteAccess writeAccess(outputImg.get());
        for (int y = intersection.y1; y < intersection.y2; ++y) {
            float* pix = (float*)writeAccess.pixelAt(intersection.x1, y);
            for (int x = intersection.x1; x < intersection.x2; ++x) {
                const float a = sampleChannel(aImg, aAccess.get(), x, y, aChannel);
                const float b = sampleChannel(bImg, bAccess.get(), x, y, bChannel);
                const float original = pix[outChannel];
                float result = applyOperation(a, b, operation);
                result = original * (1.f - mix) + result * mix;
                if (doClamp) {
                    result = std::max(0.f, std::min(1.f, result));
                }
                pix[outChannel] = result;
                pix += nComps;
            }
        }
    }

    return eStatusOK;
}

NATRON_NAMESPACE_EXIT
