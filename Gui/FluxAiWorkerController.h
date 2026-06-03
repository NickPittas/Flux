#ifndef FLUXAIWORKERCONTROLLER_H
#define FLUXAIWORKERCONTROLLER_H

#include <Python.h>

#include "Global/Macros.h"

CLANG_DIAG_OFF(deprecated)
#include <QObject>
#include <QProcess>
#include <QString>
#include <QJsonObject>
CLANG_DIAG_ON(deprecated)

NATRON_NAMESPACE_ENTER

class FluxAiWorkerController : public QObject
{
    Q_OBJECT
public:
    explicit FluxAiWorkerController(QObject* parent = nullptr);
    ~FluxAiWorkerController() override;

    bool isRunning() const;
    void startNoopJob(const QString& task,
                      const QString& modelId,
                      const QString& absoluteOutputRoot,
                      const QString& projectRelativeOutputRoot,
                      const QString& runId,
                      const QJsonObject& prompt);

public Q_SLOTS:
    void cancel();

Q_SIGNALS:
    void started();
    void progressText(const QString& text);
    void finished(bool success, const QString& message);
    void runningChanged(bool running);

private Q_SLOTS:
    void onReadyReadStandardOutput();
    void onReadyReadStandardError();
    void onFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onErrorOccurred(QProcess::ProcessError error);

private:
    QString repoRoot() const;
    void setRunning(bool running);
    void cleanupJobFile();

    QProcess* _process;
    QString _jobFilePath;
    QString _stdoutBuffer;
    bool _running;
    bool _cancelRequested;
};

NATRON_NAMESPACE_EXIT

#endif // FLUXAIWORKERCONTROLLER_H
