#include <QtTest>
#include "video/NativeEditExport.h"

class PacketReader : public SnapTray::NativePcmReader {
public:
    QList<QPair<qint64, QByteArray>> packets;
    bool failAtEnd = false;
private:
    bool nextPacket(QByteArray& pcm, qint64& frame) override {
        if (packets.isEmpty()) {
            if (failAtEnd) m_error = QStringLiteral("Decoder failed");
            return false;
        }
        const auto packet = packets.takeFirst();
        frame = packet.first; pcm = packet.second;
        return true;
    }
};
class TestNativePcmReader : public QObject {
    Q_OBJECT
private slots:
    void preservesSampleClockAcrossGapsAndPacketEdges() {
        PacketReader reader;
        reader.packets = {{-2, QByteArray(4 * 4, '\1')}, {4, QByteArray(4 * 4, '\2')}};
        QCOMPARE(reader.read(0, 5), QByteArray(2 * 4, '\1') + QByteArray(2 * 4, '\0') + QByteArray(4, '\2'));
        QCOMPARE(reader.read(5, 5), QByteArray(3 * 4, '\2') + QByteArray(2 * 4, '\0'));
        QCOMPARE(reader.read(10, 2), QByteArray(2 * 4, '\0'));
    }
    void seekSkipsDeletedSourceAudio() {
        PacketReader reader;
        reader.packets = {{0, QByteArray(4 * 4, '\1')}, {10, QByteArray(4 * 4, '\2')}, {20, QByteArray(4 * 4, '\3')}};
        QCOMPARE(reader.read(1, 2), QByteArray(2 * 4, '\1'));
        QCOMPARE(reader.read(21, 2), QByteArray(2 * 4, '\3'));
    }
    void decoderFailuresAreNotSilence() {
        PacketReader reader;
        reader.failAtEnd = true;
        QVERIFY(reader.read(0, 4).isEmpty());
        PacketReader malformed;
        malformed.packets = {{0, QByteArray(3, '\1')}};
        QVERIFY(malformed.read(0, 1).isEmpty());
    }
};
QTEST_GUILESS_MAIN(TestNativePcmReader)
#include "tst_NativePcmReader.moc"
