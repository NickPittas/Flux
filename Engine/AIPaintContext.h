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

#ifndef Engine_AIPaintContext_h
#define Engine_AIPaintContext_h

#include "Global/Macros.h"

#include <map>
#include <string>
#include <vector>

#include <QPointF>
#include <QRectF>

NATRON_NAMESPACE_ENTER

enum class AIPaintPromptType
{
    Point = 0,
    Box,
    Brush,
    Lasso,
    Scribble,
    Text,
    MaskReference
};

enum class AIPaintPromptRole
{
    Include = 0,
    Exclude,
    Neutral
};

struct AIPaintPrompt
{
    AIPaintPrompt();

    int id;
    AIPaintPromptType type;
    AIPaintPromptRole role;
    bool enabled;
    bool selected;
    double time;
    std::string coordinateSpace;
    QPointF point;
    QRectF rect;
    double displaySize;
    std::string label;
    std::string backendTag;
    std::map<std::string, std::string> metadata;
};

class AIPaintContext
{
public:
    AIPaintContext();

    int addPoint(double time, const QPointF& point, double displaySize, AIPaintPromptRole role, const std::map<std::string, std::string>& metadata = std::map<std::string, std::string>());
    int addBox(double time, const QRectF& rect, double displaySize, AIPaintPromptRole role, const std::map<std::string, std::string>& metadata = std::map<std::string, std::string>());

    void clear();
    bool selectPrompt(int id);
    bool clearSelection();
    bool deletePrompt(int id);
    bool deleteSelectedPrompt();
    int selectedPromptId() const;
    std::vector<AIPaintPrompt> prompts() const;

    std::string serialize() const;
    bool deserialize(const std::string& serialized);

private:
    int appendPrompt(const AIPaintPrompt& prompt);

    std::vector<AIPaintPrompt> _prompts;
    int _nextId;
};

NATRON_NAMESPACE_EXIT

#endif // Engine_AIPaintContext_h
