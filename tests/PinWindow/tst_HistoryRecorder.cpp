#include <QtTest/QtTest>

#include "history/HistoryRecorder.h"
#include "history/HistoryStore.h"

#include <QImage>
#include <QTemporaryDir>

namespace {

QImage makeImage(const QSize& size, const QColor& color)
{
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(color);
    return image;
}

SnapTray::CaptureSessionWriteRequest makeRequest(int index, int maxEntries = 20)
{
    SnapTray::CaptureSessionWriteRequest request;
    request.canvasImage = makeImage(QSize(400, 240), QColor(10 + index, 20 + index, 30 + index));
    request.resultImage = makeImage(QSize(160, 100), QColor(40 + index, 50 + index, 60 + index));
    request.selectionRect = QRect(0, 0, 160, 100);
    request.annotationsJson = QByteArrayLiteral("{\"items\":[]}");
    request.canvasLogicalSize = QSize(400, 240);
    request.maxEntries = maxEntries;
    request.createdAt = QDateTime(QDate(2026, 1, 1), QTime(12, 0, index, 0));
    return request;
}

} // namespace

class tst_HistoryRecorder : public QObject
{
    Q_OBJECT

private slots:
    void testSubmitCaptureSession();
    void testSerializedRetention();
};

void tst_HistoryRecorder::testSubmitCaptureSession()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    qputenv("SNAPTRAY_HISTORY_DIR", tempDir.path().toLocal8Bit());

    SnapTray::HistoryRecorder::instance().submitCaptureSession(makeRequest(0));
    QVERIFY(SnapTray::HistoryRecorder::instance().waitForIdleForTests());

    const QList<SnapTray::HistoryEntry> entries = SnapTray::HistoryStore::loadEntries();
    QCOMPARE(entries.size(), 1);
    QVERIFY(entries.first().replayAvailable);

    qunsetenv("SNAPTRAY_HISTORY_DIR");
}

void tst_HistoryRecorder::testSerializedRetention()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    qputenv("SNAPTRAY_HISTORY_DIR", tempDir.path().toLocal8Bit());

    for (int i = 0; i < 3; ++i) {
        SnapTray::HistoryRecorder::instance().submitCaptureSession(makeRequest(i, 2));
    }
    QVERIFY(SnapTray::HistoryRecorder::instance().waitForIdleForTests());

    const QList<SnapTray::HistoryEntry> entries = SnapTray::HistoryStore::loadEntries();
    QCOMPARE(entries.size(), 2);
    QCOMPARE(entries.at(0).createdAt, QDateTime(QDate(2026, 1, 1), QTime(12, 0, 2, 0)));
    QCOMPARE(entries.at(1).createdAt, QDateTime(QDate(2026, 1, 1), QTime(12, 0, 1, 0)));

    qunsetenv("SNAPTRAY_HISTORY_DIR");
}

QTEST_MAIN(tst_HistoryRecorder)
#include "tst_HistoryRecorder.moc"
