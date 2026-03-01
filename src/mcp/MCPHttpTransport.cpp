#include "mcp/MCPHttpTransport.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>

namespace SnapTray {
namespace MCP {

namespace {

constexpr const char* kMcpPath = "/mcp";
constexpr int kMaxRequestBytes = 1024 * 1024; // 1 MB

QByteArray toLowerAscii(const QByteArray& input)
{
    QByteArray lowered = input;
    lowered = lowered.toLower();
    return lowered;
}

} // namespace

MCPHttpTransport::MCPHttpTransport(QObject* parent)
    : QObject(parent)
    , m_server(new QTcpServer(this))
{
    connect(m_server, &QTcpServer::newConnection, this, &MCPHttpTransport::onNewConnection);
}

MCPHttpTransport::~MCPHttpTransport()
{
    stop();
}

bool MCPHttpTransport::start(quint16 port, QString* errorMessage)
{
    stop();

    if (!m_server->listen(QHostAddress::LocalHost, port)) {
        if (errorMessage) {
            *errorMessage = m_server->errorString();
        }
        return false;
    }

    return true;
}

void MCPHttpTransport::stop()
{
    for (auto it = m_pendingRequests.begin(); it != m_pendingRequests.end(); ++it) {
        if (it.key()) {
            it.key()->disconnect(this);
            it.key()->disconnectFromHost();
            it.key()->deleteLater();
        }
    }
    m_pendingRequests.clear();

    if (m_server->isListening()) {
        m_server->close();
    }
}

bool MCPHttpTransport::isListening() const
{
    return m_server->isListening();
}

quint16 MCPHttpTransport::port() const
{
    return m_server->serverPort();
}

QHostAddress MCPHttpTransport::listenAddress() const
{
    return m_server->serverAddress();
}

void MCPHttpTransport::setRpcHandler(RpcHandler handler)
{
    m_rpcHandler = std::move(handler);
}

void MCPHttpTransport::onNewConnection()
{
    while (QTcpSocket* socket = m_server->nextPendingConnection()) {
        m_pendingRequests.insert(socket, PendingRequest{});
        connect(socket, &QTcpSocket::readyRead, this, &MCPHttpTransport::onSocketReadyRead);
        connect(socket, &QTcpSocket::disconnected, this, &MCPHttpTransport::onSocketDisconnected);
    }
}

void MCPHttpTransport::onSocketReadyRead()
{
    auto* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) {
        return;
    }

    auto it = m_pendingRequests.find(socket);
    if (it == m_pendingRequests.end()) {
        return;
    }

    it->buffer.append(socket->readAll());
    if (it->buffer.size() > kMaxRequestBytes) {
        sendResponse(socket, 413, "text/plain; charset=utf-8", "Request too large");
        return;
    }

    processSocketBuffer(socket);
}

void MCPHttpTransport::onSocketDisconnected()
{
    auto* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) {
        return;
    }

    m_pendingRequests.remove(socket);
    socket->deleteLater();
}

bool MCPHttpTransport::parseHeaders(
    PendingRequest& req, int* headerEndPos, QString* errorMessage) const
{
    if (!headerEndPos) {
        return false;
    }

    *headerEndPos = req.buffer.indexOf("\r\n\r\n");
    if (*headerEndPos < 0) {
        return false;
    }

    const QByteArray headersBlock = req.buffer.left(*headerEndPos);
    const QList<QByteArray> lines = headersBlock.split('\n');
    if (lines.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Missing request line");
        }
        return false;
    }

    const QList<QByteArray> requestLineParts = lines.first().trimmed().split(' ');
    if (requestLineParts.size() < 2) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Malformed request line");
        }
        return false;
    }

    req.method = requestLineParts.at(0).trimmed().toUpper();
    req.path = requestLineParts.at(1).trimmed();
    req.contentLength = 0;

    for (int i = 1; i < lines.size(); ++i) {
        const QByteArray trimmedLine = lines.at(i).trimmed();
        if (trimmedLine.isEmpty()) {
            continue;
        }

        const int colonPos = trimmedLine.indexOf(':');
        if (colonPos <= 0) {
            continue;
        }

        const QByteArray key = toLowerAscii(trimmedLine.left(colonPos).trimmed());
        const QByteArray value = trimmedLine.mid(colonPos + 1).trimmed();

        if (key == "content-length") {
            bool ok = false;
            const int contentLength = value.toInt(&ok);
            if (!ok || contentLength < 0) {
                if (errorMessage) {
                    *errorMessage = QStringLiteral("Invalid Content-Length");
                }
                return false;
            }
            req.contentLength = contentLength;
        }
    }

    req.headersParsed = true;
    return true;
}

void MCPHttpTransport::processSocketBuffer(QTcpSocket* socket)
{
    auto it = m_pendingRequests.find(socket);
    if (it == m_pendingRequests.end()) {
        return;
    }

    PendingRequest& req = it.value();
    int headerEndPos = -1;

    if (!req.headersParsed) {
        QString parseError;
        const bool parsed = parseHeaders(req, &headerEndPos, &parseError);
        if (!parsed) {
            if (headerEndPos >= 0) {
                sendResponse(socket, 400, "text/plain; charset=utf-8", parseError.toUtf8());
            }
            return;
        }

        req.buffer.remove(0, headerEndPos + 4);
    }

    if (req.path != kMcpPath) {
        sendResponse(socket, 404, "text/plain; charset=utf-8", "Not Found");
        return;
    }

    if (req.method == "GET") {
        sendResponse(socket, 405, "text/plain; charset=utf-8", "Method Not Allowed");
        return;
    }

    if (req.method != "POST") {
        sendResponse(socket, 405, "text/plain; charset=utf-8", "Method Not Allowed");
        return;
    }

    if (req.contentLength < 0) {
        sendResponse(socket, 411, "text/plain; charset=utf-8", "Length Required");
        return;
    }

    if (req.buffer.size() < req.contentLength) {
        return;
    }

    const QByteArray payload = req.buffer.left(req.contentLength);
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        sendResponse(socket, 400, "text/plain; charset=utf-8", "Invalid JSON payload");
        return;
    }

    if (!m_rpcHandler) {
        sendResponse(socket, 503, "text/plain; charset=utf-8", "MCP handler is unavailable");
        return;
    }

    int statusCode = 200;
    QByteArray responseBody;
    QByteArray responseType = "application/json; charset=utf-8";
    m_rpcHandler(doc.isArray() ? QJsonValue(doc.array()) : QJsonValue(doc.object()),
        &statusCode,
        &responseBody,
        &responseType);

    sendResponse(socket, statusCode, responseType, responseBody);
}

void MCPHttpTransport::sendResponse(
    QTcpSocket* socket, int statusCode, const QByteArray& contentType, const QByteArray& body)
{
    if (!socket) {
        return;
    }

    QByteArray response;
    response.reserve(256 + body.size());
    response.append("HTTP/1.1 ");
    response.append(QByteArray::number(statusCode));
    response.append(" ");
    response.append(reasonPhrase(statusCode));
    response.append("\r\n");
    if (!body.isEmpty()) {
        response.append("Content-Type: ");
        response.append(contentType.isEmpty() ? "application/json; charset=utf-8" : contentType);
        response.append("\r\n");
    }
    response.append("Content-Length: ");
    response.append(QByteArray::number(body.size()));
    response.append("\r\n");
    response.append("Connection: close\r\n\r\n");
    response.append(body);

    socket->write(response);
    socket->disconnectFromHost();
}

QByteArray MCPHttpTransport::reasonPhrase(int statusCode)
{
    switch (statusCode) {
    case 200:
        return "OK";
    case 202:
        return "Accepted";
    case 400:
        return "Bad Request";
    case 404:
        return "Not Found";
    case 405:
        return "Method Not Allowed";
    case 411:
        return "Length Required";
    case 413:
        return "Payload Too Large";
    case 500:
        return "Internal Server Error";
    case 503:
        return "Service Unavailable";
    default:
        return "Error";
    }
}

} // namespace MCP
} // namespace SnapTray
