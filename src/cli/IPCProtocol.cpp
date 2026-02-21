#include "cli/IPCProtocol.h"
#include "version.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QLocalSocket>

#include <limits>

namespace SnapTray {
namespace CLI {

namespace {

bool readExactWithDeadline(
    QLocalSocket& socket,
    qsizetype targetSize,
    int timeoutMs,
    QElapsedTimer& timer,
    QByteArray& output)
{
    output.clear();
    if (targetSize <= static_cast<qsizetype>(std::numeric_limits<int>::max())) {
        output.reserve(static_cast<int>(targetSize));
    }

    while (output.size() < targetSize) {
        const qint64 bytesToRead = static_cast<qint64>(targetSize - output.size());
        const QByteArray chunk = socket.read(bytesToRead);
        if (!chunk.isEmpty()) {
            output.append(chunk);
            continue;
        }

        const int remainingTimeout = timeoutMs - static_cast<int>(timer.elapsed());
        if (remainingTimeout <= 0) {
            return false;
        }

        if (!socket.waitForReadyRead(remainingTimeout)) {
            return false;
        }
    }

    return true;
}

} // namespace

// --- IPCMessage ---

QByteArray IPCMessage::toJson() const
{
    QJsonObject obj;
    obj["command"] = command;
    obj["options"] = options;
    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

IPCMessage IPCMessage::fromJson(const QByteArray& data)
{
    IPCMessage msg;
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isObject()) {
        QJsonObject obj = doc.object();
        msg.command = obj["command"].toString();
        msg.options = obj["options"].toObject();
    }
    return msg;
}

// --- IPCResponse ---

QByteArray IPCResponse::toJson() const
{
    QJsonObject obj;
    obj["success"] = success;
    obj["message"] = message;
    obj["error"] = error;
    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

IPCResponse IPCResponse::fromJson(const QByteArray& data)
{
    IPCResponse resp;
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isObject()) {
        QJsonObject obj = doc.object();
        resp.success = obj["success"].toBool();
        resp.message = obj["message"].toString();
        resp.error = obj["error"].toString();
    }
    return resp;
}

// --- IPCProtocol ---

IPCProtocol::IPCProtocol() = default;
IPCProtocol::~IPCProtocol() = default;

QString IPCProtocol::getServerName()
{
    // Must match SingleInstanceGuard server name generation
    const QString appId = SNAPTRAY_APP_BUNDLE_ID;
    return QString("snaptray-%1")
        .arg(QString(QCryptographicHash::hash(appId.toUtf8(), QCryptographicHash::Md5).toHex()));
}

bool IPCProtocol::isMainInstanceRunning() const
{
    QLocalSocket socket;
    socket.connectToServer(getServerName());
    bool connected = socket.waitForConnected(500);
    if (connected) {
        socket.disconnectFromServer();
    }
    return connected;
}

IPCResponse IPCProtocol::sendCommand(const IPCMessage& message, bool waitResponse)
{
    IPCResponse response;
    QLocalSocket socket;

    socket.connectToServer(getServerName());
    if (!socket.waitForConnected(kConnectionTimeout)) {
        response.error = "Failed to connect to SnapTray";
        return response;
    }

    // Send message with length prefix
    QByteArray data = message.toJson();
    QByteArray packet;
    QDataStream stream(&packet, QIODevice::WriteOnly);
    stream << static_cast<quint32>(data.size());
    packet.append(data);
    socket.write(packet);

    if (!socket.waitForBytesWritten(kConnectionTimeout)) {
        response.error = "Failed to send command";
        return response;
    }

    if (!waitResponse) {
        response.success = true;
        response.message = "Command sent";
        socket.disconnectFromServer();
        return response;
    }

    // Read response with length prefix
    QElapsedTimer responseTimer;
    responseTimer.start();

    QByteArray headerData;
    if (!readExactWithDeadline(
            socket, sizeof(quint32), kResponseTimeout, responseTimer, headerData)) {
        response.error = "Timeout waiting for response";
        return response;
    }

    QDataStream readStream(headerData);
    quint32 size = 0;
    readStream >> size;
    if (readStream.status() != QDataStream::Ok || size == 0) {
        response.error = "Invalid response header";
        return response;
    }

    QByteArray responseData;
    if (!readExactWithDeadline(socket, size, kResponseTimeout, responseTimer, responseData)) {
        response.error = "Timeout waiting for response";
        return response;
    }

    socket.disconnectFromServer();
    return IPCResponse::fromJson(responseData);
}

} // namespace CLI
} // namespace SnapTray
