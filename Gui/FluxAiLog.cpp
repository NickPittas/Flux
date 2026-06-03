#include "Gui/FluxAiLog.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

#include <cstdio>

NATRON_NAMESPACE_ENTER

namespace {

QString
fallbackLogDir()
{
    return QDir::home().filePath(QString::fromUtf8(".local/state/Flux/ai-logs"));
}

QString
baseLogDir(const QString& projectPath)
{
    if (!projectPath.trimmed().isEmpty()) {
        return QDir(projectPath).filePath(QString::fromUtf8("FluxGenerated/AI/logs"));
    }
    return fallbackLogDir();
}

QString
sanitized(const QString& value)
{
    QString out = value;
    out.replace(QLatin1Char('\n'), QLatin1Char(' '));
    out.replace(QLatin1Char('\r'), QLatin1Char(' '));
    return out.trimmed();
}

bool
appendJsonLine(const QString& path, const QByteArray& line)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        std::fprintf(stderr, "[FLUX-AI] log_write_failed path='%s' error='%s'\n",
                     path.toUtf8().constData(), file.errorString().toUtf8().constData());
        return false;
    }
    file.write(line);
    file.write("\n");
    file.flush();
    return true;
}

} // namespace

QString
FluxAiLog::sessionId()
{
    static const QString id = QDateTime::currentDateTimeUtc().toString(QString::fromUtf8("yyyyMMdd-HHmmss-zzz"));
    return id;
}

QString
FluxAiLog::latestLogPath(const QString& projectPath)
{
    return QDir(baseLogDir(projectPath)).filePath(QString::fromUtf8("flux-ai-latest.jsonl"));
}

QString
FluxAiLog::sessionLogPath(const QString& projectPath)
{
    return QDir(baseLogDir(projectPath)).filePath(QString::fromUtf8("flux-ai-%1.jsonl").arg(sessionId()));
}

QString
FluxAiLog::writeEvent(const QString& projectPath,
                      const QString& level,
                      const QString& area,
                      const QString& step,
                      const QString& status,
                      const QString& message,
                      const QString& expected,
                      const QJsonObject& found,
                      const QString& nextAction)
{
    const QString effectiveProjectPath = projectPath.trimmed();
    QJsonObject event;
    event.insert(QString::fromUtf8("time_utc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    event.insert(QString::fromUtf8("session_id"), sessionId());
    event.insert(QString::fromUtf8("level"), sanitized(level));
    event.insert(QString::fromUtf8("area"), sanitized(area));
    event.insert(QString::fromUtf8("step"), sanitized(step));
    event.insert(QString::fromUtf8("status"), sanitized(status));
    event.insert(QString::fromUtf8("message"), sanitized(message));
    event.insert(QString::fromUtf8("project_path"), effectiveProjectPath);
    event.insert(QString::fromUtf8("log_path_latest"), latestLogPath(effectiveProjectPath));
    event.insert(QString::fromUtf8("log_path_session"), sessionLogPath(effectiveProjectPath));
    if (!expected.trimmed().isEmpty()) {
        event.insert(QString::fromUtf8("expected"), sanitized(expected));
    }
    if (!found.isEmpty()) {
        event.insert(QString::fromUtf8("found"), found);
    }
    if (!nextAction.trimmed().isEmpty()) {
        event.insert(QString::fromUtf8("next_action"), sanitized(nextAction));
    }

    const QByteArray json = QJsonDocument(event).toJson(QJsonDocument::Compact);
    const QString latest = latestLogPath(effectiveProjectPath);
    const QString session = sessionLogPath(effectiveProjectPath);
    appendJsonLine(latest, json);
    if (session != latest) {
        appendJsonLine(session, json);
    }

    std::fprintf(stderr, "[FLUX-AI] %s area=%s step=%s status=%s message=%s log=%s\n",
                 sanitized(level).toUtf8().constData(),
                 sanitized(area).toUtf8().constData(),
                 sanitized(step).toUtf8().constData(),
                 sanitized(status).toUtf8().constData(),
                 sanitized(message).toUtf8().constData(),
                 latest.toUtf8().constData());
    return latest;
}

NATRON_NAMESPACE_EXIT
