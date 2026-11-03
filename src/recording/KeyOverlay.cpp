#include "recording/KeyOverlay.h"

#include <QFont>
#include <QFontMetricsF>
#include <QImage>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>

namespace SnapTray {

namespace {

constexpr double kSizeFractions[] = {0.035, 0.05, 0.07}; // text height, of the shorter side
constexpr double kMarginFraction = 0.05;
constexpr double kPaddingEm = 0.45;
constexpr double kGapEm = 0.35;
constexpr double kRadiusEm = 0.35;
constexpr int kMinFontPx = 9;

template <typename Enum>
struct Named {
    Enum value;
    const char* name;
};

constexpr Named<KeyOverlaySettings::Position> kPositions[] = {
    {KeyOverlaySettings::Position::BottomCenter, "bottomCenter"},
    {KeyOverlaySettings::Position::BottomLeft, "bottomLeft"},
    {KeyOverlaySettings::Position::BottomRight, "bottomRight"},
    {KeyOverlaySettings::Position::TopCenter, "topCenter"},
};
constexpr Named<KeyOverlaySettings::Size> kSizes[] = {
    {KeyOverlaySettings::Size::Small, "small"},
    {KeyOverlaySettings::Size::Medium, "medium"},
    {KeyOverlaySettings::Size::Large, "large"},
};
constexpr Named<KeyOverlaySettings::Style> kStyles[] = {
    {KeyOverlaySettings::Style::Dark, "dark"},
    {KeyOverlaySettings::Style::Light, "light"},
};

template <typename Enum, size_t N>
const char* nameOf(const Named<Enum> (&table)[N], Enum value)
{
    for (const auto& entry : table) {
        if (entry.value == value) return entry.name;
    }
    return table[0].name;
}

template <typename Enum, size_t N>
Enum valueOf(const Named<Enum> (&table)[N], const QString& name, Enum fallback)
{
    for (const auto& entry : table) {
        if (name == QLatin1String(entry.name)) return entry.value;
    }
    return fallback;
}

} // namespace

bool KeyOverlaySettings::operator==(const KeyOverlaySettings& o) const
{
    return enabled == o.enabled && position == o.position && size == o.size && style == o.style
        && displayMs == o.displayMs;
}

QJsonObject KeyOverlaySettings::toJson() const
{
    return {{QStringLiteral("enabled"), enabled},
            {QStringLiteral("position"), QLatin1String(nameOf(kPositions, position))},
            {QStringLiteral("size"), QLatin1String(nameOf(kSizes, size))},
            {QStringLiteral("style"), QLatin1String(nameOf(kStyles, style))},
            {QStringLiteral("displayMs"), double(displayMs)}};
}

KeyOverlaySettings KeyOverlaySettings::fromJson(const QJsonObject& json)
{
    KeyOverlaySettings s;
    s.enabled = json.value(QStringLiteral("enabled")).toBool(s.enabled);
    s.position = valueOf(kPositions, json.value(QStringLiteral("position")).toString(), s.position);
    s.size = valueOf(kSizes, json.value(QStringLiteral("size")).toString(), s.size);
    s.style = valueOf(kStyles, json.value(QStringLiteral("style")).toString(), s.style);
    const qint64 display = qint64(json.value(QStringLiteral("displayMs")).toDouble(double(s.displayMs)));
    if (std::find(std::begin(kDisplayChoices), std::end(kDisplayChoices), display) != std::end(kDisplayChoices)) {
        s.displayMs = display;
    }
    return s;
}

KeyOverlayRenderer::KeyOverlayRenderer(const KeyTimeline& timeline, const KeyOverlaySettings& settings)
    : m_timeline(timeline)
    , m_settings(settings)
{
    m_hints.reserve(timeline.strokes.size());
    for (size_t i = 0; i < timeline.strokes.size(); ++i) {
        const auto& stroke = timeline.strokes[i];
        const QString label = keyStrokeLabel(stroke, timeline.info.macSymbols);
        const bool continuesGroup = i > 0
            && stroke.tMs - timeline.strokes[i - 1].tMs < KeyOverlaySettings::kMergeGapMs;
        const size_t groupStart = continuesGroup ? m_hints.back().groupStart : i;
        const size_t runStart = continuesGroup && label == m_hints.back().label ? m_hints.back().runStart : i;
        m_hints.push_back({label, groupStart, runStart});
    }
}

void KeyOverlayRenderer::setTimeline(const TimelineMap& timeline)
{
    m_kept = timeline.segments();
}

qint64 KeyOverlayRenderer::partStart(qint64 ms) const
{
    if (m_kept.empty()) return std::numeric_limits<qint64>::min() / 2;
    auto it = std::upper_bound(m_kept.begin(), m_kept.end(), ms,
                               [](qint64 t, const TimelineSegment& s) { return t < s.startMs; });
    if (it != m_kept.begin()) {
        --it;
        if (it->contains(ms)) return it->startMs;
    }
    return std::numeric_limits<qint64>::max() / 2; // cut out: nothing shows
}

bool KeyOverlayRenderer::groupAt(qint64 sourceMs, size_t* first, size_t* last) const
{
    const auto& strokes = m_timeline.strokes;
    auto it = std::upper_bound(strokes.begin(), strokes.end(), sourceMs,
                               [](qint64 t, const KeyStroke& s) { return t < s.tMs; });
    if (it == strokes.begin()) return false;
    const qint64 from = partStart(sourceMs);
    size_t end = size_t(it - strokes.begin()) - 1;
    if (strokes[end].tMs < from || sourceMs >= strokes[end].tMs + m_settings.displayMs) return false;
    size_t start = m_hints[end].groupStart;
    // A trim or cut can bisect a cached group. Find its first kept stroke
    // without walking the preceding typing run on every frame.
    if (strokes[start].tMs < from) {
        start = size_t(std::lower_bound(strokes.begin() + ptrdiff_t(start), it, from,
                                       [](const KeyStroke& s, qint64 t) { return s.tMs < t; }) - strokes.begin());
    }
    *first = start;
    *last = end;
    return true;
}

QStringList KeyOverlayRenderer::tokensAt(qint64 sourceMs) const
{
    size_t first = 0, last = 0;
    if (!m_settings.enabled || !groupAt(sourceMs, &first, &last)) return {};
    QStringList tokens;
    tokens.reserve(KeyOverlaySettings::kMaxTokens);
    size_t end = last;
    while (tokens.size() < KeyOverlaySettings::kMaxTokens) {
        const auto& hint = m_hints[end];
        const size_t start = std::max(first, hint.runStart);
        const size_t repeats = end - start + 1;
        tokens.prepend(repeats > 1 ? QStringLiteral("%1 \u00D7%2").arg(hint.label).arg(qulonglong(repeats)) : hint.label);
        if (start == first) break;
        end = start - 1;
    }
    return tokens;
}

double KeyOverlayRenderer::opacityAt(qint64 sourceMs) const
{
    size_t first = 0, last = 0;
    if (!m_settings.enabled || !groupAt(sourceMs, &first, &last)) return 0.0;
    const auto& strokes = m_timeline.strokes;
    const double in = std::clamp(double(sourceMs - strokes[first].tMs) / KeyOverlaySettings::kFadeInMs, 0.0, 1.0);
    const qint64 end = strokes[last].tMs + m_settings.displayMs;
    const double out = std::clamp(double(end - sourceMs) / KeyOverlaySettings::kFadeOutMs, 0.0, 1.0);
    return std::min(in, out);
}

void KeyOverlayRenderer::paint(QImage& frame, qint64 sourceMs) const
{
    if (tokensAt(sourceMs).isEmpty()) return;
    QPainter painter(&frame);
    paint(painter, sourceMs, QRectF(QPointF(0, 0), QSizeF(frame.size())), frame.size());
}

void KeyOverlayRenderer::paint(QPainter& painter, qint64 sourceMs, const QRectF& target, const QSize& pictureSize) const
{
    const QStringList tokens = tokensAt(sourceMs);
    const double opacity = opacityAt(sourceMs);
    if (tokens.isEmpty() || opacity <= 0.0 || target.isEmpty() || pictureSize.isEmpty()) return;
    // Laid out in picture pixels, then mapped onto the target.
    const double shortSide = std::min(pictureSize.width(), pictureSize.height());
    QFont font;
    font.setPixelSize(std::max(kMinFontPx, int(std::lround(shortSide * kSizeFractions[int(m_settings.size)]))));
    font.setWeight(QFont::DemiBold);
    const QFontMetricsF metrics(font);
    const double em = font.pixelSize();
    const double padding = em * kPaddingEm, gap = em * kGapEm, height = metrics.height() + 2 * padding;
    std::vector<double> widths;
    double total = 0.0;
    for (const auto& token : tokens) {
        widths.push_back(metrics.horizontalAdvance(token) + 2 * padding);
        total += widths.back();
    }
    total += gap * (tokens.size() - 1);
    const double margin = shortSide * kMarginFraction;
    double x = (pictureSize.width() - total) / 2.0;
    double y = pictureSize.height() - margin - height;
    switch (m_settings.position) {
    case KeyOverlaySettings::Position::BottomCenter: break;
    case KeyOverlaySettings::Position::BottomLeft: x = margin; break;
    case KeyOverlaySettings::Position::BottomRight: x = pictureSize.width() - margin - total; break;
    case KeyOverlaySettings::Position::TopCenter: y = margin; break;
    }
    const bool dark = m_settings.style == KeyOverlaySettings::Style::Dark;
    const QColor fill = dark ? QColor(20, 20, 20, 210) : QColor(250, 250, 250, 225);
    const QColor text = dark ? QColor(Qt::white) : QColor(17, 17, 17);
    const QColor edge = dark ? QColor(255, 255, 255, 60) : QColor(0, 0, 0, 50);

    painter.save();
    painter.translate(target.topLeft());
    painter.scale(target.width() / pictureSize.width(), target.height() / pictureSize.height());
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setOpacity(painter.opacity() * opacity);
    painter.setFont(font);
    for (size_t i = 0; i < size_t(tokens.size()); ++i) {
        const QRectF cap(x, y, widths[i], height);
        painter.setPen(QPen(edge, std::max(1.0, em / 16.0)));
        painter.setBrush(fill);
        painter.drawRoundedRect(cap, em * kRadiusEm, em * kRadiusEm);
        painter.setPen(text);
        painter.drawText(cap, Qt::AlignCenter, tokens[int(i)]);
        x += widths[i] + gap;
    }
    painter.restore();
}

} // namespace SnapTray
