#include <QtTest/QtTest>

#include "annotations/AnnotationLayer.h"
#include "annotations/PolylineAnnotation.h"
#include "tools/ToolContext.h"
#include "tools/handlers/PolylineToolHandler.h"

class TestPolylineToolHandler : public QObject
{
    Q_OBJECT

private slots:
    void rapidDistantClickDoesNotFinish();
};

void TestPolylineToolHandler::rapidDistantClickDoesNotFinish()
{
    AnnotationLayer layer;
    ToolContext context;
    context.annotationLayer = &layer;
    context.color = Qt::red;
    context.width = 3;
    context.lineStyle = LineStyle::Solid;

    PolylineToolHandler handler;
    const QPoint start(20, 20);
    const QPoint nextVertex(180, 140);

    handler.onMousePress(&context, start);
    handler.onMousePress(&context, nextVertex);

    QVERIFY(handler.isDrawing());
    QCOMPARE(layer.itemCount(), size_t(0));

    // Completion is driven by the real double-click event, not click timing.
    handler.onDoubleClick(&context, nextVertex);

    QVERIFY(!handler.isDrawing());
    QCOMPARE(layer.itemCount(), size_t(1));
    auto* polyline = dynamic_cast<PolylineAnnotation*>(layer.itemAt(0));
    QVERIFY(polyline != nullptr);
    QCOMPARE(polyline->points(), QVector<QPoint>({start, nextVertex}));
}

QTEST_MAIN(TestPolylineToolHandler)
#include "tst_PolylineToolHandler.moc"
