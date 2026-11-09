#pragma once

#include <QColor>
#include <QFont>
#include <QVariant>

class AnnotationItem;

// Style properties the tool options strip can change on a selected annotation (#224).
enum class StyleProperty {
    Color = 0,
    Width,
    Fill,
    ArrowStyle,
    LineStyle,
    FontFamily,
    FontSize,
    Bold,
    Italic,
    Underline,
};

enum class StyleTargetKind { None = 0, Text, Arrow, Polyline, Shape };

struct AnnotationStyleValues {
    StyleTargetKind kind = StyleTargetKind::None;
    QColor color;
    int width = 0;
    bool filled = false;
    int shapeType = 0;    // ShapeType, for the strip label
    int arrowStyle = 0;   // LineEndStyle
    int lineStyle = 0;    // LineStyle
    QFont font;
};

namespace AnnotationStyle {
StyleTargetKind kindOf(const AnnotationItem& item);
AnnotationStyleValues read(const AnnotationItem& item);
bool supports(StyleTargetKind kind, StyleProperty property);
// Returns false without changing the item when unsupported or unchanged.
bool apply(AnnotationItem& item, StyleProperty property, const QVariant& value);
// Copies only the style (colour, width, fill, line ends, line style, font) of
// `from` onto `to`; geometry, text, and visibility of `to` are untouched.
// Returns false when the two items are not the same stylable kind.
bool copyStyle(const AnnotationItem& from, AnnotationItem& to);
// True when the two items are the same stylable kind with equal style values.
bool equalStyle(const AnnotationItem& a, const AnnotationItem& b);
} // namespace AnnotationStyle
