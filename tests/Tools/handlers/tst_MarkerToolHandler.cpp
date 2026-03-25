#include <QtTest/QtTest>

#include "annotations/AnnotationLayer.h"
#include "tools/ToolContext.h"
#include "tools/handlers/MarkerToolHandler.h"
#include <QImage>
#include <QPainter>

class TestMarkerToolHandler : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void testPreviewBounds_DuringAndAfterDrawing();
    void testPreviewBounds_StaysLocalAsStrokeGrows();
    void testFloatInputPreservesFractionalPoints();
    void testFiltersMicroMoves();

private:
    MarkerToolHandler* m_handler = nullptr;
    ToolContext* m_context = nullptr;
    AnnotationLayer* m_layer = nullptr;
};

void TestMarkerToolHandler::init()
{
    m_handler = new MarkerToolHandler();
    m_layer = new AnnotationLayer();
    m_context = new ToolContext();
    m_context->annotationLayer = m_layer;
    m_context->color = Qt::yellow;
}

void TestMarkerToolHandler::cleanup()
{
    delete m_handler;
    delete m_context;
    delete m_layer;
    m_handler = nullptr;
    m_context = nullptr;
    m_layer = nullptr;
}

void TestMarkerToolHandler::testPreviewBounds_DuringAndAfterDrawing()
{
    QCOMPARE(m_handler->previewBounds(), QRect());

    m_handler->onMousePress(m_context, QPoint(40, 40));
    const QRect initialBounds = m_handler->previewBounds();
    QVERIFY(initialBounds.isValid());
    QVERIFY(!initialBounds.isEmpty());

    m_handler->onMouseMove(m_context, QPoint(80, 60));
    m_handler->onMouseMove(m_context, QPoint(120, 80));

    const QRect duringBounds = m_handler->previewBounds();
    QVERIFY(duringBounds.isValid());
    QVERIFY(!duringBounds.isEmpty());
    QVERIFY(duringBounds.left() <= 40);
    QVERIFY(duringBounds.top() <= 40);
    QVERIFY(duringBounds.right() >= 120);
    QVERIFY(duringBounds.bottom() >= 80);

    m_handler->onMouseRelease(m_context, QPoint(140, 90));
    QCOMPARE(m_handler->previewBounds(), QRect());
}

void TestMarkerToolHandler::testPreviewBounds_StaysLocalAsStrokeGrows()
{
    m_handler->onMousePress(m_context, QPoint(20, 20));
    for (int i = 1; i < 12; ++i) {
        m_handler->onMouseMove(m_context, QPoint(20 + i * 20, 20 + i * 10));
    }

    const QRect previewBounds = m_handler->previewBounds();
    QVERIFY(previewBounds.isValid());
    QVERIFY(!previewBounds.isEmpty());
    QVERIFY(previewBounds.width() < 140);
}

void TestMarkerToolHandler::testFloatInputPreservesFractionalPoints()
{
    m_handler->onMousePressF(m_context, QPointF(10.25, 10.75));
    m_handler->onMouseMoveF(m_context, QPointF(14.5, 16.25));
    m_handler->onMouseReleaseF(m_context, QPointF(19.75, 21.5));

    QCOMPARE(m_layer->itemCount(), size_t(1));
    auto* stroke = dynamic_cast<MarkerStroke*>(m_layer->itemAt(0));
    QVERIFY(stroke != nullptr);
    const QVector<QPointF> points = stroke->points();
    QVERIFY(!points.isEmpty());

    bool hasFractionalPoint = false;
    for (const QPointF& point : points) {
        if (!qFuzzyCompare(point.x(), qRound(point.x())) ||
            !qFuzzyCompare(point.y(), qRound(point.y()))) {
            hasFractionalPoint = true;
            break;
        }
    }
    QVERIFY(hasFractionalPoint);
}

void TestMarkerToolHandler::testFiltersMicroMoves()
{
    m_handler->onMousePressF(m_context, QPointF(10.0, 10.0));
    m_handler->onMouseMoveF(m_context, QPointF(11.0, 11.0));
    m_handler->onMouseMoveF(m_context, QPointF(11.4, 11.4));
    m_handler->onMouseReleaseF(m_context, QPointF(12.0, 12.0));

    QCOMPARE(m_layer->itemCount(), size_t(1));
    auto* stroke = dynamic_cast<MarkerStroke*>(m_layer->itemAt(0));
    QVERIFY(stroke != nullptr);
    QCOMPARE(stroke->points().size(), qsizetype(2));
}

QTEST_MAIN(TestMarkerToolHandler)
#include "tst_MarkerToolHandler.moc"
