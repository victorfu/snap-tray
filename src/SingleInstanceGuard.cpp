#include "SingleInstanceGuard.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QLockFile>
#include <QLocalServer>
#include <QLocalSocket>
#include <QStandardPaths>
#include <QThread>

#include <memory>

namespace {
constexpr int kConnectionCheckTimeoutMs = 200;
constexpr int kActivationConnectTimeoutMs = 200;
constexpr int kActivationSendTimeoutMs = 1000;
constexpr int kActivationRetryDelayMs = 100;
constexpr int kActivationMaxAttempts = 5;
constexpr int kServerListenRetryDelayMs = 50;
constexpr int kServerListenMaxAttempts = 3;
}

SingleInstanceGuard::SingleInstanceGuard(const QString &appId, QObject *parent)
    : QObject(parent)
{
    // Use hash to avoid special character issues in server name
    m_serverName = QString("snaptray-%1")
        .arg(QString(QCryptographicHash::hash(appId.toUtf8(),
             QCryptographicHash::Md5).toHex()));

    const QString tempDirPath = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    const QString baseDir = tempDirPath.isEmpty() ? QDir::tempPath() : tempDirPath;
    m_lockFilePath = QDir(baseDir).filePath(m_serverName + ".lock");
}

SingleInstanceGuard::~SingleInstanceGuard()
{
    if (m_server && m_server->isListening()) {
        m_server->close();
        QLocalServer::removeServer(m_serverName);
    }

    releaseInstanceLock();
}

bool SingleInstanceGuard::acquireInstanceLock()
{
    if (m_instanceLock && m_instanceLock->isLocked()) {
        return true;
    }

    m_instanceLock = std::make_unique<QLockFile>(m_lockFilePath);
    // This lock is expected to outlive 30 seconds (default stale timeout).
    m_instanceLock->setStaleLockTime(0);

    if (m_instanceLock->tryLock(0)) {
        return true;
    }

    // A stale lock file can survive a crash/forced kill. Recover once before failing.
    if (m_instanceLock->error() == QLockFile::LockFailedError
        && m_instanceLock->removeStaleLockFile()
        && m_instanceLock->tryLock(0)) {
        return true;
    }

    m_instanceLock.reset();
    return false;
}

void SingleInstanceGuard::releaseInstanceLock()
{
    if (!m_instanceLock) {
        return;
    }

    if (m_instanceLock->isLocked()) {
        m_instanceLock->unlock();
    }
    m_instanceLock.reset();
}

bool SingleInstanceGuard::startServerWithSafeRecovery()
{
    if (!m_server) {
        return false;
    }

    for (int attempt = 0; attempt < kServerListenMaxAttempts; ++attempt) {
        if (m_server->listen(m_serverName)) {
            return true;
        }

        // If another server is connectable, treat this process as secondary.
        QLocalSocket probeSocket;
        probeSocket.connectToServer(m_serverName);
        if (probeSocket.waitForConnected(kConnectionCheckTimeoutMs)) {
            probeSocket.disconnectFromServer();
            return false;
        }

        // No active listener found; attempt stale endpoint cleanup and retry.
        QLocalServer::removeServer(m_serverName);
        if (attempt + 1 < kServerListenMaxAttempts) {
            QThread::msleep(kServerListenRetryDelayMs);
        }
    }

    return false;
}

bool SingleInstanceGuard::tryLock()
{
    if (m_server && m_server->isListening()) {
        return true;
    }

    if (!acquireInstanceLock()) {
        return false;
    }

    m_server = new QLocalServer(this);

    if (!startServerWithSafeRecovery()) {
        delete m_server;
        m_server = nullptr;
        releaseInstanceLock();
        return false;
    }

    connect(m_server, &QLocalServer::newConnection,
            this, &SingleInstanceGuard::onNewConnection);
    return true;
}

void SingleInstanceGuard::sendActivateMessage()
{
    for (int attempt = 0; attempt < kActivationMaxAttempts; ++attempt) {
        QLocalSocket socket;
        socket.connectToServer(m_serverName);
        if (socket.waitForConnected(kActivationConnectTimeoutMs)) {
            socket.write("activate");
            if (socket.waitForBytesWritten(kActivationSendTimeoutMs)) {
                socket.disconnectFromServer();
                return;
            }
        }

        if (attempt + 1 < kActivationMaxAttempts) {
            QThread::msleep(kActivationRetryDelayMs);
        }
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
