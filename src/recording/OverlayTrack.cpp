#include "recording/OverlayTrack.h"

#include <QBuffer>
#include <QFontMetricsF>
#include <QImageReader>
#include <QJsonArray>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace SnapTray {

namespace {

constexpr double kPaddingEm = 0.35;
constexpr double kRadiusEm = 0.3;
constexpr int kMinFontPx = 6;

template <typename Enum>
struct Named {
    Enum value;
    const char* name;
};
constexpr Named<OverlaySegment::Kind> kKinds[] = {{OverlaySegment::Kind::Text, "text"}, {OverlaySegment::Kind::Image, "image"}};
constexpr Named<OverlaySegment::Background> kBackgrounds[] = {
    {OverlaySegment::Background::None, "none"}, {OverlaySegment::Background::Dark, "dark"},
    {OverlaySegment::Background::Light, "light"}};
constexpr Named<OverlaySegment::Align> kAligns[] = {
    {OverlaySegment::Align::Left, "left"}, {OverlaySegment::Align::Center, "center"}, {OverlaySegment::Align::Right, "right"}};

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

QFont fontFor(const OverlaySegment& segment, const QSize& picture)
{
    QFont font;
    const double shortSide = std::min(picture.width(), picture.height());
    font.setPixelSize(std::max(kMinFontPx, int(std::lround(segment.size * shortSide))));
    font.setWeight(QFont::DemiBold);
    return font;
}

Qt::Alignment alignmentOf(OverlaySegment::Align align)
{
    switch (align) {
    case OverlaySegment::Align::Left: return Qt::AlignLeft;
    case OverlaySegment::Align::Center: return Qt::AlignHCenter;
    case OverlaySegment::Align::Right: return Qt::AlignRight;
    }
    return Qt::AlignLeft;
}

} // namespace

double OverlaySegment::opacityAt(qint64 ms) const
{
    if (!isActiveAt(ms)) return 0.0;
    double fade = 1.0;
    if (fadeMs > 0) {
        fade = std::min(fade, double(ms - startMs) / fadeMs);
        if (endMs >= 0) fade = std::min(fade, double(endMs - ms) / fadeMs);
    }
    fade = std::clamp(fade, 0.0, 1.0);
    return kind == Kind::Image ? fade * std::clamp(opacity, 0.0, 1.0) : fade;
}

bool OverlaySegment::operator==(const OverlaySegment& o) const
{
    // Images are never changed in place: the same pixels share a cache key.
    return kind == o.kind && startMs == o.startMs && endMs == o.endMs && position == o.position
        && qFuzzyCompare(size, o.size) && fadeMs == o.fadeMs && text == o.text && color == o.color
        && background == o.background && align == o.align && image.cacheKey() == o.image.cacheKey()
        && qFuzzyCompare(opacity + 1.0, o.opacity + 1.0);
}

QRectF OverlayTrack::boundsOf(const OverlaySegment& segment, const QSize& picture)
{
    if (picture.isEmpty()) return {};
    const QPointF at(segment.position.x() * picture.width(), segment.position.y() * picture.height());
    if (segment.kind == OverlaySegment::Kind::Image) {
        if (segment.image.isNull()) return {};
        const double width = segment.size * picture.width();
        return QRectF(at, QSizeF(width, width * segment.image.height() / segment.image.width()));
    }
    const QFont font = fontFor(segment, picture);
    const QFontMetricsF metrics(font);
    const QStringList lines = (segment.text.isEmpty() ? QStringLiteral(" ") : segment.text).split(QLatin1Char('\n'));
    double width = 0.0;
    for (const auto& line : lines) width = std::max(width, metrics.horizontalAdvance(line));
    const double padding = font.pixelSize() * kPaddingEm;
    return QRectF(at, QSizeF(width + 2 * padding, metrics.lineSpacing() * lines.size() + 2 * padding));
}

void OverlayTrack::paint(QImage& frame, qint64 sourceMs) const
{
    if (segments.empty()) return;
    QPainter painter(&frame);
    paint(painter, sourceMs, QRectF(QPointF(0, 0), QSizeF(frame.size())), frame.size());
}

void OverlayTrack::paint(QPainter& painter, qint64 sourceMs, const QRectF& target, const QSize& picture) const
{
    if (target.isEmpty() || picture.isEmpty()) return;
    painter.save();
    painter.translate(target.topLeft());
    painter.scale(target.width() / picture.width(), target.height() / picture.height());
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    const double baseOpacity = painter.opacity();
    for (const auto& segment : segments) {
        const double opacity = segment.opacityAt(sourceMs);
        if (opacity <= 0.0) continue;
        const QRectF bounds = boundsOf(segment, picture);
        if (bounds.isEmpty()) continue;
        painter.setOpacity(baseOpacity * opacity);
        if (segment.kind == OverlaySegment::Kind::Image) {
            painter.drawImage(bounds, segment.image);
            continue;
        }
        const QFont font = fontFor(segment, picture);
        const double em = font.pixelSize();
        if (segment.background != OverlaySegment::Background::None) {
            const bool dark = segment.background == OverlaySegment::Background::Dark;
            painter.setPen(Qt::NoPen);
            painter.setBrush(dark ? QColor(0, 0, 0, 160) : QColor(255, 255, 255, 200));
            painter.drawRoundedRect(bounds, em * kRadiusEm, em * kRadiusEm);
        }
        painter.setFont(font);
        painter.setPen(segment.color);
        const double padding = em * kPaddingEm;
        painter.drawText(bounds.adjusted(padding, padding, -padding, -padding),
                         int(alignmentOf(segment.align) | Qt::AlignTop), segment.text);
    }
    painter.setOpacity(baseOpacity);
    painter.restore();
}

QJsonObject OverlayTrack::toJson() const
{
    QJsonArray list;
    for (const auto& s : segments) {
        QJsonObject o{{QStringLiteral("kind"), QLatin1String(nameOf(kKinds, s.kind))},
                      {QStringLiteral("start"), double(s.startMs)},
                      {QStringLiteral("end"), double(s.endMs)},
                      {QStringLiteral("x"), s.position.x()},
                      {QStringLiteral("y"), s.position.y()},
                      {QStringLiteral("size"), s.size},
                      {QStringLiteral("fadeMs"), double(s.fadeMs)}};
        if (s.kind == OverlaySegment::Kind::Text) {
            o.insert(QStringLiteral("text"), s.text);
            o.insert(QStringLiteral("color"), s.color.name(QColor::HexArgb));
            o.insert(QStringLiteral("background"), QLatin1String(nameOf(kBackgrounds, s.background)));
            o.insert(QStringLiteral("align"), QLatin1String(nameOf(kAligns, s.align)));
        } else {
            QByteArray png;
            QBuffer buffer(&png);
            buffer.open(QIODevice::WriteOnly);
            s.image.save(&buffer, "PNG");
            o.insert(QStringLiteral("png"), QString::fromLatin1(png.toBase64()));
            o.insert(QStringLiteral("opacity"), s.opacity);
        }
        list.append(o);
    }
    return {{QStringLiteral("segments"), list}};
}

OverlayTrack OverlayTrack::fromJson(const QJsonObject& json)
{
    OverlayTrack track;
    for (const auto& value : json.value(QStringLiteral("segments")).toArray()) {
        const QJsonObject o = value.toObject();
        OverlaySegment s;
        s.kind = valueOf(kKinds, o.value(QStringLiteral("kind")).toString(), s.kind);
        s.startMs = qint64(o.value(QStringLiteral("start")).toDouble(0));
        s.endMs = qint64(o.value(QStringLiteral("end")).toDouble(-1));
        s.position = QPointF(std::clamp(o.value(QStringLiteral("x")).toDouble(0.05), 0.0, 1.0),
                             std::clamp(o.value(QStringLiteral("y")).toDouble(0.05), 0.0, 1.0));
        s.size = std::clamp(o.value(QStringLiteral("size")).toDouble(s.size), OverlaySegment::kMinSize, OverlaySegment::kMaxSize);
        s.fadeMs = std::max<qint64>(0, qint64(o.value(QStringLiteral("fadeMs")).toDouble(double(s.fadeMs))));
        if (s.startMs < 0 || (s.endMs >= 0 && s.endMs <= s.startMs)) continue;
        if (s.kind == OverlaySegment::Kind::Text) {
            s.text = o.value(QStringLiteral("text")).toString();
            const QColor color(o.value(QStringLiteral("color")).toString());
            if (color.isValid()) s.color = color;
            s.background = valueOf(kBackgrounds, o.value(QStringLiteral("background")).toString(), s.background);
            s.align = valueOf(kAligns, o.value(QStringLiteral("align")).toString(), s.align);
        } else {
            s.image = QImage::fromData(QByteArray::fromBase64(o.value(QStringLiteral("png")).toString().toLatin1()), "PNG");
            if (s.image.isNull()) continue;
            s.opacity = std::clamp(o.value(QStringLiteral("opacity")).toDouble(1.0), 0.0, 1.0);
        }
        track.segments.push_back(s);
    }
    return track;
}

QImage OverlayTrack::loadImage(const QString& path)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);
    QSize size = reader.size();
    // SVG and large images: decoded straight at a bounded size.
    if (size.isValid() && std::max(size.width(), size.height()) > OverlaySegment::kMaxImageEdge) {
        size.scale(OverlaySegment::kMaxImageEdge, OverlaySegment::kMaxImageEdge, Qt::KeepAspectRatio);
        reader.setScaledSize(size);
    }
    QImage image = reader.read();
    if (image.isNull()) return image;
    if (std::max(image.width(), image.height()) > OverlaySegment::kMaxImageEdge) {
        image = image.scaled(OverlaySegment::kMaxImageEdge, OverlaySegment::kMaxImageEdge, Qt::KeepAspectRatio,
                             Qt::SmoothTransformation);
    }
    return image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

} // namespace SnapTray
