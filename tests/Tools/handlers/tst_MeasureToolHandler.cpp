#include <QtTest/QtTest>

#include "tools/handlers/MeasureToolHandler.h"

class TestMeasureToolHandler : public QObject
{
    Q_OBJECT

private slots:
    void testToolId();
    void testConstrainEndpoint_NoShift();
    void testConstrainEndpoint_ShiftHorizontal();
    void testConstrainEndpoint_ShiftVertical();
    void testConstrainEndpoint_ShiftDiagonalEqual();
};

void TestMeasureToolHandler::testToolId()
{
    MeasureToolHandler handler;
    QCOMPARE(handler.toolId(), ToolId::Measure);
}

void TestMeasureToolHandler::testConstrainEndpoint_NoShift()
{
    MeasureToolHandler handler;
    QPoint start(10, 10);
    QPoint end(50, 30);

    QPoint result = handler.constrainEndpoint(start, end, false);
    QCOMPARE(result, end);
}

void TestMeasureToolHandler::testConstrainEndpoint_ShiftHorizontal()
{
    MeasureToolHandler handler;
    QPoint start(10, 10);
    QPoint end(50, 20);  // dx=40 > dy=10 -> horizontal

    QPoint result = handler.constrainEndpoint(start, end, true);
    QCOMPARE(result, QPoint(50, 10));
}

void TestMeasureToolHandler::testConstrainEndpoint_ShiftVertical()
{
    MeasureToolHandler handler;
    QPoint start(10, 10);
    QPoint end(20, 50);  // dx=10 < dy=40 -> vertical

    QPoint result = handler.constrainEndpoint(start, end, true);
    QCOMPARE(result, QPoint(10, 50));
}

void TestMeasureToolHandler::testConstrainEndpoint_ShiftDiagonalEqual()
{
    MeasureToolHandler handler;
    QPoint start(10, 10);
    QPoint end(30, 30);  // dx=20 == dy=20 -> dx >= dy -> horizontal

    QPoint result = handler.constrainEndpoint(start, end, true);
    QCOMPARE(result, QPoint(30, 10));
}

QTEST_MAIN(TestMeasureToolHandler)
#include "tst_MeasureToolHandler.moc"
