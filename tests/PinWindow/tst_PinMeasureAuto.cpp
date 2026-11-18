#include <QtTest/QtTest>
#include <QImage>
#include <QMouseEvent>

#include "PinWindow.h"

#include "qml/PinToolOptionsViewModel.h"
#include "qml/QmlFloatingSubToolbar.h"
#include "settings/PinWindowSettingsManager.h"
#include "tools/ToolManager.h"
#include "tools/handlers/MeasureToolHandler.h"

// #348: the Measure tool's auto mode in a real pin: mode switch and setting,
// hover routing, image pixels under zoom / rotation / device pixel ratio, and
// one cached image for all hovers.
class TestPinWindowMeasure : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void modeSwitchIsSavedAndShown();
    void hoverMeasuresImagePixels_data();
    void hoverMeasuresImagePixels();
    void hoverBuildsTheImageOnceAndAgainOnlyWhenItChanges();
    void leavingThePinOrManualModeHidesTheSpans();
    void hiddenToolbarOrLiveUpdateStopsHovering();

private:
    static QPixmap elementPixmap(qreal dpr);
    static void hover(PinWindow& window, const QPoint& displayPos);
};

namespace {

// 300 x 200 device pixels with a 120 x 80 px element at (60, 40).
constexpr int kImageWidth = 300;
constexpr int kImageHeight = 200;
const QRect kElement(60, 40, 120, 80);

} // namespace

QPixmap TestPinWindowMeasure::elementPixmap(qreal dpr)
{
    QImage image(kImageWidth, kImageHeight, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    for (int y = kElement.top(); y <= kElement.bottom(); ++y) {
        for (int x = kElement.left(); x <= kElement.right(); ++x) {
            image.setPixelColor(x, y, QColor(20, 120, 60));
        }
    }
    QPixmap pixmap = QPixmap::fromImage(image);
    pixmap.setDevicePixelRatio(dpr);
    return pixmap;
}

void TestPinWindowMeasure::hover(PinWindow& window, const QPoint& displayPos)
{
    QMouseEvent event(QEvent::MouseMove, QPointF(displayPos), QPointF(window.mapToGlobal(displayPos)),
                      Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&window, &event);
}

void TestPinWindowMeasure::init()
{
    PinWindowSettingsManager::instance().saveMeasureAutoMode(false);
}

void TestPinWindowMeasure::modeSwitchIsSavedAndShown()
{
    PinWindow window(elementPixmap(1.0), QPoint(0, 0), nullptr, false);
    window.showToolbar();
    window.handleToolbarToolSelected(static_cast<int>(ToolId::Measure));
    MeasureToolHandler* tool = window.measureToolHandler();
    auto* options = window.m_subToolbar->viewModel();
    QVERIFY(tool);

    // Manual stays the default.
    QVERIFY(options->showMeasureSection());
    QVERIFY(!options->showCutOutSection());
    QCOMPARE(options->measureMode(), int(PinToolOptionsViewModel::MeasureManual));
    QVERIFY(!tool->autoMode());

    options->handleMeasureModeSelected(PinToolOptionsViewModel::MeasureAuto);
    QVERIFY(tool->autoMode());
    QCOMPARE(options->measureMode(), int(PinToolOptionsViewModel::MeasureAuto));
    QVERIFY(PinWindowSettingsManager::instance().loadMeasureAutoMode());

    // A new pin starts in the saved mode.
    PinWindow second(elementPixmap(1.0), QPoint(0, 0), nullptr, false);
    second.showToolbar();
    second.handleToolbarToolSelected(static_cast<int>(ToolId::Measure));
    QVERIFY(second.measureToolHandler()->autoMode());
    QCOMPARE(second.m_subToolbar->viewModel()->measureMode(), int(PinToolOptionsViewModel::MeasureAuto));

    options->handleMeasureModeSelected(PinToolOptionsViewModel::MeasureManual);
    QVERIFY(!tool->autoMode());
    QVERIFY(!PinWindowSettingsManager::instance().loadMeasureAutoMode());

    window.handleToolbarToolSelected(static_cast<int>(ToolId::Pencil));
    QVERIFY(!options->showMeasureSection());
}

void TestPinWindowMeasure::hoverMeasuresImagePixels_data()
{
    QTest::addColumn<qreal>("dpr");
    QTest::addColumn<qreal>("zoom");
    QTest::addColumn<int>("rotations");
    QTest::newRow("1x") << 1.0 << 1.0 << 0;
    QTest::newRow("retina 2x") << 2.0 << 1.0 << 0;
    QTest::newRow("windows 150%") << 1.5 << 1.0 << 0;
    QTest::newRow("zoomed 200%") << 1.0 << 2.0 << 0;
    QTest::newRow("2x rotated") << 2.0 << 1.0 << 1;
    QTest::newRow("1.25x rotated twice, zoomed") << 1.25 << 1.5 << 2;
}

void TestPinWindowMeasure::hoverMeasuresImagePixels()
{
    QFETCH(qreal, dpr);
    QFETCH(qreal, zoom);
    QFETCH(int, rotations);

    PinWindow window(elementPixmap(dpr), QPoint(0, 0), nullptr, false);
    window.showToolbar();
    if (!qFuzzyCompare(zoom, 1.0)) {
        window.setZoomLevel(zoom);
    }
    for (int i = 0; i < rotations; ++i) {
        window.rotateRight();
    }
    window.handleToolbarToolSelected(static_cast<int>(ToolId::Measure));
    window.m_subToolbar->viewModel()->handleMeasureModeSelected(PinToolOptionsViewModel::MeasureAuto);
    MeasureToolHandler* tool = window.measureToolHandler();
    QVERIFY(tool->autoMode());
    QCOMPARE(window.cropSourcePixelSize(), QSize(kImageWidth, kImageHeight));

    // The element's middle, from image pixels to tool units to the screen.
    const QSize toolSize = window.cropToolImageSize();
    const QPointF toolPos(kElement.center().x() * toolSize.width() / qreal(kImageWidth),
                          kElement.center().y() * toolSize.height() / qreal(kImageHeight));
    hover(window, window.mapFromOriginalCoords(toolPos).toPoint());

    QVERIFY(tool->hasHoverMeasurement());
    const auto horizontal = tool->hoverSpan(Qt::Horizontal);
    const auto vertical = tool->hoverSpan(Qt::Vertical);
    QVERIFY(horizontal && vertical);
    // Spans are in the image's own pixels and orientation, whatever the view.
    QCOMPARE(horizontal->first, kElement.left());
    QCOMPARE(horizontal->length(), kElement.width());
    QCOMPARE(vertical->first, kElement.top());
    QCOMPARE(vertical->length(), kElement.height());
    QCOMPARE(tool->imageDevicePixelRatio(), dpr);
}

// Hovers must not rebuild the image: one copy, reused until the image changes.
void TestPinWindowMeasure::hoverBuildsTheImageOnceAndAgainOnlyWhenItChanges()
{
    PinWindow window(elementPixmap(1.0), QPoint(0, 0), nullptr, false);
    window.showToolbar();
    window.handleToolbarToolSelected(static_cast<int>(ToolId::Measure));
    window.m_subToolbar->viewModel()->handleMeasureModeSelected(PinToolOptionsViewModel::MeasureAuto);
    MeasureToolHandler* tool = window.measureToolHandler();

    // Across the whole pin, ending inside the element.
    for (int x = 290; x > 100; x -= 7) {
        hover(window, QPoint(x, 80));
    }
    QVERIFY(tool->hasHoverMeasurement());
    QCOMPARE(window.m_measureRasterBuildCount, 1);
    const std::shared_ptr<const QImage> raster = window.measureRaster();
    QCOMPARE(raster->size(), QSize(kImageWidth, kImageHeight));
    qInfo("measure image: %lld bytes for %dx%d", static_cast<long long>(raster->sizeInBytes()),
          raster->width(), raster->height());

    // Zoom only changes the view: the same image stays.
    window.setZoomLevel(1.5);
    hover(window, QPoint(150, 120));
    QCOMPARE(window.m_measureRasterBuildCount, 1);

    // A changed image (a crop) is read again, once.
    QVERIFY(window.applyCropSourcePixels(QRect(50, 30, 200, 120)));
    window.handleToolbarToolSelected(static_cast<int>(ToolId::Measure));
    QCOMPARE(window.cropSourcePixelSize(), QSize(200, 120));
    for (int x = 10; x < 150; x += 9) {
        hover(window, QPoint(x, 40));
    }
    QCOMPARE(window.m_measureRasterBuildCount, 2);
    QVERIFY(tool->hasHoverMeasurement());
    QCOMPARE(tool->hoverSpan(Qt::Horizontal)->last, kElement.right() - 50);
}

void TestPinWindowMeasure::leavingThePinOrManualModeHidesTheSpans()
{
    PinWindow window(elementPixmap(1.0), QPoint(0, 0), nullptr, false);
    window.showToolbar();
    window.handleToolbarToolSelected(static_cast<int>(ToolId::Measure));
    auto* options = window.m_subToolbar->viewModel();
    MeasureToolHandler* tool = window.measureToolHandler();

    // Manual: hovering measures nothing.
    hover(window, kElement.center());
    QVERIFY(!tool->hasHoverMeasurement());
    QCOMPARE(window.m_measureRasterBuildCount, 0);

    options->handleMeasureModeSelected(PinToolOptionsViewModel::MeasureAuto);
    hover(window, kElement.center());
    QVERIFY(tool->hasHoverMeasurement());

    QEvent leave(QEvent::Leave);
    QCoreApplication::sendEvent(&window, &leave);
    QVERIFY(!tool->hasHoverMeasurement());

    hover(window, kElement.center());
    QVERIFY(tool->hasHoverMeasurement());
    options->handleMeasureModeSelected(PinToolOptionsViewModel::MeasureManual);
    QVERIFY(!tool->hasHoverMeasurement());
    // Manual mode does not keep the image copy around.
    QVERIFY(!window.m_measureRaster);

    options->handleMeasureModeSelected(PinToolOptionsViewModel::MeasureAuto);
    hover(window, kElement.center());
    QVERIFY(window.m_measureRaster);
    window.handleToolbarToolSelected(static_cast<int>(ToolId::Pencil));
    QVERIFY(!window.m_measureRaster);
}

void TestPinWindowMeasure::hiddenToolbarOrLiveUpdateStopsHovering()
{
    PinWindow window(elementPixmap(1.0), QPoint(0, 0), nullptr, false);
    window.showToolbar();
    window.handleToolbarToolSelected(static_cast<int>(ToolId::Measure));
    window.m_subToolbar->viewModel()->handleMeasureModeSelected(PinToolOptionsViewModel::MeasureAuto);
    MeasureToolHandler* tool = window.measureToolHandler();

    hover(window, kElement.center());
    QVERIFY(tool->hasHoverMeasurement());
    QVERIFY(window.m_measureRaster);

    // Leaving annotation mode keeps Measure selected but drops what it showed.
    window.exitAnnotationMode(false);
    QVERIFY(!tool->hasHoverMeasurement());
    QVERIFY(!window.m_measureRaster);

    // While Live Update runs each frame is a new image: no hover scanning.
    window.handleToolbarToolSelected(static_cast<int>(ToolId::Measure));
    QVERIFY(window.isMeasureHoverActive());
    window.m_isLiveMode = true;
    window.m_livePaused = false;
    QVERIFY(!window.isMeasureHoverActive());
    hover(window, kElement.center());
    QVERIFY(!tool->hasHoverMeasurement());
    window.m_livePaused = true;
    QVERIFY(window.isMeasureHoverActive());
    window.m_isLiveMode = false;
}

QTEST_MAIN(TestPinWindowMeasure)
#include "tst_PinMeasureAuto.moc"
