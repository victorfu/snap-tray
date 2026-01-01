#ifndef VIDEO_ANNOTATION_H
#define VIDEO_ANNOTATION_H

#include <QColor>
#include <QFont>
#include <QJsonObject>
#include <QList>
#include <QPointF>
#include <QRectF>
#include <QString>

enum class VideoAnnotationType {
    Arrow,
    Line,
    Rectangle,
    Ellipse,
    Pencil,
    Marker,
    Text,
    StepBadge,
    Blur,
    Highlight
};

enum class AnnotationAnimation {
    None,
    FadeIn,
    FadeOut,
    FadeInOut,
    Pop,
    SlideIn
};

struct VideoAnnotation {
    QString id;
    VideoAnnotationType type = VideoAnnotationType::Arrow;
    qint64 startTimeMs = 0;
    qint64 endTimeMs = -1; // -1 = until end of video

    // Position (relative coordinates 0.0-1.0)
    QPointF startPoint;
    QPointF endPoint;
    QList<QPointF> path; // For Pencil/Marker strokes

    // Appearance
    QColor color = Qt::red;
    int lineWidth = 3;
    bool filled = false;
    qreal opacity = 1.0;

    // Type-specific properties
    QString text;
    QFont font;
    int stepNumber = 1;
    int blurRadius = 15;

    // Animation
    AnnotationAnimation enterAnimation = AnnotationAnimation::None;
    AnnotationAnimation exitAnimation = AnnotationAnimation::None;
    int animationDurationMs = 200;

    // Layer order
    int zOrder = 0;

    // Utility methods
    bool isVisibleAt(qint64 timeMs, qint64 videoDurationMs) const;
    qreal opacityAt(qint64 timeMs, qint64 videoDurationMs) const;
    QRectF boundingRect() const;

    // Coordinate conversion helpers
    QPointF absoluteStartPoint(const QSizeF &videoSize) const;
    QPointF absoluteEndPoint(const QSizeF &videoSize) const;
    QList<QPointF> absolutePath(const QSizeF &videoSize) const;

    // Serialization
    QJsonObject toJson() const;
    static VideoAnnotation fromJson(const QJsonObject &json);

    // ID generation
    static QString generateId();
};

#endif // VIDEO_ANNOTATION_H
