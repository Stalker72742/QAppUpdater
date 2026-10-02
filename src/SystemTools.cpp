#include "SystemTools.h"

#include <QDir>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace QAppUpdater::SystemTools {

QString tarExe()
{
    QString const windows = qEnvironmentVariable("SystemRoot", QStringLiteral("C:\\Windows"));
    return QDir(windows).filePath(QStringLiteral("System32/tar.exe"));
}

bool isProcessRunning(qint64 pid)
{
#ifdef Q_OS_WIN
    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
    if (!process)
        return false; // gone (or never existed)
    bool const running = WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
    CloseHandle(process);
    return running;
#else
    Q_UNUSED(pid);
    return false;
#endif
}

} // namespace QAppUpdater::SystemTools
