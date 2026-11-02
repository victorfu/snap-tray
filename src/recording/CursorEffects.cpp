#include "recording/CursorEffects.h"

#include <QPainter>
#include <QPainterPath>
#include <QtMath>

#include <algorithm>
#include <cmath>

namespace SnapTray {

namespace {

struct SmoothingTau {
    CursorEffectSettings::Smoothing smoothing;
    const char* name;
    double tauMs; // time constant of the low-pass filter
};

constexpr SmoothingTau kSmoothing[] = {
    {CursorEffectSettings::Smoothing::Off, "off", 0.0},
    {CursorEffectSettings::Smoothing::Low, "low", 40.0},
    {CursorEffectSettings::Smoothing::Medium, "medium", 80.0},
    {CursorEffectSettings::Smoothing::High, "high", 140.0},
};

// The filter looks this many time constants back; older positions weigh
// less than e^-6 (0.25 %).
constexpr double kSmoothingWindowTaus = 6.0;

// Ripple look, in logical pixels of the recorded screen.
constexpr double kRippleStartRadius = 6.0;
constexpr double kRippleEndRadius = 28.0;
constexpr double kRippleRingWidth = 3.0;
constexpr int kRippleFillAlpha = 70;
constexpr int kRippleRingAlpha = 220;

// The generic pointer for timelines without shapes, in logical pixels.
constexpr double kDefaultArrowHeight = 22.0;

QPainterPath defaultArrow()
{
    // Tip at (0, 0): the hotspot. Unit height, scaled when drawn.
    QPainterPath path;
    path.moveTo(0.0, 0.0);
    path.lineTo(0.0, 0.80);
    path.lineTo(0.20, 0.62);
    path.lineTo(0.33, 0.92);
    path.lineTo(0.44, 0.87);
    path.lineTo(0.31, 0.58);
    path.lineTo(0.56, 0.58);
    path.closeSubpath();
    return path;
}

} // namespace

bool CursorEffectSettings::operator==(const CursorEffectSettings& o) const
{
    return showCursor == o.showCursor && qFuzzyCompare(scale, o.scale) && smoothing == o.smoothing
        && clickRipple == o.clickRipple;
}

QJsonObject CursorEffectSettings::toJson() const
{
    const char* smoothingName = "off";
    for (const auto& entry : kSmoothing) {
        if (entry.smoothing == smoothing) smoothingName = entry.name;
    }
    return {{QStringLiteral("showCursor"), showCursor},
            {QStringLiteral("scale"), scale},
            {QStringLiteral("smoothing"), QLatin1String(smoothingName)},
            {QStringLiteral("clickRipple"), clickRipple}};
}

CursorEffectSettings CursorEffectSettings::fromJson(const QJsonObject& json)
{
    CursorEffectSettings settings;
    settings.showCursor = json.value(QStringLiteral("showCursor")).toBool(settings.showCursor);
    settings.scale = std::clamp(json.value(QStringLiteral("scale")).toDouble(settings.scale), kMinScale, kMaxScale);
    const QString smoothing = json.value(QStringLiteral("smoothing")).toString();
    for (const auto& entry : kSmoothing) {
        if (smoothing == QLatin1String(entry.name)) settings.smoothing = entry.smoothing;
    }
    settings.clickRipple = json.value(QStringLiteral("clickRipple")).toBool(settings.clickRipple);
    return settings;
}

CursorEffectAvailability CursorEffectAvailability::of(const CursorTimeline* timeline)
{
    CursorEffectAvailability availability;
    if (!timeline || timeline->samples.empty() || timeline->info.cursorInVideo) {
        // No data, or the pointer is already in the picture: drawing another
        // one would double it.
        return availability;
    }
    availability.cursor = true;
    availability.clicks = timeline->info.clicksAvailable;
    return availability;
}

QPointF CursorFrameMapping::toOutput(const QPointF& sourcePoint) const
{
    const QRectF crop = sourceCrop.isEmpty() ? QRectF(QPointF(0, 0), QSizeF(sourceSize)) : sourceCrop;
    if (crop.isEmpty() || outputSize.isEmpty()) return {};
    return QPointF((sourcePoint.x() - crop.x()) * outputSize.width() / crop.width(),
                   (sourcePoint.y() - crop.y()) * outputSize.height() / crop.height());
}

double CursorFrameMapping::scale() const
{
    const QRectF crop = sourceCrop.isEmpty() ? QRectF(QPointF(0, 0), QSizeF(sourceSize)) : sourceCrop;
    return crop.isEmpty() ? 1.0 : double(outputSize.width()) / crop.width();
}

CursorEffectRenderer::CursorEffectRenderer(const CursorTimeline& timeline, const CursorEffectSettings& settings)
    : m_timeline(timeline)
    , m_settings(settings)
{
}

double CursorEffectRenderer::smoothingTauMs(CursorEffectSettings::Smoothing smoothing)
{
    for (const auto& entry : kSmoothing) {
        if (entry.smoothing == smoothing) return entry.tauMs;
    }
    return 0.0;
}

QPointF CursorEffectRenderer::rawPosition(qint64 sourceMs) const
{
    const CursorSample* sample = m_timeline.sampleAt(sourceMs);
    return sample ? QPointF(sample->position) : QPointF();
}

QPointF CursorEffectRenderer::smoothedPosition(qint64 sourceMs, double tauMs) const
{
    // Exponential low-pass of the recorded (piecewise constant) path, in
    // closed form over a fixed window: a function of time only.
    const auto& samples = m_timeline.samples;
    const double windowStart = double(sourceMs) - kSmoothingWindowTaus * tauMs;
    auto it = std::upper_bound(samples.begin(), samples.end(), sourceMs,
                               [](qint64 t, const CursorSample& s) { return t < s.tMs; });
    if (it == samples.begin()) return QPointF(samples.front().position);
    QPointF sum;
    double weight = 0.0;
    double segmentEnd = double(sourceMs);
    for (auto s = it; s != samples.begin();) {
        --s;
        // The first sample also stands for the time before it.
        const double segmentStart = s == samples.begin() ? windowStart : std::max(double(s->tMs), windowStart);
        if (segmentEnd > segmentStart) {
            const double w = std::exp(-(double(sourceMs) - segmentEnd) / tauMs)
                - std::exp(-(double(sourceMs) - segmentStart) / tauMs);
            sum += QPointF(s->position) * w;
            weight += w;
        }
        segmentEnd = segmentStart;
        if (segmentStart <= windowStart) break;
    }
    return weight > 0.0 ? sum / weight : QPointF((it - 1)->position);
}

qint64 CursorEffectRenderer::distanceToButtonEvent(qint64 sourceMs) const
{
    const auto& buttons = m_timeline.buttons;
    auto it = std::lower_bound(buttons.begin(), buttons.end(), sourceMs,
                               [](const CursorButtonEvent& e, qint64 t) { return e.tMs < t; });
    qint64 distance = std::numeric_limits<qint64>::max();
    if (it != buttons.end()) distance = it->tMs - sourceMs;
    if (it != buttons.begin()) distance = std::min(distance, sourceMs - (it - 1)->tMs);
    return distance;
}

std::optional<QPointF> CursorEffectRenderer::pointerPosition(qint64 sourceMs) const
{
    const CursorSample* sample = m_timeline.sampleAt(sourceMs);
    if (!sample || sourceMs < m_timeline.samples.front().tMs || !sample->visible) return std::nullopt;
    const QPointF raw(sample->position);
    double tau = smoothingTauMs(m_settings.smoothing);
    if (tau <= 0.0) return raw;
    if (m_timeline.buttonsDownAt(sourceMs)) tau *= kDragSmoothingFactor;
    // Near a press or release the pointer is exactly where the click was:
    // smoothing fades out instead of drifting toward another target.
    const double fade = std::clamp(double(distanceToButtonEvent(sourceMs)) / kClickSnapMs, 0.0, 1.0);
    return raw + (smoothedPosition(sourceMs, tau) - raw) * fade;
}

void CursorEffectRenderer::paint(QImage& frame, qint64 sourceMs, const CursorFrameMapping& mapping) const
{
    if (!m_settings.drawsAnything()) return;
    QPainter painter(&frame);
    paint(painter, sourceMs, mapping);
}

void CursorEffectRenderer::paint(QPainter& painter, qint64 sourceMs, const CursorFrameMapping& mapping) const
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    if (m_settings.clickRipple && m_timeline.info.clicksAvailable) paintRipples(painter, sourceMs, mapping);
    if (m_settings.showCursor) paintPointer(painter, sourceMs, mapping);
    painter.restore();
}

void CursorEffectRenderer::paintRipples(QPainter& painter, qint64 sourceMs, const CursorFrameMapping& mapping) const
{
    const double unit = m_timeline.info.pixelRatio * mapping.scale() * m_settings.scale;
    const auto& buttons = m_timeline.buttons;
    auto it = std::lower_bound(buttons.begin(), buttons.end(), sourceMs - kRippleDurationMs + 1,
                               [](const CursorButtonEvent& e, qint64 t) { return e.tMs < t; });
    for (; it != buttons.end() && it->tMs <= sourceMs; ++it) {
        if (!it->down || it->synthetic) continue;
        // Anchored to the real press: time and place, never the smoothed path.
        const double progress = double(sourceMs - it->tMs) / kRippleDurationMs;
        const double eased = 1.0 - std::pow(1.0 - progress, 3.0);
        const double radius = (kRippleStartRadius + (kRippleEndRadius - kRippleStartRadius) * eased) * unit;
        const double fade = 1.0 - progress;
        const QPointF center = mapping.toOutput(QPointF(it->position));
        painter.setPen(QPen(QColor(0, 0, 0, int(kRippleRingAlpha * fade * 0.5)), (kRippleRingWidth + 1.5) * unit));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(center, radius, radius);
        painter.setPen(QPen(QColor(255, 255, 255, int(kRippleRingAlpha * fade)), kRippleRingWidth * unit));
        painter.setBrush(QColor(255, 255, 255, int(kRippleFillAlpha * fade)));
        painter.drawEllipse(center, radius, radius);
    }
}

void CursorEffectRenderer::paintPointer(QPainter& painter, qint64 sourceMs, const CursorFrameMapping& mapping) const
{
    const auto position = pointerPosition(sourceMs);
    if (!position) return;
    const QPointF at = mapping.toOutput(*position);
    const double scale = mapping.scale() * m_settings.scale;
    const CursorSample* sample = m_timeline.sampleAt(sourceMs);
    const CursorShape* shape = sample ? m_timeline.shape(sample->shapeId) : nullptr;
    if (shape) {
        // Shape images are in recorded (video) pixels; the hotspot stays on the point.
        const QRectF target(at - QPointF(shape->hotspot) * scale, QSizeF(shape->image.size()) * scale);
        painter.drawImage(target, shape->image);
        return;
    }
    // No recorded shape: a plain arrow with an outline, readable on any background.
    const double height = kDefaultArrowHeight * m_timeline.info.pixelRatio * scale;
    QPainterPath arrow = defaultArrow();
    QTransform transform;
    transform.translate(at.x(), at.y());
    transform.scale(height, height);
    arrow = transform.map(arrow);
    painter.setPen(QPen(Qt::white, std::max(1.0, height / 14.0), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::black);
    painter.drawPath(arrow);
}

} // namespace SnapTray
