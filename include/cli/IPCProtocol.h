#ifndef IPC_PROTOCOL_H
#define IPC_PROTOCOL_H

#include <QByteArray>
#include <QJsonObject>
#include <QString>

class QLocalSocket;

namespace SnapTray {
namespace CLI {

/**
 * @brief IPC message structure
 */
struct IPCMessage
{
    QString command;
    QJsonObject options;

    QByteArray toJson() const;
    static IPCMessage fromJson(const QByteArray& data);
};

/**
 * @brief IPC response structure
 */
struct IPCResponse
{
    bool success = false;
    QString message;
    QString error;
    // CLIResult::Code the CLI should exit with; kUnsetCode lets the CLI pick
    // its default (Success or GeneralError from `success`).
    int code = kUnsetCode;
    // Command specific structured result (for example the selected region).
    QJsonObject result;
    // Binary payload (for example PNG bytes); base64 encoded on the wire.
    QByteArray data;

    static constexpr int kUnsetCode = -1;

    QByteArray toJson() const;
    static IPCResponse fromJson(const QByteArray& data);
};

/**
 * @brief IPC protocol handler for CLI communication
 */
class IPCProtocol
{
public:
    IPCProtocol();
    ~IPCProtocol();

    /**
     * @brief Check if main instance is running
     */
    bool isMainInstanceRunning() const;

    /**
     * @brief Send command to main instance
     * @param message Command message
     * @param waitResponse Whether to wait for response
     * @param responseTimeoutMs How long to wait for the response; kWaitIndefinitely
     *        waits until the main instance answers or the connection drops
     * @return Response from main instance
     */
    IPCResponse sendCommand(const IPCMessage& message,
                            bool waitResponse = false,
                            int responseTimeoutMs = kResponseTimeout);

    /**
     * @brief Write a length-prefixed response to a connected CLI client
     * @return false when the socket is gone or the response is too large
     */
    static bool writeResponse(QLocalSocket* socket, const IPCResponse& response);

    /**
     * @brief Get server name (consistent with SingleInstanceGuard)
     */
    static QString getServerName();

    static constexpr int kConnectionTimeout = 1000;  // ms
    static constexpr int kResponseTimeout = 30000;   // ms
    static constexpr int kWaitIndefinitely = -1;
    // Responses may carry a whole PNG capture (base64), so allow large bodies.
    static constexpr quint32 kMaxResponseSize = 512u * 1024u * 1024u;
};

} // namespace CLI
} // namespace SnapTray

#endif // IPC_PROTOCOL_H
