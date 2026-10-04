#include "QAppUpdater/AppControlServer.h"

#include <QCoreApplication>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>

#include "QAppUpdater/InstallLayout.h"

namespace QAppUpdater {

AppControlServer::AppControlServer(QObject* parent)
    : QObject(parent)
    , m_server(new QLocalServer(this))
{
    QString const name = InstallLayout::controlServerName(InstallLayout::rootDir());
    // Only the current user's processes may connect.
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    if (!m_server->listen(name)) {
        // Most likely a second copy of the app from the same folder; the updater then talks to the first one.
        qWarning().noquote() << "AppControlServer: can't listen on" << name << "-" << m_server->errorString();
        return;
    }
    connect(m_server, &QLocalServer::newConnection, this, &AppControlServer::onNewConnection);
}

void AppControlServer::onNewConnection()
{
    while (QLocalSocket* socket = m_server->nextPendingConnection()) {
        connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        socket->write("pid " + QByteArray::number(QCoreApplication::applicationPid()) + "\n");
        socket->flush();
        connect(socket, &QLocalSocket::readyRead, this, [this, socket] {
            while (socket->canReadLine()) {
                QByteArray const line = socket->readLine().trimmed();
                if (line == "quit") {
                    qDebug() << "AppControlServer: quit requested";
                    Q_EMIT quitRequested();
                } else if (line.startsWith("status ")) {
                    QJsonObject const status = QJsonDocument::fromJson(line.mid(7)).object();
                    Q_EMIT updaterStatus(status.value(QStringLiteral("stage")).toString(),
                                         status.value(QStringLiteral("done")).toInteger(),
                                         status.value(QStringLiteral("total")).toInteger());
                } else if (line.startsWith("result ")) {
                    QJsonObject const result = QJsonDocument::fromJson(line.mid(7)).object();
                    Q_EMIT updaterFinished(result.value(QStringLiteral("ok")).toBool(),
                                           result.value(QStringLiteral("message")).toString());
                }
            }
        });
    }
}

} // namespace QAppUpdater
