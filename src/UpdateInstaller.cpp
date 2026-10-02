#include "UpdateInstaller.h"

#include <memory>

#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocalSocket>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QTimer>

#include "QAppUpdater/AppInfo.h"
#include "QAppUpdater/InstallLayout.h"
#include "SystemTools.h"

namespace {

constexpr int kAppExitPollMs = 200;
constexpr int kAppExitTimeoutMs = 30000;
constexpr int kControlConnectTimeoutMs = 1500;

bool isSafeRelativePath(QString const& relative)
{
    if (relative.isEmpty() || relative.contains(QLatin1Char('\\')) || QDir::isAbsolutePath(relative)
        || relative.contains(QLatin1Char(':')))
        return false;
    // Windows strips a trailing dot/space from a name ("Saved." is "Saved")
    // and resolves 8.3 short names ("DEFAUL~1"): either would let a path slip
    // past InstallLayout::isProtectedPath()'s name check.
    for (QString const& segment : relative.split(QLatin1Char('/'))) {
        if (segment.endsWith(QLatin1Char('.')) || segment.endsWith(QLatin1Char(' ')) || segment.contains(QLatin1Char('~')))
            return false;
    }
    return QDir::cleanPath(relative) == relative && !relative.startsWith(QLatin1String("../"))
           && relative != QLatin1String("..");
}

QByteArray fileHash(QString const& path, QCryptographicHash::Algorithm algorithm)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(algorithm);
    hash.addData(&file);
    return hash.result().toHex();
}

bool sameContents(QString const& a, QString const& b)
{
    QFileInfo const infoA(a);
    QFileInfo const infoB(b);
    if (!infoA.isFile() || !infoB.isFile() || infoA.size() != infoB.size())
        return false;
    return fileHash(a, QCryptographicHash::Sha1) == fileHash(b, QCryptographicHash::Sha1);
}

QString backupPathFor(QString const& target)
{
    QString candidate = target + QStringLiteral(".old");
    for (int i = 1; QFile::exists(candidate) && !QFile::remove(candidate); ++i)
        candidate = target + QStringLiteral(".old%1").arg(i);
    return candidate;
}

} // namespace

namespace QAppUpdater {

UpdateInstaller::UpdateInstaller(QObject* parent)
    : QObject(parent)
    , m_root(InstallLayout::rootDir())
    , m_network(new QNetworkAccessManager(this))
{
}

QString UpdateInstaller::workPath(QString const& relative) const
{
    return QDir(m_root).filePath(InstallLayout::kWorkDir + QLatin1Char('/') + relative);
}

void UpdateInstaller::install(ReleaseInfo const& release, bool restartApp)
{
    if (m_running)
        return;
    m_running = true;
    m_canceled = false;
    m_release = release;
    m_restartApp = restartApp;
    m_packageDir.clear();
    m_packageFiles.clear();
    qInfo().noquote() << "UpdateInstaller: installing" << release.version.toString() << "into" << m_root;

    if (!release.packageUrl.isEmpty())
        download();
    else if (release.isLocalFolder()) {
        m_packageDir = release.packagePath;
        validateAndCloseApp();
    } else
        extract(release.packagePath);
}

void UpdateInstaller::cancel()
{
    if (!m_running)
        return;
    m_canceled = true;
    if (m_reply)
        m_reply->abort();
    if (m_tar)
        m_tar->kill();
    if (m_waitTimer)
        m_waitTimer->stop();
    if (m_socket)
        m_socket->abort();
    fail(tr("Canceled."));
}

// --- Download ---------------------------------------------------------------

void UpdateInstaller::download()
{
    Q_EMIT stageChanged(tr("Downloading %1...").arg(m_release.title));
    Q_EMIT progress(0, m_release.packageSize);

    QString const fileName = QFileInfo(QUrl(m_release.packageUrl).path()).fileName();
    QString const target = workPath(QStringLiteral("download/") + fileName);
    QDir().mkpath(QFileInfo(target).absolutePath());

    m_downloadFile = new QFile(target + QStringLiteral(".part"), this);
    if (!m_downloadFile->open(QIODevice::WriteOnly | QIODevice::Truncate))
        return fail(tr("Can't write %1: %2").arg(m_downloadFile->fileName(), m_downloadFile->errorString()));

    QNetworkRequest request{QUrl(m_release.packageUrl)};
    request.setHeader(QNetworkRequest::UserAgentHeader, appInfo().name + QStringLiteral("-Updater"));
    request.setRawHeader("Accept", "application/octet-stream");
    request.setTransferTimeout(30000); // since the last received byte, not in total

    m_reply = m_network->get(request);
    QNetworkReply* reply = m_reply;
    // A failed write (full disk) stops the download and is what gets reported,
    // not the "incomplete download" it would otherwise turn into.
    auto writeError = std::make_shared<QString>();
    auto writeChunk = [this, reply, writeError] {
        QByteArray const data = reply->readAll();
        if (writeError->isEmpty() && m_downloadFile->write(data) != data.size()) {
            *writeError = m_downloadFile->errorString();
            reply->abort();
        }
    };
    connect(reply, &QNetworkReply::readyRead, this, writeChunk);
    connect(reply, &QNetworkReply::downloadProgress, this, &UpdateInstaller::progress);
    connect(reply, &QNetworkReply::finished, this, [this, reply, target, writeChunk, writeError] {
        reply->deleteLater();
        writeChunk();
        m_downloadFile->close();
        QString const partPath = m_downloadFile->fileName();
        m_downloadFile->deleteLater();
        m_downloadFile = nullptr;

        if (m_canceled)
            return;
        if (!writeError->isEmpty())
            return fail(tr("Can't write the download: %1").arg(*writeError));
        if (reply->error() != QNetworkReply::NoError)
            return fail(tr("Download failed: %1").arg(reply->errorString()));
        if (m_release.packageSize >= 0 && QFileInfo(partPath).size() != m_release.packageSize)
            return fail(tr("Download is incomplete (%1 of %2 bytes).").arg(QFileInfo(partPath).size()).arg(m_release.packageSize));
        if (!m_release.packageSha256.isEmpty()
            && fileHash(partPath, QCryptographicHash::Sha256) != m_release.packageSha256)
            return fail(tr("The downloaded package doesn't match its SHA-256 checksum."));

        QFile::remove(target);
        if (!QFile::rename(partPath, target))
            return fail(tr("Can't write %1.").arg(target));
        extract(target);
    });
}

// --- Extract ----------------------------------------------------------------

void UpdateInstaller::extract(QString const& zipPath)
{
    Q_EMIT stageChanged(tr("Unpacking..."));
    Q_EMIT progress(0, -1);

    QString const staging = workPath(QStringLiteral("staging"));
    QDir(staging).removeRecursively();
    if (!QDir().mkpath(staging))
        return fail(tr("Can't create %1.").arg(staging));

    m_tar = new QProcess(this);
    QProcess* tar = m_tar;
    connect(tar, &QProcess::finished, this, [this, tar, staging](int exitCode, QProcess::ExitStatus status) {
        tar->deleteLater();
        if (m_canceled)
            return;
        if (status != QProcess::NormalExit || exitCode != 0)
            return fail(tr("Unpacking failed: %1").arg(QString::fromLocal8Bit(tar->readAllStandardError()).trimmed()));
        m_packageDir = staging;
        validateAndCloseApp();
    });
    connect(tar, &QProcess::errorOccurred, this, [this, tar](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart && !m_canceled)
            fail(tr("Can't run %1 (needs Windows 10 1803 or later).").arg(SystemTools::tarExe()));
    });
    tar->start(SystemTools::tarExe(),
               {QStringLiteral("-xf"), QDir::toNativeSeparators(zipPath), QStringLiteral("-C"), QDir::toNativeSeparators(staging)});
}

// --- Validate + close the app -----------------------------------------------

void UpdateInstaller::validateAndCloseApp()
{
    QDir const package(m_packageDir);
    if (QDir::cleanPath(package.absolutePath()).compare(QDir::cleanPath(QDir(m_root).absolutePath()), Qt::CaseInsensitive) == 0)
        return fail(tr("The update source is this installation itself."));

    InstallLayout::PackageManifest manifest;
    QString error;
    if (!InstallLayout::PackageManifest::read(package.filePath(InstallLayout::kManifestFile), manifest, &error))
        return fail(tr("The package has no readable %1: %2").arg(InstallLayout::kManifestFile, error));

    // A package made for a newer install procedure than this updater knows.
    if (manifest.minUpdaterProtocol > kUpdaterProtocol) {
        QString const where = m_release.pageUrl.isEmpty() ? tr("Install it manually.")
                                                          : tr("Download and install it manually: %1").arg(m_release.pageUrl);
        return fail(tr("%1 %2 can't be installed by this updater (it needs updater protocol %3, this one "
                       "has %4). %5")
                        .arg(appInfo().name, manifest.version.toString())
                        .arg(manifest.minUpdaterProtocol)
                        .arg(kUpdaterProtocol)
                        .arg(where));
    }

    m_packageFiles = manifest.files;
    if (m_packageFiles.isEmpty())
        return fail(tr("The package's %1 lists no files.").arg(InstallLayout::kManifestFile));
    if (!m_packageFiles.contains(appInfo().appExe))
        return fail(tr("The package doesn't contain %1.").arg(appInfo().appExe));

    for (QString const& relative : std::as_const(m_packageFiles)) {
        if (!isSafeRelativePath(relative) || InstallLayout::isProtectedPath(relative))
            return fail(tr("The package lists a file it may not write: %1").arg(relative));
        if (!QFileInfo(package.filePath(relative)).isFile())
            return fail(tr("The package is missing %1.").arg(relative));
    }

    closeApp();
}

void UpdateInstaller::closeApp()
{
    Q_EMIT stageChanged(tr("Closing %1...").arg(appInfo().name));

    m_socket = new QLocalSocket(this);
    QLocalSocket* socket = m_socket;
    auto notRunning = [this, socket] {
        socket->deleteLater();
        if (!m_canceled)
            apply();
    };

    connect(socket, &QLocalSocket::errorOccurred, this, [notRunning](QLocalSocket::LocalSocketError) { notRunning(); },
            Qt::SingleShotConnection);
    connect(socket, &QLocalSocket::connected, this, [this, socket] {
        // The app greets with its pid, then gets told to quit.
        auto onGreeting = [this, socket] {
            if (!socket->canReadLine())
                return;
            QByteArray const line = socket->readLine().trimmed();
            qint64 const pid = line.startsWith("pid ") ? line.mid(4).toLongLong() : 0;
            if (pid <= 0) {
                // Without a pid there's no telling when it has exited.
                socket->disconnect(this);
                socket->abort();
                socket->deleteLater();
                return fail(tr("%1 is running but didn't respond. Close it yourself and try again.").arg(appInfo().name));
            }
            socket->write("quit\n");
            socket->flush();
            socket->disconnect(this);
            socket->deleteLater();
            qInfo() << "UpdateInstaller: asked the app (pid" << pid << ") to quit";
            Q_EMIT stageChanged(tr("Waiting for %1 to save and close...").arg(appInfo().name));
            waitForAppExit(pid, kAppExitTimeoutMs / kAppExitPollMs);
        };
        connect(socket, &QLocalSocket::readyRead, this, onGreeting);
        QTimer::singleShot(kControlConnectTimeoutMs, socket, [this, socket] {
            // Disarm the socket first: a late greeting, or the app closing
            // (-> errorOccurred), would otherwise still go on to install.
            socket->disconnect(this);
            socket->abort();
            socket->deleteLater();
            fail(tr("%1 is running but didn't respond. Close it yourself and try again.").arg(appInfo().name));
        });
        onGreeting();
    });

    socket->connectToServer(InstallLayout::controlServerName(m_root));
    QTimer::singleShot(kControlConnectTimeoutMs, socket, [socket] {
        if (socket->state() != QLocalSocket::ConnectedState)
            socket->abort(); // -> errorOccurred -> treated as not running
    });
}

void UpdateInstaller::waitForAppExit(qint64 pid, int attemptsLeft)
{
    if (m_canceled || !m_running)
        return;
    if (pid <= 0 || !SystemTools::isProcessRunning(pid)) {
        apply();
        return;
    }
    if (attemptsLeft <= 0)
        return fail(tr("%1 didn't close. Close it yourself and try again.").arg(appInfo().name));

    m_waitTimer = new QTimer(this);
    m_waitTimer->setSingleShot(true);
    connect(m_waitTimer, &QTimer::timeout, this, [this, pid, attemptsLeft] {
        m_waitTimer->deleteLater();
        waitForAppExit(pid, attemptsLeft - 1);
    });
    m_waitTimer->start(kAppExitPollMs);
}

// --- Replace files ----------------------------------------------------------

void UpdateInstaller::apply()
{
    if (!m_running)
        return; // already finished (failed or canceled): never touch the install

    Q_EMIT stageChanged(tr("Installing %1...").arg(m_release.version.toString()));

    struct Change {
        QString target;
        QString backup;   // where the previous file went; empty if there was none
        bool written{false};
    };
    QList<Change> journal;
    QDir const root(m_root);
    QDir const package(m_packageDir);
    // Read before the loop below overwrites package.json with the new one.
    QStringList const oldFiles = InstallLayout::installedFiles(m_root);

    QStringList unrestored; // files a rollback couldn't put back: the install is broken
    auto rollback = [&journal, &unrestored](QString const& reason) {
        qWarning().noquote() << "UpdateInstaller: rolling back:" << reason;
        for (auto it = journal.rbegin(); it != journal.rend(); ++it) {
            if (it->written)
                QFile::remove(it->target);
            if (!it->backup.isEmpty() && !QFile::rename(it->backup, it->target)) {
                qWarning().noquote() << "UpdateInstaller: couldn't restore" << it->target << "from" << it->backup;
                unrestored << QDir::toNativeSeparators(it->target);
            }
        }
    };
    // After a rollback: the failure, plus whatever the rollback left broken.
    auto failRolledBack = [this, &unrestored](QString message) {
        if (!unrestored.isEmpty())
            message += QStringLiteral("\n\n")
                       + tr("These files couldn't be restored, so %1 may not start: %2. "
                            "Download the release and install it by hand.")
                             .arg(appInfo().name, unrestored.join(QStringLiteral(", ")));
        fail(message);
    };
    auto moveAside = [&journal](QString const& target) -> bool {
        QString const backup = backupPathFor(target);
        if (!QFile::rename(target, backup))
            return false;
        journal.append({target, backup, false});
        return true;
    };

    // package.json goes last: until it's written, the old list still
    // describes what's on disk.
    QStringList toWrite = m_packageFiles;
    toWrite.append(InstallLayout::kManifestFile);

    qint64 const total = toWrite.size();
    qint64 done = 0;
    int skipped = 0;
    for (QString const& relative : std::as_const(toWrite)) {
        Q_EMIT progress(done++, total);
        QString const source = package.filePath(relative);
        QString const target = root.filePath(relative);

        if (sameContents(source, target)) {
            ++skipped;
            continue;
        }
        if (!QDir().mkpath(QFileInfo(target).absolutePath())) {
            rollback(QStringLiteral("mkpath"));
            return failRolledBack(tr("Can't create the folder for %1.").arg(relative));
        }
        if (QFile::exists(target) && !moveAside(target)) {
            rollback(QStringLiteral("rename ") + target);
            return failRolledBack(tr("%1 is in use and can't be replaced.").arg(relative));
        }
        if (!QFile::copy(source, target)) {
            rollback(QStringLiteral("copy ") + target);
            return failRolledBack(tr("Can't write %1.").arg(relative));
        }
        if (journal.isEmpty() || journal.last().target != target)
            journal.append({target, QString(), true});
        else
            journal.last().written = true;
    }

    // Program files the new version no longer ships.
    for (QString const& relative : oldFiles) {
        if (m_packageFiles.contains(relative, Qt::CaseInsensitive) || !isSafeRelativePath(relative)
            || InstallLayout::isProtectedPath(relative))
            continue;
        QString const target = root.filePath(relative);
        if (QFile::exists(target) && !moveAside(target)) {
            rollback(QStringLiteral("remove ") + target);
            return failRolledBack(tr("%1 is in use and can't be removed.").arg(relative));
        }
    }
    Q_EMIT progress(total, total);

    // Committed. Delete the backups; the ones still in use (this updater's
    // own exe and DLLs) go on the leftovers list for the next start.
    QStringList locked;
    for (Change const& change : std::as_const(journal))
        if (!change.backup.isEmpty() && !QFile::remove(change.backup))
            locked.append(change.backup);
    InstallLayout::addLeftovers(m_root, locked);

    if (m_packageDir == workPath(QStringLiteral("staging")))
        QDir(m_packageDir).removeRecursively();
    QDir(workPath(QStringLiteral("download"))).removeRecursively();

    qInfo().noquote() << "UpdateInstaller: installed" << m_release.version.toString() << "-" << journal.size()
                      << "files changed," << skipped << "unchanged," << locked.size() << "left for later";

    if (m_restartApp) {
        if (QProcess::startDetached(root.filePath(appInfo().appExe), {}, m_root))
            qInfo() << "UpdateInstaller: restarted the app";
        else
            qWarning() << "UpdateInstaller: couldn't restart the app";
    }
    finish(true, tr("%1 %2 is installed.").arg(appInfo().name, m_release.version.toString()));
}

void UpdateInstaller::fail(QString const& message)
{
    qWarning().noquote() << "UpdateInstaller: failed:" << message;
    finish(false, message);
}

void UpdateInstaller::finish(bool ok, QString const& message)
{
    if (!m_running)
        return;
    m_running = false;
    Q_EMIT finished(ok, message);
}

} // namespace QAppUpdater
