#include "annotations/AnnotationStyleLabels.h"

#include "annotations/ArrowAnnotation.h"
#include "annotations/LineStyle.h"

#include <QCoreApplication>

namespace {

constexpr const char* kContext = "AnnotationStyleLabels";

struct ArrowStyleLabel {
    LineEndStyle value;
    const char* text;
};

constexpr ArrowStyleLabel kArrowStyleLabels[] = {
    {LineEndStyle::None, QT_TRANSLATE_NOOP("AnnotationStyleLabels", "No Arrow")},
    {LineEndStyle::EndArrow, QT_TRANSLATE_NOOP("AnnotationStyleLabels", "End Arrow")},
    {LineEndStyle::EndArrowOutline, QT_TRANSLATE_NOOP("AnnotationStyleLabels", "End Arrow Outline")},
    {LineEndStyle::EndArrowLine, QT_TRANSLATE_NOOP("AnnotationStyleLabels", "End Arrow Line")},
    {LineEndStyle::BothArrow, QT_TRANSLATE_NOOP("AnnotationStyleLabels", "Both Ends")},
    {LineEndStyle::BothArrowOutline, QT_TRANSLATE_NOOP("AnnotationStyleLabels", "Both Ends Outline")},
};

struct LineStyleLabel {
    LineStyle value;
    const char* text;
};

constexpr LineStyleLabel kLineStyleLabels[] = {
    {LineStyle::Solid, QT_TRANSLATE_NOOP("AnnotationStyleLabels", "Solid")},
    {LineStyle::Dashed, QT_TRANSLATE_NOOP("AnnotationStyleLabels", "Dashed")},
    {LineStyle::Dotted, QT_TRANSLATE_NOOP("AnnotationStyleLabels", "Dotted")},
};

} // namespace

namespace AnnotationStyleLabels {

QString arrowStyle(LineEndStyle style)
{
    for (const auto& entry : kArrowStyleLabels) {
        if (entry.value == style) return QCoreApplication::translate(kContext, entry.text);
    }
    return QString();
}

QString lineStyle(LineStyle style)
{
    for (const auto& entry : kLineStyleLabels) {
        if (entry.value == style) return QCoreApplication::translate(kContext, entry.text);
    }
    return QString();
}

} // namespace AnnotationStyleLabels
