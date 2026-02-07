#include <QtTest>

#include <QCryptographicHash>
#include <QDataStream>
#include <QLocalSocket>
#include <QSignalSpy>

#include "SingleInstanceGuard.h"

class tst_SingleInstanceGuard : public QObject
{
    Q_OBJECT

private slots:
    void testLengthPrefixedCommand_HeaderSplit();
    void testLengthPrefixedCommand_BodySplit();
    void testActivateMessage_SplitAcrossWrites();
    void testIncompletePacket_DisconnectDoesNotEmit();

private:
    static QString serverNameForAppId(const QString& appId);
    static QByteArray buildLengthPrefixedPacket(const QByteArray& payload);
    static bool sendChunks(const QString& serverName, const QList<QByteArray>& chunks);
    static QString testAppId(const QString& testSuffix);
};

QString tst_SingleInstanceGuard::serverNameForAppId(const QString& appId)
{
    return QString("snaptray-%1")
        .arg(QString(QCryptographicHash::hash(appId.toUtf8(), QCryptographicHash::Md5).toHex()));
}

QByteArray tst_SingleInstanceGuard::buildLengthPrefixedPacket(const QByteArray& payload)
{
    QByteArray packet;
    QDataStream stream(&packet, QIODevice::WriteOnly);
    stream << static_cast<quint32>(payload.size());
    packet.append(payload);
    return packet;
}

bool tst_SingleInstanceGuard::sendChunks(const QString& serverName, const QList<QByteArray>& chunks)
{
    QLocalSocket socket;
    socket.connectToServer(serverName);
    if (!socket.waitForConnected(1000)) {
        return false;
    }

    for (int i = 0; i < chunks.size(); ++i) {
        socket.write(chunks[i]);
        if (!socket.waitForBytesWritten(1000)) {
            return false;
        }
        if (i + 1 < chunks.size()) {
            QTest::qWait(20);
        }
    }

    socket.disconnectFromServer();
    socket.waitForDisconnected(1000);
    return true;
}

QString tst_SingleInstanceGuard::testAppId(const QString& testSuffix)
{
    return QString("com.victorfu.snaptray.test.%1.%2")
        .arg(QCoreApplication::applicationPid())
        .arg(testSuffix);
}

void tst_SingleInstanceGuard::testLengthPrefixedCommand_HeaderSplit()
{
    const QString appId = testAppId("header-split");
    const QString serverName = serverNameForAppId(appId);
    SingleInstanceGuard guard(appId);
    QVERIFY(guard.tryLock());

    QSignalSpy activateSpy(&guard, &SingleInstanceGuard::activateRequested);
    QSignalSpy commandSpy(&guard, &SingleInstanceGuard::commandReceived);
    QVERIFY(activateSpy.isValid());
    QVERIFY(commandSpy.isValid());

    const QByteArray payload = R"({"command":"region"})";
    const QByteArray packet = buildLengthPrefixedPacket(payload);

    const QList<QByteArray> chunks = {packet.left(2), packet.mid(2)};
    QVERIFY(sendChunks(serverName, chunks));

    QTRY_COMPARE(commandSpy.count(), 1);
    QCOMPARE(activateSpy.count(), 0);
    QCOMPARE(commandSpy.at(0).at(0).toByteArray(), payload);
}

void tst_SingleInstanceGuard::testLengthPrefixedCommand_BodySplit()
{
    const QString appId = testAppId("body-split");
    const QString serverName = serverNameForAppId(appId);
    SingleInstanceGuard guard(appId);
    QVERIFY(guard.tryLock());

    QSignalSpy activateSpy(&guard, &SingleInstanceGuard::activateRequested);
    QSignalSpy commandSpy(&guard, &SingleInstanceGuard::commandReceived);
    QVERIFY(activateSpy.isValid());
    QVERIFY(commandSpy.isValid());

    const QByteArray payload = R"({"command":"canvas","options":{"x":12}})";
    const QByteArray packet = buildLengthPrefixedPacket(payload);

    const int splitPos = static_cast<int>(sizeof(quint32)) + 3;
    const QList<QByteArray> chunks = {packet.left(splitPos), packet.mid(splitPos)};
    QVERIFY(sendChunks(serverName, chunks));

    QTRY_COMPARE(commandSpy.count(), 1);
    QCOMPARE(activateSpy.count(), 0);
    QCOMPARE(commandSpy.at(0).at(0).toByteArray(), payload);
}

void tst_SingleInstanceGuard::testActivateMessage_SplitAcrossWrites()
{
    const QString appId = testAppId("activate-split");
    const QString serverName = serverNameForAppId(appId);
    SingleInstanceGuard guard(appId);
    QVERIFY(guard.tryLock());

    QSignalSpy activateSpy(&guard, &SingleInstanceGuard::activateRequested);
    QSignalSpy commandSpy(&guard, &SingleInstanceGuard::commandReceived);
    QVERIFY(activateSpy.isValid());
    QVERIFY(commandSpy.isValid());

    const QList<QByteArray> chunks = {"act", "ivate"};
    QVERIFY(sendChunks(serverName, chunks));

    QTRY_COMPARE(activateSpy.count(), 1);
    QCOMPARE(commandSpy.count(), 0);
}

void tst_SingleInstanceGuard::testIncompletePacket_DisconnectDoesNotEmit()
{
    const QString appId = testAppId("incomplete");
    const QString serverName = serverNameForAppId(appId);
    SingleInstanceGuard guard(appId);
    QVERIFY(guard.tryLock());

    QSignalSpy activateSpy(&guard, &SingleInstanceGuard::activateRequested);
    QSignalSpy commandSpy(&guard, &SingleInstanceGuard::commandReceived);
    QVERIFY(activateSpy.isValid());
    QVERIFY(commandSpy.isValid());

    const QByteArray payload = R"({"command":"pin"})";
    const QByteArray packet = buildLengthPrefixedPacket(payload);
    const QList<QByteArray> chunks = {packet.left(static_cast<int>(sizeof(quint32)) + 2)};
    QVERIFY(sendChunks(serverName, chunks));

    QTest::qWait(100);
    QCOMPARE(activateSpy.count(), 0);
    QCOMPARE(commandSpy.count(), 0);
}

QTEST_MAIN(tst_SingleInstanceGuard)
#include "tst_SingleInstanceGuard.moc"
