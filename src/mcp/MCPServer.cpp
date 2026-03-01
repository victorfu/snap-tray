#include "mcp/MCPServer.h"

#include "mcp/MCPHttpTransport.h"
#include "version.h"

#include <QJsonArray>
#include <QJsonDocument>

namespace SnapTray {
namespace MCP {
namespace {

constexpr const char* kJsonRpcVersion = "2.0";
constexpr const char* kMcpProtocolVersion = "2025-11-25";

QJsonObject makeTextContent(const QString& text)
{
    return QJsonObject{{"type", "text"}, {"text", text}};
}

} // namespace

MCPServer::MCPServer(const ToolCallContext& context, QObject* parent)
    : QObject(parent)
    , m_context(context)
    , m_httpTransport(std::make_unique<MCPHttpTransport>(this))
{
    m_httpTransport->setRpcHandler([this](
                                       const QJsonValue& payload,
                                       int* httpStatus,
                                       QByteArray* responseBody,
                                       QByteArray* contentType) {
        handleRpcPayload(payload, httpStatus, responseBody, contentType);
    });
}

MCPServer::~MCPServer()
{
    stop();
}

bool MCPServer::start(quint16 port, QString* errorMessage)
{
    return m_httpTransport->start(port, errorMessage);
}

void MCPServer::stop()
{
    m_httpTransport->stop();
}

bool MCPServer::isListening() const
{
    return m_httpTransport->isListening();
}

quint16 MCPServer::port() const
{
    return m_httpTransport->port();
}

QHostAddress MCPServer::listenAddress() const
{
    return m_httpTransport->listenAddress();
}

void MCPServer::handleRpcPayload(
    const QJsonValue& payload, int* httpStatus, QByteArray* responseBody, QByteArray* contentType)
{
    if (!httpStatus || !responseBody || !contentType) {
        return;
    }

    *httpStatus = 200;
    *responseBody = QByteArray();
    *contentType = "application/json; charset=utf-8";

    if (payload.isArray()) {
        const QJsonArray requestBatch = payload.toArray();
        QJsonArray responseBatch;
        for (const QJsonValue& item : requestBatch) {
            if (!item.isObject()) {
                responseBatch.append(makeErrorResponse(
                    QJsonValue(), -32600, QStringLiteral("Invalid Request: batch item must be object")));
                continue;
            }

            bool hasResponse = false;
            const QJsonObject response = handleRequestObject(item.toObject(), &hasResponse);
            if (hasResponse) {
                responseBatch.append(response);
            }
        }

        if (responseBatch.isEmpty()) {
            *httpStatus = 202;
            *contentType = "text/plain; charset=utf-8";
            return;
        }

        *responseBody = QJsonDocument(responseBatch).toJson(QJsonDocument::Compact);
        return;
    }

    if (!payload.isObject()) {
        *responseBody = QJsonDocument(makeErrorResponse(
                                          QJsonValue(), -32600, QStringLiteral("Invalid Request")))
                            .toJson(QJsonDocument::Compact);
        return;
    }

    bool hasResponse = false;
    const QJsonObject response = handleRequestObject(payload.toObject(), &hasResponse);
    if (!hasResponse) {
        *httpStatus = 202;
        *contentType = "text/plain; charset=utf-8";
        return;
    }

    *responseBody = QJsonDocument(response).toJson(QJsonDocument::Compact);
}

QJsonObject MCPServer::handleRequestObject(const QJsonObject& request, bool* hasResponse) const
{
    if (!hasResponse) {
        return makeErrorResponse(QJsonValue(), -32603, QStringLiteral("Internal server error"));
    }

    const bool hasId = request.contains("id");
    const QJsonValue id = hasId ? request.value("id") : QJsonValue();
    *hasResponse = hasId;

    const QString jsonrpc = request.value("jsonrpc").toString();
    if (jsonrpc != kJsonRpcVersion) {
        *hasResponse = true;
        return makeErrorResponse(id, -32600, QStringLiteral("Invalid Request: jsonrpc must be '2.0'"));
    }

    const QJsonValue methodValue = request.value("method");
    if (!methodValue.isString()) {
        *hasResponse = true;
        return makeErrorResponse(id, -32600, QStringLiteral("Invalid Request: missing method"));
    }
    const QString method = methodValue.toString();

    if (method == "notifications/initialized") {
        *hasResponse = hasId;
        return makeSuccessResponse(id, QJsonObject{});
    }

    if (method == "initialize") {
        QJsonObject result;
        result["protocolVersion"] = kMcpProtocolVersion;
        result["capabilities"] = QJsonObject{{"tools", QJsonObject{}}};
        result["serverInfo"] = QJsonObject{
            {"name", QStringLiteral("snaptray")},
            {"version", QStringLiteral(SNAPTRAY_VERSION)},
        };
        return makeSuccessResponse(id, result);
    }

    if (method == "ping") {
        return makeSuccessResponse(id, QJsonObject{});
    }

    if (method == "tools/list") {
        return makeSuccessResponse(id, QJsonObject{{"tools", buildToolDefinitions()}});
    }

    if (method == "tools/call") {
        const QJsonValue paramsValue = request.value("params");
        if (!paramsValue.isObject()) {
            return makeErrorResponse(id, -32602, QStringLiteral("Invalid params: params must be object"));
        }
        const QJsonObject params = paramsValue.toObject();

        const QString toolName = params.value("name").toString();
        if (toolName.isEmpty()) {
            return makeErrorResponse(id, -32602, QStringLiteral("Invalid params: missing tool name"));
        }

        QJsonObject arguments;
        if (params.contains("arguments")) {
            const QJsonValue argumentsValue = params.value("arguments");
            if (!argumentsValue.isObject()) {
                return makeErrorResponse(
                    id, -32602, QStringLiteral("Invalid params: arguments must be object"));
            }
            arguments = argumentsValue.toObject();
        }

        const ToolCallResult callResult = callTool(toolName, arguments, m_context);
        if (!callResult.success) {
            return makeErrorResponse(id, -32000, callResult.errorMessage);
        }

        const QString outputText =
            QString::fromUtf8(QJsonDocument(callResult.output).toJson(QJsonDocument::Compact));
        QJsonObject result;
        result["structuredContent"] = callResult.output;
        result["content"] = QJsonArray{makeTextContent(outputText)};
        result["isError"] = false;
        return makeSuccessResponse(id, result);
    }

    return makeErrorResponse(
        id, -32601, QStringLiteral("Method not found: %1").arg(method));
}

QJsonObject MCPServer::makeSuccessResponse(const QJsonValue& id, const QJsonObject& result)
{
    return QJsonObject{
        {"jsonrpc", kJsonRpcVersion},
        {"id", id},
        {"result", result},
    };
}

QJsonObject MCPServer::makeErrorResponse(
    const QJsonValue& id, int code, const QString& message, const QJsonValue& data)
{
    QJsonObject error{
        {"code", code},
        {"message", message},
    };
    if (!data.isUndefined() && !data.isNull()) {
        error["data"] = data;
    }

    return QJsonObject{
        {"jsonrpc", kJsonRpcVersion},
        {"id", id},
        {"error", error},
    };
}

} // namespace MCP
} // namespace SnapTray
