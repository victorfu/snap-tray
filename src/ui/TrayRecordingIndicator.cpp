#include "ui/TrayRecordingIndicator.h"

#include <QColor>
#include <QPainter>
#include <QPen>
#include <QRectF>

namespace SnapTray {

namespace {

// Badge geometry, as fractions of the icon's shorter side.
constexpr qreal kBadgeDiameterRatio = 0.56;
constexpr qreal kBadgeOutlineRatio = 0.06;
constexpr qreal kStartingRingWidthRatio = 0.22;
constexpr qreal kPauseBarWidthRatio = 0.18;
constexpr qreal kPauseBarHeightRatio = 0.5;
constexpr qreal kPauseBarGapRatio = 0.14;
constexpr qreal kEllipsisDotRatio = 0.16;
constexpr qreal kEllipsisGapRatio = 0.1;

// Colors. The white outline separates the badge from both light and dark
// tray / menu bar backgrounds.
const QColor kBadgeOutlineColor(0xFF, 0xFF, 0xFF);
const QColor kRecordingColor(0xEF, 0x44, 0x44);
const QColor kPausedColor(0xF5, 0x9E, 0x0B);
const QColor kFinalizingColor(0x64, 0x74, 0x8B);
const QColor kGlyphColor(0xFF, 0xFF, 0xFF);

constexpr qint64 kMsPerSecond = 1000;
constexpr qint64 kSecondsPerMinute = 60;
constexpr qint64 kSecondsPerHour = 3600;

void drawStartingBadge(QPainter& painter, const QRectF& badge)
{
    // Hollow red ring on white: "about to record".
    painter.setBrush(kBadgeOutlineColor);
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(badge);

    const qreal ringWidth = badge.width() * kStartingRingWidthRatio;
    const qreal inset = ringWidth / 2.0 + badge.width() * kBadgeOutlineRatio;
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(kRecordingColor, ringWidth));
    painter.drawEllipse(badge.adjusted(inset, inset, -inset, -inset));
}

void drawFilledBadge(QPainter& painter, const QRectF& badge, const QColor& fill)
{
    painter.setPen(Qt::NoPen);
    painter.setBrush(kBadgeOutlineColor);
    painter.drawEllipse(badge);

    const qreal outline = badge.width() * kBadgeOutlineRatio;
    painter.setBrush(fill);
    painter.drawEllipse(badge.adjusted(outline, outline, -outline, -outline));
}

void drawPauseGlyph(QPainter& painter, const QRectF& badge)
{
    const qreal barWidth = badge.width() * kPauseBarWidthRatio;
    const qreal barHeight = badge.height() * kPauseBarHeightRatio;
    const qreal gap = badge.width() * kPauseBarGapRatio;
    const qreal top = badge.center().y() - barHeight / 2.0;
    const qreal left = badge.center().x() - gap / 2.0 - barWidth;

    painter.setPen(Qt::NoPen);
    painter.setBrush(kGlyphColor);
    painter.drawRect(QRectF(left, top, barWidth, barHeight));
    painter.drawRect(QRectF(left + barWidth + gap, top, barWidth, barHeight));
}

void drawEllipsisGlyph(QPainter& painter, const QRectF& badge)
{
    const qreal dot = badge.width() * kEllipsisDotRatio;
    const qreal gap = badge.width() * kEllipsisGapRatio;
    const qreal totalWidth = dot * 3.0 + gap * 2.0;
    const qreal top = badge.center().y() - dot / 2.0;
    qreal left = badge.center().x() - totalWidth / 2.0;

    painter.setPen(Qt::NoPen);
    painter.setBrush(kGlyphColor);
    for (int i = 0; i < 3; ++i) {
        painter.drawEllipse(QRectF(left, top, dot, dot));
        left += dot + gap;
    }
}

} // namespace

QPixmap composeTrayRecordingPixmap(const QPixmap& base, TrayRecordingStatus status)
{
    if (status == TrayRecordingStatus::Idle || base.isNull()) {
        return base;
    }

    QPixmap result = base.copy();
    result.setDevicePixelRatio(base.devicePixelRatio());

    const QSizeF logicalSize = base.deviceIndependentSize();
    const qreal diameter = qMin(logicalSize.width(), logicalSize.height()) * kBadgeDiameterRatio;
    const QRectF badge(logicalSize.width() - diameter,
                       logicalSize.height() - diameter,
                       diameter,
                       diameter);

    QPainter painter(&result);
    painter.setRenderHint(QPainter::Antialiasing);

    switch (status) {
    case TrayRecordingStatus::Preparing:
    case TrayRecordingStatus::Countdown:
        drawStartingBadge(painter, badge);
        break;
    case TrayRecordingStatus::Recording:
        drawFilledBadge(painter, badge, kRecordingColor);
        break;
    case TrayRecordingStatus::Paused:
        drawFilledBadge(painter, badge, kPausedColor);
        drawPauseGlyph(painter, badge);
        break;
    case TrayRecordingStatus::Finalizing:
        drawFilledBadge(painter, badge, kFinalizingColor);
        drawEllipsisGlyph(painter, badge);
        break;
    case TrayRecordingStatus::Idle:
        break;
    }

    return result;
}

QString formatTrayRecordingElapsed(qint64 elapsedMs)
{
    const qint64 totalSeconds = qMax<qint64>(0, elapsedMs) / kMsPerSecond;
    const qint64 hours = totalSeconds / kSecondsPerHour;
    const qint64 minutes = (totalSeconds % kSecondsPerHour) / kSecondsPerMinute;
    const qint64 seconds = totalSeconds % kSecondsPerMinute;

    if (hours > 0) {
        return QStringLiteral("%1:%2:%3")
            .arg(hours)
            .arg(minutes, 2, 10, QLatin1Char('0'))
            .arg(seconds, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1:%2")
        .arg(minutes)
        .arg(seconds, 2, 10, QLatin1Char('0'));
}

} // namespace SnapTray
