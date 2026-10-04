#include "QAppUpdater/InstallLayout.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include "QAppUpdater/AppInfo.h"

namespace {

QStringList readStringList(QString const& path, QString const& key)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QStringList list;
    for (QJsonValue const& value : QJsonDocument::fromJson(file.readAll()).object().value(key).toArray())
        list.append(value.toString());
    return list;
}

QString resultPath(QString const& root)
{
    return QDir(root).filePath(QAppUpdater::InstallLayout::kWorkDir + QStringLiteral("/result.json"));
}

QString leftoversPath(QString const& root)
{
    return QDir(root).filePath(QAppUpdater::InstallLayout::kWorkDir + QStringLiteral("/leftovers.json"));
}

void writeLeftovers(QString const& root, QStringList const& paths)
{
    QString const path = leftoversPath(root);
    if (paths.isEmpty()) {
        QFile::remove(path);
        return;
    }
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(QJsonObject{{QStringLiteral("files"), QJsonArray::fromStringList(paths)}}).toJson());
        file.commit();
    }
}

} // namespace

namespace QAppUpdater {

QString packageFileName(QVersionNumber const& version)
{
    return QStringLiteral("%1-%2-win64.zip").arg(appInfo().name, version.toString());
}

bool isPackageFileName(QString const& fileName)
{
    QRegularExpression const pattern(QStringLiteral("^%1-.+-win64\\.zip$").arg(QRegularExpression::escape(appInfo().name)),
                                     QRegularExpression::CaseInsensitiveOption);
    return pattern.match(fileName).hasMatch();
}

} // namespace QAppUpdater

namespace QAppUpdater::InstallLayout {

bool PackageManifest::parse(QByteArray const& json, PackageManifest& manifest, QString* error)
{
    QJsonParseError parseError;
    QJsonObject const root = QJsonDocument::fromJson(json, &parseError).object();
    manifest = {};
    manifest.version = QVersionNumber::fromString(root.value(QStringLiteral("version")).toString());
    if (manifest.version.isNull()) {
        if (error)
            *error = parseError.error != QJsonParseError::NoError ? parseError.errorString()
                                                                  : QStringLiteral("no \"version\"");
        return false;
    }
    manifest.name = root.value(QStringLiteral("name")).toString();
    manifest.minUpdaterProtocol = root.value(QStringLiteral("minUpdaterProtocol")).toInt(1);
    for (QJsonValue const& value : root.value(QStringLiteral("files")).toArray())
        manifest.files.append(value.toString());
    manifest.built = root.value(QStringLiteral("built")).toString();
    manifest.notes = root.value(QStringLiteral("notes")).toString();
    return true;
}

bool PackageManifest::read(QString const& path, PackageManifest& manifest, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        manifest = {};
        if (error)
            *error = file.errorString();
        return false;
    }
    return parse(file.readAll(), manifest, error);
}

QString rootDir()
{
    return QCoreApplication::applicationDirPath();
}

QVersionNumber exeVersion(QString const& exePath)
{
#ifdef Q_OS_WIN
    std::wstring const path = QDir::toNativeSeparators(exePath).toStdWString();
    DWORD const size = GetFileVersionInfoSizeW(path.c_str(), nullptr);
    if (size == 0)
        return {};
    QByteArray data(static_cast<qsizetype>(size), Qt::Uninitialized);
    if (!GetFileVersionInfoW(path.c_str(), 0, size, data.data()))
        return {};
    VS_FIXEDFILEINFO* info = nullptr;
    UINT length = 0;
    if (!VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&info), &length) || !info)
        return {};
    QList<int> segments{HIWORD(info->dwFileVersionMS), LOWORD(info->dwFileVersionMS),
                        HIWORD(info->dwFileVersionLS), LOWORD(info->dwFileVersionLS)};
    if (segments.last() == 0)
        segments.removeLast(); // "0.2.0.0" -> "0.2.0", as the project version is written
    return QVersionNumber(segments);
#else
    Q_UNUSED(exePath);
    return {};
#endif
}

QVersionNumber installedVersion(QString const& root)
{
    return exeVersion(QDir(root).filePath(appInfo().appExe));
}

bool isNewer(QVersionNumber const& available, QVersionNumber const& installed)
{
    return installed.isNull() || available.normalized() > installed.normalized();
}

QStringList installedFiles(QString const& root)
{
    return readStringList(QDir(root).filePath(kManifestFile), QStringLiteral("files"));
}

bool isProtectedPath(QString const& relative)
{
    QString const top = relative.section(QLatin1Char('/'), 0, 0);
    QStringList names{kWorkDir, kUpdaterConfigFile, kManifestFile, kLogsDir};
    names += appInfo().protectedPaths;
    for (QString const& name : std::as_const(names))
        if (top.compare(name, Qt::CaseInsensitive) == 0)
            return true;
    return false;
}

void addLeftovers(QString const& root, QStringList const& absolutePaths)
{
    QStringList paths = readStringList(leftoversPath(root), QStringLiteral("files"));
    for (QString const& path : absolutePaths)
        if (!paths.contains(path))
            paths.append(path);
    writeLeftovers(root, paths);
}

void removeLeftovers(QString const& root)
{
    QStringList stillLocked;
    for (QString const& path : readStringList(leftoversPath(root), QStringLiteral("files"))) {
        if (!QFile::exists(path))
            continue;
        if (QFile::remove(path))
            qDebug().noquote() << "QAppUpdater: removed update leftover" << path;
        else
            stillLocked.append(path);
    }
    writeLeftovers(root, stillLocked);
}

QString controlServerName(QString const& root)
{
    QByteArray const key = QDir::cleanPath(root).toLower().toUtf8();
    return appInfo().name + QLatin1Char('-')
           + QString::fromLatin1(QCryptographicHash::hash(key, QCryptographicHash::Sha1).toHex().left(16));
}

void writeUpdateResult(QString const& root, UpdateResult const& result)
{
    QString const path = resultPath(root);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.write(QJsonDocument(QJsonObject{
                                 {QStringLiteral("ok"), result.ok},
                                 {QStringLiteral("message"), result.message},
                                 {QStringLiteral("version"), result.version.toString()},
                                 {QStringLiteral("finished"), result.finished.toString(Qt::ISODate)},
                             })
                   .toJson());
    file.commit();
}

std::optional<UpdateResult> takeUpdateResult(QString const& root)
{
    QFile file(resultPath(root));
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    QJsonObject const object = QJsonDocument::fromJson(file.readAll()).object();
    file.close();
    file.remove();

    UpdateResult result;
    result.ok = object.value(QStringLiteral("ok")).toBool();
    result.message = object.value(QStringLiteral("message")).toString();
    result.version = QVersionNumber::fromString(object.value(QStringLiteral("version")).toString());
    result.finished = QDateTime::fromString(object.value(QStringLiteral("finished")).toString(), Qt::ISODate);
    return result;
}

bool startUpdater(QString const& root, QStringList const& arguments, QString* error)
{
    QProcess process;
    process.setProgram(QDir(root).filePath(appInfo().updaterExe));
    process.setArguments(arguments);
    process.setWorkingDirectory(root);
#ifdef Q_OS_WIN
    // The updater is a console program (for its CLI); in GUI mode it mustn't get a console window.
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) {
        args->flags |= CREATE_NO_WINDOW;
    });
#endif
    if (process.startDetached())
        return true;
    if (error)
        *error = process.errorString();
    return false;
}

} // namespace QAppUpdater::InstallLayout
