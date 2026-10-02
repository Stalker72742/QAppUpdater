#include "QAppUpdater/UpdaterConfig.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include "QAppUpdater/AppInfo.h"
#include "QAppUpdater/InstallLayout.h"

namespace QAppUpdater {

UpdaterConfig UpdaterConfig::defaults()
{
    UpdaterConfig config;
    config.repo = appInfo().defaultRepo;
    return config;
}

QString UpdaterConfig::defaultPath()
{
    return QDir(InstallLayout::rootDir()).filePath(InstallLayout::kUpdaterConfigFile);
}

QString UpdaterConfig::sourceName(Source source)
{
    return source == Source::Local ? QStringLiteral("local") : QStringLiteral("github");
}

QString UpdaterConfig::describeSource() const
{
    if (source == Source::Local)
        return QStringLiteral("Local: ") + (localPath.isEmpty() ? QStringLiteral("(no path set)") : QDir::toNativeSeparators(localPath));
    return QStringLiteral("GitHub: ") + (repo.isEmpty() ? QStringLiteral("(no repository set)") : repo)
           + (prerelease ? QStringLiteral(" (incl. pre-releases)") : QString());
}

bool UpdaterConfig::load(QString const& path, UpdaterConfig& config, QString* error)
{
    config = defaults();

    QFile file(path);
    if (!file.exists())
        return true;
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }

    QJsonParseError parseError;
    QJsonDocument const doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (!doc.isObject()) {
        if (error)
            *error = parseError.error != QJsonParseError::NoError
                         ? QStringLiteral("%1 at offset %2").arg(parseError.errorString()).arg(parseError.offset)
                         : QStringLiteral("not a JSON object");
        return false;
    }

    QJsonObject const root = doc.object();
    QString const source = root.value(QStringLiteral("source")).toString(sourceName(config.source));
    if (source == QLatin1String("local"))
        config.source = Source::Local;
    else if (source == QLatin1String("github"))
        config.source = Source::GitHub;
    else {
        if (error)
            *error = QStringLiteral("unknown source \"%1\" (expected \"github\" or \"local\")").arg(source);
        return false;
    }

    QJsonObject const github = root.value(QStringLiteral("github")).toObject();
    config.repo = github.value(QStringLiteral("repo")).toString(config.repo);
    config.prerelease = github.value(QStringLiteral("prerelease")).toBool(config.prerelease);
    config.localPath = root.value(QStringLiteral("local")).toObject().value(QStringLiteral("path")).toString();
    config.restartApp = root.value(QStringLiteral("restartApp")).toBool(config.restartApp);
    return true;
}

bool UpdaterConfig::save(QString const& path, QString* error) const
{
    QJsonObject const root{
        {QStringLiteral("source"), sourceName(source)},
        {QStringLiteral("github"), QJsonObject{{QStringLiteral("repo"), repo}, {QStringLiteral("prerelease"), prerelease}}},
        {QStringLiteral("local"), QJsonObject{{QStringLiteral("path"), localPath}}},
        {QStringLiteral("restartApp"), restartApp},
    };

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

} // namespace QAppUpdater
