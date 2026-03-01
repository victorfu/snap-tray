#ifndef SINGLEINSTANCEGUARD_H
#define SINGLEINSTANCEGUARD_H

#include <QByteArray>
#include <QObject>
#include <memory>

class QLocalServer;
class QLockFile;

class SingleInstanceGuard : public QObject
{
    Q_OBJECT
public:
    explicit SingleInstanceGuard(const QString &appId, QObject *parent = nullptr);
    ~SingleInstanceGuard();

    bool tryLock();              // Returns true if this is the primary instance
    void sendActivateMessage();  // Notify existing instance

signals:
    void activateRequested();
    void commandReceived(const QByteArray& commandData);

private slots:
    void onNewConnection();

private:
    enum class ServerStartResult {
        Listening,
        ActivePrimaryDetected,
        Unavailable
    };

    bool acquireInstanceLock();
    void releaseInstanceLock();
    ServerStartResult startServerWithSafeRecovery();

    QString m_serverName;
    QString m_lockFilePath;
    QLocalServer *m_server = nullptr;
    std::unique_ptr<QLockFile> m_instanceLock;
};

#endif // SINGLEINSTANCEGUARD_H
