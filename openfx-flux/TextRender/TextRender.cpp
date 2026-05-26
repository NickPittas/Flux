/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of Flux.
 *
 * Flux is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 * ***** END LICENSE BLOCK ***** */

#include <cassert>
#include <cfloat>
#include <cmath>
#include <memory>
#include <string>

#include "TextRasterizer.h"
#include "ofxsGenerator.h"
#include "ofxsMacros.h"
#include "ofxsProcessing.H"

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "FluxTextRender"
#define kPluginGrouping "Flux"
#define kPluginDescription "Flux-owned text renderer using Fontconfig, HarfBuzz, FreeType, and custom premultiplied RGBA compositing."
#define kPluginIdentifier "net.flux.openfx.TextRender"
#define kPluginVersionMajor 0
#define kPluginVersionMinor 2

#define kSupportsByte true
#define kSupportsUShort true
#define kSupportsHalf false
#define kSupportsFloat true

#define kSupportsTiles 0
#define kSupportsMultiResolution 0
#define kSupportsRenderScale 0
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false
#define kRenderThreadSafety eRenderFullySafe

#define kParamText "text"
#define kParamTextLabel "Text"
#define kParamTextHint "Text string rendered by the Flux text renderer."

#define kParamFont "font"
#define kParamFontLabel "Font Family"
#define kParamFontHint "Font family resolved through Fontconfig."

#define kParamFontStyle "fontStyle"
#define kParamFontStyleLabel "Font Style"
#define kParamFontStyleHint "Font style resolved through Fontconfig, e.g. Regular, Bold, Italic."

#define kParamFontSize "fontSize"
#define kParamFontSizeLabel "Font Size"
#define kParamFontSizeHint "Font size in project pixels."

#define kParamColor "fillColor"
#define kParamColorLabel "Fill Color"
#define kParamColorHint "Prototype fill color."

#define kParamTracking "tracking"
#define kParamTrackingLabel "Tracking"
#define kParamTrackingHint "Extra spacing added between shaped glyph advances in pixels."

#define kParamLeading "leading"
#define kParamLeadingLabel "Leading"
#define kParamLeadingHint "Line spacing in pixels. Zero uses the font default."

#define kParamAlignment "alignment"
#define kParamAlignmentLabel "Alignment"
#define kParamAlignmentHint "Multiline horizontal alignment."

#define kParamAnimatorStackJson "animatorStackJson"
#define kParamAnimatorStackJsonLabel "Animator Stack JSON"
#define kParamAnimatorStackJsonHint "Flux text animator stack, including dynamic knob values and keyframe curves."

#define kParamAnimatorTimeDependency "animatorTimeDependency"
#define kParamAnimatorTimeDependencyLabel "Animator Time Dependency"
#define kParamAnimatorTimeDependencyHint "Hidden animated scalar used to make host caches vary when embedded animator curves vary over time."

class TextRenderProcessorBase : public ImageProcessor
{
protected:
    OfxRGBAColourD _color;
    OfxRectI _dstBounds;
    std::shared_ptr<const FluxText::Raster> _raster;

public:
    explicit TextRenderProcessorBase(ImageEffect& instance)
        : ImageProcessor(instance)
    {
        _color.r = 0.1;
        _color.g = 0.65;
        _color.b = 1.0;
        _color.a = 1.0;
        _dstBounds.x1 = 0;
        _dstBounds.y1 = 0;
        _dstBounds.x2 = 1;
        _dstBounds.y2 = 1;
    }

    void setColor(const OfxRGBAColourD& color)
    {
        _color = color;
    }

    void setDstBounds(const OfxRectI& bounds)
    {
        _dstBounds = bounds;
    }

    void setRaster(const std::shared_ptr<const FluxText::Raster>& raster)
    {
        _raster = raster;
    }
};

template <class PIX, int nComponents, int max>
class TextRenderProcessor : public TextRenderProcessorBase
{
public:
    explicit TextRenderProcessor(ImageEffect& instance)
        : TextRenderProcessorBase(instance)
    {
    }

private:
    static PIX toPix(double value)
    {
        if (max == 1) {
            return static_cast<PIX>(value);
        }
        value = std::max(0.0, std::min(1.0, value));
        return static_cast<PIX>(std::floor(value * max + 0.5));
    }

    void multiThreadProcessImages(const OfxRectI& procWindow, const OfxPointD& rs) OVERRIDE FINAL
    {
        unused(rs);

        for (int y = procWindow.y1; y < procWindow.y2; ++y) {
            if (_effect.abort()) {
                break;
            }

            PIX* dstPix = reinterpret_cast<PIX*>(_dstImg->getPixelAddress(procWindow.x1, y));
            for (int x = procWindow.x1; x < procWindow.x2; ++x) {
                double r = _color.r, g = _color.g, b = _color.b, alpha = 0.0;
                if (_raster) {
                    _raster->colorAt(x, y, &r, &g, &b, &alpha);
                }

                if (nComponents == 1) {
                    dstPix[0] = toPix(alpha);
                } else if (nComponents == 2) {
                    dstPix[0] = toPix(r * alpha);
                    dstPix[1] = toPix(g * alpha);
                } else if (nComponents == 3) {
                    dstPix[0] = toPix(r * alpha);
                    dstPix[1] = toPix(g * alpha);
                    dstPix[2] = toPix(b * alpha);
                } else {
                    assert(nComponents == 4);
                    dstPix[0] = toPix(r * alpha);
                    dstPix[1] = toPix(g * alpha);
                    dstPix[2] = toPix(b * alpha);
                    dstPix[3] = toPix(alpha);
                }
                dstPix += nComponents;
            }
        }
    }
};

class TextRenderPlugin : public GeneratorPlugin
{
public:
    explicit TextRenderPlugin(OfxImageEffectHandle handle)
        : GeneratorPlugin(handle, true, kSupportsByte, kSupportsUShort, kSupportsHalf, kSupportsFloat)
        , _text(NULL)
        , _font(NULL)
        , _fontStyle(NULL)
        , _fontSize(NULL)
        , _color(NULL)
        , _tracking(NULL)
        , _leading(NULL)
        , _alignment(NULL)
        , _animatorStackJson(NULL)
        , _animatorTimeDependency(NULL)
    {
        _text = fetchStringParam(kParamText);
        _font = fetchStringParam(kParamFont);
        _fontStyle = fetchStringParam(kParamFontStyle);
        _fontSize = fetchDoubleParam(kParamFontSize);
        _color = fetchRGBAParam(kParamColor);
        _tracking = fetchDoubleParam(kParamTracking);
        _leading = fetchDoubleParam(kParamLeading);
        _alignment = fetchChoiceParam(kParamAlignment);
        _animatorStackJson = fetchStringParam(kParamAnimatorStackJson);
        _animatorTimeDependency = fetchDoubleParam(kParamAnimatorTimeDependency);
        assert(_text && _font && _fontStyle && _fontSize && _color && _tracking && _leading && _alignment && _animatorStackJson && _animatorTimeDependency);
    }

private:
    void render(const RenderArguments& args) OVERRIDE FINAL;
    void getClipPreferences(ClipPreferencesSetter& clipPreferences) OVERRIDE FINAL;

    template <int nComponents>
    void renderInternal(const RenderArguments& args, BitDepthEnum dstBitDepth);

    void setupAndProcess(TextRenderProcessorBase& processor, const RenderArguments& args);

private:
    StringParam* _text;
    StringParam* _font;
    StringParam* _fontStyle;
    DoubleParam* _fontSize;
    RGBAParam* _color;
    DoubleParam* _tracking;
    DoubleParam* _leading;
    ChoiceParam* _alignment;
    StringParam* _animatorStackJson;
    DoubleParam* _animatorTimeDependency;
};

void TextRenderPlugin::setupAndProcess(TextRenderProcessorBase& processor, const RenderArguments& args)
{
    std::unique_ptr<Image> dst(_dstClip->fetchImage(args.time));
    if (!dst.get()) {
        throwSuiteStatusException(kOfxStatFailed);
    }

#ifndef NDEBUG
    if (dst->getPixelDepth() != _dstClip->getPixelDepth() || dst->getPixelComponents() != _dstClip->getPixelComponents()) {
        setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong depth or components");
        throwSuiteStatusException(kOfxStatFailed);
    }
    checkBadRenderScaleOrField(dst, args);
#endif

    OfxRGBAColourD color;
    _color->getValueAtTime(args.time, color.r, color.g, color.b, color.a);

    FluxText::RenderRequest request;
    _text->getValueAtTime(args.time, request.text);
    _font->getValueAtTime(args.time, request.font);
    _fontStyle->getValueAtTime(args.time, request.fontStyle);
    _fontSize->getValueAtTime(args.time, request.fontSize);
    _tracking->getValueAtTime(args.time, request.tracking);
    _leading->getValueAtTime(args.time, request.leading);
    _alignment->getValueAtTime(args.time, request.alignment);
    _animatorStackJson->getValueAtTime(args.time, request.animatorStackJson);
    double animatorTimeDependency = 0.0;
    _animatorTimeDependency->getValueAtTime(args.time, animatorTimeDependency);
    request.fillColor[0] = color.r;
    request.fillColor[1] = color.g;
    request.fillColor[2] = color.b;
    request.fillColor[3] = color.a;
    request.time = args.time;
    request.renderScaleX = args.renderScale.x;
    request.renderScaleY = args.renderScale.y;
    request.outputBounds = dst->getBounds();
    request.bounds = request.outputBounds;

    OfxRectD rod;
    if (!getRegionOfDefinition(args.time, rod)) {
        OfxPointD projectSize = getProjectSize();
        OfxPointD projectOffset = getProjectOffset();
        rod.x1 = projectOffset.x;
        rod.y1 = projectOffset.y;
        rod.x2 = projectOffset.x + projectSize.x;
        rod.y2 = projectOffset.y + projectSize.y;
    }
    const double scaleX = args.renderScale.x > 0.0 ? args.renderScale.x : 1.0;
    const double scaleY = args.renderScale.y > 0.0 ? args.renderScale.y : 1.0;
    request.layoutBounds.x1 = static_cast<int>(std::floor(rod.x1 * scaleX));
    request.layoutBounds.y1 = static_cast<int>(std::floor(rod.y1 * scaleY));
    request.layoutBounds.x2 = static_cast<int>(std::ceil(rod.x2 * scaleX));
    request.layoutBounds.y2 = static_cast<int>(std::ceil(rod.y2 * scaleY));
    if (request.layoutBounds.x2 <= request.layoutBounds.x1 || request.layoutBounds.y2 <= request.layoutBounds.y1) {
        request.layoutBounds = request.outputBounds;
    }

    std::string rasterError;
    std::shared_ptr<FluxText::Raster> raster = FluxText::renderText(request, &rasterError);
    if (!rasterError.empty()) {
        setPersistentMessage(Message::eMessageWarning, "", rasterError);
    } else {
        clearPersistentMessage();
    }

    processor.setDstImg(dst.get());
    processor.setDstBounds(dst->getBounds());
    processor.setRenderWindow(dst->getBounds(), args.renderScale);
    processor.setColor(color);
    processor.setRaster(raster);
    processor.process();
}

template <int nComponents>
void TextRenderPlugin::renderInternal(const RenderArguments& args, BitDepthEnum dstBitDepth)
{
    switch (dstBitDepth) {
    case eBitDepthUByte: {
        TextRenderProcessor<unsigned char, nComponents, 255> processor(*this);
        setupAndProcess(processor, args);
        break;
    }
    case eBitDepthUShort: {
        TextRenderProcessor<unsigned short, nComponents, 65535> processor(*this);
        setupAndProcess(processor, args);
        break;
    }
    case eBitDepthFloat: {
        TextRenderProcessor<float, nComponents, 1> processor(*this);
        setupAndProcess(processor, args);
        break;
    }
    default:
        throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

void TextRenderPlugin::render(const RenderArguments& args)
{
    const BitDepthEnum dstBitDepth = _dstClip->getPixelDepth();
    const PixelComponentEnum dstComponents = _dstClip->getPixelComponents();

    checkComponents(dstBitDepth, dstComponents);

    if (dstComponents == ePixelComponentRGBA) {
        renderInternal<4>(args, dstBitDepth);
    } else if (dstComponents == ePixelComponentRGB) {
        renderInternal<3>(args, dstBitDepth);
#ifdef OFX_EXTENSIONS_NATRON
    } else if (dstComponents == ePixelComponentXY) {
        renderInternal<2>(args, dstBitDepth);
#endif
    } else {
        assert(dstComponents == ePixelComponentAlpha);
        renderInternal<1>(args, dstBitDepth);
    }
}

void TextRenderPlugin::getClipPreferences(ClipPreferencesSetter& clipPreferences)
{
    clipPreferences.setOutputHasContinuousSamples(true);
    GeneratorPlugin::getClipPreferences(clipPreferences);
    clipPreferences.setOutputPremultiplication(eImagePreMultiplied);
}

class TextRenderPluginFactory : public PluginFactoryHelper<TextRenderPluginFactory>
{
public:
    TextRenderPluginFactory(const std::string& id, unsigned int verMaj, unsigned int verMin)
        : PluginFactoryHelper<TextRenderPluginFactory>(id, verMaj, verMin)
    {
    }

    void describe(ImageEffectDescriptor& desc);
    void describeInContext(ImageEffectDescriptor& desc, ContextEnum context);
    ImageEffect* createInstance(OfxImageEffectHandle handle, ContextEnum context);
};

void TextRenderPluginFactory::describe(ImageEffectDescriptor& desc)
{
    desc.setLabel(kPluginName);
    desc.setPluginDescription(kPluginDescription);
    desc.setPluginGrouping(kPluginGrouping);
    desc.addSupportedContext(eContextGenerator);
    desc.addSupportedContext(eContextGeneral);
    if (kSupportsByte) {
        desc.addSupportedBitDepth(eBitDepthUByte);
    }
    if (kSupportsUShort) {
        desc.addSupportedBitDepth(eBitDepthUShort);
    }
    if (kSupportsFloat) {
        desc.addSupportedBitDepth(eBitDepthFloat);
    }

    desc.setSingleInstance(false);
    desc.setHostFrameThreading(false);
    desc.setSupportsMultiResolution(kSupportsMultiResolution);
    desc.setSupportsTiles(kSupportsTiles);
    desc.setTemporalClipAccess(false);
    desc.setRenderTwiceAlways(false);
    desc.setSupportsMultipleClipPARs(kSupportsMultipleClipPARs);
    desc.setSupportsMultipleClipDepths(kSupportsMultipleClipDepths);
    desc.setRenderThreadSafety(kRenderThreadSafety);
#ifdef OFX_EXTENSIONS_NATRON
    desc.setChannelSelector(ePixelComponentRGBA);
#endif

    generatorDescribe(desc);
}

void TextRenderPluginFactory::describeInContext(ImageEffectDescriptor& desc, ContextEnum context)
{
    ClipDescriptor* srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->addSupportedComponent(ePixelComponentRGB);
#ifdef OFX_EXTENSIONS_NATRON
    srcClip->addSupportedComponent(ePixelComponentXY);
#endif
    srcClip->addSupportedComponent(ePixelComponentAlpha);
    srcClip->setSupportsTiles(kSupportsTiles);
    srcClip->setOptional(true);

    ClipDescriptor* dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
#ifdef OFX_EXTENSIONS_NATRON
    dstClip->addSupportedComponent(ePixelComponentXY);
#endif
    dstClip->addSupportedComponent(ePixelComponentAlpha);
    dstClip->setSupportsTiles(kSupportsTiles);

    PageParamDescriptor* page = desc.definePageParam("Controls");
    generatorDescribeInContext(page, desc, *dstClip, eGeneratorExtentDefault, ePixelComponentRGBA, true, context);

    StringParamDescriptor* textParam = desc.defineStringParam(kParamText);
    textParam->setLabel(kParamTextLabel);
    textParam->setHint(kParamTextHint);
    textParam->setDefault("Flux Motion Text");
    textParam->setAnimates(true);
    if (page) {
        page->addChild(*textParam);
    }

    StringParamDescriptor* fontParam = desc.defineStringParam(kParamFont);
    fontParam->setLabel(kParamFontLabel);
    fontParam->setHint(kParamFontHint);
    fontParam->setDefault("Sans");
    fontParam->setAnimates(true);
    if (page) {
        page->addChild(*fontParam);
    }

    StringParamDescriptor* fontStyleParam = desc.defineStringParam(kParamFontStyle);
    fontStyleParam->setLabel(kParamFontStyleLabel);
    fontStyleParam->setHint(kParamFontStyleHint);
    fontStyleParam->setDefault("Regular");
    fontStyleParam->setAnimates(true);
    if (page) {
        page->addChild(*fontStyleParam);
    }

    DoubleParamDescriptor* fontSizeParam = desc.defineDoubleParam(kParamFontSize);
    fontSizeParam->setLabel(kParamFontSizeLabel);
    fontSizeParam->setHint(kParamFontSizeHint);
    fontSizeParam->setDefault(96.0);
    fontSizeParam->setRange(1.0, 2000.0);
    fontSizeParam->setDisplayRange(1.0, 300.0);
    fontSizeParam->setAnimates(true);
    if (page) {
        page->addChild(*fontSizeParam);
    }

    RGBAParamDescriptor* colorParam = desc.defineRGBAParam(kParamColor);
    colorParam->setLabel(kParamColorLabel);
    colorParam->setHint(kParamColorHint);
    colorParam->setDefault(0.1, 0.65, 1.0, 1.0);
    colorParam->setRange(0.0, 0.0, 0.0, 0.0, DBL_MAX, DBL_MAX, DBL_MAX, DBL_MAX);
    colorParam->setDisplayRange(0.0, 0.0, 0.0, 0.0, 1.0, 1.0, 1.0, 1.0);
    colorParam->setAnimates(true);
    if (page) {
        page->addChild(*colorParam);
    }

    DoubleParamDescriptor* trackingParam = desc.defineDoubleParam(kParamTracking);
    trackingParam->setLabel(kParamTrackingLabel);
    trackingParam->setHint(kParamTrackingHint);
    trackingParam->setDefault(0.0);
    trackingParam->setRange(-500.0, 1000.0);
    trackingParam->setDisplayRange(-50.0, 200.0);
    trackingParam->setAnimates(true);
    if (page) {
        page->addChild(*trackingParam);
    }

    DoubleParamDescriptor* leadingParam = desc.defineDoubleParam(kParamLeading);
    leadingParam->setLabel(kParamLeadingLabel);
    leadingParam->setHint(kParamLeadingHint);
    leadingParam->setDefault(0.0);
    leadingParam->setRange(0.0, 5000.0);
    leadingParam->setDisplayRange(0.0, 500.0);
    leadingParam->setAnimates(true);
    if (page) {
        page->addChild(*leadingParam);
    }

    ChoiceParamDescriptor* alignmentParam = desc.defineChoiceParam(kParamAlignment);
    alignmentParam->setLabel(kParamAlignmentLabel);
    alignmentParam->setHint(kParamAlignmentHint);
    alignmentParam->appendOption("Left");
    alignmentParam->appendOption("Center");
    alignmentParam->appendOption("Right");
    alignmentParam->setDefault(1);
    alignmentParam->setAnimates(true);
    if (page) {
        page->addChild(*alignmentParam);
    }

    StringParamDescriptor* animatorParam = desc.defineStringParam(kParamAnimatorStackJson);
    animatorParam->setLabel(kParamAnimatorStackJsonLabel);
    animatorParam->setHint(kParamAnimatorStackJsonHint);
    animatorParam->setDefault("{\"version\":1,\"animators\":[]}");
    animatorParam->setAnimates(true);
    animatorParam->setIsSecret(true);
    if (page) {
        page->addChild(*animatorParam);
    }

    DoubleParamDescriptor* animatorTimeDependencyParam = desc.defineDoubleParam(kParamAnimatorTimeDependency);
    animatorTimeDependencyParam->setLabel(kParamAnimatorTimeDependencyLabel);
    animatorTimeDependencyParam->setHint(kParamAnimatorTimeDependencyHint);
    animatorTimeDependencyParam->setDefault(0.0);
    animatorTimeDependencyParam->setAnimates(true);
    animatorTimeDependencyParam->setIsSecret(true);
    if (page) {
        page->addChild(*animatorTimeDependencyParam);
    }
}

ImageEffect* TextRenderPluginFactory::createInstance(OfxImageEffectHandle handle, ContextEnum /*context*/)
{
    return new TextRenderPlugin(handle);
}

static TextRenderPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
