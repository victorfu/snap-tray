#include "video/VideoAnnotation.h"
#include <QJsonArray>
#include <QUuid>
#include <cmath>

bool VideoAnnotation::isVisibleAt(qint64 timeMs, qint64 videoDurationMs) const
{
    if (timeMs < startTimeMs) {
        return false;
    }

    qint64 effectiveEndTime = (endTimeMs < 0) ? videoDurationMs : endTimeMs;
    return timeMs <= effectiveEndTime;
}

qreal VideoAnnotation::opacityAt(qint64 timeMs, qint64 videoDurationMs) const
{
    if (!isVisibleAt(timeMs, videoDurationMs)) {
        return 0.0;
    }

    qint64 effectiveEndTime = (endTimeMs < 0) ? videoDurationMs : endTimeMs;
    qreal baseOpacity = opacity;

    // Handle enter animation
    if (enterAnimation != AnnotationAnimation::None) {
        qint64 animEndTime = startTimeMs + animationDurationMs;
        if (timeMs < animEndTime) {
            qreal t = static_cast<qreal>(timeMs - startTimeMs) / animationDurationMs;
            t = qBound(0.0, t, 1.0);

            switch (enterAnimation) {
            case AnnotationAnimation::FadeIn:
            case AnnotationAnimation::FadeInOut:
                baseOpacity *= t;
                break;
            case AnnotationAnimation::Pop:
                // Ease-out back for pop effect
                t = 1.0 - std::pow(1.0 - t, 3.0);
                baseOpacity *= t;
                break;
            case AnnotationAnimation::SlideIn:
                baseOpacity *= t;
                break;
            default:
                break;
            }
        }
    }

    // Handle exit animation
    if (exitAnimation != AnnotationAnimation::None ||
        enterAnimation == AnnotationAnimation::FadeInOut) {
        qint64 animStartTime = effectiveEndTime - animationDurationMs;
        if (timeMs > animStartTime) {
            qreal t = static_cast<qreal>(effectiveEndTime - timeMs) / animationDurationMs;
            t = qBound(0.0, t, 1.0);

            AnnotationAnimation exitAnim = (enterAnimation == AnnotationAnimation::FadeInOut)
                                               ? AnnotationAnimation::FadeOut
                                               : exitAnimation;

            switch (exitAnim) {
            case AnnotationAnimation::FadeOut:
                baseOpacity *= t;
                break;
            default:
                break;
            }
        }
    }

    return baseOpacity;
}

QRectF VideoAnnotation::boundingRect() const
{
    switch (type) {
    case VideoAnnotationType::Arrow:
    case VideoAnnotationType::Line:
        return QRectF(startPoint, endPoint).normalized();

    case VideoAnnotationType::Rectangle:
    case VideoAnnotationType::Ellipse:
    case VideoAnnotationType::Blur:
    case VideoAnnotationType::Highlight:
        return QRectF(startPoint, endPoint).normalized();

    case VideoAnnotationType::Pencil:
    case VideoAnnotationType::Marker:
        if (path.isEmpty()) {
            return QRectF();
        }
        {
            qreal minX = path[0].x(), maxX = path[0].x();
            qreal minY = path[0].y(), maxY = path[0].y();
            for (const auto &pt : path) {
                minX = qMin(minX, pt.x());
                maxX = qMax(maxX, pt.x());
                minY = qMin(minY, pt.y());
                maxY = qMax(maxY, pt.y());
            }
            return QRectF(minX, minY, maxX - minX, maxY - minY);
        }

    case VideoAnnotationType::Text:
    case VideoAnnotationType::StepBadge:
        // Approximate bounding rect for text/badge
        return QRectF(startPoint.x(), startPoint.y(), 0.2, 0.05);

    default:
        return QRectF();
    }
}

QPointF VideoAnnotation::absoluteStartPoint(const QSizeF &videoSize) const
{
    return QPointF(startPoint.x() * videoSize.width(), startPoint.y() * videoSize.height());
}

QPointF VideoAnnotation::absoluteEndPoint(const QSizeF &videoSize) const
{
    return QPointF(endPoint.x() * videoSize.width(), endPoint.y() * videoSize.height());
}

QList<QPointF> VideoAnnotation::absolutePath(const QSizeF &videoSize) const
{
    QList<QPointF> result;
    result.reserve(path.size());
    for (const auto &pt : path) {
        result.append(QPointF(pt.x() * videoSize.width(), pt.y() * videoSize.height()));
    }
    return result;
}

QJsonObject VideoAnnotation::toJson() const
{
    QJsonObject obj;
    obj["id"] = id;
    obj["type"] = static_cast<int>(type);
    obj["startTimeMs"] = startTimeMs;
    obj["endTimeMs"] = endTimeMs;

    // Position
    QJsonObject start;
    start["x"] = startPoint.x();
    start["y"] = startPoint.y();
    obj["startPoint"] = start;

    QJsonObject end;
    end["x"] = endPoint.x();
    end["y"] = endPoint.y();
    obj["endPoint"] = end;

    // Path for strokes
    if (!path.isEmpty()) {
        QJsonArray pathArray;
        for (const auto &pt : path) {
            QJsonObject ptObj;
            ptObj["x"] = pt.x();
            ptObj["y"] = pt.y();
            pathArray.append(ptObj);
        }
        obj["path"] = pathArray;
    }

    // Appearance
    obj["color"] = color.name(QColor::HexArgb);
    obj["lineWidth"] = lineWidth;
    obj["filled"] = filled;
    obj["opacity"] = opacity;

    // Type-specific
    if (!text.isEmpty()) {
        obj["text"] = text;
    }
    if (type == VideoAnnotationType::Text) {
        obj["fontFamily"] = font.family();
        obj["fontSize"] = font.pointSize();
        obj["fontBold"] = font.bold();
    }
    if (type == VideoAnnotationType::StepBadge) {
        obj["stepNumber"] = stepNumber;
    }
    if (type == VideoAnnotationType::Blur) {
        obj["blurRadius"] = blurRadius;
    }

    // Animation
    obj["enterAnimation"] = static_cast<int>(enterAnimation);
    obj["exitAnimation"] = static_cast<int>(exitAnimation);
    obj["animationDurationMs"] = animationDurationMs;

    obj["zOrder"] = zOrder;

    return obj;
}

VideoAnnotation VideoAnnotation::fromJson(const QJsonObject &json)
{
    VideoAnnotation ann;

    ann.id = json["id"].toString();
    ann.type = static_cast<VideoAnnotationType>(json["type"].toInt());
    ann.startTimeMs = json["startTimeMs"].toVariant().toLongLong();
    ann.endTimeMs = json["endTimeMs"].toVariant().toLongLong();

    // Position
    QJsonObject start = json["startPoint"].toObject();
    ann.startPoint = QPointF(start["x"].toDouble(), start["y"].toDouble());

    QJsonObject end = json["endPoint"].toObject();
    ann.endPoint = QPointF(end["x"].toDouble(), end["y"].toDouble());

    // Path
    if (json.contains("path")) {
        QJsonArray pathArray = json["path"].toArray();
        for (const auto &ptVal : pathArray) {
            QJsonObject ptObj = ptVal.toObject();
            ann.path.append(QPointF(ptObj["x"].toDouble(), ptObj["y"].toDouble()));
        }
    }

    // Appearance
    ann.color = QColor(json["color"].toString());
    ann.lineWidth = json["lineWidth"].toInt(3);
    ann.filled = json["filled"].toBool();
    ann.opacity = json["opacity"].toDouble(1.0);

    // Type-specific
    ann.text = json["text"].toString();
    if (json.contains("fontFamily")) {
        ann.font.setFamily(json["fontFamily"].toString());
        ann.font.setPointSize(json["fontSize"].toInt(12));
        ann.font.setBold(json["fontBold"].toBool());
    }
    ann.stepNumber = json["stepNumber"].toInt(1);
    ann.blurRadius = json["blurRadius"].toInt(15);

    // Animation
    ann.enterAnimation = static_cast<AnnotationAnimation>(json["enterAnimation"].toInt());
    ann.exitAnimation = static_cast<AnnotationAnimation>(json["exitAnimation"].toInt());
    ann.animationDurationMs = json["animationDurationMs"].toInt(200);

    ann.zOrder = json["zOrder"].toInt();

    return ann;
}

QString VideoAnnotation::generateId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}
