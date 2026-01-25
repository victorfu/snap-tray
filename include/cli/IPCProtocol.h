#ifndef IPC_PROTOCOL_H
#define IPC_PROTOCOL_H

#include <QByteArray>
#include <QJsonObject>
#include <QString>

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
    QByteArray data;

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
     * @return Response from main instance
     */
    IPCResponse sendCommand(const IPCMessage& message, bool waitResponse = false);

    /**
     * @brief Get server name (consistent with SingleInstanceGuard)
     */
    static QString getServerName();

private:
    static constexpr int kConnectionTimeout = 1000;  // ms
    static constexpr int kResponseTimeout = 30000;   // ms
};

} // namespace CLI
} // namespace SnapTray

#endif // IPC_PROTOCOL_H
