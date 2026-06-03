#ifndef FLUXAILOG_H
#define FLUXAILOG_H

#include <QString>
#include <QJsonObject>

#include "Global/Macros.h"

NATRON_NAMESPACE_ENTER

class FluxAiLog
{
public:
    static QString writeEvent(const QString& projectPath,
                              const QString& level,
                              const QString& area,
                              const QString& step,
                              const QString& status,
                              const QString& message,
                              const QString& expected = QString(),
                              const QJsonObject& found = QJsonObject(),
                              const QString& nextAction = QString());

    static QString latestLogPath(const QString& projectPath);
    static QString sessionLogPath(const QString& projectPath);
    static QString sessionId();
};

NATRON_NAMESPACE_EXIT

#endif // FLUXAILOG_H
