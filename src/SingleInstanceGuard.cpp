#include "SingleInstanceGuard.h"
#include <QLocalServer>
#include <QLocalSocket>
#include <QCryptographicHash>

SingleInstanceGuard::SingleInstanceGuard(const QString &appId, QObject *parent)
    : QObject(parent)
{
    // Use hash to avoid special character issues in server name
    m_serverName = QString("snaptray-%1")
        .arg(QString(QCryptographicHash::hash(appId.toUtf8(),
             QCryptographicHash::Md5).toHex()));
}

SingleInstanceGuard::~SingleInstanceGuard()
{
    if (m_server) {
        m_server->close();
    }
}

bool SingleInstanceGuard::tryLock()
{
    // Try to connect to existing server (check if another instance is running)
    QLocalSocket socket;
    socket.connectToServer(m_serverName);
    if (socket.waitForConnected(500)) {
        // Another instance is already running
        socket.disconnectFromServer();
        return false;
    }

    // Create server
    m_server = new QLocalServer(this);

    // Remove any stale socket file (Linux/macOS)
    QLocalServer::removeServer(m_serverName);

    if (!m_server->listen(m_serverName)) {
        return false;
    }

    connect(m_server, &QLocalServer::newConnection,
            this, &SingleInstanceGuard::onNewConnection);
    return true;
}

void SingleInstanceGuard::sendActivateMessage()
{
    QLocalSocket socket;
    socket.connectToServer(m_serverName);
    if (socket.waitForConnected(1000)) {
        socket.write("activate");
        socket.waitForBytesWritten(1000);
        socket.disconnectFromServer();
    }
}

void SingleInstanceGuard::onNewConnection()
{
    QLocalSocket *client = m_server->nextPendingConnection();
    if (client) {
        connect(client, &QLocalSocket::readyRead, this, [this, client]() {
            client->readAll();  // Read message (currently only "activate")
            emit activateRequested();
            client->deleteLater();
        });
        connect(client, &QLocalSocket::disconnected,
                client, &QLocalSocket::deleteLater);
    }
}
