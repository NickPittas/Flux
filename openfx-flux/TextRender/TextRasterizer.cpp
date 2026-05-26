/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of Flux.
 *
 * Flux is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 * ***** END LICENSE BLOCK ***** */

#include "TextRasterizer.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <map>
#include <regex>
#include <sstream>

#include <fontconfig/fontconfig.h>
#include <hb-ft.h>
#include <hb.h>

#include <ft2build.h>
#include FT_FREETYPE_H

namespace FluxText
{
namespace
{

struct GlyphPlacement
{
    unsigned int glyphIndex = 0;
    double xOffset = 0.0;
    double yOffset = 0.0;
    double xAdvance = 0.0;
    unsigned int cluster = 0;
    bool isSpace = false;
    int charIndex = 0;
    int noSpaceIndex = -1;
    int wordIndex = -1;
    int lineIndex = 0;
};

struct ShapedLine
{
    std::vector<GlyphPlacement> glyphs;
    double baseline = 0.0;
    double advance = 0.0;
    std::string text;
};

struct AnimParam { double value = 0.0; std::vector<std::pair<double, double> > keys; };
struct TextAnimator {
    bool enabled = true; int basedOn = 0; int shape = 0; int anchor = 1;
    AnimParam start, end, offset, amount;
    AnimParam position[2], scale[2], rotation, opacity, fill[4], tracking;
};
struct BoundsD { double minX, minY, maxX, maxY; bool valid; BoundsD() : minX(0), minY(0), maxX(0), maxY(0), valid(false) {} };
struct DrawState { double x = 0, y = 0, sx = 1, sy = 1, rot = 0, opacity = 1, color[4] = {0.1,0.65,1,1}; };

double clamp01(double v) { return std::max(0.0, std::min(1.0, v)); }

double evalParam(const AnimParam& p, double time)
{
    if (p.keys.empty()) return p.value;
    if (time <= p.keys.front().first) return p.keys.front().second;
    for (size_t i = 1; i < p.keys.size(); ++i) {
        if (time <= p.keys[i].first) {
            const double t0 = p.keys[i - 1].first, t1 = p.keys[i].first;
            const double v0 = p.keys[i - 1].second, v1 = p.keys[i].second;
            const double a = (std::abs(t1 - t0) < 1e-6) ? 0.0 : (time - t0) / (t1 - t0);
            return v0 + (v1 - v0) * clamp01(a);
        }
    }
    return p.keys.back().second;
}

std::string objectForKey(const std::string& s, const std::string& key)
{
    size_t k = s.find("\"" + key + "\"");
    if (k == std::string::npos) return std::string();
    size_t b = s.find('{', k);
    if (b == std::string::npos) return std::string();
    int depth = 0;
    for (size_t i = b; i < s.size(); ++i) {
        if (s[i] == '{') ++depth;
        if (s[i] == '}' && --depth == 0) return s.substr(b, i - b + 1);
    }
    return std::string();
}

std::vector<std::string> objectsInArrayForKey(const std::string& s, const std::string& key)
{
    std::vector<std::string> out;
    size_t k = s.find("\"" + key + "\"");
    if (k == std::string::npos) return out;
    size_t b = s.find('[', k);
    if (b == std::string::npos) return out;
    size_t e = std::string::npos;
    int arrayDepth = 0;
    for (size_t n = b; n < s.size(); ++n) {
        if (s[n] == '[') ++arrayDepth;
        if (s[n] == ']' && --arrayDepth == 0) { e = n; break; }
    }
    if (e == std::string::npos) return out;
    for (size_t i = b; i < e; ++i) {
        if (s[i] != '{') continue;
        int depth = 0;
        for (size_t j = i; j < e; ++j) {
            if (s[j] == '{') ++depth;
            if (s[j] == '}' && --depth == 0) { out.push_back(s.substr(i, j - i + 1)); i = j; break; }
        }
    }
    return out;
}

double numberForKey(const std::string& s, const std::string& key, double fallback)
{
    std::regex re("\\\"" + key + "\\\"\\s*:\\s*(-?[0-9]+(?:\\.[0-9]+)?)");
    std::smatch m;
    return std::regex_search(s, m, re) ? std::stod(m[1].str()) : fallback;
}

bool boolForKey(const std::string& s, const std::string& key, bool fallback)
{
    size_t k = s.find("\"" + key + "\"");
    if (k == std::string::npos) return fallback;
    size_t t = s.find("true", k), f = s.find("false", k);
    return t != std::string::npos && (f == std::string::npos || t < f);
}

AnimParam paramForKey(const std::string& s, const std::string& key, double fallback)
{
    AnimParam p; p.value = fallback;
    std::string obj = key.empty() ? s : objectForKey(s, key);
    if (obj.empty()) return p;
    p.value = numberForKey(obj, "v", fallback);
    std::regex re("\\{\\\"t\\\":(-?[0-9]+(?:\\.[0-9]+)?),\\\"v\\\":(-?[0-9]+(?:\\.[0-9]+)?)\\}");
    for (std::sregex_iterator it(obj.begin(), obj.end(), re), end; it != end; ++it) {
        p.keys.push_back(std::make_pair(std::stod((*it)[1].str()), std::stod((*it)[2].str())));
    }
    std::sort(p.keys.begin(), p.keys.end());
    return p;
}

std::vector<TextAnimator> parseAnimators(const std::string& json)
{
    std::vector<TextAnimator> result;
    for (const std::string& obj : objectsInArrayForKey(json, "animators")) {
        TextAnimator a;
        a.scale[0].value = 100.0;
        a.scale[1].value = 100.0;
        a.enabled = boolForKey(obj, "enabled", true);
        a.basedOn = static_cast<int>(numberForKey(obj, "basedOn", 0));
        a.shape = static_cast<int>(numberForKey(obj, "shape", 1));
        a.anchor = std::max(0, std::min(2, static_cast<int>(numberForKey(obj, "anchor", 1))));
        std::string sel = objectForKey(obj, "selector");
        a.start = paramForKey(sel, "start", 0.0); a.end = paramForKey(sel, "end", 100.0);
        a.offset = paramForKey(sel, "offset", 0.0); a.amount = paramForKey(sel, "amount", 100.0);
        std::string targets = objectForKey(obj, "targets");
        std::vector<std::string> pos = objectsInArrayForKey(targets, "position");
        std::vector<std::string> sca = objectsInArrayForKey(targets, "scale");
        std::vector<std::string> fill = objectsInArrayForKey(targets, "fillColor");
        for (int i = 0; i < 2; ++i) { if (i < (int)pos.size()) a.position[i] = paramForKey(pos[i], "", 0.0); if (i < (int)sca.size()) a.scale[i] = paramForKey(sca[i], "", 100.0); }
        a.rotation = paramForKey(targets, "rotation", 0.0); a.opacity = paramForKey(targets, "opacity", 100.0); a.tracking = paramForKey(targets, "tracking", 0.0);
        for (int i = 0; i < 4; ++i) a.fill[i] = (i < (int)fill.size()) ? paramForKey(fill[i], "", i == 3 ? 1.0 : 0.0) : AnimParam();
        result.push_back(a);
    }
    return result;
}

std::string lowerAscii(const std::string& value)
{
    std::string out = value;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

std::vector<std::string> splitLines(const std::string& text)
{
    std::vector<std::string> lines;
    std::stringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(line);
    }
    if (lines.empty()) {
        lines.push_back(std::string());
    }
    return lines;
}

std::string resolveFontFile(const std::string& family, const std::string& style, std::string* error)
{
    if (!FcInit()) {
        if (error) {
            *error = "Fontconfig initialization failed";
        }
        return std::string();
    }

    FcPattern* pattern = FcPatternCreate();
    if (!pattern) {
        if (error) {
            *error = "Fontconfig pattern creation failed";
        }
        return std::string();
    }

    const std::string requested = family.empty() ? "Sans" : family;
    FcPatternAddString(pattern, FC_FAMILY, reinterpret_cast<const FcChar8*>(requested.c_str()));
    if (!style.empty()) {
        FcPatternAddString(pattern, FC_STYLE, reinterpret_cast<const FcChar8*>(style.c_str()));
    }
    const std::string lowerStyle = lowerAscii(style);
    FcPatternAddInteger(pattern, FC_SLANT, lowerStyle.find("italic") != std::string::npos || lowerStyle.find("oblique") != std::string::npos ? FC_SLANT_ITALIC : FC_SLANT_ROMAN);
    FcPatternAddInteger(pattern, FC_WEIGHT, lowerStyle.find("bold") != std::string::npos ? FC_WEIGHT_BOLD : FC_WEIGHT_REGULAR);
    FcDefaultSubstitute(pattern);
    FcConfigSubstitute(NULL, pattern, FcMatchPattern);

    FcResult result = FcResultNoMatch;
    FcPattern* matched = FcFontMatch(NULL, pattern, &result);
    FcPatternDestroy(pattern);
    if (!matched) {
        if (error) {
            *error = "No Fontconfig match for font family: " + requested;
        }
        return std::string();
    }

    FcChar8* file = NULL;
    std::string path;
    if (FcPatternGetString(matched, FC_FILE, 0, &file) == FcResultMatch && file) {
        path = reinterpret_cast<const char*>(file);
    }
    FcPatternDestroy(matched);

    if (path.empty() && error) {
        *error = "Fontconfig match did not contain a font file for: " + requested;
    }
    return path;
}

std::vector<GlyphPlacement> shapeLine(hb_font_t* font, const std::string& line, double tracking, int lineIndex, int* charCounter, int* noSpaceCounter, double* advance)
{
    std::vector<GlyphPlacement> glyphs;
    hb_buffer_t* buffer = hb_buffer_create();
    hb_buffer_add_utf8(buffer, line.c_str(), static_cast<int>(line.size()), 0, static_cast<int>(line.size()));
    hb_buffer_guess_segment_properties(buffer);
    hb_shape(font, buffer, NULL, 0);

    unsigned int count = 0;
    hb_glyph_info_t* infos = hb_buffer_get_glyph_infos(buffer, &count);
    hb_glyph_position_t* positions = hb_buffer_get_glyph_positions(buffer, &count);
    glyphs.reserve(count);
    double totalAdvance = 0.0;
    for (unsigned int i = 0; i < count; ++i) {
        GlyphPlacement glyph;
        glyph.glyphIndex = infos[i].codepoint;
        glyph.cluster = infos[i].cluster;
        glyph.lineIndex = lineIndex;
        glyph.charIndex = (*charCounter)++;
        glyph.isSpace = glyph.cluster < line.size() && std::isspace(static_cast<unsigned char>(line[glyph.cluster]));
        if (!glyph.isSpace) {
            glyph.noSpaceIndex = (*noSpaceCounter)++;
        }
        glyph.xOffset = positions[i].x_offset / 64.0;
        glyph.yOffset = positions[i].y_offset / 64.0;
        glyph.xAdvance = positions[i].x_advance / 64.0;
        if (i + 1 < count) {
            glyph.xAdvance += tracking;
        }
        totalAdvance += glyph.xAdvance;
        glyphs.push_back(glyph);
    }

    if (advance) {
        *advance = totalAdvance;
    }

    hb_buffer_destroy(buffer);
    return glyphs;
}

void blendRGBA(Raster& raster, int x, int y, float coverage, const double color[4], double opacity)
{
    if (coverage <= 0.0f || x < raster.bounds.x1 || x >= raster.bounds.x2 || y < raster.bounds.y1 || y >= raster.bounds.y2) return;
    const int ix = x - raster.bounds.x1, iy = y - raster.bounds.y1;
    const size_t p = (static_cast<size_t>(iy) * static_cast<size_t>(raster.width) + static_cast<size_t>(ix)) * 4u;
    const double a = clamp01(coverage * color[3] * opacity);
    raster.rgba[p + 0] = static_cast<float>(color[0] * a + raster.rgba[p + 0] * (1.0 - a));
    raster.rgba[p + 1] = static_cast<float>(color[1] * a + raster.rgba[p + 1] * (1.0 - a));
    raster.rgba[p + 2] = static_cast<float>(color[2] * a + raster.rgba[p + 2] * (1.0 - a));
    raster.rgba[p + 3] = static_cast<float>(a + raster.rgba[p + 3] * (1.0 - a));
    float& alpha = raster.alpha[static_cast<size_t>(iy) * static_cast<size_t>(raster.width) + static_cast<size_t>(ix)];
    alpha = std::max(alpha, raster.rgba[p + 3]);
}

void grow(BoundsD& b, double x1, double y1, double x2, double y2)
{
    if (!b.valid) { b.minX = x1; b.maxX = x2; b.minY = y1; b.maxY = y2; b.valid = true; return; }
    b.minX = std::min(b.minX, x1); b.minY = std::min(b.minY, y1); b.maxX = std::max(b.maxX, x2); b.maxY = std::max(b.maxY, y2);
}

double selectorWeight(const TextAnimator& a, int index, int count, double time)
{
    if (!a.enabled || count <= 0 || index < 0) return 0.0;
    const double start = evalParam(a.start, time);
    const double end = evalParam(a.end, time);
    const double amount = clamp01(evalParam(a.amount, time) / 100.0);
    const double offset = std::max(-100.0, std::min(100.0, evalParam(a.offset, time)));

    const double elementPercent = 100.0 * (static_cast<double>(index) + 0.5) / static_cast<double>(count);
    const double lo = std::max(0.0, std::min(start, end));
    const double hi = std::min(100.0, std::max(start, end));
    if (elementPercent < lo || elementPercent > hi || std::abs(hi - lo) < 1e-6) return 0.0;

    const double u = clamp01((elementPercent - lo) / (hi - lo));
    const double rank = offset >= 0.0 ? u : (1.0 - u);
    const double progress = std::abs(offset) / 100.0;
    double w = 0.0;
    if (a.shape == 0) {
        // Square: the selector is a hard on/off switch. Offset 0 leaves the
        // selected range fully in the animator state. Offset +100 returns it
        // to the original state from start to end; -100 does the same from end
        // to start.
        if (progress <= 0.0) {
            w = 1.0;
        } else if (progress >= 1.0) {
            w = 0.0;
        } else {
            w = rank > progress ? 1.0 : 0.0;
        }
    } else if (a.shape == 1) {
        // Linear: same endpoints/direction as Square, but interpolate through
        // the crossing instead of snapping in one frame/one element.
        if (progress <= 0.0) {
            w = 1.0;
        } else if (progress >= 1.0) {
            w = 0.0;
        } else {
            const double transition = std::max(1.0 / static_cast<double>(count), 1e-6);
            const double originalWeight = clamp01((progress - rank) / transition + 0.5);
            w = 1.0 - originalWeight;
        }
    } else if (a.shape == 2) {
        // Ramp Up: per-element gradient across the selected range.
        //   -100 => all elements at animator/new state
        //      0 => rising ramp from original at start to animator at end
        //    100 => all elements back at original state
        w = clamp01(u - (offset / 100.0));
    } else if (a.shape == 3) {
        // Ramp Down: inverse per-element gradient across the selected range.
        //   -100 => all elements at original state
        //      0 => falling ramp from animator at start to original at end
        //    100 => all elements at animator/new state
        w = clamp01((1.0 - u) + (offset / 100.0));
    }
    return clamp01(w * amount);
}

float monoCoverage(const unsigned char* row, int x)
{
    return (row[x / 8] & (0x80 >> (x % 8))) ? 1.0f : 0.0f;
}

} // namespace

float Raster::alphaAt(int x, int y) const
{
    if (x < bounds.x1 || x >= bounds.x2 || y < bounds.y1 || y >= bounds.y2) {
        return 0.0f;
    }
    const int ix = x - bounds.x1;
    const int iy = y - bounds.y1;
    return alpha[static_cast<size_t>(iy) * static_cast<size_t>(width) + static_cast<size_t>(ix)];
}

void Raster::colorAt(int x, int y, double* r, double* g, double* b, double* a) const
{
    if (x < bounds.x1 || x >= bounds.x2 || y < bounds.y1 || y >= bounds.y2 || rgba.empty()) {
        if (r) *r = 0.0; if (g) *g = 0.0; if (b) *b = 0.0; if (a) *a = 0.0;
        return;
    }
    const int ix = x - bounds.x1;
    const int iy = y - bounds.y1;
    const size_t p = (static_cast<size_t>(iy) * static_cast<size_t>(width) + static_cast<size_t>(ix)) * 4u;
    const double alpha = rgba[p + 3];
    if (a) *a = alpha;
    if (alpha > 1e-8) {
        if (r) *r = rgba[p + 0] / alpha;
        if (g) *g = rgba[p + 1] / alpha;
        if (b) *b = rgba[p + 2] / alpha;
    } else {
        if (r) *r = 0.0; if (g) *g = 0.0; if (b) *b = 0.0;
    }
}

std::shared_ptr<Raster> renderText(const RenderRequest& request, std::string* error)
{
    const OfxRectI outputBounds = (request.outputBounds.x2 > request.outputBounds.x1 && request.outputBounds.y2 > request.outputBounds.y1)
        ? request.outputBounds
        : request.bounds;
    const OfxRectI layoutBounds = (request.layoutBounds.x2 > request.layoutBounds.x1 && request.layoutBounds.y2 > request.layoutBounds.y1)
        ? request.layoutBounds
        : outputBounds;

    std::shared_ptr<Raster> raster(new Raster());
    raster->bounds = outputBounds;
    raster->width = std::max(1, outputBounds.x2 - outputBounds.x1);
    raster->height = std::max(1, outputBounds.y2 - outputBounds.y1);
    raster->alpha.assign(static_cast<size_t>(raster->width) * static_cast<size_t>(raster->height), 0.0f);
    raster->rgba.assign(static_cast<size_t>(raster->width) * static_cast<size_t>(raster->height) * 4u, 0.0f);

    if (request.text.empty()) {
        return raster;
    }

    const double scaleX = request.renderScaleX > 0.0 ? request.renderScaleX : 1.0;
    const double scaleY = request.renderScaleY > 0.0 ? request.renderScaleY : 1.0;
    const double fontSizeX = std::max(1.0, request.fontSize * scaleX);
    const double fontSizeY = std::max(1.0, request.fontSize * scaleY);
    const double tracking = request.tracking * scaleX;
    const double leading = request.leading * scaleY;
    const std::string fontFile = resolveFontFile(request.font, request.fontStyle, error);
    if (fontFile.empty()) {
        return raster;
    }

    FT_Library library = NULL;
    if (FT_Init_FreeType(&library) != 0) {
        if (error) {
            *error = "FreeType initialization failed";
        }
        return raster;
    }

    FT_Face face = NULL;
    if (FT_New_Face(library, fontFile.c_str(), 0, &face) != 0) {
        if (error) {
            *error = "FreeType could not load font file: " + fontFile;
        }
        FT_Done_FreeType(library);
        return raster;
    }

    FT_Set_Pixel_Sizes(face,
                       static_cast<FT_UInt>(std::max(1.0, std::round(fontSizeX))),
                       static_cast<FT_UInt>(std::max(1.0, std::round(fontSizeY))));

    hb_font_t* hbFont = hb_ft_font_create_referenced(face);
    const double defaultLineHeight = std::max(fontSizeY * 1.2, face->size ? face->size->metrics.height / 64.0 : fontSizeY * 1.2);
    const double lineHeight = leading > 0.0 ? leading : defaultLineHeight;
    const std::vector<std::string> lines = splitLines(request.text);
    std::vector<ShapedLine> shapedLines;
    shapedLines.reserve(lines.size());
    int charCounter = 0;
    int noSpaceCounter = 0;
    for (size_t i = 0; i < lines.size(); ++i) {
        ShapedLine shaped;
        shaped.text = lines[i];
        shaped.baseline = -static_cast<double>(i) * lineHeight;
        shaped.glyphs = shapeLine(hbFont, lines[i], tracking, static_cast<int>(i), &charCounter, &noSpaceCounter, &shaped.advance);
        int word = -1;
        bool inWord = false;
        for (GlyphPlacement& glyph : shaped.glyphs) {
            if (glyph.isSpace) {
                inWord = false;
            } else {
                if (!inWord) { ++word; inWord = true; }
                glyph.wordIndex = word;
            }
        }
        shapedLines.push_back(shaped);
    }

    double minX = std::numeric_limits<double>::max();
    double minY = std::numeric_limits<double>::max();
    double maxX = -std::numeric_limits<double>::max();
    double maxY = -std::numeric_limits<double>::max();
    bool hasPixels = false;

    for (const ShapedLine& line : shapedLines) {
        double penX = 0.0;
        for (const GlyphPlacement& glyph : line.glyphs) {
            if (FT_Load_Glyph(face, glyph.glyphIndex, FT_LOAD_DEFAULT) == 0 && FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL) == 0) {
                const FT_GlyphSlot slot = face->glyph;
                const double left = penX + glyph.xOffset + slot->bitmap_left;
                const double top = line.baseline + glyph.yOffset + slot->bitmap_top;
                const double right = left + slot->bitmap.width;
                const double bottom = top - slot->bitmap.rows;
                if (slot->bitmap.width > 0 && slot->bitmap.rows > 0) {
                    minX = std::min(minX, left);
                    minY = std::min(minY, bottom);
                    maxX = std::max(maxX, right);
                    maxY = std::max(maxY, top);
                    hasPixels = true;
                }
            }
            penX += glyph.xAdvance;
        }
    }

    if (!hasPixels) {
        hb_font_destroy(hbFont);
        FT_Done_Face(face);
        FT_Done_FreeType(library);
        return raster;
    }

    // The first line's advance is the reference width for the text block.
    // All subsequent lines align left/center/right relative to this first line,
    // not to the canvas/project format. Changing alignment reflows subsequent
    // lines within the block but does not move the block itself.
    const double refAdvance = std::max(1.0, shapedLines[0].advance);

    // The text block is always centered within the canvas/project format.
    // Alignment (left/center/right) only controls how subsequent lines
    // reflow within that block — it does NOT shift the entire block to
    // the canvas edge. The node transform handles overall placement.
    const double textHeight = std::max(1.0, maxY - minY);
    const double layoutWidth = std::max(1, layoutBounds.x2 - layoutBounds.x1);
    const double layoutHeight = std::max(1, layoutBounds.y2 - layoutBounds.y1);
    const double blockOffsetX = layoutBounds.x1 + (layoutWidth - refAdvance) * 0.5 - minX;
    const double offsetY = layoutBounds.y1 + (layoutHeight - textHeight) * 0.5 - minY;

    std::vector<TextAnimator> animators = parseAnimators(request.animatorStackJson);
    std::map<int, BoundsD> charBounds, noSpaceBounds, wordBounds, lineBounds;
    struct GlyphDraw { GlyphPlacement glyph; double x, y, w, h, originX, topY; };
    std::vector<GlyphDraw> draws;

    for (const ShapedLine& line : shapedLines) {
        double penX = 0.0;
        // Per-line alignment within the text block width (refAdvance = first line).
        // Subsequent lines shift relative to the first line's advance.
        double lineAlignOffset = 0.0;
        if (request.alignment == 1) {
            lineAlignOffset = (refAdvance - line.advance) * 0.5;
        } else if (request.alignment == 2) {
            lineAlignOffset = refAdvance - line.advance;
        }
        for (const GlyphPlacement& glyph : line.glyphs) {
            if (FT_Load_Glyph(face, glyph.glyphIndex, FT_LOAD_DEFAULT) != 0 || FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL) != 0) {
                penX += glyph.xAdvance;
                continue;
            }

            const FT_GlyphSlot slot = face->glyph;
            const FT_Bitmap& bitmap = slot->bitmap;
            const double originXd = blockOffsetX + lineAlignOffset + penX + glyph.xOffset + slot->bitmap_left;
            const double topYd = offsetY + line.baseline + glyph.yOffset + slot->bitmap_top;
            const int originX = static_cast<int>(std::floor(originXd));
            const int topY = static_cast<int>(std::floor(topYd));
            GlyphDraw gd;
            gd.glyph = glyph; gd.originX = originXd; gd.topY = topYd; gd.x = originXd; gd.y = topYd - bitmap.rows; gd.w = bitmap.width; gd.h = bitmap.rows;
            draws.push_back(gd);
            grow(charBounds[glyph.charIndex], gd.x, gd.y, gd.x + gd.w, gd.y + gd.h);
            if (glyph.noSpaceIndex >= 0) grow(noSpaceBounds[glyph.noSpaceIndex], gd.x, gd.y, gd.x + gd.w, gd.y + gd.h);
            if (glyph.wordIndex >= 0) grow(wordBounds[glyph.lineIndex * 100000 + glyph.wordIndex], gd.x, gd.y, gd.x + gd.w, gd.y + gd.h);
            grow(lineBounds[glyph.lineIndex], gd.x, gd.y, gd.x + gd.w, gd.y + gd.h);
            penX += glyph.xAdvance;
        }
    }

    const int totalChars = charCounter;
    const int totalNoSpaces = std::max(1, noSpaceCounter);
    const int totalLines = static_cast<int>(lines.size());

    for (const GlyphDraw& gd : draws) {
        if (FT_Load_Glyph(face, gd.glyph.glyphIndex, FT_LOAD_DEFAULT) != 0 || FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL) != 0) {
            continue;
        }
        const FT_GlyphSlot slot = face->glyph;
        const FT_Bitmap& bitmap = slot->bitmap;
        const int pitch = std::abs(bitmap.pitch);
        DrawState state;
        for (int c = 0; c < 4; ++c) state.color[c] = request.fillColor[c];
        BoundsD pivotBounds = charBounds[gd.glyph.charIndex];
        int anchor = 1;
        for (const TextAnimator& a : animators) {
            int idx = gd.glyph.charIndex, count = totalChars;
            if (a.basedOn == 1) { idx = gd.glyph.noSpaceIndex; count = totalNoSpaces; if (idx < 0) continue; pivotBounds = noSpaceBounds[idx]; }
            else if (a.basedOn == 2) { idx = gd.glyph.wordIndex; count = std::max(1, (int)wordBounds.size()); if (idx < 0) continue; pivotBounds = wordBounds[gd.glyph.lineIndex * 100000 + idx]; }
            else if (a.basedOn == 3) { idx = gd.glyph.lineIndex; count = totalLines; pivotBounds = lineBounds[idx]; }
            else { pivotBounds = charBounds[idx]; }
            const double w = selectorWeight(a, idx, count, request.time);
            if (w <= 0.0) continue;
            anchor = a.anchor;
            state.x += evalParam(a.position[0], request.time) * scaleX * w + evalParam(a.tracking, request.time) * scaleX * w * idx;
            state.y += evalParam(a.position[1], request.time) * scaleY * w;
            const double targetScaleX = std::max(0.0, evalParam(a.scale[0], request.time) / 100.0);
            const double targetScaleY = std::max(0.0, evalParam(a.scale[1], request.time) / 100.0);
            state.sx *= std::max(0.0, 1.0 + (targetScaleX - 1.0) * w);
            state.sy *= std::max(0.0, 1.0 + (targetScaleY - 1.0) * w);
            state.rot += evalParam(a.rotation, request.time) * w * 3.14159265358979323846 / 180.0;
            state.opacity *= 1.0 + ((evalParam(a.opacity, request.time) / 100.0) - 1.0) * w;
            for (int c = 0; c < 4; ++c) state.color[c] = state.color[c] + (evalParam(a.fill[c], request.time) - state.color[c]) * w;
        }
        double cx = gd.x + gd.w * 0.5;
        double cy = gd.y + gd.h * 0.5;
        if (pivotBounds.valid) {
            if (anchor == 0) {
                cx = pivotBounds.minX;
                cy = pivotBounds.minY;
            } else if (anchor == 2) {
                cx = pivotBounds.maxX;
                cy = pivotBounds.minY;
            } else {
                cx = (pivotBounds.minX + pivotBounds.maxX) * 0.5;
                cy = (pivotBounds.minY + pivotBounds.maxY) * 0.5;
            }
        }
        const double cs = std::cos(state.rot), sn = std::sin(state.rot);
        for (unsigned int row = 0; row < bitmap.rows; ++row) {
            const unsigned char* rowPtr = bitmap.buffer + (bitmap.pitch >= 0 ? row : (bitmap.rows - 1 - row)) * pitch;
            for (unsigned int col = 0; col < bitmap.width; ++col) {
                float coverage = 0.0f;
                if (bitmap.pixel_mode == FT_PIXEL_MODE_GRAY) coverage = rowPtr[col] / 255.0f;
                else if (bitmap.pixel_mode == FT_PIXEL_MODE_MONO) coverage = monoCoverage(rowPtr, static_cast<int>(col));
                if (coverage <= 0.0f) continue;
                const double px = gd.originX + col;
                const double py = gd.topY - row;
                double dx = (px - cx) * state.sx;
                double dy = (py - cy) * state.sy;
                int tx = static_cast<int>(std::floor(cx + dx * cs - dy * sn + state.x));
                int ty = static_cast<int>(std::floor(cy + dx * sn + dy * cs + state.y));
                blendRGBA(*raster, tx, ty, coverage, state.color, state.opacity);
            }
        }
    }

    hb_font_destroy(hbFont);
    FT_Done_Face(face);
    FT_Done_FreeType(library);
    return raster;
}

} // namespace FluxText
