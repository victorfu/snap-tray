#include <QtTest/QtTest>

#include "tools/handlers/MosaicToolHandler.h"
#include "tools/ToolContext.h"
#include "annotations/AnnotationLayer.h"

#include <memory>
#include "tools/ToolManager.h"
#include "tools/ToolRepaintHelper.h"
#include "history/AnnotationSerializer.h"

class TestMosaicToolHandler : public QObject
{
    Q_OBJECT

private slots:
    void incrementalRepaintAndLifecycle_data();
    void incrementalRepaintAndLifecycle();
    void testToolIdAndCapabilities();
    void testCursor_UsesCenteredPixmapHotspot();
    void testCursor_DrawsSingleSquareOutline();
    void testReleaseOnlyDrag_CommitsStroke();
    void testReleaseAtPressPoint_DoesNotCommitStroke();
    void testMoveThenDistinctRelease_RecordsFinalPointOnce();
    void testStrokeUsesMosaicWidthSlot();
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

void TestMosaicToolHandler::testStrokeUsesMosaicWidthSlot()
{
    MosaicToolHandler handler;
    ToolContext context;
    context.width = 2;
    context.mosaicWidth = ToolWidthDefaults::kMosaicBrushLarge;

    auto source = std::make_shared<QPixmap>(64, 64);
    source->fill(Qt::white);
    context.sourcePixmap = source;

    std::unique_ptr<AnnotationItem> captured;
    context.addAnnotation = [&captured](std::unique_ptr<AnnotationItem> item) {
        captured = std::move(item);
    };

    handler.onMousePress(&context, QPoint(10, 10));
    handler.onMouseMove(&context, QPoint(15, 15));
    handler.onMouseRelease(&context, QPoint(20, 20));

    const auto* stroke = dynamic_cast<MosaicStroke*>(captured.get());
    QVERIFY(stroke);
    QCOMPARE(stroke->width(), ToolWidthDefaults::kMosaicBrushLarge);
}


void TestMosaicToolHandler::incrementalRepaintAndLifecycle_data()
{
    QTest::addColumn<qreal>("dpr");
    QTest::addColumn<qreal>("targetDpr");
    for (qreal dpr : {1., 1.25, 1.5, 2.})
        for (qreal target : {1., 1.25, 1.5, 2.})
            QTest::newRow(qPrintable(QString("%1-%2").arg(dpr).arg(target))) << dpr << target;
}

void TestMosaicToolHandler::incrementalRepaintAndLifecycle()
{
    QFETCH(qreal, dpr);
    QFETCH(qreal, targetDpr);
    ToolManager manager;
    AnnotationLayer layer;
    manager.setAnnotationLayer(&layer);
    manager.registerHandler(std::make_unique<MosaicToolHandler>());
    manager.setCurrentTool(ToolId::Mosaic);
    QPixmap source(QSize(qRound(320*dpr), qRound(240*dpr)));
    source.fill(QColor(10, 80, 170)); source.setDevicePixelRatio(dpr);
    auto shared = std::make_shared<const QPixmap>(source);
    manager.setSourcePixmap(shared);
    QImage partial(QSize(qRound(320*targetDpr), qRound(240*targetDpr)), QImage::Format_ARGB32_Premultiplied);
    partial.setDevicePixelRatio(targetDpr); partial.fill(Qt::transparent);
    auto* handler = manager.currentHandler();
    manager.handleMousePress(QPoint(200, 150));
    const QVector<QPoint> path{{100, 100}, {99, 99}, {250, 170}, {40, 60}, {10, 10}};
    for (const QPoint& point : path) {
        // Coalesced moves must retain all damage until the actual paint.
        manager.handleMouseMove(point);
        manager.handleMouseMove(point + QPoint(1, 1));
        const QRect dirty = snaptray::tools::previewRepaintRect(&manager, 3);
        QVERIFY(!dirty.isEmpty());
        QImage full(partial.size(), partial.format());
        full.setDevicePixelRatio(targetDpr); full.fill(Qt::transparent);
        {
            QPainter p(&partial);
            p.setClipRect(dirty);
            p.setCompositionMode(QPainter::CompositionMode_Source);
            p.fillRect(dirty, Qt::transparent);
            p.setCompositionMode(QPainter::CompositionMode_SourceOver);
            handler->drawPreview(p);
        }
        { QPainter p(&full); handler->drawPreview(p); }
        QCOMPARE(partial, full);
    }
    manager.handleMouseRelease(path.last() + QPoint(1, 1));
    QVERIFY(handler->previewBounds().isEmpty());
    QCOMPARE(layer.itemCount(), size_t(1));
    auto renderLayer = [&](const AnnotationLayer& input) {
        QImage output(partial.size(), partial.format());
        output.setDevicePixelRatio(targetDpr); output.fill(Qt::transparent);
        QPainter p(&output); input.draw(p); p.end(); return output;
    };
    const auto committed = renderLayer(layer);
    QCOMPARE(committed, partial);
    layer.undo(); QCOMPARE(layer.itemCount(), size_t(0));
    layer.redo(); QCOMPARE(renderLayer(layer), committed);
    AnnotationLayer restored;
    QString error;
    QVERIFY2(SnapTray::deserializeAnnotationLayer(SnapTray::serializeAnnotationLayer(layer),
             &restored, shared, &error), qPrintable(error));
    QCOMPARE(renderLayer(restored), committed);

    manager.handleMousePress(QPoint(80, 80));
    manager.handleMouseMove(QPoint(120, 120));
    { QPainter p(&partial); handler->drawPreview(p); }
    QSignalSpy repaint(&manager, &ToolManager::needsRepaint);
    manager.setCurrentTool(ToolId::Pencil);
    QVERIFY(!handler->isDrawing());
    QVERIFY(handler->previewBounds().isEmpty());
    QCOMPARE(repaint.count(), 1); // old preview is cleared, even on tool switch
    manager.setCurrentTool(ToolId::Mosaic);
    QVERIFY(!handler->isDrawing());
    manager.handleMousePress(QPoint(10, 10));
    manager.handleMouseMove(QPoint(20, 20));
    manager.cancelDrawing(); // host cancellation / close path
    QVERIFY(handler->previewBounds().isEmpty());
    QVERIFY(!handler->isDrawing());
    QCOMPARE(layer.itemCount(), size_t(1));
}

QTEST_MAIN(TestMosaicToolHandler)
#include "tst_MosaicToolHandler.moc"
