#include "QAppUpdater/ReleaseFinder.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QRegularExpression>
#include <QTimer>

#include "QAppUpdater/AppInfo.h"
#include "QAppUpdater/InstallLayout.h"
#include "SystemTools.h"

namespace {

constexpr int kRequestTimeoutMs = 20000;

QVersionNumber versionFromTag(QString tag)
{
    if (tag.startsWith(QLatin1Char('v'), Qt::CaseInsensitive))
        tag.remove(0, 1);
    return QVersionNumber::fromString(tag);
}

} // namespace

namespace QAppUpdater {

bool ReleaseInfo::isLocalFolder() const
{
    return !packagePath.isEmpty() && QFileInfo(packagePath).isDir();
}

bool readFileFromZip(QString const& zipPath, QString const& fileInZip, QByteArray& contents, QString* error)
{
    QProcess tar;
    tar.start(SystemTools::tarExe(), {QStringLiteral("-xOf"), QDir::toNativeSeparators(zipPath), fileInZip});
    if (!tar.waitForFinished(30000) || tar.exitStatus() != QProcess::NormalExit || tar.exitCode() != 0) {
        if (error)
            *error = QStringLiteral("can't read %1 from %2: %3")
                         .arg(fileInZip, zipPath, QString::fromLocal8Bit(tar.readAllStandardError()).trimmed());
        return false;
    }
    contents = tar.readAllStandardOutput();
    return true;
}

ReleaseFinder::ReleaseFinder(QObject* parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
{
}

void ReleaseFinder::cancel()
{
    if (m_reply)
        m_reply->abort();
}

void ReleaseFinder::find(UpdaterConfig const& config)
{
    cancel();
    qDebug().noquote() << "ReleaseFinder: looking for the latest release," << config.describeSource();
    if (config.source == UpdaterConfig::Source::Local)
        findLocal(config);
    else
        findOnGitHub(config);
}

void ReleaseFinder::findLocal(UpdaterConfig const& config)
{
    // Results are delivered asynchronously either way, so callers never see
    // a signal before find() returns.
    auto fail = [this](QString const& error) { QTimer::singleShot(0, this, [this, error] { Q_EMIT failed(error); }); };

    QString const path = config.localPath;
    QFileInfo const info(path);
    if (path.isEmpty())
        return fail(tr("No local update path is set."));
    if (!info.exists())
        return fail(tr("%1 doesn't exist.").arg(QDir::toNativeSeparators(path)));

    ReleaseInfo release;
    release.packagePath = info.absoluteFilePath();
    QByteArray json;
    QString error;
    if (info.isDir()) {
        QFile file(QDir(path).filePath(InstallLayout::kManifestFile));
        if (file.open(QIODevice::ReadOnly))
            json = file.readAll();
    } else {
        if (!readFileFromZip(path, InstallLayout::kManifestFile, json, &error))
            return fail(error);
        release.packageSize = info.size();
    }
    InstallLayout::PackageManifest manifest;
    if (!InstallLayout::PackageManifest::parse(json, manifest, &error))
        return fail(tr("%1 has no readable %2 - is it a deployed %3 build or package?")
                        .arg(QDir::toNativeSeparators(path), InstallLayout::kManifestFile, appInfo().name));

    release.version = manifest.version;
    release.title = QStringLiteral("%1 %2").arg(appInfo().name, release.version.toString());
    QString const origin = tr("Local build from `%1`%2.")
                               .arg(QDir::toNativeSeparators(release.packagePath),
                                    manifest.built.isEmpty() ? QString() : tr(", built %1").arg(manifest.built));
    release.notes = manifest.notes.isEmpty() ? origin : manifest.notes + QStringLiteral("\n\n---\n\n") + origin;
    QTimer::singleShot(0, this, [this, release] { Q_EMIT found(release); });
}

void ReleaseFinder::findOnGitHub(UpdaterConfig const& config)
{
    static QRegularExpression const repoPattern(QStringLiteral("^[\\w.-]+/[\\w.-]+$"));
    if (!repoPattern.match(config.repo).hasMatch()) {
        QString const error = config.repo.isEmpty() ? tr("No GitHub repository is set.")
                                                    : tr("\"%1\" isn't a GitHub repository (expected owner/name).").arg(config.repo);
        QTimer::singleShot(0, this, [this, error] { Q_EMIT failed(error); });
        return;
    }

    // /releases/latest never returns pre-releases; the plain list (newest
    // first) does.
    bool const listRequest = config.prerelease;
    QUrl const url(QStringLiteral("https://api.github.com/repos/%1/releases%2")
                       .arg(config.repo, listRequest ? QStringLiteral("?per_page=10") : QStringLiteral("/latest")));
    QNetworkRequest request(url);
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    request.setHeader(QNetworkRequest::UserAgentHeader, appInfo().name + QStringLiteral("-Updater"));
    request.setTransferTimeout(kRequestTimeoutMs);

    m_reply = m_network->get(request);
    QNetworkReply* reply = m_reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, listRequest] {
        reply->deleteLater();
        onGitHubReply(reply, listRequest);
    });
}

void ReleaseFinder::onGitHubReply(QNetworkReply* reply, bool listRequest)
{
    int const status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() == QNetworkReply::OperationCanceledError)
        return;
    if (status == 404) {
        Q_EMIT failed(tr("No releases found (or the repository doesn't exist or is private)."));
        return;
    }
    if (reply->error() != QNetworkReply::NoError) {
        Q_EMIT failed(tr("GitHub request failed: %1").arg(reply->errorString()));
        return;
    }

    QJsonDocument const doc = QJsonDocument::fromJson(reply->readAll());
    QJsonObject releaseJson;
    if (listRequest) {
        for (QJsonValue const& value : doc.array()) {
            if (!value.toObject().value(QStringLiteral("draft")).toBool()) {
                releaseJson = value.toObject();
                break;
            }
        }
    } else {
        releaseJson = doc.object();
    }
    if (releaseJson.isEmpty()) {
        Q_EMIT failed(tr("No releases found."));
        return;
    }

    ReleaseInfo release;
    QString const tag = releaseJson.value(QStringLiteral("tag_name")).toString();
    release.version = versionFromTag(tag);
    release.title = releaseJson.value(QStringLiteral("name")).toString(tag);
    if (release.title.isEmpty())
        release.title = tag;
    release.notes = releaseJson.value(QStringLiteral("body")).toString();
    release.pageUrl = releaseJson.value(QStringLiteral("html_url")).toString();
    if (release.version.isNull()) {
        Q_EMIT failed(tr("The latest release's tag \"%1\" isn't a version number.").arg(tag));
        return;
    }

    for (QJsonValue const& value : releaseJson.value(QStringLiteral("assets")).toArray()) {
        QJsonObject const asset = value.toObject();
        if (isPackageFileName(asset.value(QStringLiteral("name")).toString())) {
            release.packageUrl = asset.value(QStringLiteral("browser_download_url")).toString();
            release.packageSize = asset.value(QStringLiteral("size")).toInteger(-1);
            QString const digest = asset.value(QStringLiteral("digest")).toString();
            if (digest.startsWith(QLatin1String("sha256:")))
                release.packageSha256 = digest.mid(7).toLatin1().toLower();
            break;
        }
    }
    if (release.packageUrl.isEmpty()) {
        Q_EMIT failed(tr("Release %1 has no %2-<version>-win64.zip asset.").arg(tag, appInfo().name));
        return;
    }

    qDebug().noquote() << "ReleaseFinder: latest release" << tag << release.packageUrl;
    Q_EMIT found(release);
}

} // namespace QAppUpdater
