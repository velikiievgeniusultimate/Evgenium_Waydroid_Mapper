#include "AppLog.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QtGlobal>
#include <cstdio>
#include <cstdlib>

namespace {
QMutex logMutex;
QString logPath;

QString stateDirectory()
{
    const QByteArray configured = qgetenv("XDG_STATE_HOME");
    if (!configured.isEmpty())
        return QString::fromLocal8Bit(configured)
            + QStringLiteral("/evgenium-waydroid-mapper");
    return QDir::homePath()
        + QStringLiteral("/.local/state/evgenium-waydroid-mapper");
}

void writeMessage(QtMsgType type, const QMessageLogContext &, const QString &message)
{
    const QByteArray terminalLine = message.toLocal8Bit();
    std::fprintf(stderr, "%s\n", terminalLine.constData());
    std::fflush(stderr);

    {
        const QMutexLocker locker(&logMutex);
        if (!logPath.isEmpty()) {
            QFile file(logPath);
            if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
                const QByteArray fileLine = QStringLiteral("[%1 pid=%2] %3\n")
                    .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs))
                    .arg(QCoreApplication::applicationPid())
                    .arg(message)
                    .toUtf8();
                file.write(fileLine);
            }
        }
    }

    if (type == QtFatalMsg)
        std::abort();
}
}

void AppLog::install()
{
    const QString directory = stateDirectory();
    QDir().mkpath(directory);
    logPath = directory + QStringLiteral("/runtime.log");

    const QFileInfo current(logPath);
    if (current.exists() && current.size() > 4 * 1024 * 1024) {
        const QString previous = logPath + QStringLiteral(".1");
        QFile::remove(previous);
        QFile::rename(logPath, previous);
    }

    qInstallMessageHandler(writeMessage);
}

QString AppLog::path()
{
    const QMutexLocker locker(&logMutex);
    return logPath;
}
