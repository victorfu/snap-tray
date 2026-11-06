#pragma once

#include <QString>

enum class LineEndStyle;
enum class LineStyle;

// Translated names of arrow end and line styles, shared by the style menus and
// the screen reader names of the buttons that open them (#248).
namespace AnnotationStyleLabels {

QString arrowStyle(LineEndStyle style);
QString lineStyle(LineStyle style);

} // namespace AnnotationStyleLabels
