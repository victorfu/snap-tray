#include <QtTest/QtTest>
#include <QImage>
#include <QPainter>

#include "annotations/MarkerStroke.h"
#include "annotations/PencilStroke.h"
#include "tools/ToolManager.h"

namespace {
QVector<QPointF> strokePoints(const AnnotationItem* item)
{
    if (const auto* pencil = dynamic_cast<const PencilStroke*>(item)) {
        return pencil->points();
    }
    if (const auto* marker = dynamic_cast<const MarkerStroke*>(item)) {
        return marker->points();
    }
    return {};
}

QImage renderPreview(const ToolManager& manager, qreal dpr)
{
    QImage image(qRound(240 * dpr), qRound(240 * dpr), QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(dpr);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    manager.drawCurrentPreview(painter);
    return image;
}
}

class TestStraightStrokes : public QObject
{
    Q_OBJECT
private slots:
    void testShiftStroke_data();
    void testShiftStroke();
    void testFreehand_data();
    void testFreehand();
    void testModeFixedAtPress_data();
    void testModeFixedAtPress();
    void testEmptyAndCancelledStroke_data();
    void testEmptyAndCancelledStroke();
    void testReleaseEndpoint_data();
    void testReleaseEndpoint();
};

void TestStraightStrokes::testShiftStroke_data()
{
    QTest::addColumn<int>("tool");
    QTest::addColumn<int>("lineStyle");
    QTest::addColumn<QColor>("color");
    QTest::addColumn<qreal>("dpr");
    for (qreal dpr : {1.0, 1.25, 2.0}) {
        const auto suffix = QByteArray::number(dpr);
        QTest::newRow(("pencil-" + suffix).constData())
            << int(ToolId::Pencil) << int(LineStyle::Solid) << QColor(Qt::red) << dpr;
        QTest::newRow(("pencil-dashed-" + suffix).constData())
            << int(ToolId::Pencil) << int(LineStyle::Dashed) << QColor(Qt::blue) << dpr;
        QTest::newRow(("pencil-translucent-" + suffix).constData())
            << int(ToolId::Pencil) << int(LineStyle::Solid) << QColor(30, 90, 180, 120) << dpr;
        QTest::newRow(("marker-" + suffix).constData())
            << int(ToolId::Marker) << int(LineStyle::Solid) << QColor(Qt::yellow) << dpr;
    }
}

void TestStraightStrokes::testShiftStroke()
{
    QFETCH(int, tool);
    QFETCH(int, lineStyle);
    QFETCH(QColor, color);
    QFETCH(qreal, dpr);
    AnnotationLayer layer;
    ToolManager manager;
    manager.registerDefaultHandlers();
    manager.setAnnotationLayer(&layer);
    manager.setCurrentTool(static_cast<ToolId>(tool));
    manager.context()->devicePixelRatio = dpr;
    manager.setColor(color);
    manager.setWidth(7);
    manager.setLineStyle(static_cast<LineStyle>(lineStyle));

    const QPointF start(30.25, 40.75);
    const QPointF end(180.25, 95.5); // Deliberately not an angle snap direction.
    manager.handleMousePress(start, Qt::ShiftModifier);
    manager.handleMouseMove(QPointF(70, 180), Qt::ShiftModifier);
    const QRect oldBounds = manager.currentHandler()->previewBounds();
    // An active stroke keeps the style captured at press.
    manager.setColor(Qt::green);
    manager.setWidth(11);
    manager.setLineStyle(LineStyle::Dotted);
    manager.handleMouseMove(end, Qt::ShiftModifier);
    QVERIFY(manager.currentHandler()->previewBounds().contains(oldBounds));
    const QImage preview = renderPreview(manager, dpr);
    manager.handleMouseRelease(end, Qt::ShiftModifier);

    QCOMPARE(layer.itemCount(), size_t(1));
    QCOMPARE(strokePoints(layer.itemAt(0)), QVector<QPointF>({start, end}));
    QImage expected(preview.size(), preview.format());
    expected.setDevicePixelRatio(dpr);
    expected.fill(Qt::transparent);
    {
        QPainter painter(&expected);
        if (tool == int(ToolId::Pencil)) {
            PencilStroke stroke({start, end}, color, 7, static_cast<LineStyle>(lineStyle));
            stroke.finalize();
            stroke.draw(painter);
        } else {
            MarkerStroke stroke({start, end}, color, 20);
            stroke.draw(painter);
        }
    }
    QCOMPARE(preview, expected);
    QImage committed(preview.size(), preview.format());
    committed.setDevicePixelRatio(dpr);
    committed.fill(Qt::transparent);
    {
        QPainter painter(&committed);
        layer.itemAt(0)->draw(painter);
    }
    QCOMPARE(committed, preview);
    layer.undo();
    QCOMPARE(layer.itemCount(), size_t(0));
    QVERIFY(!layer.canUndo());
    layer.redo();
    QCOMPARE(layer.itemCount(), size_t(1));
    QCOMPARE(strokePoints(layer.itemAt(0)), QVector<QPointF>({start, end}));
}

void TestStraightStrokes::testFreehand_data()
{
    QTest::addColumn<int>("tool");
    QTest::newRow("pencil") << int(ToolId::Pencil);
    QTest::newRow("marker") << int(ToolId::Marker);
}

void TestStraightStrokes::testFreehand()
{
    QFETCH(int, tool);
    AnnotationLayer layer;
    ToolManager manager;
    manager.registerDefaultHandlers();
    manager.setAnnotationLayer(&layer);
    manager.setCurrentTool(static_cast<ToolId>(tool));
    manager.handleMousePress(QPointF(30, 40));
    manager.handleMouseMove(QPointF(70, 180));
    manager.handleMouseMove(QPointF(180, 40));
    manager.handleMouseRelease(QPointF(190, 40));
    QCOMPARE(layer.itemCount(), size_t(1));
    const auto points = strokePoints(layer.itemAt(0));
    QVERIFY(points.size() > 2);
    QVERIFY(points[1].y() > 40);
}

void TestStraightStrokes::testModeFixedAtPress_data()
{
    QTest::addColumn<int>("tool");
    QTest::addColumn<bool>("shiftAtPress");
    QTest::newRow("pencil-straight") << int(ToolId::Pencil) << true;
    QTest::newRow("pencil-freehand") << int(ToolId::Pencil) << false;
    QTest::newRow("marker-straight") << int(ToolId::Marker) << true;
    QTest::newRow("marker-freehand") << int(ToolId::Marker) << false;
}

void TestStraightStrokes::testModeFixedAtPress()
{
    QFETCH(int, tool);
    QFETCH(bool, shiftAtPress);
    AnnotationLayer layer;
    ToolManager manager;
    manager.registerDefaultHandlers();
    manager.setAnnotationLayer(&layer);
    manager.setCurrentTool(static_cast<ToolId>(tool));
    const auto pressModifiers = shiftAtPress ? Qt::ShiftModifier : Qt::NoModifier;
    const auto laterModifiers = shiftAtPress ? Qt::NoModifier : Qt::ShiftModifier;
    const QPoint start(30, 40);
    const QPoint end(180, 95);
    manager.handleMousePress(start, pressModifiers);
    manager.handleMouseMove(QPoint(70, 180), laterModifiers);
    manager.handleMouseMove(QPoint(175, 90), laterModifiers);
    manager.handleMouseRelease(end, laterModifiers);
    QCOMPARE(layer.itemCount(), size_t(1));
    const auto points = strokePoints(layer.itemAt(0));
    if (shiftAtPress) {
        QCOMPARE(points, QVector<QPointF>({start, end}));
    } else {
        QVERIFY(points.size() > 2);
        QVERIFY(points[1].y() > 95);
    }
}

void TestStraightStrokes::testEmptyAndCancelledStroke_data()
{
    testFreehand_data();
}

void TestStraightStrokes::testEmptyAndCancelledStroke()
{
    QFETCH(int, tool);
    AnnotationLayer layer;
    ToolManager manager;
    manager.registerDefaultHandlers();
    manager.setAnnotationLayer(&layer);
    manager.setCurrentTool(static_cast<ToolId>(tool));
    const QPointF start(30.25, 40.75);
    manager.handleMousePress(start, Qt::ShiftModifier);
    manager.handleMouseRelease(start, Qt::ShiftModifier);
    QCOMPARE(layer.itemCount(), size_t(0));
    manager.handleMousePress(start, Qt::ShiftModifier);
    manager.handleMouseMove(QPointF(70, 180), Qt::ShiftModifier);
    manager.handleMouseMove(start, Qt::ShiftModifier);
    const QImage preview = renderPreview(manager, 1.0);
    QImage blank(preview.size(), preview.format());
    blank.fill(Qt::transparent);
    QCOMPARE(preview, blank);
    manager.handleMouseRelease(start, Qt::ShiftModifier);
    QCOMPARE(layer.itemCount(), size_t(0));
    QVERIFY(!layer.canUndo());

    manager.handleMousePress(start, Qt::ShiftModifier);
    manager.handleMouseMove(QPointF(70, 180), Qt::ShiftModifier);
    manager.cancelDrawing();
    QVERIFY(!manager.isDrawing());
    QCOMPARE(manager.currentHandler()->previewBounds(), QRect());
    QCOMPARE(renderPreview(manager, 1.0), blank);
    manager.handleMouseRelease(QPointF(70, 180), Qt::ShiftModifier);
    QCOMPARE(layer.itemCount(), size_t(0));

    manager.handleMousePress(start);
    manager.handleMouseMove(QPointF(70, 180));
    manager.handleMouseRelease(QPointF(180, 40));
    QCOMPARE(layer.itemCount(), size_t(1));
    QVERIFY(strokePoints(layer.itemAt(0)).size() > 2);
}

void TestStraightStrokes::testReleaseEndpoint_data()
{
    QTest::addColumn<int>("tool");
    QTest::addColumn<bool>("sendMove");
    QTest::newRow("pencil-release-only") << int(ToolId::Pencil) << false;
    QTest::newRow("marker-release-only") << int(ToolId::Marker) << false;
    QTest::newRow("pencil-retract") << int(ToolId::Pencil) << true;
    QTest::newRow("marker-retract") << int(ToolId::Marker) << true;
}

void TestStraightStrokes::testReleaseEndpoint()
{
    QFETCH(int, tool);
    QFETCH(bool, sendMove);
    AnnotationLayer layer;
    ToolManager manager;
    manager.registerDefaultHandlers();
    manager.setAnnotationLayer(&layer);
    manager.setCurrentTool(static_cast<ToolId>(tool));
    const QPointF start(30.25, 40.75);
    const QPointF end(31.0, 41.25); // Below the freehand sampling threshold.
    manager.handleMousePress(start, Qt::ShiftModifier);
    if (sendMove) {
        manager.handleMouseMove(QPointF(70, 180), Qt::ShiftModifier);
    }
    manager.handleMouseRelease(end, Qt::ShiftModifier);
    QCOMPARE(layer.itemCount(), size_t(1));
    QCOMPARE(strokePoints(layer.itemAt(0)), QVector<QPointF>({start, end}));
}

QTEST_MAIN(TestStraightStrokes)
#include "tst_StraightStrokes.moc"
