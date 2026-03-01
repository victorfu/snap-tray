#ifndef SNAPTRAY_MCP_SERVER_H
#define SNAPTRAY_MCP_SERVER_H

#include "mcp/MCPTools.h"

#include <QByteArray>
#include <QHostAddress>
#include <QJsonObject>
#include <QJsonValue>
#include <QObject>

#include <memory>

namespace SnapTray {
namespace MCP {

class MCPHttpTransport;

class MCPServer : public QObject
{
    Q_OBJECT

public:
    static constexpr quint16 kDefaultPort = 39200;

    explicit MCPServer(const ToolCallContext& context, QObject* parent = nullptr);
    ~MCPServer() override;

    bool start(quint16 port = kDefaultPort, QString* errorMessage = nullptr);
    void stop();

    bool isListening() const;
    quint16 port() const;
    QHostAddress listenAddress() const;

private:
    void handleRpcPayload(
        const QJsonValue& payload, int* httpStatus, QByteArray* responseBody, QByteArray* contentType);
    QJsonObject handleRequestObject(const QJsonObject& request, bool* hasResponse) const;

    static QJsonObject makeSuccessResponse(const QJsonValue& id, const QJsonObject& result);
    static QJsonObject makeErrorResponse(
        const QJsonValue& id, int code, const QString& message, const QJsonValue& data = QJsonValue());

    ToolCallContext m_context;
    std::unique_ptr<MCPHttpTransport> m_httpTransport;
};

} // namespace MCP
} // namespace SnapTray

#endif // SNAPTRAY_MCP_SERVER_H
