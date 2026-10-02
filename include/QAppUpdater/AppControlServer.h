#pragma once

#include <QObject>

class QLocalServer;

namespace QAppUpdater {

// Lives in the app and lets the updater find and close it (protocol: InstallLayout::controlServerName).
// Reports this process's pid to every client; a "quit" from one emits quitRequested(), which the app
// should answer by saving and exiting.
class AppControlServer : public QObject {
    Q_OBJECT

public:
    explicit AppControlServer(QObject* parent = nullptr);

signals:
    void quitRequested();

private:
    void onNewConnection();

    QLocalServer* m_server{nullptr};
};

} // namespace QAppUpdater
