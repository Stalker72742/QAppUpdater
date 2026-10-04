#pragma once

#include <QObject>

class QLocalServer;

namespace QAppUpdater {

// Lives in the app and lets the updater find and close it (protocol: InstallLayout::controlServerName).
// Reports this process's pid to every client; a "quit" from one emits quitRequested(), which the app
// should answer by saving and exiting. An updater started with --report also streams what it is doing,
// so the app can show the install itself instead of the updater window.
class AppControlServer : public QObject {
    Q_OBJECT

public:
    explicit AppControlServer(QObject* parent = nullptr);

signals:
    void quitRequested();

    // done/total: bytes while downloading, files while installing; total <= 0 when unknown.
    void updaterStatus(QString const& stage, qint64 done, qint64 total);
    // The updater is done without closing the app: up to date, or it failed (the app keeps running).
    void updaterFinished(bool ok, QString const& message);

private:
    void onNewConnection();

    QLocalServer* m_server{nullptr};
};

} // namespace QAppUpdater
