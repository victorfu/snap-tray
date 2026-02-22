#include <QtTest/QtTest>

#include "region/ShapeAnnotationEditor.h"
#include "annotations/AnnotationLayer.h"
#include "annotations/ShapeAnnotation.h"

class tst_ShapeAnnotationEditor : public QObject
{
    Q_OBJECT

private slots:
    void testDraggingMovesSelectedShape();
    void testRotationTransformation();
    void testScaleTransformation();
};

void tst_ShapeAnnotationEditor::testDraggingMovesSelectedShape()
{
    AnnotationLayer layer;
    auto shape = std::make_unique<ShapeAnnotation>(
        QRect(40, 40, 100, 60), ShapeType::Rectangle, Qt::red, 3);
    layer.addItem(std::move(shape));
    layer.setSelectedIndex(0);

    auto* shapeItem = dynamic_cast<ShapeAnnotation*>(layer.selectedItem());
    QVERIFY(shapeItem != nullptr);
    const QPointF originalCenter = shapeItem->center();

    ShapeAnnotationEditor editor;
    editor.setAnnotationLayer(&layer);
    editor.startDragging(QPoint(100, 100));
    editor.updateDragging(QPoint(130, 120));
    editor.finishDragging();

    const QPointF movedCenter = shapeItem->center();
    QCOMPARE(movedCenter, originalCenter + QPointF(30.0, 20.0));
}

void tst_ShapeAnnotationEditor::testRotationTransformation()
{
    AnnotationLayer layer;
    auto shape = std::make_unique<ShapeAnnotation>(
        QRect(40, 40, 100, 60), ShapeType::Rectangle, Qt::red, 3);
    layer.addItem(std::move(shape));
    layer.setSelectedIndex(0);

    auto* shapeItem = dynamic_cast<ShapeAnnotation*>(layer.selectedItem());
    QVERIFY(shapeItem != nullptr);
    const QPointF center = shapeItem->center();

    ShapeAnnotationEditor editor;
    editor.setAnnotationLayer(&layer);
    editor.startTransformation((center + QPointF(80.0, 0.0)).toPoint(), GizmoHandle::Rotation);
    editor.updateTransformation((center + QPointF(0.0, 80.0)).toPoint());
    editor.finishTransformation();

    QVERIFY(qAbs(shapeItem->rotation() - 90.0) < 2.0);
}

void tst_ShapeAnnotationEditor::testScaleTransformation()
{
    AnnotationLayer layer;
    auto shape = std::make_unique<ShapeAnnotation>(
        QRect(40, 40, 100, 60), ShapeType::Rectangle, Qt::red, 3);
    layer.addItem(std::move(shape));
    layer.setSelectedIndex(0);

    auto* shapeItem = dynamic_cast<ShapeAnnotation*>(layer.selectedItem());
    QVERIFY(shapeItem != nullptr);
    const QPointF center = shapeItem->center();

    ShapeAnnotationEditor editor;
    editor.setAnnotationLayer(&layer);
    editor.startTransformation((center + QPointF(60.0, 0.0)).toPoint(), GizmoHandle::TopRight);
    editor.updateTransformation((center + QPointF(120.0, 0.0)).toPoint());
    editor.finishTransformation();

    QVERIFY(qAbs(shapeItem->scaleX() - 2.0) < 0.1);
    QVERIFY(qAbs(shapeItem->scaleY() - 2.0) < 0.1);
}

QTEST_MAIN(tst_ShapeAnnotationEditor)
#include "tst_ShapeAnnotationEditor.moc"
