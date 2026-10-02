#pragma once

#include <QString>
#include <QStringList>
#include <QVersionNumber>

#ifndef QAU_UPDATER_PROTOCOL
#define QAU_UPDATER_PROTOCOL 1
#endif

namespace QAppUpdater {

// Version of the package format and install procedure this updater implements. A package whose
// minUpdaterProtocol is higher can't be installed by it: the user has to download that version by hand.
// Bumped only for changes an older updater would get wrong.
inline constexpr int kUpdaterProtocol = QAU_UPDATER_PROTOCOL;

// The application this updater serves. Generated per app by qau_setup() in CMake.
struct AppInfo {
    QString name;               // display name and package prefix: <name>-<version>-win64.zip
    QString appExe;             // e.g. "SoundLink.exe"
    QString updaterExe;         // e.g. "Updater.exe"
    QString version;            // the app version the updater was built with
    QString defaultRepo;        // GitHub "owner/name"; updater.json can override it
    QString runtimeDir;         // folder next to the exes with every DLL and Qt plugin
    QStringList protectedPaths; // top-level user data names a package may never write (case-insensitive)
    QString accentColor;        // updater window accent, "#RRGGBB"
};

AppInfo const& appInfo();

QString packageFileName(QVersionNumber const& version);
bool isPackageFileName(QString const& fileName);

} // namespace QAppUpdater
