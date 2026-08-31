#include <QtTest/QtTest>

#include "tools/handlers/MosaicToolHandler.h"
#include "tools/ToolContext.h"
#include "annotations/AnnotationLayer.h"

#include <memory>

class TestMosaicToolHandler : public QObject
{
    Q_OBJECT

private slots:
    void testToolIdAndCapabilities();
    void testCursor_UsesCenteredPixmapHotspot();
    void testCursor_DrawsSingleSquareOutline();
    void testReleaseOnlyDrag_CommitsStroke();
    void testReleaseAtPressPoint_DoesNotCommitStroke();
    void testMoveThenDistinctRelease_RecordsFinalPointOnce();
};

void TestMosaicToolHandler::testToolIdAndCapabilities()
{
    MosaicToolHandler handler;

    QCOMPARE(handler.toolId(), ToolId::Mosaic);
    QVERIFY(!handler.supportsColor());
    QVERIFY(handler.supportsWidth());
}

void TestMosaicToolHandler::testCursor_UsesCenteredPixmapHotspot()
{
    MosaicToolHandler handler;

    handler.setWidth(18);

    const QCursor cursor = handler.cursor();
    const QPixmap pixmap = cursor.pixmap();
    const QSizeF logicalSize = pixmap.deviceIndependentSize();

    QVERIFY(!pixmap.isNull());
    QVERIFY(pixmap.devicePixelRatio() >= 2.0);
    QCOMPARE(cursor.hotSpot(), QPoint(qRound(logicalSize.width() / 2.0), qRound(logicalSize.height() / 2.0)));
}

void TestMosaicToolHandler::testCursor_DrawsSingleSquareOutline()
{
    MosaicToolHandler handler;
    constexpr int brushWidth = 18;
    constexpr int expectedPadding = 4;
    constexpr QColor expectedColor(0x6C, 0x5C, 0xE7);

    handler.setWidth(brushWidth);

    const QPixmap pixmap = handler.cursor().pixmap();
    const qreal dpr = pixmap.devicePixelRatio();
    const QSizeF logicalSize = pixmap.deviceIndependentSize();
    const QImage image = pixmap.toImage().convertToFormat(QImage::Format_ARGB32);
    QVERIFY(!image.isNull());

    auto toPhysical = [dpr](qreal value) {
        return qRound(value * dpr);
    };

    const int centerX = image.width() / 2;
    const int centerY = image.height() / 2;
    const int topY = toPhysical(expectedPadding + 1.0);
    const QRgb topPixel = image.pixel(centerX, topY);

    QCOMPARE(qAlpha(topPixel), 255);
    QCOMPARE(qRed(topPixel), expectedColor.red());
    QCOMPARE(qGreen(topPixel), expectedColor.green());
    QCOMPARE(qBlue(topPixel), expectedColor.blue());
    QCOMPARE(qAlpha(image.pixel(toPhysical(logicalSize.width() / 2.0), toPhysical(expectedPadding + 4.0))), 0);
    QCOMPARE(qAlpha(image.pixel(image.width() / 2, image.height() / 2)), 0);
}

void TestMosaicToolHandler::testReleaseOnlyDrag_CommitsStroke()
{
    MosaicToolHandler handler;
    AnnotationLayer layer;
    ToolContext context;
    context.annotationLayer = &layer;
    QPixmap source(64, 64);
    source.fill(Qt::white);
    context.sourcePixmap = std::make_shared<const QPixmap>(source);

    const QPoint pressPoint(10, 20);
    const QPoint releasePoint(40, 20);
    handler.onMousePress(&context, pressPoint);
    handler.onMouseRelease(&context, releasePoint);

    QCOMPARE(layer.itemCount(), size_t(1));
    auto *stroke = dynamic_cast<MosaicStroke *>(layer.itemAt(0));
    QVERIFY(stroke != nullptr);
    QCOMPARE(stroke->points(), QVector<QPoint>({pressPoint, releasePoint}));
}

void TestMosaicToolHandler::testReleaseAtPressPoint_DoesNotCommitStroke()
{
    MosaicToolHandler handler;
    AnnotationLayer layer;
    ToolContext context;
    context.annotationLayer = &layer;
    QPixmap source(64, 64);
    source.fill(Qt::white);
    context.sourcePixmap = std::make_shared<const QPixmap>(source);

    const QPoint point(10, 20);
    handler.onMousePress(&context, point);
    handler.onMouseRelease(&context, point);

    QCOMPARE(layer.itemCount(), size_t(0));
}

void TestMosaicToolHandler::testMoveThenDistinctRelease_RecordsFinalPointOnce()
{
    MosaicToolHandler handler;
    AnnotationLayer layer;
    ToolContext context;
    context.annotationLayer = &layer;
    QPixmap source(64, 64);
    source.fill(Qt::white);
    context.sourcePixmap = std::make_shared<const QPixmap>(source);

    const QPoint pressPoint(10, 20);
    const QPoint movePoint(25, 20);
    const QPoint releasePoint(40, 20);
    handler.onMousePress(&context, pressPoint);
    handler.onMouseMove(&context, movePoint);
    handler.onMouseRelease(&context, releasePoint);

    QCOMPARE(layer.itemCount(), size_t(1));
    auto *stroke = dynamic_cast<MosaicStroke *>(layer.itemAt(0));
    QVERIFY(stroke != nullptr);
    QCOMPARE(
        stroke->points(),
        QVector<QPoint>({pressPoint, movePoint, releasePoint}));
}

QTEST_MAIN(TestMosaicToolHandler)
#include "tst_MosaicToolHandler.moc"
