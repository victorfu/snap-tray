#include <QtTest/QtTest>

#include <QGuiApplication>
#include "utils/CoordinateHelper.h"
#include <QTemporaryDir>

#include "RegionSelector.h"
#include "RegionSelectorTestAccess.h"
#include "annotations/AnnotationLayer.h"
#include "settings/FileSettingsManager.h"
#include "history/HistoryRecorder.h"
#include "history/AnnotationSerializer.h"
#include "annotations/ShapeAnnotation.h"
#include "annotations/MosaicRectAnnotation.h"
#include <QScopeGuard>
#include <QFile>

namespace {

// Ways a stored annotations.json stops being a document (#352).
struct AnnotationDamage {
    const char* name;
    bool removeFile;
    QByteArray payload;
};

const AnnotationDamage kAnnotationDamages[] = {
    {"missing-file", true, {}},
    {"empty-file", false, QByteArray("")},
    {"truncated-json", false, QByteArray("{")},
    {"root-not-object", false, QByteArray("[]")},
    {"items-missing", false, QByteArray("{}")},
    {"items-not-array", false, QByteArray("{\"items\":{}}")},
    {"item-not-object", false, QByteArray("{\"items\":[42]}")},
    {"unknown-item-type", false, QByteArray("{\"items\":[{\"type\":\"hologram\"}]}")},
};

bool damageAnnotations(const SnapTray::HistoryEntry& entry, const AnnotationDamage& damage)
{
    if (damage.removeFile) {
        return QFile::remove(entry.annotationsPath);
    }
    QFile file(entry.annotationsPath);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
           file.write(damage.payload) == damage.payload.size();
}

// A stored capture whose only annotation is a mosaic over the selection: the
// canvas underneath is what the saved picture was hiding.
std::optional<SnapTray::HistoryEntry> writeRedactedEntry(const QColor& canvasColor,
                                                         const QRect& selectionRect,
                                                         const QDateTime& createdAt,
                                                         const QString& windowTitle)
{
    SnapTray::CaptureSessionWriteRequest request;
    request.canvasImage = QImage(400, 300, QImage::Format_RGB32);
    request.canvasImage.fill(canvasColor);
    request.selectionRect = selectionRect;
    request.resultImage = request.canvasImage.copy(selectionRect);
    request.canvasLogicalSize = request.canvasImage.size();
    request.createdAt = createdAt;
    request.windowTitle = windowTitle;

    AnnotationLayer layer;
    layer.addItem(std::make_unique<MosaicRectAnnotation>(
        selectionRect,
        std::make_shared<const QPixmap>(QPixmap::fromImage(request.canvasImage)),
        12,
        MosaicBlurType::Pixelate));
    request.annotationsJson = SnapTray::serializeAnnotationLayer(layer);
    return SnapTray::HistoryStore::writeCaptureSession(request);
}

// Hand-built entries need the document a stored one always has.
QString writeEmptyAnnotations(const QTemporaryDir& directory)
{
    const QString path = directory.filePath(QStringLiteral("annotations.json"));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) ||
        file.write(SnapTray::serializeAnnotationLayer(AnnotationLayer())) <= 0) {
        return {};
    }
    return path;
}

// Everything a failed replay must leave alone.
struct EditState {
    QRect selectionRect;
    QByteArray annotations;
    bool canUndo = false;
    bool canRedo = false;
    QImage background;
    qreal devicePixelRatio = 0.0;
    QSize canvasSize;
    int replayIndex = 0;
    bool replayActive = false;
    QString windowTitle;
    QString ownerApp;
    int cornerRadius = 0;
    QVector<QRect> regions;
    bool hasBeautify = false;

    bool operator==(const EditState& other) const
    {
        return cornerRadius == other.cornerRadius && regions == other.regions &&
               hasBeautify == other.hasBeautify &&
               selectionRect == other.selectionRect && annotations == other.annotations &&
               canUndo == other.canUndo && canRedo == other.canRedo &&
               background == other.background && devicePixelRatio == other.devicePixelRatio &&
               canvasSize == other.canvasSize && replayIndex == other.replayIndex &&
               replayActive == other.replayActive && windowTitle == other.windowTitle &&
               ownerApp == other.ownerApp;
    }
};

EditState editStateOf(RegionSelector& selector)
{
    AnnotationLayer* layer = RegionSelectorTestAccess::annotationLayer(selector);
    EditState state;
    state.selectionRect = RegionSelectorTestAccess::selectionRect(selector);
    state.annotations = SnapTray::serializeAnnotationLayer(*layer);
    state.canUndo = layer->canUndo();
    state.canRedo = layer->canRedo();
    state.background = RegionSelectorTestAccess::backgroundImage(selector);
    state.devicePixelRatio = RegionSelectorTestAccess::devicePixelRatio(selector);
    state.canvasSize = selector.size();
    state.replayIndex = RegionSelectorTestAccess::historyReplayIndex(selector);
    state.replayActive = RegionSelectorTestAccess::historyReplayActive(selector);
    if (const auto request = RegionSelectorTestAccess::currentHistoryRequest(selector)) {
        state.windowTitle = request->windowTitle;
        state.ownerApp = request->ownerApp;
        state.cornerRadius = request->cornerRadius;
        for (const auto& region : request->captureRegions) {
            state.regions.append(region.rect);
        }
    }
    state.hasBeautify = selector.historyReplayBeautify().has_value();
    return state;
}

// Two red rectangles with the second one undone: annotations, Undo and Redo
// are all live.
void addUndoableEdits(AnnotationLayer* layer)
{
    layer->addItem(std::make_unique<ShapeAnnotation>(QRect(40, 40, 30, 25),
        ShapeType::Rectangle, Qt::red, 3, true));
    layer->addItem(std::make_unique<ShapeAnnotation>(QRect(90, 90, 30, 25),
        ShapeType::Rectangle, Qt::red, 3, true));
    layer->undo();
}

} // namespace

class tst_RegionSelectorHistoryReplay : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void testBoundarySnapCaptureRoundTrip();
    void testBoundarySnapHistoryAndCloseLifecycle();
    void testResizeEscapeDoesNotAcceptQuickPin_data();
    void testResizeEscapeDoesNotAcceptQuickPin();
    void testBuildCaptureSessionWriteRequestPreservesFields();
    void testReplayedBeautifySettingsReachThePin();
    void testRestoreLiveReplaySlotRecordsCaptureContext();
    void testApplyHistoryReplayEntryRecordsCaptureContext();
    void testDetectedWindowMetadataSurvivesHighlightClear();
    void testPreservedSelectionRecordsOriginalHistory_data();
    void testPreservedSelectionRecordsOriginalHistory();
    void testLiveEditsSurviveHistoryNavigation_data();
    void testLiveEditsSurviveHistoryNavigation();
    void testDamagedEntryLeavesCurrentEditUntouched_data();
    void testDamagedEntryLeavesCurrentEditUntouched();
    void testNavigationStepsOverDamagedEntries();
    void testFailedReplayKeepsMultiRegionBeautifiedEdit();
    void testEntryWithoutSelectionIsNotReplayed();
    void testValidEntriesStillReplay();
};

void tst_RegionSelectorHistoryReplay::initTestCase()
{
    if (QGuiApplication::screens().isEmpty()) {
        QSKIP("No screens available for RegionSelector history replay tests.");
    }
}

void tst_RegionSelectorHistoryReplay::testBoundarySnapCaptureRoundTrip()
{
    RegionSelector selector;
    QScreen* screen = QGuiApplication::primaryScreen();
    QVERIFY(screen);
    QPixmap initial(400,400); initial.fill(Qt::black);
    selector.initializeForScreen(screen, initial);
    for (qreal dpr : {1.0,1.25,1.5,2.0}) {
        QImage image(400,400,QImage::Format_RGB32);image.fill(Qt::black);
        {QPainter p(&image);p.fillRect(qCeil(100*dpr),0,200,400,Qt::white);}
        QPixmap source=QPixmap::fromImage(image);source.setDevicePixelRatio(dpr);
        RegionSelectorTestAccess::replaceCanvasAfterPreservingSelection(selector,source);
        selector.move(-1200, -600); // applyCanvasGeometry must run before this assertion.
        QCOMPARE(selector.pos(), QPoint(-1200, -600));
        RegionSelectorTestAccess::setSelectionRect(selector,QRect(20,20,77,80));
        RegionSelectorTestAccess::dispatchMousePress(selector,QPoint(98,60));
        QTest::keyPress(&selector,Qt::Key_S);
        const QRect snapped(20,20,80,80);
        QCOMPARE(RegionSelectorTestAccess::selectionRect(selector),snapped);
        const QRect physical=CoordinateHelper::toPhysicalCoveringRect(snapped,dpr);
        const auto exported=RegionSelectorTestAccess::exportSelection(selector);
        QCOMPARE(exported.size(),physical.size());
        auto expected=image.copy(physical);expected.setDevicePixelRatio(dpr);
        QCOMPARE(exported.toImage().convertToFormat(QImage::Format_RGB32),expected);
        auto history=RegionSelectorTestAccess::currentHistoryRequest(selector);
        QVERIFY(history);
        QCOMPARE(history->selectionRect,snapped);
        QCOMPARE(history->canvasImage.convertToFormat(QImage::Format_RGB32),
                 source.toImage().convertToFormat(QImage::Format_RGB32));
        QTest::keyRelease(&selector,Qt::Key_S);
        QCOMPARE(RegionSelectorTestAccess::selectionRect(selector),QRect(20,20,77,80));
        QTest::keyPress(&selector,Qt::Key_S);
        QTest::keyClick(&selector,Qt::Key_Escape);
        QCOMPARE(RegionSelectorTestAccess::selectionRect(selector),QRect(20,20,77,80));
        QTest::keyRelease(&selector,Qt::Key_S);
    }
}

void tst_RegionSelectorHistoryReplay::testBoundarySnapHistoryAndCloseLifecycle()
{
    QTemporaryDir directory;QVERIFY(directory.isValid());
    QImage image(400,400,QImage::Format_RGB32);image.fill(Qt::black);
    {QPainter painter(&image);painter.fillRect(100,0,300,400,Qt::white);}
    const QPixmap source=QPixmap::fromImage(image);
    RegionSelector selector;selector.setAttribute(Qt::WA_DeleteOnClose,false);
    selector.initializeForScreen(QGuiApplication::primaryScreen(),source);
    RegionSelectorTestAccess::replaceCanvasAfterPreservingSelection(selector,source);
    const QRect raw(20,20,77,80), snapped(20,20,80,80);
    RegionSelectorTestAccess::setSelectionRect(selector,raw);
    RegionSelectorTestAccess::setHistoryLiveSlot(selector,source,1,{400,400},raw,0);
    SnapTray::HistoryEntry entry;
    entry.id="boundary-snap-lifecycle";entry.replayAvailable=true;
    entry.canvasPath=directory.filePath("canvas.png");
    entry.annotationsPath=writeEmptyAnnotations(directory);
    entry.canvasLogicalSize={400,400};entry.devicePixelRatio=1;entry.selectionRect=raw;
    QImage replay(400, 400, QImage::Format_RGB32);
    replay.fill(Qt::black);
    {
        QPainter painter(&replay);
        painter.fillRect(200, 0, 200, 400, Qt::white);
    }
    QVERIFY(replay.save(entry.canvasPath));
    QVERIFY(RegionSelectorTestAccess::loadSingleReplayEntry(selector,entry));
    RegionSelectorTestAccess::dispatchMousePress(selector,{98,60});
    QTest::keyPress(&selector,Qt::Key_S);
    QCOMPARE(RegionSelectorTestAccess::selectionRect(selector),raw);
    QVERIFY(!RegionSelectorTestAccess::snappedEdges(selector));
    QTest::keyRelease(&selector,Qt::Key_S);
    RegionSelectorTestAccess::dispatchMouseRelease(selector,{98,60});
    RegionSelectorTestAccess::invokeRestoreLiveReplaySlot(selector);
    RegionSelectorTestAccess::dispatchMousePress(selector,{98,60});
    QTest::keyPress(&selector,Qt::Key_S);
    QCOMPARE(RegionSelectorTestAccess::selectionRect(selector),snapped);
    QVERIFY(RegionSelectorTestAccess::snappedEdges(selector));
    RegionSelectorTestAccess::dispatchClose(selector);
    QVERIFY(!RegionSelectorTestAccess::snappedEdges(selector));
    // Even direct late key delivery cannot reuse the closed capture's pixels.
    QTest::keyRelease(&selector,Qt::Key_S);
    QTest::keyPress(&selector,Qt::Key_S);
    QCOMPARE(RegionSelectorTestAccess::selectionRect(selector),raw);
    QVERIFY(!RegionSelectorTestAccess::snappedEdges(selector));
}

void tst_RegionSelectorHistoryReplay::testResizeEscapeDoesNotAcceptQuickPin_data()
{
    QTest::addColumn<bool>("quickPin");
    QTest::addColumn<bool>("snapping");
    QTest::addColumn<bool>("viaFilter");
    for (bool quickPin : {false, true}) {
        for (bool snapping : {false, true}) {
            for (bool viaFilter : {false, true}) {
                const auto name = QString("%1-%2-%3")
                    .arg(quickPin ? "quick-pin" : "ordinary-capture")
                    .arg(snapping ? "snapped" : "raw-resize")
                    .arg(viaFilter ? "event-filter" : "key-handler");
                QTest::newRow(qPrintable(name)) << quickPin << snapping << viaFilter;
            }
        }
    }
}

void tst_RegionSelectorHistoryReplay::testResizeEscapeDoesNotAcceptQuickPin()
{
    QFETCH(bool, quickPin);
    QFETCH(bool, snapping);
    QFETCH(bool, viaFilter);
    QTemporaryDir history;
    QVERIFY(history.isValid());
    const QByteArray previousHistory = qgetenv("SNAPTRAY_HISTORY_DIR");
    qputenv("SNAPTRAY_HISTORY_DIR", history.path().toUtf8());
    const auto restore = qScopeGuard([&] {
        QCoreApplication::sendPostedEvents(qApp, QEvent::MetaCall);
        SnapTray::HistoryRecorder::instance().waitForIdleForTests();
        if (previousHistory.isNull()) qunsetenv("SNAPTRAY_HISTORY_DIR");
        else qputenv("SNAPTRAY_HISTORY_DIR", previousHistory);
    });
    QImage image(400, 400, QImage::Format_RGB32);
    image.fill(Qt::black);
    {
        QPainter painter(&image);
        painter.fillRect(100, 0, 300, 400, Qt::white);
    }
    RegionSelector selector;
    selector.setAttribute(Qt::WA_DeleteOnClose, false);
    selector.setQuickPinMode(quickPin);
    const QPixmap source = QPixmap::fromImage(image);
    selector.initializeForScreen(QGuiApplication::primaryScreen(), source);
    RegionSelectorTestAccess::replaceCanvasAfterPreservingSelection(selector, source);
    const QRect original(20, 20, 77, 80);
    RegionSelectorTestAccess::setSelectionRect(selector, original);
    QSignalSpy selected(&selector, &RegionSelector::regionSelected);
    RegionSelectorTestAccess::dispatchWidgetMousePress(selector, {98, 60});
    if (snapping) QTest::keyPress(&selector, Qt::Key_S);
    RegionSelectorTestAccess::dispatchWidgetMouseMove(selector, {100, 60}, Qt::LeftButton);
    QVERIFY(RegionSelectorTestAccess::selectionRect(selector) != original);
    RegionSelectorTestAccess::dispatchEscapePress(selector, viaFilter);
    QCOMPARE(RegionSelectorTestAccess::selectionRect(selector), original);
    RegionSelectorTestAccess::dispatchWidgetMouseRelease(selector, {100, 60});
    QCOMPARE(selected.count(), 0);
    QCoreApplication::sendPostedEvents(qApp, QEvent::MetaCall);
    QVERIFY(SnapTray::HistoryRecorder::instance().waitForIdleForTests());
    QVERIFY(SnapTray::HistoryStore::loadEntries().isEmpty());
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

void tst_RegionSelectorHistoryReplay::testReplayedBeautifySettingsReachThePin()
{
    RegionSelector selector;
    QScreen* screen = QGuiApplication::primaryScreen();
    QVERIFY(screen);

    QPixmap initialCapture(QSize(120, 90));
    initialCapture.fill(Qt::gray);
    selector.initializeForScreen(screen, initialCapture);
    QVERIFY(!selector.historyReplayBeautify());
    QVERIFY(selector.lastRecordedHistoryEntryId().isEmpty());

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QPixmap replayPixmap(QSize(160, 100));
    replayPixmap.fill(Qt::darkYellow);
    const QString canvasPath = tempDir.filePath(QStringLiteral("canvas.png"));
    QVERIFY(replayPixmap.save(canvasPath));

    SnapTray::HistoryEntry entry;
    entry.id = QStringLiteral("beautified-entry");
    entry.canvasPath = canvasPath;
    entry.annotationsPath = writeEmptyAnnotations(tempDir);
    entry.canvasLogicalSize = QSize(160, 100);
    entry.selectionRect = QRect(5, 6, 30, 20);
    BeautifySettings look;
    look.padding = 17;
    entry.beautify = look;

    // The pin made from a replayed entry continues from that entry's look.
    QVERIFY(RegionSelectorTestAccess::invokeApplyHistoryReplayEntry(selector, entry));
    QVERIFY(selector.historyReplayBeautify());
    QCOMPARE(selector.historyReplayBeautify()->padding, 17);

    // Every recorded capture knows its history id before it is written, so
    // the pin can name the entry that holds its document.
    const auto request = RegionSelectorTestAccess::currentHistoryRequest(selector);
    QVERIFY(request);
    QVERIFY(!request->id.isEmpty());
    QVERIFY(!request->beautify);

    // A plain entry, or going back to the live capture, drops it again.
    entry.beautify.reset();
    QVERIFY(RegionSelectorTestAccess::invokeApplyHistoryReplayEntry(selector, entry));
    QVERIFY(!selector.historyReplayBeautify());
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
    entry.annotationsPath = writeEmptyAnnotations(tempDir);
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

void tst_RegionSelectorHistoryReplay::testLiveEditsSurviveHistoryNavigation_data()
{
    QTest::addColumn<bool>("previousRoundTrip");
    QTest::addColumn<bool>("noopForward");
    QTest::addColumn<bool>("openByEntryId");
    for (bool openByEntryId : {false, true}) {
        const char* entryPoint = openByEntryId ? "entry-id" : "keyboard";
        QTest::addRow("%s-first-round-trip", entryPoint) << false << false << openByEntryId;
        QTest::addRow("%s-second-round-trip", entryPoint) << true << false << openByEntryId;
        QTest::addRow("%s-noop-period", entryPoint) << false << true << openByEntryId;
    }
}

void tst_RegionSelectorHistoryReplay::testLiveEditsSurviveHistoryNavigation()
{
    QFETCH(bool, previousRoundTrip);
    QFETCH(bool, noopForward);
    QFETCH(bool, openByEntryId);
    QTemporaryDir history;
    QVERIFY(history.isValid());
    const QByteArray previousHistory = qgetenv("SNAPTRAY_HISTORY_DIR");
    qputenv("SNAPTRAY_HISTORY_DIR", history.path().toUtf8());
    const auto restoreEnvironment = qScopeGuard([&] {
        if (previousHistory.isNull()) qunsetenv("SNAPTRAY_HISTORY_DIR");
        else qputenv("SNAPTRAY_HISTORY_DIR", previousHistory);
    });

    SnapTray::CaptureSessionWriteRequest stored;
    stored.canvasImage = QImage(400, 300, QImage::Format_RGB32);
    stored.canvasImage.fill(Qt::blue);
    stored.selectionRect = QRect(5, 5, 80, 60);
    stored.resultImage = stored.canvasImage.copy(stored.selectionRect);
    stored.canvasLogicalSize = stored.canvasImage.size();
    AnnotationLayer emptyLayer;
    stored.annotationsJson = SnapTray::serializeAnnotationLayer(emptyLayer);
    const auto entry = SnapTray::HistoryStore::writeCaptureSession(stored);
    QVERIFY(entry);

    // A second history entry exercises navigation within history without
    // replacing the live snapshot with the currently displayed history entry.
    auto older = stored;
    older.createdAt = stored.createdAt.addSecs(-1);
    older.selectionRect = QRect(10, 10, 60, 40);
    older.resultImage = older.canvasImage.copy(older.selectionRect);
    QVERIFY(SnapTray::HistoryStore::writeCaptureSession(older));

    RegionSelector selector;
    selector.setAttribute(Qt::WA_DeleteOnClose, false);
    QPixmap liveCapture(400, 300);
    liveCapture.fill(Qt::white);
    selector.initializeForScreen(QGuiApplication::primaryScreen(), liveCapture);
    const QRect originalCrop(20, 20, 200, 150);
    const QRect editedCrop(30, 35, 250, 190);
    RegionSelectorTestAccess::setSelectionRect(selector, originalCrop);
    auto* layer = RegionSelectorTestAccess::annotationLayer(selector);
    layer->addItem(std::make_unique<ShapeAnnotation>(QRect(40, 40, 30, 25),
        ShapeType::Rectangle, Qt::red, 3, true));
    const QByteArray annotationA = SnapTray::serializeAnnotationLayer(*layer);

    if (previousRoundTrip) {
        QTest::keyClick(&selector, Qt::Key_Comma);
        QCOMPARE(RegionSelectorTestAccess::selectionRect(selector), stored.selectionRect);
        QVERIFY(layer->isEmpty());
        QTest::keyClick(&selector, Qt::Key_Period);
    }
    if (noopForward) {
        QTest::keyClick(&selector, Qt::Key_Period);
    }
    QCOMPARE(RegionSelectorTestAccess::selectionRect(selector), originalCrop);
    QCOMPARE(SnapTray::serializeAnnotationLayer(*layer), annotationA);

    layer->addItem(std::make_unique<ShapeAnnotation>(QRect(100, 100, 40, 35),
        ShapeType::Rectangle, Qt::green, 3, true));
    RegionSelectorTestAccess::setSelectionRect(selector, editedCrop);
    const QByteArray annotationsAB = SnapTray::serializeAnnotationLayer(*layer);
    QCOMPARE(layer->itemCount(), size_t(2));

    if (openByEntryId) {
        QVERIFY(selector.beginHistoryReplay(entry->id));
    } else {
        QTest::keyClick(&selector, Qt::Key_Comma);
    }
    QCOMPARE(RegionSelectorTestAccess::selectionRect(selector), stored.selectionRect);
    QVERIFY(layer->isEmpty());
    QTest::keyClick(&selector, Qt::Key_Comma);
    QCOMPARE(RegionSelectorTestAccess::selectionRect(selector), older.selectionRect);
    if (openByEntryId) {
        QVERIFY(selector.beginHistoryReplay(entry->id));
    } else {
        QTest::keyClick(&selector, Qt::Key_Period);
    }
    QCOMPARE(RegionSelectorTestAccess::selectionRect(selector), stored.selectionRect);
    QTest::keyClick(&selector, Qt::Key_Period);
    QCOMPARE(layer->itemCount(), size_t(2));
    QCOMPARE(SnapTray::serializeAnnotationLayer(*layer), annotationsAB);
    QCOMPARE(RegionSelectorTestAccess::selectionRect(selector), editedCrop);
}

// #352: an entry whose annotations cannot be read is refused before anything
// on screen changes, whichever way it is reached and whatever is on screen.
void tst_RegionSelectorHistoryReplay::testDamagedEntryLeavesCurrentEditUntouched_data()
{
    QTest::addColumn<int>("damageIndex");
    QTest::addColumn<bool>("fromHistoryEntry");
    QTest::addColumn<bool>("openByEntryId");
    for (int i = 0; i < int(std::size(kAnnotationDamages)); ++i) {
        for (bool fromHistoryEntry : {false, true}) {
            for (bool openByEntryId : {false, true}) {
                QTest::addRow("%s-from-%s-by-%s", kAnnotationDamages[i].name,
                              fromHistoryEntry ? "history-entry" : "live-capture",
                              openByEntryId ? "entry-id" : "keyboard")
                    << i << fromHistoryEntry << openByEntryId;
            }
        }
    }
}

void tst_RegionSelectorHistoryReplay::testDamagedEntryLeavesCurrentEditUntouched()
{
    QFETCH(int, damageIndex);
    QFETCH(bool, fromHistoryEntry);
    QFETCH(bool, openByEntryId);
    QTemporaryDir history;
    QVERIFY(history.isValid());
    const QByteArray previousHistory = qgetenv("SNAPTRAY_HISTORY_DIR");
    qputenv("SNAPTRAY_HISTORY_DIR", history.path().toUtf8());
    const auto restoreEnvironment = qScopeGuard([&] {
        if (previousHistory.isNull()) qunsetenv("SNAPTRAY_HISTORY_DIR");
        else qputenv("SNAPTRAY_HISTORY_DIR", previousHistory);
    });

    // Keyboard navigation walks from the live capture to the newest entry and
    // on to older ones. The damaged entry is the next and last stop either way.
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const QRect goodSelection(5, 5, 80, 60);
    std::optional<SnapTray::HistoryEntry> good;
    if (fromHistoryEntry) {
        good = writeRedactedEntry(Qt::blue, goodSelection, now, "Good title");
        QVERIFY(good);
    }
    const auto damaged = writeRedactedEntry(Qt::darkGreen, QRect(10, 10, 60, 40),
                                            now.addSecs(-1), "Damaged title");
    QVERIFY(damaged);
    QVERIFY(damageAnnotations(*damaged, kAnnotationDamages[damageIndex]));

    RegionSelector selector;
    selector.setAttribute(Qt::WA_DeleteOnClose, false);
    QPixmap liveCapture(400, 300);
    liveCapture.fill(Qt::white);
    selector.initializeForScreen(QGuiApplication::primaryScreen(), liveCapture);
    const QRect liveCrop(20, 20, 200, 150);
    RegionSelectorTestAccess::setSelectionRect(selector, liveCrop);
    auto* layer = RegionSelectorTestAccess::annotationLayer(selector);
    addUndoableEdits(layer);
    const EditState liveState = editStateOf(selector);

    if (fromHistoryEntry) {
        QTest::keyClick(&selector, Qt::Key_Comma);
        QCOMPARE(RegionSelectorTestAccess::historyReplayIndex(selector), 0);
        QCOMPARE(RegionSelectorTestAccess::selectionRect(selector), goodSelection);
        QCOMPARE(layer->itemCount(), size_t(1));
        addUndoableEdits(layer);
    }
    const EditState before = editStateOf(selector);
    QCOMPARE(before.replayIndex, fromHistoryEntry ? 0 : -1);
    QCOMPARE(before.replayActive, fromHistoryEntry);
    QVERIFY(before.canUndo);
    QVERIFY(before.canRedo);
    QCOMPARE(before.windowTitle, fromHistoryEntry ? QStringLiteral("Good title") : QString());

    QSignalSpy changedSpy(layer, &AnnotationLayer::changed);
    if (openByEntryId) {
        QVERIFY(!selector.beginHistoryReplay(damaged->id));
    } else {
        QTest::keyClick(&selector, Qt::Key_Comma);
    }

    QCOMPARE(changedSpy.count(), 0);
    QVERIFY(editStateOf(selector) == before);

    // Undo and Redo still act on the edit that was on screen.
    const size_t itemCount = layer->itemCount();
    layer->redo();
    QCOMPARE(layer->itemCount(), itemCount + 1);
    layer->undo();
    layer->undo();
    QCOMPARE(layer->itemCount(), itemCount - 1);
    layer->redo();
    QVERIFY(editStateOf(selector) == before);

    // The index did not move: one step forward is where it was before.
    if (fromHistoryEntry) {
        QTest::keyClick(&selector, Qt::Key_Period);
        QCOMPARE(RegionSelectorTestAccess::historyReplayIndex(selector), -1);
        QVERIFY(editStateOf(selector).selectionRect == liveState.selectionRect);
        QCOMPARE(editStateOf(selector).annotations, liveState.annotations);
    } else {
        good = writeRedactedEntry(Qt::blue, goodSelection, now.addSecs(-2), "Good title");
        QVERIFY(good);
        QVERIFY(selector.beginHistoryReplay(good->id));
        QCOMPARE(RegionSelectorTestAccess::selectionRect(selector), goodSelection);
        QCOMPARE(layer->itemCount(), size_t(1));
    }
}

// A damaged entry does not cut off the entries behind it, nor the way back to
// the live capture from an older entry.
void tst_RegionSelectorHistoryReplay::testNavigationStepsOverDamagedEntries()
{
    QTemporaryDir history;
    QVERIFY(history.isValid());
    const QByteArray previousHistory = qgetenv("SNAPTRAY_HISTORY_DIR");
    qputenv("SNAPTRAY_HISTORY_DIR", history.path().toUtf8());
    const auto restoreEnvironment = qScopeGuard([&] {
        if (previousHistory.isNull()) qunsetenv("SNAPTRAY_HISTORY_DIR");
        else qputenv("SNAPTRAY_HISTORY_DIR", previousHistory);
    });

    // Newest to oldest: damaged, newer, damaged, older.
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const QRect newerSelection(5, 5, 80, 60);
    const QRect olderSelection(15, 25, 70, 50);
    const auto newestDamaged = writeRedactedEntry(Qt::red, QRect(1, 1, 20, 20), now, "D1");
    const auto newer = writeRedactedEntry(Qt::blue, newerSelection, now.addSecs(-1), "Newer");
    const auto middleDamaged = writeRedactedEntry(Qt::red, QRect(2, 2, 20, 20), now.addSecs(-2), "D2");
    const auto older = writeRedactedEntry(Qt::green, olderSelection, now.addSecs(-3), "Older");
    QVERIFY(newestDamaged && newer && middleDamaged && older);
    QVERIFY(damageAnnotations(*newestDamaged, kAnnotationDamages[0]));
    QVERIFY(damageAnnotations(*middleDamaged, kAnnotationDamages[2]));

    RegionSelector selector;
    selector.setAttribute(Qt::WA_DeleteOnClose, false);
    QPixmap liveCapture(400, 300);
    liveCapture.fill(Qt::white);
    selector.initializeForScreen(QGuiApplication::primaryScreen(), liveCapture);
    RegionSelectorTestAccess::setSelectionRect(selector, QRect(20, 20, 200, 150));
    auto* layer = RegionSelectorTestAccess::annotationLayer(selector);
    addUndoableEdits(layer);
    const EditState liveState = editStateOf(selector);

    QTest::keyClick(&selector, Qt::Key_Comma);
    QCOMPARE(RegionSelectorTestAccess::historyReplayIndex(selector), 1);
    QCOMPARE(RegionSelectorTestAccess::selectionRect(selector), newerSelection);
    QTest::keyClick(&selector, Qt::Key_Comma);
    QCOMPARE(RegionSelectorTestAccess::historyReplayIndex(selector), 3);
    QCOMPARE(RegionSelectorTestAccess::selectionRect(selector), olderSelection);
    const EditState oldest = editStateOf(selector);
    QTest::keyClick(&selector, Qt::Key_Comma);
    QVERIFY(editStateOf(selector) == oldest);

    QTest::keyClick(&selector, Qt::Key_Period);
    QCOMPARE(RegionSelectorTestAccess::historyReplayIndex(selector), 1);
    QCOMPARE(RegionSelectorTestAccess::selectionRect(selector), newerSelection);
    QTest::keyClick(&selector, Qt::Key_Period);
    QCOMPARE(RegionSelectorTestAccess::historyReplayIndex(selector), -1);
    QVERIFY(!RegionSelectorTestAccess::historyReplayActive(selector));
    // Undo history is not part of the live slot's round trip (#109).
    QCOMPARE(editStateOf(selector).selectionRect, liveState.selectionRect);
    QCOMPARE(editStateOf(selector).annotations, liveState.annotations);
    QCOMPARE(editStateOf(selector).background, liveState.background);
}

// The same guarantee for an edit that is more than one rectangle: a replayed
// multi-region entry with a rounded corner and its own Beautify look.
void tst_RegionSelectorHistoryReplay::testFailedReplayKeepsMultiRegionBeautifiedEdit()
{
    QTemporaryDir history;
    QVERIFY(history.isValid());
    const QByteArray previousHistory = qgetenv("SNAPTRAY_HISTORY_DIR");
    qputenv("SNAPTRAY_HISTORY_DIR", history.path().toUtf8());
    const auto restoreEnvironment = qScopeGuard([&] {
        if (previousHistory.isNull()) qunsetenv("SNAPTRAY_HISTORY_DIR");
        else qputenv("SNAPTRAY_HISTORY_DIR", previousHistory);
    });

    SnapTray::CaptureSessionWriteRequest request;
    request.canvasImage = QImage(400, 300, QImage::Format_RGB32);
    request.canvasImage.fill(Qt::darkCyan);
    request.canvasLogicalSize = request.canvasImage.size();
    request.selectionRect = QRect(10, 10, 200, 100);
    request.captureRegions = {
        MultiRegionManager::Region{QRect(10, 10, 80, 60), QColor(Qt::green), 1, false},
        MultiRegionManager::Region{QRect(120, 40, 90, 70), QColor(Qt::red), 2, true},
    };
    request.cornerRadius = 7;
    BeautifySettings look;
    look.padding = 17;
    request.beautify = look;
    request.resultImage = request.canvasImage.copy(request.selectionRect);
    request.annotationsJson = SnapTray::serializeAnnotationLayer(AnnotationLayer());
    const auto shown = SnapTray::HistoryStore::writeCaptureSession(request);
    QVERIFY(shown);
    const auto damaged = writeRedactedEntry(Qt::red, QRect(1, 1, 20, 20),
                                            request.createdAt.addSecs(-1), "Damaged");
    QVERIFY(damaged);
    QVERIFY(damageAnnotations(*damaged, kAnnotationDamages[2]));

    RegionSelector selector;
    selector.setAttribute(Qt::WA_DeleteOnClose, false);
    QPixmap liveCapture(400, 300);
    liveCapture.fill(Qt::white);
    selector.initializeForScreen(QGuiApplication::primaryScreen(), liveCapture);
    QVERIFY(selector.beginHistoryReplay(shown->id));
    addUndoableEdits(RegionSelectorTestAccess::annotationLayer(selector));
    const EditState before = editStateOf(selector);
    QCOMPARE(before.regions, QVector<QRect>({QRect(10, 10, 80, 60), QRect(120, 40, 90, 70)}));
    QCOMPARE(before.cornerRadius, 7);
    QVERIFY(before.hasBeautify);

    QVERIFY(!selector.beginHistoryReplay(damaged->id));
    QVERIFY(editStateOf(selector) == before);
    QTest::keyClick(&selector, Qt::Key_Comma);
    QVERIFY(editStateOf(selector) == before);
    QCOMPARE(selector.historyReplayBeautify()->padding, 17);
}

// A manifest that lost its selection is damaged too, not a capture of nothing.
void tst_RegionSelectorHistoryReplay::testEntryWithoutSelectionIsNotReplayed()
{
    QTemporaryDir history;
    QVERIFY(history.isValid());
    const QByteArray previousHistory = qgetenv("SNAPTRAY_HISTORY_DIR");
    qputenv("SNAPTRAY_HISTORY_DIR", history.path().toUtf8());
    const auto restoreEnvironment = qScopeGuard([&] {
        if (previousHistory.isNull()) qunsetenv("SNAPTRAY_HISTORY_DIR");
        else qputenv("SNAPTRAY_HISTORY_DIR", previousHistory);
    });
    auto entry = writeRedactedEntry(Qt::blue, QRect(5, 5, 80, 60),
                                    QDateTime::currentDateTimeUtc(), "Title");
    QVERIFY(entry);

    RegionSelector selector;
    QPixmap liveCapture(400, 300);
    liveCapture.fill(Qt::white);
    selector.initializeForScreen(QGuiApplication::primaryScreen(), liveCapture);
    RegionSelectorTestAccess::setSelectionRect(selector, QRect(20, 20, 200, 150));
    addUndoableEdits(RegionSelectorTestAccess::annotationLayer(selector));
    const EditState before = editStateOf(selector);

    entry->selectionRect = QRect();
    QVERIFY(!RegionSelectorTestAccess::invokeApplyHistoryReplayEntry(selector, *entry));
    QVERIFY(editStateOf(selector) == before);

    entry->selectionRect = QRect(5, 5, 80, 60);
    entry->canvasPath = history.filePath("no-such-canvas.png");
    QVERIFY(!RegionSelectorTestAccess::invokeApplyHistoryReplayEntry(selector, *entry));
    QVERIFY(editStateOf(selector) == before);
}

// The stricter check keeps what was valid: a document with no annotations,
// and multi-region captures at a fractional device pixel ratio.
void tst_RegionSelectorHistoryReplay::testValidEntriesStillReplay()
{
    QTemporaryDir history;
    QVERIFY(history.isValid());
    const QByteArray previousHistory = qgetenv("SNAPTRAY_HISTORY_DIR");
    qputenv("SNAPTRAY_HISTORY_DIR", history.path().toUtf8());
    const auto restoreEnvironment = qScopeGuard([&] {
        if (previousHistory.isNull()) qunsetenv("SNAPTRAY_HISTORY_DIR");
        else qputenv("SNAPTRAY_HISTORY_DIR", previousHistory);
    });

    const qreal dpr = 1.5;
    SnapTray::CaptureSessionWriteRequest request;
    request.canvasImage = QImage(600, 450, QImage::Format_RGB32);
    request.canvasImage.fill(Qt::darkCyan);
    request.devicePixelRatio = dpr;
    request.canvasLogicalSize = QSize(400, 300);
    request.selectionRect = QRect(10, 10, 200, 100);
    request.captureRegions = {
        MultiRegionManager::Region{QRect(10, 10, 80, 60), QColor(Qt::green), 1, false},
        MultiRegionManager::Region{QRect(120, 40, 90, 70), QColor(Qt::red), 2, true},
    };
    request.resultImage = request.canvasImage.copy(QRect(15, 15, 300, 150));
    request.annotationsJson = SnapTray::serializeAnnotationLayer(AnnotationLayer());
    QCOMPARE(request.annotationsJson, QByteArray("{\"items\":[]}"));
    const auto multiRegion = SnapTray::HistoryStore::writeCaptureSession(request);
    QVERIFY(multiRegion);

    RegionSelector selector;
    selector.setAttribute(Qt::WA_DeleteOnClose, false);
    QPixmap liveCapture(400, 300);
    liveCapture.fill(Qt::white);
    selector.initializeForScreen(QGuiApplication::primaryScreen(), liveCapture);
    auto* layer = RegionSelectorTestAccess::annotationLayer(selector);
    addUndoableEdits(layer);

    QVERIFY(selector.beginHistoryReplay(multiRegion->id));
    QVERIFY(layer->isEmpty());
    QVERIFY(RegionSelectorTestAccess::historyReplayActive(selector));
    QCOMPARE(RegionSelectorTestAccess::devicePixelRatio(selector), dpr);
    QCOMPARE(RegionSelectorTestAccess::backgroundPixelSize(selector), QSize(600, 450));
    QCOMPARE(selector.size(), QSize(400, 300));
    const auto replayed = RegionSelectorTestAccess::currentHistoryRequest(selector);
    QVERIFY(replayed);
    QCOMPARE(replayed->captureRegions.size(), 2);
    QCOMPARE(replayed->captureRegions.at(0).rect, QRect(10, 10, 80, 60));
    QCOMPARE(replayed->captureRegions.at(1).rect, QRect(120, 40, 90, 70));

    // A redacted entry comes back with its mosaic, drawn from its own canvas.
    const auto redacted = writeRedactedEntry(Qt::blue, QRect(5, 5, 80, 60),
                                             QDateTime::currentDateTimeUtc().addSecs(5), "Title");
    QVERIFY(redacted);
    QVERIFY(selector.beginHistoryReplay(redacted->id));
    QCOMPARE(layer->itemCount(), size_t(1));
    QVERIFY(dynamic_cast<MosaicRectAnnotation*>(layer->itemAt(0)));
    QCOMPARE(RegionSelectorTestAccess::selectionRect(selector), QRect(5, 5, 80, 60));
}

QTEST_MAIN(tst_RegionSelectorHistoryReplay)
#include "tst_HistoryReplay.moc"
