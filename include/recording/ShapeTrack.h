#pragma once
#include "annotations/ShapeParameters.h"
#include <QImage>
#include <QRectF>
#include <vector>

namespace SnapTray {
struct ShapeSegment {
    enum Space { Content, Screen };
    ShapeParameters shape;
    Space space = Content;
    qint64 startMs = 0, endMs = -1, fadeInMs = 150, fadeOutMs = 150;
    bool operator==(const ShapeSegment& o) const;
    double opacityAt(qint64 sourceMs) const;
};
struct ShapeTrack {
    std::vector<ShapeSegment> segments;
    bool isEmpty() const { return segments.empty(); }
    bool operator==(const ShapeTrack& o) const { return segments == o.segments; }
    bool operator!=(const ShapeTrack& o) const { return !(*this == o); }
    // Content is drawn into source pixels after masking and before zoom.
    // Screen uses proportions of the final picture, after cursor and keys.
    void paint(QPainter& painter, qint64 ms, ShapeSegment::Space space,
               const QRectF& target, const QRectF& sourceBounds, const QSize& sourceSize) const;
    void paint(QImage& frame, qint64 ms, ShapeSegment::Space space,
               const QRectF& sourceBounds, const QSize& sourceSize) const;
    QJsonObject toJson() const;
    static ShapeTrack fromJson(const QJsonObject& json);
};
}
