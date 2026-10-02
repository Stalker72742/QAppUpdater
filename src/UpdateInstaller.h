#pragma once

#include <QObject>
#include <QPointer>
#include <QStringList>

#include "QAppUpdater/ReleaseFinder.h"

class QFile;
class QLocalSocket;
class QNetworkAccessManager;
class QNetworkReply;
class QProcess;
class QTimer;

// Installs a release over the install folder, in steps:
//   download (GitHub) -> extract (zip, via tar.exe) -> validate the package
//   (incl. its minUpdaterProtocol) ->
//   ask the running app to save and quit, wait for it -> replace files -> restart it.
//
// Only files listed in the package's package.json are written, plus files the
// previously installed package.json listed and the new one doesn't are removed; user data
// (InstallLayout::isProtectedPath) is never touched. Every replaced file
// is first renamed aside, so a failure rolls the whole install back. Renaming
// (unlike overwriting) works on the running updater's own exe and DLLs: they
// keep running from the renamed files, which InstallLayout::removeLeftovers()
// deletes on the next start.
namespace QAppUpdater {

class UpdateInstaller : public QObject {
    Q_OBJECT

public:
    explicit UpdateInstaller(QObject* parent = nullptr);

    // Defaults to InstallLayout::rootDir().
    void setInstallRoot(QString const& root) { m_root = root; }

    void install(ReleaseInfo const& release, bool restartApp);
    // Stops a download/extraction/wait; too late once files are being replaced.
    void cancel();
    bool isRunning() const { return m_running; }

signals:
    void stageChanged(QString const& text);
    void progress(qint64 done, qint64 total); // total < 0: busy, no known size
    void finished(bool ok, QString const& message);

private:
    void download();
    void extract(QString const& zipPath);
    void validateAndCloseApp();
    void closeApp();
    void waitForAppExit(qint64 pid, int attemptsLeft);
    void apply();
    void fail(QString const& message);
    void finish(bool ok, QString const& message);

    QString workPath(QString const& relative) const;

    QString m_root;
    ReleaseInfo m_release;
    bool m_restartApp{true};
    bool m_running{false};
    bool m_canceled{false};

    QString m_packageDir; // extracted package or the local folder itself
    QStringList m_packageFiles;

    QNetworkAccessManager* m_network{nullptr};
    QPointer<QNetworkReply> m_reply;
    QFile* m_downloadFile{nullptr};
    QPointer<QProcess> m_tar;
    QPointer<QLocalSocket> m_socket;
    QPointer<QTimer> m_waitTimer;
};

} // namespace QAppUpdater
