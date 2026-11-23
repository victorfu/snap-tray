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

bool canFitHorizontallyInsideSelection(const QRect& selectionRect, const QSize& size, int inset)
{
    if (!selectionRect.isValid() || selectionRect.isEmpty() || !size.isValid() || size.isEmpty()) {
        return false;
    }

    const QRect insideBounds = selectionRect.normalized().adjusted(inset, inset, -inset, -inset);
    return insideBounds.isValid() && insideBounds.width() >= size.width();
}

bool hasRoomAboveSelection(const QRect& selectionRect, const QSize& size)
{
    if (!selectionRect.isValid() || selectionRect.isEmpty() || !size.isValid() || size.isEmpty()) {
        return false;
    }

    return selectionRect.normalized().top() - size.height() - kDimensionPanelTopGap >=
           kDimensionPanelInset;
}

bool canUseInsideFallback(const QRect& selectionRect, const QSize& size, int inset)
{
    if (!selectionRect.isValid() || selectionRect.isEmpty() || !size.isValid() || size.isEmpty()) {
        return false;
    }

    const QRect insideBounds = selectionRect.normalized().adjusted(inset, inset, -inset, -inset);
    return insideBounds.isValid() &&
           insideBounds.width() >= size.width() &&
           insideBounds.height() >= size.height() + kDimensionPanelTopGap;
}

QRect clampIntoBounds(QRect rect, const QRect& bounds)
{
    if (rect.right() > bounds.right()) {
        rect.moveRight(bounds.right());
    }
    if (rect.left() < bounds.left()) {
        rect.moveLeft(bounds.left());
    }
    if (rect.bottom() > bounds.bottom()) {
        rect.moveBottom(bounds.bottom());
    }
    if (rect.top() < bounds.top()) {
        rect.moveTop(bounds.top());
    }
    return rect;
}

// A selection too small to host the label gets it placed beside the selection.
// Candidates are tried in order; each slides only along the selection edge it
// is attached to, so it can never be pushed back over the selection.
QRect compactPanelRect(const QRect& sel, const QSize& size, const QRect& bounds)
{
    enum class Slide { Vertical, Horizontal };
    struct Candidate {
        QPoint topLeft;
        Slide slide;
    };

    const Candidate candidates[] = {
        {{sel.left() - size.width() - kDimensionPanelSideGap, sel.top()}, Slide::Vertical},
        {{sel.right() + 1 + kDimensionPanelSideGap, sel.top()}, Slide::Vertical},
        {{sel.left(), sel.top() - size.height() - kDimensionPanelTopGap}, Slide::Horizontal},
        {{sel.left(), sel.bottom() + 1 + kDimensionPanelTopGap}, Slide::Horizontal},
    };

    const QRect preferred(candidates[0].topLeft, size);
    if (!bounds.isValid()) {
        return preferred;
    }
    if (bounds.width() < size.width() || bounds.height() < size.height()) {
        return clampIntoBounds(preferred, bounds);
    }

    for (const Candidate& candidate : candidates) {
        QRect rect(candidate.topLeft, size);
        if (candidate.slide == Slide::Vertical) {
            rect.moveTop(qBound(bounds.top(), rect.top(), bounds.bottom() - size.height() + 1));
        } else {
            rect.moveLeft(qBound(bounds.left(), rect.left(), bounds.right() - size.width() + 1));
        }
        if (bounds.contains(rect) && !rect.intersects(sel)) {
            return rect;
        }
    }

    // Only reachable when the viewport itself is too small for any outside slot.
    return clampIntoBounds(preferred, bounds);
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
    Q_UNUSED(controlAnchorSize);
    return placePanel(selectionRect, panelSize(label, baseFont), viewportSize);
}

SelectionDimensionLabel::AttachmentLayout SelectionDimensionLabel::placePanel(
    const QRect& selectionRect,
    const QSize& panelSize,
    const QSize& viewportSize)
{
    AttachmentLayout layout;

    const QRect sel = selectionRect.normalized();
    if (!sel.isValid() || sel.isEmpty() || !panelSize.isValid() || panelSize.isEmpty()) {
        return layout;
    }

    const bool fitsHorizontallyInside =
        canFitHorizontallyInsideSelection(sel, panelSize, kDimensionPanelInset);
    const bool mustFallbackInside = !hasRoomAboveSelection(sel, panelSize);
    layout.compactRegion =
        !fitsHorizontallyInside ||
        (mustFallbackInside && !canUseInsideFallback(sel, panelSize, kDimensionPanelInset));

    const QRect bounds = viewportSize.isValid()
        ? QRect(QPoint(kDimensionPanelInset, kDimensionPanelInset),
                QPoint(viewportSize.width() - kDimensionPanelInset,
                       viewportSize.height() - kDimensionPanelInset))
        : QRect();

    if (layout.compactRegion) {
        layout.panelRect = compactPanelRect(sel, panelSize, bounds);
        return layout;
    }

    QRect textRect(QPoint(0, 0), panelSize);
    if (mustFallbackInside) {
        textRect.moveTo(sel.left() + kDimensionPanelInset, sel.top() + kDimensionPanelInset);
    } else {
        textRect.moveTo(sel.left(), sel.top() - panelSize.height() - kDimensionPanelTopGap);
    }

    layout.panelRect = bounds.isValid() ? clampIntoBounds(textRect, bounds) : textRect;
    return layout;
}

QSize SelectionDimensionLabel::controlAnchorSize(bool ratioLocked)
{
    const int ratioButtonWidth = ratioLocked
        ? kRegionControlRatioButtonLockedWidth
        : kRegionControlRatioButtonUnlockedWidth;
    return QSize(kRegionControlRadiusToggleWidth + ratioButtonWidth, kRegionControlHeight);
}
