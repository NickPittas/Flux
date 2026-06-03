#include "Gui/FluxAiWorkerController.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryFile>
#include <QTextStream>
#include <QTimer>

NATRON_NAMESPACE_ENTER

FluxAiWorkerController::FluxAiWorkerController(QObject* parent)
    : QObject(parent)
    , _process(new QProcess(this))
    , _jobFilePath()
    , _stdoutBuffer()
    , _running(false)
    , _cancelRequested(false)
{
    QObject::connect(_process, &QProcess::readyReadStandardOutput, this, &FluxAiWorkerController::onReadyReadStandardOutput);
    QObject::connect(_process, &QProcess::readyReadStandardError, this, &FluxAiWorkerController::onReadyReadStandardError);
    QObject::connect(_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, &FluxAiWorkerController::onFinished);
    QObject::connect(_process, &QProcess::errorOccurred, this, &FluxAiWorkerController::onErrorOccurred);
}

FluxAiWorkerController::~FluxAiWorkerController()
{
    if (_process->state() != QProcess::NotRunning) {
        _process->kill();
        _process->waitForFinished(1000);
    }
    cleanupJobFile();
}

bool FluxAiWorkerController::isRunning() const { return _running; }

QString FluxAiWorkerController::repoRoot() const
{
    const QString rel = QString::fromUtf8("tools/ai/flux_ai_worker.py");
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

void FluxAiWorkerController::startNoopJob(const QString& task,
                                           const QString& modelId,
                                           const QString& absoluteOutputRoot,
                                           const QString& projectRelativeOutputRoot,
                                           const QString& runId,
                                           const QJsonObject& prompt)
{
    if (_running) {
        Q_EMIT progressText(tr("AI worker is already running."));
        return;
    }

    if (!QDir().mkpath(absoluteOutputRoot)) {
        Q_EMIT finished(false, tr("Could not create AI output directory: %1").arg(absoluteOutputRoot));
        return;
    }

    cleanupJobFile();
    _stdoutBuffer.clear();
    QTemporaryFile jobFile(QDir::tempPath() + QString::fromUtf8("/flux-ai-job-XXXXXX.json"));
    jobFile.setAutoRemove(false);
    if (!jobFile.open()) {
        Q_EMIT finished(false, tr("Could not create temporary AI job file."));
        return;
    }

    QJsonObject job;
    job.insert(QString::fromUtf8("task"), task);
    job.insert(QString::fromUtf8("model_id"), modelId);
    job.insert(QString::fromUtf8("mode"), QString::fromUtf8("noop_cuda_probe"));
    job.insert(QString::fromUtf8("run_id"), runId);
    job.insert(QString::fromUtf8("output_dir"), absoluteOutputRoot);
    job.insert(QString::fromUtf8("output_dir_project_relative"), projectRelativeOutputRoot);
    job.insert(QString::fromUtf8("output_path_policy"), QString::fromUtf8("project_relative_fluxgenerated_ai"));
    job.insert(QString::fromUtf8("result_manifest_path"), QDir(absoluteOutputRoot).filePath(QString::fromUtf8("result_manifest.json")));
    job.insert(QString::fromUtf8("result_manifest_path_project_relative"), projectRelativeOutputRoot + QString::fromUtf8("result_manifest.json"));
    job.insert(QString::fromUtf8("prompt"), prompt.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(prompt));
    jobFile.write(QJsonDocument(job).toJson(QJsonDocument::Compact));
    jobFile.write("\n");
    _jobFilePath = jobFile.fileName();
    jobFile.close();

    const QString workerPath = QDir(repoRoot()).filePath(QString::fromUtf8("tools/ai/flux_ai_worker.py"));
    QStringList args;
    args << workerPath << QString::fromUtf8("--job") << _jobFilePath;
    _cancelRequested = false;
    setRunning(true);
    Q_EMIT progressText(tr("Starting AI worker: python3 %1").arg(args.join(QLatin1Char(' '))));
    _process->start(QString::fromUtf8("python3"), args);
    if (!_process->waitForStarted(3000)) {
        setRunning(false);
        cleanupJobFile();
        _stdoutBuffer.clear();
        Q_EMIT finished(false, tr("AI worker failed to start."));
    }
}

void FluxAiWorkerController::cancel()
{
    if (_process->state() == QProcess::NotRunning) {
        return;
    }
    _cancelRequested = true;
    Q_EMIT progressText(tr("Cancel requested."));
    _process->terminate();
    QTimer::singleShot(2000, this, [this]() {
        if (_process->state() != QProcess::NotRunning) {
            Q_EMIT progressText(tr("AI worker did not exit promptly; killing."));
            _process->kill();
        }
    });
}

void FluxAiWorkerController::onReadyReadStandardOutput()
{
    _stdoutBuffer.append(QString::fromUtf8(_process->readAllStandardOutput()));

    int newlineIndex = _stdoutBuffer.indexOf(QLatin1Char('\n'));
    while (newlineIndex >= 0) {
        const QString line = _stdoutBuffer.left(newlineIndex).trimmed();
        _stdoutBuffer.remove(0, newlineIndex + 1);
        if (!line.isEmpty()) {
            Q_EMIT progressText(line);
            if (line.contains(QString::fromUtf8("\"event\": \"started\"")) || line.contains(QString::fromUtf8("\"event\":\"started\""))) {
                Q_EMIT started();
            }
        }
        newlineIndex = _stdoutBuffer.indexOf(QLatin1Char('\n'));
    }
}

void FluxAiWorkerController::onReadyReadStandardError()
{
    const QString text = QString::fromUtf8(_process->readAllStandardError()).trimmed();
    if (!text.isEmpty()) {
        Q_EMIT progressText(tr("stderr: %1").arg(text));
    }
}

void FluxAiWorkerController::onFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    const bool ok = (exitStatus == QProcess::NormalExit && exitCode == 0 && !_cancelRequested);
    setRunning(false);
    cleanupJobFile();
    _stdoutBuffer.clear();
    Q_EMIT finished(ok, ok ? tr("AI worker finished successfully.") : tr("AI worker failed or was canceled."));
}

void FluxAiWorkerController::onErrorOccurred(QProcess::ProcessError error)
{
    Q_EMIT progressText(tr("AI worker process error: %1").arg(static_cast<int>(error)));
}

void FluxAiWorkerController::setRunning(bool running)
{
    if (_running == running) {
        return;
    }
    _running = running;
    Q_EMIT runningChanged(_running);
}

void FluxAiWorkerController::cleanupJobFile()
{
    if (!_jobFilePath.isEmpty()) {
        QFile::remove(_jobFilePath);
        _jobFilePath.clear();
    }
}

NATRON_NAMESPACE_EXIT
