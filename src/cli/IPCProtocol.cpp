#include "cli/IPCProtocol.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QJsonDocument>
#include <QLocalSocket>

namespace SnapTray {
namespace CLI {

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
    const QString appId = "com.victorfu.snaptray";
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

    // Wait for response
    if (!socket.waitForReadyRead(kResponseTimeout)) {
        response.error = "Timeout waiting for response";
        return response;
    }

    // Read response with length prefix
    QByteArray headerData = socket.read(sizeof(quint32));
    if (headerData.size() < static_cast<int>(sizeof(quint32))) {
        response.error = "Invalid response header";
        return response;
    }

    QDataStream readStream(headerData);
    quint32 size;
    readStream >> size;

    QByteArray responseData;
    while (responseData.size() < static_cast<int>(size)) {
        if (!socket.waitForReadyRead(kResponseTimeout)) {
            break;
        }
        responseData.append(socket.readAll());
    }

    socket.disconnectFromServer();
    return IPCResponse::fromJson(responseData);
}

} // namespace CLI
} // namespace SnapTray
