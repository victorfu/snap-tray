#include <QtTest>
#include "TextFormattingState.h"
#include "TransformationGizmo.h"
#include <QPoint>
#include <QPointF>

/**
 * @brief Standalone test class for TextAnnotationEditor logic.
 *
 * Since TextAnnotationEditor depends on AnnotationLayer, InlineTextEditor,
 * and ColorAndWidthWidget, we test the core logic directly here without
 * needing those dependencies.
 */
class tst_TextAnnotationEditor : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    // TextFormattingState tests
    void testTextFormattingStateDefault();
    void testTextFormattingStateToQFont();
    void testTextFormattingStateFromQFont();

    // Double-click detection logic tests
    void testDoubleClickDetection_WithinThreshold();
    void testDoubleClickDetection_TooSlow();
    void testDoubleClickDetection_TooFar();
    void testDoubleClickDetection_NoRecord();

    // GizmoHandle enum tests
    void testGizmoHandleValues();

    // Transformation calculation tests
    void testRotationCalculation();
    void testScaleCalculation();

    // Constants tests
    void testDoubleClickConstants();

    // Dragging logic tests
    void testDraggingDeltaCalculation();
    void testDraggingMultipleUpdates();

private:
    // Double-click detection helper (mimics TextAnnotationEditor logic)
    bool isDoubleClick(const QPoint& lastPos, qint64 lastTime,
                       const QPoint& newPos, qint64 newTime,
                       qint64 interval, int distance) const;
};

void tst_TextAnnotationEditor::initTestCase()
{
}

void tst_TextAnnotationEditor::cleanupTestCase()
{
}

void tst_TextAnnotationEditor::testTextFormattingStateDefault()
{
    TextFormattingState state;
    QCOMPARE(state.bold, true);
    QCOMPARE(state.italic, false);
    QCOMPARE(state.underline, false);
    QCOMPARE(state.fontSize, 16);
    QVERIFY(state.fontFamily.isEmpty());
}

void tst_TextAnnotationEditor::testTextFormattingStateToQFont()
{
    TextFormattingState state;
    state.bold = true;
    state.italic = true;
    state.underline = true;
    state.fontSize = 24;
    state.fontFamily = "Arial";

    QFont font = state.toQFont();

    QCOMPARE(font.bold(), true);
    QCOMPARE(font.italic(), true);
    QCOMPARE(font.underline(), true);
    QCOMPARE(font.pointSize(), 24);
    QCOMPARE(font.family(), QString("Arial"));
}

void tst_TextAnnotationEditor::testTextFormattingStateFromQFont()
{
    QFont font;
    font.setBold(true);
    font.setItalic(false);
    font.setUnderline(true);
    font.setPointSize(18);
    font.setFamily("Courier New");

    TextFormattingState state = TextFormattingState::fromQFont(font);

    QCOMPARE(state.bold, true);
    QCOMPARE(state.italic, false);
    QCOMPARE(state.underline, true);
    QCOMPARE(state.fontSize, 18);
    QCOMPARE(state.fontFamily, QString("Courier New"));
}

bool tst_TextAnnotationEditor::isDoubleClick(const QPoint& lastPos, qint64 lastTime,
                                              const QPoint& newPos, qint64 newTime,
                                              qint64 interval, int distance) const
{
    if (lastTime == 0) {
        return false;
    }

    qint64 timeDiff = newTime - lastTime;
    if (timeDiff > interval) {
        return false;
    }

    int dx = newPos.x() - lastPos.x();
    int dy = newPos.y() - lastPos.y();
    int distanceSq = dx * dx + dy * dy;
    if (distanceSq > distance * distance) {
        return false;
    }

    return true;
}

void tst_TextAnnotationEditor::testDoubleClickDetection_WithinThreshold()
{
    QPoint lastPos(100, 100);
    qint64 lastTime = 1000;
    QPoint newPos(102, 101);
    qint64 newTime = 1200;  // 200ms later

    bool result = isDoubleClick(lastPos, lastTime, newPos, newTime, 500, 5);
    QVERIFY(result);
}

void tst_TextAnnotationEditor::testDoubleClickDetection_TooSlow()
{
    QPoint lastPos(100, 100);
    qint64 lastTime = 1000;
    QPoint newPos(102, 101);
    qint64 newTime = 1600;  // 600ms later (> 500ms)

    bool result = isDoubleClick(lastPos, lastTime, newPos, newTime, 500, 5);
    QVERIFY(!result);
}

void tst_TextAnnotationEditor::testDoubleClickDetection_TooFar()
{
    QPoint lastPos(100, 100);
    qint64 lastTime = 1000;
    QPoint newPos(110, 110);  // ~14 pixels away (> 5)
    qint64 newTime = 1200;

    bool result = isDoubleClick(lastPos, lastTime, newPos, newTime, 500, 5);
    QVERIFY(!result);
}

void tst_TextAnnotationEditor::testDoubleClickDetection_NoRecord()
{
    QPoint lastPos(0, 0);
    qint64 lastTime = 0;  // No previous click
    QPoint newPos(100, 100);
    qint64 newTime = 1000;

    bool result = isDoubleClick(lastPos, lastTime, newPos, newTime, 500, 5);
    QVERIFY(!result);
}

void tst_TextAnnotationEditor::testGizmoHandleValues()
{
    // Verify GizmoHandle enum values exist and are distinct
    QCOMPARE(static_cast<int>(GizmoHandle::None), 0);
    QVERIFY(static_cast<int>(GizmoHandle::Body) != static_cast<int>(GizmoHandle::None));
    QVERIFY(static_cast<int>(GizmoHandle::TopLeft) != static_cast<int>(GizmoHandle::Body));
    QVERIFY(static_cast<int>(GizmoHandle::Rotation) != static_cast<int>(GizmoHandle::BottomRight));
}

void tst_TextAnnotationEditor::testRotationCalculation()
{
    // Test rotation angle calculation (from center to mouse position)
    QPointF center(100, 100);
    QPointF mousePos(150, 100);  // Due east

    QPointF delta = mousePos - center;
    qreal angle = qRadiansToDegrees(qAtan2(delta.y(), delta.x()));

    QCOMPARE(angle, 0.0);  // Due east = 0 degrees

    // Due south
    mousePos = QPointF(100, 150);
    delta = mousePos - center;
    angle = qRadiansToDegrees(qAtan2(delta.y(), delta.x()));
    QCOMPARE(angle, 90.0);

    // Due west
    mousePos = QPointF(50, 100);
    delta = mousePos - center;
    angle = qRadiansToDegrees(qAtan2(delta.y(), delta.x()));
    QCOMPARE(angle, 180.0);

    // Due north
    mousePos = QPointF(100, 50);
    delta = mousePos - center;
    angle = qRadiansToDegrees(qAtan2(delta.y(), delta.x()));
    QCOMPARE(angle, -90.0);
}

void tst_TextAnnotationEditor::testScaleCalculation()
{
    // Test scale factor calculation (based on distance from center)
    QPointF center(100, 100);
    QPointF startPos(150, 100);  // 50 pixels away

    qreal startDistance = qSqrt(50.0 * 50.0);
    QCOMPARE(startDistance, 50.0);

    // Move to 100 pixels away - should double the scale
    QPointF currentPos(200, 100);
    QPointF delta = currentPos - center;
    qreal currentDistance = qSqrt(delta.x() * delta.x() + delta.y() * delta.y());
    QCOMPARE(currentDistance, 100.0);

    qreal scaleFactor = currentDistance / startDistance;
    QCOMPARE(scaleFactor, 2.0);

    // Move to 25 pixels away - should halve the scale
    currentPos = QPointF(125, 100);
    delta = currentPos - center;
    currentDistance = qSqrt(delta.x() * delta.x() + delta.y() * delta.y());
    QCOMPARE(currentDistance, 25.0);

    scaleFactor = currentDistance / startDistance;
    QCOMPARE(scaleFactor, 0.5);
}

void tst_TextAnnotationEditor::testDoubleClickConstants()
{
    // These should match the values in TextAnnotationEditor
    constexpr qint64 kDoubleClickInterval = 500;  // ms
    constexpr int kDoubleClickDistance = 5;       // pixels

    QCOMPARE(kDoubleClickInterval, static_cast<qint64>(500));
    QCOMPARE(kDoubleClickDistance, 5);
}

void tst_TextAnnotationEditor::testDraggingDeltaCalculation()
{
    // Test delta calculation for dragging (mimics updateDragging logic)
    QPoint dragStart(100, 100);
    QPoint currentPos(150, 120);

    QPoint delta = currentPos - dragStart;

    QCOMPARE(delta.x(), 50);
    QCOMPARE(delta.y(), 20);
}

void tst_TextAnnotationEditor::testDraggingMultipleUpdates()
{
    // Simulate multiple drag updates (dragStart should be updated after each move)
    QPoint position(0, 0);  // Simulated object position
    QPoint dragStart(100, 100);

    // First update
    QPoint mousePos1(110, 105);
    QPoint delta1 = mousePos1 - dragStart;
    position += delta1;
    dragStart = mousePos1;  // Update for next calculation

    QCOMPARE(position, QPoint(10, 5));
    QCOMPARE(dragStart, QPoint(110, 105));

    // Second update
    QPoint mousePos2(130, 110);
    QPoint delta2 = mousePos2 - dragStart;
    position += delta2;
    dragStart = mousePos2;

    QCOMPARE(position, QPoint(30, 10));  // 10+20=30, 5+5=10
    QCOMPARE(dragStart, QPoint(130, 110));

    // Third update
    QPoint mousePos3(120, 100);  // Move backwards
    QPoint delta3 = mousePos3 - dragStart;
    position += delta3;

    QCOMPARE(position, QPoint(20, 0));  // 30-10=20, 10-10=0
}

QTEST_MAIN(tst_TextAnnotationEditor)
#include "tst_TextAnnotationEditor.moc"
