#include "region/SelectionDimensionLabel.h"

#include "utils/CoordinateHelper.h"

#include <QCoreApplication>
#include <QFontMetrics>

namespace {

constexpr int kDimensionPanelHeight = 28;
constexpr int kDimensionPanelPadding = 12;
constexpr int kDimensionPanelSideGap = 8;
constexpr int kDimensionPanelTopGap = 8;
constexpr int kDimensionPanelInset = 5;
constexpr int kDimensionPanelFontPointSize = 12;
constexpr int kRegionControlRadiusToggleWidth = 36;
constexpr int kRegionControlRatioButtonUnlockedWidth = 36;
constexpr int kRegionControlRatioButtonLockedWidth = 95;
constexpr int kRegionControlHeight = 28;

QString formatLabel(const SelectionDimensionLabel::DisplayMetrics& metrics)
{
    return QCoreApplication::translate("SelectionDimensionLabel", "%1 x %2 %3")
        .arg(metrics.size.width())
        .arg(metrics.size.height())
        .arg(metrics.unit);
}

QString formatWidgetLabel(const SelectionDimensionLabel::DisplayMetrics& metrics)
{
    return QCoreApplication::translate("SelectionDimensionLabel", "%1×%2 %3")
        .arg(metrics.size.width())
        .arg(metrics.size.height())
        .arg(metrics.unit);
}

bool canFitInsideSelection(const QRect& selectionRect, const QSize& size, int inset)
{
    if (!selectionRect.isValid() || selectionRect.isEmpty() || !size.isValid() || size.isEmpty()) {
        return false;
    }

    const QRect insideBounds = selectionRect.normalized().adjusted(inset, inset, -inset, -inset);
    return insideBounds.isValid() && insideBounds.width() >= size.width();
}

QFont panelFont(const QFont& baseFont)
{
    QFont font(baseFont);
    font.setPointSize(kDimensionPanelFontPointSize);
    font.setBold(true);
    return font;
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

QString SelectionDimensionLabel::widgetLabel(const QSize& physicalSize, qreal dpr)
{
    return formatWidgetLabel(displayMetrics(physicalSize, dpr));
}

QString SelectionDimensionLabel::widgetLabel(const QRect& logicalRect, qreal dpr)
{
    return formatWidgetLabel(displayMetrics(logicalRect, dpr));
}

QString SelectionDimensionLabel::widgetSampleLabel()
{
    return widgetLabel(QSize(9999, 9999), 1.0);
}

QSize SelectionDimensionLabel::panelSize(const QString& label, const QFont& baseFont)
{
    const QFont font = panelFont(baseFont);
    QFontMetrics fm(font);

    const QString maxWidthText = widgetSampleLabel();
    const int fixedWidth = fm.horizontalAdvance(maxWidthText) + kDimensionPanelPadding;
    const int actualWidth = fm.horizontalAdvance(label) + kDimensionPanelPadding;
    return QSize(qMax(fixedWidth, actualWidth), kDimensionPanelHeight);
}

SelectionDimensionLabel::AttachmentLayout SelectionDimensionLabel::selectionPanelLayout(
    const QRect& selectionRect,
    const QString& label,
    const QFont& baseFont,
    const QSize& viewportSize,
    const QSize& controlAnchorSize)
{
    AttachmentLayout layout;
    Q_UNUSED(controlAnchorSize);

    const QRect sel = selectionRect.normalized();
    if (!sel.isValid() || sel.isEmpty()) {
        return layout;
    }

    const QSize size = panelSize(label, baseFont);
    if (!size.isValid() || size.isEmpty()) {
        return layout;
    }

    // Compact mode is only about horizontal crowding. Vertical placement can sit
    // above the selection, so short-but-wide captures should keep the standard layout.
    layout.compactRegion = !canFitInsideSelection(sel, size, kDimensionPanelInset);

    QRect textRect(QPoint(0, 0), size);
    int textX = sel.left();
    int textY = sel.top() - textRect.height() - kDimensionPanelTopGap;

    if (layout.compactRegion) {
        textRect.moveTo(sel.left() - textRect.width() - kDimensionPanelSideGap, sel.top());

        if (viewportSize.isValid()) {
            const int maxX = viewportSize.width() - kDimensionPanelInset;
            const int maxY = viewportSize.height() - kDimensionPanelInset;
            if (textRect.right() > maxX) {
                textRect.moveRight(maxX);
            }
            if (textRect.left() < kDimensionPanelInset) {
                textRect.moveLeft(kDimensionPanelInset);
            }
            if (textRect.bottom() > maxY) {
                textRect.moveBottom(maxY);
            }
            if (textRect.top() < kDimensionPanelInset) {
                textRect.moveTop(kDimensionPanelInset);
            }
        }

        layout.panelRect = textRect;
        return layout;
    }

    if (textY < kDimensionPanelInset) {
        textY = sel.top() + kDimensionPanelInset;
        textX = sel.left() + kDimensionPanelInset;
    }

    textRect.moveTo(textX, textY);

    if (viewportSize.isValid()) {
        const int maxX = viewportSize.width() - kDimensionPanelInset;
        const int maxY = viewportSize.height() - kDimensionPanelInset;
        if (textRect.right() > maxX) {
            textRect.moveRight(maxX);
        }
        if (textRect.left() < kDimensionPanelInset) {
            textRect.moveLeft(kDimensionPanelInset);
        }
        if (textRect.bottom() > maxY) {
            textRect.moveBottom(maxY);
        }
        if (textRect.top() < kDimensionPanelInset) {
            textRect.moveTop(kDimensionPanelInset);
        }
    }

    layout.panelRect = textRect;
    return layout;
}

QSize SelectionDimensionLabel::controlAnchorSize(bool ratioLocked)
{
    const int ratioButtonWidth = ratioLocked
        ? kRegionControlRatioButtonLockedWidth
        : kRegionControlRatioButtonUnlockedWidth;
    return QSize(kRegionControlRadiusToggleWidth + ratioButtonWidth, kRegionControlHeight);
}
