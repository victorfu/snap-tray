#include "region/SelectionDimensionLabel.h"

#include "utils/CoordinateHelper.h"

#include <QCoreApplication>

namespace {

QString formatLabel(const SelectionDimensionLabel::DisplayMetrics& metrics)
{
    return QCoreApplication::translate("SelectionDimensionLabel", "%1 x %2 %3")
        .arg(metrics.size.width())
        .arg(metrics.size.height())
        .arg(metrics.unit);
}

} // namespace

SelectionDimensionLabel::DisplayMetrics SelectionDimensionLabel::displayMetrics(const QSize& physicalSize,
                                                                                qreal dpr)
{
#ifdef Q_OS_MACOS
    const qreal effectiveDpr = dpr > 0.0 ? dpr : 1.0;
    return {CoordinateHelper::toLogical(physicalSize, effectiveDpr), QStringLiteral("pt")};
#else
    Q_UNUSED(dpr);
    return {physicalSize, QStringLiteral("px")};
#endif
}

SelectionDimensionLabel::DisplayMetrics SelectionDimensionLabel::displayMetrics(const QRect& logicalRect,
                                                                                qreal dpr)
{
    const QRect normalizedRect = logicalRect.normalized();

#ifdef Q_OS_MACOS
    Q_UNUSED(dpr);
    return {normalizedRect.size(), QStringLiteral("pt")};
#else
    const qreal effectiveDpr = dpr > 0.0 ? dpr : 1.0;
    return {CoordinateHelper::toPhysicalCoveringRect(normalizedRect, effectiveDpr).size(),
            QStringLiteral("px")};
#endif
}

QString SelectionDimensionLabel::label(const QSize& physicalSize, qreal dpr)
{
    return formatLabel(displayMetrics(physicalSize, dpr));
}

QString SelectionDimensionLabel::label(const QRect& logicalRect, qreal dpr)
{
    return formatLabel(displayMetrics(logicalRect, dpr));
}

QString SelectionDimensionLabel::sampleLabel()
{
    return label(QSize(9999, 9999), 1.0);
}
