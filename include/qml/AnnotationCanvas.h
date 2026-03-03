#pragma once

#include <QQuickPaintedItem>

/**
 * QQuickPaintedItem subclass that serves as the annotation drawing surface
 * within QML. This bridges the existing Qt Widgets annotation rendering
 * into the Qt Quick scene graph.
 */
class AnnotationCanvas : public QQuickPaintedItem
{
    Q_OBJECT
    QML_ELEMENT

public:
    explicit AnnotationCanvas(QQuickItem *parent = nullptr);

    void paint(QPainter *painter) override;
};
