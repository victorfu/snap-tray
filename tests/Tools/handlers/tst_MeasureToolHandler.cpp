#include <QtTest/QtTest>

#include "tools/ToolContext.h"
#include "tools/handlers/MeasureToolHandler.h"

#include <QFont>
#include <QFontMetrics>
#include <QImage>
#include <QPainter>

class TestMeasureToolHandler : public QObject
{
    Q_OBJECT

private slots:
    void testToolId();
    void testConstrainEndpoint_NoShift();
    void testConstrainEndpoint_ShiftHorizontal();
    void testConstrainEndpoint_ShiftVertical();
    void testConstrainEndpoint_ShiftDiagonalEqual();

    // #348: image-based auto mode.
    void manualLabelIsUnchangedAtNativeScale();
    void manualLabelUsesImagePixelsAndLogicalUnits();
    void formatLengthNamesBothUnits_data();
    void formatLengthNamesBothUnits();
    void manualModeIgnoresHover();
    void autoHoverMeasuresTheElementInImagePixels_data();
    void autoHoverMeasuresTheElementInImagePixels();
    void autoHoverShowsNothingWithoutReliableEdges();
    void autoHoverReusesTheImageAndRescansOnlyOnNewPixels();
    void autoDragStillMeasuresManuallyWithShiftAndEscape();
    void geometryChangeOrLeavingClearsTheHover();
    void hoverOverlayPaints();
    void fractionalPointerReachesEveryImagePixel();
    void replacedImageOfTheSameSizeClearsTheHover();
    void completedLineKeepsItsPixelsWhenThePinZooms();
    void dragKeepsFractionalPointerPositions();
    void hoverLabelsStayInsideThePin();
};

namespace {

// 200 x 100 image: white, with a 80 x 40 px element at (40, 20).
QImage elementImage()
{
    QImage image(200, 100, QImage::Format_ARGB32);
    image.fill(Qt::white);
    for (int y = 20; y < 60; ++y) {
        for (int x = 40; x < 120; ++x) {
            image.setPixelColor(x, y, QColor(30, 90, 200));
        }
    }
    return image;
}

struct Host {
    ToolContext context;
    int repaints = 0;
    int rasterCalls = 0;
    std::shared_ptr<const QImage> raster;

    Host()
    {
        context.requestRepaint = [this]() { ++repaints; };
    }

    MeasureToolHandler::RasterProvider provider()
    {
        return [this]() {
            ++rasterCalls;
            return raster;
        };
    }
};

} // namespace

void TestMeasureToolHandler::testToolId()
{
    MeasureToolHandler handler;
    QCOMPARE(handler.toolId(), ToolId::Measure);
}

void TestMeasureToolHandler::testConstrainEndpoint_NoShift()
{
    MeasureToolHandler handler;
    QPointF start(10, 10);
    QPointF end(50, 30);

    QPointF result = handler.constrainEndpoint(start, end, false);
    QCOMPARE(result, end);
}

void TestMeasureToolHandler::testConstrainEndpoint_ShiftHorizontal()
{
    MeasureToolHandler handler;
    QPointF start(10, 10);
    QPointF end(50, 20);  // dx=40 > dy=10 -> horizontal

    QPointF result = handler.constrainEndpoint(start, end, true);
    QCOMPARE(result, QPointF(50, 10));
}

void TestMeasureToolHandler::testConstrainEndpoint_ShiftVertical()
{
    MeasureToolHandler handler;
    QPointF start(10, 10);
    QPointF end(20, 50);  // dx=10 < dy=40 -> vertical

    QPointF result = handler.constrainEndpoint(start, end, true);
    QCOMPARE(result, QPointF(10, 50));
}

void TestMeasureToolHandler::testConstrainEndpoint_ShiftDiagonalEqual()
{
    MeasureToolHandler handler;
    QPointF start(10, 10);
    QPointF end(30, 30);  // dx=20 == dy=20 -> dx >= dy -> horizontal

    QPointF result = handler.constrainEndpoint(start, end, true);
    QCOMPARE(result, QPointF(30, 10));
}

void TestMeasureToolHandler::manualLabelIsUnchangedAtNativeScale()
{
    // The text the tool always showed for a 40 x 30 drag at 100%.
    QCOMPARE(MeasureToolHandler::manualLabelLines(QPointF(40, -30), 1.0),
             QStringList{QStringLiteral("dx: 40  dy: 30  d: 50.0 px")});
}

void TestMeasureToolHandler::manualLabelUsesImagePixelsAndLogicalUnits()
{
    // Retina: 80 x 60 device pixels are 40 x 30 points.
    QCOMPARE(MeasureToolHandler::manualLabelLines(QPointF(80, 60), 2.0),
             (QStringList{QStringLiteral("dx: 80  dy: 60  d: 100.0 px"),
                          QStringLiteral("dx: 40  dy: 30  d: 50.0 pt")}));
    // Windows 150%: logical values may be fractional.
    QCOMPARE(MeasureToolHandler::manualLabelLines(QPointF(45, 0), 1.5).last(),
             QStringLiteral("dx: 30  dy: 0  d: 30.0 pt"));
    QCOMPARE(MeasureToolHandler::manualLabelLines(QPointF(10, 0), 1.5).last(),
             QStringLiteral("dx: 6.7  dy: 0  d: 6.7 pt"));

    // A drag on a pin zoomed to 200% counts image pixels, not screen ones.
    MeasureToolHandler handler;
    Host host;
    handler.setImageGeometry(QSize(400, 200), QSize(200, 100), 1.0);
    handler.onMousePress(&host.context, QPoint(10, 10));
    handler.onMouseMove(&host.context, QPoint(90, 70));
    handler.onMouseRelease(&host.context, QPoint(90, 70));
    QVERIFY(handler.m_hasCompletedMeasurement);
    QCOMPARE(handler.pixelsPerToolUnit(), QPointF(0.5, 0.5));
}

void TestMeasureToolHandler::formatLengthNamesBothUnits_data()
{
    QTest::addColumn<int>("pixels");
    QTest::addColumn<qreal>("dpr");
    QTest::addColumn<QString>("expected");
    const QString dot(QChar(0x00B7));
    QTest::newRow("1x") << 80 << 1.0 << QStringLiteral("80 px");
    QTest::newRow("2x") << 80 << 2.0 << QStringLiteral("80 px %1 40 pt").arg(dot);
    QTest::newRow("1.5x whole") << 81 << 1.5 << QStringLiteral("81 px %1 54 pt").arg(dot);
    QTest::newRow("1.25x fraction") << 81 << 1.25 << QStringLiteral("81 px %1 64.8 pt").arg(dot);
}

void TestMeasureToolHandler::formatLengthNamesBothUnits()
{
    QFETCH(int, pixels);
    QFETCH(qreal, dpr);
    QFETCH(QString, expected);
    QCOMPARE(MeasureToolHandler::formatLength(pixels, dpr), expected);
}

void TestMeasureToolHandler::manualModeIgnoresHover()
{
    MeasureToolHandler handler;
    Host host;
    host.raster = std::make_shared<const QImage>(elementImage());
    handler.setImageGeometry(QSize(200, 100), QSize(200, 100), 1.0);
    handler.setRasterProvider(host.provider());

    handler.onMouseMove(&host.context, QPoint(80, 40));
    QVERIFY(!handler.hasHoverMeasurement());
    QCOMPARE(host.rasterCalls, 0);
    QCOMPARE(host.repaints, 0);
}

void TestMeasureToolHandler::autoHoverMeasuresTheElementInImagePixels_data()
{
    // Tool size for the 200 x 100 px image: zoom and device pixel ratio
    // (Retina, 150%, zoomed pins) must not change the values.
    QTest::addColumn<QSize>("toolSize");
    QTest::addColumn<qreal>("dpr");
    QTest::newRow("1x") << QSize(200, 100) << 1.0;
    QTest::newRow("retina 2x") << QSize(100, 50) << 2.0;
    QTest::newRow("windows 150%") << QSize(133, 67) << 1.5;
    QTest::newRow("zoomed 250%") << QSize(500, 250) << 1.0;
}

void TestMeasureToolHandler::autoHoverMeasuresTheElementInImagePixels()
{
    QFETCH(QSize, toolSize);
    QFETCH(qreal, dpr);

    MeasureToolHandler handler;
    Host host;
    host.raster = std::make_shared<const QImage>(elementImage());
    handler.setAutoMode(true);
    handler.setImageGeometry(toolSize, QSize(200, 100), dpr);
    handler.setRasterProvider(host.provider());

    // The element's middle, in tool coordinates.
    const QPoint toolPos(toolSize.width() * 80 / 200, toolSize.height() * 40 / 100);
    handler.onMouseMove(&host.context, toolPos);
    QVERIFY(handler.hasHoverMeasurement());
    QVERIFY(host.repaints > 0);
    const auto horizontal = handler.hoverSpan(Qt::Horizontal);
    const auto vertical = handler.hoverSpan(Qt::Vertical);
    QVERIFY(horizontal && vertical);
    QCOMPARE(horizontal->first, 40);
    QCOMPARE(horizontal->length(), 80);
    QCOMPARE(vertical->first, 20);
    QCOMPARE(vertical->length(), 40);
}

// Blank space and gradients give no number at all, rather than a made-up one.
void TestMeasureToolHandler::autoHoverShowsNothingWithoutReliableEdges()
{
    MeasureToolHandler handler;
    Host host;
    QImage blank(200, 100, QImage::Format_ARGB32);
    blank.fill(QColor(240, 240, 240));
    host.raster = std::make_shared<const QImage>(blank);
    handler.setAutoMode(true);
    handler.setImageGeometry(QSize(200, 100), QSize(200, 100), 1.0);
    handler.setRasterProvider(host.provider());

    handler.onMouseMove(&host.context, QPoint(100, 50));
    QVERIFY(!handler.hasHoverMeasurement());

    // Between the element and the image border: the border is where the
    // capture was cut, so no width is claimed (and the column is blank).
    host.raster = std::make_shared<const QImage>(elementImage());
    handler.onMouseMove(&host.context, QPoint(10, 40));
    QVERIFY(!handler.hasHoverMeasurement());

    // Only an axis with edges on both sides is shown: between two full-height
    // bars the gap is measured, the blank column is not.
    QImage bars(200, 100, QImage::Format_ARGB32);
    bars.fill(Qt::white);
    for (int y = 0; y < 100; ++y) {
        for (int x : {20, 21, 22, 150, 151}) {
            bars.setPixelColor(x, y, Qt::black);
        }
    }
    host.raster = std::make_shared<const QImage>(bars);
    handler.onMouseMove(&host.context, QPoint(80, 40));
    QVERIFY(handler.hoverSpan(Qt::Horizontal));
    QVERIFY(!handler.hoverSpan(Qt::Vertical));
    QCOMPARE(handler.hoverSpan(Qt::Horizontal)->first, 23);
    QCOMPARE(handler.hoverSpan(Qt::Horizontal)->last, 149);

    // A raster that does not match the geometry is never scanned.
    host.raster = std::make_shared<const QImage>(elementImage().scaled(100, 50));
    handler.onMouseMove(&host.context, QPoint(80, 40));
    QVERIFY(!handler.hasHoverMeasurement());

    // Outside the image.
    host.raster = std::make_shared<const QImage>(elementImage());
    handler.onMouseMove(&host.context, QPoint(250, 40));
    QVERIFY(!handler.hasHoverMeasurement());
}

void TestMeasureToolHandler::autoHoverReusesTheImageAndRescansOnlyOnNewPixels()
{
    MeasureToolHandler handler;
    Host host;
    host.raster = std::make_shared<const QImage>(elementImage());
    handler.setAutoMode(true);
    // Zoomed 200%: two tool units per image pixel.
    handler.setImageGeometry(QSize(400, 200), QSize(200, 100), 1.0);
    handler.setRasterProvider(host.provider());

    handler.onMouseMove(&host.context, QPoint(160, 80));
    const int repaintsAfterFirst = host.repaints;
    // Same image pixel: nothing rescanned or repainted.
    handler.onMouseMove(&host.context, QPoint(161, 81));
    QCOMPARE(host.repaints, repaintsAfterFirst);
    QCOMPARE(handler.m_hoverPixel, QPoint(80, 40));

    // The host hands out the same cached image on every move.
    const QImage* image = host.raster.get();
    for (int x = 100; x < 220; x += 3) {
        handler.onMouseMove(&host.context, QPoint(x, 80));
    }
    QCOMPARE(handler.m_hoverRaster.get(), image);
    QCOMPARE(host.raster.use_count(), 2);   // the host and the handler, no copies
}

void TestMeasureToolHandler::autoDragStillMeasuresManuallyWithShiftAndEscape()
{
    MeasureToolHandler handler;
    Host host;
    host.raster = std::make_shared<const QImage>(elementImage());
    handler.setAutoMode(true);
    handler.setImageGeometry(QSize(200, 100), QSize(200, 100), 1.0);
    handler.setRasterProvider(host.provider());
    handler.onMouseMove(&host.context, QPoint(80, 40));
    QVERIFY(handler.hasHoverMeasurement());

    host.context.shiftPressed = true;
    handler.onMousePress(&host.context, QPoint(10, 10));
    QVERIFY(handler.isDrawing());
    QVERIFY(!handler.hasHoverMeasurement());
    handler.onMouseMove(&host.context, QPoint(60, 20));
    QCOMPARE(handler.m_endPoint, QPointF(60, 10));
    QVERIFY(handler.handleEscape(&host.context));   // cancels the drag
    QVERIFY(!handler.isDrawing());

    handler.onMousePress(&host.context, QPoint(10, 10));
    handler.onMouseRelease(&host.context, QPoint(60, 20));
    QVERIFY(handler.m_hasCompletedMeasurement);
    QCOMPARE(handler.m_completedEnd, QPointF(60, 10));

    // Hovering again shows spans next to the finished line.
    host.context.shiftPressed = false;
    handler.onMouseMove(&host.context, QPoint(80, 40));
    QVERIFY(handler.hasHoverMeasurement());
    QVERIFY(handler.handleEscape(&host.context));    // clears the line
    QVERIFY(!handler.m_hasCompletedMeasurement);
    // Hover spans never swallow Escape: it goes on to the host.
    QVERIFY(!handler.handleEscape(&host.context));
}

void TestMeasureToolHandler::geometryChangeOrLeavingClearsTheHover()
{
    MeasureToolHandler handler;
    Host host;
    host.raster = std::make_shared<const QImage>(elementImage());
    handler.setAutoMode(true);
    handler.setImageGeometry(QSize(200, 100), QSize(200, 100), 1.0);
    handler.setRasterProvider(host.provider());

    handler.onMouseMove(&host.context, QPoint(80, 40));
    QVERIFY(handler.hasHoverMeasurement());
    handler.setImageGeometry(QSize(400, 200), QSize(200, 100), 1.0);
    QVERIFY(!handler.hasHoverMeasurement());

    handler.onMouseMove(&host.context, QPoint(160, 80));
    QVERIFY(handler.clearHover());
    QVERIFY(!handler.clearHover());

    handler.onMouseMove(&host.context, QPoint(160, 80));
    handler.setAutoMode(false);
    QVERIFY(!handler.hasHoverMeasurement());

    handler.setAutoMode(true);
    handler.onMouseMove(&host.context, QPoint(160, 80));
    handler.onDeactivate(&host.context);
    QVERIFY(!handler.hasHoverMeasurement());
}

void TestMeasureToolHandler::hoverOverlayPaints()
{
    MeasureToolHandler handler;
    Host host;
    host.raster = std::make_shared<const QImage>(elementImage());
    handler.setAutoMode(true);
    handler.setImageGeometry(QSize(200, 100), QSize(200, 100), 2.0);
    handler.setRasterProvider(host.provider());
    handler.onMouseMove(&host.context, QPoint(80, 40));

    QImage canvas(200, 100, QImage::Format_ARGB32);
    canvas.fill(Qt::transparent);
    {
        QPainter painter(&canvas);
        handler.drawPreview(painter);
    }
    // Something was drawn along the hover row.
    bool painted = false;
    for (int x = 40; x < 120 && !painted; ++x) {
        painted = qAlpha(canvas.pixel(x, 40)) > 0;
    }
    QVERIFY(painted);
}

// At 2x one tool unit covers two image pixels; the pointer's fraction picks
// the pixel, so a 1px line at an odd device x can still be hovered.
void TestMeasureToolHandler::fractionalPointerReachesEveryImagePixel()
{
    QImage image(200, 100, QImage::Format_ARGB32);
    image.fill(Qt::white);
    for (int y = 0; y < 100; ++y) {
        image.setPixelColor(51, y, Qt::black);
    }
    MeasureToolHandler handler;
    Host host;
    host.raster = std::make_shared<const QImage>(image);
    handler.setAutoMode(true);
    handler.setImageGeometry(QSize(100, 50), QSize(200, 100), 2.0);
    handler.setRasterProvider(host.provider());

    // Tool x 25.6 is image x 51.2: on the line. Rounding it to 26 first would
    // land on image x 52, beside it.
    handler.onMouseMoveF(&host.context, QPointF(25.6, 20.0));
    QCOMPARE(handler.m_hoverPixel, QPoint(51, 40));
    QVERIFY(handler.hoverSpan(Qt::Horizontal));
    QCOMPARE(handler.hoverSpan(Qt::Horizontal)->length(), 1);
    QCOMPARE(MeasureToolHandler::formatLength(1, 2.0).left(4), QStringLiteral("1 px"));

    // The left half of the same tool unit is image x 50, beside the line.
    handler.onMouseMoveF(&host.context, QPointF(25.4, 20.0));
    QCOMPARE(handler.m_hoverPixel, QPoint(50, 40));
}

// Same size, new pixels (Remove Background, an undo): old spans are dropped,
// and leaving the tool lets the host free its image.
void TestMeasureToolHandler::replacedImageOfTheSameSizeClearsTheHover()
{
    MeasureToolHandler handler;
    Host host;
    int releases = 0;
    host.raster = std::make_shared<const QImage>(elementImage());
    handler.setAutoMode(true);
    handler.setImageGeometry(QSize(200, 100), QSize(200, 100), 1.0, 1);
    handler.setRasterProvider(host.provider(), [&releases]() { ++releases; });

    handler.onMouseMove(&host.context, QPoint(80, 40));
    QVERIFY(handler.hasHoverMeasurement());
    handler.setImageGeometry(QSize(200, 100), QSize(200, 100), 1.0, 1);
    QVERIFY(handler.hasHoverMeasurement());
    handler.setImageGeometry(QSize(200, 100), QSize(200, 100), 1.0, 2);
    QVERIFY(!handler.hasHoverMeasurement());

    handler.onMouseMove(&host.context, QPoint(80, 40));
    handler.onDeactivate(&host.context);
    QCOMPARE(releases, 1);
    QVERIFY(!handler.m_hoverRaster);
    handler.setAutoMode(false);
    QCOMPARE(releases, 2);
}

// The finished line is kept in image pixels: zooming moves it with the image
// instead of changing what it measures. A different image or crop drops it.
void TestMeasureToolHandler::completedLineKeepsItsPixelsWhenThePinZooms()
{
    MeasureToolHandler handler;
    Host host;
    handler.setImageGeometry(QSize(200, 100), QSize(200, 100), 1.0, 1);
    handler.onMousePress(&host.context, QPoint(20, 50));
    handler.onMouseMove(&host.context, QPoint(120, 50));
    handler.onMouseRelease(&host.context, QPoint(120, 50));
    QVERIFY(handler.m_hasCompletedMeasurement);
    QCOMPARE(handler.m_completedEnd - handler.m_completedStart, QPointF(100, 0));

    // Zoomed to 200% after the drag: still 100 image pixels, drawn twice as long.
    handler.setImageGeometry(QSize(400, 200), QSize(200, 100), 1.0, 1);
    QVERIFY(handler.m_hasCompletedMeasurement);
    QCOMPARE(handler.m_completedEnd - handler.m_completedStart, QPointF(100, 0));
    QCOMPARE(MeasureToolHandler::manualLabelLines(handler.m_completedEnd - handler.m_completedStart, 1.0),
             QStringList{QStringLiteral("dx: 100  dy: 0  d: 100.0 px")});
    QCOMPARE(handler.toTool(handler.m_completedEnd) - handler.toTool(handler.m_completedStart),
             QPointF(200, 0));

    // A crop changes the pixel grid: the old line no longer means anything.
    handler.setImageGeometry(QSize(150, 100), QSize(150, 100), 1.0, 1);
    QVERIFY(!handler.m_hasCompletedMeasurement);

    // So does another image of the same size.
    handler.onMousePress(&host.context, QPoint(20, 50));
    handler.onMouseRelease(&host.context, QPoint(120, 50));
    QVERIFY(handler.m_hasCompletedMeasurement);
    handler.setImageGeometry(QSize(150, 100), QSize(150, 100), 1.0, 2);
    QVERIFY(!handler.m_hasCompletedMeasurement);
}

// At 2x and 100% a drag from logical x 10.5 to 20 covers device pixels 21 to
// 40: 19 px. Rounding the start to 11 first would report 18.
void TestMeasureToolHandler::dragKeepsFractionalPointerPositions()
{
    MeasureToolHandler handler;
    Host host;
    handler.setImageGeometry(QSize(100, 50), QSize(200, 100), 2.0);
    handler.onMousePressF(&host.context, QPointF(10.5, 5.0));
    handler.onMouseMoveF(&host.context, QPointF(15.25, 5.0));
    QCOMPARE(handler.m_endPoint, QPointF(30.5, 10.0));
    handler.onMouseReleaseF(&host.context, QPointF(20.0, 5.0));
    QVERIFY(handler.m_hasCompletedMeasurement);
    QCOMPARE(handler.m_completedStart, QPointF(21.0, 10.0));
    QCOMPARE(handler.m_completedEnd, QPointF(40.0, 10.0));
    QCOMPARE(MeasureToolHandler::manualLabelLines(handler.m_completedEnd - handler.m_completedStart, 2.0)
                 .first(),
             QStringLiteral("dx: 19  dy: 0  d: 19.0 px"));
}

// A width label normally sits above the pointer and a height label to its
// right; near the top or right edge they move to the other side and stay in.
void TestMeasureToolHandler::hoverLabelsStayInsideThePin()
{
    QImage image(200, 100, QImage::Format_ARGB32);
    image.fill(Qt::white);
    // A thin bar near the top, and a box near the right edge.
    for (int y = 1; y <= 4; ++y) {
        for (int x = 40; x < 120; ++x) {
            image.setPixelColor(x, y, QColor(30, 90, 200));
        }
    }
    for (int y = 20; y < 60; ++y) {
        for (int x = 185; x < 198; ++x) {
            image.setPixelColor(x, y, QColor(30, 90, 200));
        }
    }
    MeasureToolHandler handler;
    Host host;
    host.raster = std::make_shared<const QImage>(image);
    handler.setAutoMode(true);
    handler.setImageGeometry(QSize(200, 100), QSize(200, 100), 1.0);
    handler.setRasterProvider(host.provider());

    QFont font;
    font.setPixelSize(12);
    const QFontMetrics fm(font);
    const QRect bounds(0, 0, 200, 100);

    handler.onMouseMove(&host.context, QPoint(80, 2));
    QVERIFY(handler.hoverSpan(Qt::Horizontal));
    const QRect widthLabel = handler.hoverLabelRect(Qt::Horizontal, fm);
    QVERIFY2(bounds.contains(widthLabel), qPrintable(QStringLiteral("width label %1,%2 %3x%4")
        .arg(widthLabel.x()).arg(widthLabel.y()).arg(widthLabel.width()).arg(widthLabel.height())));
    QVERIFY(widthLabel.top() > 2);   // moved below the span line

    handler.onMouseMove(&host.context, QPoint(190, 40));
    QVERIFY(handler.hoverSpan(Qt::Vertical));
    const QRect heightLabel = handler.hoverLabelRect(Qt::Vertical, fm);
    QVERIFY(bounds.contains(heightLabel));
    QVERIFY(heightLabel.right() < 190);   // moved left of the span line

    // With room, the usual sides are kept.
    host.raster = std::make_shared<const QImage>(elementImage());
    handler.onMouseMove(&host.context, QPoint(80, 40));
    QVERIFY(handler.hoverLabelRect(Qt::Horizontal, fm).bottom() < 40);
    QVERIFY(handler.hoverLabelRect(Qt::Vertical, fm).left() > 80);
}

QTEST_MAIN(TestMeasureToolHandler)
#include "tst_MeasureToolHandler.moc"
