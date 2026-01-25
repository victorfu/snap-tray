#ifndef SINGLEINSTANCEGUARD_H
#define SINGLEINSTANCEGUARD_H

#include <QByteArray>
#include <QObject>

class QLocalServer;

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
    QString m_serverName;
    QLocalServer *m_server = nullptr;
};

#endif // SINGLEINSTANCEGUARD_H
