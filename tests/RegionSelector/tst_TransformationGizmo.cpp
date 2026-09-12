#include <QtTest/QtTest>
#include <QFont>

#include "TransformationGizmo.h"
#include "annotations/TextBoxAnnotation.h"
#include "annotations/ShapeAnnotation.h"
#include "annotations/EmojiStickerAnnotation.h"
#include "annotations/ArrowAnnotation.h"
#include "annotations/PolylineAnnotation.h"

class tst_TransformationGizmo : public QObject
{
    Q_OBJECT

private slots:
    void testTextRotationHandleHit();
    void testTextCornerHandleHit();
    void testShapeRotationHandleHit();
    void testShapeCornerHandleHit();
    void testEmojiRotationHandleHit();
    void testSmallBoxHandles_data();
    void testSmallBoxHandles();
    void testArrowNearestHandle_data();
    void testArrowNearestHandle();
    void testPolylineNearestVertex_data();
    void testPolylineNearestVertex();
    void testBodyAndMissFallbacks();
};

void tst_TransformationGizmo::testTextRotationHandleHit()
{
    QFont font;
    font.setPointSize(16);
    TextBoxAnnotation text(QPointF(80, 60), QStringLiteral("SnapTray"), font, Qt::red);

    QPointF handlePos = TransformationGizmo::rotationHandlePosition(&text);
    GizmoHandle handle = TransformationGizmo::hitTest(&text, handlePos.toPoint());
    QCOMPARE(handle, GizmoHandle::Rotation);
}

void tst_TransformationGizmo::testTextCornerHandleHit()
{
    QFont font;
    font.setPointSize(16);
    TextBoxAnnotation text(QPointF(80, 60), QStringLiteral("SnapTray"), font, Qt::red);

    QVector<QPointF> corners = TransformationGizmo::cornerHandlePositions(&text);
    QVERIFY(corners.size() == 4);

    GizmoHandle handle = TransformationGizmo::hitTest(&text, corners[0].toPoint());
    QCOMPARE(handle, GizmoHandle::TopLeft);
}

void tst_TransformationGizmo::testShapeRotationHandleHit()
{
    ShapeAnnotation shape(QRect(60, 40, 120, 80), ShapeType::Rectangle, Qt::red, 3);

    QPointF handlePos = TransformationGizmo::rotationHandlePosition(&shape);
    GizmoHandle handle = TransformationGizmo::hitTest(&shape, handlePos.toPoint());
    QCOMPARE(handle, GizmoHandle::Rotation);
}

void tst_TransformationGizmo::testShapeCornerHandleHit()
{
    ShapeAnnotation shape(QRect(60, 40, 120, 80), ShapeType::Rectangle, Qt::red, 3);

    QVector<QPointF> corners = TransformationGizmo::cornerHandlePositions(&shape);
    QVERIFY(corners.size() == 4);

    GizmoHandle handle = TransformationGizmo::hitTest(&shape, corners[0].toPoint());
    QCOMPARE(handle, GizmoHandle::TopLeft);
}

void tst_TransformationGizmo::testEmojiRotationHandleHit()
{
    EmojiStickerAnnotation emoji(QPoint(100, 100), QStringLiteral("WW"), 1.0);

    QPointF handlePos = TransformationGizmo::rotationHandlePosition(&emoji);
    GizmoHandle handle = TransformationGizmo::hitTest(&emoji, handlePos.toPoint());
    QCOMPARE(handle, GizmoHandle::Rotation);
}

namespace {
template<typename Annotation>
void verifySmallBoxHandles(const Annotation& annotation)
{
    const auto corners = TransformationGizmo::cornerHandlePositions(&annotation);
    const GizmoHandle handles[] = {GizmoHandle::TopLeft, GizmoHandle::TopRight,
                                    GizmoHandle::BottomRight, GizmoHandle::BottomLeft};
    QCOMPARE(corners.size(), 4);
    bool overlapping = false;
    for (int i = 0; i < corners.size(); ++i) {
        const auto hit = TransformationGizmo::hitTest(&annotation, corners[i].toPoint());
        QCOMPARE(hit, handles[i]);
        overlapping |= QLineF(corners[i], corners[(i + 1) % corners.size()]).length()
            < 2 * (TransformationGizmo::kHandleRadius + TransformationGizmo::kHitTolerance);
    }
    QVERIFY(overlapping);
    QCOMPARE(TransformationGizmo::hitTest(&annotation,
        TransformationGizmo::rotationHandlePosition(&annotation).toPoint()), GizmoHandle::Rotation);
}
}

void tst_TransformationGizmo::testSmallBoxHandles_data()
{
    QTest::addColumn<int>("type");
    QTest::addColumn<int>("rotation");
    for (int type : {0, 1, 2}) {
        for (int rotation : {-135, 0, 35, 90}) {
            QTest::addRow("type-%d-rotation-%d", type, rotation) << type << rotation;
        }
    }
}

void tst_TransformationGizmo::testSmallBoxHandles()
{
    QFETCH(int, type);
    QFETCH(int, rotation);
    if (type == 0) {
        TextBoxAnnotation text(QPointF(-100, 100), QStringLiteral("x"), QFont(), Qt::red);
        text.setBox(QRectF(0, 0, 6, 8));
        text.setRotation(rotation);
        verifySmallBoxHandles(text);
    } else if (type == 1) {
        EmojiStickerAnnotation emoji(QPoint(-100, 100), QStringLiteral("😀"),
                                      EmojiStickerAnnotation::kMinScale);
        emoji.setRotation(rotation);
        verifySmallBoxHandles(emoji);
    } else {
        ShapeAnnotation shape(QRect(-100, 100, 6, 8), ShapeType::Rectangle, Qt::red, 2);
        shape.setRotation(rotation);
        verifySmallBoxHandles(shape);
    }
}

void tst_TransformationGizmo::testArrowNearestHandle_data()
{
    QTest::addColumn<QPoint>("end");
    QTest::addColumn<QPointF>("control");
    QTest::addColumn<QPoint>("point");
    QTest::addColumn<GizmoHandle>("expected");
    QTest::newRow("start-center") << QPoint(108, 100) << QPointF(104, 100)
        << QPoint(100, 100) << GizmoHandle::ArrowStart;
    QTest::newRow("end-center") << QPoint(108, 100) << QPointF(104, 100)
        << QPoint(108, 100) << GizmoHandle::ArrowEnd;
    QTest::newRow("control-center") << QPoint(108, 100) << QPointF(104, 100)
        << QPoint(104, 100) << GizmoHandle::ArrowControl;
    QTest::newRow("control-start-tie") << QPoint(108, 100) << QPointF(104, 100)
        << QPoint(102, 100) << GizmoHandle::ArrowControl;
    QTest::newRow("start-end-tie") << QPoint(108, 100) << QPointF(104, 180)
        << QPoint(104, 100) << GizmoHandle::ArrowStart;
    QTest::newRow("coincident-endpoints") << QPoint(100, 100) << QPointF(100, 140)
        << QPoint(100, 100) << GizmoHandle::ArrowStart;
    QTest::newRow("all-coincident") << QPoint(100, 100) << QPointF(100, 100)
        << QPoint(100, 100) << GizmoHandle::ArrowControl;
    QTest::newRow("outside-control-radius") << QPoint(200, 100) << QPointF(150, 100)
        << QPoint(150, 114) << GizmoHandle::None;
}

void tst_TransformationGizmo::testArrowNearestHandle()
{
    QFETCH(QPoint, end);
    QFETCH(QPointF, control);
    QFETCH(QPoint, point);
    QFETCH(GizmoHandle, expected);
    ArrowAnnotation arrow(QPoint(100, 100), end, Qt::red, 2);
    arrow.setControlPoint(control);
    QCOMPARE(TransformationGizmo::hitTest(&arrow, point), expected);
}

void tst_TransformationGizmo::testPolylineNearestVertex_data()
{
    QTest::addColumn<QVector<QPoint>>("points");
    QTest::addColumn<QPoint>("point");
    QTest::addColumn<int>("expected");
    const QVector<QPoint> close{QPoint(100, 100), QPoint(104, 100), QPoint(108, 100)};
    for (int i = 0; i < close.size(); ++i) {
        QTest::addRow("center-%d", i) << close << close[i] << i;
    }
    QTest::newRow("equal-distance") << close << QPoint(106, 100) << 1;
    QTest::newRow("coincident") << QVector<QPoint>{QPoint(100, 100), QPoint(100, 100), QPoint(100, 100)}
        << QPoint(100, 100) << 0;
    QTest::newRow("outside-radius") << close << QPoint(104, 115) << -2;
}

void tst_TransformationGizmo::testPolylineNearestVertex()
{
    QFETCH(QVector<QPoint>, points);
    QFETCH(QPoint, point);
    QFETCH(int, expected);
    PolylineAnnotation line(points, Qt::red, 2, LineEndStyle::None);
    QCOMPARE(TransformationGizmo::hitTestVertex(&line, point), expected);
}

void tst_TransformationGizmo::testBodyAndMissFallbacks()
{
    ShapeAnnotation shape(QRect(100, 100, 100, 100), ShapeType::Rectangle, Qt::red, 2, true);
    QCOMPARE(TransformationGizmo::hitTest(&shape, QPoint(150, 150)), GizmoHandle::Body);
    QCOMPARE(TransformationGizmo::hitTest(&shape, QPoint(400, 400)), GizmoHandle::None);
    // The shape's padded corners are equidistant at the top edge midpoint.
    ShapeAnnotation tiny(QRect(100, 100, 4, 4), ShapeType::Rectangle, Qt::red, 2);
    QCOMPARE(TransformationGizmo::hitTest(&tiny, QPoint(102, 92)), GizmoHandle::TopLeft);
    ArrowAnnotation arrow(QPoint(0, 0), QPoint(200, 0), Qt::red, 2, LineEndStyle::None);
    QCOMPARE(TransformationGizmo::hitTest(&arrow, QPoint(50, 0)), GizmoHandle::Body);
    QCOMPARE(TransformationGizmo::hitTest(&arrow, QPoint(400, 400)), GizmoHandle::None);
    PolylineAnnotation line({QPoint(0, 0), QPoint(200, 0)}, Qt::red, 2, LineEndStyle::None);
    QCOMPARE(TransformationGizmo::hitTestVertex(&line, QPoint(50, 0)), -1);
    QCOMPARE(TransformationGizmo::hitTestVertex(&line, QPoint(400, 400)), -2);
}

QTEST_MAIN(tst_TransformationGizmo)
#include "tst_TransformationGizmo.moc"
