/* ***** BEGIN LICENSE BLOCK *****
 * Flux — Native CorridorKey node shell
 * (C) 2026 Nick Pittas
 * GPL2 — see LICENSE.txt
 * ***** END LICENSE BLOCK ***** */

#include <Python.h>

#include "Global/Macros.h"

#include "Engine/FluxCorridorKey.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <list>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <sstream>
#include <string>
#include <vector>

#ifdef FLUX_ENABLE_CORRIDORKEY_TENSORRT
#include <cuda_runtime_api.h>
#include <dlfcn.h>
#include <NvInferRuntime.h>
#endif

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

enum FluxCorridorKeyResolution {
    eFluxCorridorKeyResolution512 = 0,
    eFluxCorridorKeyResolution768,
    eFluxCorridorKeyResolution1024,
    eFluxCorridorKeyResolution2048,
    eFluxCorridorKeyResolutionCustom
};

enum FluxCorridorKeyOutputMode {
    eFluxCorridorKeyOutputRGBA = 0,
    eFluxCorridorKeyOutputAlpha,
    eFluxCorridorKeyOutputFG
};

enum FluxCorridorKeyScreenColor {
    eFluxCorridorKeyScreenAuto = 0,
    eFluxCorridorKeyScreenGreen,
    eFluxCorridorKeyScreenBlue
};

#ifdef FLUX_ENABLE_CORRIDORKEY_TENSORRT
static const char* kCorridorKeyInputTensor = "input";
static const char* kCorridorKeyAlphaTensor = "alpha";
static const char* kCorridorKeyFgTensor = "fg";
#endif

static std::vector<ChoiceOption>
makeResolutionChoices()
{
    std::vector<ChoiceOption> choices;
    choices.push_back(ChoiceOption("512", "512", "Use the 512 TensorRT engine as the internal model resolution."));
    choices.push_back(ChoiceOption("768", "768", "Use the 768 TensorRT engine as the internal model resolution."));
    choices.push_back(ChoiceOption("1024", "1024", "Use the 1024 TensorRT engine as the internal model resolution."));
    choices.push_back(ChoiceOption("2048", "2048", "Use the 2048 TensorRT engine as the internal model resolution."));
    choices.push_back(ChoiceOption("custom", "Custom (manual engine)", "Use a manually supplied TensorRT engine."));
    return choices;
}

static std::vector<ChoiceOption>
makeOutputModeChoices()
{
    std::vector<ChoiceOption> choices;
    choices.push_back(ChoiceOption("rgba", "RGBA (FG + Alpha)", ""));
    choices.push_back(ChoiceOption("alpha", "Alpha Only", ""));
    choices.push_back(ChoiceOption("fg", "FG Only", ""));
    return choices;
}

static std::vector<ChoiceOption>
makeScreenColorChoices()
{
    std::vector<ChoiceOption> choices;
    choices.push_back(ChoiceOption("auto", "Auto", "Estimate green/blue spill per pixel."));
    choices.push_back(ChoiceOption("green", "Green", "Despill green-screen contamination."));
    choices.push_back(ChoiceOption("blue", "Blue", "Despill blue-screen contamination."));
    return choices;
}

static int
clampChoiceIndex(int value,
                 int maxExclusive)
{
    return std::max(0, std::min(value, maxExclusive - 1));
}

#ifdef FLUX_ENABLE_CORRIDORKEY_TENSORRT
static float
clamp01(float v)
{
    return std::max(0.f, std::min(1.f, v));
}

static int
resolutionChoiceToSize(int choice)
{
    switch (choice) {
    case eFluxCorridorKeyResolution512:
        return 512;
    case eFluxCorridorKeyResolution768:
        return 768;
    case eFluxCorridorKeyResolution1024:
        return 1024;
    case eFluxCorridorKeyResolution2048:
        return 2048;
    default:
        return 1024;
    }
}

static std::string
defaultCorridorKeyEnginePath()
{
    const char* defaultEngine = std::getenv("FLUX_CORRIDORKEY_ENGINE_PATH");
    return (defaultEngine && defaultEngine[0] != '\0') ? std::string(defaultEngine) : std::string();
}

static std::string
autoCorridorKeyEnginePath(int resolutionChoice)
{
    if (resolutionChoice == eFluxCorridorKeyResolutionCustom) {
        return std::string();
    }
    const char* dirEnv = std::getenv("FLUX_CORRIDORKEY_ENGINE_DIR");
    if (!dirEnv || dirEnv[0] == '\0') {
        return defaultCorridorKeyEnginePath();
    }
    std::string dir(dirEnv);
    if (!dir.empty() && dir[dir.size() - 1] != '/') {
        dir += '/';
    }
    dir += "CorridorKey_v1.0_";
    dir += std::to_string(resolutionChoiceToSize(resolutionChoice));
    dir += "_fp16.engine";
    return dir;
}

static bool
fileExists(const std::string& path)
{
    if (path.empty()) {
        return false;
    }
    std::ifstream f(path.c_str(), std::ios::binary);
    return f.good();
}

static std::string
resolveCorridorKeyEnginePath(const std::string& knobPath,
                             int resolutionChoice)
{
    const std::string envDefault = defaultCorridorKeyEnginePath();
    const bool knobIsUnsetOrDefault = knobPath.empty() || (!envDefault.empty() && knobPath == envDefault);
    if (knobIsUnsetOrDefault) {
        const std::string autoPath = autoCorridorKeyEnginePath(resolutionChoice);
        if (fileExists(autoPath)) {
            return autoPath;
        }
    }
    return knobPath.empty() ? envDefault : knobPath;
}
#endif

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

static void
copyOrConvertPlate(const ImagePtr& plate,
                   const ImagePtr& output,
                   const RectI& roi,
                   AppInstance* app)
{
    if (!output) {
        return;
    }
    if (!plate) {
        output->fillZero(roi);
        return;
    }
    if (plate->getMipmapLevel() != output->getMipmapLevel()) {
        throw std::runtime_error("Host gave image with wrong scale");
    }
    if ((plate->getComponents() != output->getComponents()) || (plate->getBitDepth() != output->getBitDepth())) {
        plate->convertToFormat(roi,
                               app->getDefaultColorSpaceForBitDepth(plate->getBitDepth()),
                               app->getDefaultColorSpaceForBitDepth(output->getBitDepth()),
                               3, false, false, output.get());
    } else {
        output->pasteFrom(*plate, roi, output->usesBitMap() && plate->usesBitMap());
    }
}

#ifdef FLUX_ENABLE_CORRIDORKEY_TENSORRT
template <typename PIX>
static float
pixelToFloat(const PIX* pix,
             int comp)
{
    return Image::convertPixelDepth<PIX, float>(pix[comp]);
}

static float
readComponentAt(const ImagePtr& img,
                Image::ReadAccess* access,
                int x,
                int y,
                int comp,
                float missingValue)
{
    if (!img || !access) {
        return missingValue;
    }
    const int nComps = (int)img->getComponentsCount();
    if (nComps <= 0) {
        return missingValue;
    }
    const int safeComp = std::max(0, std::min(comp, nComps - 1));
    const unsigned char* raw = access->pixelAt(x, y);
    if (!raw) {
        return missingValue;
    }
    switch (img->getBitDepth()) {
    case eImageBitDepthByte:
        return pixelToFloat((const unsigned char*)raw, safeComp);
    case eImageBitDepthShort:
        return pixelToFloat((const unsigned short*)raw, safeComp);
    case eImageBitDepthFloat:
        return pixelToFloat((const float*)raw, safeComp);
    default:
        return missingValue;
    }
}

static float
sampleComponentBilinear(const ImagePtr& img,
                        Image::ReadAccess* access,
                        double sx,
                        double sy,
                        int comp,
                        float missingValue)
{
    if (!img || !access) {
        return missingValue;
    }
    const RectI bounds = img->getBounds();
    if (bounds.width() <= 0 || bounds.height() <= 0) {
        return missingValue;
    }

    sx = std::max((double)bounds.x1, std::min((double)bounds.x2 - 1., sx));
    sy = std::max((double)bounds.y1, std::min((double)bounds.y2 - 1., sy));

    const int x0 = std::max(bounds.x1, std::min(bounds.x2 - 1, (int)std::floor(sx)));
    const int y0 = std::max(bounds.y1, std::min(bounds.y2 - 1, (int)std::floor(sy)));
    const int x1 = std::min(bounds.x2 - 1, x0 + 1);
    const int y1 = std::min(bounds.y2 - 1, y0 + 1);
    const float tx = (float)(sx - (double)x0);
    const float ty = (float)(sy - (double)y0);

    const float v00 = readComponentAt(img, access, x0, y0, comp, missingValue);
    const float v10 = readComponentAt(img, access, x1, y0, comp, missingValue);
    const float v01 = readComponentAt(img, access, x0, y1, comp, missingValue);
    const float v11 = readComponentAt(img, access, x1, y1, comp, missingValue);
    const float vx0 = v00 + (v10 - v00) * tx;
    const float vx1 = v01 + (v11 - v01) * tx;
    return vx0 + (vx1 - vx0) * ty;
}

static int
maskComponentIndex(const ImagePtr& mask)
{
    if (!mask) {
        return 0;
    }
    // CorridorKey-for-Nuke consumes the mask hint from the red channel.
    // SAM3/AI Paint masks are often RGB/RGBA PNGs with matte data in RGB
    // and an unhelpful/empty alpha channel, so never prefer alpha here.
    return 0;
}

static float
sampleTensorPlaneBilinear(const std::vector<float>& tensor,
                          int width,
                          int height,
                          std::size_t planeOffset,
                          double sx,
                          double sy)
{
    const std::size_t plane = (std::size_t)width * height;
    if (width <= 0 || height <= 0 || tensor.size() < planeOffset + plane) {
        return 0.f;
    }
    sx = std::max(0., std::min((double)width - 1., sx));
    sy = std::max(0., std::min((double)height - 1., sy));
    const int x0 = std::max(0, std::min(width - 1, (int)std::floor(sx)));
    const int y0 = std::max(0, std::min(height - 1, (int)std::floor(sy)));
    const int x1 = std::min(width - 1, x0 + 1);
    const int y1 = std::min(height - 1, y0 + 1);
    const float tx = (float)(sx - (double)x0);
    const float ty = (float)(sy - (double)y0);
    const float v00 = tensor[planeOffset + (std::size_t)y0 * width + x0];
    const float v10 = tensor[planeOffset + (std::size_t)y0 * width + x1];
    const float v01 = tensor[planeOffset + (std::size_t)y1 * width + x0];
    const float v11 = tensor[planeOffset + (std::size_t)y1 * width + x1];
    const float vx0 = v00 + (v10 - v00) * tx;
    const float vx1 = v01 + (v11 - v01) * tx;
    return vx0 + (vx1 - vx0) * ty;
}

static float
sampleTensor1Bilinear(const std::vector<float>& tensor,
                      int width,
                      int height,
                      double sx,
                      double sy)
{
    return sampleTensorPlaneBilinear(tensor, width, height, 0, sx, sy);
}

static float
sampleTensor3Bilinear(const std::vector<float>& tensor,
                      int width,
                      int height,
                      int channel,
                      double sx,
                      double sy)
{
    if (channel < 0 || channel >= 3) {
        return 0.f;
    }
    const std::size_t plane = (std::size_t)width * height;
    return sampleTensorPlaneBilinear(tensor, width, height, plane * (std::size_t)channel, sx, sy);
}

template <typename PIX>
static void
writeConverted(PIX* pix,
               int comp,
               float value)
{
    pix[comp] = Image::convertPixelDepth<float, PIX>(clamp01(value));
}

template <typename PIX>
static void
writeCorridorKeyPixel(PIX* pix,
                      int nComps,
                      FluxCorridorKeyOutputMode outputMode,
                      float r,
                      float g,
                      float b,
                      float a)
{
    if (!pix || nComps <= 0) {
        return;
    }
    if (outputMode == eFluxCorridorKeyOutputAlpha) {
        if (nComps == 1) {
            writeConverted(pix, 0, a);
        } else {
            writeConverted(pix, 0, a);
            if (nComps > 1) writeConverted(pix, 1, a);
            if (nComps > 2) writeConverted(pix, 2, a);
            if (nComps > 3) writeConverted(pix, 3, a);
        }
        return;
    }

    if (nComps == 1) {
        writeConverted(pix, 0, outputMode == eFluxCorridorKeyOutputFG ? (r + g + b) / 3.f : a);
        return;
    }
    writeConverted(pix, 0, r);
    if (nComps > 1) writeConverted(pix, 1, g);
    if (nComps > 2) writeConverted(pix, 2, b);
    if (nComps > 3) writeConverted(pix, 3, outputMode == eFluxCorridorKeyOutputFG ? 1.f : a);
}

static void
applyCorridorKeyDespill(float* r,
                        float* g,
                        float* b,
                        float alpha,
                        FluxCorridorKeyScreenColor screenColor,
                        double strength)
{
    if (!r || !g || !b || strength <= 0.) {
        return;
    }
    const float s = clamp01((float)strength);
    int channel = -1;
    if (screenColor == eFluxCorridorKeyScreenGreen) {
        channel = 1;
    } else if (screenColor == eFluxCorridorKeyScreenBlue) {
        channel = 2;
    } else {
        channel = (*g >= *b && *g > *r) ? 1 : ((*b > *g && *b > *r) ? 2 : -1);
    }
    if (channel < 0) {
        return;
    }
    float* spillChannel = channel == 1 ? g : b;
    const float otherA = channel == 1 ? *r : *r;
    const float otherB = channel == 1 ? *b : *g;
    const float neutral = std::max(otherA, otherB);
    const float spill = std::max(0.f, *spillChannel - neutral);
    if (spill <= 0.f) {
        return;
    }
    const float edgeWeight = 1.f - 0.5f * clamp01(alpha);
    *spillChannel = clamp01(*spillChannel - spill * s * edgeWeight);
}

static void
writeCorridorKeyOutput(const std::vector<float>& alpha,
                       const std::vector<float>& fg,
                       int modelW,
                       int modelH,
                       const RectI& sourceBounds,
                       const RectI& roi,
                       FluxCorridorKeyOutputMode outputMode,
                       bool invert,
                       FluxCorridorKeyScreenColor screenColor,
                       double despillStrength,
                       const ImagePtr& outputImg)
{
    if (!outputImg) {
        return;
    }
    const RectI intersection = roi.intersect(outputImg->getBounds());
    if (intersection.isNull()) {
        return;
    }
    const int sourceW = sourceBounds.width();
    const int sourceH = sourceBounds.height();
    if (sourceW <= 0 || sourceH <= 0) {
        outputImg->fillZero(intersection);
        return;
    }

    const int nComps = (int)outputImg->getComponentsCount();
    Image::WriteAccess writeAccess(outputImg.get());
    for (int y = intersection.y1; y < intersection.y2; ++y) {
        const double modelY = (((double)y - sourceBounds.y1 + 0.5) * modelH / sourceH) - 0.5;
        unsigned char* raw = writeAccess.pixelAt(intersection.x1, y);
        for (int x = intersection.x1; x < intersection.x2; ++x) {
            const double modelX = (((double)x - sourceBounds.x1 + 0.5) * modelW / sourceW) - 0.5;
            float a = sampleTensor1Bilinear(alpha, modelW, modelH, modelX, modelY);
            if (invert) {
                a = 1.f - a;
            }
            float r = sampleTensor3Bilinear(fg, modelW, modelH, 0, modelX, modelY);
            float g = sampleTensor3Bilinear(fg, modelW, modelH, 1, modelX, modelY);
            float b = sampleTensor3Bilinear(fg, modelW, modelH, 2, modelX, modelY);
            if (outputMode != eFluxCorridorKeyOutputAlpha) {
                applyCorridorKeyDespill(&r, &g, &b, a, screenColor, despillStrength);
            }
            switch (outputImg->getBitDepth()) {
            case eImageBitDepthByte:
                writeCorridorKeyPixel((unsigned char*)raw, nComps, outputMode, r, g, b, a);
                raw += nComps * sizeof(unsigned char);
                break;
            case eImageBitDepthShort:
                writeCorridorKeyPixel((unsigned short*)raw, nComps, outputMode, r, g, b, a);
                raw += nComps * sizeof(unsigned short);
                break;
            case eImageBitDepthFloat:
                writeCorridorKeyPixel((float*)raw, nComps, outputMode, r, g, b, a);
                raw += nComps * sizeof(float);
                break;
            default:
                break;
            }
        }
    }
}
#endif

#ifdef FLUX_ENABLE_CORRIDORKEY_TENSORRT


class CorridorKeyTensorRtDso
{
public:
    typedef void* (*CreateInferRuntimeInternalFn)(void*, int32_t);

    CorridorKeyTensorRtDso()
        : _core(nullptr)
        , _plugin(nullptr)
        , _createInferRuntimeInternal(nullptr)
    {
    }

    ~CorridorKeyTensorRtDso()
    {
        if (_plugin) {
            dlclose(_plugin);
        }
        if (_core) {
            dlclose(_core);
        }
    }

    bool ensureLoaded(std::string* error)
    {
        if (_createInferRuntimeInternal) {
            return true;
        }
        _plugin = dlopen("libnvinfer_plugin.so.10", RTLD_LAZY | RTLD_LOCAL);
        _core = dlopen("libnvinfer.so.10", RTLD_NOW | RTLD_LOCAL);
        if (!_core) {
            if (error) {
                const char* dlErr = dlerror();
                *error = "TensorRT runtime library libnvinfer.so.10 is unavailable. Install/stage the CorridorKey TensorRT toolkit before using this node";
                if (dlErr && *dlErr) {
                    *error += ": ";
                    *error += dlErr;
                }
            }
            return false;
        }
        dlerror();
        _createInferRuntimeInternal = reinterpret_cast<CreateInferRuntimeInternalFn>(dlsym(_core, "createInferRuntime_INTERNAL"));
        const char* symErr = dlerror();
        if (!_createInferRuntimeInternal || symErr) {
            if (error) {
                *error = "TensorRT symbol createInferRuntime_INTERNAL is unavailable in libnvinfer.so.10";
                if (symErr && *symErr) {
                    *error += ": ";
                    *error += symErr;
                }
            }
            return false;
        }
        return true;
    }

    nvinfer1::IRuntime* createInferRuntime(nvinfer1::ILogger& logger)
    {
        if (!_createInferRuntimeInternal) {
            return nullptr;
        }
        return static_cast<nvinfer1::IRuntime*>(_createInferRuntimeInternal(&logger, NV_TENSORRT_VERSION));
    }

private:
    void* _core;
    void* _plugin;
    CreateInferRuntimeInternalFn _createInferRuntimeInternal;
};

static CorridorKeyTensorRtDso&
corridorKeyTensorRtDso()
{
    static CorridorKeyTensorRtDso dso;
    return dso;
}

class CorridorKeyCudaDso
{
public:
    typedef const char* (*GetErrorStringFn)(cudaError_t);
    typedef cudaError_t (*MemGetInfoFn)(std::size_t*, std::size_t*);
    typedef cudaError_t (*FreeFn)(void*);
    typedef cudaError_t (*StreamDestroyFn)(cudaStream_t);
    typedef cudaError_t (*SetDeviceFn)(int);
    typedef cudaError_t (*StreamCreateFn)(cudaStream_t*);
    typedef cudaError_t (*MallocFn)(void**, std::size_t);
    typedef cudaError_t (*MemcpyAsyncFn)(void*, const void*, std::size_t, cudaMemcpyKind, cudaStream_t);
    typedef cudaError_t (*StreamSynchronizeFn)(cudaStream_t);

    CorridorKeyCudaDso()
        : _handle(nullptr), getErrorStringFn(nullptr), memGetInfoFn(nullptr), freeFn(nullptr),
          streamDestroyFn(nullptr), setDeviceFn(nullptr), streamCreateFn(nullptr), mallocFn(nullptr),
          memcpyAsyncFn(nullptr), streamSynchronizeFn(nullptr)
    {
    }

    ~CorridorKeyCudaDso()
    {
        if (_handle) {
            dlclose(_handle);
        }
    }

    bool ensureLoaded(std::string* error)
    {
        if (_handle) {
            return true;
        }
        const char* candidates[] = {"libcudart.so.13", "libcudart.so.12", "libcudart.so"};
        for (const char* candidate : candidates) {
            _handle = dlopen(candidate, RTLD_NOW | RTLD_LOCAL);
            if (_handle) {
                break;
            }
        }
        if (!_handle) {
            if (error) {
                const char* dlErr = dlerror();
                *error = "CUDA runtime library libcudart is unavailable. Install/stage CUDA runtime before using CorridorKey";
                if (dlErr && *dlErr) {
                    *error += ": ";
                    *error += dlErr;
                }
            }
            return false;
        }
        getErrorStringFn = reinterpret_cast<GetErrorStringFn>(dlsym(_handle, "cudaGetErrorString"));
        memGetInfoFn = reinterpret_cast<MemGetInfoFn>(dlsym(_handle, "cudaMemGetInfo"));
        freeFn = reinterpret_cast<FreeFn>(dlsym(_handle, "cudaFree"));
        streamDestroyFn = reinterpret_cast<StreamDestroyFn>(dlsym(_handle, "cudaStreamDestroy"));
        setDeviceFn = reinterpret_cast<SetDeviceFn>(dlsym(_handle, "cudaSetDevice"));
        streamCreateFn = reinterpret_cast<StreamCreateFn>(dlsym(_handle, "cudaStreamCreate"));
        mallocFn = reinterpret_cast<MallocFn>(dlsym(_handle, "cudaMalloc"));
        memcpyAsyncFn = reinterpret_cast<MemcpyAsyncFn>(dlsym(_handle, "cudaMemcpyAsync"));
        streamSynchronizeFn = reinterpret_cast<StreamSynchronizeFn>(dlsym(_handle, "cudaStreamSynchronize"));
        if (!getErrorStringFn || !memGetInfoFn || !freeFn || !streamDestroyFn || !setDeviceFn ||
            !streamCreateFn || !mallocFn || !memcpyAsyncFn || !streamSynchronizeFn) {
            if (error) {
                *error = "CUDA runtime library is missing one or more required CorridorKey symbols.";
            }
            return false;
        }
        return true;
    }

    const char* errorString(cudaError_t err) const { return getErrorStringFn ? getErrorStringFn(err) : "CUDA runtime unavailable"; }
    cudaError_t memGetInfo(std::size_t* freeBytes, std::size_t* totalBytes) const { return memGetInfoFn(freeBytes, totalBytes); }
    cudaError_t freeDevice(void* ptr) const { return freeFn ? freeFn(ptr) : cudaSuccess; }
    cudaError_t streamDestroy(cudaStream_t stream) const { return streamDestroyFn ? streamDestroyFn(stream) : cudaSuccess; }
    cudaError_t setDevice(int gpu) const { return setDeviceFn(gpu); }
    cudaError_t streamCreate(cudaStream_t* stream) const { return streamCreateFn(stream); }
    cudaError_t mallocDevice(void** ptr, std::size_t bytes) const { return mallocFn(ptr, bytes); }
    cudaError_t memcpyAsync(void* dst, const void* src, std::size_t bytes, cudaMemcpyKind kind, cudaStream_t stream) const { return memcpyAsyncFn(dst, src, bytes, kind, stream); }
    cudaError_t streamSynchronize(cudaStream_t stream) const { return streamSynchronizeFn(stream); }

private:
    void* _handle;
    GetErrorStringFn getErrorStringFn;
    MemGetInfoFn memGetInfoFn;
    FreeFn freeFn;
    StreamDestroyFn streamDestroyFn;
    SetDeviceFn setDeviceFn;
    StreamCreateFn streamCreateFn;
    MallocFn mallocFn;
    MemcpyAsyncFn memcpyAsyncFn;
    StreamSynchronizeFn streamSynchronizeFn;
};

static CorridorKeyCudaDso&
corridorKeyCudaDso()
{
    static CorridorKeyCudaDso dso;
    return dso;
}

class CorridorKeyTensorRtLogger
    : public nvinfer1::ILogger
{
public:
    void log(Severity severity, const char* msg) noexcept OVERRIDE
    {
        if (!msg) {
            return;
        }
        if (severity <= Severity::kWARNING) {
            _lastWarning = std::string(severityName(severity)) + ": " + msg;
        }
        std::fprintf(stderr, "CorridorKey TensorRT %s: %s\n", severityName(severity), msg);
    }

    std::string lastWarning() const { return _lastWarning; }

private:
    static const char* severityName(Severity severity)
    {
        switch (severity) {
        case Severity::kINTERNAL_ERROR: return "internal_error";
        case Severity::kERROR: return "error";
        case Severity::kWARNING: return "warning";
        case Severity::kINFO: return "info";
        case Severity::kVERBOSE: return "verbose";
        default: return "unknown";
        }
    }

private:
    std::string _lastWarning;
};

static std::size_t
volumeForDims(const nvinfer1::Dims& dims)
{
    if (dims.nbDims <= 0) {
        return 0;
    }
    std::size_t volume = 1;
    for (int i = 0; i < dims.nbDims; ++i) {
        if (dims.d[i] <= 0) {
            return 0;
        }
        volume *= (std::size_t)dims.d[i];
    }
    return volume;
}

static bool
shapeHasDynamicDim(const nvinfer1::Dims& dims)
{
    for (int i = 0; i < dims.nbDims; ++i) {
        if (dims.d[i] < 0) {
            return true;
        }
    }
    return false;
}

static std::string
cudaErrorString(cudaError_t err)
{
    return std::string(corridorKeyCudaDso().errorString(err));
}

static bool
ensureCudaOk(cudaError_t err,
             const char* action,
             std::string* error)
{
    if (err == cudaSuccess) {
        return true;
    }
    if (error) {
        *error = std::string(action) + " failed: " + cudaErrorString(err);
    }
    return false;
}

static std::string
cudaMemorySummary()
{
    std::size_t freeBytes = 0;
    std::size_t totalBytes = 0;
    const cudaError_t err = corridorKeyCudaDso().memGetInfo(&freeBytes, &totalBytes);
    if (err != cudaSuccess) {
        return std::string("cudaMemGetInfo failed: ") + cudaErrorString(err);
    }
    std::ostringstream os;
    os << "CUDA memory free=" << (freeBytes / (1024 * 1024)) << " MiB total=" << (totalBytes / (1024 * 1024)) << " MiB";
    return os.str();
}

struct CorridorKeyTensorRtCache
{
    CorridorKeyTensorRtCache()
        : stream(nullptr)
        , inputDevice(nullptr)
        , alphaDevice(nullptr)
        , fgDevice(nullptr)
        , inputBytes(0)
        , alphaBytes(0)
        , fgBytes(0)
        , modelW(0)
        , modelH(0)
        , requestedW(0)
        , requestedH(0)
        , gpu(-1)
    {
    }

    ~CorridorKeyTensorRtCache()
    {
        reset();
    }

    void reset()
    {
        if (inputDevice) {
            corridorKeyCudaDso().freeDevice(inputDevice);
            inputDevice = nullptr;
        }
        if (alphaDevice) {
            corridorKeyCudaDso().freeDevice(alphaDevice);
            alphaDevice = nullptr;
        }
        if (fgDevice) {
            corridorKeyCudaDso().freeDevice(fgDevice);
            fgDevice = nullptr;
        }
        if (stream) {
            corridorKeyCudaDso().streamDestroy(stream);
            stream = nullptr;
        }
        context.reset();
        engine.reset();
        runtime.reset();
        hostInput.clear();
        hostAlpha.clear();
        hostFg.clear();
        enginePath.clear();
        inputBytes = 0;
        alphaBytes = 0;
        fgBytes = 0;
        modelW = 0;
        requestedW = 0;
        requestedH = 0;
        modelH = 0;
        gpu = -1;
    }

    bool ensure(const std::string& requestedEnginePath,
                int requestedGpu,
                int requestedModelW,
                int requestedModelH,
                std::string* error)
    {
        if (requestedEnginePath.empty()) {
            if (error) {
                *error = "CorridorKey engine_path is empty. Set a local user-supplied TensorRT .engine file.";
            }
            return false;
        }
        if (requestedModelW <= 0 || requestedModelH <= 0) {
            if (error) {
                *error = "CorridorKey model resolution must be positive.";
            }
            return false;
        }
        if (runtime && engine && context && requestedEnginePath == enginePath && requestedGpu == gpu && requestedModelW == requestedW && requestedModelH == requestedH) {
            return true;
        }

        reset();
        gpu = requestedGpu;
        modelW = requestedModelW;
        modelH = requestedModelH;
        requestedW = requestedModelW;
        requestedH = requestedModelH;
        enginePath = requestedEnginePath;

        if (!corridorKeyCudaDso().ensureLoaded(error) || !ensureCudaOk(corridorKeyCudaDso().setDevice(gpu), "cudaSetDevice", error)) {
            reset();
            return false;
        }

        std::ifstream file(enginePath.c_str(), std::ios::binary | std::ios::ate);
        if (!file) {
            if (error) {
                *error = "Could not open CorridorKey TensorRT engine: " + enginePath;
            }
            reset();
            return false;
        }
        const std::streamsize engineSize = file.tellg();
        if (engineSize <= 0) {
            if (error) {
                *error = "CorridorKey TensorRT engine is empty: " + enginePath;
            }
            reset();
            return false;
        }
        file.seekg(0, std::ios::beg);
        std::vector<char> blob((std::size_t)engineSize);
        if (!file.read(blob.data(), engineSize)) {
            if (error) {
                *error = "Could not read CorridorKey TensorRT engine: " + enginePath;
            }
            reset();
            return false;
        }

        if (!corridorKeyTensorRtDso().ensureLoaded(error)) {
            reset();
            return false;
        }
        runtime.reset(corridorKeyTensorRtDso().createInferRuntime(logger));
        if (!runtime) {
            if (error) {
                *error = "TensorRT createInferRuntime failed.";
            }
            reset();
            return false;
        }
        engine.reset(runtime->deserializeCudaEngine(blob.data(), blob.size()));
        if (!engine) {
            if (error) {
                *error = "TensorRT could not deserialize CorridorKey engine: " + enginePath;
            }
            reset();
            return false;
        }
        {
            const nvinfer1::Dims staticInputShape = engine->getTensorShape(kCorridorKeyInputTensor);
            if (staticInputShape.nbDims == 4 && staticInputShape.d[2] > 0 && staticInputShape.d[3] > 0) {
                modelH = (int)staticInputShape.d[2];
                modelW = (int)staticInputShape.d[3];
            }
        }

        if (!validateEngine(error)) {
            reset();
            return false;
        }

#if NV_TENSORRT_MAJOR >= 10
        context.reset(engine->createExecutionContext(nvinfer1::ExecutionContextAllocationStrategy::kON_PROFILE_CHANGE));
#else
        context.reset(engine->createExecutionContext());
#endif
        if (!context) {
            if (error) {
                *error = "TensorRT could not create CorridorKey execution context. " + cudaMemorySummary();
                const std::string last = logger.lastWarning();
                if (!last.empty()) {
                    *error += ". Last TensorRT message: " + last;
                }
            }
            reset();
            return false;
        }

        nvinfer1::Dims inputShape = engine->getTensorShape(kCorridorKeyInputTensor);
        if (shapeHasDynamicDim(inputShape)) {
            if (!context->setInputShape(kCorridorKeyInputTensor, nvinfer1::Dims4(1, 4, modelH, modelW))) {
                if (error) {
                    *error = "TensorRT rejected CorridorKey input shape [1,4,H,W].";
                }
                reset();
                return false;
            }
        }

        nvinfer1::Dims alphaShape = context->getTensorShape(kCorridorKeyAlphaTensor);
        nvinfer1::Dims fgShape = context->getTensorShape(kCorridorKeyFgTensor);
        if (!validateRuntimeShape(alphaShape, 1, error) || !validateRuntimeShape(fgShape, 3, error)) {
            reset();
            return false;
        }

        const std::size_t inputFloats = (std::size_t)4 * modelW * modelH;
        const std::size_t alphaFloats = volumeForDims(alphaShape);
        const std::size_t fgFloats = volumeForDims(fgShape);
        if (inputFloats == 0 || alphaFloats != (std::size_t)modelW * modelH || fgFloats != (std::size_t)3 * modelW * modelH) {
            if (error) {
                *error = "CorridorKey TensorRT engine tensor sizes do not match the selected model resolution.";
            }
            reset();
            return false;
        }

        hostInput.resize(inputFloats);
        hostAlpha.resize(alphaFloats);
        hostFg.resize(fgFloats);
        inputBytes = inputFloats * sizeof(float);
        alphaBytes = alphaFloats * sizeof(float);
        fgBytes = fgFloats * sizeof(float);

        if (!ensureCudaOk(corridorKeyCudaDso().streamCreate(&stream), "cudaStreamCreate", error) ||
            !ensureCudaOk(corridorKeyCudaDso().mallocDevice(&inputDevice, inputBytes), "cudaMalloc(input)", error) ||
            !ensureCudaOk(corridorKeyCudaDso().mallocDevice(&alphaDevice, alphaBytes), "cudaMalloc(alpha)", error) ||
            !ensureCudaOk(corridorKeyCudaDso().mallocDevice(&fgDevice, fgBytes), "cudaMalloc(fg)", error)) {
            reset();
            return false;
        }

        return true;
    }

    bool run(const ImagePtr& plate,
             const ImagePtr& mask,
             std::string* error)
    {
        if (!plate || !mask || !context) {
            if (error) {
                *error = "CorridorKey inference requires connected plate and mask inputs.";
            }
            return false;
        }
        if (!corridorKeyCudaDso().ensureLoaded(error) || !ensureCudaOk(corridorKeyCudaDso().setDevice(gpu), "cudaSetDevice", error)) {
            return false;
        }

        preprocess(plate, mask);

        if (!context->setInputTensorAddress(kCorridorKeyInputTensor, inputDevice) ||
            !context->setOutputTensorAddress(kCorridorKeyAlphaTensor, alphaDevice) ||
            !context->setOutputTensorAddress(kCorridorKeyFgTensor, fgDevice)) {
            if (error) {
                *error = "TensorRT could not bind CorridorKey tensor addresses.";
            }
            return false;
        }

        if (!ensureCudaOk(corridorKeyCudaDso().memcpyAsync(inputDevice, hostInput.data(), inputBytes, cudaMemcpyHostToDevice, stream), "cudaMemcpyAsync(input)", error)) {
            return false;
        }
        if (!context->enqueueV3(stream)) {
            if (error) {
                *error = "TensorRT CorridorKey enqueueV3 failed.";
            }
            return false;
        }
        if (!ensureCudaOk(corridorKeyCudaDso().memcpyAsync(hostAlpha.data(), alphaDevice, alphaBytes, cudaMemcpyDeviceToHost, stream), "cudaMemcpyAsync(alpha)", error) ||
            !ensureCudaOk(corridorKeyCudaDso().memcpyAsync(hostFg.data(), fgDevice, fgBytes, cudaMemcpyDeviceToHost, stream), "cudaMemcpyAsync(fg)", error) ||
            !ensureCudaOk(corridorKeyCudaDso().streamSynchronize(stream), "cudaStreamSynchronize", error)) {
            return false;
        }
        return true;
    }

    bool validateEngine(std::string* error) const
    {
        if (!engine) {
            return false;
        }
        bool foundInput = false;
        bool foundAlpha = false;
        bool foundFg = false;
        for (int i = 0; i < engine->getNbIOTensors(); ++i) {
            const char* name = engine->getIOTensorName(i);
            if (!name) {
                continue;
            }
            const nvinfer1::TensorIOMode mode = engine->getTensorIOMode(name);
            const nvinfer1::DataType type = engine->getTensorDataType(name);
            if (type != nvinfer1::DataType::kFLOAT) {
                if (std::string(name) == kCorridorKeyInputTensor || std::string(name) == kCorridorKeyAlphaTensor || std::string(name) == kCorridorKeyFgTensor) {
                    if (error) {
                        *error = "CorridorKey TensorRT tensor '" + std::string(name) + "' must be float32.";
                    }
                    return false;
                }
            }
            if (std::string(name) == kCorridorKeyInputTensor) {
                foundInput = mode == nvinfer1::TensorIOMode::kINPUT;
            } else if (std::string(name) == kCorridorKeyAlphaTensor) {
                foundAlpha = mode == nvinfer1::TensorIOMode::kOUTPUT;
            } else if (std::string(name) == kCorridorKeyFgTensor) {
                foundFg = mode == nvinfer1::TensorIOMode::kOUTPUT;
            }
        }
        if (!foundInput || !foundAlpha || !foundFg) {
            if (error) {
                *error = "CorridorKey TensorRT engine must expose input tensor 'input' and output tensors 'alpha' and 'fg'.";
            }
            return false;
        }

        const nvinfer1::Dims inputShape = engine->getTensorShape(kCorridorKeyInputTensor);
        if (inputShape.nbDims != 4 || (inputShape.d[0] > 0 && inputShape.d[0] != 1) || (inputShape.d[1] > 0 && inputShape.d[1] != 4) ||
            (inputShape.d[2] > 0 && inputShape.d[2] != modelH) || (inputShape.d[3] > 0 && inputShape.d[3] != modelW)) {
            if (error) {
                *error = "CorridorKey TensorRT input tensor must have shape [1,4,H,W] matching the selected resolution.";
            }
            return false;
        }
        return true;
    }

    bool validateRuntimeShape(const nvinfer1::Dims& dims,
                              int expectedChannels,
                              std::string* error) const
    {
        if (dims.nbDims != 4 || dims.d[0] != 1 || dims.d[1] != expectedChannels || dims.d[2] != modelH || dims.d[3] != modelW) {
            if (error) {
                *error = "CorridorKey TensorRT output tensor shape does not match [1,C,H,W] for the selected resolution.";
            }
            return false;
        }
        return true;
    }

    void preprocess(const ImagePtr& plate,
                    const ImagePtr& mask)
    {
        static const float mean[3] = {0.485f, 0.456f, 0.406f};
        static const float stddev[3] = {0.229f, 0.224f, 0.225f};
        const RectI plateBounds = plate->getBounds();
        const int sourceW = std::max(1, plateBounds.width());
        const int sourceH = std::max(1, plateBounds.height());
        const RectI maskBounds = mask->getBounds();
        const int maskW = std::max(1, maskBounds.width());
        const int maskH = std::max(1, maskBounds.height());
        const int alphaComp = maskComponentIndex(mask);
        Image::ReadAccess plateAccess(plate.get());
        Image::ReadAccess maskAccess(mask.get());
        const std::size_t plane = (std::size_t)modelW * modelH;

        for (int y = 0; y < modelH; ++y) {
            const double srcY = plateBounds.y1 + (((double)y + 0.5) * sourceH / modelH) - 0.5;
            const double maskY = maskBounds.y1 + (((double)y + 0.5) * maskH / modelH) - 0.5;
            for (int x = 0; x < modelW; ++x) {
                const double srcX = plateBounds.x1 + (((double)x + 0.5) * sourceW / modelW) - 0.5;
                const double maskX = maskBounds.x1 + (((double)x + 0.5) * maskW / modelW) - 0.5;
                const std::size_t offset = (std::size_t)y * modelW + x;
                for (int c = 0; c < 3; ++c) {
                    const float rgb = clamp01(sampleComponentBilinear(plate, &plateAccess, srcX, srcY, c, 0.f));
                    hostInput[(std::size_t)c * plane + offset] = (rgb - mean[c]) / stddev[c];
                }
                hostInput[3 * plane + offset] = clamp01(sampleComponentBilinear(mask, &maskAccess, maskX, maskY, alphaComp, 0.f));
            }
        }
    }

    std::mutex mutex;
    CorridorKeyTensorRtLogger logger;
    std::unique_ptr<nvinfer1::IRuntime> runtime;
    std::unique_ptr<nvinfer1::ICudaEngine> engine;
    std::unique_ptr<nvinfer1::IExecutionContext> context;
    cudaStream_t stream;
    void* inputDevice;
    void* alphaDevice;
    void* fgDevice;
    std::size_t inputBytes;
    std::size_t alphaBytes;
    std::size_t fgBytes;
    int modelW;
    int modelH;
    int requestedW;
    int requestedH;
    int gpu;
    std::string enginePath;
    std::vector<float> hostInput;
    std::vector<float> hostAlpha;
    std::vector<float> hostFg;
};

#endif // FLUX_ENABLE_CORRIDORKEY_TENSORRT

} // namespace

struct FluxCorridorKeyPrivate
{
    KnobStringWPtr enginePathKnob;
    KnobChoiceWPtr resolutionKnob;
    KnobIntWPtr modelWidthKnob;
    KnobIntWPtr modelHeightKnob;
    KnobChoiceWPtr outputModeKnob;
    KnobBoolWPtr invertKnob;
    KnobChoiceWPtr screenColorKnob;
    KnobDoubleWPtr despillStrengthKnob;
    KnobIntWPtr gpuKnob;
    KnobStringWPtr statusKnob;
    KnobStringWPtr licenseNoticeKnob;
#ifdef FLUX_ENABLE_CORRIDORKEY_TENSORRT
    CorridorKeyTensorRtCache trt;
#endif
};

FluxCorridorKey::FluxCorridorKey(NodePtr node)
    : EffectInstance(node)
    , _imp(new FluxCorridorKeyPrivate())
{
}

FluxCorridorKey::~FluxCorridorKey()
{
}

std::string
FluxCorridorKey::getPluginDescription() const
{
    return "CorridorKey foreground/alpha refinement node. Input 0 is the RGB/RGBA green/blue-screen plate; input 1 is a coarse alpha hint. TensorRT runtime support is optional and requires a user-supplied CorridorKey engine file.";
}

void
FluxCorridorKey::addAcceptedComponents(int inputNb,
                                       std::list<ImagePlaneDesc>* comps)
{
    if (inputNb == 1) {
        // SAM3/AI Paint mask PNGs carry the matte in RGB/red. Prefer color
        // planes so the host does not negotiate an empty alpha-only mask.
        comps->push_back(ImagePlaneDesc::getRGBAComponents());
        comps->push_back(ImagePlaneDesc::getRGBComponents());
        comps->push_back(ImagePlaneDesc::getAlphaComponents());
        return;
    }
    comps->push_back(ImagePlaneDesc::getRGBAComponents());
    comps->push_back(ImagePlaneDesc::getRGBComponents());
    comps->push_back(ImagePlaneDesc::getAlphaComponents());
}

void
FluxCorridorKey::addSupportedBitDepth(std::list<ImageBitDepthEnum>* depths) const
{
    depths->push_back(eImageBitDepthByte);
    depths->push_back(eImageBitDepthShort);
    depths->push_back(eImageBitDepthFloat);
}

void
FluxCorridorKey::initializeKnobs()
{
    KnobPagePtr page = AppManager::createKnob<KnobPage>(this, tr("Controls"));

    KnobStringPtr enginePath = AppManager::createKnob<KnobString>(this, tr("TensorRT Engine File"), 1, false);
    enginePath->setName("engine_path");
    enginePath->setAnimationEnabled(false);
    const std::string defaultEngine = defaultCorridorKeyEnginePath();
    if (!defaultEngine.empty()) {
        enginePath->setDefaultValue(defaultEngine);
    }
    enginePath->setHintToolTip(tr("Optional manual TensorRT .engine file. Leave at the local default to let Resolution auto-select engines from FLUX_CORRIDORKEY_ENGINE_DIR."));
    page->addKnob(enginePath);
    _imp->enginePathKnob = enginePath;

    KnobChoicePtr resolution = AppManager::createKnob<KnobChoice>(this, tr("Resolution"), 1, false);
    resolution->setName("resolution");
    resolution->setAnimationEnabled(false);
    resolution->populateChoices(makeResolutionChoices());
    resolution->setDefaultValue(eFluxCorridorKeyResolution1024);
    resolution->setHintToolTip(tr("Internal model tensor size. Output resolution always remains the connected plate input resolution."));
    page->addKnob(resolution);
    _imp->resolutionKnob = resolution;

    KnobIntPtr modelWidth = AppManager::createKnob<KnobInt>(this, tr("Custom Width"), 1, false);
    modelWidth->setName("model_w");
    modelWidth->setAnimationEnabled(false);
    modelWidth->setMinimum(1);
    modelWidth->setDefaultValue(1024);
    modelWidth->setHintToolTip(tr("Custom model width for dynamic/custom TensorRT engines. Static engines use their embedded input width."));
    page->addKnob(modelWidth);
    _imp->modelWidthKnob = modelWidth;

    KnobIntPtr modelHeight = AppManager::createKnob<KnobInt>(this, tr("Custom Height"), 1, false);
    modelHeight->setName("model_h");
    modelHeight->setAnimationEnabled(false);
    modelHeight->setMinimum(1);
    modelHeight->setDefaultValue(1024);
    modelHeight->setHintToolTip(tr("Custom model height for dynamic/custom TensorRT engines. Static engines use their embedded input height."));
    page->addKnob(modelHeight);
    _imp->modelHeightKnob = modelHeight;

    KnobChoicePtr outputMode = AppManager::createKnob<KnobChoice>(this, tr("Output"), 1, false);
    outputMode->setName("output_mode");
    outputMode->setAnimationEnabled(false);
    outputMode->populateChoices(makeOutputModeChoices());
    outputMode->setDefaultValue(eFluxCorridorKeyOutputRGBA);
    outputMode->setHintToolTip(tr("CorridorKey output stream: straight foreground with alpha, alpha only, or foreground only."));
    page->addKnob(outputMode);
    _imp->outputModeKnob = outputMode;

    KnobBoolPtr invert = AppManager::createKnob<KnobBool>(this, tr("Invert Matte"), 1, false);
    invert->setName("invert");
    invert->setAnimationEnabled(false);
    invert->setDefaultValue(false);
    invert->setHintToolTip(tr("Invert the CorridorKey alpha matte after inference."));
    page->addKnob(invert);
    _imp->invertKnob = invert;

    KnobChoicePtr screenColor = AppManager::createKnob<KnobChoice>(this, tr("Screen Color"), 1, false);
    screenColor->setName("screen_color");
    screenColor->setAnimationEnabled(false);
    screenColor->populateChoices(makeScreenColorChoices());
    screenColor->setDefaultValue(eFluxCorridorKeyScreenAuto);
    screenColor->setHintToolTip(tr("Screen color for post-inference despill."));
    page->addKnob(screenColor);
    _imp->screenColorKnob = screenColor;

    KnobDoublePtr despillStrength = AppManager::createKnob<KnobDouble>(this, tr("Despill Strength"), 1, false);
    despillStrength->setName("despill_strength");
    despillStrength->setAnimationEnabled(false);
    despillStrength->setMinimum(0.);
    despillStrength->setMaximum(1.);
    despillStrength->setDefaultValue(1.);
    despillStrength->setHintToolTip(tr("Post-inference RGB despill. Alpha is unchanged. Set 0 to see raw CorridorKey foreground color."));
    page->addKnob(despillStrength);
    _imp->despillStrengthKnob = despillStrength;

    KnobIntPtr gpu = AppManager::createKnob<KnobInt>(this, tr("GPU Device"), 1, false);
    gpu->setName("gpu");
    gpu->setAnimationEnabled(false);
    gpu->setMinimum(0);
    gpu->setDefaultValue(0);
    gpu->setHintToolTip(tr("CUDA GPU device index used by the TensorRT runtime when CorridorKey support is enabled."));
    page->addKnob(gpu);
    _imp->gpuKnob = gpu;

    KnobStringPtr status = AppManager::createKnob<KnobString>(this, tr("Status"), 1, false);
    status->setName("status");
    status->setAsLabel();
    status->setAnimationEnabled(false);
#ifdef FLUX_ENABLE_CORRIDORKEY_TENSORRT
    status->setDefaultValue("TensorRT build support enabled; set engine_path to a local CorridorKey .engine file.");
#else
    status->setDefaultValue("TensorRT build support disabled; this node passes the plate through and reports a disabled runtime error.");
#endif
    page->addKnob(status);
    _imp->statusKnob = status;

    KnobStringPtr licenseNotice = AppManager::createKnob<KnobString>(this, tr("License Notice"), 1, false);
    licenseNotice->setName("license_notice");
    licenseNotice->setAsLabel();
    licenseNotice->setAnimationEnabled(false);
    licenseNotice->setDefaultValue("CorridorKey is CC BY-NC-SA 4.0 with additional terms. Flux does not bundle CorridorKey assets; commercial software integration requires a separate written agreement from Corridor Digital.");
    page->addKnob(licenseNotice);
    _imp->licenseNoticeKnob = licenseNotice;
}

StatusEnum
FluxCorridorKey::getRegionOfDefinition(U64 hash,
                                        double time,
                                        const RenderScale& scale,
                                        ViewIdx view,
                                        RectD* rod)
{
    if (getInputRod(this, 0, hash, time, scale, view, rod)) {
        return eStatusOK;
    }
    return eStatusReplyDefault;
}

StatusEnum
FluxCorridorKey::render(const RenderActionArgs& args)
{
    ImagePtr plateImg;
    EffectInstance::InputImagesMap::const_iterator plateIt = args.inputImages.find(0);
    if (plateIt != args.inputImages.end() && !plateIt->second.empty()) {
        plateImg = plateIt->second.front();
    }

    ImagePtr maskImg;
    EffectInstance::InputImagesMap::const_iterator maskIt = args.inputImages.find(1);
    if (maskIt != args.inputImages.end() && !maskIt->second.empty()) {
        maskImg = maskIt->second.front();
    }
    KnobChoicePtr screenColorKnob = _imp->screenColorKnob.lock();
    const FluxCorridorKeyScreenColor screenColor = (FluxCorridorKeyScreenColor)(screenColorKnob ? clampChoiceIndex(screenColorKnob->getValue(), 3) : eFluxCorridorKeyScreenAuto);
    KnobDoublePtr despillStrengthKnob = _imp->despillStrengthKnob.lock();
    const double despillStrength = despillStrengthKnob ? std::max(0., std::min(1., despillStrengthKnob->getValue())) : 0.;

    KnobChoicePtr outputModeKnob = _imp->outputModeKnob.lock();
    const FluxCorridorKeyOutputMode outputMode = (FluxCorridorKeyOutputMode)(outputModeKnob ? clampChoiceIndex(outputModeKnob->getValue(), 3) : eFluxCorridorKeyOutputRGBA);

#ifdef FLUX_ENABLE_CORRIDORKEY_TENSORRT
    KnobBoolPtr invertKnob = _imp->invertKnob.lock();
    const bool invert = invertKnob && invertKnob->getValue();
    KnobStringPtr enginePathKnob = _imp->enginePathKnob.lock();
    KnobChoicePtr resolutionKnob = _imp->resolutionKnob.lock();
    KnobIntPtr modelWidthKnob = _imp->modelWidthKnob.lock();
    KnobIntPtr modelHeightKnob = _imp->modelHeightKnob.lock();
    KnobIntPtr gpuKnob = _imp->gpuKnob.lock();

    const int resolutionChoice = resolutionKnob ? clampChoiceIndex(resolutionKnob->getValue(), 5) : eFluxCorridorKeyResolution1024;
    const int modelW = resolutionChoice == eFluxCorridorKeyResolutionCustom
        ? std::max(1, modelWidthKnob ? modelWidthKnob->getValue() : 1024)
        : resolutionChoiceToSize(resolutionChoice);
    const int modelH = resolutionChoice == eFluxCorridorKeyResolutionCustom
        ? std::max(1, modelHeightKnob ? modelHeightKnob->getValue() : 1024)
        : resolutionChoiceToSize(resolutionChoice);
    const int gpu = std::max(0, gpuKnob ? gpuKnob->getValue() : 0);
    const std::string knobEnginePath = enginePathKnob ? enginePathKnob->getValue() : std::string();
    const std::string enginePath = resolveCorridorKeyEnginePath(knobEnginePath, resolutionChoice);

    std::string error;
    bool ok = false;
    RectI frameBounds;
    if (!args.outputPlanes.empty() && args.outputPlanes.front().second) {
        frameBounds = args.outputPlanes.front().second->getBounds();
    } else if (plateImg) {
        frameBounds = plateImg->getBounds();
    } else {
        frameBounds = args.roi;
    }
    if (plateImg && maskImg) {
        std::lock_guard<std::mutex> guard(_imp->trt.mutex);
        ok = _imp->trt.ensure(enginePath, gpu, modelW, modelH, &error) && _imp->trt.run(plateImg, maskImg, &error);
        if (ok) {
            clearPersistentMessage(false);
            for (std::list<std::pair<ImagePlaneDesc, ImagePtr> >::const_iterator it = args.outputPlanes.begin();
                 it != args.outputPlanes.end(); ++it) {
                writeCorridorKeyOutput(_imp->trt.hostAlpha, _imp->trt.hostFg, modelW, modelH, frameBounds, args.roi, outputMode, invert, screenColor, despillStrength, it->second);
            }
            return eStatusOK;
        }
    } else {
        error = "CorridorKey inference requires both plate input 0 and mask input 1.";
    }
    setPersistentMessage(eMessageTypeError, error.empty() ? "CorridorKey TensorRT inference failed." : error);
#else
    setPersistentMessage(eMessageTypeError, "CorridorKey TensorRT support is disabled in this Flux build. Reconfigure with FLUX_ENABLE_CORRIDORKEY_TENSORRT=ON and provide a user-supplied CorridorKey .engine file.");
#endif

    for (std::list<std::pair<ImagePlaneDesc, ImagePtr> >::const_iterator it = args.outputPlanes.begin();
         it != args.outputPlanes.end(); ++it) {
        const ImagePtr& outputImg = it->second;
        if (!outputImg) {
            continue;
        }
        if (outputMode == eFluxCorridorKeyOutputAlpha) {
            outputImg->fillZero(args.roi);
        } else {
            copyOrConvertPlate(plateImg, outputImg, args.roi, getApp().get());
        }
    }

    return eStatusOK;
}

NATRON_NAMESPACE_EXIT
