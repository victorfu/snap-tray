#ifndef WINDOWDETECTORMACFILTERS_H
#define WINDOWDETECTORMACFILTERS_H

#include <QString>

#include <optional>

namespace WindowDetectorMacFilters {

inline constexpr double kMinVisibleWindowAlpha = 0.05;
inline constexpr double kContainerRoleAreaRatioLimit = 0.60;
inline constexpr double kRegularRoleAreaRatioLimit = 0.90;

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

} // namespace WindowDetectorMacFilters

#endif // WINDOWDETECTORMACFILTERS_H
