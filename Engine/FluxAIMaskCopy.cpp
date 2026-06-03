#include <Python.h>
#include "Global/Macros.h"
#include "Engine/FluxAIMaskCopy.h"

#include <cstdio>
#include <cctype>
#include <list>
#include <stdexcept>
#include <vector>

#include "Engine/AppManager.h"
#include "Engine/AppInstance.h"
#include "Engine/Image.h"
#include "Engine/ImagePlaneDesc.h"
#include "Engine/KnobTypes.h"
#include "Engine/Node.h"

NATRON_NAMESPACE_ENTER

static bool isValidCustomPlaneName(const std::string& s)
{
    if (s.empty()) {
        return false;
    }

    const unsigned char first = static_cast<unsigned char>(s[0]);
    if ( !( (first >= 'A' && first <= 'Z') || (first >= 'a' && first <= 'z') || first == '_' ) ) {
        return false;
    }

    for (std::size_t i = 1; i < s.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if ( !( (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' ) ) {
            return false;
        }
    }

    std::string lower;
    lower.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(s[i]))));
    }

    static const char* reserved[] = {
        "color",
        "rgba",
        "rgb",
        "alpha",
        "backward",
        "forward",
        "disparityleft",
        "disparityright",
        "motion",
        "none"
    };
    for (std::size_t i = 0; i < sizeof(reserved) / sizeof(reserved[0]); ++i) {
        if (lower == reserved[i]) {
            return false;
        }
    }

    return true;
}

static ImagePlaneDesc makeCustomRGBAPlane(const std::string& id)
{
    std::vector<std::string> channels;
    channels.push_back("r"); channels.push_back("g"); channels.push_back("b"); channels.push_back("a");
    return ImagePlaneDesc(id, id, "RGBA", channels);
}

struct FluxAIMaskCopyPrivate
{
    KnobStringWPtr targetPlaneKnob;
};

FluxAIMaskCopy::FluxAIMaskCopy(NodePtr node) : EffectInstance(node), _imp(new FluxAIMaskCopyPrivate()) {}
FluxAIMaskCopy::~FluxAIMaskCopy() {}

void FluxAIMaskCopy::addAcceptedComponents(int /*inputNb*/, std::list<ImagePlaneDesc>* comps)
{
    comps->push_back(ImagePlaneDesc::getRGBAComponents());
    comps->push_back(ImagePlaneDesc::getRGBComponents());
    comps->push_back(ImagePlaneDesc::getAlphaComponents());

    std::string target = "ai_mask1";
    KnobStringPtr targetKnob = _imp->targetPlaneKnob.lock();
    if (targetKnob) {
        target = targetKnob->getValue();
    }
    if (!isValidCustomPlaneName(target)) {
        target = "ai_mask1";
    }

    const ImagePlaneDesc aiMask1 = makeCustomRGBAPlane("ai_mask1");
    comps->push_back(aiMask1);
    if (target != "ai_mask1") {
        comps->push_back(makeCustomRGBAPlane(target));
    }

    NodePtr node = getNode();
    if (node) {
        node->addUserComponents(aiMask1);
        if (target != "ai_mask1") {
            node->addUserComponents(makeCustomRGBAPlane(target));
        }
    }
}

void FluxAIMaskCopy::addSupportedBitDepth(std::list<ImageBitDepthEnum>* depths) const
{
    depths->push_back(eImageBitDepthFloat);
}

void FluxAIMaskCopy::initializeKnobs()
{
    KnobStringPtr target = AppManager::createKnob<KnobString>(this, tr("Target Plane"), 1, false);
    target->setName("targetPlane");
    target->setAnimationEnabled(false);
    target->setDefaultValue("ai_mask1");
    target->setHintToolTip(tr("Custom RGBA plane name. Use letters, numbers, and underscores; start with a letter or underscore. Invalid or reserved names fall back to ai_mask1."));
    _imp->targetPlaneKnob = target;
    NodePtr node = getNode();
    if (node) {
        node->addUserComponents(makeCustomRGBAPlane("ai_mask1"));
    }
}

StatusEnum FluxAIMaskCopy::render(const RenderActionArgs& args)
{
    std::string target = "ai_mask1";
    KnobStringPtr targetKnob = _imp->targetPlaneKnob.lock();
    if (targetKnob) target = targetKnob->getValue();
    if (!isValidCustomPlaneName(target)) {
        target = "ai_mask1";
    }

    ImagePtr maskImg;
    EffectInstance::InputImagesMap::const_iterator bIt = args.inputImages.find(1);
    if (bIt != args.inputImages.end() && !bIt->second.empty()) maskImg = bIt->second.front();

    for (std::list<std::pair<ImagePlaneDesc, ImagePtr> >::const_iterator it = args.outputPlanes.begin(); it != args.outputPlanes.end(); ++it) {
        const ImagePlaneDesc& outComps = it->first;
        const ImagePtr& out = it->second;
        const bool isTarget = outComps.getPlaneID() == target;
        ImagePtr src;
        RectI roiPixel;

        if (isTarget) {
            src = maskImg;
        } else {
            src = getImage(0, args.time, args.originalScale, args.view, NULL, &outComps, false /*mapToClipPrefs*/, true /*dontUpscale*/, eStorageModeRAM, 0 /*textureDepth*/, &roiPixel);
        }
        if (!src) {
            return eStatusFailed;
        }
        if (src->getMipmapLevel() != out->getMipmapLevel()) {
            throw std::runtime_error("Host gave image with wrong scale");
        }
        if ( (src->getComponents() != out->getComponents()) || (src->getBitDepth() != out->getBitDepth()) ) {
            src->convertToFormat(args.roi,
                                 getApp()->getDefaultColorSpaceForBitDepth(src->getBitDepth()),
                                 getApp()->getDefaultColorSpaceForBitDepth(out->getBitDepth()),
                                 3, false, false, out.get());
        } else {
            out->pasteFrom(*src, args.roi, out->usesBitMap() && src->usesBitMap());
        }
    }
    return eStatusOK;
}

NATRON_NAMESPACE_EXIT
