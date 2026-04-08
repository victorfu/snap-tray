#ifndef WINDOWDETECTORMACFILTERS_H
#define WINDOWDETECTORMACFILTERS_H

#include <QPoint>
#include <QRect>
#include <QString>

#include <optional>

namespace WindowDetectorMacFilters {

enum class AxCandidateTier {
    None = 0,
    LeafControl = 1,
    GenericContainer = 2,
    PriorityContainer = 3
};

inline constexpr double kMinVisibleWindowAlpha = 0.05;
inline constexpr double kContainerRoleAreaRatioLimit = 0.60;
inline constexpr double kRegularRoleAreaRatioLimit = 0.90;
inline constexpr int kMeaningfulBoundsInsetDelta = 4;
inline constexpr double kMeaningfulBoundsAreaReductionRatio = 0.08;

inline bool shouldRejectTopLevelByAlpha(const std::optional<double> &alpha)
{
    return alpha.has_value() && *alpha < kMinVisibleWindowAlpha;
}

inline bool shouldRejectTopLevelByOnscreen(const std::optional<bool> &isOnscreen)
{
    return isOnscreen.has_value() && !*isOnscreen;
}

inline bool passesTopLevelVisibility(const std::optional<double> &alpha, const std::optional<bool> &isOnscreen)
{
    return !shouldRejectTopLevelByAlpha(alpha) && !shouldRejectTopLevelByOnscreen(isOnscreen);
}

inline bool shouldRejectAxByVisibility(bool isHidden, const std::optional<bool> &isVisible)
{
    if (isHidden) {
        return true;
    }
    return isVisible.has_value() && !*isVisible;
}

inline bool isContainerRole(const QString &role)
{
    return role == QStringLiteral("AXGroup") ||
           role == QStringLiteral("AXLayoutArea") ||
           role == QStringLiteral("AXSplitGroup") ||
           role == QStringLiteral("AXScrollArea");
}

inline bool isPriorityContainerRole(const QString &role)
{
    return role == QStringLiteral("AXSheet") ||
           role == QStringLiteral("AXDialog") ||
           role == QStringLiteral("AXPopover");
}

inline bool isMenuContainerRole(const QString &role)
{
    return role == QStringLiteral("AXMenu");
}

inline bool isMenuItemRole(const QString &role)
{
    return role == QStringLiteral("AXMenuItem") ||
           role == QStringLiteral("AXMenuBarItem");
}

inline AxCandidateTier candidateTierForRole(const QString &role)
{
    if (isMenuContainerRole(role) || isMenuItemRole(role)) {
        return AxCandidateTier::None;
    }
    if (isPriorityContainerRole(role)) {
        return AxCandidateTier::PriorityContainer;
    }
    if (isContainerRole(role)) {
        return AxCandidateTier::GenericContainer;
    }
    if (role.isEmpty()) {
        return AxCandidateTier::None;
    }
    return AxCandidateTier::LeafControl;
}

inline double maxAreaRatioForRole(const QString &role)
{
    return isContainerRole(role) ? kContainerRoleAreaRatioLimit : kRegularRoleAreaRatioLimit;
}

inline bool isAreaRatioAllowed(const QString &role, double elementArea, double windowArea)
{
    if (windowArea <= 0.0) {
        return true;
    }
    if (elementArea <= 0.0) {
        return false;
    }
    return (elementArea / windowArea) < maxAreaRatioForRole(role);
}

inline bool isMeaningfulBoundsReduction(const QRect &rawBounds, const QRect &refinedBounds)
{
    if (!rawBounds.isValid() || rawBounds.isEmpty() ||
        !refinedBounds.isValid() || refinedBounds.isEmpty()) {
        return false;
    }

    if (!rawBounds.contains(refinedBounds)) {
        return false;
    }

    const int leftInset = refinedBounds.left() - rawBounds.left();
    const int topInset = refinedBounds.top() - rawBounds.top();
    const int rightInset = rawBounds.right() - refinedBounds.right();
    const int bottomInset = rawBounds.bottom() - refinedBounds.bottom();

    if (leftInset >= kMeaningfulBoundsInsetDelta ||
        topInset >= kMeaningfulBoundsInsetDelta ||
        rightInset >= kMeaningfulBoundsInsetDelta ||
        bottomInset >= kMeaningfulBoundsInsetDelta) {
        return true;
    }

    const double rawArea = static_cast<double>(rawBounds.width()) * static_cast<double>(rawBounds.height());
    const double refinedArea = static_cast<double>(refinedBounds.width()) * static_cast<double>(refinedBounds.height());
    if (rawArea <= 0.0 || refinedArea <= 0.0) {
        return false;
    }

    return refinedArea <= rawArea * (1.0 - kMeaningfulBoundsAreaReductionRatio);
}

inline bool shouldAcceptCandidateBounds(const QRect &rawBounds,
                                        const QRect &candidateBounds,
                                        const QPoint &hitPoint,
                                        int minSize)
{
    if (!candidateBounds.isValid() || candidateBounds.isEmpty()) {
        return false;
    }

    if (minSize > 0 &&
        (candidateBounds.width() < minSize || candidateBounds.height() < minSize)) {
        return false;
    }

    if (!candidateBounds.contains(hitPoint)) {
        return false;
    }

    return isMeaningfulBoundsReduction(rawBounds, candidateBounds);
}

inline bool shouldPreferAxCandidate(AxCandidateTier candidateTier,
                                    double candidateArea,
                                    int candidateDepth,
                                    AxCandidateTier bestTier,
                                    double bestArea,
                                    int bestDepth)
{
    if (candidateTier != bestTier) {
        return static_cast<int>(candidateTier) > static_cast<int>(bestTier);
    }

    if (candidateTier == AxCandidateTier::LeafControl) {
        return candidateDepth < bestDepth;
    }

    if (candidateArea != bestArea) {
        return candidateArea > bestArea;
    }

    return candidateDepth < bestDepth;
}

} // namespace WindowDetectorMacFilters

#endif // WINDOWDETECTORMACFILTERS_H
