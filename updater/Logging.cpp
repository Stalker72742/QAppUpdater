#include "Logging.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QTextStream>
#include <QtGlobal>

#include <cstdio>

namespace {

QFile* g_logFile = nullptr;
QTextStream* g_logStream = nullptr;
QMutex g_logMutex;
bool g_mirrorToStderr = true;

char const* levelName(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:
        return "DEBUG";
    case QtInfoMsg:
        return "INFO ";
    case QtWarningMsg:
        return "WARN ";
    case QtCriticalMsg:
        return "CRIT ";
    case QtFatalMsg:
        return "FATAL";
    }
    return "?????";
}

void messageHandler(QtMsgType type, QMessageLogContext const& context, QString const& message)
{
    QMutexLocker const locker(&g_logMutex);

    QString const where = context.function ? QStringLiteral(" (%1)").arg(QString::fromUtf8(context.function))
                                             : QString();

    QString const line = QStringLiteral("%1 %2 %3%4")
                              .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz")),
                                   QString::fromLatin1(levelName(type)),
                                   message,
                                   where);

    if (g_mirrorToStderr)
        std::fprintf(stderr, "%s\n", qUtf8Printable(line));

    if (g_logStream) {
        (*g_logStream) << line << '\n';
        g_logStream->flush();
    }

    if (type == QtFatalMsg)
        std::abort();
}

} // namespace

namespace Logging {

void install(QString const& baseName)
{
    QDir const dir(QCoreApplication::applicationDirPath() + QStringLiteral("/logs"));
    if (!dir.exists())
        QDir().mkpath(dir.path());

    g_logFile = new QFile(dir.filePath(baseName + QStringLiteral(".log")));
    if (g_logFile->open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        g_logStream = new QTextStream(g_logFile);

    qInstallMessageHandler(messageHandler);

    qInfo().noquote() << "===" << QCoreApplication::applicationName() << "starting ==="
                      << QDateTime::currentDateTime().toString(Qt::ISODate);
}

void setMirrorToStderr(bool mirror)
{
    QMutexLocker const locker(&g_logMutex);
    g_mirrorToStderr = mirror;
}

} // namespace Logging
