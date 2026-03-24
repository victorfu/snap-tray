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

QTEST_MAIN(TestMarkerToolHandler)
#include "tst_MarkerToolHandler.moc"
