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

    QPixmap backgroundPixmap(QSize(24, 16));
    backgroundPixmap.fill(Qt::red);
    const QVector<MultiRegionManager::Region> captureRegions = {
        MultiRegionManager::Region{QRect(5, 6, 7, 8), QColor(Qt::green), 1, true}
    };
    const QByteArray annotationsJson = QByteArrayLiteral("{\"annotations\":[]}");
    QImage resultImage(QSize(9, 7), QImage::Format_ARGB32_Premultiplied);
    resultImage.fill(Qt::blue);
    const QRect selectionRect(1, 2, 3, 4);
    const QSize canvasLogicalSize(12, 8);
    const qreal devicePixelRatio = 2.0;
    const int cornerRadius = 9;
    const int maxEntries = 13;
    const QDateTime createdAt = QDateTime::currentDateTimeUtc();

    const SnapTray::CaptureSessionWriteRequest request =
        RegionSelectorTestAccess::buildCaptureSessionWriteRequest(
            selector,
            backgroundPixmap,
            resultImage,
            selectionRect,
            captureRegions,
            annotationsJson,
            devicePixelRatio,
            canvasLogicalSize,
            cornerRadius,
            maxEntries,
            createdAt);

    QCOMPARE(request.canvasImage, backgroundPixmap.toImage());
    QCOMPARE(request.resultImage, resultImage);
    QCOMPARE(request.selectionRect, selectionRect);
    QCOMPARE(request.captureRegions.size(), captureRegions.size());
    QCOMPARE(request.captureRegions.first().rect, captureRegions.first().rect);
    QCOMPARE(request.captureRegions.first().color, captureRegions.first().color);
    QCOMPARE(request.captureRegions.first().index, captureRegions.first().index);
    QCOMPARE(request.captureRegions.first().isActive, captureRegions.first().isActive);
    QCOMPARE(request.annotationsJson, annotationsJson);
    QCOMPARE(request.devicePixelRatio, devicePixelRatio);
    QCOMPARE(request.canvasLogicalSize, canvasLogicalSize);
    QCOMPARE(request.cornerRadius, cornerRadius);
    QCOMPARE(request.maxEntries, maxEntries);
    QCOMPARE(request.createdAt, createdAt);
}

void tst_RegionSelectorHistoryReplay::testRestoreLiveReplaySlotRecordsCaptureContext()
{
    RegionSelector selector;
    RegionSelectorTestAccess::TraceProbe probe;
    RegionSelectorTestAccess::attachTraceProbe(selector, &probe);

    QScreen* screen = QGuiApplication::primaryScreen();
    QVERIFY(screen);

    QPixmap initialCapture(QSize(100, 80));
    initialCapture.fill(Qt::black);
    selector.initializeForScreen(screen, initialCapture);

    QPixmap replayCapture(QSize(180, 120));
    replayCapture.fill(Qt::darkMagenta);
    replayCapture.setDevicePixelRatio(2.0);

    RegionSelectorTestAccess::setHistoryLiveSlot(
        selector,
        replayCapture,
        2.0,
        QSize(90, 60),
        QRect(10, 10, 40, 30),
        5);

    RegionSelectorTestAccess::invokeRestoreLiveReplaySlot(selector);

    QVERIFY(!probe.captureContextEvents.isEmpty());
    const auto& record = probe.captureContextEvents.constLast();
    QCOMPARE(record.backgroundPixelSize, replayCapture.size());
    QCOMPARE(record.backgroundLogicalSize, QSize(90, 60));
    QCOMPARE(record.devicePixelRatio, 2.0);
    QVERIFY(record.hasSourceScreen);
    QCOMPARE(RegionSelectorTestAccess::backgroundPixelSize(selector), replayCapture.size());
    QCOMPARE(RegionSelectorTestAccess::devicePixelRatio(selector), 2.0);
    QCOMPARE(RegionSelectorTestAccess::selectionRect(selector), QRect(10, 10, 40, 30));
}

void tst_RegionSelectorHistoryReplay::testApplyHistoryReplayEntryRecordsCaptureContext()
{
    RegionSelector selector;
    RegionSelectorTestAccess::TraceProbe probe;
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

    QVERIFY(RegionSelectorTestAccess::invokeApplyHistoryReplayEntry(selector, entry));

    QVERIFY(!probe.captureContextEvents.isEmpty());
    const auto& record = probe.captureContextEvents.constLast();
    QCOMPARE(record.backgroundPixelSize, replayPixmap.size());
    QCOMPARE(record.backgroundLogicalSize, QSize(80, 50));
    QCOMPARE(record.devicePixelRatio, 2.0);
    QVERIFY(record.hasSourceScreen);
    QCOMPARE(RegionSelectorTestAccess::devicePixelRatio(selector), 2.0);
    QCOMPARE(RegionSelectorTestAccess::selectionRect(selector), QRect(5, 6, 30, 20));
}

QTEST_MAIN(tst_RegionSelectorHistoryReplay)
#include "tst_HistoryReplay.moc"
