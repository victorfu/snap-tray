#include <QtTest>

#include <QDataStream>
#include <QLocalServer>
#include <QLocalSocket>

#include <chrono>
#include <future>
#include <memory>

#include "cli/IPCProtocol.h"

using namespace SnapTray::CLI;

namespace {

constexpr int kSocketTimeoutMs = 1000;
constexpr int kFutureTimeoutMs = 5000;

QByteArray buildLengthPrefixedPacket(const QByteArray& payload)
{
    QByteArray packet;
    QDataStream stream(&packet, QIODevice::WriteOnly);
    stream << static_cast<quint32>(payload.size());
    packet.append(payload);
    return packet;
}

std::future<IPCResponse> sendCommandAsync()
{
    return std::async(std::launch::async, []() {
        IPCProtocol protocol;
        IPCMessage message;
        message.command = "gui";
        return protocol.sendCommand(message, true);
    });
}

QLocalSocket* waitForClient(QLocalServer& server)
{
    if (!server.waitForNewConnection(kSocketTimeoutMs)) {
        return nullptr;
    }
    return server.nextPendingConnection();
}

bool writeChunk(QLocalSocket* socket, const QByteArray& chunk)
{
    if (!socket || chunk.isEmpty()) {
        return socket != nullptr;
    }

    qint64 writtenTotal = 0;
    while (writtenTotal < chunk.size()) {
        const qint64 written =
            socket->write(chunk.constData() + writtenTotal, chunk.size() - writtenTotal);
        if (written <= 0) {
            return false;
        }

        writtenTotal += written;
        if (!socket->waitForBytesWritten(kSocketTimeoutMs)) {
            return false;
        }
    }

    return true;
}

void disconnectClient(QLocalSocket* client)
{
    if (!client) {
        return;
    }
    client->disconnectFromServer();
    client->waitForDisconnected(kSocketTimeoutMs);
}

bool isServerOccupied(const QString& serverName)
{
    QLocalSocket probe;
    probe.connectToServer(serverName);
    const bool connected = probe.waitForConnected(100);
    if (connected) {
        probe.disconnectFromServer();
        probe.waitForDisconnected(100);
    }
    return connected;
}

void removeServerIfNotOccupied(const QString& serverName)
{
    if (!isServerOccupied(serverName)) {
        QLocalServer::removeServer(serverName);
    }
}

} // namespace

class tst_IPCProtocol : public QObject
{
    Q_OBJECT

private slots:
    void cleanup();
    void testResponseHeaderSplit_1Plus3Bytes();
    void testResponseBodySplit_MultiChunks();
    void testResponseHeaderIncomplete_Disconnect();
    void testResponseBodyIncomplete_Disconnect();

private:
    bool tryPrepareServer(QLocalServer& server, QString& skipReason);
};

void tst_IPCProtocol::cleanup() { removeServerIfNotOccupied(IPCProtocol::getServerName()); }

bool tst_IPCProtocol::tryPrepareServer(QLocalServer& server, QString& skipReason)
{
    const QString serverName = IPCProtocol::getServerName();
    if (isServerOccupied(serverName)) {
        skipReason = "SnapTray main instance is running; IPCProtocol socket tests are skipped.";
        return false;
    }

    removeServerIfNotOccupied(serverName);
    if (!server.listen(serverName)) {
        skipReason = QString("Unable to bind IPC server (%1); skipping environment-sensitive test.")
                         .arg(server.errorString());
        return false;
    }
    return true;
}

void tst_IPCProtocol::testResponseHeaderSplit_1Plus3Bytes()
{
    QLocalServer server;
    QString skipReason;
    if (!tryPrepareServer(server, skipReason)) {
        QSKIP(qPrintable(skipReason));
    }

    IPCResponse expected;
    expected.success = true;
    expected.message = "header-split-ok";

    auto future = sendCommandAsync();
    std::unique_ptr<QLocalSocket> client(waitForClient(server));
    QVERIFY(client != nullptr);

    const QByteArray packet = buildLengthPrefixedPacket(expected.toJson());
    const int headerSize = static_cast<int>(sizeof(quint32));
    QVERIFY(packet.size() > headerSize);

    QVERIFY(writeChunk(client.get(), packet.left(1)));
    QTest::qWait(40);
    QVERIFY(writeChunk(client.get(), packet.mid(1, headerSize - 1)));
    QTest::qWait(40);
    QVERIFY(writeChunk(client.get(), packet.mid(headerSize)));
    disconnectClient(client.get());

    QVERIFY(future.wait_for(std::chrono::milliseconds(kFutureTimeoutMs)) == std::future_status::ready);
    const IPCResponse actual = future.get();
    QVERIFY(actual.success);
    QCOMPARE(actual.message, expected.message);
    QCOMPARE(actual.error, QString());
}

void tst_IPCProtocol::testResponseBodySplit_MultiChunks()
{
    QLocalServer server;
    QString skipReason;
    if (!tryPrepareServer(server, skipReason)) {
        QSKIP(qPrintable(skipReason));
    }

    IPCResponse expected;
    expected.success = true;
    expected.message = "body-split-ok-with-longer-message";

    auto future = sendCommandAsync();
    std::unique_ptr<QLocalSocket> client(waitForClient(server));
    QVERIFY(client != nullptr);

    const QByteArray packet = buildLengthPrefixedPacket(expected.toJson());
    const int headerSize = static_cast<int>(sizeof(quint32));
    const QByteArray header = packet.left(headerSize);
    const QByteArray body = packet.mid(headerSize);
    QVERIFY(body.size() > 6);

    QVERIFY(writeChunk(client.get(), header));
    QVERIFY(writeChunk(client.get(), body.left(2)));
    QTest::qWait(30);
    QVERIFY(writeChunk(client.get(), body.mid(2, 3)));
    QTest::qWait(30);
    QVERIFY(writeChunk(client.get(), body.mid(5)));
    disconnectClient(client.get());

    QVERIFY(future.wait_for(std::chrono::milliseconds(kFutureTimeoutMs)) == std::future_status::ready);
    const IPCResponse actual = future.get();
    QVERIFY(actual.success);
    QCOMPARE(actual.message, expected.message);
    QCOMPARE(actual.error, QString());
}

void tst_IPCProtocol::testResponseHeaderIncomplete_Disconnect()
{
    QLocalServer server;
    QString skipReason;
    if (!tryPrepareServer(server, skipReason)) {
        QSKIP(qPrintable(skipReason));
    }

    IPCResponse expected;
    expected.success = true;
    expected.message = "incomplete-header";

    auto future = sendCommandAsync();
    std::unique_ptr<QLocalSocket> client(waitForClient(server));
    QVERIFY(client != nullptr);

    const QByteArray packet = buildLengthPrefixedPacket(expected.toJson());
    QVERIFY(packet.size() >= 2);

    QVERIFY(writeChunk(client.get(), packet.left(2)));
    disconnectClient(client.get());

    QVERIFY(future.wait_for(std::chrono::milliseconds(kFutureTimeoutMs)) == std::future_status::ready);
    const IPCResponse actual = future.get();
    QVERIFY(!actual.success);
    QCOMPARE(actual.error, QString("Timeout waiting for response"));
}

void tst_IPCProtocol::testResponseBodyIncomplete_Disconnect()
{
    QLocalServer server;
    QString skipReason;
    if (!tryPrepareServer(server, skipReason)) {
        QSKIP(qPrintable(skipReason));
    }

    IPCResponse expected;
    expected.success = true;
    expected.message = "incomplete-body";

    auto future = sendCommandAsync();
    std::unique_ptr<QLocalSocket> client(waitForClient(server));
    QVERIFY(client != nullptr);

    const QByteArray packet = buildLengthPrefixedPacket(expected.toJson());
    const int headerSize = static_cast<int>(sizeof(quint32));
    const QByteArray header = packet.left(headerSize);
    const QByteArray body = packet.mid(headerSize);
    QVERIFY(!body.isEmpty());

    QVERIFY(writeChunk(client.get(), header));
    QVERIFY(writeChunk(client.get(), body.left(body.size() / 2)));
    disconnectClient(client.get());

    QVERIFY(future.wait_for(std::chrono::milliseconds(kFutureTimeoutMs)) == std::future_status::ready);
    const IPCResponse actual = future.get();
    QVERIFY(!actual.success);
    QCOMPARE(actual.error, QString("Timeout waiting for response"));
}

QTEST_MAIN(tst_IPCProtocol)
#include "tst_IPCProtocol.moc"
