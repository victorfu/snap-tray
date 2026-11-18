#include <QtTest/QtTest>

#include <QImage>
#include <QMenu>
#include <QPainter>
#include <QSemaphore>

#include <atomic>
#include <memory>

#include "PinWindow.h"
#include "PlatformFeatures.h"
#include "annotations/AnnotationLayer.h"
#include "annotations/MosaicRectAnnotation.h"
#include "annotations/PolylineAnnotation.h"
#include "pinwindow/ForegroundCutout.h"
#include "pinwindow/ImageStripTransform.h"
#include "pinwindow/RegionLayoutManager.h"
#include "tools/ToolId.h"
#include "tools/ToolManager.h"

using SnapTray::ForegroundCutout::MaskResult;
using SnapTray::ForegroundCutout::Status;

namespace {

QPixmap patternPixmap(int width, int height, qreal dpr = 1.0)
{
    QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            image.setPixelColor(x, y, QColor(x % 256, y % 256, (x * 3 + y * 5) % 256, 255));
        }
    }
    QPixmap pixmap = QPixmap::fromImage(image);
    pixmap.setDevicePixelRatio(dpr);
    return pixmap;
}

QImage argb(const QImage& image)
{
    QImage copy = image.convertToFormat(QImage::Format_ARGB32);
    copy.setDevicePixelRatio(1.0);
    return copy;
}

// Left half of the device pixels is the subject.
MaskResult halfMask(const QImage& image)
{
    MaskResult result;
    result.mask = QImage(image.size(), QImage::Format_Grayscale8);
    result.mask.fill(0);
    for (int y = 0; y < image.height(); ++y) {
        uchar* row = result.mask.scanLine(y);
        for (int x = 0; x < image.width() / 2; ++x) {
            row[x] = 255;
        }
    }
    result.status = Status::Success;
    return result;
}

std::unique_ptr<PolylineAnnotation> line(const QPoint& a, const QPoint& b)
{
    return std::make_unique<PolylineAnnotation>(
        QVector<QPoint>{a, b}, QColor(Qt::red), 3, LineEndStyle::None, LineStyle::Solid);
}

void pressKey(PinWindow& window, int key)
{
    QKeyEvent event(QEvent::KeyPress, key, Qt::NoModifier);
    QCoreApplication::sendEvent(&window, &event);
}

// A provider that holds the worker until released, so a test can act while
// the removal is in flight. Always release it: the worker is shared.
struct Gate {
    std::shared_ptr<QSemaphore> semaphore = std::make_shared<QSemaphore>(0);
    std::shared_ptr<std::atomic_int> calls = std::make_shared<std::atomic_int>(0);

    SnapTray::ForegroundCutout::MaskProvider provider() const
    {
        return [semaphore = semaphore, calls = calls](const QImage& image) {
            ++*calls;
            semaphore->acquire();
            return halfMask(image);
        };
    }
};

} // namespace

class TestPinWindowRemoveBackground : public QObject
{
    Q_OBJECT

private slots:
    void appliesAsOneUndoStep_data();
    void appliesAsOneUndoStep();
    void annotationsStayEditableAndMosaicsResample();
    void failuresLeaveThePinUntouched_data();
    void failuresLeaveThePinUntouched();
    void staleResultIsDropped();
    void strokeBeingDrawnHoldsTheResult();
    void autoBlurStartedMeanwhileHoldsTheResult();
    void mergedPinFlattensAndKeepsExportSize();
    void escapeCancelsWithoutClosing();
    void closingWhileRunningIsSafe();
    void refusedDuringLiveUpdateAndAutoBlur();
    void exportKeepsTransparency();
    void menuActionFollowsCapability();
};

void TestPinWindowRemoveBackground::appliesAsOneUndoStep_data()
{
    QTest::addColumn<qreal>("dpr");
    QTest::newRow("1x") << 1.0;
    QTest::newRow("1.5x") << 1.5;
    QTest::newRow("2x") << 2.0;
}

void TestPinWindowRemoveBackground::appliesAsOneUndoStep()
{
    QFETCH(qreal, dpr);
    const QPixmap source = patternPixmap(240, 120, dpr);
    QVector<LayoutRegion> regions(1);
    regions[0].rect = QRect(0, 0, 100, 60);
    regions[0].originalRect = regions[0].rect;
    regions[0].image = source.toImage();
    regions[0].index = 1;

    PinWindow window(source, QPoint(0, 0), nullptr, false);
    window.m_storedRegions = regions;
    window.m_hasMultiRegionData = true;
    window.m_foregroundMaskProvider = halfMask;
    const QSize sizeBefore = window.size();
    const QRectF sampleBefore = window.sourceSampleRect();

    window.removeBackground();
    QVERIFY(window.m_backgroundRemovalInProgress);
    QTRY_VERIFY(!window.m_backgroundRemovalInProgress);

    // Full canvas kept: same size, ratio and sample rect; nothing moved.
    const QImage after = argb(window.m_originalPixmap.toImage());
    QCOMPARE(after.size(), source.size());
    QCOMPARE(window.m_originalPixmap.devicePixelRatio(), dpr);
    QCOMPARE(window.size(), sizeBefore);
    QCOMPARE(window.sourceSampleRect(), sampleBefore);
    const QImage original = argb(source.toImage());
    for (int y = 0; y < after.height(); y += 7) {
        for (int x = 0; x < after.width(); x += 5) {
            if (x < after.width() / 2) {
                QCOMPARE(after.pixel(x, y), original.pixel(x, y));
            } else {
                QCOMPARE(qAlpha(after.pixel(x, y)), 0);
            }
        }
    }
    // Layout data holds the old background, so it is dropped.
    QVERIFY(!window.m_hasMultiRegionData);
    QVERIFY(window.m_storedRegions.isEmpty());
    QCOMPARE(window.m_cropUndoStack.size(), 1);
    QVERIFY(window.m_toolbarVisible);

    window.handleToolbarUndo();
    QCOMPARE(argb(window.m_originalPixmap.toImage()), original);
    QCOMPARE(window.m_originalPixmap.devicePixelRatio(), dpr);
    QVERIFY(window.m_hasMultiRegionData);
    QCOMPARE(window.m_storedRegions.size(), 1);
    QVERIFY(!window.canUndoCrop());

    window.handleToolbarRedo();
    QCOMPARE(argb(window.m_originalPixmap.toImage()), after);
    QCOMPARE(window.size(), sizeBefore);
}

void TestPinWindowRemoveBackground::annotationsStayEditableAndMosaicsResample()
{
    const QPixmap source = patternPixmap(240, 120);
    PinWindow window(source, QPoint(0, 0), nullptr, false);
    window.showToolbar();
    window.m_foregroundMaskProvider = halfMask;
    AnnotationLayer* layer = window.m_annotationLayer;
    layer->addItem(line(QPoint(10, 10), QPoint(200, 100)));
    // A redaction over the background: after removal it covers transparent
    // pixels, never the original ones.
    layer->addItem(std::make_unique<MosaicRectAnnotation>(
        QRect(150, 20, 60, 60), window.m_sharedSourcePixmap, 8));
    const QImage exportBefore = argb(window.getExportPixmapWithAnnotations().toImage());

    window.removeBackground();
    QTRY_VERIFY(!window.m_backgroundRemovalInProgress);
    QCOMPARE(layer->itemCount(), size_t(2));
    auto* polyline = dynamic_cast<PolylineAnnotation*>(layer->itemAt(0));
    QVERIFY(polyline);
    QCOMPARE(polyline->points(), (QVector<QPoint>{QPoint(10, 10), QPoint(200, 100)}));

    const QImage exportAfter = argb(window.getExportPixmapWithAnnotations().toImage());
    QCOMPARE(exportAfter.size(), exportBefore.size());
    // Inside the mosaic, away from the line, only transparent pixels are left.
    QCOMPARE(qAlpha(exportAfter.pixel(200, 30)), 0);
    QVERIFY(qAlpha(exportBefore.pixel(200, 30)) == 255);

    // A later annotation is undone first, then the removal.
    layer->addItem(line(QPoint(20, 60), QPoint(60, 60)));
    window.updateUndoRedoState();
    window.handleToolbarUndo();
    QCOMPARE(layer->itemCount(), size_t(2));
    QVERIFY(window.canUndoCrop());
    window.handleToolbarUndo();
    QVERIFY(!window.canUndoCrop());
    QCOMPARE(argb(window.getExportPixmapWithAnnotations().toImage()), exportBefore);
}

void TestPinWindowRemoveBackground::failuresLeaveThePinUntouched_data()
{
    QTest::addColumn<int>("status");
    QTest::addColumn<int>("maskFill");
    QTest::newRow("unsupported") << int(Status::Unsupported) << -1;
    QTest::newRow("no subject from segmenter") << int(Status::NoSubject) << -1;
    QTest::newRow("failed") << int(Status::Failed) << -1;
    QTest::newRow("empty mask") << int(Status::Success) << 0;
    QTest::newRow("full mask") << int(Status::Success) << 255;
}

void TestPinWindowRemoveBackground::failuresLeaveThePinUntouched()
{
    QFETCH(int, status);
    QFETCH(int, maskFill);
    const QPixmap source = patternPixmap(120, 80);
    PinWindow window(source, QPoint(0, 0), nullptr, false);
    window.m_foregroundMaskProvider = [status, maskFill](const QImage& image) {
        MaskResult result;
        result.status = Status(status);
        if (maskFill >= 0) {
            result.mask = QImage(image.size(), QImage::Format_Grayscale8);
            result.mask.fill(maskFill);
        }
        return result;
    };

    window.removeBackground();
    QTRY_VERIFY(!window.m_backgroundRemovalInProgress);
    QCOMPARE(argb(window.m_originalPixmap.toImage()), argb(source.toImage()));
    QVERIFY(window.m_cropUndoStack.isEmpty());
}

void TestPinWindowRemoveBackground::staleResultIsDropped()
{
    const QPixmap source = patternPixmap(240, 120);
    PinWindow window(source, QPoint(0, 0), nullptr, false);
    window.showToolbar();
    Gate gate;
    window.m_foregroundMaskProvider = gate.provider();

    window.removeBackground();
    QTRY_COMPARE(gate.calls->load(), 1);
    // The image changes while the worker runs.
    ImageStrip::Request request;
    request.axis = ImageStrip::Axis::Rows;
    request.operation = ImageStrip::Operation::Remove;
    request.start = 30;
    request.length = 30;
    QVERIFY(window.applyStrip(request));
    const QImage stripped = argb(window.m_originalPixmap.toImage());

    gate.semaphore->release();
    QTRY_VERIFY(!window.m_backgroundRemovalInProgress);
    QCOMPARE(argb(window.m_originalPixmap.toImage()), stripped);
    QCOMPARE(window.m_cropUndoStack.size(), 1);
}

void TestPinWindowRemoveBackground::strokeBeingDrawnHoldsTheResult()
{
    const QPixmap source = patternPixmap(240, 120);
    PinWindow window(source, QPoint(0, 0), nullptr, false);
    window.showToolbar();
    Gate gate;
    window.m_foregroundMaskProvider = gate.provider();

    window.removeBackground();
    QTRY_COMPARE(gate.calls->load(), 1);
    // A freehand mosaic over the background, still held down when the
    // removal finishes: it samples the old pixels until it is committed.
    window.handleToolbarToolSelected(static_cast<int>(ToolId::Mosaic));
    window.m_toolManager->handleMousePress(QPointF(150, 30));
    window.m_toolManager->handleMouseMove(QPointF(200, 60));
    QVERIFY(window.m_toolManager->isDrawing());

    gate.semaphore->release();
    QTRY_VERIFY(!window.m_backgroundRemovalInProgress);
    QCOMPARE(argb(window.m_originalPixmap.toImage()), argb(source.toImage()));
    QVERIFY(window.m_cropUndoStack.isEmpty());

    // Nor can one start while the stroke is open.
    window.removeBackground();
    QVERIFY(!window.m_backgroundRemovalInProgress);
    QCOMPARE(gate.calls->load(), 1);
    window.m_toolManager->handleMouseRelease(QPointF(200, 60));
}

void TestPinWindowRemoveBackground::autoBlurStartedMeanwhileHoldsTheResult()
{
    const QPixmap source = patternPixmap(240, 120);
    PinWindow window(source, QPoint(0, 0), nullptr, false);
    Gate gate;
    window.m_foregroundMaskProvider = gate.provider();
    const quint64 autoBlurGeneration = window.m_autoBlurContentGeneration;

    window.removeBackground();
    QTRY_COMPARE(gate.calls->load(), 1);
    window.m_autoBlurInProgress = true;   // started while the worker runs
    gate.semaphore->release();
    QTRY_VERIFY(!window.m_backgroundRemovalInProgress);
    // The auto-blur pass is still current and the pin unchanged.
    QVERIFY(window.isAutoBlurRequestCurrent(autoBlurGeneration));
    QCOMPARE(argb(window.m_originalPixmap.toImage()), argb(source.toImage()));
    QVERIFY(window.m_cropUndoStack.isEmpty());
    window.m_autoBlurInProgress = false;
}

void TestPinWindowRemoveBackground::mergedPinFlattensAndKeepsExportSize()
{
    // Two overlapping regions: blue covers the right part of red. Cutting
    // each region and recomposing them would let red show through blue
    // where the coverage is partial, so the pin is flattened instead.
    QImage red(160, 120, QImage::Format_ARGB32_Premultiplied);
    red.fill(Qt::red);
    QImage blue(160, 120, QImage::Format_ARGB32_Premultiplied);
    blue.fill(Qt::blue);
    QVector<LayoutRegion> regions(2);
    regions[0].rect = QRect(0, 0, 160, 120);
    regions[0].image = red;
    regions[0].index = 1;
    regions[1].rect = QRect(80, 0, 160, 120);
    regions[1].image = blue;
    regions[1].index = 2;
    for (LayoutRegion& region : regions) {
        region.originalRect = region.rect;
    }
    QPixmap source(240, 120);
    source.fill(Qt::blue);
    {
        QPainter painter(&source);
        painter.fillRect(QRect(0, 0, 80, 120), Qt::red);
    }

    PinWindow window(source, QPoint(0, 0), nullptr, false);
    window.setMultiRegionData(regions);
    window.setZoomLevel(0.5);
    window.m_foregroundMaskProvider = halfMask;
    const QSize exportBefore = window.getExportPixmapWithAnnotations().size();
    QCOMPARE(exportBefore, QSize(240, 120));

    window.removeBackground();
    QTRY_VERIFY(!window.m_backgroundRemovalInProgress);
    QCOMPARE(window.m_cropUndoStack.size(), 1);
    // No region copy of the old pixels is kept, and none is cut on the GUI
    // thread; Arrange is unavailable until the step is undone...
    QVERIFY(!window.m_hasMultiRegionData);
    QVERIFY(window.m_storedRegions.isEmpty());
    window.enterRegionLayoutMode();
    QVERIFY(!window.isRegionLayoutMode());
    // ...but Save and Copy still use the native resolution, and they show
    // exactly the cut composed image.
    const QImage cut = argb(window.getExportPixmapWithAnnotations().toImage());
    QCOMPARE(cut.size(), exportBefore);
    QCOMPARE(cut.pixel(40, 60), qRgba(255, 0, 0, 255));
    QCOMPARE(cut.pixel(100, 60), qRgba(0, 0, 255, 255));
    QCOMPARE(qAlpha(cut.pixel(200, 60)), 0);

    window.handleToolbarUndo();
    QVERIFY(window.m_hasMultiRegionData);
    QCOMPARE(window.m_storedRegions.size(), 2);
    QCOMPARE(window.getExportPixmapWithAnnotations().size(), exportBefore);
    window.handleToolbarRedo();
    QVERIFY(!window.m_hasMultiRegionData);
    QCOMPARE(argb(window.getExportPixmapWithAnnotations().toImage()), cut);
}

void TestPinWindowRemoveBackground::escapeCancelsWithoutClosing()
{
    const QPixmap source = patternPixmap(240, 120);
    PinWindow window(source, QPoint(0, 0), nullptr, false);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    Gate gate;
    window.m_foregroundMaskProvider = gate.provider();

    window.removeBackground();
    QTRY_COMPARE(gate.calls->load(), 1);
    pressKey(window, Qt::Key_Escape);
    QVERIFY(!window.m_backgroundRemovalInProgress);
    QVERIFY(window.isVisible());

    gate.semaphore->release();
    QTest::qWait(100);
    QCoreApplication::processEvents();
    QCOMPARE(argb(window.m_originalPixmap.toImage()), argb(source.toImage()));
    QVERIFY(window.m_cropUndoStack.isEmpty());

    // It can run again afterwards.
    gate.semaphore->release();
    window.removeBackground();
    QTRY_VERIFY(!window.m_backgroundRemovalInProgress);
    QCOMPARE(window.m_cropUndoStack.size(), 1);
}

void TestPinWindowRemoveBackground::closingWhileRunningIsSafe()
{
    Gate gate;
    {
        PinWindow window(patternPixmap(240, 120), QPoint(0, 0), nullptr, false);
        window.m_foregroundMaskProvider = gate.provider();
        window.removeBackground();
        QTRY_COMPARE(gate.calls->load(), 1);
        // Destroyed while the worker is still inside the segmenter.
    }
    gate.semaphore->release();
    QTest::qWait(100);
    QCoreApplication::processEvents();
}

void TestPinWindowRemoveBackground::refusedDuringLiveUpdateAndAutoBlur()
{
    const QPixmap source = patternPixmap(120, 80);
    PinWindow window(source, QPoint(0, 0), nullptr, false);
    auto calls = std::make_shared<std::atomic_int>(0);
    window.m_foregroundMaskProvider = [calls](const QImage& image) {
        ++*calls;
        return halfMask(image);
    };

    window.m_isLiveMode = true;
    window.removeBackground();
    QVERIFY(!window.m_backgroundRemovalInProgress);
    window.m_isLiveMode = false;

    // A redaction pass still running would not be part of the snapshot.
    window.m_autoBlurInProgress = true;
    window.removeBackground();
    QVERIFY(!window.m_backgroundRemovalInProgress);
    window.m_autoBlurInProgress = false;

    QTest::qWait(50);
    QCOMPARE(calls->load(), 0);
    QCOMPARE(argb(window.m_originalPixmap.toImage()), argb(source.toImage()));

    window.removeBackground();
    QTRY_VERIFY(!window.m_backgroundRemovalInProgress);
    QCOMPARE(calls->load(), 1);
    QCOMPARE(window.m_cropUndoStack.size(), 1);
}

void TestPinWindowRemoveBackground::exportKeepsTransparency()
{
    const QPixmap source = patternPixmap(240, 120, 2.0);
    PinWindow window(source, QPoint(0, 0), nullptr, false);
    window.m_foregroundMaskProvider = halfMask;
    window.removeBackground();
    QTRY_VERIFY(!window.m_backgroundRemovalInProgress);

    const QImage exported = argb(window.getExportPixmapWithAnnotations().toImage());
    QVERIFY(exported.hasAlphaChannel());
    QCOMPARE(qAlpha(exported.pixel(exported.width() / 4, exported.height() / 2)), 255);
    QCOMPARE(qAlpha(exported.pixel(exported.width() * 3 / 4, exported.height() / 2)), 0);
}

void TestPinWindowRemoveBackground::menuActionFollowsCapability()
{
    PinWindow window(patternPixmap(120, 80), QPoint(0, 0), nullptr, false, false);
    window.createContextMenu();
    QAction* action = nullptr;
    for (QAction* candidate : window.m_contextMenu->findChildren<QAction*>()) {
        if (candidate->objectName() == QLatin1String("pinMenuRemoveBackground")) {
            action = candidate;
        }
    }
    QCOMPARE(action != nullptr,
             PlatformFeatures::instance().capabilities().supportsForegroundCutout);
}

QTEST_MAIN(TestPinWindowRemoveBackground)
#include "tst_RemoveBackground.moc"
