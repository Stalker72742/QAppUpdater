#pragma once

#include <QString>

// Installs a Qt message handler so every qDebug/qInfo/qWarning/qCritical/qFatal call (including Qt's own
// warnings) ends up in one timestamped log file: the updater's GUI mode has no console.
namespace Logging {

// Call once, right after the QApplication is constructed and before
// anything else logs. Truncates <exe_dir>/logs/<baseName>.log fresh for
// this run and mirrors every message to stderr too (harmless if nothing is
// attached to read it).
void install(QString const& baseName);

// Stops mirroring to stderr (the log file keeps everything). For the
// updater's CLI mode, whose stdout/stderr are for the user.
void setMirrorToStderr(bool mirror);

} // namespace Logging
