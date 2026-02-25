#include <QtTest/QtTest>
#include <QFont>

#include "TransformationGizmo.h"
#include "annotations/TextBoxAnnotation.h"
#include "annotations/ShapeAnnotation.h"
#include "annotations/EmojiStickerAnnotation.h"

class tst_TransformationGizmo : public QObject
{
    Q_OBJECT

private slots:
    void testTextRotationHandleHit();
    void testTextCornerHandleHit();
    void testShapeRotationHandleHit();
    void testShapeCornerHandleHit();
    void testEmojiRotationHandleHit();
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

QTEST_MAIN(tst_TransformationGizmo)
#include "tst_TransformationGizmo.moc"
