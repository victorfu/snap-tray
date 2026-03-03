#include "qml/AnnotationCanvas.h"

#include <QPainter>

AnnotationCanvas::AnnotationCanvas(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setRenderTarget(QQuickPaintedItem::FramebufferObject);
}

void AnnotationCanvas::paint(QPainter *painter)
{
    // Stub: paint a test pattern to verify the QML pipeline works
    const int w = static_cast<int>(width());
    const int h = static_cast<int>(height());

    // Light background
    painter->fillRect(0, 0, w, h, QColor(245, 245, 245));

    // Draw a grid pattern
    painter->setPen(QPen(QColor(200, 200, 200), 1));
    const int gridSize = 20;
    for (int x = 0; x < w; x += gridSize)
        painter->drawLine(x, 0, x, h);
    for (int y = 0; y < h; y += gridSize)
        painter->drawLine(0, y, w, y);

    // Draw a centered label
    painter->setPen(QPen(QColor(100, 100, 100), 1));
    QFont font;
    font.setPixelSize(16);
    painter->setFont(font);
    painter->drawText(QRect(0, 0, w, h), Qt::AlignCenter, QStringLiteral("AnnotationCanvas"));

    // Draw a diagonal cross to confirm rendering
    painter->setPen(QPen(QColor(70, 130, 180), 2));
    painter->drawLine(0, 0, w, h);
    painter->drawLine(w, 0, 0, h);
}
