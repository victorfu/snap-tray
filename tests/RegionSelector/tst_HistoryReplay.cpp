#include <QtTest/QtTest>

#include <QGuiApplication>
#include <QTemporaryDir>

#include "RegionSelector.h"
#include "RegionSelectorTestAccess.h"
#include "annotations/AnnotationLayer.h"
#include "settings/FileSettingsManager.h"
#include "history/HistoryRecorder.h"
#include "history/AnnotationSerializer.h"
#include "annotations/ShapeAnnotation.h"
#include <QScopeGuard>
#include <QFile>

class tst_RegionSelectorHistoryReplay : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void testBuildCaptureSessionWriteRequestPreservesFields();
    void testRestoreLiveReplaySlotRecordsCaptureContext();
    void testApplyHistoryReplayEntryRecordsCaptureContext();
    void testDetectedWindowMetadataSurvivesHighlightClear();
    void testPreservedSelectionRecordsOriginalHistory_data();
    void testPreservedSelectionRecordsOriginalHistory();
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

    QSignalSpy changedSpy(RegionSelectorTestAccess::annotationLayer(selector),
                          &AnnotationLayer::changed);
    RegionSelectorTestAccess::invokeRestoreLiveReplaySlot(selector);

    QCOMPARE(changedSpy.count(), 1);
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
    entry.windowTitle = QStringLiteral("Stored title");
    entry.ownerApp = QStringLiteral("Stored app");

    QSignalSpy changedSpy(RegionSelectorTestAccess::annotationLayer(selector),
                          &AnnotationLayer::changed);
    QVERIFY(RegionSelectorTestAccess::invokeApplyHistoryReplayEntry(selector, entry));
    const auto request = RegionSelectorTestAccess::currentHistoryRequest(selector);
    QVERIFY(request);
    QCOMPARE(request->windowTitle, entry.windowTitle);
    QCOMPARE(request->ownerApp, entry.ownerApp);

    QCOMPARE(changedSpy.count(), 1);
    QVERIFY(!probe.captureContextEvents.isEmpty());
    const auto& record = probe.captureContextEvents.constLast();
    QCOMPARE(record.backgroundPixelSize, replayPixmap.size());
    QCOMPARE(record.backgroundLogicalSize, QSize(80, 50));
    QCOMPARE(record.devicePixelRatio, 2.0);
    QVERIFY(record.hasSourceScreen);
    QCOMPARE(RegionSelectorTestAccess::devicePixelRatio(selector), 2.0);
    QCOMPARE(RegionSelectorTestAccess::selectionRect(selector), QRect(5, 6, 30, 20));
}

void tst_RegionSelectorHistoryReplay::testDetectedWindowMetadataSurvivesHighlightClear()
{
    RegionSelector selector;
    QScreen* screen = QGuiApplication::primaryScreen();
    QVERIFY(screen);
    QPixmap capture(400, 300);
    capture.fill(Qt::white);
    selector.initializeForScreen(screen, capture);
    const QRect window(40, 50, 180, 120);
    RegionSelectorTestAccess::seedDetectedWindow(selector, window, "Document title", "Editor");
    RegionSelectorTestAccess::dispatchMousePress(selector, window.center());
    RegionSelectorTestAccess::dispatchMouseRelease(selector, window.center());
    QCOMPARE(RegionSelectorTestAccess::selectionRect(selector), window);
    auto request = RegionSelectorTestAccess::currentHistoryRequest(selector);
    QVERIFY(request);
    QCOMPARE(request->windowTitle, QStringLiteral("Document title"));
    QCOMPARE(request->ownerApp, QStringLiteral("Editor"));

    auto& settings = FileSettingsManager::instance();
    const bool oldAutoSave = settings.loadAutoSaveScreenshots();
    settings.saveAutoSaveScreenshots(true);
    const auto save = RegionSelectorTestAccess::createSaveRequest(selector);
    settings.saveAutoSaveScreenshots(oldAutoSave);
    QVERIFY(save.uniqueSave);
    QCOMPARE(save.uniqueSave->context.windowTitle, request->windowTitle);
    QCOMPARE(save.uniqueSave->context.appName, request->ownerApp);

    // Hover/highlight may change independently; moving the completed selection
    // retains its captured context rather than adopting a newly hovered window.
    RegionSelectorTestAccess::seedDetectedWindow(selector, QRect(240, 180, 100, 100), "Unrelated", "Other");
    RegionSelectorTestAccess::moveSelection(selector, QPoint(10, 10));
    request = RegionSelectorTestAccess::currentHistoryRequest(selector);
    QVERIFY(request);
    QCOMPARE(request->windowTitle, QStringLiteral("Document title"));
    QCOMPARE(request->ownerApp, QStringLiteral("Editor"));

    // Keyboard movement and resizing retain the same captured context.
    for (const auto modifiers : {Qt::NoModifier, Qt::ShiftModifier}) {
        for (const auto key : {Qt::Key_Left, Qt::Key_Right, Qt::Key_Up, Qt::Key_Down}) {
            const QRect previous = RegionSelectorTestAccess::selectionRect(selector);
            QTest::keyClick(&selector, key, modifiers);
            QVERIFY(RegionSelectorTestAccess::selectionRect(selector) != previous);
            request = RegionSelectorTestAccess::currentHistoryRequest(selector);
            QVERIFY(request);
            QCOMPARE(request->windowTitle, QStringLiteral("Document title"));
            QCOMPARE(request->ownerApp, QStringLiteral("Editor"));
        }
    }

    // Programmatic replacement must not inherit the previous window's tokens.
    RegionSelectorTestAccess::setSelectionRect(selector, QRect(10, 10, 70, 80));
    request = RegionSelectorTestAccess::currentHistoryRequest(selector);
    QVERIFY(request);
    QVERIFY(request->windowTitle.isEmpty());
    QVERIFY(request->ownerApp.isEmpty());
    // Already prepared async requests own their naming context.
    QCOMPARE(save.uniqueSave->context.windowTitle, QStringLiteral("Document title"));
}

void tst_RegionSelectorHistoryReplay::testPreservedSelectionRecordsOriginalHistory_data()
{
    QTest::addColumn<qreal>("dpr");
    QTest::addColumn<bool>("cancel");
    for (qreal dpr : {1.0, 1.5, 2.0}) {
        QTest::addRow("dpr-%g-finish", dpr) << dpr << false;
        QTest::addRow("dpr-%g-cancel", dpr) << dpr << true;
    }
}

void tst_RegionSelectorHistoryReplay::testPreservedSelectionRecordsOriginalHistory()
{
    QFETCH(qreal, dpr);
    QFETCH(bool, cancel);
    QTemporaryDir history;
    QVERIFY(history.isValid());
    const QByteArray oldHistory = qgetenv("SNAPTRAY_HISTORY_DIR");
    qputenv("SNAPTRAY_HISTORY_DIR", history.path().toUtf8());
    const auto restoreEnvironment = qScopeGuard([&] {
        QCoreApplication::sendPostedEvents(qApp, QEvent::MetaCall);
        SnapTray::HistoryRecorder::instance().waitForIdleForTests();
        if (oldHistory.isNull()) qunsetenv("SNAPTRAY_HISTORY_DIR");
        else qputenv("SNAPTRAY_HISTORY_DIR", oldHistory);
    });
    RegionSelector selector;
    selector.setAttribute(Qt::WA_DeleteOnClose, false);
    QPixmap capture(QSize(qRound(320 * dpr), qRound(240 * dpr)));
    capture.fill(Qt::yellow);
    capture.setDevicePixelRatio(dpr);
    selector.initializeForScreen(QGuiApplication::primaryScreen(), capture);
    // Use the same capture-context contract as a real screen's DPR.
    RegionSelectorTestAccess::replaceCanvasAfterPreservingSelection(selector, capture);
    const QRect window(30, 40, 120, 80);
    RegionSelectorTestAccess::seedDetectedWindow(selector, window, "Original window", "Original app");
    RegionSelectorTestAccess::dispatchMousePress(selector, window.center());
    RegionSelectorTestAccess::dispatchMouseRelease(selector, window.center());
    auto* layer = RegionSelectorTestAccess::annotationLayer(selector);
    layer->addItem(std::make_unique<ShapeAnnotation>(QRect(50, 60, 20, 15),
        ShapeType::Rectangle, Qt::red, 3, true));
    const auto originalRequest = RegionSelectorTestAccess::currentHistoryRequest(selector);
    QVERIFY(originalRequest);
    RegionSelectorTestAccess::preserveCompletedSelection(selector);
    const QPixmap result = RegionSelectorTestAccess::preservedSelectionPixmap(selector);
    const QRect globalRect = RegionSelectorTestAccess::preservedGlobalRect(selector);
    QVERIFY(!result.isNull());

    QPixmap otherScreen(180, 120);
    otherScreen.fill(Qt::blue);
    otherScreen.setDevicePixelRatio(1.0);
    RegionSelectorTestAccess::replaceCanvasAfterPreservingSelection(selector, otherScreen);
    QSignalSpy selected(&selector, &RegionSelector::regionSelected);
    if (cancel) RegionSelectorTestAccess::clearPreservedSelection(selector);
    if (!cancel) QTest::keyClick(&selector, Qt::Key_Return);
    RegionSelectorTestAccess::finishPreservedSelection(selector);
    // RegionSelector queues submission on qApp before starting the recorder's
    // worker. Deliver it before waiting for the pool or changing history dirs.
    QCoreApplication::sendPostedEvents(qApp, QEvent::MetaCall);
    QVERIFY(SnapTray::HistoryRecorder::instance().waitForIdleForTests());
    const auto entries = SnapTray::HistoryStore::loadEntries();
    if (cancel) {
        QCOMPARE(selected.count(), 0);
        QVERIFY(entries.isEmpty());
        return;
    }
    QCOMPARE(selected.count(), 1);
    QCOMPARE(qvariant_cast<QPixmap>(selected.first()[0]).toImage(), result.toImage());
    QCOMPARE(selected.first()[1].toPoint(), globalRect.topLeft());
    QCOMPARE(selected.first()[2].toRect(), globalRect);
    QCOMPARE(entries.size(), 1);
    const auto& entry = entries.first();
    QCOMPARE(entry.selectionRect, originalRequest->selectionRect);
    QCOMPARE(entry.devicePixelRatio, dpr);
    QCOMPARE(entry.canvasLogicalSize, originalRequest->canvasLogicalSize);
    QCOMPARE(entry.windowTitle, QStringLiteral("Original window"));
    QCOMPARE(entry.ownerApp, QStringLiteral("Original app"));
    QImage canvasPixels = capture.toImage();
    canvasPixels.setDevicePixelRatio(1.0);
    QCOMPARE(QImage(entry.canvasPath).convertToFormat(QImage::Format_RGB32),
             canvasPixels.convertToFormat(QImage::Format_RGB32));
    // PNG stores pixels; DPR is carried separately in the history manifest.
    QImage resultPixels = result.toImage();
    resultPixels.setDevicePixelRatio(1.0);
    QCOMPARE(QImage(entry.resultPath).convertToFormat(QImage::Format_ARGB32),
             resultPixels.convertToFormat(QImage::Format_ARGB32));
    QFile annotations(entry.annotationsPath);
    QVERIFY(annotations.open(QIODevice::ReadOnly));
    QCOMPARE(annotations.readAll(), originalRequest->annotationsJson);
}

QTEST_MAIN(tst_RegionSelectorHistoryReplay)
#include "tst_HistoryReplay.moc"
