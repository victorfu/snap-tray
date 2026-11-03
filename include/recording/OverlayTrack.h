#pragma once

#include <QColor>
#include <QImage>
#include <QJsonObject>
#include <QPointF>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QtGlobal>

#include <vector>

class QPainter;

// Text and image overlays on the exported video (#188): titles, captions,
// a logo. Each is shown for a range of source time, placed on the exported
// picture (after crop and zoom: a title does not zoom), and fades in and
// out. Drawn after masks, so a mask never hides an overlay, and inside the
// #184 frame.
//
// Geometry is relative to the exported picture, so an overlay keeps its
// place and size at any resolution: `position` is the top-left as a
// fraction of the picture, `size` is the text height as a fraction of the
// picture's shorter side, or the image width as a fraction of its width.
// An image is kept in memory once added: deleting its file never breaks a
// later export.
namespace SnapTray {

struct OverlaySegment {
    enum class Kind { Text, Image };
    enum class Background { None, Dark, Light };
    enum class Align { Left, Center, Right };

    Kind kind = Kind::Text;
    qint64 startMs = 0;
    qint64 endMs = -1; // -1 = to the end
    QPointF position{0.05, 0.05};
    double size = 0.06;
    qint64 fadeMs = 300;
    // Text
    QString text;
    QColor color{Qt::white};
    Background background = Background::Dark;
    Align align = Align::Left;
    // Image
    QImage image;          // straight from the file, at most kMaxImageEdge
    double opacity = 1.0;

    static constexpr double kMinSize = 0.01;
    static constexpr double kMaxSize = 1.0;
    static constexpr int kMaxImageEdge = 2048;
    static constexpr qint64 kMinRangeMs = 100;

    bool isActiveAt(qint64 ms) const { return ms >= startMs && (endMs < 0 || ms < endMs); }
    double opacityAt(qint64 ms) const; // fades, times the image opacity
    bool operator==(const OverlaySegment& o) const;
};

struct OverlayTrack {
    std::vector<OverlaySegment> segments; // drawn in order: later on top

    bool isEmpty() const { return segments.empty(); }
    bool operator==(const OverlayTrack& o) const { return segments == o.segments; }
    bool operator!=(const OverlayTrack& o) const { return !(*this == o); }

    // Where a segment is drawn on a picture of this size.
    static QRectF boundsOf(const OverlaySegment& segment, const QSize& pictureSize);
    // Draws the overlays active at sourceMs onto a picture of `pictureSize`
    // shown at `target` (the frame itself for exports, a view rect for the preview).
    void paint(QPainter& painter, qint64 sourceMs, const QRectF& target, const QSize& pictureSize) const;
    void paint(QImage& frame, qint64 sourceMs) const;

    // Images are stored as PNG (for projects, #191); invalid entries are dropped.
    QJsonObject toJson() const;
    static OverlayTrack fromJson(const QJsonObject& json);
    // An image fit for an overlay: decoded, at most kMaxImageEdge.
    static QImage loadImage(const QString& path);
};

} // namespace SnapTray
