#include "annotations/AnnotationStyle.h"

#include <array>

#include "annotations/ArrowAnnotation.h"
#include "annotations/PolylineAnnotation.h"
#include "annotations/ShapeAnnotation.h"
#include "annotations/TextBoxAnnotation.h"

namespace {

constexpr unsigned bit(StyleProperty p) { return 1u << static_cast<unsigned>(p); }

constexpr unsigned kLineProperties = bit(StyleProperty::Color) | bit(StyleProperty::Width)
    | bit(StyleProperty::ArrowStyle) | bit(StyleProperty::LineStyle);

// Indexed by StyleTargetKind.
constexpr std::array<unsigned, 5> kSupported = {
    0u,                                                                     // None
    bit(StyleProperty::Color) | bit(StyleProperty::FontFamily) | bit(StyleProperty::FontSize)
        | bit(StyleProperty::Bold) | bit(StyleProperty::Italic) | bit(StyleProperty::Underline),  // Text
    kLineProperties,                                                        // Arrow
    kLineProperties,                                                        // Polyline
    bit(StyleProperty::Color) | bit(StyleProperty::Width) | bit(StyleProperty::Fill),  // Shape
};

template <typename Line>
bool applyLine(Line& line, StyleProperty property, const QVariant& value)
{
    switch (property) {
    case StyleProperty::Color: {
        const QColor color = value.value<QColor>();
        if (!color.isValid() || color == line.color()) return false;
        line.setColor(color);
        return true;
    }
    case StyleProperty::Width: {
        const int width = value.toInt();
        if (width < 1 || width == line.width()) return false;
        line.setWidth(width);
        return true;
    }
    case StyleProperty::ArrowStyle: {
        const auto style = static_cast<LineEndStyle>(value.toInt());
        if (style == line.lineEndStyle()) return false;
        line.setLineEndStyle(style);
        return true;
    }
    case StyleProperty::LineStyle: {
        const auto style = static_cast<LineStyle>(value.toInt());
        if (style == line.lineStyle()) return false;
        line.setLineStyle(style);
        return true;
    }
    default:
        return false;
    }
}

bool applyShape(ShapeAnnotation& shape, StyleProperty property, const QVariant& value)
{
    switch (property) {
    case StyleProperty::Color: {
        const QColor color = value.value<QColor>();
        if (!color.isValid() || color == shape.color()) return false;
        shape.setColor(color);
        return true;
    }
    case StyleProperty::Width: {
        const int width = value.toInt();
        if (width < 1 || width == shape.width()) return false;
        shape.setWidth(width);
        return true;
    }
    case StyleProperty::Fill:
        if (value.toBool() == shape.filled()) return false;
        shape.setFilled(value.toBool());
        return true;
    default:
        return false;
    }
}

bool applyText(TextBoxAnnotation& text, StyleProperty property, const QVariant& value)
{
    if (property == StyleProperty::Color) {
        const QColor color = value.value<QColor>();
        if (!color.isValid() || color == text.color()) return false;
        text.setColor(color);
        return true;
    }
    QFont font = text.font();
    switch (property) {
    case StyleProperty::FontFamily:
        font.setFamilies(value.toString().isEmpty() ? QFont().families() : QStringList{value.toString()});
        break;
    case StyleProperty::FontSize:
        if (value.toInt() < 1) return false;
        font.setPointSize(value.toInt());
        break;
    case StyleProperty::Bold: font.setBold(value.toBool()); break;
    case StyleProperty::Italic: font.setItalic(value.toBool()); break;
    case StyleProperty::Underline: font.setUnderline(value.toBool()); break;
    default: return false;
    }
    if (font == text.font()) return false;
    text.setFont(font);
    return true;
}

} // namespace

namespace AnnotationStyle {

StyleTargetKind kindOf(const AnnotationItem& item)
{
    if (dynamic_cast<const TextBoxAnnotation*>(&item)) return StyleTargetKind::Text;
    if (dynamic_cast<const ArrowAnnotation*>(&item)) return StyleTargetKind::Arrow;
    if (dynamic_cast<const PolylineAnnotation*>(&item)) return StyleTargetKind::Polyline;
    if (dynamic_cast<const ShapeAnnotation*>(&item)) return StyleTargetKind::Shape;
    return StyleTargetKind::None;
}

AnnotationStyleValues read(const AnnotationItem& item)
{
    AnnotationStyleValues v;
    v.kind = kindOf(item);
    if (auto* text = dynamic_cast<const TextBoxAnnotation*>(&item)) {
        v.color = text->color();
        v.font = text->font();
    } else if (auto* arrow = dynamic_cast<const ArrowAnnotation*>(&item)) {
        v.color = arrow->color();
        v.width = arrow->width();
        v.arrowStyle = static_cast<int>(arrow->lineEndStyle());
        v.lineStyle = static_cast<int>(arrow->lineStyle());
    } else if (auto* line = dynamic_cast<const PolylineAnnotation*>(&item)) {
        v.color = line->color();
        v.width = line->width();
        v.arrowStyle = static_cast<int>(line->lineEndStyle());
        v.lineStyle = static_cast<int>(line->lineStyle());
    } else if (auto* shape = dynamic_cast<const ShapeAnnotation*>(&item)) {
        v.color = shape->color();
        v.width = shape->width();
        v.filled = shape->filled();
        v.shapeType = static_cast<int>(shape->shapeType());
    }
    return v;
}

bool supports(StyleTargetKind kind, StyleProperty property)
{
    return (kSupported[static_cast<int>(kind)] & bit(property)) != 0;
}

bool copyStyle(const AnnotationItem& from, AnnotationItem& to)
{
    const StyleTargetKind kind = kindOf(from);
    if (kind == StyleTargetKind::None || kind != kindOf(to)) return false;
    const AnnotationStyleValues v = read(from);
    if (auto* text = dynamic_cast<TextBoxAnnotation*>(&to)) {
        text->setColor(v.color);
        text->setFont(v.font);
    } else if (auto* arrow = dynamic_cast<ArrowAnnotation*>(&to)) {
        arrow->setColor(v.color);
        arrow->setWidth(v.width);
        arrow->setLineEndStyle(static_cast<LineEndStyle>(v.arrowStyle));
        arrow->setLineStyle(static_cast<LineStyle>(v.lineStyle));
    } else if (auto* line = dynamic_cast<PolylineAnnotation*>(&to)) {
        line->setColor(v.color);
        line->setWidth(v.width);
        line->setLineEndStyle(static_cast<LineEndStyle>(v.arrowStyle));
        line->setLineStyle(static_cast<LineStyle>(v.lineStyle));
    } else if (auto* shape = dynamic_cast<ShapeAnnotation*>(&to)) {
        shape->setColor(v.color);
        shape->setWidth(v.width);
        shape->setFilled(v.filled);
    }
    return true;
}

bool equalStyle(const AnnotationItem& a, const AnnotationItem& b)
{
    const AnnotationStyleValues x = read(a);
    const AnnotationStyleValues y = read(b);
    return x.kind != StyleTargetKind::None && x.kind == y.kind && x.color == y.color
        && x.width == y.width && x.filled == y.filled && x.arrowStyle == y.arrowStyle
        && x.lineStyle == y.lineStyle && x.font == y.font;
}

bool apply(AnnotationItem& item, StyleProperty property, const QVariant& value)
{
    const StyleTargetKind kind = kindOf(item);
    if (!supports(kind, property)) return false;
    if (auto* text = dynamic_cast<TextBoxAnnotation*>(&item)) return applyText(*text, property, value);
    if (auto* arrow = dynamic_cast<ArrowAnnotation*>(&item)) return applyLine(*arrow, property, value);
    if (auto* line = dynamic_cast<PolylineAnnotation*>(&item)) return applyLine(*line, property, value);
    if (auto* shape = dynamic_cast<ShapeAnnotation*>(&item)) return applyShape(*shape, property, value);
    return false;
}

} // namespace AnnotationStyle
