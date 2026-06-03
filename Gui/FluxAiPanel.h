#ifndef FLUXAIPANEL_H
#define FLUXAIPANEL_H

#include "Global/Macros.h"

CLANG_DIAG_OFF(deprecated)
#include <QWidget>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QListWidget>
#include <QProgressBar>
#include <QSpinBox>
#include <QJsonObject>
#include <QJsonArray>
#include <QProcess>
#include <QTimer>
CLANG_DIAG_ON(deprecated)

#include <QMap>
#include <functional>
#include <vector>

#include "Gui/PanelWidget.h"
#include "Gui/FluxAiWorkerController.h"
#include "Gui/ProjectGuiSerialization.h"
#include "Engine/AIPaintContext.h"
#include "Engine/EngineFwd.h"
#include "Engine/RectD.h"

NATRON_NAMESPACE_ENTER

class AIPaint;
struct FluxLayer;
class ViewerGL;

class FluxAiPanel : public QWidget, public PanelWidget
{
    Q_OBJECT
public:
    explicit FluxAiPanel(Gui* gui, QWidget* parent = nullptr);
    ~FluxAiPanel() override;
    void setViewerForCapture(ViewerGL* viewer);
    void setSourceCaptureContext(ViewerGL* viewer, const NodePtr& viewerNode, int layerIndex, const QString& layerName, const QString& filePath, const NodePtr& readerNode, const QString& readerLabel, const NodePtr& aiPaintNode, int timelineFrame, int sourceFrame, int rangeFirstFrame, int rangeLastFrame);
    Q_INVOKABLE void refreshAIPaintPromptState();
    Q_INVOKABLE void requestAIPaintSam3Load();
    Q_INVOKABLE void requestAIPaintSam3Unload();
    Q_INVOKABLE void setAIPaintLivePreviewEnabled(bool enabled);
    void setSelectedSource(const QString& layerName, const QString& filePath, int timelineFrame, int sourceFrame);
    FluxAiPanelSerialization serializeForProject() const;
    void restoreFromProjectSerialization(const FluxAiPanelSerialization& serialization);

private Q_SLOTS:
    void onAddMaskClicked();
    void onReplaceMaskClicked();
    void onModelChanged();
    void onRunClicked();
    void onRunningChanged(bool running);
    void onWorkerProgress(const QString& text);
    void onWorkerFinished(bool success, const QString& message);
    void onResultHistorySelectionChanged();
    void onPreviewAgainClicked();
    void onRemoveHistoryEntryClicked();
    void onFrameRangeEdited();
    void onSam3ReadyReadStandardOutput();
    void onSam3ReadyReadStandardError();
    void onSam3Finished(int exitCode, QProcess::ExitStatus exitStatus);
    void onSam3ErrorOccurred(QProcess::ProcessError error);
    void onMatAnyone2ReadyReadStandardOutput();
    void onMatAnyone2Finished(int exitCode, QProcess::ExitStatus exitStatus);
    void onMatAnyone2ErrorOccurred(QProcess::ProcessError error);
    void onVideoMamaReadyReadStandardOutput();
    void onVideoMamaFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onVideoMamaErrorOccurred(QProcess::ProcessError error);
    void cancelSam3();
    void appendLog(const QString& text);
    void toggleLog();

private:
    void setupUi();
    void loadModelManifest();
    QString manifestPath() const;
    static QString warningSuffix(const QString& policy);
    static QString taskSlug(const QString& task);
    QString repoRoot() const;
    bool startSam3StillMask(const QString& modelId, const QString& runId, const QString& absoluteRoot, const QString& relativeRoot, const QString& sourcePng, const QJsonObject& sourceMetadata);
    bool ensureSam3WorkerStarted(QString* message);
    QString sendSam3WorkerRequest(const QJsonObject& request);
    void processSam3WorkerLine(const QByteArray& line);
    bool ensureMatAnyone2WorkerStarted(QString* message);
    QString sendMatAnyone2WorkerRequest(const QJsonObject& request);
    void processMatAnyone2WorkerLine(const QByteArray& line);
    bool ensureVideoMamaWorkerStarted(QString* message);
    QString sendVideoMamaWorkerRequest(const QJsonObject& request);
    void processVideoMamaWorkerLine(const QByteArray& line);
    void stopVideoMamaWorker();
    void runVideoMama();
    bool verifyVideoMamaResultAndWriteManifest(const QJsonObject& workerResult, QString* message, QString* selectedRelativeMask = nullptr);
    bool verifyMatAnyone2ResultAndWriteManifest(const QJsonObject& workerResult, QString* message, QString* selectedRelativeMask = nullptr);
    bool verifySam3ResultAndWriteManifest(const QJsonObject& probeResult, QString* message, QString* selectedRelativeMask = nullptr);
    void previewSam3RunResult(const QString& relativeMask);
    void addOrPromoteResultManifest(const QString& projectRelativeManifest);
    void refreshResultHistoryList(const QString& selectedManifest = QString());
    QString resultHistoryDisplayLabel(const QString& projectRelativeManifest) const;
    QString selectedResultManifestProjectRelative() const;
    bool selectedResultMaskProjectRelative(QString* relativeMask, QString* message, QString* relativeSequencePattern = nullptr) const;
    void updateUiState();
    void stopSam3PersistentWorker();
    void stopMatAnyone2Worker();
    void runMatAnyone2();
    void resetFrameRangeControls();
    void setFrameRangeControls(int firstFrame, int lastFrame, bool preserveSelection);
    bool selectedFrameRange(int* firstFrame, int* lastFrame, QString* message) const;
    QJsonObject selectedFrameRangeMetadata() const;
    void resetProgressBar();
    void setProgressBarBusy(const QString& message);
    void setProgressBarValue(int percent, const QString& message);
    void updateProgressBarFromPayload(const QJsonObject& payload, const QString& fallbackMessage);
    QString aiLogProjectPath() const;
    void writeAiLog(const QString& level, const QString& area, const QString& step,
                    const QString& status, const QString& message,
                    const QString& expected = QString(), const QJsonObject& found = QJsonObject(),
                    const QString& nextAction = QString()) const;
    void updateAiLogPathLabel() const;
    QString runReadinessSignature(const QString& modelId, bool running, bool sam3Running, bool matAnyone2Running, bool videoMamaRunning, bool projectSaved, bool canRun) const;
    QString workflowGuideText(const QString& modelId) const;
    QString runUnavailableReason(const QString& modelId, bool running, bool sam3Running, bool matAnyone2Running, bool videoMamaRunning, bool projectSaved) const;
    void updateWorkflowGuide(const QString& modelId, const QString& runReason) const;
    void updatePromptSummary();
    bool hasSelectedSource() const;
    bool refreshSourceFrameMetadata(QString* message);
    bool refreshLivePreviewFrameFromViewer(QString* message);
    void recomputeSourceFrameFromTimeline(const FluxLayer& layer);
    bool validateSourceCaptureContext(QString* message) const;
    bool exportedSourceMetadataMatchesSelection(const QJsonObject& metadata, QString* message) const;
    bool renderProcessedAIPaintFrame(int timelineFrame, int sourceFrame, const QString& outputPath, QJsonObject* metadata, QString* message) const;
    QString exportProcessedAIPaintSequence(int sourceRangeStart, int sourceRangeEnd, QJsonObject* sequenceMetadata, QString* diagnostics, std::function<bool(int, int)> progressCallback);
    bool readAIPaintPrompts(std::vector<AIPaintPrompt>* prompts, QString* message) const;
    bool readAIPaintPromptsInRange(int timelineStart, int timelineEnd, std::vector<AIPaintPrompt>* prompts, QString* message) const;
    bool promptIsInTimelineRange(const AIPaintPrompt& prompt, int timelineStart, int timelineEnd) const;
    int currentAIPaintPromptFrame() const;
    bool promptMatchesCurrentTimelineFrame(const AIPaintPrompt& prompt) const;
    bool livePreviewMaskMatchesCurrentPromptFrame(const AIPaint* aiPaint) const;
    bool chooseAIPaintPrompt(const std::vector<AIPaintPrompt>& prompts, AIPaintPrompt* prompt, QString* message) const;
    QJsonObject buildAIPaintPromptForSam(const AIPaintPrompt& prompt, QString* message) const;
    QJsonArray buildAIPaintPromptsForSam(const std::vector<AIPaintPrompt>& prompts, QString* message) const;
    bool hasRunnableAIPaintPrompt() const;
    void scheduleAIPaintLivePreview();
    void runAIPaintLivePreview();
    bool canonicalPointWithinSourceBounds(const QPointF& canonical, const RectD& format, const RectD& rod, QString* message) const;
    bool viewerCanonicalToSourcePixel(const QPointF& canonical, QPointF* sourcePixel, QString* message) const;
    QJsonObject buildSourcePointPrompt(const QJsonObject& viewerPrompt, QString* message) const;
    QJsonObject buildSourceBoxPrompt(const QJsonObject& viewerPrompt, QString* message) const;

    Gui* _gui;
    ViewerGL* _viewer;
    ViewerGL* _sourceViewer;
    NodePtr _sourceViewerNode;
    NodePtr _sourceReaderNode;
    NodePtr _sourceAIPaintNode;
    int _sourceLayerIndex;
    QString _sourceLayerName;
    QString _sourceFilePath;
    QString _sourceReaderLabel;
    int _sourceTimelineFrame;
    int _sourceSourceFrame;
    int _sourceRangeFirstFrame;
    int _sourceRangeLastFrame;
    bool _updatingFrameRangeControls;
    QComboBox* _taskCombo;
    QComboBox* _modelCombo;
    QLabel* _sourceLabel;
    QLabel* _frameRangeLabel;
    QSpinBox* _frameRangeStartSpin;
    QSpinBox* _frameRangeEndSpin;
    QLabel* _videoMamaBatchLabel;
    QComboBox* _videoMamaBatchCombo;
    QLabel* _videoMamaOverlapLabel;
    QComboBox* _videoMamaOverlapCombo;
    QLabel* _outputLabel;
    QLabel* _statusLabel;
    QProgressBar* _progressBar;
    QLabel* _aiLogPathLabel;
    QLabel* _workflowGuideLabel;
    QLabel* _runReasonLabel;
    QLabel* _promptLabel;
    QPushButton* _runButton;
    QPushButton* _addMaskButton;
    QPushButton* _replaceMaskButton;
    QPushButton* _cancelButton;
    QListWidget* _resultHistoryList;
    QPushButton* _previewAgainButton;
    QPushButton* _removeHistoryEntryButton;
    QPushButton* _logToggleButton;
    QPlainTextEdit* _log;
    FluxAiWorkerController* _worker;
    QProcess* _sam3Process;
    QProcess* _sam3WorkerProcess;
    QString _sam3StdoutBuffer;
    QByteArray _sam3WorkerStdoutBuffer;
    int _sam3WorkerNextRequestId;
    QMap<QString, QString> _sam3WorkerPendingCommands;
    bool _sam3WorkerLoaded;
    bool _sam3WorkerLoading;
    bool _sam3WorkerUnloading;
    QString _sam3LastError;
    QString _sam3AbsoluteRoot;
    QString _sam3RelativeRoot;
    QString _sam3RunId;
    QString _sam3Task;
    QString _sam3SourcePng;
    QJsonObject _sam3SourceMetadata;
    QJsonObject _sam3Prompt;
    QJsonArray _sam3Prompts;
    QString _sam3RunPendingRequestId;
    bool _sam3CancelRequested;
    bool _sam3Exporting;
    bool _hasSelectedSource;
    QStringList _resultManifestHistoryProjectRelative;
    QString _lastResultManifestProjectRelative;
    int _sourceFrameWidth;
    int _sourceFrameHeight;
    QString _sourceFramePng;
    QJsonObject _sourceFrameMetadata;
    bool _sourceFrameMetadataFresh;
    QTimer* _livePreviewDebounceTimer;
    int _livePreviewGeneration;
    QString _livePreviewPendingRequestId;
    int _livePreviewPendingGeneration;
    QProcess* _matAnyone2WorkerProcess;
    QByteArray _matAnyone2WorkerStdoutBuffer;
    int _matAnyone2WorkerNextRequestId;
    QMap<QString, QString> _matAnyone2WorkerPendingCommands;
    QString _matAnyone2RunPendingRequestId;
    bool _matAnyone2CancelRequested;
    bool _matAnyone2Running;
    bool _matAnyone2Exporting;
    QString _matAnyone2AbsoluteRoot;
    QString _matAnyone2RelativeRoot;
    QString _matAnyone2RunId;
    QString _matAnyone2Task;
    QJsonObject _matAnyone2SourceMetadata;
    QString _cachedSourceSequenceDir;
    int _cachedSourceSequenceRangeStart;
    int _cachedSourceSequenceRangeEnd;
    QJsonObject _cachedSourceSequenceMetadata;
    QJsonObject _matAnyone2PendingInferRequest;
    QProcess* _videoMamaWorkerProcess;
    QByteArray _videoMamaWorkerStdoutBuffer;
    int _videoMamaWorkerNextRequestId;
    QMap<QString, QString> _videoMamaWorkerPendingCommands;
    QString _videoMamaRunPendingRequestId;
    bool _videoMamaCancelRequested;
    bool _videoMamaRunning;
    bool _videoMamaExporting;
    QString _videoMamaAbsoluteRoot;
    QString _videoMamaRelativeRoot;
    QString _videoMamaRunId;
    QString _videoMamaTask;
    QJsonObject _videoMamaSourceMetadata;
    QJsonObject _videoMamaPendingInferRequest;
    mutable QString _lastRunReadinessSignature;
    mutable QString _lastVideoMamaUiSignature;
    mutable QString _lastPromptSummarySignature;
};

NATRON_NAMESPACE_EXIT

#endif // FLUXAIPANEL_H
