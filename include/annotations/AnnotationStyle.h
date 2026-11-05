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
} // namespace AnnotationStyle
