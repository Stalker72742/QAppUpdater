#pragma once

#include <QString>

namespace QAppUpdater::SystemTools {

// Windows' built-in bsdtar (Windows 10 1803+), which also reads zip files.
QString tarExe();

bool isProcessRunning(qint64 pid);

} // namespace QAppUpdater::SystemTools
