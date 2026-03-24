#include <QtTest/QtTest>

#include <QGuiApplication>
#include <QTemporaryDir>

#include "RegionSelector.h"
#include "RegionSelectorTestAccess.h"

class tst_RegionSelectorHistoryReplay : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void testBuildCaptureSessionWriteRequestPreservesFields();
    void testRestoreLiveReplaySlotRecordsCaptureContext();
    void testApplyHistoryReplayEntryRecordsCaptureContext();
};

void tst_RegionSelectorHistoryReplay::initTestCase()
{
    if (QGuiApplication::screens().isEmpty()) {
        QSKIP("No screens available for RegionSelector history replay tests.");
    }
}

void tst_RegionSelectorHistoryReplay::testBuildCaptureSessionWriteRequestPreservesFields()
{
    RegionSelector selector;

    RegionSelector::PendingHistorySubmission submission;
    submission.snapshot.backgroundPixmap = QPixmap(QSize(24, 16));
    submission.snapshot.backgroundPixmap.fill(Qt::red);
    submission.snapshot.selectionRect = QRect(1, 2, 3, 4);
    submission.snapshot.captureRegions = {
        MultiRegionManager::Region{QRect(5, 6, 7, 8), QColor(Qt::green), 1, true}
    };
    submission.snapshot.annotationsJson = QByteArrayLiteral("{\"annotations\":[]}");
    submission.snapshot.devicePixelRatio = 2.0;
    submission.snapshot.canvasLogicalSize = QSize(12, 8);
    submission.snapshot.cornerRadius = 9;
    submission.snapshot.maxEntries = 13;
    submission.snapshot.createdAt = QDateTime::currentDateTimeUtc();
    submission.resultImage = QImage(QSize(9, 7), QImage::Format_ARGB32_Premultiplied);
    submission.resultImage.fill(Qt::blue);

    const SnapTray::CaptureSessionWriteRequest request =
        selector.buildCaptureSessionWriteRequest(submission);

    QCOMPARE(request.canvasImage, submission.snapshot.backgroundPixmap.toImage());
    QCOMPARE(request.resultImage, submission.resultImage);
    QCOMPARE(request.selectionRect, submission.snapshot.selectionRect);
    QCOMPARE(request.captureRegions.size(), submission.snapshot.captureRegions.size());
    QCOMPARE(request.captureRegions.first().rect, submission.snapshot.captureRegions.first().rect);
    QCOMPARE(request.captureRegions.first().color, submission.snapshot.captureRegions.first().color);
    QCOMPARE(request.captureRegions.first().index, submission.snapshot.captureRegions.first().index);
    QCOMPARE(request.captureRegions.first().isActive, submission.snapshot.captureRegions.first().isActive);
    QCOMPARE(request.annotationsJson, submission.snapshot.annotationsJson);
    QCOMPARE(request.devicePixelRatio, submission.snapshot.devicePixelRatio);
    QCOMPARE(request.canvasLogicalSize, submission.snapshot.canvasLogicalSize);
    QCOMPARE(request.cornerRadius, submission.snapshot.cornerRadius);
    QCOMPARE(request.maxEntries, submission.snapshot.maxEntries);
    QCOMPARE(request.createdAt, submission.snapshot.createdAt);
}

void tst_RegionSelectorHistoryReplay::testRestoreLiveReplaySlotRecordsCaptureContext()
{
    RegionSelector selector;
    RegionSelector::RegionSelectorTraceProbe probe;
    RegionSelectorTestAccess::attachTraceProbe(selector, &probe);

    QScreen* screen = QGuiApplication::primaryScreen();
    QVERIFY(screen);

    QPixmap initialCapture(QSize(100, 80));
    initialCapture.fill(Qt::black);
    selector.initializeForScreen(screen, initialCapture);

    QPixmap replayCapture(QSize(180, 120));
    replayCapture.fill(Qt::darkMagenta);
    replayCapture.setDevicePixelRatio(2.0);

    selector.m_historyLiveSlot.valid = true;
    selector.m_historyLiveSlot.canvasLogicalSize = QSize(90, 60);
    selector.m_historyLiveSlot.backgroundPixmap = replayCapture;
    selector.m_historyLiveSlot.devicePixelRatio = 2.0;
    selector.m_historyLiveSlot.selectionRect = QRect(10, 10, 40, 30);
    selector.m_historyLiveSlot.cornerRadius = 5;

    selector.restoreLiveReplaySlot();

    QVERIFY(!probe.captureContextEvents.isEmpty());
    const auto& record = probe.captureContextEvents.constLast();
    QCOMPARE(record.backgroundPixelSize, replayCapture.size());
    QCOMPARE(record.backgroundLogicalSize, QSize(90, 60));
    QCOMPARE(record.devicePixelRatio, 2.0);
    QVERIFY(record.hasSourceScreen);
    QCOMPARE(selector.m_backgroundPixmap.size(), replayCapture.size());
    QCOMPARE(selector.m_devicePixelRatio, 2.0);
    QCOMPARE(selector.m_selectionManager->selectionRect(), QRect(10, 10, 40, 30));
}

void tst_RegionSelectorHistoryReplay::testApplyHistoryReplayEntryRecordsCaptureContext()
{
    RegionSelector selector;
    RegionSelector::RegionSelectorTraceProbe probe;
    RegionSelectorTestAccess::attachTraceProbe(selector, &probe);

    QScreen* screen = QGuiApplication::primaryScreen();
    QVERIFY(screen);

    QPixmap initialCapture(QSize(120, 90));
    initialCapture.fill(Qt::gray);
    selector.initializeForScreen(screen, initialCapture);

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    QPixmap replayPixmap(QSize(160, 100));
    replayPixmap.fill(Qt::darkYellow);
    const QString canvasPath = tempDir.filePath(QStringLiteral("canvas.png"));
    QVERIFY(replayPixmap.save(canvasPath));

    SnapTray::HistoryEntry entry;
    entry.id = QStringLiteral("test-entry");
    entry.replayAvailable = true;
    entry.canvasPath = canvasPath;
    entry.devicePixelRatio = 2.0;
    entry.canvasLogicalSize = QSize(80, 50);
    entry.selectionRect = QRect(5, 6, 30, 20);
    entry.cornerRadius = 3;

    QVERIFY(selector.applyHistoryReplayEntry(entry));

    QVERIFY(!probe.captureContextEvents.isEmpty());
    const auto& record = probe.captureContextEvents.constLast();
    QCOMPARE(record.backgroundPixelSize, replayPixmap.size());
    QCOMPARE(record.backgroundLogicalSize, QSize(80, 50));
    QCOMPARE(record.devicePixelRatio, 2.0);
    QVERIFY(record.hasSourceScreen);
    QCOMPARE(selector.m_devicePixelRatio, 2.0);
    QCOMPARE(selector.m_selectionManager->selectionRect(), QRect(5, 6, 30, 20));
}

QTEST_MAIN(tst_RegionSelectorHistoryReplay)
#include "tst_HistoryReplay.moc"
