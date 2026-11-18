#include "recording/ShapeTrack.h"
#include <QJsonArray>
#include <QDebug>
#include <QPainter>

namespace SnapTray {
bool ShapeSegment::operator==(const ShapeSegment& o) const
{
    return shape == o.shape && space == o.space && startMs == o.startMs && endMs == o.endMs
        && fadeInMs == o.fadeInMs && fadeOutMs == o.fadeOutMs;
}
double ShapeSegment::opacityAt(qint64 ms) const
{
    if (ms < startMs || (endMs >= 0 && ms >= endMs)) return 0;
    double opacity = 1;
    if (fadeInMs > 0) opacity = qMin(opacity, double(ms - startMs) / fadeInMs);
    if (fadeOutMs > 0 && endMs >= 0) opacity = qMin(opacity, double(endMs - ms) / fadeOutMs);
    return opacity;
}
void ShapeTrack::paint(QPainter& p, qint64 ms, ShapeSegment::Space space,
                       const QRectF& target, const QRectF& bounds, const QSize& sourceSize) const
{
    if (target.isEmpty() || bounds.isEmpty() || sourceSize.isEmpty()) return;
    p.save(); p.setClipRect(target, Qt::IntersectClip);
    for (const auto& segment : segments) {
        const double opacity = segment.opacityAt(ms);
        if (segment.space != space || opacity <= 0) continue;
        auto shape = segment.shape;
        p.save(); p.setOpacity(p.opacity() * opacity);
        if (space == ShapeSegment::Content) {
            p.translate(target.topLeft());
            p.scale(target.width() / bounds.width(), target.height() / bounds.height());
            p.translate(-bounds.topLeft());
            shape.draw(p, sourceSize);
        } else {
            shape.from = {shape.from.x() * bounds.width(), shape.from.y() * bounds.height()};
            shape.to = {shape.to.x() * bounds.width(), shape.to.y() * bounds.height()};
            p.translate(target.topLeft());
            p.scale(target.width() / bounds.width(), target.height() / bounds.height());
            shape.draw(p, bounds.size());
        }
        p.restore();
    }
    p.restore();
}
void ShapeTrack::paint(QImage& frame, qint64 ms, ShapeSegment::Space space,
                       const QRectF& bounds, const QSize& sourceSize) const
{
    QPainter p(&frame); paint(p, ms, space, frame.rect(), bounds, sourceSize);
}
QJsonObject ShapeTrack::toJson() const
{
    QJsonArray array;
    for (const auto& s : segments) {
        auto j = s.shape.toJson();
        j.insert("space", int(s.space)); j.insert("startMs", s.startMs); j.insert("endMs", s.endMs);
        j.insert("fadeInMs", s.fadeInMs); j.insert("fadeOutMs", s.fadeOutMs); array.append(j);
    }
    return {{"shapes", array}};
}
ShapeTrack ShapeTrack::fromJson(const QJsonObject& json)
{
    ShapeTrack result;
    constexpr qint64 maxTime = 24LL * 60 * 60 * 1000;
    const auto entries = json.value("shapes").toArray();
    for (const auto& value : entries) {
        if (result.segments.size() >= kMaxSegments) break;
        const auto j = value.toObject();
        const auto shape = ShapeParameters::fromJson(j);
        if (!shape) continue;
        ShapeSegment s; s.shape = *shape;
        const int space = j.value("space").toInt();
        if (space < 0 || space > 1) continue;
        s.space = ShapeSegment::Space(space);
        s.startMs = j.value("startMs").toInteger(); s.endMs = j.value("endMs").toInteger(-1);
        s.fadeInMs = j.value("fadeInMs").toInteger(150); s.fadeOutMs = j.value("fadeOutMs").toInteger(150);
        if (s.startMs < 0 || s.startMs > maxTime || (s.endMs != -1 && s.endMs <= s.startMs)
            || s.endMs > maxTime || s.fadeInMs < 0 || s.fadeInMs > kMaxFadeMs || s.fadeOutMs < 0 || s.fadeOutMs > kMaxFadeMs) continue;
        result.segments.push_back(s);
    }
    const auto dropped = entries.size() - qsizetype(result.segments.size());
    if (dropped) qWarning() << "ShapeTrack: ignored" << dropped << "invalid or excess shapes";
    return result;
}
}
