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

    struct ClientMessageState {
        QByteArray buffer;
        bool processed = false;
    };

    enum class ParseResult {
        Incomplete,
        Processed,
        Invalid
    };

    static constexpr quint32 kMaxIpcMessageSize = 1024 * 1024; // 1 MB safety limit
    const QByteArray kActivateMessage = "activate";
    auto state = std::make_shared<ClientMessageState>();

    auto parseBuffer = [this, state, kActivateMessage]() -> ParseResult {
        if (state->processed) {
            return ParseResult::Processed;
        }

        if (state->buffer.isEmpty()) {
            return ParseResult::Incomplete;
        }

        // Backward-compatible plain-text activation message.
        if (state->buffer == kActivateMessage) {
            state->processed = true;
            emit activateRequested();
            return ParseResult::Processed;
        }

        // Keep waiting while "activate" is still arriving in chunks.
        if (kActivateMessage.startsWith(state->buffer)) {
            return ParseResult::Incomplete;
        }

        if (state->buffer.size() < static_cast<int>(sizeof(quint32))) {
            return ParseResult::Incomplete;
        }

        QDataStream stream(state->buffer);
        quint32 messageSize = 0;
        stream >> messageSize;

        if (messageSize == 0 || messageSize > kMaxIpcMessageSize) {
            return ParseResult::Invalid;
        }

        const qint64 totalSize = static_cast<qint64>(sizeof(quint32)) + messageSize;
        if (state->buffer.size() < totalSize) {
            return ParseResult::Incomplete;
        }

        QByteArray jsonData = state->buffer.mid(sizeof(quint32), messageSize);
        state->processed = true;
        emit commandReceived(jsonData);
        return ParseResult::Processed;
    };

    auto cleanupClient = [client]() {
        client->deleteLater();
    };

    auto processPendingData = [client, state, parseBuffer, cleanupClient]() {
        if (state->processed) {
            cleanupClient();
            return;
        }

        state->buffer.append(client->readAll());

        ParseResult result = parseBuffer();
        if (result == ParseResult::Processed || result == ParseResult::Invalid) {
            cleanupClient();
        }
    };

    connect(client, &QLocalSocket::readyRead, this, processPendingData);
    connect(client, &QLocalSocket::disconnected, this, [client, state, parseBuffer, cleanupClient]() {
        if (!state->processed) {
            state->buffer.append(client->readAll());
            parseBuffer();
        }
        cleanupClient();
    });

    // Process immediately if bytes arrived before signal connection completed.
    if (client->bytesAvailable() > 0) {
        processPendingData();
    }
}
