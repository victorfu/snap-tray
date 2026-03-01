#ifndef SNAPTRAY_MCP_HTTP_TRANSPORT_H
#define SNAPTRAY_MCP_HTTP_TRANSPORT_H

#include <QByteArray>
#include <QHash>
#include <QHostAddress>
#include <QJsonValue>
#include <QObject>

#include <functional>

class QTcpServer;
class QTcpSocket;

namespace SnapTray {
namespace MCP {

class MCPHttpTransport : public QObject
{
    Q_OBJECT

public:
    using RpcHandler = std::function<void(
        const QJsonValue& payload,
        int* httpStatus,
        QByteArray* body,
        QByteArray* contentType)>;

    explicit MCPHttpTransport(QObject* parent = nullptr);
    ~MCPHttpTransport() override;

    bool start(quint16 port, QString* errorMessage = nullptr);
    void stop();
    bool isListening() const;
    quint16 port() const;
    QHostAddress listenAddress() const;
    void setRpcHandler(RpcHandler handler);

private slots:
    void onNewConnection();
    void onSocketReadyRead();
    void onSocketDisconnected();

private:
    struct PendingRequest {
        QByteArray buffer;
        bool headersParsed = false;
        int contentLength = -1;
        QByteArray method;
        QByteArray path;
    };

    bool parseHeaders(PendingRequest& req, int* headerEndPos, QString* errorMessage) const;
    void processSocketBuffer(QTcpSocket* socket);
    void sendResponse(
        QTcpSocket* socket, int statusCode, const QByteArray& contentType, const QByteArray& body);
    static QByteArray reasonPhrase(int statusCode);

    QTcpServer* m_server = nullptr;
    QHash<QTcpSocket*, PendingRequest> m_pendingRequests;
    RpcHandler m_rpcHandler;
};

} // namespace MCP
} // namespace SnapTray

#endif // SNAPTRAY_MCP_HTTP_TRANSPORT_H
