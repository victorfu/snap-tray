#include "recording/RedactionTrack.h"

#include <QJsonArray>
#include <QPainter>
#include <QtMath>

#include <algorithm>

namespace SnapTray {

namespace {

struct StyleName {
    RedactionMask::Style style;
    const char* name;
};

constexpr StyleName kStyles[] = {
    {RedactionMask::Style::Solid, "solid"},
    {RedactionMask::Style::Pixelate, "pixelate"},
    {RedactionMask::Style::Blur, "blur"},
};

const QColor kSolidColor(0, 0, 0);

QImage reduced(const QImage& area, int factor, Qt::TransformationMode mode)
{
    const QSize small(std::max(1, area.width() / factor), std::max(1, area.height() / factor));
    return area.scaled(small, Qt::IgnoreAspectRatio, mode)
        .scaled(area.size(), Qt::IgnoreAspectRatio, mode);
}

} // namespace

bool RedactionMask::isActiveAt(qint64 frameMs, qint64 frameEndMs) const
{
    const bool beforeEnd = endMs < 0 || frameMs <= endMs;
    return beforeEnd && qMax(frameEndMs, frameMs + RedactionTrack::kFrameHoldMs) > startMs;
}

bool RedactionMask::operator==(const RedactionMask& o) const
{
    return rect == o.rect && startMs == o.startMs && endMs == o.endMs && style == o.style;
}

void RedactionTrack::apply(QImage& frame, qint64 frameMs, const QRectF& frameSourceRect, qint64 frameEndMs) const
{
    if (masks.empty() || frame.isNull() || frameSourceRect.isEmpty()) return;
    const double sx = frame.width() / frameSourceRect.width();
    const double sy = frame.height() / frameSourceRect.height();
    QPainter painter;
    for (const auto& mask : masks) {
        if (!mask.isActiveAt(frameMs, frameEndMs)) continue;
        // Into frame pixels, rounded outward so a fractional edge is covered.
        const QRectF mapped((mask.rect.x() - frameSourceRect.x()) * sx, (mask.rect.y() - frameSourceRect.y()) * sy,
                            mask.rect.width() * sx, mask.rect.height() * sy);
        const QRect covered = QRect(QPoint(qFloor(mapped.left()), qFloor(mapped.top())),
                                    QPoint(qCeil(mapped.right()) - 1, qCeil(mapped.bottom()) - 1))
                                  .intersected(frame.rect());
        if (covered.isEmpty()) continue;
        if (!painter.isActive()) painter.begin(&frame);
        switch (mask.style) {
        case RedactionMask::Style::Solid:
            painter.fillRect(covered, kSolidColor);
            break;
        case RedactionMask::Style::Pixelate:
            painter.drawImage(covered.topLeft(),
                              reduced(frame.copy(covered), std::max(1, qRound(kPixelateBlock * sx)),
                                      Qt::FastTransformation));
            break;
        case RedactionMask::Style::Blur:
            painter.drawImage(covered.topLeft(),
                              reduced(frame.copy(covered), std::max(1, qRound(kBlurReduction * sx)),
                                      Qt::SmoothTransformation));
            break;
        }
    }
}

QJsonObject RedactionTrack::toJson() const
{
    QJsonArray list;
    for (const auto& mask : masks) {
        const char* style = "solid";
        for (const auto& entry : kStyles) {
            if (entry.style == mask.style) style = entry.name;
        }
        list.append(QJsonObject{{QStringLiteral("x"), mask.rect.x()},
                                {QStringLiteral("y"), mask.rect.y()},
                                {QStringLiteral("w"), mask.rect.width()},
                                {QStringLiteral("h"), mask.rect.height()},
                                {QStringLiteral("start"), mask.startMs},
                                {QStringLiteral("end"), mask.endMs},
                                {QStringLiteral("style"), QLatin1String(style)}});
    }
    return {{QStringLiteral("masks"), list}};
}

RedactionTrack RedactionTrack::fromJson(const QJsonObject& json)
{
    RedactionTrack track;
    for (const auto& value : json.value(QStringLiteral("masks")).toArray()) {
        const QJsonObject o = value.toObject();
        RedactionMask mask;
        mask.rect = QRect(o.value(QStringLiteral("x")).toInt(), o.value(QStringLiteral("y")).toInt(),
                          o.value(QStringLiteral("w")).toInt(), o.value(QStringLiteral("h")).toInt());
        mask.startMs = o.value(QStringLiteral("start")).toInteger();
        mask.endMs = o.value(QStringLiteral("end")).toInteger(-1);
        const QString style = o.value(QStringLiteral("style")).toString();
        for (const auto& entry : kStyles) {
            if (style == QLatin1String(entry.name)) mask.style = entry.style;
        }
        if (mask.rect.width() < kMinMaskSide || mask.rect.height() < kMinMaskSide) continue;
        track.masks.push_back(mask);
    }
    return track;
}

} // namespace SnapTray
