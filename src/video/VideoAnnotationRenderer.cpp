#include "video/VideoAnnotationRenderer.h"
#include "video/AnnotationTrack.h"
#include <QPainterPath>
#include <QStack>
#include <cmath>

VideoAnnotationRenderer::VideoAnnotationRenderer() = default;
VideoAnnotationRenderer::~VideoAnnotationRenderer() = default;

void VideoAnnotationRenderer::renderToFrame(QImage &frame, const AnnotationTrack *track,
                                            qint64 timeMs, qint64 videoDurationMs,
                                            const QString &selectedId)
{
    if (!track || track->isEmpty()) {
        return;
    }

    QPainter painter(&frame);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    QSizeF size(frame.width(), frame.height());
    QList<VideoAnnotation> visible = track->annotationsAt(timeMs, videoDurationMs);

    for (const auto &ann : visible) {
        bool isSelected = (ann.id == selectedId);
        renderAnnotation(painter, ann, size, timeMs, videoDurationMs, isSelected);
    }

    painter.end();
}

void VideoAnnotationRenderer::renderToWidget(QPainter &painter, const AnnotationTrack *track,
                                             qint64 timeMs, qint64 videoDurationMs,
                                             const QRect &targetRect, const QSize &videoSize,
                                             const QString &selectedId)
{
    if (!track || track->isEmpty()) {
        return;
    }

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);

    // Calculate scaling to fit video in target rect while maintaining aspect ratio
    qreal scaleX = static_cast<qreal>(targetRect.width()) / videoSize.width();
    qreal scaleY = static_cast<qreal>(targetRect.height()) / videoSize.height();
    qreal scale = qMin(scaleX, scaleY);

    qreal scaledWidth = videoSize.width() * scale;
    qreal scaledHeight = videoSize.height() * scale;

    // Center in target rect
    qreal offsetX = targetRect.x() + (targetRect.width() - scaledWidth) / 2.0;
    qreal offsetY = targetRect.y() + (targetRect.height() - scaledHeight) / 2.0;

    painter.translate(offsetX, offsetY);
    painter.scale(scale, scale);

    QSizeF size(videoSize);
    QList<VideoAnnotation> visible = track->annotationsAt(timeMs, videoDurationMs);

    for (const auto &ann : visible) {
        bool isSelected = (ann.id == selectedId);
        renderAnnotation(painter, ann, size, timeMs, videoDurationMs, isSelected);
    }

    painter.restore();
}

void VideoAnnotationRenderer::renderAnnotation(QPainter &painter,
                                               const VideoAnnotation &annotation,
                                               const QSizeF &renderSize, qint64 timeMs,
                                               qint64 videoDurationMs, bool isSelected)
{
    qreal opacity = annotation.opacityAt(timeMs, videoDurationMs);
    if (opacity <= 0.0) {
        return;
    }

    painter.save();

    switch (annotation.type) {
    case VideoAnnotationType::Arrow:
        renderArrow(painter, annotation, renderSize, opacity);
        break;
    case VideoAnnotationType::Line:
        renderLine(painter, annotation, renderSize, opacity);
        break;
    case VideoAnnotationType::Rectangle:
        renderRectangle(painter, annotation, renderSize, opacity);
        break;
    case VideoAnnotationType::Ellipse:
        renderEllipse(painter, annotation, renderSize, opacity);
        break;
    case VideoAnnotationType::Pencil:
        renderPencil(painter, annotation, renderSize, opacity);
        break;
    case VideoAnnotationType::Marker:
        renderMarker(painter, annotation, renderSize, opacity);
        break;
    case VideoAnnotationType::Text:
        renderText(painter, annotation, renderSize, opacity);
        break;
    case VideoAnnotationType::StepBadge:
        renderStepBadge(painter, annotation, renderSize, opacity);
        break;
    case VideoAnnotationType::Blur:
        renderBlur(painter, annotation, renderSize, opacity);
        break;
    case VideoAnnotationType::Highlight:
        renderHighlight(painter, annotation, renderSize, opacity);
        break;
    }

    if (isSelected) {
        renderSelectionHandles(painter, annotation, renderSize);
    }

    painter.restore();
}

bool VideoAnnotationRenderer::hitTest(const VideoAnnotation &annotation, const QPointF &point,
                                      const QSizeF &renderSize) const
{
    QRectF bounds = toPixels(annotation.boundingRect(), renderSize);
    // Add some padding for easier selection
    bounds.adjust(-5, -5, 5, 5);
    return bounds.contains(point);
}

void VideoAnnotationRenderer::setSelectionHandleSize(int size)
{
    m_selectionHandleSize = qBound(4, size, 16);
}

int VideoAnnotationRenderer::selectionHandleSize() const
{
    return m_selectionHandleSize;
}

void VideoAnnotationRenderer::renderArrow(QPainter &painter, const VideoAnnotation &ann,
                                          const QSizeF &size, qreal opacity)
{
    QPointF start = toPixels(ann.startPoint, size);
    QPointF end = toPixels(ann.endPoint, size);

    QColor color = ann.color;
    color.setAlphaF(color.alphaF() * opacity);

    QPen pen(color, ann.lineWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    // Draw line
    painter.drawLine(start, end);

    // Draw arrowhead
    painter.setBrush(color);
    drawArrowhead(painter, end, start, ann.lineWidth * 4);
}

void VideoAnnotationRenderer::renderLine(QPainter &painter, const VideoAnnotation &ann,
                                         const QSizeF &size, qreal opacity)
{
    QPointF start = toPixels(ann.startPoint, size);
    QPointF end = toPixels(ann.endPoint, size);

    QColor color = ann.color;
    color.setAlphaF(color.alphaF() * opacity);

    QPen pen(color, ann.lineWidth, Qt::SolidLine, Qt::RoundCap);
    painter.setPen(pen);
    painter.drawLine(start, end);
}

void VideoAnnotationRenderer::renderRectangle(QPainter &painter, const VideoAnnotation &ann,
                                              const QSizeF &size, qreal opacity)
{
    QRectF rect = toPixels(QRectF(ann.startPoint, ann.endPoint).normalized(), size);

    QColor color = ann.color;
    color.setAlphaF(color.alphaF() * opacity);

    QPen pen(color, ann.lineWidth);
    painter.setPen(pen);

    if (ann.filled) {
        QColor fillColor = color;
        fillColor.setAlphaF(fillColor.alphaF() * 0.3);
        painter.setBrush(fillColor);
    } else {
        painter.setBrush(Qt::NoBrush);
    }

    painter.drawRect(rect);
}

void VideoAnnotationRenderer::renderEllipse(QPainter &painter, const VideoAnnotation &ann,
                                            const QSizeF &size, qreal opacity)
{
    QRectF rect = toPixels(QRectF(ann.startPoint, ann.endPoint).normalized(), size);

    QColor color = ann.color;
    color.setAlphaF(color.alphaF() * opacity);

    QPen pen(color, ann.lineWidth);
    painter.setPen(pen);

    if (ann.filled) {
        QColor fillColor = color;
        fillColor.setAlphaF(fillColor.alphaF() * 0.3);
        painter.setBrush(fillColor);
    } else {
        painter.setBrush(Qt::NoBrush);
    }

    painter.drawEllipse(rect);
}

void VideoAnnotationRenderer::renderPencil(QPainter &painter, const VideoAnnotation &ann,
                                           const QSizeF &size, qreal opacity)
{
    if (ann.path.size() < 2) {
        return;
    }

    QColor color = ann.color;
    color.setAlphaF(color.alphaF() * opacity);

    QPen pen(color, ann.lineWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    QPainterPath path;
    QPointF first = toPixels(ann.path[0], size);
    path.moveTo(first);

    for (int i = 1; i < ann.path.size(); ++i) {
        path.lineTo(toPixels(ann.path[i], size));
    }

    painter.drawPath(path);
}

void VideoAnnotationRenderer::renderMarker(QPainter &painter, const VideoAnnotation &ann,
                                           const QSizeF &size, qreal opacity)
{
    if (ann.path.size() < 2) {
        return;
    }

    QColor color = ann.color;
    color.setAlphaF(color.alphaF() * opacity * 0.5); // Markers are semi-transparent

    QPen pen(color, ann.lineWidth * 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    QPainterPath path;
    QPointF first = toPixels(ann.path[0], size);
    path.moveTo(first);

    for (int i = 1; i < ann.path.size(); ++i) {
        path.lineTo(toPixels(ann.path[i], size));
    }

    painter.drawPath(path);
}

void VideoAnnotationRenderer::renderText(QPainter &painter, const VideoAnnotation &ann,
                                         const QSizeF &size, qreal opacity)
{
    QPointF pos = toPixels(ann.startPoint, size);

    QColor color = ann.color;
    color.setAlphaF(color.alphaF() * opacity);

    QFont font = ann.font;
    if (font.pointSize() <= 0) {
        font.setPointSize(16);
    }

    painter.setFont(font);
    painter.setPen(color);
    painter.drawText(pos, ann.text);
}

void VideoAnnotationRenderer::renderStepBadge(QPainter &painter, const VideoAnnotation &ann,
                                              const QSizeF &size, qreal opacity)
{
    QPointF center = toPixels(ann.startPoint, size);
    qreal radius = 16;

    QColor color = ann.color;
    color.setAlphaF(color.alphaF() * opacity);

    // Draw circle background
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawEllipse(center, radius, radius);

    // Draw number
    QColor textColor = Qt::white;
    textColor.setAlphaF(opacity);
    painter.setPen(textColor);

    QFont font;
    font.setPointSize(12);
    font.setBold(true);
    painter.setFont(font);

    QRectF textRect(center.x() - radius, center.y() - radius, radius * 2, radius * 2);
    painter.drawText(textRect, Qt::AlignCenter, QString::number(ann.stepNumber));
}

void VideoAnnotationRenderer::renderBlur(QPainter &painter, const VideoAnnotation &ann,
                                         const QSizeF &size, qreal opacity,
                                         QImage *sourceImage)
{
    Q_UNUSED(opacity)

    QRectF rect = toPixels(QRectF(ann.startPoint, ann.endPoint).normalized(), size);

    if (sourceImage && !sourceImage->isNull()) {
        // Extract the region and apply blur
        QRect intRect = rect.toRect().intersected(sourceImage->rect());
        if (intRect.isEmpty()) {
            return;
        }

        QImage region = sourceImage->copy(intRect);

        // Simple box blur implementation
        int radius = ann.blurRadius;
        QImage blurred = region.scaled(region.width() / (radius / 2 + 1),
                                       region.height() / (radius / 2 + 1), Qt::IgnoreAspectRatio,
                                       Qt::SmoothTransformation);
        blurred = blurred.scaled(region.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

        painter.drawImage(intRect, blurred);
    } else {
        // Fallback: draw a semi-transparent overlay to indicate blur area
        QColor blurColor(128, 128, 128, 100);
        painter.fillRect(rect, blurColor);
    }
}

void VideoAnnotationRenderer::renderHighlight(QPainter &painter, const VideoAnnotation &ann,
                                              const QSizeF &size, qreal opacity)
{
    QRectF rect = toPixels(QRectF(ann.startPoint, ann.endPoint).normalized(), size);

    QColor color = ann.color;
    color.setAlphaF(0.3 * opacity); // Always semi-transparent highlight

    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawRect(rect);
}

void VideoAnnotationRenderer::renderSelectionHandles(QPainter &painter,
                                                     const VideoAnnotation &ann,
                                                     const QSizeF &size)
{
    QRectF bounds = toPixels(ann.boundingRect(), size);
    if (bounds.isEmpty()) {
        return;
    }

    qreal hs = m_selectionHandleSize;
    qreal halfHs = hs / 2.0;

    // Selection outline
    QPen outlinePen(QColor(0, 120, 215), 1, Qt::DashLine);
    painter.setPen(outlinePen);
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(bounds);

    // Handle positions: corners and edge midpoints
    QVector<QPointF> handles = {
        bounds.topLeft(),
        QPointF(bounds.center().x(), bounds.top()),
        bounds.topRight(),
        QPointF(bounds.right(), bounds.center().y()),
        bounds.bottomRight(),
        QPointF(bounds.center().x(), bounds.bottom()),
        bounds.bottomLeft(),
        QPointF(bounds.left(), bounds.center().y())};

    painter.setPen(QPen(QColor(0, 120, 215), 1));
    painter.setBrush(Qt::white);

    for (const auto &pos : handles) {
        painter.drawRect(QRectF(pos.x() - halfHs, pos.y() - halfHs, hs, hs));
    }
}

QPointF VideoAnnotationRenderer::toPixels(const QPointF &relativePoint, const QSizeF &size) const
{
    return QPointF(relativePoint.x() * size.width(), relativePoint.y() * size.height());
}

QRectF VideoAnnotationRenderer::toPixels(const QRectF &relativeRect, const QSizeF &size) const
{
    return QRectF(relativeRect.x() * size.width(), relativeRect.y() * size.height(),
                  relativeRect.width() * size.width(), relativeRect.height() * size.height());
}

void VideoAnnotationRenderer::drawArrowhead(QPainter &painter, const QPointF &tip,
                                            const QPointF &from, qreal arrowSize)
{
    QPointF direction = tip - from;
    qreal length = std::sqrt(direction.x() * direction.x() + direction.y() * direction.y());

    if (length < 0.001) {
        return;
    }

    direction /= length;

    // Perpendicular
    QPointF perp(-direction.y(), direction.x());

    QPointF base = tip - direction * arrowSize;
    QPointF left = base + perp * (arrowSize * 0.5);
    QPointF right = base - perp * (arrowSize * 0.5);

    QPolygonF arrowHead;
    arrowHead << tip << left << right;

    painter.drawPolygon(arrowHead);
}
