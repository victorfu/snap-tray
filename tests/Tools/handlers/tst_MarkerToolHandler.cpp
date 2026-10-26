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
    void testReleaseMovesTinyTerminalSegmentOntoCursor();
    void testPointSpacingSuppressesSubPixelJitter_data();
    void testPointSpacingSuppressesSubPixelJitter();
    void testLiveTailReachesCursorBeforePointCommits();

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

void TestMarkerToolHandler::testReleaseMovesTinyTerminalSegmentOntoCursor()
{
    m_handler->onMousePressF(m_context, QPointF(10.0, 10.0));
    m_handler->onMouseMoveF(m_context, QPointF(20.0, 10.0));
    // Smoothing commits the move short of x=20, within spacing of the release.
    m_handler->onMouseReleaseF(m_context, QPointF(18.0, 10.0));

    QCOMPARE(m_layer->itemCount(), size_t(1));
    auto* stroke = dynamic_cast<MarkerStroke*>(m_layer->itemAt(0));
    QVERIFY(stroke != nullptr);
    // The stroke ends under the cursor without adding a tiny bump segment.
    QCOMPARE(stroke->points().size(), qsizetype(2));
    QCOMPARE(stroke->points().last(), QPointF(18.0, 10.0));
}

void TestMarkerToolHandler::testPointSpacingSuppressesSubPixelJitter_data()
{
    QTest::addColumn<qreal>("dpr");
    QTest::addColumn<qreal>("minSpacing");
    QTest::newRow("100%") << 1.0 << 4.0;
    QTest::newRow("150%") << 1.5 << 4.0;
    QTest::newRow("200%") << 2.0 << 3.0;
}

void TestMarkerToolHandler::testPointSpacingSuppressesSubPixelJitter()
{
    QFETCH(qreal, dpr);
    QFETCH(qreal, minSpacing);

    // One physical pixel per event, as a slow drag delivers on Windows.
    m_context->devicePixelRatio = dpr;
    const qreal step = 1.0 / dpr;
    m_handler->onMousePressF(m_context, QPointF(10.0, 10.0));
    for (qreal x = 10.0 + step; x < 70.0; x += step) {
        m_handler->onMouseMoveF(m_context, QPointF(x, 10.0));
    }
    m_handler->onMouseReleaseF(m_context, QPointF(70.0, 10.0));

    QCOMPARE(m_layer->itemCount(), size_t(1));
    auto* stroke = dynamic_cast<MarkerStroke*>(m_layer->itemAt(0));
    QVERIFY(stroke != nullptr);
    const QVector<QPointF> points = stroke->points();
    QVERIFY(points.size() >= 3);
    // The release point is exempt so the stroke still ends under the cursor.
    for (int i = 1; i < points.size() - 1; ++i) {
        QVERIFY2(QLineF(points[i - 1], points[i]).length() >= minSpacing - 1e-6,
                 qPrintable(QStringLiteral("points %1 and %2 are closer than %3")
                                .arg(i - 1).arg(i).arg(minSpacing)));
    }
    QCOMPARE(points.last(), QPointF(70.0, 10.0));
}

void TestMarkerToolHandler::testLiveTailReachesCursorBeforePointCommits()
{
    m_handler->onMousePressF(m_context, QPointF(20.0, 30.0));
    for (int i = 1; i <= 20; ++i) {
        m_handler->onMouseMoveF(m_context, QPointF(20.0 + i * 3.0, 30.0));
    }
    const QPointF cursor(80.0, 30.0);
    QVERIFY(m_handler->previewBounds().contains(cursor.toPoint()));

    QImage image(120, 60, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    {
        QPainter painter(&image);
        m_handler->drawPreview(painter);
    }
    // The committed points trail the cursor, but the preview reaches it.
    QVERIFY(qAlpha(image.pixel(cursor.toPoint())) > 0);
    QCOMPARE(qAlpha(image.pixel(QPoint(100, 30))), 0);
}

QTEST_MAIN(TestMarkerToolHandler)
#include "tst_MarkerToolHandler.moc"
