#include "annotations/ShapeParameters.h"
#include "annotations/ArrowAnnotation.h"
#include "annotations/ShapeAnnotation.h"
#include "annotations/StepBadgeAnnotation.h"
#include <QPainter>
#include <array>
#include <cmath>

namespace {
// Existing arrow/badge constructors take integer geometry. Supersampling the
// parameter space preserves subpixel placement without changing their look.
constexpr double kGeometryPrecision = 16;
using Draw = void (*)(const ShapeParameters&, QPainter&, int);
void arrow(const ShapeParameters& s, QPainter& p, int w) {
    ArrowAnnotation item((s.from * kGeometryPrecision).toPoint(), (s.to * kGeometryPrecision).toPoint(), s.color, w,
        s.kind == ShapeParameters::Line ? LineEndStyle::None : LineEndStyle(s.arrowStyle));
    item.draw(p);
}
void shape(const ShapeParameters& s, QPainter& p, int w) {
    QColor color = s.color;
    if (s.kind == ShapeParameters::Highlight) color.setAlphaF(color.alphaF() * 0.35);
    ShapeAnnotation item({}, s.kind == ShapeParameters::Ellipse ? ShapeType::Ellipse : ShapeType::Rectangle,
        color, w, s.kind == ShapeParameters::Highlight);
    item.setRectF(QRectF(s.from * kGeometryPrecision, s.to * kGeometryPrecision).normalized());
    item.draw(p);
}
void step(const ShapeParameters& s, QPainter& p, int) {
    const QRectF rect(s.from * kGeometryPrecision, s.to * kGeometryPrecision);
    StepBadgeAnnotation item(rect.center().toPoint(), s.color, s.number,
        qMax(1, qRound(qMin(qAbs(rect.width()), qAbs(rect.height())) / 2)));
    item.draw(p);
}
constexpr std::array<Draw, 6> draws{{arrow, shape, shape, arrow, shape, step}};
}
bool ShapeParameters::operator==(const ShapeParameters& o) const
{
    return kind == o.kind && from == o.from && to == o.to && color == o.color
        && width == o.width && arrowStyle == o.arrowStyle && number == o.number;
}
QJsonObject ShapeParameters::toJson() const
{
    return {{"kind", int(kind)}, {"x1", from.x()}, {"y1", from.y()}, {"x2", to.x()}, {"y2", to.y()},
        {"color", color.name(QColor::HexArgb)}, {"width", width}, {"arrowStyle", arrowStyle}, {"number", number}};
}
std::optional<ShapeParameters> ShapeParameters::fromJson(const QJsonObject& j)
{
    ShapeParameters s;
    const int k = j.value("kind").toInt(-1);
    if (k < Arrow || k > Step) return {};
    s.kind = Kind(k);
    s.from = {j.value("x1").toDouble(), j.value("y1").toDouble()};
    s.to = {j.value("x2").toDouble(), j.value("y2").toDouble()};
    for (double v : {s.from.x(), s.from.y(), s.to.x(), s.to.y()})
        if (!std::isfinite(v) || qAbs(v) > 100000) return {};
    s.color = QColor(j.value("color").toString());
    s.width = j.value("width").toDouble(4);
    s.arrowStyle = j.value("arrowStyle").toInt(1);
    s.number = j.value("number").toInt(1);
    if (!s.color.isValid() || !std::isfinite(s.width) || s.width < 1 || s.width > 64
        || s.arrowStyle < 0 || s.arrowStyle > 5 || s.number < 1 || s.number > 999) return {};
    return s;
}
void ShapeParameters::draw(QPainter& painter, const QSizeF& referenceSize) const
{
    if (kind < Arrow || kind > Step || referenceSize.isEmpty()) return;
    const double pen = width * qMin(referenceSize.width(), referenceSize.height()) / 1080.0;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(1 / kGeometryPrecision, 1 / kGeometryPrecision);
    draws[size_t(kind)](*this, painter, qMax(1, qRound(pen * kGeometryPrecision)));
    painter.restore();
}
