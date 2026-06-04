#include "Gui/FluxAiPanel.h"

#include <QAbstractItemView>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QDateTime>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QProgressBar>
#include <QScrollArea>
#include <QSpinBox>
#include <QTimer>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <climits>
#include <limits>

#include "Engine/AIPaint.h"
#include "Engine/AbortableRenderInfo.h"
#include "Engine/CreateNodeArgs.h"
#include "Engine/EffectInstance.h"
#include "Engine/Image.h"
#include "Engine/KnobTypes.h"
#include "Engine/Lut.h"
#include "Engine/ParallelRenderArgs.h"
#include "Engine/Project.h"
#include "Engine/Node.h"
#include "Engine/ImagePlaneDesc.h"
#include "Gui/Gui.h"
#include "Gui/GuiAppInstance.h"
#include "Gui/FluxAiLog.h"
#include "Gui/FluxTimeline.h"
#include "Gui/ViewerGL.h"

NATRON_NAMESPACE_ENTER

static void setAIPaintSam3StatusKnob(const NodePtr& aiPaintNode, const QString& status);
static bool aipaintLivePreviewEnabled(const NodePtr& aiPaintNode);
static void clearAIPaintLivePreviewMask(const NodePtr& aiPaintNode);

static bool sanitizeProjectRelativePath(const QString& value, QString* sanitized)
{
    const QString trimmed = value.trimmed();
    if (trimmed.isEmpty() || trimmed.contains(QString::fromUtf8("://")) || QDir::isAbsolutePath(trimmed)) {
        return false;
    }
    if ((trimmed.size() >= 2 && trimmed.at(1) == QLatin1Char(':')) || trimmed.startsWith(QString::fromUtf8("\\\\")) || trimmed.startsWith(QString::fromUtf8("//"))) {
        return false;
    }
    const QString normalizedSeparators = QString(trimmed).replace(QLatin1Char('\\'), QLatin1Char('/'));
    const QStringList components = normalizedSeparators.split(QLatin1Char('/'));
    for (const QString& component : components) {
        if (component == QString::fromUtf8("..")) {
            return false;
        }
    }
    const QString cleaned = QDir::cleanPath(normalizedSeparators);
    if (cleaned.isEmpty() || cleaned == QString::fromUtf8(".") || cleaned == QString::fromUtf8("..") || cleaned.startsWith(QString::fromUtf8("../")) || QDir::isAbsolutePath(cleaned)) {
        return false;
    }
    if (sanitized) {
        *sanitized = cleaned;
    }
    return true;
}

/**
 * @brief Convert a Natron Image (float pixel buffer) to a QImage suitable for PNG export.
 *
 * Follows the same conversion pattern as Gui::debugImage: reads from the image bounds,
 * applies sRGB LUT tonemapping via ordered dither, and flips vertically because Natron
 * images use bottom-left origin while QImage uses top-left origin.
 *
 * Supports: float (primary), byte, short. Half-float is rejected with a clear log.
 */
static QImage
fluxImageToQImage(const Image* image, QString* message)
{
    const RectI bounds = image->getBounds();
    if (bounds.isNull() || bounds.width() <= 0 || bounds.height() <= 0) {
        if (message) { *message = QString::fromUtf8("image bounds are null or empty"); }
        return QImage();
    }

    const ImageBitDepthEnum bitDepth = image->getBitDepth();
    const int srcNComps = static_cast<int>(image->getComponentsCount());
    if (srcNComps < 1 || srcNComps > 4) {
        if (message) { *message = QString::fromUtf8("unsupported component count: %1").arg(srcNComps); }
        return QImage();
    }

    const int w = bounds.width();
    const int h = bounds.height();
    QImage output(w, h, QImage::Format_ARGB32);

    if (bitDepth == eImageBitDepthFloat) {
        const Color::Lut* lut = Color::LutManager::sRGBLut();
        lut->validate();
        Image::ReadAccess acc = image->getReadRights();
        const int srcRowElements = srcNComps * w;
        const float* from = reinterpret_cast<const float*>(acc.pixelAt(bounds.x1, bounds.y1));
        for (int y = h - 1; y >= 0; --y, from += (srcRowElements - srcNComps * w)) {
            QRgb* dstPixels = reinterpret_cast<QRgb*>(output.scanLine(y));
            unsigned error_r = 0x80;
            unsigned error_g = 0x80;
            unsigned error_b = 0x80;
            for (int x = 0; x < w; ++x, from += srcNComps, ++dstPixels) {
                float r, g, b, a;
                switch (srcNComps) {
                case 1: r = g = b = *from; a = 1.0f; break;
                case 2: r = *from; g = *(from + 1); b = 0.0f; a = 1.0f; break;
                case 3: r = *from; g = *(from + 1); b = *(from + 2); a = 1.0f; break;
                case 4: r = *from; g = *(from + 1); b = *(from + 2); a = *(from + 3); break;
                default: r = g = b = 0.0f; a = 1.0f; break;
                }
                error_r = (error_r & 0xff) + lut->toColorSpaceUint8xxFromLinearFloatFast(r);
                error_g = (error_g & 0xff) + lut->toColorSpaceUint8xxFromLinearFloatFast(g);
                error_b = (error_b & 0xff) + lut->toColorSpaceUint8xxFromLinearFloatFast(b);
                *dstPixels = qRgba(static_cast<U8>(error_r >> 8),
                                   static_cast<U8>(error_g >> 8),
                                   static_cast<U8>(error_b >> 8),
                                   static_cast<U8>(std::min(255.0f, a * 255.0f)));
            }
        }
    } else if (bitDepth == eImageBitDepthByte) {
        Image::ReadAccess acc = image->getReadRights();
        const int srcRowElements = srcNComps * w;
        const unsigned char* from = acc.pixelAt(bounds.x1, bounds.y1);
        for (int y = h - 1; y >= 0; --y, from += (srcRowElements - srcNComps * w)) {
            QRgb* dstPixels = reinterpret_cast<QRgb*>(output.scanLine(y));
            for (int x = 0; x < w; ++x, from += srcNComps, ++dstPixels) {
                int r = 0, g = 0, b = 0, a = 255;
                switch (srcNComps) {
                case 1: r = g = b = *from; break;
                case 2: r = *from; g = *(from + 1); break;
                case 3: r = *from; g = *(from + 1); b = *(from + 2); break;
                case 4: r = *from; g = *(from + 1); b = *(from + 2); a = *(from + 3); break;
                default: break;
                }
                *dstPixels = qRgba(r, g, b, a);
            }
        }
    } else if (bitDepth == eImageBitDepthShort) {
        Image::ReadAccess acc = image->getReadRights();
        const int srcRowElements = srcNComps * w;
        const unsigned short* from = reinterpret_cast<const unsigned short*>(acc.pixelAt(bounds.x1, bounds.y1));
        for (int y = h - 1; y >= 0; --y, from += (srcRowElements - srcNComps * w)) {
            QRgb* dstPixels = reinterpret_cast<QRgb*>(output.scanLine(y));
            for (int x = 0; x < w; ++x, from += srcNComps, ++dstPixels) {
                int r = 0, g = 0, b = 0, a = 255;
                switch (srcNComps) {
                case 1: r = g = b = *from >> 8; break;
                case 2: r = *from >> 8; g = *(from + 1) >> 8; break;
                case 3: r = *from >> 8; g = *(from + 1) >> 8; b = *(from + 2) >> 8; break;
                case 4: r = *from >> 8; g = *(from + 1) >> 8; b = *(from + 2) >> 8; a = *(from + 3) >> 8; break;
                default: break;
                }
                *dstPixels = qRgba(r, g, b, a);
            }
        }
    } else {
        if (message) { *message = QString::fromUtf8("unsupported image bit depth for live preview conversion (half-float is not supported)"); }
        return QImage();
    }

    return output;
}

FluxAiPanel::FluxAiPanel(Gui* gui, QWidget* parent)
    : QWidget(parent)
    , PanelWidget(this, gui)
    , _gui(gui)
    , _viewer(nullptr)
    , _sourceViewer(nullptr)
    , _sourceViewerNode()
    , _sourceReaderNode()
    , _sourceAIPaintNode()
    , _sourceLayerIndex(-1)
    , _sourceLayerName()
    , _sourceFilePath()
    , _sourceReaderLabel()
    , _sourceTimelineFrame(0)
    , _sourceSourceFrame(0)
    , _sourceRangeFirstFrame(0)
    , _sourceRangeLastFrame(0)
    , _sourceTimeOffset(0)
    , _updatingFrameRangeControls(false)
    , _taskCombo(nullptr)
    , _modelCombo(nullptr)
    , _sourceLabel(nullptr)
    , _frameRangeLabel(nullptr)
    , _frameRangeStartSpin(nullptr)
    , _frameRangeEndSpin(nullptr)
    , _videoMamaBatchLabel(nullptr)
    , _videoMamaBatchCombo(nullptr)
    , _videoMamaOverlapLabel(nullptr)
    , _videoMamaOverlapCombo(nullptr)
    , _outputLabel(nullptr)
    , _statusLabel(nullptr)
    , _progressBar(nullptr)
    , _aiLogPathLabel(nullptr)
    , _workflowGuideLabel(nullptr)
    , _runReasonLabel(nullptr)
    , _promptLabel(nullptr)
    , _runButton(nullptr)
    , _addMaskButton(nullptr)
    , _replaceMaskButton(nullptr)
    , _cancelButton(nullptr)
    , _resultHistoryList(nullptr)
    , _previewAgainButton(nullptr)
    , _removeHistoryEntryButton(nullptr)
    , _logToggleButton(nullptr)
    , _log(nullptr)
    , _worker(new FluxAiWorkerController(this))
    , _sam3Process(new QProcess(this))
    , _sam3WorkerProcess(new QProcess(this))
    , _sam3StdoutBuffer()
    , _sam3WorkerStdoutBuffer()
    , _sam3WorkerNextRequestId(1)
    , _sam3WorkerPendingCommands()
    , _sam3WorkerLoaded(false)
    , _sam3WorkerLoading(false)
    , _sam3WorkerUnloading(false)
    , _sam3LastError()
    , _sam3AbsoluteRoot()
    , _sam3RelativeRoot()
    , _sam3RunId()
    , _sam3Task()
    , _sam3SourcePng()
    , _sam3SourceMetadata()
    , _sam3Prompt()
    , _sam3Prompts()
    , _sam3RunPendingRequestId()
    , _sam3CancelRequested(false)
    , _sam3Exporting(false)
    , _hasSelectedSource(false)
    , _resultManifestHistoryProjectRelative()
    , _lastResultManifestProjectRelative()
    , _sourceFrameWidth(0)
    , _sourceFrameHeight(0)
    , _sourceFramePng()
    , _sourceFrameMetadata()
    , _sourceFrameMetadataFresh(false)
    , _livePreviewDebounceTimer(new QTimer(this))
    , _livePreviewGeneration(0)
    , _livePreviewPendingRequestId()
    , _livePreviewPendingGeneration(0)
    , _matAnyone2WorkerProcess(new QProcess(this))
    , _matAnyone2WorkerStdoutBuffer()
    , _matAnyone2WorkerNextRequestId(1)
    , _matAnyone2WorkerPendingCommands()
    , _matAnyone2RunPendingRequestId()
    , _matAnyone2CancelRequested(false)
    , _matAnyone2Running(false)
    , _matAnyone2Exporting(false)
    , _matAnyone2AbsoluteRoot()
    , _matAnyone2RelativeRoot()
    , _matAnyone2RunId()
    , _matAnyone2Task()
    , _matAnyone2SourceMetadata()
, _cachedSourceSequenceDir()
, _cachedSourceSequenceRangeStart(0)
, _cachedSourceSequenceRangeEnd(0)
, _cachedSourceSequenceMetadata()
 , _matAnyone2PendingInferRequest()
, _videoMamaWorkerProcess(new QProcess(this))
, _videoMamaWorkerStdoutBuffer()
, _videoMamaWorkerNextRequestId(1)
, _videoMamaWorkerPendingCommands()
, _videoMamaRunPendingRequestId()
, _videoMamaCancelRequested(false)
, _videoMamaRunning(false)
, _videoMamaExporting(false)
, _videoMamaAbsoluteRoot()
, _videoMamaRelativeRoot()
, _videoMamaRunId()
, _videoMamaTask()
, _videoMamaSourceMetadata()
, _videoMamaPendingInferRequest()
    , _lastRunReadinessSignature()
    , _lastVideoMamaUiSignature()
    , _lastPromptSummarySignature()
{
    _livePreviewDebounceTimer->setSingleShot(true);
    _livePreviewDebounceTimer->setInterval(250);
    QObject::connect(_livePreviewDebounceTimer, &QTimer::timeout, this, &FluxAiPanel::runAIPaintLivePreview);
    setupUi();
    loadModelManifest();
    QObject::connect(_modelCombo, SIGNAL(currentIndexChanged(int)), this, SLOT(onModelChanged()));
    QObject::connect(_worker, SIGNAL(progressText(QString)), this, SLOT(onWorkerProgress(QString)));
    QObject::connect(_worker, SIGNAL(finished(bool,QString)), this, SLOT(onWorkerFinished(bool,QString)));
    QObject::connect(_worker, SIGNAL(runningChanged(bool)), this, SLOT(onRunningChanged(bool)));
    QObject::connect(_sam3Process, &QProcess::readyReadStandardOutput, this, &FluxAiPanel::onSam3ReadyReadStandardOutput);
    QObject::connect(_sam3Process, &QProcess::readyReadStandardError, this, &FluxAiPanel::onSam3ReadyReadStandardError);
    QObject::connect(_sam3Process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, &FluxAiPanel::onSam3Finished);
    QObject::connect(_sam3Process, &QProcess::errorOccurred, this, &FluxAiPanel::onSam3ErrorOccurred);
    QObject::connect(_sam3WorkerProcess, &QProcess::readyReadStandardOutput, this, &FluxAiPanel::onSam3ReadyReadStandardOutput);
    QObject::connect(_sam3WorkerProcess, &QProcess::readyReadStandardError, this, &FluxAiPanel::onSam3ReadyReadStandardError);
    QObject::connect(_sam3WorkerProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, &FluxAiPanel::onSam3Finished);
    QObject::connect(_sam3WorkerProcess, &QProcess::errorOccurred, this, &FluxAiPanel::onSam3ErrorOccurred);
    QObject::connect(_matAnyone2WorkerProcess, &QProcess::readyReadStandardOutput, this, &FluxAiPanel::onMatAnyone2ReadyReadStandardOutput);
    QObject::connect(_matAnyone2WorkerProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, &FluxAiPanel::onMatAnyone2Finished);
    QObject::connect(_matAnyone2WorkerProcess, &QProcess::errorOccurred, this, &FluxAiPanel::onMatAnyone2ErrorOccurred);
    QObject::connect(_videoMamaWorkerProcess, &QProcess::readyReadStandardOutput, this, &FluxAiPanel::onVideoMamaReadyReadStandardOutput);
    QObject::connect(_videoMamaWorkerProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, &FluxAiPanel::onVideoMamaFinished);
    QObject::connect(_videoMamaWorkerProcess, &QProcess::errorOccurred, this, &FluxAiPanel::onVideoMamaErrorOccurred);
    writeAiLog(QString::fromUtf8("info"), QString::fromUtf8("panel"), QString::fromUtf8("panel_initialized"), QString::fromUtf8("pass"),
               QString::fromUtf8("Flux AI panel initialized; file logging is active."));
}

FluxAiPanel::~FluxAiPanel()
{
    if (_sam3WorkerProcess && _sam3WorkerProcess->state() != QProcess::NotRunning) {
        QJsonObject shutdown;
        shutdown.insert(QString::fromUtf8("command"), QString::fromUtf8("shutdown"));
        sendSam3WorkerRequest(shutdown);
        if (!_sam3WorkerProcess->waitForFinished(1000)) {
            _sam3WorkerProcess->kill();
            _sam3WorkerProcess->waitForFinished(1000);
        }
    }
    if (_sam3Process && _sam3Process->state() != QProcess::NotRunning) {
        _sam3Process->kill();
        _sam3Process->waitForFinished(1000);
    }
    if (_matAnyone2WorkerProcess && _matAnyone2WorkerProcess->state() != QProcess::NotRunning) {
        QJsonObject shutdown;
        shutdown.insert(QString::fromUtf8("command"), QString::fromUtf8("shutdown"));
        sendMatAnyone2WorkerRequest(shutdown);
        if (!_matAnyone2WorkerProcess->waitForFinished(1000)) {
            _matAnyone2WorkerProcess->kill();
            _matAnyone2WorkerProcess->waitForFinished(1000);
        }
    }
    if (_videoMamaWorkerProcess && _videoMamaWorkerProcess->state() != QProcess::NotRunning) {
        QJsonObject shutdown;
        shutdown.insert(QString::fromUtf8("command"), QString::fromUtf8("shutdown"));
        sendVideoMamaWorkerRequest(shutdown);
        if (!_videoMamaWorkerProcess->waitForFinished(1000)) {
            _videoMamaWorkerProcess->kill();
            _videoMamaWorkerProcess->waitForFinished(1000);
        }
    }
}

FluxAiPanelSerialization FluxAiPanel::serializeForProject() const
{
    FluxAiPanelSerialization serialization;
    serialization.selectedTask = _taskCombo ? _taskCombo->currentText().toStdString() : std::string();
    serialization.selectedModelId = _modelCombo ? _modelCombo->currentData().toString().toStdString() : std::string();
    serialization.hasPrompt = false;
    serialization.promptJson.clear();
    serialization.sourceLabel = _sourceLabel ? _sourceLabel->text().toStdString() : std::string();
    serialization.outputLabel = _outputLabel ? _outputLabel->text().toStdString() : std::string();
    serialization.statusLabel = _statusLabel ? _statusLabel->text().toStdString() : std::string();
    QString safeLastManifest;
    if (sanitizeProjectRelativePath(_lastResultManifestProjectRelative, &safeLastManifest)) {
        serialization.lastResultManifestProjectRelative = safeLastManifest.toStdString();
    }
    for (const QString& manifestPath : _resultManifestHistoryProjectRelative) {
        QString safeManifest;
        if (sanitizeProjectRelativePath(manifestPath, &safeManifest)) {
            serialization.resultManifestHistoryProjectRelative.push_back(safeManifest.toStdString());
        }
    }
    return serialization;
}

void FluxAiPanel::restoreFromProjectSerialization(const FluxAiPanelSerialization& serialization)
{
    if (_taskCombo && !serialization.selectedTask.empty()) {
        const int taskIndex = _taskCombo->findText(QString::fromStdString(serialization.selectedTask));
        if (taskIndex >= 0) {
            _taskCombo->setCurrentIndex(taskIndex);
        }
    }

    bool modelUnavailable = false;
    if (_modelCombo && !serialization.selectedModelId.empty()) {
        const QString modelId = QString::fromStdString(serialization.selectedModelId);
        const int modelIndex = _modelCombo->findData(modelId);
        if (modelIndex >= 0) {
            _modelCombo->setCurrentIndex(modelIndex);
        } else {
            modelUnavailable = true;
            const QString unavailable = tr("Stored AI model unavailable: %1").arg(modelId);
            _statusLabel->setText(QString::fromUtf8("Status: ") + unavailable);
            appendLog(unavailable);
        }
    }

    if (_sourceLabel && !serialization.sourceLabel.empty()) {
        _sourceLabel->setText(QString::fromStdString(serialization.sourceLabel));
    }
    if (_outputLabel && !serialization.outputLabel.empty()) {
        _outputLabel->setText(QString::fromStdString(serialization.outputLabel));
    }
    if (_statusLabel && !serialization.statusLabel.empty() && !modelUnavailable) {
        _statusLabel->setText(QString::fromStdString(serialization.statusLabel));
    }
    _resultManifestHistoryProjectRelative.clear();
    for (const std::string& manifestPath : serialization.resultManifestHistoryProjectRelative) {
        QString path;
        if (sanitizeProjectRelativePath(QString::fromStdString(manifestPath), &path) && !_resultManifestHistoryProjectRelative.contains(path)) {
            _resultManifestHistoryProjectRelative << path;
        }
    }
    QString lastManifest;
    if (sanitizeProjectRelativePath(QString::fromStdString(serialization.lastResultManifestProjectRelative), &lastManifest)) {
        _lastResultManifestProjectRelative = lastManifest;
    } else {
        _lastResultManifestProjectRelative.clear();
    }
    if (!_lastResultManifestProjectRelative.isEmpty() && !_resultManifestHistoryProjectRelative.contains(_lastResultManifestProjectRelative)) {
        _resultManifestHistoryProjectRelative.prepend(_lastResultManifestProjectRelative);
    } else if (_lastResultManifestProjectRelative.isEmpty() && !_resultManifestHistoryProjectRelative.isEmpty()) {
        _lastResultManifestProjectRelative = _resultManifestHistoryProjectRelative.first();
    }
    refreshResultHistoryList(_lastResultManifestProjectRelative);
    updatePromptSummary();
    updateUiState();
}

void FluxAiPanel::setViewerForCapture(ViewerGL* viewer)
{
    clearAIPaintLivePreviewMask(_sourceAIPaintNode);
    _viewer = viewer;
    _sourceViewer = nullptr;
    _sourceViewerNode.reset();
    _sourceReaderNode.reset();
    _sourceAIPaintNode.reset();
    _sourceLayerIndex = -1;
    _sourceLayerName.clear();
    _sourceFilePath.clear();
    _sourceReaderLabel.clear();
    _sourceTimelineFrame = 0;
    _sourceSourceFrame = 0;
    _sourceRangeFirstFrame = 0;
    _sourceRangeLastFrame = 0;
    _sourceTimeOffset = 0;
    _sourceFrameWidth = 0;
    _sourceFrameHeight = 0;
    _sourceFramePng.clear();
    _sourceFrameMetadata = QJsonObject();
    _sourceFrameMetadataFresh = false;
    _hasSelectedSource = false;
    resetFrameRangeControls();
    resetProgressBar();
    updatePromptSummary();
    updateUiState();
    QJsonObject found;
    found.insert(QString::fromUtf8("viewer_present"), static_cast<bool>(_viewer));
    found.insert(QString::fromUtf8("has_selected_source"), _hasSelectedSource);
    writeAiLog(QString::fromUtf8("info"), QString::fromUtf8("panel"), QString::fromUtf8("source_context_cleared"),
               QString::fromUtf8("pass"), QString::fromUtf8("AI source capture context cleared."), QString(), found);
}

void FluxAiPanel::setSourceCaptureContext(ViewerGL* viewer, const NodePtr& viewerNode, int layerIndex, const QString& layerName, const QString& filePath, const NodePtr& readerNode, const QString& readerLabel, const NodePtr& aiPaintNode, int timelineFrame, int sourceFrame, int sourceTimeOffset, int rangeFirstFrame, int rangeLastFrame)
{
    const bool sameRangeContext = _hasSelectedSource &&
                                  _sourceLayerIndex == layerIndex &&
                                  _sourceFilePath == filePath &&
                                  _sourceRangeFirstFrame == qMin(rangeFirstFrame, rangeLastFrame) &&
                                  _sourceRangeLastFrame == qMax(rangeFirstFrame, rangeLastFrame) &&
                                  _sourceTimeOffset == sourceTimeOffset;
    if (_viewer != viewer) {
        setViewerForCapture(viewer);
    } else if (_sourceAIPaintNode && _sourceAIPaintNode != aiPaintNode) {
        clearAIPaintLivePreviewMask(_sourceAIPaintNode);
    }
    _sourceViewer = viewer;
    _sourceViewerNode = viewerNode;
    _sourceReaderNode = readerNode;
    _sourceAIPaintNode = aiPaintNode;
    _sourceLayerIndex = layerIndex;
    _sourceLayerName = layerName;
    _sourceFilePath = filePath;
    _sourceReaderLabel = readerLabel;
    _sourceTimelineFrame = timelineFrame;
    _sourceSourceFrame = sourceFrame;
    _sourceTimeOffset = sourceTimeOffset;
    setSelectedSource(layerName, filePath, timelineFrame, sourceFrame);
    setFrameRangeControls(rangeFirstFrame, rangeLastFrame, sameRangeContext);
    setAIPaintLivePreviewEnabled(aipaintLivePreviewEnabled(_sourceAIPaintNode));
    QJsonObject found;
    found.insert(QString::fromUtf8("layer_index"), layerIndex);
    found.insert(QString::fromUtf8("layer_name"), layerName);
    found.insert(QString::fromUtf8("file_path"), filePath);
    found.insert(QString::fromUtf8("reader_label"), readerLabel);
    found.insert(QString::fromUtf8("has_reader_node"), static_cast<bool>(readerNode));
    found.insert(QString::fromUtf8("has_ai_paint_node"), static_cast<bool>(aiPaintNode));
    found.insert(QString::fromUtf8("timeline_frame"), timelineFrame);
    found.insert(QString::fromUtf8("source_frame"), sourceFrame);
    found.insert(QString::fromUtf8("range_first"), qMin(rangeFirstFrame, rangeLastFrame));
    found.insert(QString::fromUtf8("range_last"), qMax(rangeFirstFrame, rangeLastFrame));
    found.insert(QString::fromUtf8("time_offset"), sourceTimeOffset);
    writeAiLog(static_cast<bool>(aiPaintNode) ? QString::fromUtf8("info") : QString::fromUtf8("warn"),
               QString::fromUtf8("panel"), QString::fromUtf8("source_context_bound"),
               static_cast<bool>(aiPaintNode) ? QString::fromUtf8("pass") : QString::fromUtf8("block"),
               static_cast<bool>(aiPaintNode) ? QString::fromUtf8("AI Panel bound to selected layer AI Paint source.") : QString::fromUtf8("AI Panel source selected, but no AI Paint node is bound."),
               QString::fromUtf8("Selected layer with AI Paint should bind AI Panel to that AI Paint node."),
               found,
               static_cast<bool>(aiPaintNode) ? QString() : QString::fromUtf8("Add/select AI Paint on the selected layer before SAM3 prompt generation."));
    refreshAIPaintPromptState();
}

void FluxAiPanel::refreshAIPaintPromptState()
{
    updatePromptSummary();
    updateUiState();
    scheduleAIPaintLivePreview();
}

void FluxAiPanel::setSelectedSource(const QString& layerName, const QString& filePath, int timelineFrame, int sourceFrame)
{
    const QString displayName = layerName.isEmpty() ? QFileInfo(filePath).fileName() : layerName;
    _hasSelectedSource = !filePath.isEmpty();
    _sourceFrameWidth = 0;
    _sourceFrameHeight = 0;
    _sourceFramePng.clear();
    _sourceFrameMetadata = QJsonObject();
    _sourceFrameMetadataFresh = false;
    _lastResultManifestProjectRelative.clear();
    refreshResultHistoryList();
    _sourceLabel->setText(QString::fromUtf8("Source: %1\nFrame: timeline %2 / source %3\nFile: %4")
                          .arg(displayName)
                          .arg(timelineFrame)
                          .arg(sourceFrame)
                          .arg(QFileInfo(filePath).fileName()));
    _statusLabel->setText(QString::fromUtf8("Status: source selected"));
    resetProgressBar();
    updateUiState();
}

void FluxAiPanel::setupUi()
{
    QVBoxLayout* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(8, 8, 8, 8);
    outerLayout->setSpacing(6);

    QScrollArea* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setFrameShape(QFrame::NoFrame);
    outerLayout->addWidget(scrollArea);

    QWidget* content = new QWidget(scrollArea);
    QVBoxLayout* layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    QLabel* header = new QLabel(tr("AI"));
    header->setObjectName(QString::fromUtf8("fluxPanelHeader"));
    layout->addWidget(header);

    _taskCombo = new QComboBox();
    _taskCombo->addItem(QString::fromUtf8("Base Matte"));
    _taskCombo->addItem(QString::fromUtf8("Refine Matte"));
    _taskCombo->addItem(QString::fromUtf8("Segment / Track"));
    _taskCombo->addItem(QString::fromUtf8("Depth"));
    layout->addWidget(new QLabel(tr("Task")));
    layout->addWidget(_taskCombo);

    _modelCombo = new QComboBox();
    layout->addWidget(new QLabel(tr("Model")));
    layout->addWidget(_modelCombo);

    _workflowGuideLabel = new QLabel(QString::fromUtf8("Workflow: select a model to see required steps."));
    _workflowGuideLabel->setWordWrap(true);
    _workflowGuideLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(_workflowGuideLabel);

    _runReasonLabel = new QLabel(QString::fromUtf8("Run unavailable because: no source selected."));
    _runReasonLabel->setWordWrap(true);
    _runReasonLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(_runReasonLabel);

    _sourceLabel = new QLabel(QString::fromUtf8("Source: none selected"));
    _sourceLabel->setWordWrap(true);
    layout->addWidget(_sourceLabel);

    _frameRangeLabel = new QLabel(QString::fromUtf8("Frame Range: no source selected"));
    _frameRangeLabel->setWordWrap(true);
    layout->addWidget(_frameRangeLabel);
    QHBoxLayout* frameRangeLayout = new QHBoxLayout();
    frameRangeLayout->setSpacing(6);
    frameRangeLayout->addWidget(new QLabel(tr("Start")));
    _frameRangeStartSpin = new QSpinBox();
    _frameRangeStartSpin->setRange(0, 0);
    _frameRangeStartSpin->setEnabled(false);
    frameRangeLayout->addWidget(_frameRangeStartSpin);
    frameRangeLayout->addWidget(new QLabel(tr("End")));
    _frameRangeEndSpin = new QSpinBox();
    _frameRangeEndSpin->setRange(0, 0);
    _frameRangeEndSpin->setEnabled(false);
    frameRangeLayout->addWidget(_frameRangeEndSpin);
    layout->addLayout(frameRangeLayout);
    QObject::connect(_frameRangeStartSpin, SIGNAL(valueChanged(int)), this, SLOT(onFrameRangeEdited()));
    QObject::connect(_frameRangeEndSpin, SIGNAL(valueChanged(int)), this, SLOT(onFrameRangeEdited()));

    QHBoxLayout* videoMamaOptionsLayout = new QHBoxLayout();
    videoMamaOptionsLayout->setSpacing(6);
    _videoMamaBatchLabel = new QLabel(tr("Batch frames"));
    _videoMamaBatchCombo = new QComboBox();
    _videoMamaBatchCombo->addItem(QString::fromUtf8("16"), 16);
    _videoMamaBatchCombo->addItem(QString::fromUtf8("32"), 32);
    _videoMamaBatchCombo->addItem(QString::fromUtf8("64"), 64);
    _videoMamaBatchCombo->addItem(QString::fromUtf8("128"), 128);
    _videoMamaOverlapLabel = new QLabel(tr("Blend overlap"));
    _videoMamaOverlapCombo = new QComboBox();
    _videoMamaOverlapCombo->addItem(QString::fromUtf8("0"), 0);
    _videoMamaOverlapCombo->addItem(QString::fromUtf8("2"), 2);
    _videoMamaOverlapCombo->addItem(QString::fromUtf8("4"), 4);
    _videoMamaOverlapCombo->setCurrentIndex(_videoMamaOverlapCombo->findData(2));
    videoMamaOptionsLayout->addWidget(_videoMamaBatchLabel);
    videoMamaOptionsLayout->addWidget(_videoMamaBatchCombo);
    videoMamaOptionsLayout->addWidget(_videoMamaOverlapLabel);
    videoMamaOptionsLayout->addWidget(_videoMamaOverlapCombo);
    layout->addLayout(videoMamaOptionsLayout);

    _outputLabel = new QLabel(QString::fromUtf8("Output: project must be saved before AI generation"));
    _outputLabel->setWordWrap(true);
    layout->addWidget(_outputLabel);

    _statusLabel = new QLabel(QString::fromUtf8("Status: idle"));
    _statusLabel->setWordWrap(true);
    layout->addWidget(_statusLabel);

    _progressBar = new QProgressBar();
    _progressBar->setRange(0, 100);
    _progressBar->setValue(0);
    _progressBar->setTextVisible(true);
    _progressBar->setFormat(QString::fromUtf8("Idle"));
    layout->addWidget(_progressBar);

    _aiLogPathLabel = new QLabel();
    _aiLogPathLabel->setWordWrap(true);
    layout->addWidget(_aiLogPathLabel);
    updateAiLogPathLabel();

    _promptLabel = new QLabel(QString::fromUtf8("Prompt summary: add AI Paint point/box prompts in the viewer toolbar"));
    _promptLabel->setWordWrap(true);
    layout->addWidget(_promptLabel);

    QHBoxLayout* buttons = new QHBoxLayout();
    _runButton = new QPushButton(tr("Run"));
    _addMaskButton = new QPushButton(tr("Add Mask"));
    _replaceMaskButton = new QPushButton(tr("Replace Mask"));
    _cancelButton = new QPushButton(tr("Cancel"));
    buttons->addWidget(_runButton);
    buttons->addWidget(_addMaskButton);
    buttons->addWidget(_replaceMaskButton);
    buttons->addWidget(_cancelButton);
    layout->addLayout(buttons);
    QObject::connect(_runButton, SIGNAL(clicked(bool)), this, SLOT(onRunClicked()));
    QObject::connect(_addMaskButton, SIGNAL(clicked(bool)), this, SLOT(onAddMaskClicked()));
    QObject::connect(_replaceMaskButton, SIGNAL(clicked(bool)), this, SLOT(onReplaceMaskClicked()));
    QObject::connect(_cancelButton, SIGNAL(clicked(bool)), this, SLOT(cancelSam3()));

    layout->addWidget(new QLabel(tr("Result History")));
    _resultHistoryList = new QListWidget();
    _resultHistoryList->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(_resultHistoryList);
    QHBoxLayout* historyButtons = new QHBoxLayout();
    _previewAgainButton = new QPushButton(tr("Preview Again"));
    _removeHistoryEntryButton = new QPushButton(tr("Remove Entry"));
    historyButtons->addWidget(_previewAgainButton);
    historyButtons->addWidget(_removeHistoryEntryButton);
    layout->addLayout(historyButtons);
    QObject::connect(_resultHistoryList, SIGNAL(itemSelectionChanged()), this, SLOT(onResultHistorySelectionChanged()));
    QObject::connect(_previewAgainButton, SIGNAL(clicked(bool)), this, SLOT(onPreviewAgainClicked()));
    QObject::connect(_removeHistoryEntryButton, SIGNAL(clicked(bool)), this, SLOT(onRemoveHistoryEntryClicked()));

    _logToggleButton = new QPushButton(tr("Show Log"));
    layout->addWidget(_logToggleButton);
    QObject::connect(_logToggleButton, SIGNAL(clicked(bool)), this, SLOT(toggleLog()));

    _log = new QPlainTextEdit();
    _log->setReadOnly(true);
    _log->setVisible(false);
    layout->addWidget(_log, 1);

    scrollArea->setWidget(content);
}

QString FluxAiPanel::repoRoot() const
{
    const QString rel = QString::fromUtf8("tools/ai/flux_provider_runtime.py");
    QStringList starts;
    starts << QDir::currentPath() << QCoreApplication::applicationDirPath();
    for (const QString& start : starts) {
        QDir dir(start);
        for (int i = 0; i < 8; ++i) {
            if (QFile::exists(dir.filePath(rel))) {
                return dir.absolutePath();
            }
            if (!dir.cdUp()) {
                break;
            }
        }
    }
    return QDir::currentPath();
}

QString FluxAiPanel::manifestPath() const
{
    return QDir(repoRoot()).filePath(QString::fromUtf8("tools/ai/model_manifest.json"));
}

QString FluxAiPanel::warningSuffix(const QString& policy)
{
    const QString p = policy.toLower();
    QStringList labels;
    if (p.contains(QString::fromUtf8("gated"))) {
        labels << QString::fromUtf8("GATED");
    }
    if (p.contains(QString::fromUtf8("noncommercial")) || p.contains(QString::fromUtf8("non-commercial")) || p.contains(QString::fromUtf8("restrictive"))) {
        labels << QString::fromUtf8("WARNING");
    }
    if (p.contains(QString::fromUtf8("external_helper"))) {
        labels << QString::fromUtf8("EXTERNAL HELPER");
    }
    return labels.isEmpty() ? QString() : QString::fromUtf8(" [") + labels.join(QString::fromUtf8(" | ")) + QString::fromUtf8("]");
}

void FluxAiPanel::loadModelManifest()
{
    _modelCombo->clear();
    const QString path = manifestPath();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        _modelCombo->addItem(QString::fromUtf8("Model manifest unavailable"), QString());
        _modelCombo->setEnabled(false);
        updateUiState();
        appendLog(tr("Model manifest unavailable: %1 (%2)").arg(path, file.errorString()));
        return;
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        _modelCombo->addItem(QString::fromUtf8("Model manifest unavailable"), QString());
        _modelCombo->setEnabled(false);
        updateUiState();
        appendLog(tr("Model manifest unavailable: %1 (%2)").arg(path, parseError.errorString()));
        return;
    }

    const QJsonArray models = doc.object().value(QString::fromUtf8("models")).toArray();
    for (const QJsonValue& value : models) {
        const QJsonObject model = value.toObject();
        const QString id = model.value(QString::fromUtf8("id")).toString();
        const QString display = model.value(QString::fromUtf8("display_name")).toString(id);
        const QString policy = model.value(QString::fromUtf8("install_policy")).toString();
        _modelCombo->addItem(display + QString::fromUtf8(" — ") + policy + warningSuffix(policy), id);
    }
    if (_modelCombo->count() == 0) {
        _modelCombo->addItem(QString::fromUtf8("Model manifest unavailable"), QString());
        _modelCombo->setEnabled(false);
        updateUiState();
        appendLog(tr("Model manifest unavailable: %1 (no models found)").arg(path));
    } else {
        _modelCombo->setEnabled(true);
        updateUiState();
    }
}

QString FluxAiPanel::taskSlug(const QString& task)
{
    QString slug;
    bool previousDash = false;
    for (const QChar ch : task.toLower()) {
        const ushort u = ch.unicode();
        const bool asciiAlphaNum = (u >= 'a' && u <= 'z') || (u >= '0' && u <= '9');
        if (asciiAlphaNum) {
            slug.append(ch);
            previousDash = false;
        } else if (!previousDash && !slug.isEmpty()) {
            slug.append(QLatin1Char('-'));
            previousDash = true;
        }
    }
    while (slug.endsWith(QLatin1Char('-'))) {
        slug.chop(1);
    }
    return slug;
}

bool FluxAiPanel::hasSelectedSource() const
{
    return _hasSelectedSource;
}

static void setAIPaintSam3StatusKnob(const NodePtr& aiPaintNode, const QString& status)
{
    if (!aiPaintNode) {
        return;
    }
    KnobIPtr knob = aiPaintNode->getKnobByName("aiPaintSam3Status");
    KnobString* statusKnob = dynamic_cast<KnobString*>(knob.get());
    if (statusKnob) {
        statusKnob->setValue(status.toStdString(), ViewSpec::all(), 0, true);
    }
}

static bool aipaintLivePreviewEnabled(const NodePtr& aiPaintNode)
{
    if (!aiPaintNode) {
        return false;
    }
    KnobIPtr knob = aiPaintNode->getKnobByName("aiPaintLivePreview");
    KnobButton* liveKnob = dynamic_cast<KnobButton*>(knob.get());
    return liveKnob && liveKnob->getValue();
}

static AIPaint* aipaintEffectFromNode(const NodePtr& aiPaintNode)
{
    if (!aiPaintNode || !aiPaintNode->isActivated()) {
        return nullptr;
    }
    EffectInstancePtr effect = aiPaintNode->getEffectInstance();
    if (!effect || effect->getPluginID() != PLUGINID_NATRON_AIPAINT) {
        return nullptr;
    }
    return dynamic_cast<AIPaint*>(effect.get());
}

static void clearAIPaintLivePreviewMask(const NodePtr& aiPaintNode)
{
    AIPaint* aiPaint = aipaintEffectFromNode(aiPaintNode);
    if (aiPaint) {
        aiPaint->clearLivePreviewMask();
    }
}

void FluxAiPanel::requestAIPaintSam3Load()
{
    QString message;
    _sam3WorkerLoading = true;
    _sam3LastError.clear();
    setAIPaintSam3StatusKnob(_sourceAIPaintNode, QString::fromUtf8("loading"));
    if (_statusLabel) {
        _statusLabel->setText(QString::fromUtf8("Status: SAM3 loading"));
    }
    if (!ensureSam3WorkerStarted(&message)) {
        _sam3WorkerLoading = false;
        _sam3WorkerLoaded = false;
        _sam3LastError = message;
        setAIPaintSam3StatusKnob(_sourceAIPaintNode, QString::fromUtf8("error: ") + message);
        appendLog(message);
        updateUiState();
        return;
    }
    QJsonObject request;
    request.insert(QString::fromUtf8("command"), QString::fromUtf8("load"));
    const QString id = sendSam3WorkerRequest(request);
    if (id.isEmpty()) {
        _sam3WorkerLoading = false;
        _sam3WorkerLoaded = false;
        _sam3LastError = QString::fromUtf8("SAM3 worker request failed");
        setAIPaintSam3StatusKnob(_sourceAIPaintNode, QString::fromUtf8("error: SAM3 worker request failed"));
    }
    updateUiState();
}

void FluxAiPanel::requestAIPaintSam3Unload()
{
    clearAIPaintLivePreviewMask(_sourceAIPaintNode);
    _sam3WorkerUnloading = true;
    _sam3LastError.clear();
    setAIPaintSam3StatusKnob(_sourceAIPaintNode, QString::fromUtf8("unloading"));
    if (_statusLabel) {
        _statusLabel->setText(QString::fromUtf8("Status: SAM3 unloading"));
    }
    QString message;
    if (!ensureSam3WorkerStarted(&message)) {
        _sam3WorkerUnloading = false;
        _sam3WorkerLoaded = false;
        setAIPaintSam3StatusKnob(_sourceAIPaintNode, QString::fromUtf8("unloaded"));
        updateUiState();
        return;
    }
    QJsonObject request;
    request.insert(QString::fromUtf8("command"), QString::fromUtf8("unload"));
    const QString id = sendSam3WorkerRequest(request);
    if (id.isEmpty()) {
        _sam3WorkerUnloading = false;
        _sam3LastError = QString::fromUtf8("SAM3 worker unload request failed");
        setAIPaintSam3StatusKnob(_sourceAIPaintNode, QString::fromUtf8("error: SAM3 worker unload request failed"));
    }
    updateUiState();
}

void FluxAiPanel::setAIPaintLivePreviewEnabled(bool enabled)
{
    if (!enabled) {
        clearAIPaintLivePreviewMask(_sourceAIPaintNode);
    }
    const QString status = enabled
        ? (_sam3WorkerLoaded ? QString::fromUtf8("live preview enabled") : QString::fromUtf8("live preview enabled; SAM3 unloaded - click Load SAM3"))
        : (_sam3WorkerLoaded ? QString::fromUtf8("loaded") : QString::fromUtf8("unloaded"));
    setAIPaintSam3StatusKnob(_sourceAIPaintNode, status);
    if (_statusLabel) {
        _statusLabel->setText(QString::fromUtf8("Status: ") + status);
    }
    updateUiState();
}

void FluxAiPanel::addOrPromoteResultManifest(const QString& projectRelativeManifest)
{
    QString path;
    if (!sanitizeProjectRelativePath(projectRelativeManifest, &path)) {
        appendLog(tr("AI result manifest rejected: unsafe project-relative path: %1").arg(projectRelativeManifest));
        return;
    }
    _resultManifestHistoryProjectRelative.removeAll(path);
    _resultManifestHistoryProjectRelative.prepend(path);
    while (_resultManifestHistoryProjectRelative.size() > 50) {
        _resultManifestHistoryProjectRelative.removeLast();
    }
    _lastResultManifestProjectRelative = path;
    refreshResultHistoryList(path);
    updateUiState();
}

void FluxAiPanel::refreshResultHistoryList(const QString& selectedManifest)
{
    if (!_resultHistoryList) {
        return;
    }
    const QString selected = selectedManifest.isEmpty() ? selectedResultManifestProjectRelative() : selectedManifest;
    _resultHistoryList->blockSignals(true);
    _resultHistoryList->clear();
    int selectedRow = -1;
    for (const QString& manifestPath : _resultManifestHistoryProjectRelative) {
        const QString label = resultHistoryDisplayLabel(manifestPath);
        QListWidgetItem* item = new QListWidgetItem(label, _resultHistoryList);
        item->setData(Qt::UserRole, manifestPath);
        item->setToolTip(tr("%1\nManifest: %2").arg(label, manifestPath));
        if (manifestPath == selected) {
            selectedRow = _resultHistoryList->count() - 1;
        }
    }
    if (selectedRow < 0 && _resultHistoryList->count() > 0) {
        selectedRow = 0;
    }
    if (selectedRow >= 0) {
        _resultHistoryList->setCurrentRow(selectedRow);
    }
    _resultHistoryList->blockSignals(false);
    _lastResultManifestProjectRelative = selectedRow >= 0 ? selectedResultManifestProjectRelative() : QString();
}

QString FluxAiPanel::resultHistoryDisplayLabel(const QString& projectRelativeManifest) const
{
    QString safeManifest;
    if (!sanitizeProjectRelativePath(projectRelativeManifest, &safeManifest)) {
        return projectRelativeManifest;
    }
    if (!_gui || !_gui->getApp() || !_gui->getApp()->getProject()) {
        return safeManifest;
    }
    QFile file(QDir(_gui->getApp()->getProject()->getProjectPath()).filePath(safeManifest));
    if (!file.open(QIODevice::ReadOnly)) {
        return projectRelativeManifest;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return projectRelativeManifest;
    }
    const QJsonObject obj = doc.object();
    QStringList parts;
    const QString runId = obj.value(QString::fromUtf8("run_id")).toString();
    if (!runId.isEmpty()) { parts << runId; }
    const QString created = obj.value(QString::fromUtf8("created_at_utc")).toString(obj.value(QString::fromUtf8("time")).toString());
    if (!created.isEmpty()) { parts << created; }
    const QJsonObject source = obj.value(QString::fromUtf8("source_metadata")).toObject();
    if (source.contains(QString::fromUtf8("source_frame"))) { parts << tr("source %1").arg(source.value(QString::fromUtf8("source_frame")).toInt()); }
    const QString model = obj.value(QString::fromUtf8("model_id")).toString();
    if (!model.isEmpty()) { parts << model; }
    const QJsonArray prompts = obj.value(QString::fromUtf8("prompts")).toArray();
    if (!prompts.isEmpty()) { parts << tr("%1 prompt(s)").arg(prompts.size()); }
    const QString mask = obj.value(QString::fromUtf8("selected_mask_path_project_relative")).toString();
    if (!mask.isEmpty()) { parts << QFileInfo(mask).fileName(); }
    return parts.isEmpty() ? safeManifest : parts.join(QString::fromUtf8(" | "));
}

QString FluxAiPanel::selectedResultManifestProjectRelative() const
{
    if (!_resultHistoryList || !_resultHistoryList->currentItem()) {
        return QString();
    }
    return _resultHistoryList->currentItem()->data(Qt::UserRole).toString();
}

static int resultManifestGenerationTimeOffset(const QJsonObject& manifest, int fallbackTimeOffset)
{
    const QJsonObject source = manifest.value(QString::fromUtf8("source_metadata")).toObject();
    const char* frameRangeKeys[] = {
        "sam3_frame_range",
        "matanyone2_frame_range",
        "videomama_frame_range"
    };
    for (const char* key : frameRangeKeys) {
        const QJsonObject range = source.value(QString::fromUtf8(key)).toObject();
        if (range.contains(QString::fromUtf8("time_offset"))) {
            return range.value(QString::fromUtf8("time_offset")).toInt(fallbackTimeOffset);
        }
    }
    if (source.contains(QString::fromUtf8("time_offset"))) {
        return source.value(QString::fromUtf8("time_offset")).toInt(fallbackTimeOffset);
    }
    return fallbackTimeOffset;
}

static bool inferSequencePatternFrameRange(const QString& projectPath,
                                           const QString& projectRelativePattern,
                                           int* firstFrame,
                                           int* lastFrame)
{
    if (projectPath.isEmpty() || projectRelativePattern.isEmpty() || !projectRelativePattern.contains(QLatin1Char('#'))) {
        return false;
    }

    const QString absolutePattern = QDir(projectPath).filePath(projectRelativePattern);
    const QFileInfo patternInfo(absolutePattern);
    const QString filePattern = patternInfo.fileName();
    const QRegularExpression hashRe(QLatin1String("#{2,}"));
    const QRegularExpressionMatch match = hashRe.match(filePattern);
    if (!match.hasMatch()) {
        return false;
    }

    const QString prefix = filePattern.left(match.capturedStart());
    const QString suffix = filePattern.mid(match.capturedEnd());
    const int digits = match.capturedLength();
    const QStringList entries = QDir(patternInfo.absolutePath()).entryList(QDir::Files | QDir::Readable, QDir::Name);
    int minFrame = INT_MAX;
    int maxFrame = INT_MIN;
    for (const QString& entry : entries) {
        if (!entry.startsWith(prefix) || !entry.endsWith(suffix)) {
            continue;
        }
        const int numberStart = prefix.size();
        const int numberLength = entry.size() - prefix.size() - suffix.size();
        if (numberLength != digits) {
            continue;
        }
        const QString numberText = entry.mid(numberStart, numberLength);
        bool ok = false;
        const int frame = numberText.toInt(&ok);
        if (!ok) {
            continue;
        }
        minFrame = qMin(minFrame, frame);
        maxFrame = qMax(maxFrame, frame);
    }
    if (minFrame == INT_MAX || maxFrame == INT_MIN) {
        return false;
    }
    if (firstFrame) { *firstFrame = minFrame; }
    if (lastFrame) { *lastFrame = maxFrame; }
    return true;
}

static QJsonArray renumberSequenceFrames(const QJsonArray& frames,
                                         int firstFrame)
{
    QJsonArray renumbered;
    int frame = firstFrame;
    for (const QJsonValue& value : frames) {
        QJsonObject obj = value.toObject();
        obj.insert(QString::fromUtf8("source_frame"), frame);
        obj.insert(QString::fromUtf8("timeline_frame"), frame);
        renumbered.append(obj);
        ++frame;
    }
    return renumbered;
}

int FluxAiPanel::selectedResultGenerationTimeOffset(int fallbackTimeOffset) const
{
    const QString manifestPath = selectedResultManifestProjectRelative();
    if (manifestPath.isEmpty() || !_gui || !_gui->getApp() || !_gui->getApp()->getProject()) {
        return fallbackTimeOffset;
    }
    QString safeManifest;
    if (!sanitizeProjectRelativePath(manifestPath, &safeManifest)) {
        return fallbackTimeOffset;
    }
    QFile file(QDir(_gui->getApp()->getProject()->getProjectPath()).filePath(safeManifest));
    if (!file.open(QIODevice::ReadOnly)) {
        return fallbackTimeOffset;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return fallbackTimeOffset;
    }
    return resultManifestGenerationTimeOffset(doc.object(), fallbackTimeOffset);
}

bool FluxAiPanel::selectedResultGenerationRange(int* firstFrame, int* lastFrame, int* timeOffset) const
{
    const QString manifestPath = selectedResultManifestProjectRelative();
    if (manifestPath.isEmpty() || !_gui || !_gui->getApp() || !_gui->getApp()->getProject()) {
        return false;
    }
    QString safeManifest;
    if (!sanitizeProjectRelativePath(manifestPath, &safeManifest)) {
        return false;
    }
    QFile file(QDir(_gui->getApp()->getProject()->getProjectPath()).filePath(safeManifest));
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return false;
    }

    const QJsonObject source = doc.object().value(QString::fromUtf8("source_metadata")).toObject();
    int start = INT_MIN;
    int end = INT_MIN;
    const QString projectPath = _gui->getApp()->getProject()->getProjectPath();
    QString maskPattern;
    if (sanitizeProjectRelativePath(doc.object().value(QString::fromUtf8("selected_mask_sequence_pattern_project_relative")).toString(), &maskPattern) &&
        inferSequencePatternFrameRange(projectPath, maskPattern, &start, &end)) {
        if (firstFrame) { *firstFrame = qMin(start, end); }
        if (lastFrame) { *lastFrame = qMax(start, end); }
        if (timeOffset) { *timeOffset = resultManifestGenerationTimeOffset(doc.object(), _sourceTimeOffset); }
        return true;
    }

    const QJsonObject sam3Sequence = source.value(QString::fromUtf8("sam3_source_sequence")).toObject();
    start = sam3Sequence.value(QString::fromUtf8("source_frame_start")).toInt(INT_MIN);
    end = sam3Sequence.value(QString::fromUtf8("source_frame_end")).toInt(INT_MIN);
    if (start == INT_MIN || end == INT_MIN) {
        const QJsonObject frameRange = source.value(QString::fromUtf8("sam3_frame_range")).toObject();
        start = frameRange.value(QString::fromUtf8("source_start")).toInt(INT_MIN);
        end = frameRange.value(QString::fromUtf8("source_end")).toInt(INT_MIN);
    }
    if (start == INT_MIN || end == INT_MIN) {
        return false;
    }
    if (firstFrame) { *firstFrame = qMin(start, end); }
    if (lastFrame) { *lastFrame = qMax(start, end); }
    if (timeOffset) { *timeOffset = resultManifestGenerationTimeOffset(doc.object(), _sourceTimeOffset); }
    return true;
}

bool FluxAiPanel::selectedResultMaskProjectRelative(QString* relativeMask, QString* message, QString* relativeSequencePattern) const
{
    const QString manifestPath = selectedResultManifestProjectRelative();
    if (manifestPath.isEmpty()) {
        if (message) { *message = QString::fromUtf8("No AI result history item is selected."); }
        return false;
    }
    if (!_gui || !_gui->getApp() || !_gui->getApp()->getProject()) {
        if (message) { *message = QString::fromUtf8("AI result preview unavailable: project is missing."); }
        return false;
    }
    QString safeManifest;
    if (!sanitizeProjectRelativePath(manifestPath, &safeManifest)) {
        if (message) { *message = tr("AI result manifest has unsafe project-relative path: %1").arg(manifestPath); }
        return false;
    }
    QFile file(QDir(_gui->getApp()->getProject()->getProjectPath()).filePath(safeManifest));
    if (!file.open(QIODevice::ReadOnly)) {
        if (message) { *message = tr("AI result manifest unavailable: %1").arg(manifestPath); }
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (message) { *message = tr("AI result manifest could not be read: %1").arg(parseError.errorString()); }
        return false;
    }
    QString mask;
    if (!sanitizeProjectRelativePath(doc.object().value(QString::fromUtf8("selected_mask_path_project_relative")).toString(), &mask)) {
        if (message) { *message = QString::fromUtf8("AI result manifest has no safe selected mask path."); }
        return false;
    }
    QFileInfo maskInfo(QDir(_gui->getApp()->getProject()->getProjectPath()).filePath(mask));
    if (!maskInfo.isFile() || maskInfo.size() <= 0) {
        if (message) { *message = tr("AI result selected mask is missing or empty: %1").arg(mask); }
        return false;
    }
    if (relativeMask) { *relativeMask = mask; }

    if (relativeSequencePattern) {
        relativeSequencePattern->clear();
        const QString seqRaw = doc.object().value(QString::fromUtf8("selected_mask_sequence_pattern_project_relative")).toString();
        if (!seqRaw.isEmpty()) {
            QString seqSafe;
            if (sanitizeProjectRelativePath(seqRaw, &seqSafe) && !seqSafe.isEmpty()) {
                // Do not validate with QFileInfo::isFile(); sequence patterns like mask_######.png are not individual files.
                if (seqSafe.contains(QLatin1Char('#'))) {
                    *relativeSequencePattern = seqSafe;
                }
            }
        }
    }

    return true;
}

void FluxAiPanel::updateUiState()
{
    const bool sam3Running = _sam3Process && _sam3Process->state() != QProcess::NotRunning;
    const bool sam3ControllerBusy = _sam3WorkerLoading || _sam3WorkerUnloading;
    bool sam3PersistentInferencePending = !_sam3RunPendingRequestId.isEmpty();
    for (QMap<QString, QString>::const_iterator it = _sam3WorkerPendingCommands.constBegin(); it != _sam3WorkerPendingCommands.constEnd(); ++it) {
        if ((it.value() == QString::fromUtf8("infer_still") || it.value() == QString::fromUtf8("infer_video")) && it.key() != _livePreviewPendingRequestId) {
            sam3PersistentInferencePending = true;
            break;
        }
    }
    const bool matAnyone2Running = _matAnyone2Running;
    bool matAnyone2InferencePending = !_matAnyone2RunPendingRequestId.isEmpty();
    for (QMap<QString, QString>::const_iterator it = _matAnyone2WorkerPendingCommands.constBegin(); it != _matAnyone2WorkerPendingCommands.constEnd(); ++it) {
        if (it.value() == QString::fromUtf8("infer_video")) {
            matAnyone2InferencePending = true;
            break;
        }
    }
    const bool videoMamaRunning = _videoMamaRunning;
    bool videoMamaInferencePending = !_videoMamaRunPendingRequestId.isEmpty();
    for (QMap<QString, QString>::const_iterator it = _videoMamaWorkerPendingCommands.constBegin(); it != _videoMamaWorkerPendingCommands.constEnd(); ++it) {
        if (it.value() == QString::fromUtf8("infer_video")) {
            videoMamaInferencePending = true;
            break;
        }
    }
    const bool running = (_worker && _worker->isRunning()) || sam3ControllerBusy || sam3PersistentInferencePending || _sam3Exporting || matAnyone2InferencePending || _matAnyone2Exporting || videoMamaInferencePending || _videoMamaExporting;
    const bool projectSaved = _gui && _gui->getApp() && _gui->getApp()->getProject() && _gui->getApp()->getProject()->hasProjectBeenSavedByUser();
    if (_runButton) {
        const QString modelId = _modelCombo ? _modelCombo->currentData().toString() : QString();
        bool matAnyone2Ready = false;
        if (modelId == QString::fromUtf8("matanyone2") && !running && !sam3Running && !matAnyone2Running && !videoMamaRunning && hasSelectedSource()) {
            int rs = 0, re = 0;
            QString rangeMsg;
            const bool hasVideoRange = selectedFrameRange(&rs, &re, &rangeMsg) && (re > rs);
            // AI Paint live preview counts as having a base mask
            AIPaint* aiPaint2 = aipaintEffectFromNode(_sourceAIPaintNode);
            const bool hasAIPaintMask = livePreviewMaskMatchesCurrentPromptFrame(aiPaint2) && !aiPaint2->livePreviewMaskPath().isEmpty() && QFileInfo(aiPaint2->livePreviewMaskPath()).isFile();
            QString historyMask, historyMsg;
            const bool hasHistoryMask = selectedResultMaskProjectRelative(&historyMask, &historyMsg);
            matAnyone2Ready = hasVideoRange && (hasAIPaintMask || hasHistoryMask);
        }
        bool videoMamaReady = false;
        if (modelId == QString::fromUtf8("videomama") && !running && !sam3Running && !matAnyone2Running && !videoMamaRunning && hasSelectedSource()) {
            int rs = 0, re = 0;
            QString rangeMsg;
            const bool hasVideoRange = selectedFrameRange(&rs, &re, &rangeMsg) && (re > rs);
            QString historyMask, historyMsg, historyMaskPattern;
            const bool hasSam3MaskSequence = selectedResultMaskProjectRelative(&historyMask, &historyMsg, &historyMaskPattern) && !historyMaskPattern.isEmpty();
            bool hasSam3SourceSequence = false;
            QString safeManifest;
            if (sanitizeProjectRelativePath(selectedResultManifestProjectRelative(), &safeManifest) && _gui && _gui->getApp() && _gui->getApp()->getProject()) {
                QFile mf(QDir(_gui->getApp()->getProject()->getProjectPath()).filePath(safeManifest));
                if (mf.open(QIODevice::ReadOnly)) {
                    QJsonParseError parseError;
                    const QJsonDocument doc = QJsonDocument::fromJson(mf.readAll(), &parseError);
                    if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
                        const QJsonObject obj = doc.object();
                        hasSam3SourceSequence = obj.value(QString::fromUtf8("model_id")).toString() == QString::fromUtf8("sam3_transformers") &&
                                                !obj.value(QString::fromUtf8("source_metadata")).toObject().value(QString::fromUtf8("sam3_source_sequence")).toObject().isEmpty();
                    }
                }
            }
            videoMamaReady = hasVideoRange && hasSam3MaskSequence && hasSam3SourceSequence;
        }
        const bool sam3Ready = !running && !sam3Running && !matAnyone2Running && !videoMamaRunning && hasSelectedSource() && modelId == QString::fromUtf8("sam3_transformers") && hasRunnableAIPaintPrompt();
        const bool canRun = projectSaved && (sam3Ready || matAnyone2Ready || videoMamaReady);
        const QString runReason = canRun ? QString::fromUtf8("ready.") : runUnavailableReason(modelId, running, sam3Running, matAnyone2Running, videoMamaRunning, projectSaved);
        updateWorkflowGuide(modelId, runReason);
        _runButton->setEnabled(canRun);
        const QString signature = runReadinessSignature(modelId, running, sam3Running, matAnyone2Running, videoMamaRunning, projectSaved, canRun);
        if (signature != _lastRunReadinessSignature) {
            QJsonObject found;
            found.insert(QString::fromUtf8("model_id"), modelId);
            found.insert(QString::fromUtf8("project_saved"), projectSaved);
            found.insert(QString::fromUtf8("has_selected_source"), hasSelectedSource());
            found.insert(QString::fromUtf8("has_source_ai_paint_node"), static_cast<bool>(_sourceAIPaintNode));
            found.insert(QString::fromUtf8("source_layer_index"), _sourceLayerIndex);
            found.insert(QString::fromUtf8("source_layer_name"), _sourceLayerName);
            found.insert(QString::fromUtf8("timeline_frame"), _sourceTimelineFrame);
            found.insert(QString::fromUtf8("source_frame"), _sourceSourceFrame);
            found.insert(QString::fromUtf8("prompt_frame"), currentAIPaintPromptFrame());
            found.insert(QString::fromUtf8("sam3_ready"), sam3Ready);
            found.insert(QString::fromUtf8("matanyone2_ready"), matAnyone2Ready);
            found.insert(QString::fromUtf8("videomama_ready"), videoMamaReady);
            found.insert(QString::fromUtf8("run_button_enabled"), canRun);
            found.insert(QString::fromUtf8("last_result_manifest"), _lastResultManifestProjectRelative);
            _lastRunReadinessSignature = signature;
            writeAiLog(canRun ? QString::fromUtf8("info") : QString::fromUtf8("warn"),
                       QString::fromUtf8("panel"), QString::fromUtf8("run_button_enable_evaluation"),
                       canRun ? QString::fromUtf8("pass") : QString::fromUtf8("block"),
                       canRun ? QString::fromUtf8("Run button enabled for current AI model.") : QString::fromUtf8("Run button disabled for current AI model."),
                       QString::fromUtf8("Saved project, selected source, selected model prerequisites, and required prompt/base mask state."),
                       found,
                       canRun ? QString() : QString::fromUtf8("Check source binding, AI Paint prompts/current-frame mask, selected history mask, project saved state, and worker busy state."));
        }
        if (!canRun) {
            _runButton->setToolTip(QString::fromUtf8("Run unavailable: %1").arg(runReason));
        } else {
            _runButton->setToolTip(QString::fromUtf8("Run %1").arg(_modelCombo ? _modelCombo->currentText() : QString::fromUtf8("AI model")));
        }
    } else {
        const QString modelId = _modelCombo ? _modelCombo->currentData().toString() : QString();
        updateWorkflowGuide(modelId, QString::fromUtf8("run button is not available."));
    }
    if (_addMaskButton) {
        _addMaskButton->setEnabled(!_lastResultManifestProjectRelative.isEmpty() && !running && !sam3Running && !matAnyone2Running && !videoMamaRunning);
    }
    if (_replaceMaskButton) {
        _replaceMaskButton->setEnabled(!_lastResultManifestProjectRelative.isEmpty() && !running && !sam3Running && !matAnyone2Running && !videoMamaRunning);
    }
    const bool historyActionEnabled = !selectedResultManifestProjectRelative().isEmpty() && !running && !sam3Running && !matAnyone2Running && !videoMamaRunning;
    if (_previewAgainButton) {
        _previewAgainButton->setEnabled(historyActionEnabled);
    }
    if (_removeHistoryEntryButton) {
        _removeHistoryEntryButton->setEnabled(historyActionEnabled);
    }
    if (_cancelButton) {
        _cancelButton->setEnabled(running || sam3Running || matAnyone2Running || videoMamaRunning);
    }
    if (_frameRangeStartSpin) {
        _frameRangeStartSpin->setEnabled(!running && !sam3Running && !matAnyone2Running && !videoMamaRunning && hasSelectedSource());
    }
    if (_frameRangeEndSpin) {
        _frameRangeEndSpin->setEnabled(!running && !sam3Running && !matAnyone2Running && !videoMamaRunning && hasSelectedSource());
    }
    const QString currentModelId = _modelCombo ? _modelCombo->currentData().toString() : QString();
    const bool showVideoMamaOptions = currentModelId == QString::fromUtf8("videomama");
    const bool enableVideoMamaOptions = showVideoMamaOptions && !running && !sam3Running && !matAnyone2Running && !videoMamaRunning;
    if (_videoMamaBatchLabel) { _videoMamaBatchLabel->setVisible(showVideoMamaOptions); }
    if (_videoMamaBatchCombo) {
        _videoMamaBatchCombo->setVisible(showVideoMamaOptions);
        _videoMamaBatchCombo->setEnabled(enableVideoMamaOptions);
    }
    if (_videoMamaOverlapLabel) { _videoMamaOverlapLabel->setVisible(showVideoMamaOptions); }
    if (_videoMamaOverlapCombo) {
        _videoMamaOverlapCombo->setVisible(showVideoMamaOptions);
        _videoMamaOverlapCombo->setEnabled(enableVideoMamaOptions);
    }
    const QString videoMamaSignature = QString::fromUtf8("visible=%1|enabled=%2|batch=%3|overlap=%4")
            .arg(showVideoMamaOptions).arg(enableVideoMamaOptions)
            .arg(_videoMamaBatchCombo ? _videoMamaBatchCombo->currentData().toInt() : 0)
            .arg(_videoMamaOverlapCombo ? _videoMamaOverlapCombo->currentData().toInt() : 0);
    if (videoMamaSignature != _lastVideoMamaUiSignature) {
        QJsonObject videoMamaUi;
        videoMamaUi.insert(QString::fromUtf8("visible"), showVideoMamaOptions);
        videoMamaUi.insert(QString::fromUtf8("enabled"), enableVideoMamaOptions);
        videoMamaUi.insert(QString::fromUtf8("batch_frames"), _videoMamaBatchCombo ? _videoMamaBatchCombo->currentData().toInt() : 0);
        videoMamaUi.insert(QString::fromUtf8("blend_overlap"), _videoMamaOverlapCombo ? _videoMamaOverlapCombo->currentData().toInt() : 0);
        _lastVideoMamaUiSignature = videoMamaSignature;
        writeAiLog(QString::fromUtf8("info"), QString::fromUtf8("panel"), QString::fromUtf8("videomama_dropdown_state"),
                   showVideoMamaOptions ? QString::fromUtf8("pass") : QString::fromUtf8("skip"),
                   showVideoMamaOptions ? QString::fromUtf8("VideoMaMa batching/blending controls are visible for selected model.") : QString::fromUtf8("VideoMaMa controls hidden because selected model is not VideoMaMa."),
                   QString::fromUtf8("VideoMaMa selected shows Batch frames and Blend overlap controls with current values."), videoMamaUi);
    }
}

void FluxAiPanel::resetFrameRangeControls()
{
    _updatingFrameRangeControls = true;
    if (_frameRangeStartSpin) {
        _frameRangeStartSpin->setRange(0, 0);
        _frameRangeStartSpin->setValue(0);
        _frameRangeStartSpin->setEnabled(false);
    }
    if (_frameRangeEndSpin) {
        _frameRangeEndSpin->setRange(0, 0);
        _frameRangeEndSpin->setValue(0);
        _frameRangeEndSpin->setEnabled(false);
    }
    if (_frameRangeLabel) {
        _frameRangeLabel->setText(QString::fromUtf8("Frame Range: no source selected"));
    }
    _updatingFrameRangeControls = false;
}

void FluxAiPanel::setFrameRangeControls(int firstFrame, int lastFrame, bool preserveSelection)
{
    const int minimumFrame = qMin(firstFrame, lastFrame);
    const int maximumFrame = qMax(firstFrame, lastFrame);
    const int displayMinimumFrame = minimumFrame + _sourceTimeOffset;
    const int displayMaximumFrame = maximumFrame + _sourceTimeOffset;
    const int previousStart = _frameRangeStartSpin ? _frameRangeStartSpin->value() : displayMinimumFrame;
    const int previousEnd = _frameRangeEndSpin ? _frameRangeEndSpin->value() : displayMaximumFrame;
    const int selectedStart = preserveSelection ? qBound(displayMinimumFrame, previousStart, displayMaximumFrame) : displayMinimumFrame;
    const int selectedEnd = preserveSelection ? qBound(displayMinimumFrame, previousEnd, displayMaximumFrame) : displayMaximumFrame;

    _sourceRangeFirstFrame = minimumFrame;
    _sourceRangeLastFrame = maximumFrame;
    _updatingFrameRangeControls = true;
    if (_frameRangeStartSpin) {
        _frameRangeStartSpin->setRange(displayMinimumFrame, displayMaximumFrame);
        _frameRangeStartSpin->setValue(qMin(selectedStart, selectedEnd));
    }
    if (_frameRangeEndSpin) {
        _frameRangeEndSpin->setRange(displayMinimumFrame, displayMaximumFrame);
        _frameRangeEndSpin->setValue(qMax(selectedStart, selectedEnd));
    }
    if (_frameRangeLabel) {
        _frameRangeLabel->setText(QString::fromUtf8("Frame Range: timeline %1-%2 (trim default, source %3-%4)").arg(displayMinimumFrame).arg(displayMaximumFrame).arg(minimumFrame).arg(maximumFrame));
    }
    _updatingFrameRangeControls = false;
    updateUiState();
}

bool FluxAiPanel::selectedFrameRange(int* firstFrame, int* lastFrame, QString* message) const
{
    if (!_frameRangeStartSpin || !_frameRangeEndSpin || !hasSelectedSource()) {
        if (message) { *message = QString::fromUtf8("SAM3 frame range unavailable: no selected source."); }
        return false;
    }
    const int timelineStart = _frameRangeStartSpin->value();
    const int timelineEnd = _frameRangeEndSpin->value();
    const int start = timelineStart - _sourceTimeOffset;
    const int end = timelineEnd - _sourceTimeOffset;
    if (start > end) {
        if (message) { *message = QString::fromUtf8("SAM3 frame range is invalid: start is after end."); }
        return false;
    }
    if (start < _sourceRangeFirstFrame || end > _sourceRangeLastFrame) {
        if (message) {
            *message = QString::fromUtf8("SAM3 frame range is outside the selected layer trim: requested timeline %1-%2 (source %3-%4), trim timeline %5-%6 (source %7-%8).")
                           .arg(timelineStart)
                           .arg(timelineEnd)
                           .arg(start)
                           .arg(end)
                           .arg(_sourceRangeFirstFrame + _sourceTimeOffset)
                           .arg(_sourceRangeLastFrame + _sourceTimeOffset)
                           .arg(_sourceRangeFirstFrame)
                           .arg(_sourceRangeLastFrame);
        }
        return false;
    }
    if (firstFrame) { *firstFrame = start; }
    if (lastFrame) { *lastFrame = end; }
    return true;
}

QJsonObject FluxAiPanel::selectedFrameRangeMetadata() const
{
    int start = _sourceRangeFirstFrame;
    int end = _sourceRangeLastFrame;
    selectedFrameRange(&start, &end, nullptr);
    const int timeOffset = _sourceTimeOffset;
    QJsonObject metadata;
    metadata.insert(QString::fromUtf8("source_start"), start);
    metadata.insert(QString::fromUtf8("source_end"), end);
    metadata.insert(QString::fromUtf8("duration_frames"), qMax(0, end - start + 1));
    metadata.insert(QString::fromUtf8("default_trim_source_start"), _sourceRangeFirstFrame);
    metadata.insert(QString::fromUtf8("default_trim_source_end"), _sourceRangeLastFrame);
    metadata.insert(QString::fromUtf8("current_source_frame"), _sourceSourceFrame);
    metadata.insert(QString::fromUtf8("time_offset"), timeOffset);
    metadata.insert(QString::fromUtf8("timeline_start"), start + timeOffset);
    metadata.insert(QString::fromUtf8("timeline_end"), end + timeOffset);
    metadata.insert(QString::fromUtf8("preserves_layer_trim_by_default"), true);
    return metadata;
}

void FluxAiPanel::resetProgressBar()
{
    if (!_progressBar) {
        return;
    }
    _progressBar->setRange(0, 100);
    _progressBar->setValue(0);
    _progressBar->setFormat(QString::fromUtf8("Idle"));
}

void FluxAiPanel::setProgressBarBusy(const QString& message)
{
    if (!_progressBar) {
        return;
    }
    _progressBar->setRange(0, 0);
    _progressBar->setFormat(message.isEmpty() ? QString::fromUtf8("Working") : message);
}

void FluxAiPanel::setProgressBarValue(int percent, const QString& message)
{
    if (!_progressBar) {
        return;
    }
    const int clamped = qBound(0, percent, 100);
    _progressBar->setRange(0, 100);
    _progressBar->setValue(clamped);
    _progressBar->setFormat(message.isEmpty() ? QString::fromUtf8("%p%") : QString::fromUtf8("%1 - %p%").arg(message));
}

void FluxAiPanel::updateProgressBarFromPayload(const QJsonObject& payload, const QString& fallbackMessage)
{
    const QString message = payload.value(QString::fromUtf8("message")).toString(fallbackMessage);
    if (payload.contains(QString::fromUtf8("percent"))) {
        setProgressBarValue(payload.value(QString::fromUtf8("percent")).toInt(0), message);
        return;
    }
    const int completed = payload.value(QString::fromUtf8("completed")).toInt(-1);
    const int total = payload.value(QString::fromUtf8("total")).toInt(0);
    if (completed >= 0 && total > 0) {
        setProgressBarValue(static_cast<int>(std::round((static_cast<double>(completed) / static_cast<double>(total)) * 100.0)), message);
        return;
    }
    setProgressBarBusy(message);
}

QString FluxAiPanel::aiLogProjectPath() const
{
    if (_gui && _gui->getApp() && _gui->getApp()->getProject() && _gui->getApp()->getProject()->hasProjectBeenSavedByUser()) {
        return _gui->getApp()->getProject()->getProjectPath();
    }
    return QString();
}

void FluxAiPanel::writeAiLog(const QString& level, const QString& area, const QString& step,
                             const QString& status, const QString& message,
                             const QString& expected, const QJsonObject& found,
                             const QString& nextAction) const
{
    const QString path = FluxAiLog::writeEvent(aiLogProjectPath(), level, area, step, status, message, expected, found, nextAction);
    if (_aiLogPathLabel) {
        _aiLogPathLabel->setText(QString::fromUtf8("AI log: %1").arg(path));
    }
}

void FluxAiPanel::updateAiLogPathLabel() const
{
    if (_aiLogPathLabel) {
        _aiLogPathLabel->setText(QString::fromUtf8("AI log: %1").arg(FluxAiLog::latestLogPath(aiLogProjectPath())));
    }
}

QString FluxAiPanel::runReadinessSignature(const QString& modelId, bool running, bool sam3Running, bool matAnyone2Running, bool videoMamaRunning, bool projectSaved, bool canRun) const
{
    return QString::fromUtf8("model=%1|run=%2|sam3=%3|mat2=%4|vm=%5|saved=%6|source=%7|aipaint=%8|result=%9|can=%10|frame=%11")
            .arg(modelId)
            .arg(running).arg(sam3Running).arg(matAnyone2Running).arg(videoMamaRunning)
            .arg(projectSaved).arg(hasSelectedSource()).arg(static_cast<bool>(_sourceAIPaintNode))
            .arg(!_lastResultManifestProjectRelative.isEmpty()).arg(canRun).arg(_sourceTimelineFrame);
}

QString FluxAiPanel::workflowGuideText(const QString& modelId) const
{
    if (modelId == QString::fromUtf8("sam3_transformers")) {
        return QString::fromUtf8(
            "Workflow — SAM3\n"
            "1. Select the footage layer in the timeline.\n"
            "2. Add/select AI Paint on that layer.\n"
            "3. In AI Paint, choose Point or Box and add prompts in the viewer.\n"
            "4. Save the project.\n"
            "5. Run SAM3. For a sequence, use the frame range above before Run.\n"
            "After SAM3: use Result History as the base mask for VideoMaMa, or Add/Replace Mask only if you want to apply it to the comp.");
    }
    if (modelId == QString::fromUtf8("matanyone2")) {
        return QString::fromUtf8(
            "Workflow — MatAnyone2\n"
            "1. Select the same source footage layer.\n"
            "2. Set Start/End to a video range (End > Start).\n"
            "3. Provide a base mask: select a SAM3 result in Result History, or keep an AI Paint live-preview mask on the current frame.\n"
            "4. Save the project.\n"
            "5. Run MatAnyone2. You do not need to Add Mask first unless you want the SAM3 result applied to the layer.");
    }
    if (modelId == QString::fromUtf8("videomama")) {
        return QString::fromUtf8(
            "Workflow — VideoMaMa\n"
            "1. Select the source footage layer, not a generated mask layer.\n"
            "2. Set Start/End to a video range (End > Start).\n"
            "3. Select the SAM3 mask/sequence in Result History, or use the current AI Paint live-preview mask.\n"
            "4. Choose Batch frames and Blend overlap.\n"
            "5. Run VideoMaMa. Do not Add Mask first unless you want to apply the SAM3 result to the comp.");
    }
    return QString::fromUtf8(
        "Workflow\n"
        "1. Select a source footage layer.\n"
        "2. Select a model.\n"
        "3. Follow the model-specific prerequisites shown here.");
}

QString FluxAiPanel::runUnavailableReason(const QString& modelId, bool running, bool sam3Running, bool matAnyone2Running, bool videoMamaRunning, bool projectSaved) const
{
    if (running || sam3Running || matAnyone2Running || videoMamaRunning) {
        return QString::fromUtf8("another AI job or worker action is still running.");
    }
    if (!projectSaved) {
        return QString::fromUtf8("save the project first so Flux can write project-relative AI media.");
    }
    if (!hasSelectedSource()) {
        return QString::fromUtf8("select the source footage layer in the timeline.");
    }

    if (modelId == QString::fromUtf8("sam3_transformers")) {
        QString promptMessage;
        std::vector<AIPaintPrompt> prompts;
        if (!readAIPaintPrompts(&prompts, &promptMessage)) {
            return promptMessage;
        }
        if (!hasRunnableAIPaintPrompt()) {
            return QString::fromUtf8("add at least one enabled Include Point or Box prompt with AI Paint on the current source frame.");
        }
        return QString::fromUtf8("ready.");
    }

    if (modelId == QString::fromUtf8("matanyone2") || modelId == QString::fromUtf8("videomama")) {
        int rs = 0;
        int re = 0;
        QString rangeMsg;
        if (!selectedFrameRange(&rs, &re, &rangeMsg) || re <= rs) {
            return QString::fromUtf8("set a video frame range where End is greater than Start.");
        }

        bool hasAIPaintMask = false;
        AIPaint* aiPaint = aipaintEffectFromNode(_sourceAIPaintNode);
        if (aiPaint) {
            const QString liveMask = aiPaint->livePreviewMaskPath();
            hasAIPaintMask = livePreviewMaskMatchesCurrentPromptFrame(aiPaint) && !liveMask.isEmpty() && QFileInfo(liveMask).isFile();
        }
        QString historyMask;
        QString historyMsg;
        const bool hasHistoryMask = selectedResultMaskProjectRelative(&historyMask, &historyMsg);
        if (!hasAIPaintMask && !hasHistoryMask) {
            return QString::fromUtf8("select a SAM3 result in Result History, or create a current AI Paint live-preview mask. Add Mask is not required.");
        }
        return QString::fromUtf8("ready.");
    }

    return QString::fromUtf8("select a supported model.");
}

void FluxAiPanel::updateWorkflowGuide(const QString& modelId, const QString& runReason) const
{
    if (_workflowGuideLabel) {
        _workflowGuideLabel->setText(workflowGuideText(modelId));
    }
    if (_runReasonLabel) {
        const bool ready = runReason == QString::fromUtf8("ready.");
        _runReasonLabel->setText(ready ? QString::fromUtf8("Run ready: all required inputs are present.")
                                       : QString::fromUtf8("Run unavailable because: %1").arg(runReason));
    }
}

void FluxAiPanel::onFrameRangeEdited()
{
    if (_updatingFrameRangeControls || !_frameRangeStartSpin || !_frameRangeEndSpin) {
        return;
    }
    _updatingFrameRangeControls = true;
    if (_frameRangeStartSpin->value() > _frameRangeEndSpin->value()) {
        if (QObject::sender() == _frameRangeStartSpin) {
            _frameRangeEndSpin->setValue(_frameRangeStartSpin->value());
        } else {
            _frameRangeStartSpin->setValue(_frameRangeEndSpin->value());
        }
    }
    _updatingFrameRangeControls = false;
}



bool FluxAiPanel::readAIPaintPrompts(std::vector<AIPaintPrompt>* prompts, QString* message) const
{
    if (!_sourceAIPaintNode || !_sourceAIPaintNode->isActivated()) {
        if (message) { *message = QString::fromUtf8("selected layer has no AI Paint effect"); }
        return false;
    }
    EffectInstancePtr effect = _sourceAIPaintNode->getEffectInstance();
    if (!effect || effect->getPluginID() != PLUGINID_NATRON_AIPAINT) {
        if (message) { *message = QString::fromUtf8("selected prompt provider is not AI Paint"); }
        return false;
    }
    const AIPaint* aiPaint = dynamic_cast<const AIPaint*>(effect.get());
    if (!aiPaint) {
        if (message) { *message = QString::fromUtf8("selected AI Paint provider cannot be read"); }
        return false;
    }
    if (prompts) {
        prompts->clear();
        const std::vector<AIPaintPrompt> allPrompts = aiPaint->getPrompts();
        for (const AIPaintPrompt& prompt : allPrompts) {
            if (promptMatchesCurrentTimelineFrame(prompt)) {
                prompts->push_back(prompt);
            }
        }
    }
    return true;
}

bool FluxAiPanel::promptIsInTimelineRange(const AIPaintPrompt& prompt, int timelineStart, int timelineEnd) const
{
    // Check metadata timelineFrame first
    {
        const auto it = prompt.metadata.find("timelineFrame");
        if (it != prompt.metadata.end()) {
            try {
                const int tf = std::stoi(it->second);
                return tf >= timelineStart && tf <= timelineEnd;
            } catch (...) {}
        }
    }
    // Check metadata sourceFrame + current timeOffset -> timeline frame
    {
        const auto it = prompt.metadata.find("sourceFrame");
        if (it != prompt.metadata.end()) {
            try {
                const int sf = std::stoi(it->second);
                const int timeOffset = _sourceTimeOffset;
                const int tf = sf + timeOffset;
                return tf >= timelineStart && tf <= timelineEnd;
            } catch (...) {}
        }
    }
    if (std::isfinite(prompt.time)) {
        const int tf = static_cast<int>(std::lround(prompt.time));
        return tf >= timelineStart && tf <= timelineEnd;
    }
    // Prompts without frame info: include (same as promptMatchesCurrentTimelineFrame does)
    return true;
}

bool FluxAiPanel::readAIPaintPromptsInRange(int timelineStart, int timelineEnd,
                                             std::vector<AIPaintPrompt>* prompts, QString* message) const
{
    if (!_sourceAIPaintNode || !_sourceAIPaintNode->isActivated()) {
        if (message) { *message = QString::fromUtf8("selected layer has no AI Paint effect"); }
        return false;
    }
    EffectInstancePtr effect = _sourceAIPaintNode->getEffectInstance();
    if (!effect || effect->getPluginID() != PLUGINID_NATRON_AIPAINT) {
        if (message) { *message = QString::fromUtf8("selected prompt provider is not AI Paint"); }
        return false;
    }
    const AIPaint* aiPaint = dynamic_cast<const AIPaint*>(effect.get());
    if (!aiPaint) {
        if (message) { *message = QString::fromUtf8("selected AI Paint provider cannot be read"); }
        return false;
    }
    if (prompts) {
        prompts->clear();
        const std::vector<AIPaintPrompt> allPrompts = aiPaint->getPrompts();
        for (const AIPaintPrompt& prompt : allPrompts) {
            if (!prompt.enabled || (prompt.type != AIPaintPromptType::Point && prompt.type != AIPaintPromptType::Box)) {
                continue;
            }
            if (prompt.role != AIPaintPromptRole::Include) {
                continue;
            }
            if (promptIsInTimelineRange(prompt, timelineStart, timelineEnd)) {
                prompts->push_back(prompt);
            }
        }
    }
    return true;
}

bool FluxAiPanel::renderProcessedAIPaintFrame(int timelineFrame, int sourceFrame,
                                               const QString& outputPath, QJsonObject* metadata, QString* message) const
{
    if (!_sourceAIPaintNode || !_sourceAIPaintNode->isActivated()) {
        if (message) { *message = QString::fromUtf8("processed frame render failed: no active AI Paint node"); }
        return false;
    }
    EffectInstancePtr effect = _sourceAIPaintNode->getEffectInstance();
    if (!effect) {
        if (message) { *message = QString::fromUtf8("processed frame render failed: AI Paint effect instance unavailable"); }
        return false;
    }

    // Compute RoD at the requested timeline frame
    RectD rod;
    bool isProjectFormat = false;
    const U64 renderHash = _sourceAIPaintNode->getHashValue();
    const StatusEnum stat = effect->getRegionOfDefinition_public(renderHash, timelineFrame,
                                                                  RenderScale::identity, ViewIdx(0),
                                                                  &rod, &isProjectFormat);
    if (stat == eStatusFailed || rod.isNull() || rod.x2 <= rod.x1 || rod.y2 <= rod.y1) {
        if (message) { *message = QString::fromUtf8("processed frame render failed: invalid RoD at timeline frame %1").arg(timelineFrame); }
        return false;
    }
    const double par = effect->getAspectRatio(-1);
    const RectI renderWindow = rod.toPixelEnclosing(0, par);
    if (renderWindow.isNull() || renderWindow.width() <= 0 || renderWindow.height() <= 0) {
        if (message) { *message = QString::fromUtf8("processed frame render failed: invalid render window at timeline frame %1").arg(timelineFrame); }
        return false;
    }

    RenderingFlagSetter flagIsRendering(_sourceAIPaintNode);
    AbortableRenderInfoPtr abortInfo = AbortableRenderInfo::create(true, 0);
    ParallelRenderArgsSetter frameRenderArgs(timelineFrame, ViewIdx(0), true, false,
                                              abortInfo, _sourceAIPaintNode, 0,
                                              _sourceAIPaintNode->getApp()->getTimeLine().get(),
                                              _sourceAIPaintNode, false, false, RenderStatsPtr());
    FrameRequestMap request;
    const StatusEnum reqStat = EffectInstance::computeRequestPass(timelineFrame, ViewIdx(0), 0, rod,
                                                                   _sourceAIPaintNode, request);
    if (reqStat == eStatusFailed) {
        if (message) { *message = QString::fromUtf8("processed frame render failed: computeRequestPass failed at timeline frame %1").arg(timelineFrame); }
        return false;
    }
    frameRenderArgs.updateNodesRequest(request);

    std::list<ImagePlaneDesc> requestedComps;
    ImagePlaneDesc plane, pairedPlane;
    effect->getMetadataComponents(-1, &plane, &pairedPlane);
    requestedComps.push_back(plane);
    const ImageBitDepthEnum requestedDepth = effect->getBitDepth(-1);
    std::map<ImagePlaneDesc, ImagePtr> planes;
    try {
        EffectInstance::RenderRoIArgs args(timelineFrame, RenderScale::identity, 0, ViewIdx(0),
                                            false, renderWindow, rod, requestedComps, requestedDepth,
                                            false, effect.get(), eStorageModeRAM, timelineFrame);
        const EffectInstance::RenderRoIRetCode retCode = effect->renderRoI(args, &planes);
        if (retCode != EffectInstance::eRenderRoIRetCodeOk || planes.empty()) {
            if (message) { *message = QString::fromUtf8("processed frame render failed: renderRoI returned no image at timeline frame %1").arg(timelineFrame); }
            return false;
        }
    } catch (...) {
        if (message) { *message = QString::fromUtf8("processed frame render failed: renderRoI threw exception at timeline frame %1").arg(timelineFrame); }
        return false;
    }
    const ImagePtr imagePtr = planes.begin()->second;
    if (!imagePtr) {
        if (message) { *message = QString::fromUtf8("processed frame render failed: rendered image is null at timeline frame %1").arg(timelineFrame); }
        return false;
    }

    const Image* const img = imagePtr.get();
    const RectI bounds = img->getBounds();
    if (bounds.isNull() || bounds.width() <= 0 || bounds.height() <= 0) {
        if (message) { *message = QString::fromUtf8("processed frame render failed: image bounds empty at timeline frame %1").arg(timelineFrame); }
        return false;
    }

    QString convertMessage;
    const QImage qimg = fluxImageToQImage(img, &convertMessage);
    if (qimg.isNull()) {
        if (message) { *message = QString::fromUtf8("processed frame render failed: Image-to-QImage conversion failed at timeline frame %1: %2").arg(timelineFrame).arg(convertMessage); }
        return false;
    }

    QImageWriter writer(outputPath, QString::fromUtf8("PNG").toUtf8());
    if (!writer.write(qimg)) {
        if (message) { *message = QString::fromUtf8("processed frame render failed: could not write PNG at timeline frame %1: %2").arg(timelineFrame).arg(writer.errorString()); }
        QFile::remove(outputPath);
        return false;
    }

    if (metadata) {
        const RectD imageRod = img->getRoD();
        metadata->insert(QString::fromUtf8("capture_mode"), QString::fromUtf8("node-render-roi"));
        metadata->insert(QString::fromUtf8("render_mode"), QString::fromUtf8("direct_render_roi"));
        metadata->insert(QString::fromUtf8("render_node"), QString::fromUtf8(_sourceAIPaintNode->getScriptName().c_str()));
        metadata->insert(QString::fromUtf8("timeline_frame"), timelineFrame);
        metadata->insert(QString::fromUtf8("source_frame"), sourceFrame);
        metadata->insert(QString::fromUtf8("time_offset"), timelineFrame - sourceFrame);
        metadata->insert(QString::fromUtf8("width"), qimg.width());
        metadata->insert(QString::fromUtf8("height"), qimg.height());
        metadata->insert(QString::fromUtf8("exported_png_width"), qimg.width());
        metadata->insert(QString::fromUtf8("exported_png_height"), qimg.height());

        QJsonObject boundsObj;
        boundsObj.insert(QString::fromUtf8("x1"), bounds.x1);
        boundsObj.insert(QString::fromUtf8("y1"), bounds.y1);
        boundsObj.insert(QString::fromUtf8("x2"), bounds.x2);
        boundsObj.insert(QString::fromUtf8("y2"), bounds.y2);
        metadata->insert(QString::fromUtf8("image_bounds"), boundsObj);

        QJsonObject rodObj;
        rodObj.insert(QString::fromUtf8("x1"), imageRod.x1);
        rodObj.insert(QString::fromUtf8("y1"), imageRod.y1);
        rodObj.insert(QString::fromUtf8("x2"), imageRod.x2);
        rodObj.insert(QString::fromUtf8("y2"), imageRod.y2);
        metadata->insert(QString::fromUtf8("rod"), rodObj);
    }
    return true;
}

QString FluxAiPanel::exportProcessedAIPaintSequence(int sourceRangeStart, int sourceRangeEnd,
                                                     QJsonObject* sequenceMetadata, QString* diagnostics,
                                                     std::function<bool(int, int)> progressCallback)
{
    if (!_gui || !_gui->getApp() || !_gui->getApp()->getProject()) {
        if (diagnostics) { *diagnostics = QString::fromUtf8("processed sequence export unavailable: project unavailable"); }
        return QString();
    }
    FluxTimeline* timeline = _gui->getFluxTimeline();
    if (!timeline) {
        if (diagnostics) { *diagnostics = QString::fromUtf8("processed sequence export unavailable: missing Flux timeline"); }
        return QString();
    }
    const QList<FluxLayer>& layers = timeline->getLayers();
    if (_sourceLayerIndex < 0 || _sourceLayerIndex >= layers.size()) {
        if (diagnostics) { *diagnostics = QString::fromUtf8("processed sequence export unavailable: layer index invalid"); }
        return QString();
    }
    const FluxLayer& layer = layers[_sourceLayerIndex];
    if (!_sourceAIPaintNode || !_sourceAIPaintNode->isActivated()) {
        if (diagnostics) { *diagnostics = QString::fromUtf8("processed sequence export unavailable: no active AI Paint node"); }
        return QString();
    }

    const int clampedStart = qBound(layer.originalFirstFrame, sourceRangeStart, layer.originalLastFrame);
    const int clampedEnd = qBound(layer.originalFirstFrame, sourceRangeEnd, layer.originalLastFrame);
    if (clampedStart > clampedEnd) {
        if (diagnostics) { *diagnostics = QString::fromUtf8("processed sequence export unavailable: range outside original media"); }
        return QString();
    }

    const QString projectDir = _gui->getApp()->getProject()->getProjectPath();
    if (projectDir.isEmpty()) {
        if (diagnostics) { *diagnostics = QString::fromUtf8("processed sequence export unavailable: project must be saved first"); }
        return QString();
    }

    QString safeName = _sourceLayerName.isEmpty() ? QString::fromUtf8("layer") : _sourceLayerName;
    safeName.replace(QRegularExpression(QString::fromUtf8("[^A-Za-z0-9_.-]+")), QString::fromUtf8("_"));
    if (safeName.isEmpty() || safeName == QString::fromUtf8(".") || safeName == QString::fromUtf8("..")) {
        safeName = QString::fromUtf8("layer");
    }
    const QString sequenceDirPath = QDir(projectDir).filePath(
        QString::fromUtf8("FluxGenerated/AI/.processed_sequences/%1/").arg(safeName));
    QDir sequenceDir;
    if (!sequenceDir.mkpath(sequenceDirPath)) {
        if (diagnostics) { *diagnostics = QString::fromUtf8("processed sequence export unavailable: could not create directory %1").arg(sequenceDirPath); }
        return QString();
    }

    QJsonArray frames;
    int seqWidth = 0;
    int seqHeight = 0;
    QJsonObject sequenceImageBounds;
    QJsonObject sequenceRoD;
    const int totalFrames = clampedEnd - clampedStart + 1;
    int completedFrames = 0;

    for (int sourceFrame = clampedStart; sourceFrame <= clampedEnd; ++sourceFrame) {
        const int timelineFrame = sourceFrame + layer.timeOffset;

        const QString outputName = QString::fromUtf8("processed_%1.png").arg(timelineFrame, 6, 10, QLatin1Char('0'));
        const QString outputPath = QDir(sequenceDirPath).filePath(outputName);

        QJsonObject frameMeta;
        QString frameMessage;
        if (!renderProcessedAIPaintFrame(timelineFrame, sourceFrame, outputPath, &frameMeta, &frameMessage)) {
            // Clean up partially exported frames
            for (const QJsonValue& fv : frames) {
                QFile::remove(fv.toObject().value(QString::fromUtf8("path")).toString());
            }
            if (diagnostics) { *diagnostics = frameMessage; }
            return QString();
        }

        const int fw = frameMeta.value(QString::fromUtf8("width")).toInt(0);
        const int fh = frameMeta.value(QString::fromUtf8("height")).toInt(0);
        if (fw <= 0 || fh <= 0) {
            QFile::remove(outputPath);
            for (const QJsonValue& fv : frames) { QFile::remove(fv.toObject().value(QString::fromUtf8("path")).toString()); }
            if (diagnostics) { *diagnostics = QString::fromUtf8("processed sequence export failed: invalid dimensions at source frame %1").arg(sourceFrame); }
            return QString();
        }
        if (seqWidth == 0 && seqHeight == 0) {
            seqWidth = fw;
            seqHeight = fh;
            sequenceImageBounds = frameMeta.value(QString::fromUtf8("image_bounds")).toObject();
            sequenceRoD = frameMeta.value(QString::fromUtf8("rod")).toObject();
        } else if (seqWidth != fw || seqHeight != fh) {
            QFile::remove(outputPath);
            for (const QJsonValue& fv : frames) { QFile::remove(fv.toObject().value(QString::fromUtf8("path")).toString()); }
            if (diagnostics) { *diagnostics = QString::fromUtf8("processed sequence export failed: dimension change at source frame %1 (%2x%3 vs %4x%5)")
                                                                        .arg(sourceFrame).arg(fw).arg(fh).arg(seqWidth).arg(seqHeight); }
            return QString();
        } else {
            const QJsonObject frameBounds = frameMeta.value(QString::fromUtf8("image_bounds")).toObject();
            const QJsonObject frameRod = frameMeta.value(QString::fromUtf8("rod")).toObject();
            if (frameBounds != sequenceImageBounds) {
                QFile::remove(outputPath);
                for (const QJsonValue& fv : frames) { QFile::remove(fv.toObject().value(QString::fromUtf8("path")).toString()); }
                if (diagnostics) { *diagnostics = QString::fromUtf8("processed sequence export failed: image_bounds change at source frame %1 (expected %2, got %3)")
                                                                            .arg(sourceFrame)
                                                                            .arg(QString::fromUtf8(QJsonDocument(sequenceImageBounds).toJson(QJsonDocument::Compact)))
                                                                            .arg(QString::fromUtf8(QJsonDocument(frameBounds).toJson(QJsonDocument::Compact))); }
                return QString();
            }
            if (frameRod != sequenceRoD) {
                QFile::remove(outputPath);
                for (const QJsonValue& fv : frames) { QFile::remove(fv.toObject().value(QString::fromUtf8("path")).toString()); }
                if (diagnostics) { *diagnostics = QString::fromUtf8("processed sequence export failed: region of definition change at source frame %1 (expected %2, got %3)")
                                                                            .arg(sourceFrame)
                                                                            .arg(QString::fromUtf8(QJsonDocument(sequenceRoD).toJson(QJsonDocument::Compact)))
                                                                            .arg(QString::fromUtf8(QJsonDocument(frameRod).toJson(QJsonDocument::Compact))); }
                return QString();
            }
        }

        QJsonObject frameObj;
        frameObj.insert(QString::fromUtf8("path"), outputPath);
        frameObj.insert(QString::fromUtf8("basename"), outputName);
        frameObj.insert(QString::fromUtf8("source_frame"), sourceFrame);
        frameObj.insert(QString::fromUtf8("timeline_frame"), timelineFrame);
        frameObj.insert(QString::fromUtf8("zero_based_source_frame"), sourceFrame - layer.originalFirstFrame);
        frames.append(frameObj);
        ++completedFrames;

        if (progressCallback && !progressCallback(completedFrames, totalFrames)) {
            QFile::remove(outputPath);
            for (const QJsonValue& fv : frames) { QFile::remove(fv.toObject().value(QString::fromUtf8("path")).toString()); }
            if (diagnostics) { *diagnostics = QString::fromUtf8("processed sequence export canceled"); }
            return QString();
        }
    }

    if (frames.isEmpty()) {
        if (diagnostics) { *diagnostics = QString::fromUtf8("processed sequence export failed: no frames exported"); }
        return QString();
    }

    if (sequenceMetadata) {
        QJsonObject meta;
        meta.insert(QString::fromUtf8("capture_mode"), QString::fromUtf8("node-render-roi"));
        meta.insert(QString::fromUtf8("export_target"), QString::fromUtf8("processed_ai_paint_sequence"));
        meta.insert(QString::fromUtf8("render_mode"), QString::fromUtf8("direct_render_roi_sequence"));
        meta.insert(QString::fromUtf8("selected_layer_index"), _sourceLayerIndex);
        meta.insert(QString::fromUtf8("selected_layer_name"), _sourceLayerName);
        meta.insert(QString::fromUtf8("reader_label"), _sourceReaderLabel);
        meta.insert(QString::fromUtf8("render_node"), QString::fromUtf8(_sourceAIPaintNode->getScriptName().c_str()));
        meta.insert(QString::fromUtf8("source_frame"), _sourceSourceFrame);
        meta.insert(QString::fromUtf8("timeline_frame"), _sourceTimelineFrame);
        meta.insert(QString::fromUtf8("source_frame_start"), clampedStart);
        meta.insert(QString::fromUtf8("source_frame_end"), clampedEnd);
        meta.insert(QString::fromUtf8("timeline_frame_start"), clampedStart + layer.timeOffset);
        meta.insert(QString::fromUtf8("timeline_frame_end"), clampedEnd + layer.timeOffset);
        meta.insert(QString::fromUtf8("duration_frames"), frames.size());
        meta.insert(QString::fromUtf8("original_first_frame"), layer.originalFirstFrame);
        meta.insert(QString::fromUtf8("original_last_frame"), layer.originalLastFrame);
        meta.insert(QString::fromUtf8("time_offset"), layer.timeOffset);
        meta.insert(QString::fromUtf8("width"), seqWidth);
        meta.insert(QString::fromUtf8("height"), seqHeight);
        meta.insert(QString::fromUtf8("exported_png_width"), seqWidth);
        meta.insert(QString::fromUtf8("exported_png_height"), seqHeight);
        meta.insert(QString::fromUtf8("image_bounds"), sequenceImageBounds);
        meta.insert(QString::fromUtf8("rod"), sequenceRoD);
        meta.insert(QString::fromUtf8("temporary_source_sequence_dir"), sequenceDirPath);
        meta.insert(QString::fromUtf8("frames"), frames);
        if (std::isfinite(layer.sourceFrameRate) && layer.sourceFrameRate > 0.0) {
            meta.insert(QString::fromUtf8("source_frame_rate"), layer.sourceFrameRate);
        }
        *sequenceMetadata = meta;
    }
    if (diagnostics) {
        *diagnostics = QString::fromUtf8("processed AI Paint sequence exported (%1 frames, %2x%3)").arg(frames.size()).arg(seqWidth).arg(seqHeight);
    }
    fprintf(stderr,
            "FLUX-SAM3-A1 processed sequence capture complete: layer=%d sourceRange=[%d,%d] timelineRange=[%d,%d] frames=%d dimensions=%dx%d outputDir='%s'\n",
            _sourceLayerIndex, clampedStart, clampedEnd,
            clampedStart + layer.timeOffset, clampedEnd + layer.timeOffset,
            static_cast<int>(frames.size()), seqWidth, seqHeight, sequenceDirPath.toStdString().c_str());
    return sequenceDirPath;
}

int FluxAiPanel::currentAIPaintPromptFrame() const
{
    // AIPaint overlay callbacks receive Natron/source timeline time. In the Flux UI
    // context this can differ from the layer-row frame (_sourceTimelineFrame), e.g.
    // frame 1 source media displayed at Flux timeline frame 0. Use source frame as
    // the primary prompt ownership key and keep timeline frame as a legacy fallback.
    return _sourceSourceFrame;
}

bool FluxAiPanel::promptMatchesCurrentTimelineFrame(const AIPaintPrompt& prompt) const
{
    const int promptFrame = currentAIPaintPromptFrame();
    const int legacyTimelineFrame = _sourceTimelineFrame;
    const std::map<std::string, std::string>::const_iterator it = prompt.metadata.find("timelineFrame");
    if (it != prompt.metadata.end()) {
        try {
            const int storedFrame = std::stoi(it->second);
            return storedFrame == promptFrame || storedFrame == legacyTimelineFrame;
        } catch (...) {
            // Fall through to legacy prompt.time below.
        }
    }
    if (!std::isfinite(prompt.time)) {
        return true;
    }
    const int storedFrame = static_cast<int>(std::lround(prompt.time));
    return storedFrame == promptFrame || storedFrame == legacyTimelineFrame;
}

bool FluxAiPanel::livePreviewMaskMatchesCurrentPromptFrame(const AIPaint* aiPaint) const
{
    if (!aiPaint) {
        return false;
    }
    const int frame = aiPaint->livePreviewMaskTimelineFrame();
    return frame == currentAIPaintPromptFrame() || frame == _sourceTimelineFrame;
}

bool FluxAiPanel::chooseAIPaintPrompt(const std::vector<AIPaintPrompt>& prompts, AIPaintPrompt* prompt, QString* message) const
{
    const AIPaintPrompt* selected = nullptr;
    const AIPaintPrompt* newest = nullptr;
    for (const AIPaintPrompt& p : prompts) {
        if (!p.enabled || (p.type != AIPaintPromptType::Point && p.type != AIPaintPromptType::Box)) {
            continue;
        }
        if (p.selected && (!selected || p.id > selected->id)) {
            selected = &p;
        }
        if (!newest || p.id > newest->id) {
            newest = &p;
        }
    }
    const AIPaintPrompt* chosen = selected ? selected : newest;
    if (!chosen) {
        if (message) { *message = QString::fromUtf8("selected AI Paint effect has no enabled point/box prompts"); }
        return false;
    }
    if (chosen->role == AIPaintPromptRole::Exclude) {
        if (message) { *message = QString::fromUtf8("AI Paint exclude prompts are not supported by this SAM3 path yet"); }
        return false;
    }
    if (chosen->role == AIPaintPromptRole::Neutral) {
        if (message) { *message = QString::fromUtf8("AI Paint neutral prompts are not supported by this SAM3 path yet"); }
        return false;
    }
    if (prompt) { *prompt = *chosen; }
    return true;
}

static QString aiPaintPromptTypeString(AIPaintPromptType type)
{
    return type == AIPaintPromptType::Point ? QString::fromUtf8("point") : QString::fromUtf8("box");
}

static QString aiPaintPromptRoleString(AIPaintPromptRole role)
{
    switch (role) {
    case AIPaintPromptRole::Include: return QString::fromUtf8("include");
    case AIPaintPromptRole::Exclude: return QString::fromUtf8("exclude");
    case AIPaintPromptRole::Neutral: return QString::fromUtf8("neutral");
    }
    return QString::fromUtf8("unknown");
}

QJsonObject FluxAiPanel::buildAIPaintPromptForSam(const AIPaintPrompt& prompt, QString* message) const
{
    QJsonObject viewerPrompt;
    viewerPrompt.insert(QString::fromUtf8("type"), aiPaintPromptTypeString(prompt.type));
    viewerPrompt.insert(QString::fromUtf8("label"), 1);
    if (prompt.type == AIPaintPromptType::Point) {
        viewerPrompt.insert(QString::fromUtf8("canonical"), QJsonObject{{QString::fromUtf8("x"), prompt.point.x()}, {QString::fromUtf8("y"), prompt.point.y()}});
    } else if (prompt.type == AIPaintPromptType::Box) {
        viewerPrompt.insert(QString::fromUtf8("canonical_min"), QJsonObject{{QString::fromUtf8("x"), prompt.rect.left()}, {QString::fromUtf8("y"), prompt.rect.top()}});
        viewerPrompt.insert(QString::fromUtf8("canonical_max"), QJsonObject{{QString::fromUtf8("x"), prompt.rect.right()}, {QString::fromUtf8("y"), prompt.rect.bottom()}});
    } else {
        if (message) { *message = QString::fromUtf8("unsupported AI Paint prompt type"); }
        return QJsonObject();
    }
    QJsonObject converted = prompt.type == AIPaintPromptType::Point ? buildSourcePointPrompt(viewerPrompt, message) : buildSourceBoxPrompt(viewerPrompt, message);
    if (converted.isEmpty()) {
        return QJsonObject();
    }

    // --- Resolve per-prompt frame ownership ---
    // Deriving sourceFrame from timelineFrame uses current panel source context/timeOffset
    // because AIPaint.cpp lacks source layer context.
    const int timeOffset = _sourceTimeOffset;

    // Build a QJsonObject view of prompt.metadata for lookup.
    QJsonObject promptMeta;
    for (const auto& kv : prompt.metadata) {
        promptMeta.insert(QString::fromStdString(kv.first), QString::fromStdString(kv.second));
    }

    int resolvedSourceFrame = _sourceSourceFrame;
    int resolvedTimelineFrame = _sourceTimelineFrame;
    QString frameOwnershipSource = QString::fromUtf8("currentSourceFallback");

    // Resolution order: metadata sourceFrame > metadata timelineFrame > legacy prompt.time > fallback
    {
        const QString sfStr = promptMeta.value(QString::fromUtf8("sourceFrame")).toString();
        bool sfOk = false;
        const int sf = sfStr.toInt(&sfOk);
        if (sfOk && !sfStr.isEmpty()) {
            resolvedSourceFrame = sf;
            const QString tfStr = promptMeta.value(QString::fromUtf8("timelineFrame")).toString();
            bool tfOk = false;
            const int tf = tfStr.toInt(&tfOk);
            resolvedTimelineFrame = tfOk && !tfStr.isEmpty() ? tf : (sf + timeOffset);
            frameOwnershipSource = QString::fromUtf8("metadataSourceFrame");
        }
    }
    if (frameOwnershipSource == QString::fromUtf8("currentSourceFallback")) {
        const QString tfStr = promptMeta.value(QString::fromUtf8("timelineFrame")).toString();
        bool tfOk = false;
        const int tf = tfStr.toInt(&tfOk);
        if (tfOk && !tfStr.isEmpty()) {
            resolvedTimelineFrame = tf;
            resolvedSourceFrame = tf - timeOffset;
            frameOwnershipSource = QString::fromUtf8("metadataTimelineFrame");
        }
    }
    if (frameOwnershipSource == QString::fromUtf8("currentSourceFallback")) {
        if (std::isfinite(prompt.time)) {
            const int legacyTf = static_cast<int>(std::lround(prompt.time));
            resolvedTimelineFrame = legacyTf;
            resolvedSourceFrame = legacyTf - timeOffset;
            frameOwnershipSource = QString::fromUtf8("legacyPromptTime");
        }
    }

    QJsonObject provenance;
    provenance.insert(QString::fromUtf8("id"), prompt.id);
    provenance.insert(QString::fromUtf8("type"), aiPaintPromptTypeString(prompt.type));
    provenance.insert(QString::fromUtf8("role"), aiPaintPromptRoleString(prompt.role));
    provenance.insert(QString::fromUtf8("enabled"), prompt.enabled);
    provenance.insert(QString::fromUtf8("selected"), prompt.selected);
    provenance.insert(QString::fromUtf8("time"), prompt.time);
    provenance.insert(QString::fromUtf8("coordinateSpace"), QString::fromStdString(prompt.coordinateSpace));
    provenance.insert(QString::fromUtf8("label"), QString::fromStdString(prompt.label));
    provenance.insert(QString::fromUtf8("backendTag"), QString::fromStdString(prompt.backendTag));
    provenance.insert(QString::fromUtf8("sourceFrame"), resolvedSourceFrame);
    provenance.insert(QString::fromUtf8("timelineFrame"), resolvedTimelineFrame);
    provenance.insert(QString::fromUtf8("frameOwnershipSource"), frameOwnershipSource);
    provenance.insert(QString::fromUtf8("metadata"), promptMeta);
    converted.insert(QString::fromUtf8("ai_paint_prompt"), provenance);

    // --- Update source_metadata with prompt-local frame values for consistency/debugging ---
    QJsonObject srcMeta = converted.value(QString::fromUtf8("source_metadata")).toObject();
    srcMeta.insert(QString::fromUtf8("current_source_frame"), resolvedSourceFrame);
    srcMeta.insert(QString::fromUtf8("source_frame"), resolvedSourceFrame);
    srcMeta.insert(QString::fromUtf8("timeline_frame"), resolvedTimelineFrame);
    srcMeta.insert(QString::fromUtf8("time_offset"), timeOffset);
    srcMeta.insert(QString::fromUtf8("ai_paint_prompt_frame_source"), frameOwnershipSource);
    converted.insert(QString::fromUtf8("source_metadata"), srcMeta);

    return converted;
}

QJsonArray FluxAiPanel::buildAIPaintPromptsForSam(const std::vector<AIPaintPrompt>& prompts, QString* message) const
{
    QJsonArray converted;
    int points = 0;
    int boxes = 0;
    for (const AIPaintPrompt& p : prompts) {
        if (!p.enabled || (p.type != AIPaintPromptType::Point && p.type != AIPaintPromptType::Box)) {
            continue;
        }
        if (p.role != AIPaintPromptRole::Include) {
            if (message) { *message = QString::fromUtf8("AI Paint exclude/neutral prompts are not supported by this SAM3 path yet"); }
            return QJsonArray();
        }
        QJsonObject one = buildAIPaintPromptForSam(p, message);
        if (one.isEmpty()) {
            return QJsonArray();
        }
        one.insert(QString::fromUtf8("id"), QString::number(p.id));
        converted.append(one);
        if (p.type == AIPaintPromptType::Point) {
            ++points;
        } else {
            ++boxes;
        }
    }

    if (converted.isEmpty()) {
        if (message) { *message = QString::fromUtf8("selected AI Paint effect has no enabled include point/box prompts on the current frame"); }
        return QJsonArray();
    }
    if (message) {
        *message = QString::fromUtf8("SAM3/MatAnyone using %1 current-frame prompt(s): %2 point(s), %3 box(es)")
                   .arg(converted.size())
                   .arg(points)
                   .arg(boxes);
    }
    return converted;
}

bool FluxAiPanel::hasRunnableAIPaintPrompt() const
{
    std::vector<AIPaintPrompt> prompts;
    if (!readAIPaintPrompts(&prompts, nullptr)) {
        return false;
    }
    bool hasIncludePrompt = false;
    for (const AIPaintPrompt& p : prompts) {
        if (!p.enabled || (p.type != AIPaintPromptType::Point && p.type != AIPaintPromptType::Box)) {
            continue;
        }
        if (p.role != AIPaintPromptRole::Include) {
            return false;
        }
        hasIncludePrompt = true;
    }
    return hasIncludePrompt;
}

bool FluxAiPanel::refreshSourceFrameMetadata(QString* message)
{
    _sourceFrameMetadataFresh = false;
    _sourceFramePng.clear();
    _sourceFrameMetadata = QJsonObject();
    _sourceFrameWidth = 0;
    _sourceFrameHeight = 0;
    if (!_gui) {
        if (message) { *message = QString::fromUtf8("source-frame export unavailable: missing GUI"); }
        return false;
    }
    FluxTimeline* timeline = _gui->getFluxTimeline();
    if (!timeline) {
        if (message) { *message = QString::fromUtf8("source-frame export unavailable: missing Flux timeline"); }
        return false;
    }
    const QList<FluxLayer>& layers = timeline->getLayers();
    const int selectedLayerIndex = timeline->getSelectedLayerIndex();
    if (selectedLayerIndex < 0 || selectedLayerIndex >= layers.size()) {
        if (message) { *message = QString::fromUtf8("source-frame export blocked: selected source layer no longer exists"); }
        return false;
    }
    if (selectedLayerIndex != _sourceLayerIndex) {
        if (message) { *message = QString::fromUtf8("source-frame export blocked: selected layer changed; reselect Source Viewer for this layer"); }
        return false;
    }
    const FluxLayer& layer = layers[selectedLayerIndex];
    if (!_sourceLayerName.isEmpty() && layer.name != _sourceLayerName) {
        if (message) { *message = QString::fromUtf8("source-frame export blocked: selected layer name no longer matches source context"); }
        return false;
    }
    if (!_sourceFilePath.isEmpty() && layer.filePath != _sourceFilePath) {
        if (message) { *message = QString::fromUtf8("source-frame export blocked: selected layer source file no longer matches source context"); }
        return false;
    }
    const bool hasLayerReaderNode = static_cast<bool>(layer.readerNode);
    if (!_sourceReaderNode || !_sourceReaderNode->isActivated() || !hasLayerReaderNode || !layer.readerNode->isActivated()) {
        if (message) { *message = QString::fromUtf8("source-frame export blocked: selected layer reader is missing or inactive"); }
        return false;
    }
    if (layer.readerNode != _sourceReaderNode) {
        if (message) { *message = QString::fromUtf8("source-frame export blocked: selected layer reader no longer matches source context"); }
        return false;
    }
    const QString currentReaderLabel = QString::fromStdString(layer.readerNode->getLabel());
    if (!_sourceReaderLabel.isEmpty() && currentReaderLabel != _sourceReaderLabel) {
        if (message) { *message = QString::fromUtf8("source-frame export blocked: selected layer reader label no longer matches source context"); }
        return false;
    }

    const int currentTimelineFrame = timeline->getCurrentFrame();
    const int requestedSourceFrame = currentTimelineFrame - layer.timeOffset;
    const int currentSourceFrame = qBound(layer.originalFirstFrame, requestedSourceFrame, layer.originalLastFrame);
    if (requestedSourceFrame != currentSourceFrame) {
        appendLog(QString::fromUtf8("AI Panel source frame clamped before export: requested=%1; clamped=%2; originalRange=[%3,%4]")
                      .arg(requestedSourceFrame)
                      .arg(currentSourceFrame)
                      .arg(layer.originalFirstFrame)
                      .arg(layer.originalLastFrame));
    }
    _sourceTimelineFrame = currentTimelineFrame;
    _sourceSourceFrame = currentSourceFrame;
    if (_sourceLabel) {
        const QString displayName = _sourceLayerName.isEmpty() ? QString::fromUtf8("<unnamed>") : _sourceLayerName;
        _sourceLabel->setText(QString::fromUtf8("Source: %1\nFrame: timeline %2 / source %3\nFile: %4")
                              .arg(displayName)
                              .arg(_sourceTimelineFrame)
                              .arg(_sourceSourceFrame)
                              .arg(_sourceFilePath));
    }

    QJsonObject metadata;
    const bool hasSourceViewer = _sourceViewer != nullptr;
    const bool hasSourceViewerNode = static_cast<bool>(_sourceViewerNode);
    const bool hasReaderNode = static_cast<bool>(_sourceReaderNode);
    const bool readerActivated = hasReaderNode && _sourceReaderNode->isActivated();
    QString diagnostics = QString::fromUtf8("AI Panel current source-frame export attempted; layerIndex=%1; layerName='%2'; sourceFile='%3'; readerLabel='%4'; readerNodePresent=%5; readerActivated=%6; timelineFrame=%7; sourceFrame=%8; requestedSourceFrame=%9; originalRange=[%10,%11]; sourceViewerPresent=%12; sourceViewerNodePresent=%13")
                              .arg(_sourceLayerIndex)
                              .arg(_sourceLayerName)
                              .arg(_sourceFilePath)
                              .arg(_sourceReaderLabel)
                              .arg(hasReaderNode ? QString::fromUtf8("true") : QString::fromUtf8("false"))
                              .arg(readerActivated ? QString::fromUtf8("true") : QString::fromUtf8("false"))
                              .arg(_sourceTimelineFrame)
                              .arg(_sourceSourceFrame)
                              .arg(requestedSourceFrame)
                              .arg(layer.originalFirstFrame)
                              .arg(layer.originalLastFrame)
                              .arg(hasSourceViewer ? QString::fromUtf8("true") : QString::fromUtf8("false"))
                              .arg(hasSourceViewerNode ? QString::fromUtf8("true") : QString::fromUtf8("false"));
    const QString attemptedDiagnostics = diagnostics;
    const QString png = _gui->exportFluxSam3SourceFrameForSourceContext(_sourceLayerIndex, _sourceLayerName, _sourceFilePath, _sourceReaderNode, _sourceReaderLabel, _sourceTimelineFrame, _sourceSourceFrame, &metadata, &diagnostics);
    if (diagnostics.isEmpty()) {
        diagnostics = attemptedDiagnostics;
    } else if (!diagnostics.contains(attemptedDiagnostics)) {
        diagnostics = attemptedDiagnostics + QString::fromUtf8("; exporterDiagnostics=") + diagnostics;
    }
    QFileInfo pngInfo(png);
    if (!metadata.isEmpty()) {
        const int exportedTimelineFrame = metadata.value(QString::fromUtf8("timeline_frame")).toInt(_sourceTimelineFrame);
        const int exportedSourceFrame = metadata.value(QString::fromUtf8("source_frame")).toInt(_sourceSourceFrame);
        if (exportedTimelineFrame != _sourceTimelineFrame || exportedSourceFrame != _sourceSourceFrame) {
            _sourceTimelineFrame = exportedTimelineFrame;
            _sourceSourceFrame = exportedSourceFrame;
            if (_sourceLabel) {
                const QString displayName = _sourceLayerName.isEmpty() ? QString::fromUtf8("<unnamed>") : _sourceLayerName;
                _sourceLabel->setText(QString::fromUtf8("Source: %1\nFrame: timeline %2 / source %3\nFile: %4")
                                      .arg(displayName)
                                      .arg(_sourceTimelineFrame)
                                      .arg(_sourceSourceFrame)
                                      .arg(_sourceFilePath));
            }
        }
    }
    if (png.isEmpty() || !pngInfo.isFile() || pngInfo.size() <= 0) {
        const QString outputFacts = QString::fromUtf8("; returnedPng='%1'; exists=%2; isFile=%3; size=%4")
                                      .arg(png.isEmpty() ? QString::fromUtf8("<empty>") : png)
                                      .arg(pngInfo.exists() ? QString::fromUtf8("true") : QString::fromUtf8("false"))
                                      .arg(pngInfo.isFile() ? QString::fromUtf8("true") : QString::fromUtf8("false"))
                                      .arg(pngInfo.size());
        if (message) { *message = diagnostics + outputFacts; }
        return false;
    }
    if (!exportedSourceMetadataMatchesSelection(metadata, message)) {
        return false;
    }
    const int width = metadata.value(QString::fromUtf8("width")).toInt(metadata.value(QString::fromUtf8("exported_png_width")).toInt(0));
    const int height = metadata.value(QString::fromUtf8("height")).toInt(metadata.value(QString::fromUtf8("exported_png_height")).toInt(0));
    const int exportedWidth = metadata.value(QString::fromUtf8("exported_png_width")).toInt(width);
    const int exportedHeight = metadata.value(QString::fromUtf8("exported_png_height")).toInt(height);
    if (width <= 0 || height <= 0 || exportedWidth <= 0 || exportedHeight <= 0 || width != exportedWidth || height != exportedHeight) {
        if (message) { *message = QString::fromUtf8("source-frame export metadata dimensions are invalid"); }
        return false;
    }
    _sourceFramePng = png;
    _sourceFrameMetadata = metadata;
    _sourceFrameWidth = width;
    _sourceFrameHeight = height;
    _sourceFrameMetadataFresh = true;
    appendLog(QString::fromUtf8("AI Panel stored-context source-frame export succeeded: path='%1'; size=%2; dimensions=%3x%4; layerIndex=%5; layerName='%6'; sourceFrame=%7; timelineFrame=%8; exportTarget=%9; renderMode=%10")
                  .arg(png)
                  .arg(pngInfo.size())
                  .arg(width)
                  .arg(height)
                  .arg(_sourceLayerIndex)
                  .arg(_sourceLayerName)
                  .arg(_sourceSourceFrame)
                  .arg(_sourceTimelineFrame)
                  .arg(metadata.value(QString::fromUtf8("export_target")).toString(QString::fromUtf8("<unknown>")))
                  .arg(metadata.value(QString::fromUtf8("render_mode")).toString(QString::fromUtf8("<unknown>"))));
    if (message) { *message = QString::fromUtf8("source frame exported for AI Paint prompt inference"); }
    return true;
}

void FluxAiPanel::recomputeSourceFrameFromTimeline(const FluxLayer& layer)
{
    FluxTimeline* timeline = _gui ? _gui->getFluxTimeline() : nullptr;
    if (!timeline) {
        return;
    }
    const int currentTimelineFrame = timeline->getCurrentFrame();
    const int requestedSourceFrame = currentTimelineFrame - layer.timeOffset;
    const int currentSourceFrame = qBound(layer.originalFirstFrame, requestedSourceFrame, layer.originalLastFrame);
    if (requestedSourceFrame != currentSourceFrame) {
        appendLog(QString::fromUtf8("AI Panel source frame clamped: requested=%1; clamped=%2; originalRange=[%3,%4]")
                      .arg(requestedSourceFrame)
                      .arg(currentSourceFrame)
                      .arg(layer.originalFirstFrame)
                      .arg(layer.originalLastFrame));
    }
    _sourceTimelineFrame = currentTimelineFrame;
    _sourceSourceFrame = currentSourceFrame;
    if (_sourceLabel) {
        const QString displayName = _sourceLayerName.isEmpty() ? QString::fromUtf8("<unnamed>") : _sourceLayerName;
        _sourceLabel->setText(QString::fromUtf8("Source: %1\nFrame: timeline %2 / source %3\nFile: %4")
                              .arg(displayName)
                              .arg(_sourceTimelineFrame)
                              .arg(_sourceSourceFrame)
                              .arg(_sourceFilePath));
    }
}

bool FluxAiPanel::refreshLivePreviewFrameFromViewer(QString* message)
{
    _sourceFrameMetadataFresh = false;
    _sourceFramePng.clear();
    _sourceFrameMetadata = QJsonObject();
    _sourceFrameWidth = 0;
    _sourceFrameHeight = 0;

    if (!_gui) {
        if (message) { *message = QString::fromUtf8("viewer image capture unavailable: missing GUI"); }
        return false;
    }

    FluxTimeline* timeline = _gui->getFluxTimeline();
    if (!timeline) {
        if (message) { *message = QString::fromUtf8("viewer image capture unavailable: missing Flux timeline"); }
        return false;
    }

    const int selectedLayerIndex = timeline->getSelectedLayerIndex();
    const QList<FluxLayer>& layers = timeline->getLayers();
    if (selectedLayerIndex < 0 || selectedLayerIndex >= layers.size()) {
        if (message) { *message = QString::fromUtf8("viewer image capture unavailable: selected source layer no longer exists"); }
        return false;
    }
    if (selectedLayerIndex != _sourceLayerIndex) {
        if (message) { *message = QString::fromUtf8("viewer image capture unavailable: selected layer changed; reselect Source Viewer for this layer"); }
        return false;
    }

    // Recompute timeline/source frame from current timeline position before capture
    recomputeSourceFrameFromTimeline(layers[selectedLayerIndex]);

    if (!_sourceViewer) {
        if (message) { *message = QString::fromUtf8("viewer image capture unavailable: viewer not set; wait for viewer to display frame"); }
        return false;
    }
    if (_viewer != _sourceViewer) {
        if (message) { *message = QString::fromUtf8("viewer image capture unavailable: viewer source changed; reselect Source Viewer for this layer"); }
        return false;
    }
    if (!_sourceViewer->displayingImage()) {
        if (message) { *message = QString::fromUtf8("viewer image capture unavailable: viewer not displaying; wait for viewer to display frame"); }
        return false;
    }

    // Render the selected AI Paint node directly at 100% scale. Do not use the
    // ViewerGL cached tiles here: those tiles can be zoom/proxy mipmaps,
    // viewport-cropped, or stale from an earlier graph. SAM3 live preview must
    // infer on the full processed node-tree result at the AI Paint point.
    const unsigned int requestedMipmapLevel = 0;
    ImagePtr imagePtr;
    if (!_sourceAIPaintNode || !_sourceAIPaintNode->isActivated()) {
        if (message) { *message = QString::fromUtf8("processed AI Paint capture failed: selected layer has no active AI Paint node"); }
        return false;
    }
    EffectInstancePtr effect = _sourceAIPaintNode->getEffectInstance();
    if (!effect) {
        if (message) { *message = QString::fromUtf8("processed AI Paint capture failed: AI Paint effect instance is unavailable"); }
        return false;
    }

    RectD rod;
    bool isProjectFormat = false;
    const int renderTime = _sourceTimelineFrame;
    const U64 renderHash = _sourceAIPaintNode->getHashValue();
    StatusEnum stat = effect->getRegionOfDefinition_public(renderHash, renderTime,
                                                            RenderScale::identity, ViewIdx(0),
                                                            &rod, &isProjectFormat);
    if (stat == eStatusFailed || rod.isNull() || rod.x2 <= rod.x1 || rod.y2 <= rod.y1) {
        if (message) { *message = QString::fromUtf8("processed AI Paint capture failed: invalid full-resolution RoD [%1,%2,%3,%4]")
                                   .arg(rod.x1).arg(rod.y1).arg(rod.x2).arg(rod.y2); }
        return false;
    }
    const double par = effect->getAspectRatio(-1);
    const RectI renderWindow = rod.toPixelEnclosing(requestedMipmapLevel, par);
    if (renderWindow.isNull() || renderWindow.width() <= 0 || renderWindow.height() <= 0) {
        if (message) { *message = QString::fromUtf8("processed AI Paint capture failed: invalid full-resolution render window"); }
        return false;
    }

    RenderingFlagSetter flagIsRendering(_sourceAIPaintNode);
    AbortableRenderInfoPtr abortInfo = AbortableRenderInfo::create(true, 0);
    ParallelRenderArgsSetter frameRenderArgs(renderTime,
                                             ViewIdx(0),
                                             true,
                                             false,
                                             abortInfo,
                                             _sourceAIPaintNode,
                                             0,
                                             _sourceAIPaintNode->getApp()->getTimeLine().get(),
                                             _sourceAIPaintNode,
                                             false,
                                             false,
                                             RenderStatsPtr());
    FrameRequestMap request;
    stat = EffectInstance::computeRequestPass(renderTime, ViewIdx(0), requestedMipmapLevel, rod,
                                              _sourceAIPaintNode, request);
    if (stat == eStatusFailed) {
        if (message) { *message = QString::fromUtf8("processed AI Paint capture failed: could not compute render request pass"); }
        return false;
    }
    frameRenderArgs.updateNodesRequest(request);

    std::list<ImagePlaneDesc> requestedComps;
    ImagePlaneDesc plane, pairedPlane;
    effect->getMetadataComponents(-1, &plane, &pairedPlane);
    requestedComps.push_back(plane);
    const ImageBitDepthEnum requestedDepth = effect->getBitDepth(-1);
    std::map<ImagePlaneDesc, ImagePtr> planes;
    try {
        EffectInstance::RenderRoIArgs args(renderTime,
                                           RenderScale::identity,
                                           requestedMipmapLevel,
                                           ViewIdx(0),
                                           false,
                                           renderWindow,
                                           rod,
                                           requestedComps,
                                           requestedDepth,
                                           false,
                                           effect.get(),
                                           eStorageModeRAM,
                                           renderTime);
        const EffectInstance::RenderRoIRetCode retCode = effect->renderRoI(args, &planes);
        if (retCode != EffectInstance::eRenderRoIRetCodeOk || planes.empty()) {
            if (message) { *message = QString::fromUtf8("processed AI Paint capture failed: renderRoI returned no image"); }
            return false;
        }
    } catch (...) {
        if (message) { *message = QString::fromUtf8("processed AI Paint capture failed: renderRoI threw an exception"); }
        return false;
    }
    imagePtr = planes.begin()->second;
    if (!imagePtr) {
        if (message) { *message = QString::fromUtf8("processed AI Paint capture failed: rendered image is null"); }
        return false;
    }

    const Image* const img = imagePtr.get();
    const RectI bounds = img->getBounds();
    if (bounds.isNull() || bounds.width() <= 0 || bounds.height() <= 0) {
        if (message) { *message = QString::fromUtf8("viewer image capture failed: image bounds are null or empty"); }
        return false;
    }

    // Convert Image -> QImage (handles float, byte, short; rejects half-float)
    QString convertMessage;
    const QImage qimg = fluxImageToQImage(img, &convertMessage);
    if (qimg.isNull()) {
        if (message) { *message = QString::fromUtf8("viewer image capture failed: Image-to-QImage conversion failed: %1").arg(convertMessage); }
        return false;
    }

    const int width = qimg.width();
    const int height = qimg.height();

    // Write PNG to temp directory
    const QString previewDirPath = QDir::temp().filePath(QString::fromUtf8("Flux/sam3_preview"));
    QDir previewDir;
    if (!previewDir.mkpath(previewDirPath)) {
        if (message) { *message = QString::fromUtf8("viewer image capture failed: could not create temporary directory %1").arg(previewDirPath); }
        return false;
    }

    QString safeName = _sourceLayerName.isEmpty() ? QString::fromUtf8("layer") : _sourceLayerName;
    safeName.replace(QRegularExpression(QString::fromUtf8("[^A-Za-z0-9_.-]+")), QString::fromUtf8("_"));
    if (safeName.isEmpty() || safeName == QString::fromUtf8(".") || safeName == QString::fromUtf8("..")) {
        safeName = QString::fromUtf8("layer");
    }
    const QString unique = QUuid::createUuid().toString(QUuid::Id128).left(12);
    const QString outputPath = QDir(previewDirPath).filePath(QString::fromUtf8("viewer_img_%1_%2_%3.png").arg(safeName).arg(_sourceTimelineFrame).arg(unique));

    QImageWriter writer(outputPath, QString::fromUtf8("PNG").toUtf8());
    if (!writer.write(qimg)) {
        if (message) { *message = QString::fromUtf8("viewer image capture failed: could not write PNG to %1 (%2)").arg(outputPath, writer.errorString()); }
        QFile::remove(outputPath);
        return false;
    }

    // Build metadata for direct processed-node capture
    const RectD imageRod = img->getRoD();
    const ImageBitDepthEnum bitDepth = img->getBitDepth();
    const unsigned int components = img->getComponentsCount();

    QString bitDepthStr;
    switch (bitDepth) {
    case eImageBitDepthByte:  bitDepthStr = QString::fromUtf8("byte"); break;
    case eImageBitDepthShort: bitDepthStr = QString::fromUtf8("short"); break;
    case eImageBitDepthHalf:  bitDepthStr = QString::fromUtf8("half"); break;
    case eImageBitDepthFloat: bitDepthStr = QString::fromUtf8("float"); break;
    default:                  bitDepthStr = QString::fromUtf8("unknown"); break;
    }

    QJsonObject metadata;
    metadata.insert(QString::fromUtf8("capture_mode"), QString::fromUtf8("node-render-roi"));
    metadata.insert(QString::fromUtf8("export_target"), QString::fromUtf8("viewer_processed_image"));
    metadata.insert(QString::fromUtf8("render_mode"), QString::fromUtf8("direct_render_roi"));
    metadata.insert(QString::fromUtf8("timeline_frame"), _sourceTimelineFrame);
    metadata.insert(QString::fromUtf8("source_frame"), _sourceSourceFrame);
    metadata.insert(QString::fromUtf8("time_offset"), _sourceTimelineFrame - _sourceSourceFrame);
    metadata.insert(QString::fromUtf8("render_time_timeline_frame"), renderTime);
    metadata.insert(QString::fromUtf8("selected_layer_index"), selectedLayerIndex);
    metadata.insert(QString::fromUtf8("selected_layer_name"), _sourceLayerName);
    metadata.insert(QString::fromUtf8("reader_label"), _sourceReaderLabel);
    metadata.insert(QString::fromUtf8("render_node"), QString::fromUtf8(_sourceAIPaintNode->getScriptName().c_str()));
    metadata.insert(QString::fromUtf8("is_project_format"), isProjectFormat);

    // Image bounds (pixel data window): x1=left, y1=bottom, x2=right, y2=top
    QJsonObject boundsObj;
    boundsObj.insert(QString::fromUtf8("x1"), bounds.x1);
    boundsObj.insert(QString::fromUtf8("y1"), bounds.y1);
    boundsObj.insert(QString::fromUtf8("x2"), bounds.x2);
    boundsObj.insert(QString::fromUtf8("y2"), bounds.y2);
    metadata.insert(QString::fromUtf8("image_bounds"), boundsObj);

    // Region of definition (canonical coordinates)
    QJsonObject rodObj;
    rodObj.insert(QString::fromUtf8("x1"), imageRod.x1);
    rodObj.insert(QString::fromUtf8("y1"), imageRod.y1);
    rodObj.insert(QString::fromUtf8("x2"), imageRod.x2);
    rodObj.insert(QString::fromUtf8("y2"), imageRod.y2);
    metadata.insert(QString::fromUtf8("rod"), rodObj);

    // Dimensions from image bounds (matches PNG pixel dimensions)
    metadata.insert(QString::fromUtf8("width"), width);
    metadata.insert(QString::fromUtf8("height"), height);
    metadata.insert(QString::fromUtf8("exported_png_width"), width);
    metadata.insert(QString::fromUtf8("exported_png_height"), height);
    metadata.insert(QString::fromUtf8("bit_depth"), bitDepthStr);
    metadata.insert(QString::fromUtf8("components"), static_cast<int>(components));
    metadata.insert(QString::fromUtf8("requested_mipmap_level"), static_cast<int>(requestedMipmapLevel));

    _sourceFramePng = outputPath;
    _sourceFrameMetadata = metadata;
    _sourceFrameWidth = width;
    _sourceFrameHeight = height;
    _sourceFrameMetadataFresh = true;

    fprintf(stderr,
            "FLUX-SAM3-A1 live preview captured processed AI Paint render: layer=%d name='%s' readerLabel='%s' timelineFrame=%d sourceFrame=%d bounds=[%d,%d,%d,%d] rod=[%g,%g,%g,%g] dimensions=%dx%d bitDepth=%s components=%u output='%s'\n",
            selectedLayerIndex, _sourceLayerName.toStdString().c_str(), _sourceReaderLabel.toStdString().c_str(),
            _sourceTimelineFrame, _sourceSourceFrame,
            bounds.x1, bounds.y1, bounds.x2, bounds.y2,
            imageRod.x1, imageRod.y1, imageRod.x2, imageRod.y2,
            width, height, bitDepthStr.toStdString().c_str(), components, outputPath.toStdString().c_str());
    appendLog(QString::fromUtf8("SAM3 live preview captured 100%% processed AI Paint image: %1x%2 bounds=[%3,%4,%5,%6]")
                  .arg(width).arg(height).arg(bounds.x1).arg(bounds.y1).arg(bounds.x2).arg(bounds.y2));
    if (message) { *message = QString::fromUtf8("100% processed AI Paint image captured for SAM3 live preview"); }
    return true;
}

bool FluxAiPanel::exportedSourceMetadataMatchesSelection(const QJsonObject& metadata, QString* message) const
{
    const QString staleMessage = QString::fromUtf8("source-frame export no longer matches selected source; reselect Source Viewer for this layer");
    const QString selectedLayerIndexKey = QString::fromUtf8("selected_layer_index");
    const QString selectedLayerNameKey = QString::fromUtf8("selected_layer_name");
    const QString readerLabelKey = QString::fromUtf8("reader_label");
    const QString sourceFrameKey = QString::fromUtf8("source_frame");
    const bool matches = metadata.contains(selectedLayerIndexKey) && metadata.value(selectedLayerIndexKey).toInt(-1) == _sourceLayerIndex &&
                         metadata.contains(selectedLayerNameKey) && metadata.value(selectedLayerNameKey).toString() == _sourceLayerName &&
                         metadata.contains(readerLabelKey) && metadata.value(readerLabelKey).toString() == _sourceReaderLabel &&
                         metadata.contains(sourceFrameKey) && metadata.value(sourceFrameKey).toInt(INT_MIN) == _sourceSourceFrame;
    if (!matches) {
        if (message) { *message = staleMessage; }
        return false;
    }
    return true;
}

bool FluxAiPanel::validateSourceCaptureContext(QString* message) const
{
    if (!_viewer || _viewer != _sourceViewer) {
        if (message) { *message = QString::fromUtf8("viewer source changed; reselect Source Viewer for this layer"); }
        return false;
    }
    const QString captureMode = _sourceFrameMetadata.value(QString::fromUtf8("capture_mode")).toString();
    const bool usesImageBoundsCapture = (captureMode == QString::fromUtf8("viewer-last-rendered-image") ||
                                         captureMode == QString::fromUtf8("node-render-roi"));
    const bool isViewerLiveCapture = (usesImageBoundsCapture ||
                                       captureMode == QString::fromUtf8("viewer-current-frame-buffer"));
    if (!_sourceFrameMetadataFresh || _sourceFrameWidth <= 0 || _sourceFrameHeight <= 0) {
        if (message) { *message = QString::fromUtf8("source-frame dimensions unavailable"); }
        return false;
    }
    if (_sourceFrameMetadata.value(QString::fromUtf8("exported_png_width")).toInt(0) != _sourceFrameWidth ||
        _sourceFrameMetadata.value(QString::fromUtf8("exported_png_height")).toInt(0) != _sourceFrameHeight) {
        if (message) { *message = QString::fromUtf8("exported source metadata dimensions are stale"); }
        return false;
    }
    if (isViewerLiveCapture) {
        // Live preview captures: confirm metadata matches current selection and dimensions are valid.
        // No graph topology checks needed beyond viewer object/source selection.
        if (!exportedSourceMetadataMatchesSelection(_sourceFrameMetadata, message)) {
            return false;
        }
        // For image-bound captures, verify image_bounds metadata is present and consistent.
        if (usesImageBoundsCapture) {
            const QJsonObject boundsObj = _sourceFrameMetadata.value(QString::fromUtf8("image_bounds")).toObject();
            if (boundsObj.isEmpty()) {
                if (message) { *message = QString::fromUtf8("processed source capture metadata missing image_bounds"); }
                return false;
            }
            const int bw = boundsObj.value(QString::fromUtf8("x2")).toInt(0) - boundsObj.value(QString::fromUtf8("x1")).toInt(0);
            const int bh = boundsObj.value(QString::fromUtf8("y2")).toInt(0) - boundsObj.value(QString::fromUtf8("y1")).toInt(0);
            if (bw != _sourceFrameWidth || bh != _sourceFrameHeight) {
                if (message) { *message = QString::fromUtf8("processed source bounds dimensions %1x%2 do not match metadata %3x%4")
                                           .arg(bw).arg(bh).arg(_sourceFrameWidth).arg(_sourceFrameHeight); }
                return false;
            }
        }
        return true;
    }
    if (!_sourceViewerNode || !_sourceViewerNode->isActivated()) {
        if (message) { *message = QString::fromUtf8("source viewer node is unavailable"); }
        return false;
    }
    if (!_sourceReaderNode || !_sourceReaderNode->isActivated()) {
        if (message) { *message = QString::fromUtf8("selected reader source is unavailable"); }
        return false;
    }
    const NodePtr viewerInput0 = _sourceViewerNode->getInput(0);
    const bool viewerShowsAIPaint = _sourceAIPaintNode && _sourceAIPaintNode->isActivated() && viewerInput0 == _sourceAIPaintNode;
    if (!viewerShowsAIPaint && viewerInput0 != _sourceReaderNode) {
        if (message) { *message = QString::fromUtf8("AI Work Viewer input 0 no longer matches stored AI Paint node or stored reader"); }
        return false;
    }
    if (!exportedSourceMetadataMatchesSelection(_sourceFrameMetadata, message)) {
        return false;
    }
    return true;
}

bool FluxAiPanel::canonicalPointWithinSourceBounds(const QPointF& canonical, const RectD& format, const RectD& rod, QString* message) const
{
    if (canonical.x() < format.x1 || canonical.x() > format.x2 || canonical.y() < format.y1 || canonical.y() > format.y2 ||
        canonical.x() < rod.x1 || canonical.x() > rod.x2 || canonical.y() < rod.y1 || canonical.y() > rod.y2) {
        if (message) { *message = QString::fromUtf8("prompt is outside source image bounds"); }
        return false;
    }
    return true;
}

bool FluxAiPanel::viewerCanonicalToSourcePixel(const QPointF& canonical, QPointF* sourcePixel, QString* message) const
{
    if (!validateSourceCaptureContext(message)) {
        return false;
    }

    const QString captureMode = _sourceFrameMetadata.value(QString::fromUtf8("capture_mode")).toString();
    const bool usesImageBoundsCapture = (captureMode == QString::fromUtf8("viewer-last-rendered-image") ||
                                         captureMode == QString::fromUtf8("node-render-roi"));

    // For image-bound captures: use image bounds from metadata to map canonical coords
    // into the captured image pixel space. The PNG has top-left origin so output coords are
    // in PNG image pixels — no widget size, pan/zoom, or devicePixelRatio involved.
    if (usesImageBoundsCapture) {
        const QJsonObject boundsObj = _sourceFrameMetadata.value(QString::fromUtf8("image_bounds")).toObject();
        const int bx1 = boundsObj.value(QString::fromUtf8("x1")).toInt(0);
        const int by1 = boundsObj.value(QString::fromUtf8("y1")).toInt(0);
        const int bx2 = boundsObj.value(QString::fromUtf8("x2")).toInt(0);
        const int by2 = boundsObj.value(QString::fromUtf8("y2")).toInt(0);
        const int bw = bx2 - bx1;
        const int bh = by2 - by1;
        if (bw <= 0 || bh <= 0) {
            if (message) { *message = QString::fromUtf8("processed source bounds are invalid [%1,%2,%3,%4]").arg(bx1).arg(by1).arg(bx2).arg(by2); }
            return false;
        }

        // Use RoD from metadata for bounds checking (canonical coordinates)
        const QJsonObject rodObj = _sourceFrameMetadata.value(QString::fromUtf8("rod")).toObject();
        const double rod_x1 = rodObj.value(QString::fromUtf8("x1")).toDouble(0.0);
        const double rod_y1 = rodObj.value(QString::fromUtf8("y1")).toDouble(0.0);
        const double rod_x2 = rodObj.value(QString::fromUtf8("x2")).toDouble(0.0);
        const double rod_y2 = rodObj.value(QString::fromUtf8("y2")).toDouble(0.0);
        const RectD rod(rod_x1, rod_y1, rod_x2, rod_y2);

        // Use image bounds as the format equivalent in canonical space:
        // bounds are in pixel coords, but for a 1:1 pixel-to-canonical image, bounds map directly.
        // If bounds start at (0,0), format is the same as bounds dimensions.
        // For arbitrary bounds offsets, canonical coords map as: canonical_x - bx1 -> pixel_x
        const RectD formatCanonical(static_cast<double>(bx1), static_cast<double>(by1),
                                     static_cast<double>(bx2), static_cast<double>(by2));

        if (!canonicalPointWithinSourceBounds(canonical, formatCanonical, rod, message)) {
            return false;
        }

        // Map canonical to PNG pixel coordinates:
        // PNG row 0 is top, so y=0 in PNG = by2 (top) in Natron, y=(bh-1) in PNG = by1 (bottom) in Natron
        const double px = canonical.x() - static_cast<double>(bx1);
        const double py = static_cast<double>(by2) - canonical.y();
        const double x = std::max(0.0, std::min(static_cast<double>(bw - 1), px));
        const double y = std::max(0.0, std::min(static_cast<double>(bh - 1), py));
        if (sourcePixel) {
            *sourcePixel = QPointF(x, y);
        }
        return true;
    }

    // Legacy path for other capture modes (viewer-current-frame-buffer, exported frames)
    const RectD format = _viewer->getCanonicalFormat(0);
    const RectD rod = _viewer->getRoD(0);
    const double formatWidth = format.x2 - format.x1;
    const double formatHeight = format.y2 - format.y1;
    if (formatWidth <= 0.0 || formatHeight <= 0.0 || rod.x2 <= rod.x1 || rod.y2 <= rod.y1) {
        if (message) {
            *message = QString::fromUtf8("viewer format/RoD is invalid");
        }
        return false;
    }

    const double widthDelta = std::abs(formatWidth - static_cast<double>(_sourceFrameWidth));
    const double heightDelta = std::abs(formatHeight - static_cast<double>(_sourceFrameHeight));
    const bool isViewerFramebuffer = (captureMode == QString::fromUtf8("viewer-current-frame-buffer"));
    if (!isViewerFramebuffer && (widthDelta > 0.01 || heightDelta > 0.01)) {
        if (message) {
            *message = QString::fromUtf8("viewer format %1x%2 does not match exported source %3x%4")
                       .arg(formatWidth).arg(formatHeight).arg(_sourceFrameWidth).arg(_sourceFrameHeight);
        }
        return false;
    }
    if (!canonicalPointWithinSourceBounds(canonical, format, rod, message)) {
        return false;
    }

    const double normalizedX = (canonical.x() - format.x1) / formatWidth;
    const double normalizedY = (format.y2 - canonical.y()) / formatHeight;
    const double x = std::max(0.0, std::min(static_cast<double>(_sourceFrameWidth - 1), normalizedX * _sourceFrameWidth));
    const double y = std::max(0.0, std::min(static_cast<double>(_sourceFrameHeight - 1), normalizedY * _sourceFrameHeight));
    if (sourcePixel) {
        *sourcePixel = QPointF(x, y);
    }
    return true;
}

QJsonObject FluxAiPanel::buildSourcePointPrompt(const QJsonObject& viewerPrompt, QString* message) const
{
    const QJsonObject canonical = viewerPrompt.value(QString::fromUtf8("canonical")).toObject();
    QPointF source;
    if (!viewerCanonicalToSourcePixel(QPointF(canonical.value(QString::fromUtf8("x")).toDouble(),
                                             canonical.value(QString::fromUtf8("y")).toDouble()), &source, message)) {
        return QJsonObject();
    }
    QJsonObject prompt = viewerPrompt;
    QJsonObject sourceFloat{{QString::fromUtf8("x"), source.x()}, {QString::fromUtf8("y"), source.y()}};
    const int sx = std::max(0, std::min(_sourceFrameWidth - 1, static_cast<int>(std::lround(source.x()))));
    const int sy = std::max(0, std::min(_sourceFrameHeight - 1, static_cast<int>(std::lround(source.y()))));
    prompt.insert(QString::fromUtf8("source_float"), sourceFloat);
    prompt.insert(QString::fromUtf8("source_xy"), QJsonObject{{QString::fromUtf8("x"), sx}, {QString::fromUtf8("y"), sy}});
    prompt.insert(QString::fromUtf8("source_width"), _sourceFrameWidth);
    prompt.insert(QString::fromUtf8("source_height"), _sourceFrameHeight);
    prompt.insert(QString::fromUtf8("source_metadata"), _sourceFrameMetadata);
    return prompt;
}

void FluxAiPanel::scheduleAIPaintLivePreview()
{
    ++_livePreviewGeneration;
    if (_livePreviewDebounceTimer) {
        _livePreviewDebounceTimer->start();
    }
}

void FluxAiPanel::runAIPaintLivePreview()
{
    if (_sam3Exporting) {
        return;
    }
    if (!_livePreviewPendingRequestId.isEmpty()) {
        appendLog(QString::fromUtf8("SAM3 live preview deferred: previous preview inference is still running."));
        return;
    }
    for (QMap<QString, QString>::const_iterator it = _sam3WorkerPendingCommands.constBegin(); it != _sam3WorkerPendingCommands.constEnd(); ++it) {
        if (it.value() == QString::fromUtf8("infer_still") && it.key() != _sam3RunPendingRequestId) {
            appendLog(QString::fromUtf8("SAM3 live preview deferred: queued preview inference is still running."));
            return;
        }
    }
    const int generation = _livePreviewGeneration;
    QString message;
    if (!hasSelectedSource() || !_sam3WorkerLoaded || !aipaintLivePreviewEnabled(_sourceAIPaintNode)) {
        return;
    }
    if (!_gui || !_gui->getApp() || !_gui->getApp()->getProject() || !_gui->getApp()->getProject()->hasProjectBeenSavedByUser()) {
        appendLog(QString::fromUtf8("SAM3 live preview skipped: project must be saved for project-relative live cache."));
        return;
    }
    std::vector<AIPaintPrompt> prompts;
    if (!readAIPaintPrompts(&prompts, &message)) {
        appendLog(QString::fromUtf8("SAM3 live preview skipped: ") + message);
        return;
    }
    if (!refreshLivePreviewFrameFromViewer(&message)) {
        appendLog(QString::fromUtf8("SAM3 live preview skipped: ") + message);
        return;
    }
    const QJsonArray samPrompts = buildAIPaintPromptsForSam(prompts, &message);
    if (samPrompts.isEmpty()) {
        appendLog(QString::fromUtf8("SAM3 live preview skipped: ") + message);
        return;
    }
    const QString projectPath = _gui->getApp()->getProject()->getProjectPath();
    const QString relativeRoot = QString::fromUtf8("FluxGenerated/AI/live/sam3_transformers/current/");
    const QString absoluteRoot = QDir(projectPath).filePath(relativeRoot);
    QDir().mkpath(absoluteRoot);
    QJsonObject request;
    request.insert(QString::fromUtf8("command"), QString::fromUtf8("infer_still"));
    request.insert(QString::fromUtf8("image"), _sourceFramePng);
    request.insert(QString::fromUtf8("output_dir"), absoluteRoot);
    request.insert(QString::fromUtf8("source_width"), _sourceFrameWidth);
    request.insert(QString::fromUtf8("source_height"), _sourceFrameHeight);
    request.insert(QString::fromUtf8("prompts"), samPrompts);
    request.insert(QString::fromUtf8("live_preview"), true);
    request.insert(QString::fromUtf8("generation"), generation);
    QImageReader inputReader(_sourceFramePng);
    const QSize inputSize = inputReader.size();
    QJsonObject proof;
    proof.insert(QString::fromUtf8("image"), _sourceFramePng);
    proof.insert(QString::fromUtf8("source_width"), _sourceFrameWidth);
    proof.insert(QString::fromUtf8("source_height"), _sourceFrameHeight);
    proof.insert(QString::fromUtf8("png_width"), inputSize.width());
    proof.insert(QString::fromUtf8("png_height"), inputSize.height());
    proof.insert(QString::fromUtf8("png_bytes"), static_cast<double>(QFileInfo(_sourceFramePng).size()));
    proof.insert(QString::fromUtf8("capture_mode"), _sourceFrameMetadata.value(QString::fromUtf8("capture_mode")).toString());
    writeAiLog(QString::fromUtf8("info"), QString::fromUtf8("sam3"), QString::fromUtf8("live_preview_request_dimensions"),
               (inputSize.width() == _sourceFrameWidth && inputSize.height() == _sourceFrameHeight) ? QString::fromUtf8("pass") : QString::fromUtf8("block"),
               QString::fromUtf8("SAM3 live preview request dimensions checked before worker send."),
               (inputSize.width() == _sourceFrameWidth && inputSize.height() == _sourceFrameHeight) ? QString() : QString::fromUtf8("PNG dimensions do not match declared SAM3 source dimensions."),
               proof);
    if (inputSize.width() != _sourceFrameWidth || inputSize.height() != _sourceFrameHeight) {
        appendLog(QString::fromUtf8("SAM3 live preview skipped: captured PNG %1x%2 does not match declared source %3x%4.")
                      .arg(inputSize.width()).arg(inputSize.height()).arg(_sourceFrameWidth).arg(_sourceFrameHeight));
        return;
    }
    const QString id = sendSam3WorkerRequest(request);
    if (id.isEmpty()) {
        appendLog(QString::fromUtf8("SAM3 live preview request failed to send."));
        return;
    }
    _livePreviewPendingRequestId = id;
    _livePreviewPendingGeneration = generation;
    setAIPaintSam3StatusKnob(_sourceAIPaintNode, QString::fromUtf8("inferring"));
    if (_statusLabel) {
        _statusLabel->setText(QString::fromUtf8("Status: SAM3 live preview inferring"));
    }
}

QJsonObject FluxAiPanel::buildSourceBoxPrompt(const QJsonObject& viewerPrompt, QString* message) const
{
    QPointF sourceMin;
    QPointF sourceMax;
    const QJsonObject cMin = viewerPrompt.value(QString::fromUtf8("canonical_min")).toObject();
    const QJsonObject cMax = viewerPrompt.value(QString::fromUtf8("canonical_max")).toObject();
    if (!viewerCanonicalToSourcePixel(QPointF(cMin.value(QString::fromUtf8("x")).toDouble(), cMin.value(QString::fromUtf8("y")).toDouble()), &sourceMin, message) ||
        !viewerCanonicalToSourcePixel(QPointF(cMax.value(QString::fromUtf8("x")).toDouble(), cMax.value(QString::fromUtf8("y")).toDouble()), &sourceMax, message)) {
        return QJsonObject();
    }
    const double minX = std::min(sourceMin.x(), sourceMax.x());
    const double minY = std::min(sourceMin.y(), sourceMax.y());
    const double maxX = std::max(sourceMin.x(), sourceMax.x());
    const double maxY = std::max(sourceMin.y(), sourceMax.y());
    const int ix1 = std::max(0, std::min(_sourceFrameWidth - 1, static_cast<int>(std::floor(minX))));
    const int iy1 = std::max(0, std::min(_sourceFrameHeight - 1, static_cast<int>(std::floor(minY))));
    const int ix2 = std::max(0, std::min(_sourceFrameWidth - 1, static_cast<int>(std::ceil(maxX))));
    const int iy2 = std::max(0, std::min(_sourceFrameHeight - 1, static_cast<int>(std::ceil(maxY))));
    QJsonObject prompt = viewerPrompt;
    prompt.insert(QString::fromUtf8("source_float_min"), QJsonObject{{QString::fromUtf8("x"), minX}, {QString::fromUtf8("y"), minY}});
    prompt.insert(QString::fromUtf8("source_float_max"), QJsonObject{{QString::fromUtf8("x"), maxX}, {QString::fromUtf8("y"), maxY}});
    prompt.insert(QString::fromUtf8("source_xyxy"), QJsonArray{ix1, iy1, ix2, iy2});
    prompt.insert(QString::fromUtf8("source_width"), _sourceFrameWidth);
    prompt.insert(QString::fromUtf8("source_height"), _sourceFrameHeight);
    prompt.insert(QString::fromUtf8("source_metadata"), _sourceFrameMetadata);
    return prompt;
}

void FluxAiPanel::updatePromptSummary()
{
    if (!_promptLabel) {
        return;
    }
    if (!hasSelectedSource()) {
        _promptLabel->setText(QString::fromUtf8("Prompt summary: no selected source; use Source Viewer on a footage layer with AI Paint"));
        const QString signature = QString::fromUtf8("no-source");
        if (signature != _lastPromptSummarySignature) {
            _lastPromptSummarySignature = signature;
            writeAiLog(QString::fromUtf8("warn"), QString::fromUtf8("aipaint"), QString::fromUtf8("prompt_store_read"),
                       QString::fromUtf8("block"), QString::fromUtf8("No selected source; AI Paint prompts cannot be read."),
                       QString::fromUtf8("Selected layer/source with AI Paint prompt provider."));
        }
        return;
    }
    std::vector<AIPaintPrompt> prompts;
    QString message;
    if (!readAIPaintPrompts(&prompts, &message)) {
        _promptLabel->setText(QString::fromUtf8("Prompt summary: ") + message);
        const QString signature = QString::fromUtf8("read-fail:%1:%2").arg(_sourceTimelineFrame).arg(message);
        if (signature != _lastPromptSummarySignature) {
            _lastPromptSummarySignature = signature;
            QJsonObject found;
            found.insert(QString::fromUtf8("timeline_frame"), _sourceTimelineFrame);
            found.insert(QString::fromUtf8("has_ai_paint_node"), static_cast<bool>(_sourceAIPaintNode));
            found.insert(QString::fromUtf8("reason"), message);
            writeAiLog(QString::fromUtf8("warn"), QString::fromUtf8("aipaint"), QString::fromUtf8("prompt_store_read"),
                       QString::fromUtf8("block"), message,
                       QString::fromUtf8("AI Paint node bound with readable current-frame prompts."), found,
                       QString::fromUtf8("Verify selected layer has AI Paint and that prompt storage is populated."));
        }
        return;
    }
    int points = 0;
    int boxes = 0;
    for (const AIPaintPrompt& p : prompts) {
        if (!p.enabled) { continue; }
        if (p.type == AIPaintPromptType::Point) { ++points; }
        if (p.type == AIPaintPromptType::Box) { ++boxes; }
    }
    int runnablePoints = 0;
    int runnableBoxes = 0;
    bool hasUnsupportedRole = false;
    for (const AIPaintPrompt& p : prompts) {
        if (!p.enabled || (p.type != AIPaintPromptType::Point && p.type != AIPaintPromptType::Box)) {
            continue;
        }
        if (p.role != AIPaintPromptRole::Include) {
            hasUnsupportedRole = true;
            continue;
        }
        if (p.type == AIPaintPromptType::Point) { ++runnablePoints; }
        if (p.type == AIPaintPromptType::Box) { ++runnableBoxes; }
    }
    const int runnablePrompts = runnablePoints + runnableBoxes;
    const QString signature = QString::fromUtf8("frame=%1|total=%2|p=%3|b=%4|rp=%5|rb=%6|unsupported=%7")
            .arg(_sourceTimelineFrame).arg(static_cast<int>(prompts.size())).arg(points).arg(boxes)
            .arg(runnablePoints).arg(runnableBoxes).arg(hasUnsupportedRole);
    if (signature != _lastPromptSummarySignature) {
        _lastPromptSummarySignature = signature;
        QJsonObject found;
        found.insert(QString::fromUtf8("timeline_frame"), _sourceTimelineFrame);
        found.insert(QString::fromUtf8("stored_prompt_count"), static_cast<int>(prompts.size()));
        found.insert(QString::fromUtf8("enabled_points"), points);
        found.insert(QString::fromUtf8("enabled_boxes"), boxes);
        found.insert(QString::fromUtf8("runnable_include_points"), runnablePoints);
        found.insert(QString::fromUtf8("runnable_include_boxes"), runnableBoxes);
        found.insert(QString::fromUtf8("has_unsupported_role"), hasUnsupportedRole);
        writeAiLog(runnablePrompts > 0 ? QString::fromUtf8("info") : QString::fromUtf8("warn"),
                   QString::fromUtf8("aipaint"), QString::fromUtf8("prompt_store_read"),
                   runnablePrompts > 0 ? QString::fromUtf8("pass") : QString::fromUtf8("block"),
                   runnablePrompts > 0 ? QString::fromUtf8("Current-frame AI Paint include prompts are available for SAM3.") : QString::fromUtf8("No runnable current-frame AI Paint include prompts found."),
                   QString::fromUtf8("One or more enabled Include point/box prompts on the current frame."), found,
                   runnablePrompts > 0 ? QString() : QString::fromUtf8("Add an Include point or box prompt on the current timeline frame."));
    }
    if (runnablePrompts <= 0) {
        _promptLabel->setText(QString::fromUtf8("Prompt summary: AI Paint enabled points %1, boxes %2; no enabled include point/box prompts on current frame").arg(points).arg(boxes));
        return;
    }
    _promptLabel->setText(QString::fromUtf8("Prompt summary: AI Paint current-frame prompt set: %1 include prompt(s) (%2 point(s), %3 box(es))%4")
                          .arg(runnablePrompts)
                          .arg(runnablePoints)
                          .arg(runnableBoxes)
                          .arg(hasUnsupportedRole ? QString::fromUtf8("; exclude/neutral prompts block this SAM3 path") : QString()));
}

void FluxAiPanel::onAddMaskClicked()
{
    QString relativeMask;
    QString message;
    QString sequencePattern;
    if (!selectedResultMaskProjectRelative(&relativeMask, &message, &sequencePattern)) {
        _statusLabel->setText(QString::fromUtf8("Status: add mask unavailable"));
        appendLog(message);
        updateUiState();
        return;
    }
    const QString readRelativeMask = sequencePattern.isEmpty() ? relativeMask : sequencePattern;
    QString manifestRelative;
    QString safeManifest;
    if (sanitizeProjectRelativePath(selectedResultManifestProjectRelative(), &safeManifest)) {
        manifestRelative = safeManifest;
    }
    FluxTimeline* timeline = _gui ? _gui->getFluxTimeline() : nullptr;
    if (!timeline || !timeline->addAIMaskCopyToSelectedLayer(relativeMask, manifestRelative, &message, readRelativeMask)) {
        _statusLabel->setText(QString::fromUtf8("Status: add mask failed"));
        appendLog(message.isEmpty() ? QString::fromUtf8("Add Mask failed.") : message);
        updateUiState();
        return;
    }
    _statusLabel->setText(QString::fromUtf8("Status: mask added"));
    appendLog(message);
    updateUiState();
}

void FluxAiPanel::onReplaceMaskClicked()
{
    QString relativeMask;
    QString message;
    QString sequencePattern;
    if (!selectedResultMaskProjectRelative(&relativeMask, &message, &sequencePattern)) {
        _statusLabel->setText(QString::fromUtf8("Status: replace mask unavailable"));
        appendLog(message);
        updateUiState();
        return;
    }
    const QString readRelativeMask = sequencePattern.isEmpty() ? relativeMask : sequencePattern;
    QString manifestRelative;
    QString safeManifest;
    if (sanitizeProjectRelativePath(selectedResultManifestProjectRelative(), &safeManifest)) {
        manifestRelative = safeManifest;
    }
    FluxTimeline* timeline = _gui ? _gui->getFluxTimeline() : nullptr;
    if (!timeline || !timeline->replaceSelectedAIMaskCopy(relativeMask, manifestRelative, &message, readRelativeMask)) {
        _statusLabel->setText(QString::fromUtf8("Status: replace mask failed"));
        appendLog(message.isEmpty() ? QString::fromUtf8("Replace Mask failed: select an AI Mask Copy effect row in the timeline.") : message);
        updateUiState();
        return;
    }
    _statusLabel->setText(QString::fromUtf8("Status: mask replaced"));
    appendLog(message);
    updateUiState();
}

void FluxAiPanel::onPreviewAgainClicked()
{
    QString relativeMask;
    QString message;
    if (!selectedResultMaskProjectRelative(&relativeMask, &message)) {
        _statusLabel->setText(QString::fromUtf8("Status: preview unavailable"));
        appendLog(message);
        updateUiState();
        return;
    }
    previewSam3RunResult(relativeMask);
}

void FluxAiPanel::onRemoveHistoryEntryClicked()
{
    QString selectedManifest = selectedResultManifestProjectRelative();
    if (selectedManifest.isEmpty()) {
        updateUiState();
        return;
    }
    QString safeManifest;
    if (!sanitizeProjectRelativePath(selectedManifest, &safeManifest)) {
        appendLog(tr("AI result history entry rejected: unsafe project-relative path: %1").arg(selectedManifest));
        updateUiState();
        return;
    }
    const int row = _resultHistoryList ? _resultHistoryList->currentRow() : -1;
    _resultManifestHistoryProjectRelative.removeAll(safeManifest);
    QString nextSelection;
    if (!_resultManifestHistoryProjectRelative.isEmpty()) {
        const int lastRow = static_cast<int>(_resultManifestHistoryProjectRelative.size()) - 1;
        const int nextRow = std::max(0, std::min(row, lastRow));
        nextSelection = _resultManifestHistoryProjectRelative.at(nextRow);
    }
    _lastResultManifestProjectRelative = nextSelection;
    refreshResultHistoryList(nextSelection);
    updateUiState();
}

void FluxAiPanel::onSam3ReadyReadStandardOutput()
{
    QProcess* process = qobject_cast<QProcess*>(sender());
    if (process == _sam3WorkerProcess) {
        _sam3WorkerStdoutBuffer.append(_sam3WorkerProcess->readAllStandardOutput());
        int newline = _sam3WorkerStdoutBuffer.indexOf('\n');
        while (newline >= 0) {
            const QByteArray line = _sam3WorkerStdoutBuffer.left(newline).trimmed();
            _sam3WorkerStdoutBuffer.remove(0, newline + 1);
            if (!line.isEmpty()) {
                processSam3WorkerLine(line);
            }
            newline = _sam3WorkerStdoutBuffer.indexOf('\n');
        }
        return;
    }
    _sam3StdoutBuffer.append(QString::fromUtf8(_sam3Process->readAllStandardOutput()));
}

void FluxAiPanel::onSam3ReadyReadStandardError()
{
    QProcess* process = qobject_cast<QProcess*>(sender());
    const QString text = QString::fromUtf8(process ? process->readAllStandardError() : _sam3Process->readAllStandardError()).trimmed();
    if (!text.isEmpty()) {
        appendLog(QString::fromUtf8(process == _sam3WorkerProcess ? "sam3 worker stderr: " : "sam3 stderr: ") + text);
    }
}

void FluxAiPanel::onSam3Finished(int exitCode, QProcess::ExitStatus exitStatus)
{
    QProcess* process = qobject_cast<QProcess*>(sender());
    if (process == _sam3WorkerProcess) {
        const bool hadPendingRun = !_sam3RunPendingRequestId.isEmpty();
        _sam3RunPendingRequestId.clear();
        _sam3WorkerPendingCommands.clear();
        _sam3WorkerLoading = false;
        _sam3WorkerUnloading = false;
        _sam3WorkerLoaded = false;
        if (hadPendingRun) {
            appendLog(tr("SAM3 inference failed: persistent worker exited before completion."));
            if (_statusLabel) {
                _statusLabel->setText(QString::fromUtf8("Status: failure: SAM3 worker exited before inference completed"));
            }
            resetProgressBar();
        }
        setAIPaintSam3StatusKnob(_sourceAIPaintNode, QString::fromUtf8("unloaded"));
        appendLog(tr("SAM3 persistent worker exited with code %1.").arg(exitCode));
        updateUiState();
        return;
    }
    _sam3StdoutBuffer.append(QString::fromUtf8(_sam3Process->readAllStandardOutput()));
    onRunningChanged(false);
    if (_sam3CancelRequested) {
        _statusLabel->setText(QString::fromUtf8("Status: canceled"));
        resetProgressBar();
        appendLog(QString::fromUtf8("SAM3 generation canceled."));
        return;
    }
    if (exitStatus != QProcess::NormalExit || exitCode != 0) {
        _statusLabel->setText(QString::fromUtf8("Status: failure"));
        resetProgressBar();
        appendLog(tr("SAM3 probe failed with exit code %1.").arg(exitCode));
        appendLog(_sam3StdoutBuffer.trimmed());
        return;
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(_sam3StdoutBuffer.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        QFile resultFile(QDir(_sam3AbsoluteRoot).filePath(QString::fromUtf8("sam3_transformers_real_inference_result.json")));
        if (resultFile.open(QIODevice::ReadOnly)) {
            doc = QJsonDocument::fromJson(resultFile.readAll(), &parseError);
        }
    }
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        _statusLabel->setText(QString::fromUtf8("Status: failure"));
        resetProgressBar();
        appendLog(tr("SAM3 probe result JSON could not be parsed: %1").arg(parseError.errorString()));
        return;
    }

    QString message;
    const bool ok = verifySam3ResultAndWriteManifest(doc.object(), &message);
    _statusLabel->setText(ok ? QString::fromUtf8("Status: success") : QString::fromUtf8("Status: failure"));
    if (ok) {
        setProgressBarValue(100, QString::fromUtf8("Complete"));
    } else {
        resetProgressBar();
    }
    appendLog(message);
    if (ok) {
        const QString key = _sam3Prompt.value(QString::fromUtf8("type")).toString();
        previewSam3RunResult(_sam3RelativeRoot + QString::fromUtf8("mask_%1.png").arg(key));
    }
    updateUiState();
}

void FluxAiPanel::onSam3ErrorOccurred(QProcess::ProcessError error)
{
    appendLog(tr("SAM3 process error: %1").arg(static_cast<int>(error)));
}

void FluxAiPanel::onMatAnyone2ErrorOccurred(QProcess::ProcessError error)
{
    appendLog(tr("MatAnyone2 worker process error: %1").arg(static_cast<int>(error)));
    _matAnyone2RunPendingRequestId.clear();
    _matAnyone2PendingInferRequest = QJsonObject();
    _matAnyone2Running = false;
    _matAnyone2Exporting = false;
    resetProgressBar();
    if (_statusLabel) {
        _statusLabel->setText(QString::fromUtf8("Status: MatAnyone2 worker error"));
    }
    updateUiState();
}

void FluxAiPanel::cancelSam3()
{
    if (_sam3Exporting) {
        _sam3CancelRequested = true;
        if (_statusLabel) {
            _statusLabel->setText(QString::fromUtf8("Status: canceling export"));
        }
        setProgressBarBusy(QString::fromUtf8("Canceling export"));
        appendLog(QString::fromUtf8("Cancel requested during source-sequence export."));
        return;
    }
    if (_matAnyone2Exporting) {
        _matAnyone2CancelRequested = true;
        if (_statusLabel) {
            _statusLabel->setText(QString::fromUtf8("Status: canceling MatAnyone2 export"));
        }
        setProgressBarBusy(QString::fromUtf8("Canceling MatAnyone2 export"));
        appendLog(QString::fromUtf8("Cancel requested during MatAnyone2 source-sequence export."));
        return;
    }
    if (_videoMamaExporting) {
        _videoMamaCancelRequested = true;
        if (_statusLabel) {
            _statusLabel->setText(QString::fromUtf8("Status: canceling VideoMaMa export"));
        }
        setProgressBarBusy(QString::fromUtf8("Canceling VideoMaMa export"));
        appendLog(QString::fromUtf8("Cancel requested during VideoMaMa source-sequence export."));
        return;
    }
    if (_sam3WorkerProcess && _sam3WorkerProcess->state() != QProcess::NotRunning && !_sam3WorkerPendingCommands.isEmpty()) {
        QJsonObject cancel;
        cancel.insert(QString::fromUtf8("command"), QString::fromUtf8("cancel"));
        cancel.insert(QString::fromUtf8("target_id"), _sam3WorkerPendingCommands.firstKey());
        sendSam3WorkerRequest(cancel);
        appendLog(QString::fromUtf8("SAM3 worker cancel requested."));
        setProgressBarBusy(QString::fromUtf8("Canceling"));
    }
    if (_matAnyone2WorkerProcess && _matAnyone2WorkerProcess->state() != QProcess::NotRunning && !_matAnyone2WorkerPendingCommands.isEmpty()) {
        QJsonObject cancel;
        cancel.insert(QString::fromUtf8("command"), QString::fromUtf8("cancel"));
        cancel.insert(QString::fromUtf8("target_id"), _matAnyone2WorkerPendingCommands.firstKey());
        sendMatAnyone2WorkerRequest(cancel);
        appendLog(QString::fromUtf8("MatAnyone2 worker cancel requested."));
        setProgressBarBusy(QString::fromUtf8("Canceling MatAnyone2"));
    }
    if (_videoMamaWorkerProcess && _videoMamaWorkerProcess->state() != QProcess::NotRunning && !_videoMamaWorkerPendingCommands.isEmpty()) {
        // VideoMaMa pipeline.run() is synchronous and blocks stdin reading,
        // so a soft cancel command cannot interrupt in-flight inference.
        // Hard-kill the worker process to stop immediately.
        appendLog(QString::fromUtf8("VideoMaMa cancel: terminating worker process (hard cancel — in-flight diffusion cannot be interrupted via stdin)."));
        _videoMamaWorkerProcess->terminate();
        if (!_videoMamaWorkerProcess->waitForFinished(2000)) {
            appendLog(QString::fromUtf8("VideoMaMa cancel: terminate timeout, killing worker process."));
            _videoMamaWorkerProcess->kill();
            _videoMamaWorkerProcess->waitForFinished(1000);
        }
        _videoMamaRunning = false;
        _videoMamaRunPendingRequestId.clear();
        _videoMamaPendingInferRequest = QJsonObject();
        _videoMamaWorkerPendingCommands.clear();
        _videoMamaWorkerStdoutBuffer.clear();
        if (_statusLabel) { _statusLabel->setText(QString::fromUtf8("Status: canceled")); }
        setProgressBarValue(0, QString::fromUtf8("Canceled"));
        appendLog(QString::fromUtf8("VideoMaMa worker canceled by user."));
        updateUiState();
        return;
    }
    if (!_sam3Process || _sam3Process->state() == QProcess::NotRunning) {
        return;
    }
    _sam3CancelRequested = true;
    appendLog(QString::fromUtf8("Cancel requested."));
    _sam3Process->terminate();
}

void FluxAiPanel::appendLog(const QString& text)
{
    if (!text.isEmpty()) {
        if (_log) {
            _log->appendPlainText(text);
        }
        QJsonObject found;
        found.insert(QString::fromUtf8("ui_log_text"), text);
        writeAiLog(QString::fromUtf8("info"), QString::fromUtf8("panel"), QString::fromUtf8("ui_log_message"),
                   QString::fromUtf8("pass"), text, QString(), found);
    }
}

void FluxAiPanel::toggleLog()
{
    const bool show = !_log->isVisible();
    _log->setVisible(show);
    _logToggleButton->setText(show ? tr("Hide Log") : tr("Show Log"));
}

void FluxAiPanel::onModelChanged()
{
    const QString modelId = _modelCombo ? _modelCombo->currentData().toString() : QString();
    if (modelId == QString::fromUtf8("matanyone2") || modelId == QString::fromUtf8("videomama")) {
        const QString path = manifestPath();
        QFile file(path);
        QString warningText = QString::fromUtf8("NON-COMMERCIAL");
        if (file.open(QIODevice::ReadOnly)) {
            QJsonParseError parseError;
            const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
            if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
                const QJsonArray models = doc.object().value(QString::fromUtf8("models")).toArray();
                for (const QJsonValue& value : models) {
                    const QJsonObject model = value.toObject();
                    if (model.value(QString::fromUtf8("id")).toString() == modelId) {
                        const QString manifestWarning = model.value(QString::fromUtf8("warning_text")).toString();
                        if (!manifestWarning.isEmpty()) {
                            warningText = manifestWarning;
                        }
                        break;
                    }
                }
            }
        }
        if (_statusLabel) { _statusLabel->setText(QString::fromUtf8("Status: ") + warningText); }
        appendLog(warningText);
    }
    updateUiState();
}

void FluxAiPanel::onRunClicked()
{
    const QString modelId = _modelCombo ? _modelCombo->currentData().toString() : QString();
    if (modelId == QString::fromUtf8("matanyone2")) {
        runMatAnyone2();
        return;
    }
    if (modelId == QString::fromUtf8("videomama")) {
        runVideoMama();
        return;
    }
    if (modelId != QString::fromUtf8("sam3_transformers")) {
        appendLog(tr("Cannot start AI worker: select SAM3."));
        updateUiState();
        return;
    }
    if (!hasSelectedSource()) {
        appendLog(tr("Cannot start AI worker: no source is selected."));
        if (_statusLabel) { _statusLabel->setText(QString::fromUtf8("Status: select a source first")); }
        updateUiState();
        return;
    }
    if (!_gui || !_gui->getApp() || !_gui->getApp()->getProject()) {
        appendLog(tr("Cannot start AI worker: project is unavailable."));
        updateUiState();
        return;
    }

    ProjectPtr project = _gui->getApp()->getProject();
    if (!project->hasProjectBeenSavedByUser()) {
        if (!_gui->saveProjectAs()) {
            const QString canceled = QString::fromUtf8("AI generation canceled: project must be saved first.");
            if (_statusLabel) { _statusLabel->setText(QString::fromUtf8("Status: ") + canceled); }
            appendLog(canceled);
            return;
        }
    }
    if (!project->hasProjectBeenSavedByUser()) {
        const QString canceled = QString::fromUtf8("AI generation canceled: project must be saved first.");
        if (_statusLabel) { _statusLabel->setText(QString::fromUtf8("Status: ") + canceled); }
        appendLog(canceled);
        return;
    }

    QString message;
    int rangeStart = 0;
    int rangeEnd = 0;
    if (!selectedFrameRange(&rangeStart, &rangeEnd, &message)) {
        if (_statusLabel) { _statusLabel->setText(QString::fromUtf8("Status: frame range blocked")); }
        appendLog(message);
        updateUiState();
        return;
    }
    const bool videoRun = rangeEnd > rangeStart;
    // Both still and video SAM3 runs use the processed AI Paint render path
    // (node-render-roi). Video runs render each frame's processed AI Paint
    // result via direct renderRoI — no reader/ffmpeg source involved.
    const bool sourceReady = refreshLivePreviewFrameFromViewer(&message) && validateSourceCaptureContext(&message);
    if (!sourceReady) {
        if (_statusLabel) { _statusLabel->setText(QString::fromUtf8("Status: source capture blocked")); }
        appendLog(message);
        updateUiState();
        return;
    }
    QJsonObject sourceMetadata = _sourceFrameMetadata;

    // Compute timeline range from source-frame range for prompt lookup
    FluxTimeline* fluxTimeline = _gui->getFluxTimeline();
    const QList<FluxLayer>& layers = fluxTimeline ? fluxTimeline->getLayers() : QList<FluxLayer>();
    const int timeOffset = (_sourceLayerIndex >= 0 && _sourceLayerIndex < layers.size())
                           ? layers[_sourceLayerIndex].timeOffset : (_sourceTimelineFrame - _sourceSourceFrame);
    const int timelineRangeStart = rangeStart + timeOffset;
    const int timelineRangeEnd = rangeEnd + timeOffset;

    const QJsonObject frameRangeMetadata = selectedFrameRangeMetadata();
    sourceMetadata.insert(QString::fromUtf8("sam3_frame_range"), frameRangeMetadata);
    _sourceFrameMetadata = sourceMetadata;
    QJsonObject sequenceMetadata;
    if (videoRun) {
        _sam3Exporting = true;
        _sam3CancelRequested = false;
        if (_statusLabel) {
            _statusLabel->setText(QString::fromUtf8("Status: exporting processed AI Paint frames"));
        }
        setProgressBarValue(0, QString::fromUtf8("Exporting processed frames"));
        updateUiState();

        auto exportProgressCallback = [this](int completed, int total) -> bool {
            const int percent = total > 0 ? static_cast<int>((static_cast<double>(completed) / static_cast<double>(total)) * 100.0) : 0;
            setProgressBarValue(percent, QString::fromUtf8("Exporting processed frames %1/%2").arg(completed).arg(total));
            QCoreApplication::processEvents();
            return !_sam3CancelRequested;
        };

        QString sequenceDiagnostics;
        const QString sequenceDir = exportProcessedAIPaintSequence(rangeStart, rangeEnd, &sequenceMetadata, &sequenceDiagnostics, exportProgressCallback);

        _sam3Exporting = false;

        if (sequenceDir.isEmpty() || sequenceMetadata.value(QString::fromUtf8("frames")).toArray().isEmpty()) {
            if (_sam3CancelRequested) {
                if (_statusLabel) { _statusLabel->setText(QString::fromUtf8("Status: canceled")); }
                setProgressBarValue(0, QString::fromUtf8("Canceled"));
                appendLog(QString::fromUtf8("SAM3 processed sequence export canceled."));
            } else {
                if (_statusLabel) { _statusLabel->setText(QString::fromUtf8("Status: processed sequence blocked")); }
                appendLog(sequenceDiagnostics.isEmpty() ? QString::fromUtf8("processed sequence export failed") : sequenceDiagnostics);
            }
            updateUiState();
            return;
        }
        sourceMetadata.insert(QString::fromUtf8("sam3_source_sequence"), sequenceMetadata);

        // Set source metadata to sequence dimensions before building prompts
        // so buildAIPaintPromptForSam picks up correct width/height/time_offset
        _sourceFrameMetadata = sequenceMetadata;
        _sourceFrameMetadata.insert(QString::fromUtf8("sam3_frame_range"), frameRangeMetadata);
        _sourceFrameMetadata.insert(QString::fromUtf8("sam3_source_sequence"), sequenceMetadata);
        _sourceFrameWidth = sequenceMetadata.value(QString::fromUtf8("width")).toInt(0);
        _sourceFrameHeight = sequenceMetadata.value(QString::fromUtf8("height")).toInt(0);

        appendLog(QString::fromUtf8("SAM3 processed AI Paint sequence exported: %1 frames, %2x%3, capture_mode=%4, render_mode=%5 (no reader_source, no external-source-ffmpeg)")
                      .arg(sequenceMetadata.value(QString::fromUtf8("frames")).toArray().size())
                      .arg(_sourceFrameWidth)
                      .arg(_sourceFrameHeight)
                      .arg(sequenceMetadata.value(QString::fromUtf8("capture_mode")).toString())
                      .arg(sequenceMetadata.value(QString::fromUtf8("render_mode")).toString()));
    }

    // Collect prompts: for video runs, gather all enabled Include prompts in the
    // selected timeline range. For still runs, use current-frame prompts.
    std::vector<AIPaintPrompt> prompts;
    if (videoRun) {
        if (!readAIPaintPromptsInRange(timelineRangeStart, timelineRangeEnd, &prompts, &message)) {
            if (_statusLabel) { _statusLabel->setText(QString::fromUtf8("Status: prompt read failed")); }
            appendLog(message);
            updateUiState();
            return;
        }
        if (prompts.empty()) {
            if (_statusLabel) { _statusLabel->setText(QString::fromUtf8("Status: no prompts in range")); }
            appendLog(QString::fromUtf8("No enabled Include point/box prompts found in timeline range %1-%2. "
                                         "Draw at least one prompt on a frame within the selected range.")
                          .arg(timelineRangeStart).arg(timelineRangeEnd));
            updateUiState();
            return;
        }
    } else {
        if (!readAIPaintPrompts(&prompts, &message)) {
            if (_statusLabel) { _statusLabel->setText(QString::fromUtf8("Status: prompt required")); }
            appendLog(message);
            updateUiState();
            return;
        }
    }
    const QJsonArray samPrompts = buildAIPaintPromptsForSam(prompts, &message);
    if (samPrompts.isEmpty()) {
        if (_statusLabel) { _statusLabel->setText(QString::fromUtf8("Status: prompt required")); }
        appendLog(message);
        updateUiState();
        return;
    }
    // Free GPU and drop queued/stale live-preview requests before a full SAM3
    // video run. Live preview uses the same persistent worker stdin; without a
    // fresh worker, stale infer_still requests can continue executing before the
    // real video run and keep PyTorch allocations alive.
    if (videoRun) {
        if (_livePreviewDebounceTimer) {
            _livePreviewDebounceTimer->stop();
        }
        stopSam3PersistentWorker();
        stopMatAnyone2Worker();
        stopVideoMamaWorker();
    }
    if (!ensureSam3WorkerStarted(&message)) {
        if (_statusLabel) { _statusLabel->setText(QString::fromUtf8("Status: failure")); }
        appendLog(message);
        updateUiState();
        return;
    }

    const QString runId = QString::fromUtf8("run-") + QDateTime::currentDateTimeUtc().toString(QString::fromUtf8("yyyyMMdd-HHmmss"));
    const QString slug = taskSlug(_taskCombo ? _taskCombo->currentText() : QString::fromUtf8("matte"));
    const QString relativeRoot = QString::fromUtf8("FluxGenerated/AI/%1/%2/%3/").arg(slug, modelId, runId);
    const QString absoluteRoot = QDir(project->getProjectPath()).filePath(relativeRoot);
    if (!QDir().mkpath(absoluteRoot)) {
        if (_statusLabel) { _statusLabel->setText(QString::fromUtf8("Status: failure")); }
        appendLog(tr("Could not create AI output directory: %1").arg(absoluteRoot));
        updateUiState();
        return;
    }

    _sam3AbsoluteRoot = absoluteRoot;
    _sam3RelativeRoot = relativeRoot;
    _sam3RunId = runId;
    _sam3Task = _taskCombo ? _taskCombo->currentText() : QString();
    _sam3SourcePng = _sourceFramePng;
    _sam3SourceMetadata = sourceMetadata;
    _sam3Prompts = samPrompts;
    _sam3Prompt = samPrompts.size() == 1 ? samPrompts.at(0).toObject() : QJsonObject{{QString::fromUtf8("type"), QString::fromUtf8("multi")}, {QString::fromUtf8("count"), samPrompts.size()}};
    _sam3CancelRequested = false;
    _lastResultManifestProjectRelative.clear();

    QJsonObject request;
    request.insert(QString::fromUtf8("command"), videoRun ? QString::fromUtf8("infer_video") : QString::fromUtf8("infer_still"));
    request.insert(QString::fromUtf8("image"), _sourceFramePng);
    request.insert(QString::fromUtf8("output_dir"), absoluteRoot);
    request.insert(QString::fromUtf8("source_width"), _sourceFrameWidth);
    request.insert(QString::fromUtf8("source_height"), _sourceFrameHeight);
    request.insert(QString::fromUtf8("frame_range"), frameRangeMetadata);
    request.insert(QString::fromUtf8("source_range_start"), rangeStart);
    request.insert(QString::fromUtf8("source_range_end"), rangeEnd);
    if (videoRun) {
        request.insert(QString::fromUtf8("frames"), sequenceMetadata.value(QString::fromUtf8("frames")).toArray());
        request.insert(QString::fromUtf8("source_width"), sequenceMetadata.value(QString::fromUtf8("width")).toInt(_sourceFrameWidth));
        request.insert(QString::fromUtf8("source_height"), sequenceMetadata.value(QString::fromUtf8("height")).toInt(_sourceFrameHeight));
        request.insert(QString::fromUtf8("source_sequence"), sequenceMetadata);
    }
    request.insert(QString::fromUtf8("prompts"), samPrompts);
    request.insert(QString::fromUtf8("live_preview"), false);
    request.insert(QString::fromUtf8("run_id"), runId);
    const QString id = sendSam3WorkerRequest(request);
    if (id.isEmpty()) {
        if (_statusLabel) { _statusLabel->setText(QString::fromUtf8("Status: failure")); }
        appendLog(QString::fromUtf8("SAM3 Run failed: persistent worker request could not be sent."));
        updateUiState();
        return;
    }
    _sam3RunPendingRequestId = id;
    if (_outputLabel) { _outputLabel->setText(QString::fromUtf8("Output: ") + relativeRoot); }
    if (_statusLabel) { _statusLabel->setText(QString::fromUtf8("Status: SAM3 running")); }
    setProgressBarValue(0, QString::fromUtf8("Starting SAM3"));
    appendLog(QString::fromUtf8("Starting SAM3 persistent worker %1 inference: %2; sourceRange=%3-%4")
                  .arg(videoRun ? QString::fromUtf8("video") : QString::fromUtf8("still"))
                  .arg(runId)
                  .arg(rangeStart)
                  .arg(rangeEnd));
    updateUiState();
}


void FluxAiPanel::onRunningChanged(bool running)
{
    Q_UNUSED(running);
    updateUiState();
}

void FluxAiPanel::onWorkerProgress(const QString& text)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &parseError);
    const QJsonObject obj = (parseError.error == QJsonParseError::NoError && doc.isObject()) ? doc.object() : QJsonObject();
    if (!obj.isEmpty()) {
        const QString manifestPath = obj.value(QString::fromUtf8("result_manifest_path_project_relative")).toString();
        if (!manifestPath.isEmpty()) {
            _lastResultManifestProjectRelative = manifestPath;
        }
        const QString event = obj.value(QString::fromUtf8("event")).toString();
        if (event == QString::fromUtf8("progress")) {
            const QString message = obj.value(QString::fromUtf8("message")).toString(text);
            _statusLabel->setText(QString::fromUtf8("Status: ") + message);
            updateProgressBarFromPayload(obj, message);
        } else if (event == QString::fromUtf8("started")) {
            _statusLabel->setText(QString::fromUtf8("Status: running"));
            setProgressBarBusy(QString::fromUtf8("Running"));
        }
    }
    appendLog(text);
}

void FluxAiPanel::onWorkerFinished(bool success, const QString& message)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8(), &parseError);
    if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
        const QString manifestPath = doc.object().value(QString::fromUtf8("result_manifest_path_project_relative")).toString();
        if (!manifestPath.isEmpty()) {
            _lastResultManifestProjectRelative = manifestPath;
        }
    }
    _statusLabel->setText(success ? QString::fromUtf8("Status: success") : QString::fromUtf8("Status: failure"));
    if (success) {
        setProgressBarValue(100, QString::fromUtf8("Complete"));
    } else {
        resetProgressBar();
    }
    appendLog(message);
    updateUiState();
}

bool FluxAiPanel::verifySam3ResultAndWriteManifest(const QJsonObject& probeResult, QString* message, QString* selectedRelativeMask)
{
    if (!_gui || !_gui->getApp() || !_gui->getApp()->getProject()) {
        if (message) { *message = QString::fromUtf8("SAM3 result verification failed: project is unavailable."); }
        return false;
    }

    const QString projectPath = _gui->getApp()->getProject()->getProjectPath();
    const QDir projectDir(projectPath);
    auto pathToProjectRelative = [&](const QString& path, QString* relative) -> bool {
        if (path.isEmpty()) {
            return false;
        }
        QString candidate = path;
        if (QDir::isAbsolutePath(candidate)) {
            candidate = projectDir.relativeFilePath(QDir::cleanPath(candidate));
        }
        return sanitizeProjectRelativePath(candidate, relative);
    };

    const QJsonObject result = probeResult.value(QString::fromUtf8("result")).toObject(probeResult);
    const QJsonObject resultMasks = result.value(QString::fromUtf8("masks")).toObject();
    QString selectedMaskPath = result.value(QString::fromUtf8("selected_mask_path")).toString();
    if (selectedMaskPath.isEmpty()) {
        selectedMaskPath = resultMasks.value(QString::fromUtf8("combined")).toString();
    }
    if (selectedMaskPath.isEmpty()) {
        for (QJsonObject::const_iterator it = resultMasks.constBegin(); it != resultMasks.constEnd(); ++it) {
            selectedMaskPath = it.value().toString();
            if (!selectedMaskPath.isEmpty()) {
                break;
            }
        }
    }

    QString selectedMaskRelative;
    if (!pathToProjectRelative(selectedMaskPath, &selectedMaskRelative)) {
        if (message) { *message = QString::fromUtf8("SAM3 result has no safe selected mask path."); }
        return false;
    }
    QFileInfo selectedMaskInfo(projectDir.filePath(selectedMaskRelative));
    if (!selectedMaskInfo.isFile() || selectedMaskInfo.size() <= 0) {
        if (message) { *message = tr("SAM3 selected mask is missing or empty: %1").arg(selectedMaskRelative); }
        return false;
    }

    QJsonObject manifestMasks;
    for (QJsonObject::const_iterator it = resultMasks.constBegin(); it != resultMasks.constEnd(); ++it) {
        QString relative;
        if (pathToProjectRelative(it.value().toString(), &relative)) {
            manifestMasks.insert(it.key(), relative);
        }
    }
    if (manifestMasks.isEmpty()) {
        manifestMasks.insert(QString::fromUtf8("selected"), selectedMaskRelative);
    }

    const QJsonObject proofs = result.value(QString::fromUtf8("proofs")).toObject();
    QJsonObject proofMetrics;
    bool hasNonzeroProof = false;
    for (QJsonObject::const_iterator it = proofs.constBegin(); it != proofs.constEnd(); ++it) {
        const QJsonObject proof = it.value().toObject();
        const int nonzero = proof.value(QString::fromUtf8("nonzero_pixels")).toInt(0);
        if (proof.value(QString::fromUtf8("status")).toString() == QString::fromUtf8("succeeded") && nonzero > 0) {
            hasNonzeroProof = true;
        }
        proofMetrics.insert(it.key(), nonzero);
    }
    if (!hasNonzeroProof) {
        if (message) { *message = QString::fromUtf8("SAM3 result has no successful nonzero prompt proof."); }
        return false;
    }

    QString resultJsonRelative;
    const QString resultPath = result.value(QString::fromUtf8("result_path")).toString(probeResult.value(QString::fromUtf8("result_path")).toString());
    pathToProjectRelative(resultPath, &resultJsonRelative);
    if (resultJsonRelative.isEmpty()) {
        resultJsonRelative = _sam3RelativeRoot + QString::fromUtf8("sam3_transformers_real_inference_result.json");
    }
    QString selectedSequencePatternRelative;
    pathToProjectRelative(result.value(QString::fromUtf8("selected_mask_sequence_pattern")).toString(), &selectedSequencePatternRelative);

    QJsonObject manifest;
    manifest.insert(QString::fromUtf8("schema"), QString::fromUtf8("flux.ai.result_manifest.v1"));
    manifest.insert(QString::fromUtf8("created_at_utc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    manifest.insert(QString::fromUtf8("run_id"), _sam3RunId);
    manifest.insert(QString::fromUtf8("task"), _sam3Task);
    manifest.insert(QString::fromUtf8("model_id"), QString::fromUtf8("sam3_transformers"));
    manifest.insert(QString::fromUtf8("provider_runtime_id"), QString::fromUtf8("sam3"));
    manifest.insert(QString::fromUtf8("source_metadata"), _sam3SourceMetadata);
    manifest.insert(QString::fromUtf8("source_dimensions"), QJsonObject{{QString::fromUtf8("width"), _sourceFrameWidth}, {QString::fromUtf8("height"), _sourceFrameHeight}});
    manifest.insert(QString::fromUtf8("selected_prompt_kind"), _sam3Prompts.size() > 1 ? QString::fromUtf8("multi") : _sam3Prompt.value(QString::fromUtf8("type")).toString());
    manifest.insert(QString::fromUtf8("selected_prompt"), _sam3Prompt);
    manifest.insert(QString::fromUtf8("selected_mask_path_project_relative"), selectedMaskRelative);
    if (!selectedSequencePatternRelative.isEmpty()) {
        manifest.insert(QString::fromUtf8("selected_mask_sequence_pattern_project_relative"), selectedSequencePatternRelative);
    }
    manifest.insert(QString::fromUtf8("output_dir_project_relative"), _sam3RelativeRoot);
    manifest.insert(QString::fromUtf8("probe_result_project_relative"), resultJsonRelative);
    manifest.insert(QString::fromUtf8("result_manifest_path_project_relative"), _sam3RelativeRoot + QString::fromUtf8("result_manifest.json"));
    manifest.insert(QString::fromUtf8("prompts"), _sam3Prompts);
    manifest.insert(QString::fromUtf8("masks"), manifestMasks);
    manifest.insert(QString::fromUtf8("proof_nonzero_pixels"), proofMetrics);
    manifest.insert(QString::fromUtf8("worker_result"), result);

    QFile out(QDir(_sam3AbsoluteRoot).filePath(QString::fromUtf8("result_manifest.json")));
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (message) { *message = tr("Could not write SAM3 result manifest: %1").arg(out.errorString()); }
        return false;
    }
    out.write(QJsonDocument(manifest).toJson(QJsonDocument::Indented));
    out.write("\n");

    const QString manifestRelative = _sam3RelativeRoot + QString::fromUtf8("result_manifest.json");
    addOrPromoteResultManifest(manifestRelative);
    if (selectedRelativeMask) { *selectedRelativeMask = selectedMaskRelative; }
    if (message) { *message = tr("SAM3 masks generated: %1").arg(manifestRelative); }
    return true;
}

void FluxAiPanel::onResultHistorySelectionChanged()
{
    const QString manifest = selectedResultManifestProjectRelative();
    if (!manifest.isEmpty()) {
        _lastResultManifestProjectRelative = manifest;
    }
    updateUiState();
}

bool FluxAiPanel::ensureSam3WorkerStarted(QString* message)
{
    if (_sam3WorkerProcess && _sam3WorkerProcess->state() != QProcess::NotRunning) {
        if (message) { *message = QString::fromUtf8("SAM3 worker already running."); }
        return true;
    }
    if (!_sam3WorkerProcess) {
        if (message) { *message = QString::fromUtf8("SAM3 worker process object is missing."); }
        return false;
    }

    const QString root = repoRoot();
    const QString runtimeScript = QDir(root).filePath(QString::fromUtf8("tools/ai/flux_provider_runtime.py"));
    const QString workerScript = QDir(root).filePath(QString::fromUtf8("tools/ai/sam3_transformers_worker.py"));
    if (!QFileInfo(runtimeScript).isFile() || !QFileInfo(workerScript).isFile()) {
        if (message) { *message = tr("SAM3 worker startup failed: missing runtime script %1 or worker script %2.").arg(runtimeScript, workerScript); }
        return false;
    }

    QProcess resolver;
    resolver.setWorkingDirectory(root);
    resolver.start(QString::fromUtf8("python3"), QStringList() << runtimeScript << QString::fromUtf8("python") << QString::fromUtf8("sam3"));
    if (!resolver.waitForStarted(5000) || !resolver.waitForFinished(10000) || resolver.exitStatus() != QProcess::NormalExit || resolver.exitCode() != 0) {
        const QString stderrText = QString::fromUtf8(resolver.readAllStandardError()).trimmed();
        if (message) { *message = tr("SAM3 worker startup failed: provider python resolution failed via python3 %1 python sam3. stderr: %2").arg(runtimeScript, stderrText); }
        return false;
    }
    const QString providerPython = QString::fromUtf8(resolver.readAllStandardOutput()).trimmed();
    if (providerPython.isEmpty() || !QFileInfo(providerPython).isExecutable()) {
        if (message) { *message = tr("SAM3 worker startup failed: provider python is not executable: %1 (script %2).").arg(providerPython, workerScript); }
        return false;
    }

    _sam3WorkerStdoutBuffer.clear();
    _sam3WorkerPendingCommands.clear();
    _sam3WorkerProcess->setWorkingDirectory(root);
    _sam3WorkerProcess->start(providerPython, QStringList() << workerScript);
    if (!_sam3WorkerProcess->waitForStarted(10000)) {
        const QString stderrText = QString::fromUtf8(_sam3WorkerProcess->readAllStandardError()).trimmed();
        if (message) { *message = tr("SAM3 worker startup failed: could not start %1 %2. stderr: %3").arg(providerPython, workerScript, stderrText); }
        return false;
    }
    appendLog(tr("SAM3 worker started: %1 %2").arg(providerPython, workerScript));
    if (message) { *message = QString::fromUtf8("SAM3 worker started."); }
    return true;
}

QString FluxAiPanel::sendSam3WorkerRequest(const QJsonObject& request)
{
    if (!_sam3WorkerProcess || _sam3WorkerProcess->state() == QProcess::NotRunning) {
        appendLog(QString::fromUtf8("SAM3 worker request failed: worker is not running."));
        return QString();
    }
    QJsonObject outbound = request;
    const QString command = outbound.value(QString::fromUtf8("command")).toString();
    if (command.isEmpty()) {
        appendLog(QString::fromUtf8("SAM3 worker request failed: command is empty."));
        return QString();
    }
    QString id = outbound.value(QString::fromUtf8("id")).toString();
    if (id.isEmpty()) {
        id = QString::fromUtf8("sam3-%1").arg(_sam3WorkerNextRequestId++);
        outbound.insert(QString::fromUtf8("id"), id);
    }
    _sam3WorkerPendingCommands.insert(id, command);
    const QByteArray payload = QJsonDocument(outbound).toJson(QJsonDocument::Compact) + QByteArray("\n");
    const qint64 written = _sam3WorkerProcess->write(payload);
    if (written != payload.size() || !_sam3WorkerProcess->waitForBytesWritten(5000)) {
        _sam3WorkerPendingCommands.remove(id);
        appendLog(tr("SAM3 worker request failed: could not write %1 request %2.").arg(command, id));
        return QString();
    }
    QJsonObject found;
    found.insert(QString::fromUtf8("request_id"), id);
    found.insert(QString::fromUtf8("command"), command);
    found.insert(QString::fromUtf8("payload_bytes"), payload.size());
    writeAiLog(QString::fromUtf8("info"), QString::fromUtf8("worker"), QString::fromUtf8("sam3_request_sent"),
               QString::fromUtf8("pass"), QString::fromUtf8("SAM3 worker request written."), QString(), found);
    return id;
}

void FluxAiPanel::processSam3WorkerLine(const QByteArray& line)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(line, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        appendLog(QString::fromUtf8("sam3 worker stdout: ") + QString::fromUtf8(line.trimmed()));
        return;
    }
    const QJsonObject response = doc.object();
    if (response.value(QString::fromUtf8("event")).toString() == QString::fromUtf8("ready")) {
        appendLog(QString::fromUtf8("SAM3 worker ready."));
        return;
    }
    if (response.value(QString::fromUtf8("event")).toString() == QString::fromUtf8("progress")) {
        const QString id = response.value(QString::fromUtf8("id")).toString();
        const QString command = response.value(QString::fromUtf8("command")).toString(_sam3WorkerPendingCommands.value(id));
        QJsonObject payload = response.value(QString::fromUtf8("payload")).toObject();
        if (payload.isEmpty()) {
            payload = response;
        }
        const QString message = payload.value(QString::fromUtf8("message")).toString(QString::fromUtf8("SAM3 working"));
        if ((command != QString::fromUtf8("infer_still") && command != QString::fromUtf8("infer_video")) || id == _sam3RunPendingRequestId) {
            if (_statusLabel) { _statusLabel->setText(QString::fromUtf8("Status: ") + message); }
            updateProgressBarFromPayload(payload, message);
        }
        appendLog(QString::fromUtf8("SAM3 progress: ") + message);
        return;
    }

    const QString id = response.value(QString::fromUtf8("id")).toString();
    const QString command = response.value(QString::fromUtf8("command")).toString(_sam3WorkerPendingCommands.value(id));
    const bool ok = response.value(QString::fromUtf8("ok")).toBool(false);
    QJsonObject payload = response.value(QString::fromUtf8("payload")).toObject();
    if (payload.isEmpty() && response.contains(QString::fromUtf8("result"))) {
        payload = response;
    }
    if (!id.isEmpty()) {
        _sam3WorkerPendingCommands.remove(id);
    }

    if (!ok) {
        const QString errorText = response.value(QString::fromUtf8("error")).toString(payload.value(QString::fromUtf8("status")).toString(QString::fromUtf8("blocked")));
        if (id == _sam3RunPendingRequestId) { _sam3RunPendingRequestId.clear(); }
        if (id == _livePreviewPendingRequestId) { _livePreviewPendingRequestId.clear(); }
        _sam3WorkerLoading = false;
        _sam3WorkerUnloading = false;
        _sam3LastError = errorText;
        setAIPaintSam3StatusKnob(_sourceAIPaintNode, QString::fromUtf8("error: ") + errorText);
        if (_statusLabel) { _statusLabel->setText(QString::fromUtf8("Status: failure")); }
        resetProgressBar();
        appendLog(tr("SAM3 worker %1 request failed: %2").arg(command, errorText));
        updateUiState();
        return;
    }

    if (command == QString::fromUtf8("load")) {
        _sam3WorkerLoading = false;
        _sam3WorkerLoaded = payload.value(QString::fromUtf8("loaded")).toBool(payload.value(QString::fromUtf8("status")).toString() == QString::fromUtf8("ok"));
        setAIPaintSam3StatusKnob(_sourceAIPaintNode, _sam3WorkerLoaded ? QString::fromUtf8("loaded") : QString::fromUtf8("unloaded"));
        if (_statusLabel) { _statusLabel->setText(_sam3WorkerLoaded ? QString::fromUtf8("Status: SAM3 loaded") : QString::fromUtf8("Status: SAM3 not loaded")); }
        appendLog(_sam3WorkerLoaded ? QString::fromUtf8("SAM3 worker loaded.") : QString::fromUtf8("SAM3 worker load returned without loaded state."));
    } else if (command == QString::fromUtf8("unload") || command == QString::fromUtf8("shutdown")) {
        _sam3WorkerUnloading = false;
        _sam3WorkerLoaded = false;
        clearAIPaintLivePreviewMask(_sourceAIPaintNode);
        setAIPaintSam3StatusKnob(_sourceAIPaintNode, QString::fromUtf8("unloaded"));
        if (_statusLabel) { _statusLabel->setText(QString::fromUtf8("Status: SAM3 unloaded")); }
        appendLog(QString::fromUtf8("SAM3 worker unloaded."));
    } else if (command == QString::fromUtf8("infer_still") || command == QString::fromUtf8("infer_video")) {
        const bool livePreview = id == _livePreviewPendingRequestId;
        if (livePreview) {
            _livePreviewPendingRequestId.clear();
            const int responseGeneration = payload.value(QString::fromUtf8("generation")).toInt(_livePreviewPendingGeneration);
            if (responseGeneration != _livePreviewPendingGeneration || _livePreviewPendingGeneration != _livePreviewGeneration) {
                appendLog(QString::fromUtf8("SAM3 live preview ignored: stale frame/prompt generation."));
                updateUiState();
                return;
            }
            if (payload.value(QString::fromUtf8("status")).toString() == QString::fromUtf8("ok") || payload.value(QString::fromUtf8("status")).toString() == QString::fromUtf8("blocked")) {
                const QJsonObject result = payload.value(QString::fromUtf8("result")).toObject();
                QString mask = result.value(QString::fromUtf8("selected_mask_path")).toString();
                if (mask.isEmpty()) { mask = result.value(QString::fromUtf8("masks")).toObject().value(QString::fromUtf8("combined")).toString(); }
                AIPaint* aiPaint = aipaintEffectFromNode(_sourceAIPaintNode);
                if (aiPaint && !mask.isEmpty() && QFileInfo(mask).isFile()) {
                    QImageReader maskReader(mask);
                    const QSize maskSize = maskReader.size();
                    QJsonObject responseProof;
                    responseProof.insert(QString::fromUtf8("mask"), mask);
                    responseProof.insert(QString::fromUtf8("mask_width"), maskSize.width());
                    responseProof.insert(QString::fromUtf8("mask_height"), maskSize.height());
                    responseProof.insert(QString::fromUtf8("expected_width"), _sourceFrameWidth);
                    responseProof.insert(QString::fromUtf8("expected_height"), _sourceFrameHeight);
                    responseProof.insert(QString::fromUtf8("worker_source_image"), result.value(QString::fromUtf8("source_image")).toObject());
                    writeAiLog(QString::fromUtf8("info"), QString::fromUtf8("sam3"), QString::fromUtf8("live_preview_mask_dimensions"),
                               (maskSize.width() == _sourceFrameWidth && maskSize.height() == _sourceFrameHeight) ? QString::fromUtf8("pass") : QString::fromUtf8("block"),
                               QString::fromUtf8("SAM3 live preview returned mask dimensions checked."),
                               (maskSize.width() == _sourceFrameWidth && maskSize.height() == _sourceFrameHeight) ? QString() : QString::fromUtf8("Returned mask dimensions do not match processed source dimensions."),
                               responseProof);
                    if (maskSize.width() != _sourceFrameWidth || maskSize.height() != _sourceFrameHeight) {
                        clearAIPaintLivePreviewMask(_sourceAIPaintNode);
                        setAIPaintSam3StatusKnob(_sourceAIPaintNode, QString::fromUtf8("live preview blocked"));
                        appendLog(QString::fromUtf8("SAM3 live preview blocked: returned mask %1x%2 does not match processed source %3x%4.")
                                      .arg(maskSize.width()).arg(maskSize.height()).arg(_sourceFrameWidth).arg(_sourceFrameHeight));
                        updateUiState();
                        return;
                    }
                    // Pass canonical image_bounds from source frame metadata so the
                    // overlay is drawn at the correct position instead of (0,0)-(w,h).
                    const QJsonObject boundsObj = _sourceFrameMetadata.value(QString::fromUtf8("image_bounds")).toObject();
                    const double bx1 = boundsObj.value(QString::fromUtf8("x1")).toDouble(0.0);
                    const double by1 = boundsObj.value(QString::fromUtf8("y1")).toDouble(0.0);
                    const double bx2 = boundsObj.value(QString::fromUtf8("x2")).toDouble(0.0);
                    const double by2 = boundsObj.value(QString::fromUtf8("y2")).toDouble(0.0);
                    const bool hasBounds = !boundsObj.isEmpty() && (bx2 > bx1) && (by2 > by1);
                    aiPaint->setLivePreviewMaskPath(mask, _sourceTimelineFrame,
                                                     bx1, by1, bx2, by2, hasBounds);
                    setAIPaintSam3StatusKnob(_sourceAIPaintNode, QString::fromUtf8("live preview ready"));
                    if (_statusLabel) { _statusLabel->setText(QString::fromUtf8("Status: SAM3 live preview ready")); }
                    appendLog(QString::fromUtf8("SAM3 live preview updated: %1 (bounds_valid=%2 [%3,%4,%5,%6])")
                                  .arg(mask)
                                  .arg(hasBounds ? 1 : 0)
                                  .arg(bx1).arg(by1).arg(bx2).arg(by2));
                } else {
                    clearAIPaintLivePreviewMask(_sourceAIPaintNode);
                    setAIPaintSam3StatusKnob(_sourceAIPaintNode, QString::fromUtf8("live preview blocked"));
                    appendLog(QString::fromUtf8("SAM3 live preview produced no usable mask."));
                }
            }
        } else if (id != _sam3RunPendingRequestId) {
            appendLog(QString::fromUtf8("SAM3 worker inference response ignored: no matching active run/live-preview request."));
        } else {
            QString verifyMessage;
            QString selectedRelativeMask;
            const bool verified = verifySam3ResultAndWriteManifest(payload, &verifyMessage, &selectedRelativeMask);
            _sam3RunPendingRequestId.clear();
            if (_statusLabel) { _statusLabel->setText(verified ? QString::fromUtf8("Status: success") : QString::fromUtf8("Status: failure")); }
            if (verified) {
                setProgressBarValue(100, QString::fromUtf8("Complete"));
            } else {
                resetProgressBar();
            }
            appendLog(verifyMessage);
            if (verified) {
                previewSam3RunResult(selectedRelativeMask);
            }
        }
    } else if (command == QString::fromUtf8("cancel")) {
        appendLog(QString::fromUtf8("SAM3 worker cancel acknowledged."));
    } else {
        appendLog(QString::fromUtf8("SAM3 worker response: ") + QString::fromUtf8(line.trimmed()));
    }
    updateUiState();
}

void FluxAiPanel::previewSam3RunResult(const QString& relativeMask)
{
    QString safeMask;
    if (!sanitizeProjectRelativePath(relativeMask, &safeMask)) {
        appendLog(tr("AI result preview blocked: unsafe project-relative mask path: %1").arg(relativeMask));
        return;
    }
    if (!_gui || !_gui->getApp() || !_gui->getApp()->getProject()) {
        appendLog(QString::fromUtf8("AI result preview blocked: project is unavailable."));
        return;
    }
    const QString absolutePath = QDir(_gui->getApp()->getProject()->getProjectPath()).filePath(safeMask);
    const QFileInfo maskInfo(absolutePath);
    if (!maskInfo.isFile() || maskInfo.size() <= 0) {
        appendLog(tr("AI result preview blocked: mask is missing or empty: %1").arg(safeMask));
        return;
    }
    QString diagnostics;
    const bool ok = _gui->previewFluxAiResultPngInWorkViewer(absolutePath, &diagnostics);
    appendLog(ok ? tr("AI result previewed in AI Work Viewer: %1").arg(safeMask) : diagnostics);
}

void FluxAiPanel::stopSam3PersistentWorker()
{
    if (!_sam3WorkerProcess || _sam3WorkerProcess->state() == QProcess::NotRunning) {
        return;
    }
    appendLog(QString::fromUtf8("Stopping SAM3 worker to free GPU for MatAnyone2"));
    QJsonObject shutdown;
    shutdown.insert(QString::fromUtf8("command"), QString::fromUtf8("shutdown"));
    sendSam3WorkerRequest(shutdown);
    if (!_sam3WorkerProcess->waitForFinished(3000)) {
        _sam3WorkerProcess->kill();
        _sam3WorkerProcess->waitForFinished(1000);
    }
    _sam3WorkerStdoutBuffer.clear();
    _sam3WorkerPendingCommands.clear();
    _sam3WorkerLoaded = false;
    _sam3WorkerLoading = false;
    _sam3WorkerUnloading = false;
    _sam3RunPendingRequestId.clear();
    _livePreviewPendingRequestId.clear();
    _sam3CancelRequested = false;
    setAIPaintSam3StatusKnob(_sourceAIPaintNode, QString::fromUtf8("unloaded"));
}

void FluxAiPanel::stopMatAnyone2Worker()
{
    if (!_matAnyone2WorkerProcess || _matAnyone2WorkerProcess->state() == QProcess::NotRunning) {
        return;
    }
    appendLog(QString::fromUtf8("Stopping MatAnyone2 worker to free GPU for SAM3"));
    QJsonObject shutdown;
    shutdown.insert(QString::fromUtf8("command"), QString::fromUtf8("shutdown"));
    sendMatAnyone2WorkerRequest(shutdown);
    if (!_matAnyone2WorkerProcess->waitForFinished(3000)) {
        _matAnyone2WorkerProcess->kill();
        _matAnyone2WorkerProcess->waitForFinished(1000);
    }
    _matAnyone2WorkerStdoutBuffer.clear();
    _matAnyone2WorkerPendingCommands.clear();
    _matAnyone2RunPendingRequestId.clear();
    _matAnyone2PendingInferRequest = QJsonObject();
    _matAnyone2CancelRequested = false;
    _matAnyone2Running = false;
}

void FluxAiPanel::runMatAnyone2()
{
    const QString modelId = QString::fromUtf8("matanyone2");
    if (!hasSelectedSource()) {
        appendLog(tr("Cannot start MatAnyone2: no source is selected."));
        if (_statusLabel) { _statusLabel->setText(QString::fromUtf8("Status: select a source first")); }
        updateUiState();
        return;
    }
    if (!_gui || !_gui->getApp() || !_gui->getApp()->getProject()) {
        appendLog(tr("Cannot start MatAnyone2: project is unavailable."));
        updateUiState();
        return;
    }
    ProjectPtr project = _gui->getApp()->getProject();
    if (!project->hasProjectBeenSavedByUser()) {
        if (!_gui->saveProjectAs()) {
            const QString canceled = QString::fromUtf8("MatAnyone2 canceled: project must be saved first.");
            if (_statusLabel) { _statusLabel->setText(QString::fromUtf8("Status: ") + canceled); }
            appendLog(canceled);
            return;
        }
    }
    if (!project->hasProjectBeenSavedByUser()) {
        const QString canceled = QString::fromUtf8("MatAnyone2 canceled: project must be saved first.");
        if (_statusLabel) { _statusLabel->setText(QString::fromUtf8("Status: ") + canceled); }
        appendLog(canceled);
        return;
    }

    // Show NON-COMMERCIAL warning
    const QString path = manifestPath();
    QString warningText = QString::fromUtf8("NON-COMMERCIAL: MatAnyone2 is non-commercial/research licensed.");
    QFile manifestFile(path);
    if (manifestFile.open(QIODevice::ReadOnly)) {
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(manifestFile.readAll(), &parseError);
        if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
            const QJsonArray models = doc.object().value(QString::fromUtf8("models")).toArray();
            for (const QJsonValue& value : models) {
                const QJsonObject model = value.toObject();
                if (model.value(QString::fromUtf8("id")).toString() == modelId) {
                    const QString manifestWarning = model.value(QString::fromUtf8("warning_text")).toString();
                    if (!manifestWarning.isEmpty()) {
                        warningText = manifestWarning;
                    }
                    break;
                }
            }
        }
    }
    appendLog(warningText);

    QString message;
    if (!_sourceAIPaintNode || !_sourceAIPaintNode->isActivated()) {
        if (_statusLabel) { _statusLabel->setText(QString::fromUtf8("Status: source capture blocked")); }
        appendLog(QString::fromUtf8("MatAnyone2 blocked: selected layer has no active AI Paint source context."));
        updateUiState();
        return;
    }

    // Validate video range
    int rangeStart = 0, rangeEnd = 0;
    if (!selectedFrameRange(&rangeStart, &rangeEnd, &message) || rangeEnd <= rangeStart) {
        if (_statusLabel) { _statusLabel->setText(QString::fromUtf8("Status: MatAnyone2 requires a video range (end > start)")); }
        appendLog(tr("MatAnyone2 requires a video range (end > start). %1").arg(message));
        updateUiState();
        return;
    }
    int generationTimeOffsetForResult = _sourceTimeOffset;

    // Resolve base mask — multiple sources, pick first available
    // 1. AI Paint live preview mask on the current source node
    // 2. Selected SAM3 result mask (still or extracted from video sequence)
    QString baseMaskAbsolute;
    QString baseMaskRelative;

    AIPaint* aiPaint = aipaintEffectFromNode(_sourceAIPaintNode);
    if (aiPaint) {
        const QString liveMask = aiPaint->livePreviewMaskPath();
        if (livePreviewMaskMatchesCurrentPromptFrame(aiPaint) && !liveMask.isEmpty() && QFileInfo(liveMask).isFile() && QFileInfo(liveMask).size() > 0) {
            baseMaskAbsolute = liveMask;
            QString relative;
            if (sanitizeProjectRelativePath(QDir(project->getProjectPath()).relativeFilePath(liveMask), &relative)) {
                baseMaskRelative = relative;
            } else {
                baseMaskRelative = liveMask;  // absolute fallback
            }
            appendLog(tr("MatAnyone2 using AI Paint live preview mask: %1").arg(baseMaskRelative));
        }
    }

    // 2. Selected result history mask
    if (baseMaskAbsolute.isEmpty()) {
        QString maskRelative, maskMessage;
        QString sequencePattern;
        int historyRangeStart = 0;
        int historyRangeEnd = 0;
        int historyTimeOffset = _sourceTimeOffset;
        if (selectedResultMaskProjectRelative(&maskRelative, &maskMessage, &sequencePattern)) {
            if (selectedResultGenerationRange(&historyRangeStart, &historyRangeEnd, &historyTimeOffset)) {
                rangeStart = historyRangeStart;
                rangeEnd = historyRangeEnd;
                generationTimeOffsetForResult = historyTimeOffset;
                appendLog(tr("MatAnyone2 using selected SAM3 history frame range %1-%2.").arg(rangeStart).arg(rangeEnd));
            }
            // If the result has a video sequence, try to extract the mask for rangeStart in the selected result's numbering.
            if (!sequencePattern.isEmpty() && sequencePattern.contains(QLatin1Char('#'))) {
                QRegularExpression hashRe(QLatin1String("#{2,}"));
                QRegularExpressionMatch match = hashRe.match(sequencePattern);
                if (match.hasMatch()) {
                    const int hashLen = match.capturedLength();
                    const int maskFrame = rangeStart;
                    const QString frameNumStr = QString::number(maskFrame).rightJustified(hashLen, QLatin1Char('0'));
                    const QString seqMaskRelative = sequencePattern.mid(0, match.capturedStart()) + frameNumStr + sequencePattern.mid(match.capturedEnd());
                    const QString seqMaskAbsolute = QDir(project->getProjectPath()).filePath(seqMaskRelative);
                    if (QFileInfo(seqMaskAbsolute).isFile() && QFileInfo(seqMaskAbsolute).size() > 0) {
                        baseMaskAbsolute = seqMaskAbsolute;
                        baseMaskRelative = seqMaskRelative;
                        appendLog(tr("MatAnyone2 using mask frame %1 from SAM3 video sequence: %2").arg(maskFrame).arg(seqMaskRelative));
                    }
                }
            }
            // Fallback to the selected still mask if sequence extraction failed
            if (baseMaskAbsolute.isEmpty()) {
                const QString absMask = QDir(project->getProjectPath()).filePath(maskRelative);
                if (QFileInfo(absMask).isFile() && QFileInfo(absMask).size() > 0) {
                    baseMaskAbsolute = absMask;
                    baseMaskRelative = maskRelative;
                    appendLog(tr("MatAnyone2 using selected result mask: %1").arg(maskRelative));
                }
            }
        } else {
            if (_statusLabel) { _statusLabel->setText(QString::fromUtf8("Status: MatAnyone2 requires a base mask")); }
            appendLog(tr("MatAnyone2 requires a base mask. Use AI Paint to select a subject, or select a SAM3 result from history. %1").arg(maskMessage));
            updateUiState();
            return;
        }
    }

    if (baseMaskAbsolute.isEmpty() || !QFileInfo(baseMaskAbsolute).isFile() || QFileInfo(baseMaskAbsolute).size() <= 0) {
        if (_statusLabel) { _statusLabel->setText(QString::fromUtf8("Status: base mask file is missing or empty")); }
        appendLog(tr("MatAnyone2 base mask file is missing or empty."));
        updateUiState();
        return;
    }

    // Reuse cached source sequence if same range, otherwise export
    QJsonObject sourceMetadata = _sourceFrameMetadata;
    QJsonObject frameRangeMetadata = selectedFrameRangeMetadata();
    frameRangeMetadata.insert(QString::fromUtf8("source_start"), rangeStart);
    frameRangeMetadata.insert(QString::fromUtf8("source_end"), rangeEnd);
    frameRangeMetadata.insert(QString::fromUtf8("duration_frames"), qMax(0, rangeEnd - rangeStart + 1));
    frameRangeMetadata.insert(QString::fromUtf8("time_offset"), generationTimeOffsetForResult);
    frameRangeMetadata.insert(QString::fromUtf8("timeline_start"), rangeStart + generationTimeOffsetForResult);
    frameRangeMetadata.insert(QString::fromUtf8("timeline_end"), rangeEnd + generationTimeOffsetForResult);
    sourceMetadata.insert(QString::fromUtf8("matanyone2_frame_range"), frameRangeMetadata);
    _sourceFrameMetadata = sourceMetadata;

    QString sequenceDir;
    QJsonObject sequenceMetadata;

    // Check selected result manifest for a reusable source sequence (e.g. from SAM3 video run)
    if (sequenceDir.isEmpty()) {
        const QString selectedManifestRel = selectedResultManifestProjectRelative();
        if (!selectedManifestRel.isEmpty()) {
            QString safeManifest;
            if (sanitizeProjectRelativePath(selectedManifestRel, &safeManifest)) {
                const QString manifestAbsPath = QDir(project->getProjectPath()).filePath(safeManifest);
                QFile mf(manifestAbsPath);
                if (mf.open(QIODevice::ReadOnly)) {
                    QJsonParseError mpe;
                    const QJsonDocument mdoc = QJsonDocument::fromJson(mf.readAll(), &mpe);
                    if (mpe.error == QJsonParseError::NoError && mdoc.isObject()) {
                        const QJsonObject mobj = mdoc.object();
                        const QJsonObject srcMeta = mobj.value(QString::fromUtf8("source_metadata")).toObject();
                        // Try matanyone2_source_sequence first, then sam3_source_sequence as fallback
                        QStringList seqKeys;
                        seqKeys << QString::fromUtf8("matanyone2_source_sequence")
                                << QString::fromUtf8("sam3_source_sequence");
                        for (const QString& seqKey : seqKeys) {
                            if (!sequenceDir.isEmpty()) { break; }
                            const QJsonObject manifestSeq = srcMeta.value(seqKey).toObject();
                            if (manifestSeq.isEmpty()) { continue; }

                            const QString baseDir = manifestSeq.value(QString::fromUtf8("temporary_source_sequence_dir")).toString();
                            if (baseDir.isEmpty()) { continue; }

                            const QJsonArray manifestFrames = manifestSeq.value(QString::fromUtf8("frames")).toArray();
                            if (manifestFrames.isEmpty()) { continue; }

                            const QString firstRelPath = manifestFrames.at(0).toObject().value(QString::fromUtf8("path")).toString();
                            const QString lastRelPath = manifestFrames.at(manifestFrames.size() - 1).toObject().value(QString::fromUtf8("path")).toString();
                            const QString firstAbsPath = QDir(baseDir).filePath(firstRelPath);
                            const QString lastAbsPath = QDir(baseDir).filePath(lastRelPath);

                            if (QFileInfo(firstAbsPath).isFile() && QFileInfo(lastAbsPath).isFile()) {
                                sequenceDir = baseDir;
                                sequenceMetadata = manifestSeq;
                                sequenceMetadata.insert(QString::fromUtf8("frames"), renumberSequenceFrames(manifestFrames, rangeStart));
                                sequenceMetadata.insert(QString::fromUtf8("source_frame_start"), rangeStart);
                                sequenceMetadata.insert(QString::fromUtf8("source_frame_end"), rangeEnd);
                                sequenceMetadata.insert(QString::fromUtf8("timeline_frame_start"), rangeStart);
                                sequenceMetadata.insert(QString::fromUtf8("timeline_frame_end"), rangeEnd);
                                appendLog(tr("MatAnyone2 reusing source sequence from selected result manifest (%1) with selected mask numbering %2-%3")
                                          .arg(seqKey).arg(rangeStart).arg(rangeEnd));
                            }
                        }
                    }
                }
            }
        }
    }

    if (sequenceDir.isEmpty() && _cachedSourceSequenceRangeStart == rangeStart && _cachedSourceSequenceRangeEnd == rangeEnd && !_cachedSourceSequenceDir.isEmpty() && !_cachedSourceSequenceMetadata.isEmpty()) {
        // Verify cached frames still exist (resolve relative paths against cached dir)
        const QJsonArray cachedFrames = _cachedSourceSequenceMetadata.value(QString::fromUtf8("frames")).toArray();
        bool framesValid = !cachedFrames.isEmpty();
        if (framesValid) {
            const QString firstRel = cachedFrames.at(0).toObject().value(QString::fromUtf8("path")).toString();
            const QString lastRel = cachedFrames.at(cachedFrames.size() - 1).toObject().value(QString::fromUtf8("path")).toString();
            const QString firstAbs = QDir(_cachedSourceSequenceDir).filePath(firstRel);
            const QString lastAbs = QDir(_cachedSourceSequenceDir).filePath(lastRel);
            framesValid = QFileInfo(firstAbs).isFile() && QFileInfo(lastAbs).isFile();
        }
        if (framesValid) {
            sequenceDir = _cachedSourceSequenceDir;
            sequenceMetadata = _cachedSourceSequenceMetadata;
            appendLog(tr("MatAnyone2 reusing cached source sequence for range %1-%2").arg(rangeStart).arg(rangeEnd));
        }
    }

    if (sequenceDir.isEmpty()) {
        _matAnyone2Exporting = true;
        _matAnyone2CancelRequested = false;
        if (_statusLabel) {
            _statusLabel->setText(QString::fromUtf8("Status: exporting MatAnyone2 source frames"));
        }
        setProgressBarValue(0, QString::fromUtf8("Exporting source frames for MatAnyone2"));
        updateUiState();

        auto exportProgressCallback = [this](int completed, int total) -> bool {
            const int percent = total > 0 ? static_cast<int>((static_cast<double>(completed) / static_cast<double>(total)) * 100.0) : 0;
            setProgressBarValue(percent, QString::fromUtf8("Exporting source frames %1/%2").arg(completed).arg(total));
            QCoreApplication::processEvents();
            return !_matAnyone2CancelRequested;
        };

        QString sequenceDiagnostics;
        sequenceDir = _gui->exportFluxSam3SourceSequenceForSourceContext(_sourceLayerIndex, _sourceLayerName, _sourceFilePath, _sourceReaderNode, _sourceReaderLabel, rangeStart, rangeEnd, &sequenceMetadata, &sequenceDiagnostics, exportProgressCallback);

        _matAnyone2Exporting = false;

        if (sequenceDir.isEmpty() || sequenceMetadata.value(QString::fromUtf8("frames")).toArray().isEmpty()) {
            if (_matAnyone2CancelRequested) {
                if (_statusLabel) { _statusLabel->setText(QString::fromUtf8("Status: canceled")); }
                setProgressBarValue(0, QString::fromUtf8("Canceled"));
                appendLog(QString::fromUtf8("MatAnyone2 source-sequence export canceled."));
            } else {
                if (_statusLabel) { _statusLabel->setText(QString::fromUtf8("Status: source sequence blocked")); }
                appendLog(sequenceDiagnostics.isEmpty() ? QString::fromUtf8("source-sequence export failed") : sequenceDiagnostics);
            }
            updateUiState();
            return;
        }

        // Cache for future reuse
        _cachedSourceSequenceDir = sequenceDir;
        _cachedSourceSequenceRangeStart = rangeStart;
        _cachedSourceSequenceRangeEnd = rangeEnd;
        _cachedSourceSequenceMetadata = sequenceMetadata;
    }

    sourceMetadata.insert(QString::fromUtf8("matanyone2_source_sequence"), sequenceMetadata);
    _sourceFrameMetadata = sourceMetadata;

    // Free GPU by stopping SAM3 persistent worker and VideoMaMa before starting MatAnyone2
    stopSam3PersistentWorker();
    stopVideoMamaWorker();

    // Start worker
    if (!ensureMatAnyone2WorkerStarted(&message)) {
        if (_statusLabel) { _statusLabel->setText(QString::fromUtf8("Status: failure")); }
        appendLog(message);
        updateUiState();
        return;
    }

    const QString runId = QString::fromUtf8("run-") + QDateTime::currentDateTimeUtc().toString(QString::fromUtf8("yyyyMMdd-HHmmss"));
    const QString slug = taskSlug(_taskCombo ? _taskCombo->currentText() : QString::fromUtf8("refine-matte"));
    const QString relativeRoot = QString::fromUtf8("FluxGenerated/AI/%1/%2/%3/").arg(slug, modelId, runId);
    const QString absoluteRoot = QDir(project->getProjectPath()).filePath(relativeRoot);
    if (!QDir().mkpath(absoluteRoot)) {
        if (_statusLabel) { _statusLabel->setText(QString::fromUtf8("Status: failure")); }
        appendLog(tr("Could not create MatAnyone2 output directory: %1").arg(absoluteRoot));
        updateUiState();
        return;
    }

    _matAnyone2AbsoluteRoot = absoluteRoot;
    _matAnyone2RelativeRoot = relativeRoot;
    _matAnyone2RunId = runId;
    _matAnyone2Task = _taskCombo ? _taskCombo->currentText() : QString();
    _matAnyone2SourceMetadata = sourceMetadata;
    _matAnyone2CancelRequested = false;
    _lastResultManifestProjectRelative.clear();

    // Build the infer_video request and store it as pending
    QJsonObject inferRequest;
    inferRequest.insert(QString::fromUtf8("command"), QString::fromUtf8("infer_video"));
    inferRequest.insert(QString::fromUtf8("frames"), sequenceMetadata.value(QString::fromUtf8("frames")).toArray());
    inferRequest.insert(QString::fromUtf8("first_frame_mask"), baseMaskAbsolute);
    inferRequest.insert(QString::fromUtf8("first_frame_mask_project_relative"), baseMaskRelative);
    inferRequest.insert(QString::fromUtf8("output_dir"), absoluteRoot);
    inferRequest.insert(QString::fromUtf8("source_width"), sequenceMetadata.value(QString::fromUtf8("width")).toInt(_sourceFrameWidth));
    inferRequest.insert(QString::fromUtf8("source_height"), sequenceMetadata.value(QString::fromUtf8("height")).toInt(_sourceFrameHeight));
    inferRequest.insert(QString::fromUtf8("source_range_start"), rangeStart);
    inferRequest.insert(QString::fromUtf8("source_range_end"), rangeEnd);
    inferRequest.insert(QString::fromUtf8("frame_range"), frameRangeMetadata);
    inferRequest.insert(QString::fromUtf8("source_sequence"), sequenceMetadata);
    inferRequest.insert(QString::fromUtf8("source_metadata"), sourceMetadata);
    inferRequest.insert(QString::fromUtf8("model_id"), modelId);
    inferRequest.insert(QString::fromUtf8("noncommercial_acknowledged"), true);
    inferRequest.insert(QString::fromUtf8("run_id"), runId);
    _matAnyone2PendingInferRequest = inferRequest;

    // Send load first; infer_video will be auto-sent when load succeeds
    QJsonObject loadRequest;
    loadRequest.insert(QString::fromUtf8("command"), QString::fromUtf8("load"));

    const QString loadId = sendMatAnyone2WorkerRequest(loadRequest);
    if (loadId.isEmpty()) {
        _matAnyone2PendingInferRequest = QJsonObject();
        if (_statusLabel) { _statusLabel->setText(QString::fromUtf8("Status: failure")); }
        appendLog(QString::fromUtf8("MatAnyone2 load request failed: worker request could not be sent."));
        updateUiState();
        return;
    }
    _matAnyone2RunPendingRequestId = loadId;
    _matAnyone2Running = true;
    if (_outputLabel) { _outputLabel->setText(QString::fromUtf8("Output: ") + relativeRoot); }
    if (_statusLabel) { _statusLabel->setText(QString::fromUtf8("Status: MatAnyone2 loading model")); }
    setProgressBarValue(0, QString::fromUtf8("Loading MatAnyone2 model"));
    appendLog(QString::fromUtf8("Starting MatAnyone2: loading model before inference: %1; sourceRange=%2-%3; baseMask=%4")
              .arg(runId)
              .arg(rangeStart)
              .arg(rangeEnd)
              .arg(baseMaskRelative));
    updateUiState();
}

bool FluxAiPanel::ensureMatAnyone2WorkerStarted(QString* message)
{
    if (_matAnyone2WorkerProcess && _matAnyone2WorkerProcess->state() != QProcess::NotRunning) {
        if (message) { *message = QString::fromUtf8("MatAnyone2 worker already running."); }
        return true;
    }
    if (!_matAnyone2WorkerProcess) {
        if (message) { *message = QString::fromUtf8("MatAnyone2 worker process object is missing."); }
        return false;
    }

    const QString root = repoRoot();
    const QString runtimeScript = QDir(root).filePath(QString::fromUtf8("tools/ai/flux_provider_runtime.py"));
    const QString workerScript = QDir(root).filePath(QString::fromUtf8("tools/ai/matanyone2_worker.py"));
    if (!QFileInfo(runtimeScript).isFile() || !QFileInfo(workerScript).isFile()) {
        if (message) { *message = tr("MatAnyone2 worker startup failed: missing runtime script %1 or worker script %2.").arg(runtimeScript, workerScript); }
        return false;
    }

    QProcess resolver;
    resolver.setWorkingDirectory(root);
    resolver.start(QString::fromUtf8("python3"), QStringList() << runtimeScript << QString::fromUtf8("python") << QString::fromUtf8("matanyone2"));
    if (!resolver.waitForStarted(5000) || !resolver.waitForFinished(10000) || resolver.exitStatus() != QProcess::NormalExit || resolver.exitCode() != 0) {
        const QString stderrText = QString::fromUtf8(resolver.readAllStandardError()).trimmed();
        if (message) { *message = tr("MatAnyone2 worker startup failed: provider python resolution failed. stderr: %1").arg(stderrText); }
        return false;
    }
    const QString providerPython = QString::fromUtf8(resolver.readAllStandardOutput()).trimmed();
    if (providerPython.isEmpty() || !QFileInfo(providerPython).isExecutable()) {
        if (message) { *message = tr("MatAnyone2 worker startup failed: provider python is not executable: %1").arg(providerPython); }
        return false;
    }

    _matAnyone2WorkerStdoutBuffer.clear();
    _matAnyone2WorkerPendingCommands.clear();
    _matAnyone2WorkerProcess->setWorkingDirectory(root);
    _matAnyone2WorkerProcess->start(providerPython, QStringList() << workerScript);
    if (!_matAnyone2WorkerProcess->waitForStarted(10000)) {
        const QString stderrText = QString::fromUtf8(_matAnyone2WorkerProcess->readAllStandardError()).trimmed();
        if (message) { *message = tr("MatAnyone2 worker startup failed: could not start %1 %2. stderr: %3").arg(providerPython, workerScript, stderrText); }
        return false;
    }
    appendLog(tr("MatAnyone2 worker started: %1 %2").arg(providerPython, workerScript));
    if (message) { *message = QString::fromUtf8("MatAnyone2 worker started."); }
    return true;
}

QString FluxAiPanel::sendMatAnyone2WorkerRequest(const QJsonObject& request)
{
    if (!_matAnyone2WorkerProcess || _matAnyone2WorkerProcess->state() == QProcess::NotRunning) {
        appendLog(QString::fromUtf8("MatAnyone2 worker request failed: worker is not running."));
        return QString();
    }
    QJsonObject outbound = request;
    const QString command = outbound.value(QString::fromUtf8("command")).toString();
    if (command.isEmpty()) {
        appendLog(QString::fromUtf8("MatAnyone2 worker request failed: command is empty."));
        return QString();
    }
    QString id = outbound.value(QString::fromUtf8("id")).toString();
    if (id.isEmpty()) {
        id = QString::fromUtf8("ma2-%1").arg(_matAnyone2WorkerNextRequestId++);
        outbound.insert(QString::fromUtf8("id"), id);
    }
    _matAnyone2WorkerPendingCommands.insert(id, command);
    const QByteArray payload = QJsonDocument(outbound).toJson(QJsonDocument::Compact) + QByteArray("\n");
    const qint64 written = _matAnyone2WorkerProcess->write(payload);
    if (written != payload.size() || !_matAnyone2WorkerProcess->waitForBytesWritten(5000)) {
        _matAnyone2WorkerPendingCommands.remove(id);
        appendLog(tr("MatAnyone2 worker request failed: could not write %1 request %2.").arg(command, id));
        return QString();
    }
    QJsonObject found;
    found.insert(QString::fromUtf8("request_id"), id);
    found.insert(QString::fromUtf8("command"), command);
    found.insert(QString::fromUtf8("payload_bytes"), payload.size());
    writeAiLog(QString::fromUtf8("info"), QString::fromUtf8("worker"), QString::fromUtf8("matanyone2_request_sent"),
               QString::fromUtf8("pass"), QString::fromUtf8("MatAnyone2 worker request written."), QString(), found);
    return id;
}

void FluxAiPanel::processMatAnyone2WorkerLine(const QByteArray& line)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(line, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        appendLog(QString::fromUtf8("matanyone2 worker stdout: ") + QString::fromUtf8(line.trimmed()));
        return;
    }
    const QJsonObject response = doc.object();
    if (response.value(QString::fromUtf8("event")).toString() == QString::fromUtf8("ready")) {
        appendLog(QString::fromUtf8("MatAnyone2 worker ready."));
        const QString wt = response.value(QString::fromUtf8("warning_text")).toString();
        if (!wt.isEmpty()) {
            appendLog(wt);
        }
        return;
    }
    if (response.value(QString::fromUtf8("event")).toString() == QString::fromUtf8("progress")) {
        const QString id = response.value(QString::fromUtf8("id")).toString();
        QJsonObject payload = response.value(QString::fromUtf8("payload")).toObject();
        if (payload.isEmpty()) { payload = response; }
        const QString msg = payload.value(QString::fromUtf8("message")).toString(QString::fromUtf8("MatAnyone2 working"));
        if (_statusLabel) { _statusLabel->setText(QString::fromUtf8("Status: ") + msg); }
        updateProgressBarFromPayload(payload, msg);
        appendLog(QString::fromUtf8("MatAnyone2 progress: ") + msg);
        return;
    }

    const QString id = response.value(QString::fromUtf8("id")).toString();
    const QString command = response.value(QString::fromUtf8("command")).toString(_matAnyone2WorkerPendingCommands.value(id));
    const bool ok = response.value(QString::fromUtf8("ok")).toBool(false);
    QJsonObject payload = response.value(QString::fromUtf8("payload")).toObject();
    if (payload.isEmpty() && response.contains(QString::fromUtf8("result"))) { payload = response; }
    if (!id.isEmpty()) { _matAnyone2WorkerPendingCommands.remove(id); }

    if (!ok) {
        const QString errorText = payload.value(QString::fromUtf8("error")).toString(
            response.value(QString::fromUtf8("error")).toString(
                payload.value(QString::fromUtf8("status")).toString(QString::fromUtf8("blocked"))));
        if (id == _matAnyone2RunPendingRequestId) { _matAnyone2RunPendingRequestId.clear(); }
        // If load failed, clear any pending infer_video request
        if (command == QString::fromUtf8("load")) {
            _matAnyone2PendingInferRequest = QJsonObject();
            _matAnyone2Running = false;
            appendLog(tr("MatAnyone2 load failed; canceling pending infer_video."));
        }
        // If infer_video failed, stop the worker and unfreeze UI
        if (command == QString::fromUtf8("infer_video")) {
            _matAnyone2Running = false;
            stopMatAnyone2Worker();
        }
        if (_statusLabel) { _statusLabel->setText(QString::fromUtf8("Status: failure")); }
        resetProgressBar();
        // Log blockers if present
        const QJsonArray blockers = payload.value(QString::fromUtf8("blockers")).toArray();
        if (!blockers.isEmpty()) {
            for (const QJsonValue& b : blockers) {
                appendLog(tr("MatAnyone2 blocked: %1").arg(b.toString()));
            }
        } else {
            appendLog(tr("MatAnyone2 worker %1 request failed: %2").arg(command, errorText));
        }
        const QString traceback = payload.value(QString::fromUtf8("traceback")).toString();
        if (!traceback.isEmpty()) {
            appendLog(tr("MatAnyone2 worker traceback: %1").arg(traceback));
        }
        updateUiState();
        return;
    }

    if (command == QString::fromUtf8("load")) {
        // Model loaded successfully; auto-send the pending infer_video request
        appendLog(tr("MatAnyone2 model loaded successfully."));
        if (!_matAnyone2PendingInferRequest.isEmpty()) {
            const QString inferId = sendMatAnyone2WorkerRequest(_matAnyone2PendingInferRequest);
            _matAnyone2PendingInferRequest = QJsonObject();
            if (inferId.isEmpty()) {
                if (_statusLabel) { _statusLabel->setText(QString::fromUtf8("Status: failure")); }
                appendLog(QString::fromUtf8("MatAnyone2 infer_video request failed: worker request could not be sent."));
                resetProgressBar();
                updateUiState();
                return;
            }
            _matAnyone2RunPendingRequestId = inferId;
            _matAnyone2Running = true;
            if (_statusLabel) { _statusLabel->setText(QString::fromUtf8("Status: MatAnyone2 running")); }
            setProgressBarValue(0, QString::fromUtf8("Starting MatAnyone2 inference"));
            appendLog(QString::fromUtf8("MatAnyone2 model loaded; sending infer_video request."));
        }
    } else if (command == QString::fromUtf8("infer_video")) {
        QString verifyMessage;
        QString selectedRelativeMask;
        const bool verified = verifyMatAnyone2ResultAndWriteManifest(payload, &verifyMessage, &selectedRelativeMask);
        _matAnyone2RunPendingRequestId.clear();
        _matAnyone2Running = false;
        stopMatAnyone2Worker();
        if (_statusLabel) { _statusLabel->setText(verified ? QString::fromUtf8("Status: success") : QString::fromUtf8("Status: failure")); }
        if (verified) {
            setProgressBarValue(100, QString::fromUtf8("Complete"));
        } else {
            resetProgressBar();
        }
        appendLog(verifyMessage);
        if (verified) {
            previewSam3RunResult(selectedRelativeMask);
        }
    } else if (command == QString::fromUtf8("cancel")) {
        _matAnyone2Running = false;
        stopMatAnyone2Worker();
        appendLog(QString::fromUtf8("MatAnyone2 worker cancel acknowledged."));
    } else {
        appendLog(QString::fromUtf8("MatAnyone2 worker response: ") + QString::fromUtf8(line.trimmed()));
    }
    updateUiState();
}

bool FluxAiPanel::verifyMatAnyone2ResultAndWriteManifest(const QJsonObject& workerResult, QString* message, QString* selectedRelativeMask)
{
    if (!_gui || !_gui->getApp() || !_gui->getApp()->getProject()) {
        if (message) { *message = QString::fromUtf8("MatAnyone2 result verification failed: project is unavailable."); }
        return false;
    }

    const QString projectPath = _gui->getApp()->getProject()->getProjectPath();
    const QDir projectDir(projectPath);
    auto pathToProjectRelative = [&](const QString& path, QString* relative) -> bool {
        if (path.isEmpty()) { return false; }
        QString candidate = path;
        if (QDir::isAbsolutePath(candidate)) {
            candidate = projectDir.relativeFilePath(QDir::cleanPath(candidate));
        }
        return sanitizeProjectRelativePath(candidate, relative);
    };

    QString selectedMaskPath = workerResult.value(QString::fromUtf8("selected_mask_path")).toString();
    // If the worker didn't provide a selected mask path, try to find the first mask file in the output directory
    if (selectedMaskPath.isEmpty()) {
        QDir outputDir(_matAnyone2AbsoluteRoot);
        const QStringList maskFiles = outputDir.entryList(QStringList() << QString::fromUtf8("alpha_*.png") << QString::fromUtf8("mask_*.png"), QDir::Files, QDir::Name);
        if (!maskFiles.isEmpty()) {
            selectedMaskPath = outputDir.filePath(maskFiles.first());
        }
    }
    QString selectedMaskRelative;
    if (!pathToProjectRelative(selectedMaskPath, &selectedMaskRelative)) {
        if (message) { *message = QString::fromUtf8("MatAnyone2 result has no safe selected mask path."); }
        return false;
    }
    QFileInfo selectedMaskInfo(projectDir.filePath(selectedMaskRelative));
    if (!selectedMaskInfo.isFile() || selectedMaskInfo.size() <= 0) {
        if (message) { *message = tr("MatAnyone2 selected mask is missing or empty: %1").arg(selectedMaskRelative); }
        return false;
    }

    QString selectedSequencePatternRelative;
    pathToProjectRelative(workerResult.value(QString::fromUtf8("selected_mask_sequence_pattern")).toString(), &selectedSequencePatternRelative);

    const QJsonObject proofs = workerResult.value(QString::fromUtf8("proofs")).toObject();
    const QJsonObject videoProofs = proofs.value(QString::fromUtf8("video")).toObject();
    const int nonzeroPixels = videoProofs.value(QString::fromUtf8("nonzero_pixels")).toInt(0);
    const bool nonzeroUnverified = videoProofs.value(QString::fromUtf8("nonzero_unverified")).toBool(false);
    const int successfulFrames = videoProofs.value(QString::fromUtf8("successful_frame_count")).toInt(0);
    if ((nonzeroPixels <= 0 && !nonzeroUnverified) || successfulFrames <= 0) {
        if (message) { *message = QString::fromUtf8("MatAnyone2 result has no successful alpha frames."); }
        return false;
    }

    QJsonObject manifest;
    manifest.insert(QString::fromUtf8("schema"), QString::fromUtf8("flux.ai.result_manifest.v1"));
    manifest.insert(QString::fromUtf8("created_at_utc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    manifest.insert(QString::fromUtf8("run_id"), _matAnyone2RunId);
    manifest.insert(QString::fromUtf8("task"), _matAnyone2Task);
    manifest.insert(QString::fromUtf8("model_id"), QString::fromUtf8("matanyone2"));
    manifest.insert(QString::fromUtf8("provider_runtime_id"), QString::fromUtf8("sam3"));
    manifest.insert(QString::fromUtf8("source_metadata"), _matAnyone2SourceMetadata);
    manifest.insert(QString::fromUtf8("source_dimensions"), QJsonObject{{QString::fromUtf8("width"), _sourceFrameWidth}, {QString::fromUtf8("height"), _sourceFrameHeight}});
    manifest.insert(QString::fromUtf8("selected_mask_path_project_relative"), selectedMaskRelative);
    if (!selectedSequencePatternRelative.isEmpty()) {
        manifest.insert(QString::fromUtf8("selected_mask_sequence_pattern_project_relative"), selectedSequencePatternRelative);
    }
    manifest.insert(QString::fromUtf8("output_dir_project_relative"), _matAnyone2RelativeRoot);
    manifest.insert(QString::fromUtf8("result_manifest_path_project_relative"), _matAnyone2RelativeRoot + QString::fromUtf8("result_manifest.json"));
    manifest.insert(QString::fromUtf8("worker_result"), workerResult);
    manifest.insert(QString::fromUtf8("proof_nonzero_pixels"), QJsonObject{{QString::fromUtf8("video"), nonzeroPixels}, {QString::fromUtf8("video_unverified"), nonzeroUnverified}});
    manifest.insert(QString::fromUtf8("noncommercial"), true);
    manifest.insert(QString::fromUtf8("warning_text"), workerResult.value(QString::fromUtf8("warning_text")).toString(QString::fromUtf8("NON-COMMERCIAL: MatAnyone2 is non-commercial/research licensed.")));
    manifest.insert(QString::fromUtf8("successful_frame_count"), successfulFrames);

    // Build masks array — scan the output directory for mask frame PNGs
    {
        QDir outputDir(_matAnyone2AbsoluteRoot);
        const QStringList maskFiles = outputDir.entryList(QStringList() << QString::fromUtf8("alpha_*.png") << QString::fromUtf8("mask_*.png"), QDir::Files, QDir::Name);
        QJsonArray masksArray;
        for (const QString& maskFile : maskFiles) {
            QString maskRelative;
            if (pathToProjectRelative(outputDir.filePath(maskFile), &maskRelative)) {
                QJsonObject entry;
                entry.insert(QString::fromUtf8("path"), maskRelative);
                // Try to extract frame number from filename like alpha_000001.png or mask_000001.png
                const QString prefix = maskFile.startsWith(QString::fromUtf8("alpha_")) ? QString::fromUtf8("alpha_") : QString::fromUtf8("mask_");
                const QString frameStr = maskFile.mid(prefix.length(), maskFile.length() - prefix.length() - QString::fromUtf8(".png").length());
                bool okFrame = false;
                const int frameNum = frameStr.toInt(&okFrame);
                if (okFrame) {
                    entry.insert(QString::fromUtf8("frame"), frameNum);
                }
                masksArray.append(entry);
            }
        }
        manifest.insert(QString::fromUtf8("masks"), masksArray);
    }

    QFile out(QDir(_matAnyone2AbsoluteRoot).filePath(QString::fromUtf8("result_manifest.json")));
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (message) { *message = tr("Could not write MatAnyone2 result manifest: %1").arg(out.errorString()); }
        return false;
    }
    out.write(QJsonDocument(manifest).toJson(QJsonDocument::Indented));
    out.write("\n");

    const QString manifestRelative = _matAnyone2RelativeRoot + QString::fromUtf8("result_manifest.json");
    addOrPromoteResultManifest(manifestRelative);
    if (selectedRelativeMask) { *selectedRelativeMask = selectedMaskRelative; }
    if (message) { *message = tr("MatAnyone2 alpha masks generated: %1").arg(manifestRelative); }
    return true;
}

void FluxAiPanel::onMatAnyone2ReadyReadStandardOutput()
{
    if (!_matAnyone2WorkerProcess) { return; }
    _matAnyone2WorkerStdoutBuffer += _matAnyone2WorkerProcess->readAllStandardOutput();
    int newlinePos;
    while ((newlinePos = _matAnyone2WorkerStdoutBuffer.indexOf('\n')) >= 0) {
        const QByteArray line = _matAnyone2WorkerStdoutBuffer.left(newlinePos);
        _matAnyone2WorkerStdoutBuffer = _matAnyone2WorkerStdoutBuffer.mid(newlinePos + 1);
        if (!line.trimmed().isEmpty()) {
            processMatAnyone2WorkerLine(line);
        }
    }
}

void FluxAiPanel::onMatAnyone2Finished(int exitCode, QProcess::ExitStatus exitStatus)
{
    Q_UNUSED(exitCode);
    Q_UNUSED(exitStatus);
    if (!_matAnyone2WorkerStdoutBuffer.isEmpty()) {
        processMatAnyone2WorkerLine(_matAnyone2WorkerStdoutBuffer);
        _matAnyone2WorkerStdoutBuffer.clear();
    }
    if (!_matAnyone2RunPendingRequestId.isEmpty()) {
        _matAnyone2RunPendingRequestId.clear();
        if (_statusLabel) { _statusLabel->setText(QString::fromUtf8("Status: MatAnyone2 worker exited unexpectedly")); }
        resetProgressBar();
        appendLog(QString::fromUtf8("MatAnyone2 worker process exited while inference was pending."));
    }
    _matAnyone2Running = false;
    updateUiState();
}

void FluxAiPanel::onVideoMamaErrorOccurred(QProcess::ProcessError error)
{
    Q_UNUSED(error);
    appendLog(tr("VideoMaMa worker process error: %1").arg(_videoMamaWorkerProcess ? _videoMamaWorkerProcess->errorString() : QString::fromUtf8("unknown")));
}

void FluxAiPanel::onVideoMamaReadyReadStandardOutput()
{
    if (!_videoMamaWorkerProcess) { return; }
    _videoMamaWorkerStdoutBuffer += _videoMamaWorkerProcess->readAllStandardOutput();
    int newlinePos;
    while ((newlinePos = _videoMamaWorkerStdoutBuffer.indexOf('\n')) >= 0) {
        const QByteArray line = _videoMamaWorkerStdoutBuffer.left(newlinePos);
        _videoMamaWorkerStdoutBuffer = _videoMamaWorkerStdoutBuffer.mid(newlinePos + 1);
        if (!line.trimmed().isEmpty()) {
            processVideoMamaWorkerLine(line);
        }
    }
}

void FluxAiPanel::onVideoMamaFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    Q_UNUSED(exitCode);
    Q_UNUSED(exitStatus);
    if (!_videoMamaWorkerStdoutBuffer.isEmpty()) {
        processVideoMamaWorkerLine(_videoMamaWorkerStdoutBuffer);
        _videoMamaWorkerStdoutBuffer.clear();
    }
    if (!_videoMamaRunPendingRequestId.isEmpty()) {
        _videoMamaRunPendingRequestId.clear();
        if (_statusLabel) { _statusLabel->setText(QString::fromUtf8("Status: VideoMaMa worker exited unexpectedly")); }
        resetProgressBar();
        appendLog(QString::fromUtf8("VideoMaMa worker process exited while inference was pending."));
    }
    _videoMamaRunning = false;
    updateUiState();
}

bool FluxAiPanel::ensureVideoMamaWorkerStarted(QString* message)
{
    if (_videoMamaWorkerProcess && _videoMamaWorkerProcess->state() != QProcess::NotRunning) {
        if (message) { *message = QString::fromUtf8("VideoMaMa worker already running."); }
        return true;
    }
    if (!_videoMamaWorkerProcess) {
        if (message) { *message = QString::fromUtf8("VideoMaMa worker process object is missing."); }
        return false;
    }

    const QString root = repoRoot();
    const QString runtimeScript = QDir(root).filePath(QString::fromUtf8("tools/ai/flux_provider_runtime.py"));
    const QString workerScript = QDir(root).filePath(QString::fromUtf8("tools/ai/videomama_worker.py"));
    if (!QFileInfo(runtimeScript).isFile() || !QFileInfo(workerScript).isFile()) {
        if (message) { *message = tr("VideoMaMa worker startup failed: missing runtime script %1 or worker script %2.").arg(runtimeScript, workerScript); }
        return false;
    }

    QProcess resolver;
    resolver.setWorkingDirectory(root);
    resolver.start(QString::fromUtf8("python3"), QStringList() << runtimeScript << QString::fromUtf8("python") << QString::fromUtf8("videomama"));
    if (!resolver.waitForStarted(5000) || !resolver.waitForFinished(10000) || resolver.exitStatus() != QProcess::NormalExit || resolver.exitCode() != 0) {
        const QString stderrText = QString::fromUtf8(resolver.readAllStandardError()).trimmed();
        if (message) { *message = tr("VideoMaMa worker startup failed: provider python resolution failed. stderr: %1").arg(stderrText); }
        return false;
    }
    const QString providerPython = QString::fromUtf8(resolver.readAllStandardOutput()).trimmed();
    if (providerPython.isEmpty() || !QFileInfo(providerPython).isExecutable()) {
        if (message) { *message = tr("VideoMaMa worker startup failed: provider python is not executable: %1").arg(providerPython); }
        return false;
    }

    _videoMamaWorkerStdoutBuffer.clear();
    _videoMamaWorkerPendingCommands.clear();
    _videoMamaWorkerProcess->setWorkingDirectory(root);
    _videoMamaWorkerProcess->start(providerPython, QStringList() << workerScript);
    if (!_videoMamaWorkerProcess->waitForStarted(10000)) {
        const QString stderrText = QString::fromUtf8(_videoMamaWorkerProcess->readAllStandardError()).trimmed();
        if (message) { *message = tr("VideoMaMa worker startup failed: could not start %1 %2. stderr: %3").arg(providerPython, workerScript, stderrText); }
        return false;
    }
    appendLog(tr("VideoMaMa worker started: %1 %2").arg(providerPython, workerScript));
    if (message) { *message = QString::fromUtf8("VideoMaMa worker started."); }
    return true;
}

QString FluxAiPanel::sendVideoMamaWorkerRequest(const QJsonObject& request)
{
    if (!_videoMamaWorkerProcess || _videoMamaWorkerProcess->state() == QProcess::NotRunning) {
        appendLog(QString::fromUtf8("VideoMaMa worker request failed: worker is not running."));
        return QString();
    }
    QJsonObject outbound = request;
    const QString command = outbound.value(QString::fromUtf8("command")).toString();
    if (command.isEmpty()) {
        appendLog(QString::fromUtf8("VideoMaMa worker request failed: command is empty."));
        return QString();
    }
    QString id = outbound.value(QString::fromUtf8("id")).toString();
    if (id.isEmpty()) {
        id = QString::fromUtf8("vmm-%1").arg(_videoMamaWorkerNextRequestId++);
        outbound.insert(QString::fromUtf8("id"), id);
    }
    _videoMamaWorkerPendingCommands.insert(id, command);
    const QByteArray payload = QJsonDocument(outbound).toJson(QJsonDocument::Compact) + QByteArray("\n");
    const qint64 written = _videoMamaWorkerProcess->write(payload);
    if (written != payload.size() || !_videoMamaWorkerProcess->waitForBytesWritten(5000)) {
        _videoMamaWorkerPendingCommands.remove(id);
        appendLog(tr("VideoMaMa worker request failed: could not write %1 request %2.").arg(command, id));
        return QString();
    }
    QJsonObject found;
    found.insert(QString::fromUtf8("request_id"), id);
    found.insert(QString::fromUtf8("command"), command);
    found.insert(QString::fromUtf8("payload_bytes"), payload.size());
    writeAiLog(QString::fromUtf8("info"), QString::fromUtf8("worker"), QString::fromUtf8("videomama_request_sent"),
               QString::fromUtf8("pass"), QString::fromUtf8("VideoMaMa worker request written."), QString(), found);
    return id;
}

void FluxAiPanel::processVideoMamaWorkerLine(const QByteArray& line)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(line, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        appendLog(QString::fromUtf8("videomama worker stdout: ") + QString::fromUtf8(line.trimmed()));
        return;
    }
    const QJsonObject response = doc.object();
    if (response.value(QString::fromUtf8("event")).toString() == QString::fromUtf8("ready")) {
        appendLog(QString::fromUtf8("VideoMaMa worker ready."));
        const QString wt = response.value(QString::fromUtf8("warning_text")).toString();
        if (!wt.isEmpty()) {
            appendLog(wt);
        }
        return;
    }
    if (response.value(QString::fromUtf8("event")).toString() == QString::fromUtf8("progress")) {
        const QString id = response.value(QString::fromUtf8("id")).toString();
        QJsonObject payload = response.value(QString::fromUtf8("payload")).toObject();
        if (payload.isEmpty()) { payload = response; }
        const QString msg = payload.value(QString::fromUtf8("message")).toString(QString::fromUtf8("VideoMaMa working"));
        if (_statusLabel) { _statusLabel->setText(QString::fromUtf8("Status: ") + msg); }
        updateProgressBarFromPayload(payload, msg);
        appendLog(QString::fromUtf8("VideoMaMa progress: ") + msg);
        return;
    }

    const QString id = response.value(QString::fromUtf8("id")).toString();
    const QString command = response.value(QString::fromUtf8("command")).toString(_videoMamaWorkerPendingCommands.value(id));
    const bool ok = response.value(QString::fromUtf8("ok")).toBool(false);
    QJsonObject payload = response.value(QString::fromUtf8("payload")).toObject();
    if (payload.isEmpty() && response.contains(QString::fromUtf8("result"))) { payload = response; }
    if (!id.isEmpty()) { _videoMamaWorkerPendingCommands.remove(id); }

    if (!ok) {
        const QString errorText = payload.value(QString::fromUtf8("error")).toString(
            response.value(QString::fromUtf8("error")).toString(
                payload.value(QString::fromUtf8("status")).toString(QString::fromUtf8("blocked"))));
        if (id == _videoMamaRunPendingRequestId) { _videoMamaRunPendingRequestId.clear(); }
        if (command == QString::fromUtf8("load")) {
            _videoMamaPendingInferRequest = QJsonObject();
            _videoMamaRunning = false;
            appendLog(tr("VideoMaMa load failed; canceling pending infer_video."));
        }
        if (command == QString::fromUtf8("infer_video")) {
            _videoMamaRunning = false;
            stopVideoMamaWorker();
        }
        if (_statusLabel) { _statusLabel->setText(QString::fromUtf8("Status: failure")); }
        resetProgressBar();
        const QJsonArray blockers = payload.value(QString::fromUtf8("blockers")).toArray();
        if (!blockers.isEmpty()) {
            for (const QJsonValue& b : blockers) {
                appendLog(tr("VideoMaMa blocked: %1").arg(b.toString()));
            }
        } else {
            appendLog(tr("VideoMaMa worker %1 request failed: %2").arg(command, errorText));
        }
        const QString traceback = payload.value(QString::fromUtf8("traceback")).toString();
        if (!traceback.isEmpty()) {
            appendLog(tr("VideoMaMa worker traceback: %1").arg(traceback));
        }
        updateUiState();
        return;
    }

    if (command == QString::fromUtf8("load")) {
        appendLog(tr("VideoMaMa model loaded successfully."));
        if (!_videoMamaPendingInferRequest.isEmpty()) {
            const QString inferId = sendVideoMamaWorkerRequest(_videoMamaPendingInferRequest);
            _videoMamaPendingInferRequest = QJsonObject();
            if (inferId.isEmpty()) {
                if (_statusLabel) { _statusLabel->setText(QString::fromUtf8("Status: failure")); }
                appendLog(QString::fromUtf8("VideoMaMa infer_video request failed: worker request could not be sent."));
                resetProgressBar();
                updateUiState();
                return;
            }
            _videoMamaRunPendingRequestId = inferId;
            _videoMamaRunning = true;
            if (_statusLabel) { _statusLabel->setText(QString::fromUtf8("Status: VideoMaMa running")); }
            setProgressBarValue(0, QString::fromUtf8("Starting VideoMaMa inference"));
            appendLog(QString::fromUtf8("VideoMaMa model loaded; sending infer_video request."));
        }
    } else if (command == QString::fromUtf8("infer_video")) {
        QString verifyMessage;
        QString selectedRelativeMask;
        const bool verified = verifyVideoMamaResultAndWriteManifest(payload, &verifyMessage, &selectedRelativeMask);
        _videoMamaRunPendingRequestId.clear();
        _videoMamaRunning = false;
        stopVideoMamaWorker();
        if (_statusLabel) { _statusLabel->setText(verified ? QString::fromUtf8("Status: success") : QString::fromUtf8("Status: failure")); }
        if (verified) {
            setProgressBarValue(100, QString::fromUtf8("Complete"));
        } else {
            resetProgressBar();
        }
        appendLog(verifyMessage);
        if (verified) {
            previewSam3RunResult(selectedRelativeMask);
        }
    } else if (command == QString::fromUtf8("cancel")) {
        _videoMamaRunning = false;
        stopVideoMamaWorker();
        appendLog(QString::fromUtf8("VideoMaMa worker cancel acknowledged."));
    } else {
        appendLog(QString::fromUtf8("VideoMaMa worker response: ") + QString::fromUtf8(line.trimmed()));
    }
    updateUiState();
}

void FluxAiPanel::stopVideoMamaWorker()
{
    if (!_videoMamaWorkerProcess || _videoMamaWorkerProcess->state() == QProcess::NotRunning) {
        return;
    }
    appendLog(QString::fromUtf8("Stopping VideoMaMa worker to free GPU"));
    QJsonObject shutdown;
    shutdown.insert(QString::fromUtf8("command"), QString::fromUtf8("shutdown"));
    sendVideoMamaWorkerRequest(shutdown);
    if (!_videoMamaWorkerProcess->waitForFinished(3000)) {
        _videoMamaWorkerProcess->kill();
        _videoMamaWorkerProcess->waitForFinished(1000);
    }
    _videoMamaWorkerStdoutBuffer.clear();
    _videoMamaWorkerPendingCommands.clear();
    _videoMamaRunPendingRequestId.clear();
    _videoMamaPendingInferRequest = QJsonObject();
    _videoMamaCancelRequested = false;
    _videoMamaRunning = false;
}

void FluxAiPanel::runVideoMama()
{
    const QString modelId = QString::fromUtf8("videomama");
    if (!hasSelectedSource()) {
        appendLog(tr("Cannot start VideoMaMa: no source is selected."));
        if (_statusLabel) { _statusLabel->setText(QString::fromUtf8("Status: select a source first")); }
        updateUiState();
        return;
    }
    if (!_gui || !_gui->getApp() || !_gui->getApp()->getProject()) {
        appendLog(tr("Cannot start VideoMaMa: project is unavailable."));
        updateUiState();
        return;
    }
    ProjectPtr project = _gui->getApp()->getProject();
    if (!project->hasProjectBeenSavedByUser()) {
        if (!_gui->saveProjectAs()) {
            const QString canceled = QString::fromUtf8("VideoMaMa canceled: project must be saved first.");
            if (_statusLabel) { _statusLabel->setText(QString::fromUtf8("Status: ") + canceled); }
            appendLog(canceled);
            return;
        }
    }
    if (!project->hasProjectBeenSavedByUser()) {
        const QString canceled = QString::fromUtf8("VideoMaMa canceled: project must be saved first.");
        if (_statusLabel) { _statusLabel->setText(QString::fromUtf8("Status: ") + canceled); }
        appendLog(canceled);
        return;
    }

    // Show NON-COMMERCIAL warning
    const QString path = manifestPath();
    QString warningText = QString::fromUtf8("NON-COMMERCIAL: VideoMaMa is CC BY-NC 4.0 licensed.");
    QFile manifestFile(path);
    if (manifestFile.open(QIODevice::ReadOnly)) {
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(manifestFile.readAll(), &parseError);
        if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
            const QJsonArray models = doc.object().value(QString::fromUtf8("models")).toArray();
            for (const QJsonValue& value : models) {
                const QJsonObject model = value.toObject();
                if (model.value(QString::fromUtf8("id")).toString() == modelId) {
                    const QString manifestWarning = model.value(QString::fromUtf8("warning_text")).toString();
                    if (!manifestWarning.isEmpty()) {
                        warningText = manifestWarning;
                    }
                    break;
                }
            }
        }
    }
    appendLog(warningText);

    QString message;
    // VideoMaMa must reuse the selected SAM3 result's processed source sequence.
    // Do not call refreshSourceFrameMetadata() here: that would export reader/source footage.
    if (!_sourceAIPaintNode || !_sourceAIPaintNode->isActivated()) {
        if (_statusLabel) { _statusLabel->setText(QString::fromUtf8("Status: source capture blocked")); }
        appendLog(QString::fromUtf8("VideoMaMa blocked: selected layer has no active AI Paint source context (no footage re-export)."));
        updateUiState();
        return;
    }

    // Validate video range
    int rangeStart = 0, rangeEnd = 0;
    if (!selectedFrameRange(&rangeStart, &rangeEnd, &message) || rangeEnd <= rangeStart) {
        if (_statusLabel) { _statusLabel->setText(QString::fromUtf8("Status: VideoMaMa requires a video range (end > start)")); }
        appendLog(tr("VideoMaMa requires a video range (end > start). %1").arg(message));
        updateUiState();
        return;
    }
    int maskGenerationTimeOffset = _sourceTimeOffset;
    int selectedHistoryStart = 0;
    int selectedHistoryEnd = 0;
    if (selectedResultGenerationRange(&selectedHistoryStart, &selectedHistoryEnd, &maskGenerationTimeOffset)) {
        rangeStart = selectedHistoryStart;
        rangeEnd = selectedHistoryEnd;
        appendLog(tr("VideoMaMa using selected SAM3 history frame range %1-%2.").arg(rangeStart).arg(rangeEnd));
    }

    // Resolve base mask from the selected SAM3 result. Do not use AI Paint live
    // preview here: VideoMaMa must consume the exported SAM3 mask sequence.
    QString baseMaskAbsolute;
    QString baseMaskRelative;
    QString maskSequencePatternForWorker;  // project-relative sequence pattern for per-frame mask lookup

    if (baseMaskAbsolute.isEmpty()) {
        QString maskRelative, maskMessage;
        QString sequencePattern;
        if (selectedResultMaskProjectRelative(&maskRelative, &maskMessage, &sequencePattern)) {
            // If a sequence pattern (from SAM3 tracking history) is available, pass it to the worker
            // for per-frame mask resolution AND extract the first frame as baseMaskAbsolute fallback.
            if (!sequencePattern.isEmpty() && sequencePattern.contains(QLatin1Char('#'))) {
                maskSequencePatternForWorker = sequencePattern;
                appendLog(tr("VideoMaMa using mask sequence pattern from history: %1").arg(sequencePattern));

                QRegularExpression hashRe(QLatin1String("#{2,}"));
                QRegularExpressionMatch match = hashRe.match(sequencePattern);
                if (match.hasMatch()) {
                    const int hashLen = match.capturedLength();
                    const int firstTimelineFrame = rangeStart;
                    const QString frameNumStr = QString::number(firstTimelineFrame).rightJustified(hashLen, QLatin1Char('0'));
                    const QString seqMaskRelative = sequencePattern.mid(0, match.capturedStart()) + frameNumStr + sequencePattern.mid(match.capturedEnd());
                    const QString seqMaskAbsolute = QDir(project->getProjectPath()).filePath(seqMaskRelative);
                    if (QFileInfo(seqMaskAbsolute).isFile() && QFileInfo(seqMaskAbsolute).size() > 0) {
                        baseMaskAbsolute = seqMaskAbsolute;
                        baseMaskRelative = seqMaskRelative;
                        appendLog(tr("VideoMaMa using SAM3 mask frame timeline %1 from video sequence: %2").arg(firstTimelineFrame).arg(seqMaskRelative));
                    }
                }
            }
            if (baseMaskAbsolute.isEmpty()) {
                const QString absMask = QDir(project->getProjectPath()).filePath(maskRelative);
                if (QFileInfo(absMask).isFile() && QFileInfo(absMask).size() > 0) {
                    baseMaskAbsolute = absMask;
                    baseMaskRelative = maskRelative;
                    appendLog(tr("VideoMaMa using selected result mask: %1").arg(maskRelative));
                }
            }
        } else {
            if (_statusLabel) { _statusLabel->setText(QString::fromUtf8("Status: VideoMaMa requires a base mask")); }
            appendLog(tr("VideoMaMa requires a base mask. Use AI Paint to select a subject, or select a result from history. %1").arg(maskMessage));
            updateUiState();
            return;
        }
    }

    if (maskSequencePatternForWorker.isEmpty()) {
        if (_statusLabel) { _statusLabel->setText(QString::fromUtf8("Status: SAM3 mask sequence required")); }
        appendLog(QString::fromUtf8("VideoMaMa blocked: selected result does not provide an exported SAM3 mask sequence. Select/run SAM3 for this range first (no footage re-export)."));
        updateUiState();
        return;
    }

    if (baseMaskAbsolute.isEmpty() || !QFileInfo(baseMaskAbsolute).isFile() || QFileInfo(baseMaskAbsolute).size() <= 0) {
        if (_statusLabel) { _statusLabel->setText(QString::fromUtf8("Status: base mask file is missing or empty")); }
        appendLog(tr("VideoMaMa base mask file is missing or empty."));
        updateUiState();
        return;
    }

    // VideoMaMa must reuse SAM3 exported footage and mask — never re-export.
    QJsonObject sourceMetadata = _sourceFrameMetadata;
    QJsonObject frameRangeMetadata = selectedFrameRangeMetadata();
    frameRangeMetadata.insert(QString::fromUtf8("source_start"), rangeStart);
    frameRangeMetadata.insert(QString::fromUtf8("source_end"), rangeEnd);
    frameRangeMetadata.insert(QString::fromUtf8("duration_frames"), qMax(0, rangeEnd - rangeStart + 1));
    frameRangeMetadata.insert(QString::fromUtf8("time_offset"), maskGenerationTimeOffset);
    frameRangeMetadata.insert(QString::fromUtf8("timeline_start"), rangeStart + maskGenerationTimeOffset);
    frameRangeMetadata.insert(QString::fromUtf8("timeline_end"), rangeEnd + maskGenerationTimeOffset);
    sourceMetadata.insert(QString::fromUtf8("videomama_frame_range"), frameRangeMetadata);
    _sourceFrameMetadata = sourceMetadata;

    QString sequenceDir;
    QJsonObject sequenceMetadata;

    // Only source: selected SAM3 result manifest with sam3_source_sequence
    {
        const QString selectedManifestRel = selectedResultManifestProjectRelative();
        if (selectedManifestRel.isEmpty()) {
            if (_statusLabel) { _statusLabel->setText(QString::fromUtf8("Status: no SAM3 result selected")); }
            appendLog(QString::fromUtf8("VideoMaMa blocked: no AI result selected. Please select a SAM3 result from history first (no footage re-export)."));
            updateUiState();
            return;
        }

        QString safeManifest;
        if (!sanitizeProjectRelativePath(selectedManifestRel, &safeManifest)) {
            if (_statusLabel) { _statusLabel->setText(QString::fromUtf8("Status: unsafe manifest path")); }
            appendLog(QString::fromUtf8("VideoMaMa blocked: selected result manifest has unsafe path: %1").arg(selectedManifestRel));
            updateUiState();
            return;
        }

        const QString manifestAbsPath = QDir(project->getProjectPath()).filePath(safeManifest);
        QFile mf(manifestAbsPath);
        if (!mf.open(QIODevice::ReadOnly)) {
            if (_statusLabel) { _statusLabel->setText(QString::fromUtf8("Status: cannot read manifest")); }
            appendLog(QString::fromUtf8("VideoMaMa blocked: cannot read selected result manifest: %1").arg(safeManifest));
            updateUiState();
            return;
        }

        QJsonParseError mpe;
        const QJsonDocument mdoc = QJsonDocument::fromJson(mf.readAll(), &mpe);
        if (mpe.error != QJsonParseError::NoError || !mdoc.isObject()) {
            if (_statusLabel) { _statusLabel->setText(QString::fromUtf8("Status: invalid manifest")); }
            appendLog(QString::fromUtf8("VideoMaMa blocked: selected result manifest is not valid JSON: %1").arg(mpe.errorString()));
            updateUiState();
            return;
        }

        const QJsonObject mobj = mdoc.object();
        const QString manifestModelId = mobj.value(QString::fromUtf8("model_id")).toString();

        // Require SAM3 manifest
        if (manifestModelId != QString::fromUtf8("sam3_transformers")) {
            if (_statusLabel) { _statusLabel->setText(QString::fromUtf8("Status: selected result is not SAM3")); }
            appendLog(QString::fromUtf8("VideoMaMa blocked: selected result is from '%1', not SAM3. Please select a SAM3 result first (no footage re-export).").arg(manifestModelId.isEmpty() ? QString::fromUtf8("unknown") : manifestModelId));
            updateUiState();
            return;
        }

        const QJsonObject srcMeta = mobj.value(QString::fromUtf8("source_metadata")).toObject();
        const QJsonObject manifestSeq = srcMeta.value(QString::fromUtf8("sam3_source_sequence")).toObject();

        if (manifestSeq.isEmpty()) {
            if (_statusLabel) { _statusLabel->setText(QString::fromUtf8("Status: SAM3 manifest missing source sequence")); }
            appendLog(QString::fromUtf8("VideoMaMa blocked: selected SAM3 result manifest has no sam3_source_sequence metadata. Re-run SAM3 for the requested range (no footage re-export)."));
            updateUiState();
            return;
        }

        const QString baseDir = manifestSeq.value(QString::fromUtf8("temporary_source_sequence_dir")).toString();
        const bool baseDirAbsent = baseDir.isEmpty();

        const QJsonArray manifestFrames = manifestSeq.value(QString::fromUtf8("frames")).toArray();
        if (manifestFrames.isEmpty()) {
            if (_statusLabel) { _statusLabel->setText(QString::fromUtf8("Status: SAM3 source sequence has no frames")); }
            appendLog(QString::fromUtf8("VideoMaMa blocked: SAM3 source sequence frames array is empty (no footage re-export)."));
            updateUiState();
            return;
        }

        // Range check: use source_frame_start/end if present, otherwise infer from frames[].source_frame
        const int manifestStart = manifestSeq.value(QString::fromUtf8("source_frame_start")).toInt(-1);
        const int manifestEnd = manifestSeq.value(QString::fromUtf8("source_frame_end")).toInt(-1);
        int effectiveStart = manifestStart;
        int effectiveEnd = manifestEnd;
        if (effectiveStart < 0 || effectiveEnd < 0) {
            int inferredFirst = std::numeric_limits<int>::max();
            int inferredLast = std::numeric_limits<int>::min();
            for (const QJsonValue& frameVal : manifestFrames) {
                const int sf = frameVal.toObject().value(QString::fromUtf8("source_frame")).toInt(-1);
                if (sf >= 0) {
                    inferredFirst = qMin(inferredFirst, sf);
                    inferredLast = qMax(inferredLast, sf);
                }
            }
            if (inferredFirst <= inferredLast) {
                effectiveStart = inferredFirst;
                effectiveEnd = inferredLast;
            }
        }
        const int expectedFrames = qMax(0, rangeEnd - rangeStart + 1);
        if (expectedFrames <= 0 || manifestFrames.size() != expectedFrames) {
            if (_statusLabel) { _statusLabel->setText(QString::fromUtf8("Status: SAM3 sequence length mismatch")); }
            appendLog(QString::fromUtf8("VideoMaMa blocked: selected SAM3 source sequence has %1 frame(s), but selected mask numbering %2-%3 requires %4.")
                      .arg(manifestFrames.size()).arg(rangeStart).arg(rangeEnd).arg(expectedFrames));
            updateUiState();
            return;
        }

        // Build normalized frames array with absolute paths for worker safety
        QJsonArray normalizedFrames;
        bool allFramesExist = true;
        QString resolvedBaseDir;
        for (const QJsonValue& frameVal : manifestFrames) {
            const QJsonObject frameObj = frameVal.toObject();
            const QString framePath = frameObj.value(QString::fromUtf8("path")).toString();
            QString frameAbsPath;
            if (QDir::isAbsolutePath(framePath)) {
                frameAbsPath = QDir::cleanPath(framePath);
            } else if (!baseDir.isEmpty()) {
                frameAbsPath = QDir::cleanPath(QDir(baseDir).filePath(framePath));
            } else {
                // Relative path with no baseDir: resolve against project directory
                frameAbsPath = QDir::cleanPath(QDir(project->getProjectPath()).filePath(framePath));
            }
            if (!QFileInfo(frameAbsPath).isFile()) {
                allFramesExist = false;
                appendLog(QString::fromUtf8("VideoMaMa: SAM3 source frame missing: %1 (resolved to %2)").arg(framePath, frameAbsPath));
                break;
            }
            QJsonObject normalizedFrame = frameObj;
            const int frameNumber = rangeStart + normalizedFrames.size();
            normalizedFrame.insert(QString::fromUtf8("path"), frameAbsPath);
            normalizedFrame.insert(QString::fromUtf8("source_frame"), frameNumber);
            normalizedFrame.insert(QString::fromUtf8("timeline_frame"), frameNumber);
            normalizedFrames.append(normalizedFrame);
            if (resolvedBaseDir.isEmpty()) {
                resolvedBaseDir = QFileInfo(frameAbsPath).absolutePath();
            }
        }

        if (!allFramesExist) {
            if (_statusLabel) { _statusLabel->setText(QString::fromUtf8("Status: SAM3 source frames missing")); }
            appendLog(QString::fromUtf8("VideoMaMa blocked: one or more SAM3 source sequence frames are missing on disk. Re-run SAM3 for range %1-%2 (no footage re-export).").arg(rangeStart).arg(rangeEnd));
            updateUiState();
            return;
        }

        sequenceDir = baseDir.isEmpty() ? resolvedBaseDir : baseDir;
        sequenceMetadata = manifestSeq;
        sequenceMetadata.insert(QString::fromUtf8("frames"), normalizedFrames);
        sequenceMetadata.insert(QString::fromUtf8("source_frame_start"), rangeStart);
        sequenceMetadata.insert(QString::fromUtf8("source_frame_end"), rangeEnd);
        sequenceMetadata.insert(QString::fromUtf8("timeline_frame_start"), rangeStart);
        sequenceMetadata.insert(QString::fromUtf8("timeline_frame_end"), rangeEnd);
        if (baseDirAbsent) {
            sequenceMetadata.insert(QString::fromUtf8("temporary_source_sequence_dir"), sequenceDir);
        }
        if (baseDirAbsent) {
            appendLog(QString::fromUtf8("VideoMaMa reusing SAM3 frame paths — directory metadata was absent, resolved %1 frame(s) from manifest paths").arg(normalizedFrames.size()));
        }
        appendLog(QString::fromUtf8("VideoMaMa reusing SAM3 processed source sequence for range %1-%2 — no footage re-export").arg(rangeStart).arg(rangeEnd));
    }

    sourceMetadata.insert(QString::fromUtf8("videomama_source_sequence"), sequenceMetadata);
    _sourceFrameMetadata = sourceMetadata;

    // Free GPU by stopping SAM3 and MatAnyone2 workers before starting VideoMaMa
    stopSam3PersistentWorker();
    stopMatAnyone2Worker();

    // Start worker
    if (!ensureVideoMamaWorkerStarted(&message)) {
        if (_statusLabel) { _statusLabel->setText(QString::fromUtf8("Status: failure")); }
        appendLog(message);
        updateUiState();
        return;
    }

    const QString runId = QString::fromUtf8("run-") + QDateTime::currentDateTimeUtc().toString(QString::fromUtf8("yyyyMMdd-HHmmss"));
    const QString slug = taskSlug(_taskCombo ? _taskCombo->currentText() : QString::fromUtf8("refine-matte"));
    const QString relativeRoot = QString::fromUtf8("FluxGenerated/AI/%1/%2/%3/").arg(slug, modelId, runId);
    const QString absoluteRoot = QDir(project->getProjectPath()).filePath(relativeRoot);
    if (!QDir().mkpath(absoluteRoot)) {
        if (_statusLabel) { _statusLabel->setText(QString::fromUtf8("Status: failure")); }
        appendLog(tr("Could not create VideoMaMa output directory: %1").arg(absoluteRoot));
        updateUiState();
        return;
    }

    _videoMamaAbsoluteRoot = absoluteRoot;
    _videoMamaRelativeRoot = relativeRoot;
    _videoMamaRunId = runId;
    _videoMamaTask = _taskCombo ? _taskCombo->currentText() : QString();
    _videoMamaSourceMetadata = sourceMetadata;
    _videoMamaCancelRequested = false;
    _lastResultManifestProjectRelative.clear();

    QJsonObject inferRequest;
    inferRequest.insert(QString::fromUtf8("command"), QString::fromUtf8("infer_video"));
    inferRequest.insert(QString::fromUtf8("frames"), sequenceMetadata.value(QString::fromUtf8("frames")).toArray());
    inferRequest.insert(QString::fromUtf8("first_frame_mask"), baseMaskAbsolute);
    inferRequest.insert(QString::fromUtf8("first_frame_mask_project_relative"), baseMaskRelative);
    // Pass per-frame mask sequence pattern (project-relative) for worker per-frame mask resolution
    if (!maskSequencePatternForWorker.isEmpty()) {
        inferRequest.insert(QString::fromUtf8("mask_sequence_pattern"), maskSequencePatternForWorker);
        inferRequest.insert(QString::fromUtf8("mask_sequence_pattern_project_relative"), maskSequencePatternForWorker);
    }
    // Pass project dir so worker can resolve project-relative mask paths
    inferRequest.insert(QString::fromUtf8("project_dir"), project->getProjectPath());
    inferRequest.insert(QString::fromUtf8("output_dir"), absoluteRoot);
    inferRequest.insert(QString::fromUtf8("source_width"), sequenceMetadata.value(QString::fromUtf8("width")).toInt(_sourceFrameWidth));
    inferRequest.insert(QString::fromUtf8("source_height"), sequenceMetadata.value(QString::fromUtf8("height")).toInt(_sourceFrameHeight));
    inferRequest.insert(QString::fromUtf8("source_range_start"), rangeStart);
    inferRequest.insert(QString::fromUtf8("source_range_end"), rangeEnd);
    inferRequest.insert(QString::fromUtf8("frame_range"), frameRangeMetadata);
    inferRequest.insert(QString::fromUtf8("source_sequence"), sequenceMetadata);
    inferRequest.insert(QString::fromUtf8("source_metadata"), sourceMetadata);
    const int videoMamaChunkSize = _videoMamaBatchCombo ? _videoMamaBatchCombo->currentData().toInt() : 16;
    const int videoMamaOverlap = _videoMamaOverlapCombo ? _videoMamaOverlapCombo->currentData().toInt() : 2;
    inferRequest.insert(QString::fromUtf8("chunk_size"), videoMamaChunkSize);
    inferRequest.insert(QString::fromUtf8("overlap"), videoMamaOverlap);
    inferRequest.insert(QString::fromUtf8("model_id"), modelId);
    inferRequest.insert(QString::fromUtf8("noncommercial_acknowledged"), true);
    inferRequest.insert(QString::fromUtf8("run_id"), runId);
    _videoMamaPendingInferRequest = inferRequest;

    QJsonObject loadRequest;
    loadRequest.insert(QString::fromUtf8("command"), QString::fromUtf8("load"));

    const QString loadId = sendVideoMamaWorkerRequest(loadRequest);
    if (loadId.isEmpty()) {
        _videoMamaPendingInferRequest = QJsonObject();
        if (_statusLabel) { _statusLabel->setText(QString::fromUtf8("Status: failure")); }
        appendLog(QString::fromUtf8("VideoMaMa load request failed: worker request could not be sent."));
        updateUiState();
        return;
    }
    _videoMamaRunPendingRequestId = loadId;
    _videoMamaRunning = true;
    if (_outputLabel) { _outputLabel->setText(QString::fromUtf8("Output: ") + relativeRoot); }
    if (_statusLabel) { _statusLabel->setText(QString::fromUtf8("Status: VideoMaMa loading model")); }
    setProgressBarValue(0, QString::fromUtf8("Loading VideoMaMa model"));
    appendLog(QString::fromUtf8("Starting VideoMaMa: loading model before inference: %1; sourceRange=%2-%3; baseMask=%4")
              .arg(runId)
              .arg(rangeStart)
              .arg(rangeEnd)
              .arg(baseMaskRelative));
    appendLog(QString::fromUtf8("VideoMaMa settings: batchFrames=%1; blendOverlap=%2")
              .arg(videoMamaChunkSize)
              .arg(videoMamaOverlap));
    updateUiState();
}

bool FluxAiPanel::verifyVideoMamaResultAndWriteManifest(const QJsonObject& workerResult, QString* message, QString* selectedRelativeMask)
{
    if (!_gui || !_gui->getApp() || !_gui->getApp()->getProject()) {
        if (message) { *message = QString::fromUtf8("VideoMaMa result verification failed: project is unavailable."); }
        return false;
    }

    const QString projectPath = _gui->getApp()->getProject()->getProjectPath();
    const QDir projectDir(projectPath);
    auto pathToProjectRelative = [&](const QString& path, QString* relative) -> bool {
        if (path.isEmpty()) { return false; }
        QString candidate = path;
        if (QDir::isAbsolutePath(candidate)) {
            candidate = projectDir.relativeFilePath(QDir::cleanPath(candidate));
        }
        return sanitizeProjectRelativePath(candidate, relative);
    };

    QString selectedMaskPath = workerResult.value(QString::fromUtf8("selected_mask_path")).toString();
    if (selectedMaskPath.isEmpty()) {
        QDir outputDir(_videoMamaAbsoluteRoot);
        const QStringList maskFiles = outputDir.entryList(QStringList() << QString::fromUtf8("alpha_*.png") << QString::fromUtf8("mask_*.png"), QDir::Files, QDir::Name);
        if (!maskFiles.isEmpty()) {
            selectedMaskPath = outputDir.filePath(maskFiles.first());
        }
    }
    QString selectedMaskRelative;
    if (!pathToProjectRelative(selectedMaskPath, &selectedMaskRelative)) {
        if (message) { *message = QString::fromUtf8("VideoMaMa result has no safe selected mask path."); }
        return false;
    }
    QFileInfo selectedMaskInfo(projectDir.filePath(selectedMaskRelative));
    if (!selectedMaskInfo.isFile() || selectedMaskInfo.size() <= 0) {
        if (message) { *message = tr("VideoMaMa selected mask is missing or empty: %1").arg(selectedMaskRelative); }
        return false;
    }

    QString selectedSequencePatternRelative;
    pathToProjectRelative(workerResult.value(QString::fromUtf8("selected_mask_sequence_pattern")).toString(), &selectedSequencePatternRelative);

    const QJsonObject proofs = workerResult.value(QString::fromUtf8("proofs")).toObject();
    const QJsonObject videoProofs = proofs.value(QString::fromUtf8("video")).toObject();
    const int nonzeroPixels = videoProofs.value(QString::fromUtf8("nonzero_pixels")).toInt(0);
    const bool nonzeroUnverified = videoProofs.value(QString::fromUtf8("nonzero_unverified")).toBool(false);
    const int successfulFrames = videoProofs.value(QString::fromUtf8("successful_frame_count")).toInt(0);
    if ((nonzeroPixels <= 0 && !nonzeroUnverified) || successfulFrames <= 0) {
        if (message) { *message = QString::fromUtf8("VideoMaMa result has no successful alpha frames."); }
        return false;
    }

    QJsonObject manifest;
    manifest.insert(QString::fromUtf8("schema"), QString::fromUtf8("flux.ai.result_manifest.v1"));
    manifest.insert(QString::fromUtf8("created_at_utc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    manifest.insert(QString::fromUtf8("run_id"), _videoMamaRunId);
    manifest.insert(QString::fromUtf8("task"), _videoMamaTask);
    manifest.insert(QString::fromUtf8("model_id"), QString::fromUtf8("videomama"));
    manifest.insert(QString::fromUtf8("provider_runtime_id"), QString::fromUtf8("videomama"));
    manifest.insert(QString::fromUtf8("source_metadata"), _videoMamaSourceMetadata);
    manifest.insert(QString::fromUtf8("source_dimensions"), QJsonObject{{QString::fromUtf8("width"), _sourceFrameWidth}, {QString::fromUtf8("height"), _sourceFrameHeight}});
    manifest.insert(QString::fromUtf8("selected_mask_path_project_relative"), selectedMaskRelative);
    if (!selectedSequencePatternRelative.isEmpty()) {
        manifest.insert(QString::fromUtf8("selected_mask_sequence_pattern_project_relative"), selectedSequencePatternRelative);
    }
    manifest.insert(QString::fromUtf8("output_dir_project_relative"), _videoMamaRelativeRoot);
    manifest.insert(QString::fromUtf8("result_manifest_path_project_relative"), _videoMamaRelativeRoot + QString::fromUtf8("result_manifest.json"));
    manifest.insert(QString::fromUtf8("worker_result"), workerResult);
    manifest.insert(QString::fromUtf8("proof_nonzero_pixels"), QJsonObject{{QString::fromUtf8("video"), nonzeroPixels}, {QString::fromUtf8("video_unverified"), nonzeroUnverified}});
    manifest.insert(QString::fromUtf8("noncommercial"), true);
    manifest.insert(QString::fromUtf8("warning_text"), workerResult.value(QString::fromUtf8("warning_text")).toString(QString::fromUtf8("NON-COMMERCIAL: VideoMaMa is CC BY-NC 4.0 licensed.")));
    manifest.insert(QString::fromUtf8("successful_frame_count"), successfulFrames);

    // Build masks array
    {
        QDir outputDir(_videoMamaAbsoluteRoot);
        const QStringList maskFiles = outputDir.entryList(QStringList() << QString::fromUtf8("alpha_*.png") << QString::fromUtf8("mask_*.png"), QDir::Files, QDir::Name);
        QJsonArray masksArray;
        for (const QString& maskFile : maskFiles) {
            QString maskRelative;
            if (pathToProjectRelative(outputDir.filePath(maskFile), &maskRelative)) {
                QJsonObject entry;
                entry.insert(QString::fromUtf8("path"), maskRelative);
                const QString prefix = maskFile.startsWith(QString::fromUtf8("alpha_")) ? QString::fromUtf8("alpha_") : QString::fromUtf8("mask_");
                const QString frameStr = maskFile.mid(prefix.length(), maskFile.length() - prefix.length() - QString::fromUtf8(".png").length());
                bool okFrame = false;
                const int frameNum = frameStr.toInt(&okFrame);
                if (okFrame) {
                    entry.insert(QString::fromUtf8("frame"), frameNum);
                }
                masksArray.append(entry);
            }
        }
        manifest.insert(QString::fromUtf8("masks"), masksArray);
    }

    QFile out(QDir(_videoMamaAbsoluteRoot).filePath(QString::fromUtf8("result_manifest.json")));
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (message) { *message = tr("Could not write VideoMaMa result manifest: %1").arg(out.errorString()); }
        return false;
    }
    out.write(QJsonDocument(manifest).toJson(QJsonDocument::Indented));
    out.write("\n");

    const QString manifestRelative = _videoMamaRelativeRoot + QString::fromUtf8("result_manifest.json");
    addOrPromoteResultManifest(manifestRelative);
    if (selectedRelativeMask) { *selectedRelativeMask = selectedMaskRelative; }
    if (message) { *message = tr("VideoMaMa alpha masks generated: %1").arg(manifestRelative); }
    return true;
}

NATRON_NAMESPACE_EXIT
