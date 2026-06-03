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

#include "AIPaintContext.h"

#include <algorithm>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

NATRON_NAMESPACE_ENTER

namespace {

static QString
typeToString(AIPaintPromptType type)
{
    switch (type) {
    case AIPaintPromptType::Point: return QString::fromUtf8("point");
    case AIPaintPromptType::Box: return QString::fromUtf8("box");
    case AIPaintPromptType::Brush: return QString::fromUtf8("brush");
    case AIPaintPromptType::Lasso: return QString::fromUtf8("lasso");
    case AIPaintPromptType::Scribble: return QString::fromUtf8("scribble");
    case AIPaintPromptType::Text: return QString::fromUtf8("text");
    case AIPaintPromptType::MaskReference: return QString::fromUtf8("maskRef");
    }

    return QString::fromUtf8("point");
}

static AIPaintPromptType
stringToType(const QString& value)
{
    if (value == QString::fromUtf8("box")) return AIPaintPromptType::Box;
    if (value == QString::fromUtf8("brush")) return AIPaintPromptType::Brush;
    if (value == QString::fromUtf8("lasso")) return AIPaintPromptType::Lasso;
    if (value == QString::fromUtf8("scribble")) return AIPaintPromptType::Scribble;
    if (value == QString::fromUtf8("text")) return AIPaintPromptType::Text;
    if (value == QString::fromUtf8("maskRef")) return AIPaintPromptType::MaskReference;

    return AIPaintPromptType::Point;
}

static QString
roleToString(AIPaintPromptRole role)
{
    switch (role) {
    case AIPaintPromptRole::Include: return QString::fromUtf8("include");
    case AIPaintPromptRole::Exclude: return QString::fromUtf8("exclude");
    case AIPaintPromptRole::Neutral: return QString::fromUtf8("neutral");
    }

    return QString::fromUtf8("include");
}

static AIPaintPromptRole
stringToRole(const QString& value)
{
    if (value == QString::fromUtf8("exclude")) return AIPaintPromptRole::Exclude;
    if (value == QString::fromUtf8("neutral")) return AIPaintPromptRole::Neutral;

    return AIPaintPromptRole::Include;
}

} // namespace

AIPaintPrompt::AIPaintPrompt()
    : id(0)
    , type(AIPaintPromptType::Point)
    , role(AIPaintPromptRole::Include)
    , enabled(true)
    , selected(false)
    , time(0.)
    , coordinateSpace("canonical")
    , point(0., 0.)
    , rect(0., 0., 0., 0.)
    , displaySize(12.)
    , label()
    , backendTag()
    , metadata()
{
}

AIPaintContext::AIPaintContext()
    : _prompts()
    , _nextId(1)
{
}

int
AIPaintContext::appendPrompt(const AIPaintPrompt& prompt)
{
    AIPaintPrompt copy = prompt;
    if (copy.id <= 0) {
        copy.id = _nextId++;
    } else {
        _nextId = std::max(_nextId, copy.id + 1);
    }
    if (copy.selected) {
        for (AIPaintPrompt& existing : _prompts) {
            existing.selected = false;
        }
    }
    _prompts.push_back(copy);

    return copy.id;
}

int
AIPaintContext::addPoint(double time,
                         const QPointF& point,
                         double displaySize,
                         AIPaintPromptRole role,
                         const std::map<std::string, std::string>& metadata)
{
    AIPaintPrompt prompt;
    prompt.type = AIPaintPromptType::Point;
    prompt.role = role;
    prompt.time = time;
    prompt.point = point;
    prompt.displaySize = displaySize;
    prompt.selected = true;
    prompt.metadata = metadata;

    return appendPrompt(prompt);
}

int
AIPaintContext::addBox(double time,
                       const QRectF& rect,
                       double displaySize,
                       AIPaintPromptRole role,
                       const std::map<std::string, std::string>& metadata)
{
    AIPaintPrompt prompt;
    prompt.type = AIPaintPromptType::Box;
    prompt.role = role;
    prompt.time = time;
    prompt.rect = rect.normalized();
    prompt.displaySize = displaySize;
    prompt.selected = true;
    prompt.metadata = metadata;

    return appendPrompt(prompt);
}

void
AIPaintContext::clear()
{
    _prompts.clear();
    _nextId = 1;
}

bool
AIPaintContext::selectPrompt(int id)
{
    bool found = false;
    for (AIPaintPrompt& prompt : _prompts) {
        const bool selected = prompt.id == id;
        found = found || selected;
        prompt.selected = selected;
    }

    return found;
}

bool
AIPaintContext::clearSelection()
{
    bool changed = false;
    for (AIPaintPrompt& prompt : _prompts) {
        changed = changed || prompt.selected;
        prompt.selected = false;
    }

    return changed;
}

bool
AIPaintContext::deletePrompt(int id)
{
    std::vector<AIPaintPrompt>::iterator it = std::remove_if(_prompts.begin(), _prompts.end(), [id](const AIPaintPrompt& prompt) {
        return prompt.id == id;
    });
    if (it == _prompts.end()) {
        return false;
    }
    _prompts.erase(it, _prompts.end());

    return true;
}

bool
AIPaintContext::deleteSelectedPrompt()
{
    const int id = selectedPromptId();
    if (id <= 0) {
        return false;
    }

    return deletePrompt(id);
}

int
AIPaintContext::selectedPromptId() const
{
    for (const AIPaintPrompt& prompt : _prompts) {
        if (prompt.selected) {
            return prompt.id;
        }
    }

    return -1;
}

std::vector<AIPaintPrompt>
AIPaintContext::prompts() const
{
    return _prompts;
}

std::string
AIPaintContext::serialize() const
{
    QJsonObject root;
    root.insert(QString::fromUtf8("version"), 1);

    QJsonArray promptArray;
    for (const AIPaintPrompt& prompt : _prompts) {
        QJsonObject object;
        object.insert(QString::fromUtf8("id"), prompt.id);
        object.insert(QString::fromUtf8("type"), typeToString(prompt.type));
        object.insert(QString::fromUtf8("role"), roleToString(prompt.role));
        object.insert(QString::fromUtf8("enabled"), prompt.enabled);
        object.insert(QString::fromUtf8("selected"), prompt.selected);
        object.insert(QString::fromUtf8("time"), prompt.time);
        object.insert(QString::fromUtf8("coordinateSpace"), QString::fromUtf8(prompt.coordinateSpace.c_str()));
        object.insert(QString::fromUtf8("x"), prompt.point.x());
        object.insert(QString::fromUtf8("y"), prompt.point.y());
        object.insert(QString::fromUtf8("rectX"), prompt.rect.x());
        object.insert(QString::fromUtf8("rectY"), prompt.rect.y());
        object.insert(QString::fromUtf8("rectW"), prompt.rect.width());
        object.insert(QString::fromUtf8("rectH"), prompt.rect.height());
        object.insert(QString::fromUtf8("displaySize"), prompt.displaySize);
        object.insert(QString::fromUtf8("label"), QString::fromUtf8(prompt.label.c_str()));
        object.insert(QString::fromUtf8("backendTag"), QString::fromUtf8(prompt.backendTag.c_str()));

        QJsonObject metadata;
        for (std::map<std::string, std::string>::const_iterator it = prompt.metadata.begin(); it != prompt.metadata.end(); ++it) {
            metadata.insert(QString::fromUtf8(it->first.c_str()), QString::fromUtf8(it->second.c_str()));
        }
        object.insert(QString::fromUtf8("metadata"), metadata);
        promptArray.append(object);
    }
    root.insert(QString::fromUtf8("prompts"), promptArray);

    return QJsonDocument(root).toJson(QJsonDocument::Compact).toStdString();
}

bool
AIPaintContext::deserialize(const std::string& serialized)
{
    if (serialized.empty()) {
        clear();
        return true;
    }

    QJsonParseError error;
    QJsonDocument document = QJsonDocument::fromJson(QByteArray::fromStdString(serialized), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return false;
    }

    QJsonArray array = document.object().value(QString::fromUtf8("prompts")).toArray();
    _prompts.clear();
    _nextId = 1;

    for (const QJsonValue& value : array) {
        if (!value.isObject()) {
            continue;
        }
        QJsonObject object = value.toObject();
        AIPaintPrompt prompt;
        prompt.id = object.value(QString::fromUtf8("id")).toInt();
        prompt.type = stringToType(object.value(QString::fromUtf8("type")).toString());
        prompt.role = stringToRole(object.value(QString::fromUtf8("role")).toString());
        prompt.enabled = object.value(QString::fromUtf8("enabled")).toBool(true);
        prompt.selected = object.value(QString::fromUtf8("selected")).toBool(false) && selectedPromptId() < 0;
        prompt.time = object.value(QString::fromUtf8("time")).toDouble(0.);
        prompt.coordinateSpace = object.value(QString::fromUtf8("coordinateSpace")).toString(QString::fromUtf8("canonical")).toStdString();
        prompt.point = QPointF(object.value(QString::fromUtf8("x")).toDouble(), object.value(QString::fromUtf8("y")).toDouble());
        prompt.rect = QRectF(object.value(QString::fromUtf8("rectX")).toDouble(), object.value(QString::fromUtf8("rectY")).toDouble(),
                             object.value(QString::fromUtf8("rectW")).toDouble(), object.value(QString::fromUtf8("rectH")).toDouble()).normalized();
        prompt.displaySize = object.value(QString::fromUtf8("displaySize")).toDouble(12.);
        prompt.label = object.value(QString::fromUtf8("label")).toString().toStdString();
        prompt.backendTag = object.value(QString::fromUtf8("backendTag")).toString().toStdString();

        QJsonObject metadata = object.value(QString::fromUtf8("metadata")).toObject();
        for (QJsonObject::const_iterator it = metadata.begin(); it != metadata.end(); ++it) {
            prompt.metadata[it.key().toStdString()] = it.value().toString().toStdString();
        }
        appendPrompt(prompt);
    }

    return true;
}

NATRON_NAMESPACE_EXIT
