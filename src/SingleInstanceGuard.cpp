#include "SingleInstanceGuard.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QLocalServer>
#include <QLocalSocket>

#include <memory>

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
        QLocalServer::removeServer(m_serverName);
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
    QLocalSocket* client = m_server->nextPendingConnection();
    if (!client) {
        return;
    }

    // Use shared flag to prevent double-processing
    auto processed = std::make_shared<bool>(false);

    // Lambda to process received data (only once)
    auto processData = [this, client, processed]() {
        // Prevent double-processing
        if (*processed) {
            return;
        }
        *processed = true;

        QByteArray data = client->readAll();

        // Always schedule client for deletion (fixes cleanup on early return)
        client->deleteLater();

        if (data.isEmpty()) {
            return;
        }

        // Check if it's a simple "activate" message (backward compat)
        if (data == "activate") {
            emit activateRequested();
        }
        // Check for length-prefixed JSON message
        else if (data.size() >= static_cast<int>(sizeof(quint32))) {
            QDataStream stream(data);
            quint32 messageSize;
            stream >> messageSize;

            // Read the JSON message
            QByteArray jsonData = data.mid(sizeof(quint32), messageSize);

            // If we have the complete message
            if (jsonData.size() >= static_cast<int>(messageSize)) {
                emit commandReceived(jsonData);
            }
        }
    };

    connect(client, &QLocalSocket::readyRead, this, processData);
    // Handle disconnection - cleanup if processData hasn't run yet
    connect(client, &QLocalSocket::disconnected, this, [processed, client]() {
        if (!*processed) {
            *processed = true;
            client->deleteLater();
        }
    });

    // Check if data is already available (race condition fix)
    if (client->bytesAvailable() > 0) {
        processData();
    }
}
