#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVersionNumber>

#include <optional>

// What an installed app folder looks like, shared by the app and the updater:
//
//   <App>.exe, Updater.exe, bin/...   program files; the exes carry the version (VERSIONINFO)
//   package.json                      manifest of the package that was installed (PackageManifest)
//   updater.json                      updater settings (user data)
//   update/                           updater scratch: downloads, staging, leftovers.json, result.json
//   logs/ + AppInfo::protectedPaths   user data: never in a package, never touched by updates
namespace QAppUpdater::InstallLayout {

inline QString const kManifestFile = QStringLiteral("package.json");
inline QString const kUpdaterConfigFile = QStringLiteral("updater.json");
inline QString const kWorkDir = QStringLiteral("update");
inline QString const kLogsDir = QStringLiteral("logs");

// package.json: written into every deployed folder and package zip, copied into the install folder on update.
// Describes the package; the installed version itself is read from the app exe.
struct PackageManifest {
    QString name;
    QVersionNumber version;
    int minUpdaterProtocol{1};
    QStringList files; // every program file, relative, '/'-separated (not package.json itself)
    QString built;     // ISO date-time of the deploy
    QString notes;     // optional Markdown, for local builds without release notes

    // False (with `error`) if the JSON is invalid or has no version.
    static bool parse(QByteArray const& json, PackageManifest& manifest, QString* error = nullptr);
    static bool read(QString const& path, PackageManifest& manifest, QString* error = nullptr);
};

// The folder both exes live in.
QString rootDir();

// The FILEVERSION of an exe's VERSIONINFO resource; null if it has none.
QVersionNumber exeVersion(QString const& exePath);
// What's installed in `root`: the app exe's version.
QVersionNumber installedVersion(QString const& root);
// `available` > `installed`, ignoring trailing zeros ("0.2" == "0.2.0"); a null installed version is always older.
bool isNewer(QVersionNumber const& available, QVersionNumber const& installed);
// Program files the installed package listed; empty if unknown.
QStringList installedFiles(QString const& root);

// User data and updater files a package may never write; `relative` is '/'-separated.
bool isProtectedPath(QString const& relative);

// Files an update renamed aside because they were in use (the running updater's own exe and DLLs).
// Deleted by whichever exe starts next; still-locked ones stay listed.
void addLeftovers(QString const& root, QStringList const& absolutePaths);
void removeLeftovers(QString const& root);

// QLocalServer name the running app listens on, per install folder. Protocol: the server writes "pid <n>\n"
// on connect; the client may send "quit\n", which makes the app save everything and exit, and (updater --report)
// "status {\"stage\": ..., \"done\": n, \"total\": n}\n" and "result {\"ok\": ..., \"message\": ...}\n" lines
// about an install in progress.

// How the last install that closed the app ended, for the app to show when it starts again
// (it was closed meanwhile, so nobody saw the updater's own message).
struct UpdateResult {
    bool ok{false};
    QString message;
    QVersionNumber version;
    QDateTime finished;
};
void writeUpdateResult(QString const& root, UpdateResult const& result);
// Reads and deletes it, so it's shown once.
std::optional<UpdateResult> takeUpdateResult(QString const& root);
QString controlServerName(QString const& root);

// Starts the updater from `root` detached and without a console window.
bool startUpdater(QString const& root, QStringList const& arguments = {}, QString* error = nullptr);

} // namespace QAppUpdater::InstallLayout
