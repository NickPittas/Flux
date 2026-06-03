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

#include "AIPaint.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>

#include <QString>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

#include <cairo/cairo.h>

#include "Global/GLIncludes.h"

#include "Engine/AIPaintContext.h"
#include "Engine/AppManager.h"
#include "Engine/AppInstance.h"
#include "Engine/Format.h"
#include "Engine/Image.h"
#include "Engine/ImagePlaneDesc.h"
#include "Engine/KnobTypes.h"
#include "Engine/Node.h"
#include "Engine/Project.h"
#include "Engine/Transform.h"

NATRON_NAMESPACE_ENTER

namespace {

// Build per-prompt frame ownership metadata from the overlay creation time.
// Note: sourceFrame is NOT derived here because AIPaint.cpp lacks source layer context.
std::map<std::string, std::string>
buildPromptFrameMetadata(double time)
{
    std::map<std::string, std::string> md;
    md["timelineFrame"] = std::to_string(std::lround(time));
    md["frameOwnership"] = "timelineFrame";
    {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.17g", time);
        md["timelineTime"] = std::string(buf);
    }
    return md;
}

bool
promptOwnsTimelineFrame(const AIPaintPrompt& prompt, double time)
{
    if (!std::isfinite(time)) {
        return true;
    }

    const int currentFrame = static_cast<int>(std::lround(time));
    std::map<std::string, std::string>::const_iterator it = prompt.metadata.find("timelineFrame");
    if (it != prompt.metadata.end()) {
        try {
            return std::stoi(it->second) == currentFrame;
        } catch (...) {
            // Fall through to legacy prompt.time below.
        }
    }

    if (!std::isfinite(prompt.time)) {
        return true;
    }
    return static_cast<int>(std::lround(prompt.time)) == currentFrame;
}

} // anonymous namespace

namespace {

QString
aiPaintLogSessionId()
{
    static const QString id = QDateTime::currentDateTimeUtc().toString(QString::fromUtf8("yyyyMMdd-HHmmss-zzz"));
    return id;
}

QString
aiPaintProjectPath(const EffectInstance* effect)
{
    if (!effect) {
        return QString();
    }
    NodePtr node = effect->getNode();
    if (!node) {
        return QString();
    }
    AppInstancePtr app = node->getApp();
    if (!app || !app->getProject() || !app->getProject()->hasProjectBeenSavedByUser()) {
        return QString();
    }
    return app->getProject()->getProjectPath();
}

QString
aiPaintLogDir(const QString& projectPath)
{
    if (!projectPath.trimmed().isEmpty()) {
        return QDir(projectPath).filePath(QString::fromUtf8("FluxGenerated/AI/logs"));
    }
    return QDir::home().filePath(QString::fromUtf8(".local/state/Flux/ai-logs"));
}

QString
aiPaintLatestLogPath(const QString& projectPath)
{
    return QDir(aiPaintLogDir(projectPath)).filePath(QString::fromUtf8("flux-ai-latest.jsonl"));
}

QString
aiPaintSessionLogPath(const QString& projectPath)
{
    return QDir(aiPaintLogDir(projectPath)).filePath(QString::fromUtf8("flux-ai-%1.jsonl").arg(aiPaintLogSessionId()));
}

void
aiPaintAppendJsonLine(const QString& path, const QByteArray& line)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        std::fprintf(stderr, "[FLUX-AI] log_write_failed path='%s' error='%s'\n",
                     path.toUtf8().constData(), file.errorString().toUtf8().constData());
        return;
    }
    file.write(line);
    file.write("\n");
    file.flush();
}

void
aiPaintLogEvent(const EffectInstance* effect,
                const QString& step,
                const QString& status,
                const QString& message,
                const QJsonObject& found = QJsonObject(),
                const QString& nextAction = QString())
{
    const QString projectPath = aiPaintProjectPath(effect);
    const QString latest = aiPaintLatestLogPath(projectPath);
    const QString session = aiPaintSessionLogPath(projectPath);
    QJsonObject event;
    event.insert(QString::fromUtf8("time_utc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    event.insert(QString::fromUtf8("session_id"), aiPaintLogSessionId());
    event.insert(QString::fromUtf8("level"), status == QString::fromUtf8("block") ? QString::fromUtf8("warn") : QString::fromUtf8("info"));
    event.insert(QString::fromUtf8("area"), QString::fromUtf8("aipaint"));
    event.insert(QString::fromUtf8("step"), step);
    event.insert(QString::fromUtf8("status"), status);
    event.insert(QString::fromUtf8("message"), message);
    event.insert(QString::fromUtf8("expected"), QString::fromUtf8("AI Paint overlay tools write current-frame Include prompts into aiPaintPromptStore."));
    event.insert(QString::fromUtf8("project_path"), projectPath);
    event.insert(QString::fromUtf8("log_path_latest"), latest);
    event.insert(QString::fromUtf8("log_path_session"), session);
    if (!found.isEmpty()) {
        event.insert(QString::fromUtf8("found"), found);
    }
    if (!nextAction.trimmed().isEmpty()) {
        event.insert(QString::fromUtf8("next_action"), nextAction);
    }
    const QByteArray line = QJsonDocument(event).toJson(QJsonDocument::Compact);
    aiPaintAppendJsonLine(latest, line);
    if (session != latest) {
        aiPaintAppendJsonLine(session, line);
    }
    std::fprintf(stderr, "[FLUX-AI] info area=aipaint step=%s status=%s message=%s log=%s\n",
                 step.toUtf8().constData(), status.toUtf8().constData(),
                 message.toUtf8().constData(), latest.toUtf8().constData());
}

int
currentFramePromptCount(const std::vector<AIPaintPrompt>& prompts, double time)
{
    int count = 0;
    for (const AIPaintPrompt& prompt : prompts) {
        if (prompt.enabled && promptOwnsTimelineFrame(prompt, time)) {
            ++count;
        }
    }
    return count;
}

#define kAIPaintParamPage "aiPaint"
#define kAIPaintParamToolbar "aiPaintToolbar"
#define kAIPaintParamSelectToolGroup "aiPaintSelectToolGroup"
#define kAIPaintParamPointToolGroup "aiPaintPointToolGroup"
#define kAIPaintParamBoxToolGroup "aiPaintBoxToolGroup"
#define kAIPaintParamSelectTool "aiPaintSelectTool"
#define kAIPaintParamPointTool "aiPaintPointTool"
#define kAIPaintParamBoxTool "aiPaintBoxTool"
#define kAIPaintParamDeleteSelected "aiPaintDeleteSelected"
#define kAIPaintParamClearAll "aiPaintClearAll"
#define kAIPaintParamLoadSam3 "aiPaintLoadSam3"
#define kAIPaintParamUnloadSam3 "aiPaintUnloadSam3"
#define kAIPaintParamLivePreview "aiPaintLivePreview"
#define kAIPaintParamSam3Status "aiPaintSam3Status"
#define kAIPaintParamPromptStore "aiPaintPromptStore"

static const double kDefaultPromptDisplaySize = 12.;

enum class AIPaintTool
{
    Select,
    Point,
    Box
};

static void
promptColor(AIPaintPromptRole role, float* r, float* g, float* b)
{
    switch (role) {
    case AIPaintPromptRole::Include:
        *r = 0.15f; *g = 0.9f; *b = 0.25f;
        break;
    case AIPaintPromptRole::Exclude:
        *r = 0.95f; *g = 0.18f; *b = 0.12f;
        break;
    case AIPaintPromptRole::Neutral:
        *r = 0.2f; *g = 0.55f; *b = 1.f;
        break;
    }
}

static bool
pointOnBox(const QRectF& rect, const QPointF& pos, double tolerance)
{
    QRectF expanded = rect.normalized().adjusted(-tolerance, -tolerance, tolerance, tolerance);
    if (!expanded.contains(pos)) {
        return false;
    }

    const QRectF normalized = rect.normalized();
    const bool withinHorizontalSpan = pos.x() >= normalized.left() - tolerance && pos.x() <= normalized.right() + tolerance;
    const bool withinVerticalSpan = pos.y() >= normalized.top() - tolerance && pos.y() <= normalized.bottom() + tolerance;
    const bool nearVerticalEdge = withinVerticalSpan && (std::abs(pos.x() - normalized.left()) <= tolerance || std::abs(pos.x() - normalized.right()) <= tolerance);
    const bool nearHorizontalEdge = withinHorizontalSpan && (std::abs(pos.y() - normalized.top()) <= tolerance || std::abs(pos.y() - normalized.bottom()) <= tolerance);
    return nearVerticalEdge || nearHorizontalEdge;
}

static void
makeExclusive(const KnobButtonPtr& selected, const KnobButtonPtr& first, const KnobButtonPtr& second)
{
    if (!selected || !selected->getValue()) {
        return;
    }
    if (first && first != selected && first->getValue()) {
        first->setValue(false, ViewSpec::all(), 0, true);
    }
    if (second && second != selected && second->getValue()) {
        second->setValue(false, ViewSpec::all(), 0, true);
    }
}

} // namespace

struct AIPaintPrivate
{
    AIPaintContext context;
    KnobButtonWPtr selectTool;
    KnobButtonWPtr pointTool;
    KnobButtonWPtr boxTool;
    KnobButtonWPtr deleteSelected;
    KnobButtonWPtr clearAll;
    KnobButtonWPtr loadSam3;
    KnobButtonWPtr unloadSam3;
    KnobButtonWPtr livePreview;
    KnobStringWPtr sam3Status;
    KnobStringWPtr promptStore;
    bool isDraggingBox;
    QPointF boxDragStart;
    QPointF boxDragCurrent;
    QString liveMaskPath;
    int liveMaskTimelineFrame;
    RectD liveMaskCanonicalBounds;
    bool liveMaskBoundsValid;
    QString lastDrawLogSignature;
    QString lastToolLogSignature;

    AIPaintPrivate()
        : context()
        , selectTool()
        , pointTool()
        , boxTool()
        , deleteSelected()
        , clearAll()
        , loadSam3()
        , unloadSam3()
        , livePreview()
        , sam3Status()
        , promptStore()
        , isDraggingBox(false)
        , boxDragStart(0., 0.)
        , boxDragCurrent(0., 0.)
        , liveMaskPath()
        , liveMaskTimelineFrame(std::numeric_limits<int>::min())
        , liveMaskCanonicalBounds()
        , liveMaskBoundsValid(false)
        , lastDrawLogSignature()
        , lastToolLogSignature()
    {
    }

    AIPaintTool activeTool() const
    {
        KnobButtonPtr point = pointTool.lock();
        if (point && point->getValue()) {
            return AIPaintTool::Point;
        }
        KnobButtonPtr box = boxTool.lock();
        if (box && box->getValue()) {
            return AIPaintTool::Box;
        }
        return AIPaintTool::Select;
    }
};

AIPaint::AIPaint(NodePtr node)
    : EffectInstance(node)
    , _imp(new AIPaintPrivate())
{
    setSupportsRenderScaleMaybe(eSupportsYes);
}

AIPaint::~AIPaint()
{
}

std::vector<AIPaintPrompt>
AIPaint::getPrompts() const
{
    return _imp->context.prompts();
}

void
AIPaint::persistPromptsAndRedraw()
{
    KnobStringPtr store = _imp->promptStore.lock();
    if (store) {
        const std::string serialized = _imp->context.serialize();
        store->setValue(serialized, ViewSpec::all(), 0, true);
        QJsonObject found;
        found.insert(QString::fromUtf8("stored_prompt_count"), static_cast<int>(_imp->context.prompts().size()));
        found.insert(QString::fromUtf8("serialized_bytes"), static_cast<int>(serialized.size()));
        aiPaintLogEvent(this, QString::fromUtf8("prompt_store_serialized"), QString::fromUtf8("pass"),
                        QString::fromUtf8("AI Paint prompt store serialized."), found);
    } else {
        aiPaintLogEvent(this, QString::fromUtf8("prompt_store_serialized"), QString::fromUtf8("block"),
                        QString::fromUtf8("AI Paint prompt store knob is missing."), QJsonObject(),
                        QString::fromUtf8("Verify AIPaint::initializeKnobs created aiPaintPromptStore."));
    }
    redrawOverlayInteract();
}

bool
AIPaint::selectPrompt(int id)
{
    if (!_imp->context.selectPrompt(id)) {
        return false;
    }
    persistPromptsAndRedraw();
    return true;
}

bool
AIPaint::clearPromptSelection()
{
    if (!_imp->context.clearSelection()) {
        return false;
    }
    persistPromptsAndRedraw();
    return true;
}

bool
AIPaint::deletePrompt(int id)
{
    if (!_imp->context.deletePrompt(id)) {
        return false;
    }
    persistPromptsAndRedraw();
    return true;
}

bool
AIPaint::deleteSelectedPrompt()
{
    if (!_imp->context.deleteSelectedPrompt()) {
        return false;
    }
    persistPromptsAndRedraw();
    return true;
}

int
AIPaint::selectedPromptId() const
{
    return _imp->context.selectedPromptId();
}

void
AIPaint::setLivePreviewMaskPath(const QString& absolutePngPath, int timelineFrame,
                                double boundsX1, double boundsY1,
                                double boundsX2, double boundsY2,
                                bool boundsValid)
{
    const bool incomingBoundsValid = boundsValid && boundsX2 > boundsX1 && boundsY2 > boundsY1;
    const bool sameBounds = (!incomingBoundsValid && !_imp->liveMaskBoundsValid) ||
                            (incomingBoundsValid && _imp->liveMaskBoundsValid &&
                             _imp->liveMaskCanonicalBounds.x1 == boundsX1 &&
                             _imp->liveMaskCanonicalBounds.y1 == boundsY1 &&
                             _imp->liveMaskCanonicalBounds.x2 == boundsX2 &&
                             _imp->liveMaskCanonicalBounds.y2 == boundsY2);
    if (_imp->liveMaskPath == absolutePngPath &&
        _imp->liveMaskTimelineFrame == timelineFrame &&
        sameBounds) {
        redrawOverlayInteract();
        return;
    }
    _imp->liveMaskPath = absolutePngPath;
    _imp->liveMaskTimelineFrame = timelineFrame;
    if (incomingBoundsValid) {
        _imp->liveMaskCanonicalBounds = RectD(boundsX1, boundsY1, boundsX2, boundsY2);
        _imp->liveMaskBoundsValid = true;
    } else {
        _imp->liveMaskCanonicalBounds = RectD();
        _imp->liveMaskBoundsValid = false;
    }
    std::fprintf(stderr, "[FLUX-AI] live_mask_bounds_applied path='%s' frame=%d bounds_valid=%d bounds=[%g,%g,%g,%g]\n",
                 absolutePngPath.toUtf8().constData(), timelineFrame,
                 _imp->liveMaskBoundsValid ? 1 : 0,
                 _imp->liveMaskCanonicalBounds.x1, _imp->liveMaskCanonicalBounds.y1,
                 _imp->liveMaskCanonicalBounds.x2, _imp->liveMaskCanonicalBounds.y2);
    redrawOverlayInteract();
}

QString
AIPaint::livePreviewMaskPath() const
{
    return _imp->liveMaskPath;
}

int
AIPaint::livePreviewMaskTimelineFrame() const
{
    return _imp->liveMaskTimelineFrame;
}

void
AIPaint::clearLivePreviewMask()
{
    if (_imp->liveMaskPath.isEmpty()) {
        return;
    }
    _imp->liveMaskPath.clear();
    _imp->liveMaskTimelineFrame = std::numeric_limits<int>::min();
    _imp->liveMaskCanonicalBounds = RectD();
    _imp->liveMaskBoundsValid = false;
    redrawOverlayInteract();
}

std::string
AIPaint::getPluginID() const
{
    return PLUGINID_NATRON_AIPAINT;
}

std::string
AIPaint::getPluginLabel() const
{
    return "AI Paint";
}

std::string
AIPaint::getPluginDescription() const
{
    return "Stores AI matte prompt annotations and draws them as viewer-only overlays. "
           "The node is an image identity: prompt dots and boxes are not rendered into RGBA output.";
}

void
AIPaint::getPluginGrouping(std::list<std::string>* grouping) const
{
    grouping->push_back(PLUGIN_GROUP_PAINT);
}

std::string
AIPaint::getInputLabel(int /*inputNb*/) const
{
    return "Source";
}

void
AIPaint::addAcceptedComponents(int /*inputNb*/, std::list<ImagePlaneDesc>* comps)
{
    comps->push_back(ImagePlaneDesc::getRGBComponents());
    comps->push_back(ImagePlaneDesc::getRGBAComponents());
    comps->push_back(ImagePlaneDesc::getAlphaComponents());
}

void
AIPaint::addSupportedBitDepth(std::list<ImageBitDepthEnum>* depths) const
{
    depths->push_back(eImageBitDepthByte);
    depths->push_back(eImageBitDepthShort);
    depths->push_back(eImageBitDepthFloat);
}

RenderSafetyEnum
AIPaint::renderThreadSafety() const
{
    return eRenderSafetyFullySafeFrame;
}

bool
AIPaint::isHostChannelSelectorSupported(bool* /*defaultR*/, bool* /*defaultG*/, bool* /*defaultB*/, bool* /*defaultA*/) const
{
    return false;
}

void
AIPaint::initializeKnobs()
{
    KnobPagePtr page = AppManager::createKnob<KnobPage>(this, tr("AI Paint"));

    KnobStringPtr info = AppManager::createKnob<KnobString>(this, tr("Info"));
    info->setName("aiPaintStatus");
    info->setAsLabel();
    info->setAnimationEnabled(false);
    info->setEvaluateOnChange(false);
    info->setIsPersistent(false);
    info->setValue("AI Paint stores viewer-only point and box prompts. These overlays do not render into RGBA output.", ViewSpec::all(), 0, true);
    page->addKnob(info);

    KnobStringPtr promptStore = AppManager::createKnob<KnobString>(this, tr("Prompts"));
    promptStore->setName(kAIPaintParamPromptStore);
    promptStore->setHintToolTip(tr("Versioned AI Paint prompt data stored with the project."));
    promptStore->setAsMultiLine();
    promptStore->setAnimationEnabled(false);
    promptStore->setEvaluateOnChange(false);
    promptStore->setSecretByDefault(true);
    page->addKnob(promptStore);
    _imp->promptStore = promptStore;

    KnobPagePtr toolbar = AppManager::createKnob<KnobPage>(this, std::string(kAIPaintParamToolbar));
    toolbar->setAsToolBar(true);
    toolbar->setEvaluateOnChange(false);
    toolbar->setSecretByDefault(true);

    KnobGroupPtr selectGroup = AppManager::createKnob<KnobGroup>(this, tr("Select"));
    selectGroup->setName(kAIPaintParamSelectToolGroup);
    selectGroup->setAsToolButton(true);
    selectGroup->setEvaluateOnChange(false);
    selectGroup->setSecretByDefault(true);
    selectGroup->setInViewerContextCanHaveShortcut(true);
    selectGroup->setIsPersistent(false);
    toolbar->addKnob(selectGroup);

    KnobButtonPtr selectTool = AppManager::createKnob<KnobButton>(this, tr("Select"));
    selectTool->setName(kAIPaintParamSelectTool);
    selectTool->setHintToolTip(tr("Idle/select mode. Viewer clicks do not add AI Paint prompts."));
    selectTool->setCheckable(true);
    selectTool->setDefaultValue(true);
    selectTool->setEvaluateOnChange(false);
    selectTool->setSecretByDefault(true);
    selectTool->setIsPersistent(false);
    selectGroup->addKnob(selectTool);
    _imp->selectTool = selectTool;

    KnobGroupPtr pointGroup = AppManager::createKnob<KnobGroup>(this, tr("AI Point"));
    pointGroup->setName(kAIPaintParamPointToolGroup);
    pointGroup->setAsToolButton(true);
    pointGroup->setEvaluateOnChange(false);
    pointGroup->setSecretByDefault(true);
    pointGroup->setInViewerContextCanHaveShortcut(true);
    pointGroup->setIsPersistent(false);
    pointGroup->setIconLabel(NATRON_IMAGES_PATH "addPoints.png");
    toolbar->addKnob(pointGroup);

    KnobButtonPtr pointTool = AppManager::createKnob<KnobButton>(this, tr("AI Point"));
    pointTool->setName(kAIPaintParamPointTool);
    pointTool->setHintToolTip(tr("Add an include point prompt for AI matte generation."));
    pointTool->setCheckable(true);
    pointTool->setDefaultValue(false);
    pointTool->setEvaluateOnChange(false);
    pointTool->setSecretByDefault(true);
    pointTool->setIsPersistent(false);
    pointTool->setIconLabel(NATRON_IMAGES_PATH "addPoints.png");
    pointGroup->addKnob(pointTool);
    _imp->pointTool = pointTool;

    KnobGroupPtr boxGroup = AppManager::createKnob<KnobGroup>(this, tr("AI Box"));
    boxGroup->setName(kAIPaintParamBoxToolGroup);
    boxGroup->setAsToolButton(true);
    boxGroup->setEvaluateOnChange(false);
    boxGroup->setSecretByDefault(true);
    boxGroup->setInViewerContextCanHaveShortcut(true);
    boxGroup->setIsPersistent(false);
    boxGroup->setIconLabel(NATRON_IMAGES_PATH "rectangle.png");
    toolbar->addKnob(boxGroup);

    KnobButtonPtr boxTool = AppManager::createKnob<KnobButton>(this, tr("AI Box"));
    boxTool->setName(kAIPaintParamBoxTool);
    boxTool->setHintToolTip(tr("Drag an include box prompt for AI matte generation."));
    boxTool->setCheckable(true);
    boxTool->setDefaultValue(false);
    boxTool->setEvaluateOnChange(false);
    boxTool->setSecretByDefault(true);
    boxTool->setIsPersistent(false);
    boxTool->setIconLabel(NATRON_IMAGES_PATH "rectangle.png");
    boxGroup->addKnob(boxTool);
    _imp->boxTool = boxTool;

    KnobButtonPtr deleteSelected = AppManager::createKnob<KnobButton>(this, tr("Delete Selected Prompt"));
    deleteSelected->setName(kAIPaintParamDeleteSelected);
    deleteSelected->setHintToolTip(tr("Delete the currently selected AI Paint prompt."));
    deleteSelected->setEvaluateOnChange(false);
    deleteSelected->setIsPersistent(false);
    page->addKnob(deleteSelected);
    _imp->deleteSelected = deleteSelected;

    KnobButtonPtr clearAll = AppManager::createKnob<KnobButton>(this, tr("Clear All Prompts"));
    clearAll->setName(kAIPaintParamClearAll);
    clearAll->setHintToolTip(tr("Remove all AI Paint prompts from this node."));
    clearAll->setEvaluateOnChange(false);
    clearAll->setIsPersistent(false);
    page->addKnob(clearAll);
    _imp->clearAll = clearAll;

    KnobButtonPtr loadSam3 = AppManager::createKnob<KnobButton>(this, tr("Load SAM3"));
    loadSam3->setName(kAIPaintParamLoadSam3);
    loadSam3->setHintToolTip(tr("Start the persistent SAM3 worker and load the model into VRAM."));
    loadSam3->setEvaluateOnChange(false);
    loadSam3->setIsPersistent(false);
    page->addKnob(loadSam3);
    _imp->loadSam3 = loadSam3;

    KnobButtonPtr unloadSam3 = AppManager::createKnob<KnobButton>(this, tr("Unload SAM3"));
    unloadSam3->setName(kAIPaintParamUnloadSam3);
    unloadSam3->setHintToolTip(tr("Unload SAM3 from the persistent worker and release VRAM."));
    unloadSam3->setEvaluateOnChange(false);
    unloadSam3->setIsPersistent(false);
    page->addKnob(unloadSam3);
    _imp->unloadSam3 = unloadSam3;

    KnobButtonPtr livePreview = AppManager::createKnob<KnobButton>(this, tr("Live Preview"));
    livePreview->setName(kAIPaintParamLivePreview);
    livePreview->setHintToolTip(tr("Enable SAM3 live-preview state for this AI Paint node. This does not load SAM3 automatically; use Load SAM3 first."));
    livePreview->setCheckable(true);
    livePreview->setDefaultValue(false);
    livePreview->setEvaluateOnChange(false);
    page->addKnob(livePreview);
    _imp->livePreview = livePreview;

    KnobStringPtr sam3Status = AppManager::createKnob<KnobString>(this, tr("SAM3 Status"));
    sam3Status->setName(kAIPaintParamSam3Status);
    sam3Status->setHintToolTip(tr("Runtime SAM3 controller status for this AI Paint node."));
    sam3Status->setAsLabel();
    sam3Status->setAnimationEnabled(false);
    sam3Status->setEvaluateOnChange(false);
    sam3Status->setIsPersistent(false);
    sam3Status->setValue("unloaded", ViewSpec::all(), 0, true);
    page->addKnob(sam3Status);
    _imp->sam3Status = sam3Status;

    addOverlaySlaveParam(selectTool);
    addOverlaySlaveParam(pointTool);
    addOverlaySlaveParam(boxTool);
}

void
AIPaint::onKnobsLoaded()
{
    KnobStringPtr store = _imp->promptStore.lock();
    if (store) {
        _imp->context.deserialize(store->getValue());
    }
}

bool
AIPaint::knobChanged(KnobI* k,
                     ValueChangedReasonEnum /*reason*/,
                     ViewSpec /*view*/,
                     double /*time*/,
                     bool /*originatedFromMainThread*/)
{
    KnobButtonPtr select = _imp->selectTool.lock();
    KnobButtonPtr point = _imp->pointTool.lock();
    KnobButtonPtr box = _imp->boxTool.lock();
    KnobButtonPtr deleteSelected = _imp->deleteSelected.lock();
    KnobButtonPtr clearAll = _imp->clearAll.lock();
    KnobButtonPtr loadSam3 = _imp->loadSam3.lock();
    KnobButtonPtr unloadSam3 = _imp->unloadSam3.lock();
    KnobButtonPtr livePreview = _imp->livePreview.lock();
    KnobStringPtr sam3Status = _imp->sam3Status.lock();
    KnobStringPtr store = _imp->promptStore.lock();

    if (select && k == select.get()) {
        makeExclusive(select, point, box);
        _imp->isDraggingBox = false;
        QJsonObject found;
        found.insert(QString::fromUtf8("tool"), QString::fromUtf8("select"));
        found.insert(QString::fromUtf8("checked"), select->getValue());
        aiPaintLogEvent(this, QString::fromUtf8("tool_changed"), QString::fromUtf8("pass"),
                        QString::fromUtf8("AI Paint select tool changed."), found);
        redrawOverlayInteract();
        return true;
    }
    if (point && k == point.get()) {
        makeExclusive(point, select, box);
        _imp->isDraggingBox = false;
        QJsonObject found;
        found.insert(QString::fromUtf8("tool"), QString::fromUtf8("point"));
        found.insert(QString::fromUtf8("checked"), point->getValue());
        aiPaintLogEvent(this, QString::fromUtf8("tool_changed"), QString::fromUtf8("pass"),
                        QString::fromUtf8("AI Paint point tool changed."), found);
        redrawOverlayInteract();
        return true;
    }
    if (box && k == box.get()) {
        makeExclusive(box, select, point);
        _imp->isDraggingBox = false;
        QJsonObject found;
        found.insert(QString::fromUtf8("tool"), QString::fromUtf8("box"));
        found.insert(QString::fromUtf8("checked"), box->getValue());
        aiPaintLogEvent(this, QString::fromUtf8("tool_changed"), QString::fromUtf8("pass"),
                        QString::fromUtf8("AI Paint box tool changed."), found);
        redrawOverlayInteract();
        return true;
    }
    if (deleteSelected && k == deleteSelected.get()) {
        if (_imp->context.deleteSelectedPrompt() && store) {
            store->setValue(_imp->context.serialize(), ViewSpec::all(), 0, true);
        }
        redrawOverlayInteract();
        return true;
    }
    if (clearAll && k == clearAll.get()) {
        _imp->context.clear();
        if (store) {
            store->setValue(_imp->context.serialize(), ViewSpec::all(), 0, true);
        }
        _imp->liveMaskPath.clear();
        _imp->liveMaskTimelineFrame = std::numeric_limits<int>::min();
        _imp->liveMaskCanonicalBounds = RectD();
        _imp->liveMaskBoundsValid = false;
        redrawOverlayInteract();
        return true;
    }
    if (store && k == store.get()) {
        _imp->context.deserialize(store->getValue());
        QJsonObject found;
        found.insert(QString::fromUtf8("stored_prompt_count"), static_cast<int>(_imp->context.prompts().size()));
        found.insert(QString::fromUtf8("serialized_bytes"), static_cast<int>(store->getValue().size()));
        aiPaintLogEvent(this, QString::fromUtf8("prompt_store_deserialized"), QString::fromUtf8("pass"),
                        QString::fromUtf8("AI Paint prompt store knob changed and was deserialized."), found);
        redrawOverlayInteract();
        return true;
    }
    if (loadSam3 && k == loadSam3.get()) {
        if (sam3Status) {
            sam3Status->setValue("loading", ViewSpec::all(), 0, true);
        }
        return true;
    }
    if (unloadSam3 && k == unloadSam3.get()) {
        if (sam3Status) {
            sam3Status->setValue("unloading", ViewSpec::all(), 0, true);
        }
        return true;
    }
    if (livePreview && k == livePreview.get()) {
        if (sam3Status) {
            sam3Status->setValue(livePreview->getValue() ? "live preview enabled; SAM3 load required if unloaded" : "unloaded", ViewSpec::all(), 0, true);
        }
        return true;
    }

    return false;
}

void
AIPaint::drawOverlay(double time, const RenderScale& /*renderScale*/, ViewIdx /*view*/)
{
    OverlaySupport* overlay = getCurrentViewportForOverlays();
    if (!overlay) {
        return;
    }

    double pixelScaleX = 1.;
    double pixelScaleY = 1.;
    overlay->getPixelScale(pixelScaleX, pixelScaleY);
    double screenPixelRatio = overlay->getScreenPixelRatio();

    GLProtectAttrib a(GL_CURRENT_BIT | GL_COLOR_BUFFER_BIT | GL_LINE_BIT | GL_POINT_BIT | GL_ENABLE_BIT | GL_HINT_BIT | GL_TEXTURE_BIT);
    glEnable(GL_LINE_SMOOTH);
    glHint(GL_LINE_SMOOTH_HINT, GL_DONT_CARE);

    const int currentTimelineFrame = std::isfinite(time) ? static_cast<int>(std::lround(time)) : std::numeric_limits<int>::min();
    const bool drawLiveMask = !_imp->liveMaskPath.isEmpty() &&
                              (_imp->liveMaskTimelineFrame == std::numeric_limits<int>::min() ||
                               _imp->liveMaskTimelineFrame == currentTimelineFrame);
    if (drawLiveMask) {
        cairo_surface_t* surface = cairo_image_surface_create_from_png(_imp->liveMaskPath.toUtf8().constData());
        GLuint liveMaskTexture = 0;
        int liveMaskWidth = 0;
        int liveMaskHeight = 0;
        if (surface && cairo_surface_status(surface) == CAIRO_STATUS_SUCCESS) {
            cairo_surface_flush(surface);
            const int width = cairo_image_surface_get_width(surface);
            const int height = cairo_image_surface_get_height(surface);
            const int stride = cairo_image_surface_get_stride(surface);
            unsigned char* data = cairo_image_surface_get_data(surface);
            if (width > 0 && height > 0 && stride == width * 4 && data) {
                glGenTextures(1, &liveMaskTexture);
                glBindTexture(GL_TEXTURE_2D, liveMaskTexture);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height,
                             0, GL_BGRA, GL_UNSIGNED_BYTE, data);
                glBindTexture(GL_TEXTURE_2D, 0);
                liveMaskWidth = width;
                liveMaskHeight = height;
            }
        }

        if (liveMaskTexture != 0 && liveMaskWidth > 0 && liveMaskHeight > 0) {
            double vx1, vy1, vx2, vy2;
            if (_imp->liveMaskBoundsValid) {
                // Use stored canonical bounds for correct overlay alignment.
                // Bounds are in canonical (pixel) space: x1=left, y1=bottom, x2=right, y2=top.
                // tex (0,0) -> vertex (x1, y2)   bottom-left of texture maps to top-left of canonical rect
                // tex (1,0) -> vertex (x2, y2)   bottom-right of texture maps to top-right of canonical rect
                // tex (1,1) -> vertex (x2, y1)   top-right of texture maps to bottom-right of canonical rect
                // tex (0,1) -> vertex (x1, y1)   top-left of texture maps to bottom-left of canonical rect
                vx1 = _imp->liveMaskCanonicalBounds.x1;
                vy1 = _imp->liveMaskCanonicalBounds.y1;
                vx2 = _imp->liveMaskCanonicalBounds.x2;
                vy2 = _imp->liveMaskCanonicalBounds.y2;
            } else {
                // Fallback: draw at (0,0)-(width,height) for backward compatibility.
                vx1 = 0.;
                vy1 = 0.;
                vx2 = liveMaskWidth;
                vy2 = liveMaskHeight;
            }
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, liveMaskTexture);
            glColor4f(0.1f, 0.65f, 1.f, 0.45f);
            glBegin(GL_QUADS);
            glTexCoord2f(0.f, 0.f); glVertex2d(vx1, vy2);
            glTexCoord2f(1.f, 0.f); glVertex2d(vx2, vy2);
            glTexCoord2f(1.f, 1.f); glVertex2d(vx2, vy1);
            glTexCoord2f(0.f, 1.f); glVertex2d(vx1, vy1);
            glEnd();
            glBindTexture(GL_TEXTURE_2D, 0);
            glDisable(GL_TEXTURE_2D);
            glDeleteTextures(1, &liveMaskTexture);
            std::fprintf(stderr, "[FLUX-AI] live_mask_drawn bounds_valid=%d vertex=[%g,%g,%g,%g] tex_size=%dx%d\n",
                         _imp->liveMaskBoundsValid ? 1 : 0, vx1, vy1, vx2, vy2, liveMaskWidth, liveMaskHeight);
        }

        if (surface) {
            cairo_surface_destroy(surface);
        }
    }

    std::vector<AIPaintPrompt> prompts = _imp->context.prompts();
    const int currentPromptCount = currentFramePromptCount(prompts, time);
    const QString drawSignature = QString::fromUtf8("frame=%1|total=%2|current=%3|live=%4|bounds_valid=%5|bounds=%6,%7,%8,%9")
            .arg(currentTimelineFrame)
            .arg(static_cast<int>(prompts.size()))
            .arg(currentPromptCount)
            .arg(drawLiveMask)
            .arg(_imp->liveMaskBoundsValid ? 1 : 0)
            .arg(_imp->liveMaskCanonicalBounds.x1)
            .arg(_imp->liveMaskCanonicalBounds.y1)
            .arg(_imp->liveMaskCanonicalBounds.x2)
            .arg(_imp->liveMaskCanonicalBounds.y2);
    if (drawSignature != _imp->lastDrawLogSignature) {
        _imp->lastDrawLogSignature = drawSignature;
        QJsonObject found;
        found.insert(QString::fromUtf8("timeline_frame"), currentTimelineFrame);
        found.insert(QString::fromUtf8("stored_prompt_count"), static_cast<int>(prompts.size()));
        found.insert(QString::fromUtf8("current_frame_enabled_prompt_count"), currentPromptCount);
        found.insert(QString::fromUtf8("draw_live_mask"), drawLiveMask);
        found.insert(QString::fromUtf8("live_mask_path"), _imp->liveMaskPath);
        found.insert(QString::fromUtf8("live_mask_bounds_valid"), _imp->liveMaskBoundsValid);
        if (_imp->liveMaskBoundsValid) {
            QJsonObject boundsLog;
            boundsLog.insert(QString::fromUtf8("x1"), _imp->liveMaskCanonicalBounds.x1);
            boundsLog.insert(QString::fromUtf8("y1"), _imp->liveMaskCanonicalBounds.y1);
            boundsLog.insert(QString::fromUtf8("x2"), _imp->liveMaskCanonicalBounds.x2);
            boundsLog.insert(QString::fromUtf8("y2"), _imp->liveMaskCanonicalBounds.y2);
            found.insert(QString::fromUtf8("live_mask_canonical_bounds"), boundsLog);
        }
        aiPaintLogEvent(this, QString::fromUtf8("overlay_draw_state"),
                        currentPromptCount > 0 || prompts.empty() ? QString::fromUtf8("pass") : QString::fromUtf8("block"),
                        currentPromptCount > 0 ? QString::fromUtf8("AI Paint overlay drawing current-frame prompts.") : QString::fromUtf8("AI Paint overlay has no current-frame prompts to draw."),
                        found,
                        currentPromptCount > 0 || prompts.empty() ? QString() : QString::fromUtf8("Check prompt frame ownership metadata vs viewer timeline frame."));
    }
    for (const AIPaintPrompt& prompt : prompts) {
        if (!prompt.enabled) {
            continue;
        }
        if (!promptOwnsTimelineFrame(prompt, time)) {
            continue;
        }
        float r = 0.f, g = 0.f, b = 0.f;
        promptColor(prompt.role, &r, &g, &b);
        double radiusX = std::max(3., prompt.displaySize) * pixelScaleX;
        double radiusY = std::max(3., prompt.displaySize) * pixelScaleY;

        if (prompt.type == AIPaintPromptType::Point) {
            const double x = prompt.point.x();
            const double y = prompt.point.y();
            glPointSize(std::max(5., prompt.displaySize) * screenPixelRatio);
            glColor4f(0.f, 0.f, 0.f, 0.85f);
            glBegin(GL_POINTS);
            glVertex2d(x, y);
            glEnd();
            glPointSize(std::max(3., prompt.displaySize - 3.) * screenPixelRatio);
            glColor4f(r, g, b, 1.f);
            glBegin(GL_POINTS);
            glVertex2d(x, y);
            glEnd();
            if (prompt.selected) {
                glLineWidth(3. * screenPixelRatio);
                glColor4f(1.f, 1.f, 1.f, 1.f);
                glBegin(GL_LINE_LOOP);
                glVertex2d(x - radiusX, y - radiusY);
                glVertex2d(x + radiusX, y - radiusY);
                glVertex2d(x + radiusX, y + radiusY);
                glVertex2d(x - radiusX, y + radiusY);
                glEnd();
                glColor4f(r, g, b, 1.f);
            }
            glLineWidth(1.5 * screenPixelRatio);
            glBegin(GL_LINES);
            glVertex2d(x - radiusX, y);
            glVertex2d(x + radiusX, y);
            glVertex2d(x, y - radiusY);
            glVertex2d(x, y + radiusY);
            glEnd();
        } else if (prompt.type == AIPaintPromptType::Box) {
            QRectF rect = prompt.rect.normalized();
            glLineWidth(3. * screenPixelRatio);
            glColor4f(0.f, 0.f, 0.f, 0.85f);
            glBegin(GL_LINE_LOOP);
            glVertex2d(rect.left(), rect.top());
            glVertex2d(rect.right(), rect.top());
            glVertex2d(rect.right(), rect.bottom());
            glVertex2d(rect.left(), rect.bottom());
            glEnd();
            if (prompt.selected) {
                glLineWidth(5. * screenPixelRatio);
                glColor4f(1.f, 1.f, 1.f, 1.f);
                glBegin(GL_LINE_LOOP);
                glVertex2d(rect.left(), rect.top());
                glVertex2d(rect.right(), rect.top());
                glVertex2d(rect.right(), rect.bottom());
                glVertex2d(rect.left(), rect.bottom());
                glEnd();
            }
            glLineWidth(1.5 * screenPixelRatio);
            glColor4f(r, g, b, 1.f);
            glBegin(GL_LINE_LOOP);
            glVertex2d(rect.left(), rect.top());
            glVertex2d(rect.right(), rect.top());
            glVertex2d(rect.right(), rect.bottom());
            glVertex2d(rect.left(), rect.bottom());
            glEnd();
        }
    }

    if (_imp->isDraggingBox) {
        QRectF rect(_imp->boxDragStart, _imp->boxDragCurrent);
        rect = rect.normalized();
        glLineWidth(1.5 * screenPixelRatio);
        glColor4f(0.2f, 0.55f, 1.f, 1.f);
        glBegin(GL_LINE_LOOP);
        glVertex2d(rect.left(), rect.top());
        glVertex2d(rect.right(), rect.top());
        glVertex2d(rect.right(), rect.bottom());
        glVertex2d(rect.left(), rect.bottom());
        glEnd();
    }
}

bool
AIPaint::onOverlayPenDown(double time,
                          const RenderScale& /*renderScale*/,
                          ViewIdx /*view*/,
                          const QPointF& /*viewportPos*/,
                          const QPointF& pos,
                          double /*pressure*/,
                          double /*timestamp*/,
                          PenType pen)
{
    if (pen != ePenTypeLMB && pen != ePenTypePen) {
        QJsonObject found;
        found.insert(QString::fromUtf8("pen"), static_cast<int>(pen));
        found.insert(QString::fromUtf8("time"), time);
        found.insert(QString::fromUtf8("x"), pos.x());
        found.insert(QString::fromUtf8("y"), pos.y());
        aiPaintLogEvent(this, QString::fromUtf8("overlay_pen_down"), QString::fromUtf8("skip"),
                        QString::fromUtf8("AI Paint ignored non-left-button/non-pen overlay input."), found);
        return false;
    }

    const AIPaintTool tool = _imp->activeTool();
    QJsonObject penFound;
    penFound.insert(QString::fromUtf8("time"), time);
    penFound.insert(QString::fromUtf8("timeline_frame"), std::isfinite(time) ? static_cast<int>(std::lround(time)) : std::numeric_limits<int>::min());
    penFound.insert(QString::fromUtf8("x"), pos.x());
    penFound.insert(QString::fromUtf8("y"), pos.y());
    penFound.insert(QString::fromUtf8("pen"), static_cast<int>(pen));
    penFound.insert(QString::fromUtf8("active_tool"), tool == AIPaintTool::Point ? QString::fromUtf8("point") : (tool == AIPaintTool::Box ? QString::fromUtf8("box") : QString::fromUtf8("select")));
    aiPaintLogEvent(this, QString::fromUtf8("overlay_pen_down"), QString::fromUtf8("pass"),
                    QString::fromUtf8("AI Paint overlay received pen down."), penFound);
    if (tool == AIPaintTool::Select) {
        double pixelScaleX = 1.;
        double pixelScaleY = 1.;
        OverlaySupport* overlay = getCurrentViewportForOverlays();
        if (overlay) {
            overlay->getPixelScale(pixelScaleX, pixelScaleY);
        }
        const double promptScale = std::max(pixelScaleX, pixelScaleY);
        const std::vector<AIPaintPrompt> prompts = _imp->context.prompts();
        int hitId = -1;
        for (std::vector<AIPaintPrompt>::const_reverse_iterator it = prompts.rbegin(); it != prompts.rend(); ++it) {
            if (!it->enabled || !promptOwnsTimelineFrame(*it, time)) {
                continue;
            }
            const double tolerance = std::max(6., it->displaySize) * promptScale;
            if (it->type == AIPaintPromptType::Point) {
                const double dx = pos.x() - it->point.x();
                const double dy = pos.y() - it->point.y();
                if ((dx * dx + dy * dy) <= tolerance * tolerance) {
                    hitId = it->id;
                    break;
                }
            } else if (it->type == AIPaintPromptType::Box && pointOnBox(it->rect.normalized(), pos, tolerance)) {
                hitId = it->id;
                break;
            }
        }

        const int oldSelected = _imp->context.selectedPromptId();
        const bool changed = hitId > 0 ? _imp->context.selectPrompt(hitId) && oldSelected != hitId : _imp->context.clearSelection();
        if (changed) {
            KnobStringPtr store = _imp->promptStore.lock();
            if (store) {
                store->setValue(_imp->context.serialize(), ViewSpec::all(), 0, true);
            }
            redrawOverlayInteract();
            return true;
        }
        return hitId > 0;
    }

    if (tool == AIPaintTool::Point) {
        const int id = _imp->context.addPoint(time, pos, kDefaultPromptDisplaySize, AIPaintPromptRole::Include, buildPromptFrameMetadata(time));
        KnobStringPtr store = _imp->promptStore.lock();
        if (store) {
            store->setValue(_imp->context.serialize(), ViewSpec::all(), 0, true);
        }
        QJsonObject found;
        found.insert(QString::fromUtf8("prompt_id"), id);
        found.insert(QString::fromUtf8("type"), QString::fromUtf8("point"));
        found.insert(QString::fromUtf8("role"), QString::fromUtf8("include"));
        found.insert(QString::fromUtf8("time"), time);
        found.insert(QString::fromUtf8("timeline_frame"), std::isfinite(time) ? static_cast<int>(std::lround(time)) : std::numeric_limits<int>::min());
        found.insert(QString::fromUtf8("x"), pos.x());
        found.insert(QString::fromUtf8("y"), pos.y());
        found.insert(QString::fromUtf8("stored_prompt_count"), static_cast<int>(_imp->context.prompts().size()));
        found.insert(QString::fromUtf8("store_knob_present"), static_cast<bool>(store));
        aiPaintLogEvent(this, QString::fromUtf8("prompt_added"), QString::fromUtf8("pass"),
                        QString::fromUtf8("AI Paint point prompt added to prompt store."), found);
        redrawOverlayInteract();
        return true;
    }

    _imp->isDraggingBox = true;
    _imp->boxDragStart = pos;
    _imp->boxDragCurrent = pos;
    redrawOverlayInteract();

    return true;
}

bool
AIPaint::onOverlayPenMotion(double /*time*/, const RenderScale& /*renderScale*/, ViewIdx /*view*/,
                            const QPointF& /*viewportPos*/, const QPointF& pos,
                            double /*pressure*/, double /*timestamp*/)
{
    if (!_imp->isDraggingBox) {
        return false;
    }

    _imp->boxDragCurrent = pos;
    redrawOverlayInteract();

    return true;
}

bool
AIPaint::onOverlayPenUp(double time,
                        const RenderScale& /*renderScale*/,
                        ViewIdx /*view*/,
                        const QPointF& /*viewportPos*/,
                        const QPointF& pos,
                        double /*pressure*/,
                        double /*timestamp*/)
{
    if (!_imp->isDraggingBox) {
        return false;
    }

    _imp->isDraggingBox = false;
    QRectF rect(_imp->boxDragStart, pos);
    rect = rect.normalized();
    if (rect.width() > 1e-6 && rect.height() > 1e-6) {
        const int id = _imp->context.addBox(time, rect, kDefaultPromptDisplaySize, AIPaintPromptRole::Include, buildPromptFrameMetadata(time));
        KnobStringPtr store = _imp->promptStore.lock();
        if (store) {
            store->setValue(_imp->context.serialize(), ViewSpec::all(), 0, true);
        }
        QJsonObject found;
        found.insert(QString::fromUtf8("prompt_id"), id);
        found.insert(QString::fromUtf8("type"), QString::fromUtf8("box"));
        found.insert(QString::fromUtf8("role"), QString::fromUtf8("include"));
        found.insert(QString::fromUtf8("time"), time);
        found.insert(QString::fromUtf8("timeline_frame"), std::isfinite(time) ? static_cast<int>(std::lround(time)) : std::numeric_limits<int>::min());
        found.insert(QString::fromUtf8("x1"), rect.left());
        found.insert(QString::fromUtf8("y1"), rect.top());
        found.insert(QString::fromUtf8("x2"), rect.right());
        found.insert(QString::fromUtf8("y2"), rect.bottom());
        found.insert(QString::fromUtf8("stored_prompt_count"), static_cast<int>(_imp->context.prompts().size()));
        found.insert(QString::fromUtf8("store_knob_present"), static_cast<bool>(store));
        aiPaintLogEvent(this, QString::fromUtf8("prompt_added"), QString::fromUtf8("pass"),
                        QString::fromUtf8("AI Paint box prompt added to prompt store."), found);
    } else {
        QJsonObject found;
        found.insert(QString::fromUtf8("time"), time);
        found.insert(QString::fromUtf8("x1"), rect.left());
        found.insert(QString::fromUtf8("y1"), rect.top());
        found.insert(QString::fromUtf8("x2"), rect.right());
        found.insert(QString::fromUtf8("y2"), rect.bottom());
        aiPaintLogEvent(this, QString::fromUtf8("prompt_added"), QString::fromUtf8("skip"),
                        QString::fromUtf8("AI Paint box drag ignored because rectangle was empty."), found);
    }
    redrawOverlayInteract();

    return true;
}

StatusEnum
AIPaint::getTransform(double /*time*/, const RenderScale& /*renderScale*/, bool /*draftRender*/,
                      ViewIdx /*view*/, EffectInstancePtr* inputToTransform,
                      Transform::Matrix3x3* transform)
{
    *inputToTransform = getInput(0);
    if (!*inputToTransform) {
        return eStatusFailed;
    }
    transform->a = 1.; transform->b = 0.; transform->c = 0.;
    transform->d = 0.; transform->e = 1.; transform->f = 0.;
    transform->g = 0.; transform->h = 0.; transform->i = 1.;

    return eStatusOK;
}

bool
AIPaint::getInputsHoldingTransform(std::list<int>* inputs) const
{
    inputs->push_back(0);
    return true;
}

bool
AIPaint::isIdentity(double time,
                    const RenderScale& /*scale*/,
                    const RectI& /*roi*/,
                    ViewIdx view,
                    double* inputTime,
                    ViewIdx* inputView,
                    int* inputNb)
{
    *inputTime = time;
    *inputView = view;
    *inputNb = 0;

    // AI Paint is pixel-identical to its input, but it owns crop-like output
    // bounds so SAM3/AI exports see the full canvas rather than an upstream
    // active RoD. Returning true here would let the host bypass this node and
    // keep the cropped upstream bounds.
    return false;
}

StatusEnum
AIPaint::getRegionOfDefinition(U64 hash, double time, const RenderScale& scale, ViewIdx view, RectD* rod)
{
    if (!rod) {
        return eStatusFailed;
    }

    EffectInstancePtr input = getInput(0);
    if (input) {
        const RectI inputFormat = input->getOutputFormat();
        if (!inputFormat.isNull() && inputFormat.width() > 0 && inputFormat.height() > 0) {
            const double par = input->getAspectRatio(-1);
            *rod = inputFormat.toCanonical_noClipping(0, par);
            return eStatusOK;
        }
    }

    NodePtr node = getNode();
    AppInstancePtr app = node ? node->getApp() : AppInstancePtr();
    ProjectPtr project = app ? app->getProject() : ProjectPtr();
    if (project) {
        Format projectFormat;
        project->getProjectDefaultFormat(&projectFormat);
        if (!projectFormat.isNull() && projectFormat.width() > 0 && projectFormat.height() > 0) {
            *rod = projectFormat.toCanonicalFormat();
            return eStatusOK;
        }
    }

    return EffectInstance::getRegionOfDefinition(hash, time, scale, view, rod);
}

StatusEnum
AIPaint::render(const RenderActionArgs& args)
{
    for (std::list<std::pair<ImagePlaneDesc, ImagePtr> >::const_iterator it = args.outputPlanes.begin();
         it != args.outputPlanes.end(); ++it) {
        const ImagePlaneDesc& outComps = it->first;
        const ImagePtr& out = it->second;
        if (!out) {
            continue;
        }

        out->fillZero(args.roi);

        ImagePtr src;
        EffectInstance::InputImagesMap::const_iterator inputIt = args.inputImages.find(0);
        if (inputIt != args.inputImages.end() && !inputIt->second.empty()) {
            src = inputIt->second.front();
        }
        if (!src) {
            RectI roiPixel;
            src = getImage(0, args.time, args.originalScale, args.view, NULL, &outComps,
                           false /*mapToClipPrefs*/, true /*dontUpscale*/, eStorageModeRAM,
                           0 /*textureDepth*/, &roiPixel);
        }
        if (!src) {
            continue;
        }
        if (src->getMipmapLevel() != out->getMipmapLevel()) {
            throw std::runtime_error("Host gave image with wrong scale");
        }

        const RectI copyRoi = args.roi.intersect(src->getBounds()).intersect(out->getBounds());
        if (copyRoi.isNull()) {
            continue;
        }

        if ( (src->getComponents() != out->getComponents()) || (src->getBitDepth() != out->getBitDepth()) ) {
            src->convertToFormat(copyRoi,
                                 getApp()->getDefaultColorSpaceForBitDepth(src->getBitDepth()),
                                 getApp()->getDefaultColorSpaceForBitDepth(out->getBitDepth()),
                                 3, false, false, out.get());
        } else {
            out->pasteFrom(*src, copyRoi, out->usesBitMap() && src->usesBitMap());
        }
    }

    return eStatusOK;
}

NATRON_NAMESPACE_EXIT
