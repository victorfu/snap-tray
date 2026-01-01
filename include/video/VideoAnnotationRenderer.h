#ifndef VIDEO_ANNOTATION_RENDERER_H
#define VIDEO_ANNOTATION_RENDERER_H

#include "video/VideoAnnotation.h"
#include <QImage>
#include <QPainter>
#include <QRect>
#include <QSize>

class AnnotationTrack;

class VideoAnnotationRenderer {
public:
    VideoAnnotationRenderer();
    ~VideoAnnotationRenderer();

    // Render annotations directly onto a video frame (for export)
    void renderToFrame(QImage &frame, const AnnotationTrack *track, qint64 timeMs,
                       qint64 videoDurationMs, const QString &selectedId = QString());

    // Render annotations to a widget (for preview, with coordinate transformation)
    void renderToWidget(QPainter &painter, const AnnotationTrack *track, qint64 timeMs,
                        qint64 videoDurationMs, const QRect &targetRect,
                        const QSize &videoSize, const QString &selectedId = QString());

    // Render a single annotation
    void renderAnnotation(QPainter &painter, const VideoAnnotation &annotation,
                          const QSizeF &renderSize, qint64 timeMs, qint64 videoDurationMs,
                          bool isSelected = false);

    // Hit testing for selection
    bool hitTest(const VideoAnnotation &annotation, const QPointF &point,
                 const QSizeF &renderSize) const;

    // Settings
    void setSelectionHandleSize(int size);
    int selectionHandleSize() const;

private:
    void renderArrow(QPainter &painter, const VideoAnnotation &ann, const QSizeF &size,
                     qreal opacity);
    void renderLine(QPainter &painter, const VideoAnnotation &ann, const QSizeF &size,
                    qreal opacity);
    void renderRectangle(QPainter &painter, const VideoAnnotation &ann, const QSizeF &size,
                         qreal opacity);
    void renderEllipse(QPainter &painter, const VideoAnnotation &ann, const QSizeF &size,
                       qreal opacity);
    void renderPencil(QPainter &painter, const VideoAnnotation &ann, const QSizeF &size,
                      qreal opacity);
    void renderMarker(QPainter &painter, const VideoAnnotation &ann, const QSizeF &size,
                      qreal opacity);
    void renderText(QPainter &painter, const VideoAnnotation &ann, const QSizeF &size,
                    qreal opacity);
    void renderStepBadge(QPainter &painter, const VideoAnnotation &ann, const QSizeF &size,
                         qreal opacity);
    void renderBlur(QPainter &painter, const VideoAnnotation &ann, const QSizeF &size,
                    qreal opacity, QImage *sourceImage = nullptr);
    void renderHighlight(QPainter &painter, const VideoAnnotation &ann, const QSizeF &size,
                         qreal opacity);
    void renderSelectionHandles(QPainter &painter, const VideoAnnotation &ann,
                                const QSizeF &size);

    // Helpers
    QPointF toPixels(const QPointF &relativePoint, const QSizeF &size) const;
    QRectF toPixels(const QRectF &relativeRect, const QSizeF &size) const;
    void drawArrowhead(QPainter &painter, const QPointF &tip, const QPointF &from,
                       qreal size);

    int m_selectionHandleSize = 8;
};

#endif // VIDEO_ANNOTATION_RENDERER_H
