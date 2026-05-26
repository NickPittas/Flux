/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of Flux.
 *
 * Flux is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 * ***** END LICENSE BLOCK ***** */

#ifndef FLUX_TEXT_RASTERIZER_H
#define FLUX_TEXT_RASTERIZER_H

#include <memory>
#include <string>
#include <vector>

#include "ofxCore.h"

namespace FluxText
{

struct RenderRequest
{
    std::string text;
    std::string font;
    std::string fontStyle;
    double fontSize = 96.0;
    double tracking = 0.0;
    double leading = 0.0;
    int alignment = 1; // 0=left, 1=center, 2=right
    double fillColor[4] = {0.1, 0.65, 1.0, 1.0};
    std::string animatorStackJson;
    double time = 0.0;
    double renderScaleX = 1.0;
    double renderScaleY = 1.0;
    OfxRectI bounds = {0, 0, 1, 1};
    OfxRectI layoutBounds = {0, 0, 1, 1};
    OfxRectI outputBounds = {0, 0, 1, 1};
};

struct Raster
{
    OfxRectI bounds = {0, 0, 1, 1};
    int width = 1;
    int height = 1;
    std::vector<float> alpha;
    std::vector<float> rgba;

    float alphaAt(int x, int y) const;
    void colorAt(int x, int y, double* r, double* g, double* b, double* a) const;
};

std::shared_ptr<Raster> renderText(const RenderRequest& request, std::string* error);

} // namespace FluxText

#endif // FLUX_TEXT_RASTERIZER_H
