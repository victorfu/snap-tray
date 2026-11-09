#include "scrollcapture/ScrollAreaLocator.h"
#include "scrollcapture/ScrollInjector.h"

#include <QJsonArray>

namespace snaptray::scrollcapture {

namespace {

struct LocateStatusName {
    LocateStatus status;
    const char* name;
};

constexpr LocateStatusName kLocateStatusNames[] = {
    {LocateStatus::Found, "found"},
    {LocateStatus::NoScrollArea, "no_scroll_area"},
    {LocateStatus::NoWindow, "no_window"},
    {LocateStatus::PermissionDenied, "permission_denied"},
    {LocateStatus::Blocked, "blocked"},
    {LocateStatus::Unsupported, "unsupported"},
    {LocateStatus::Failed, "failed"},
};

struct AccessibilityBoostName {
    AccessibilityBoost boost;
    const char* name;
};

constexpr AccessibilityBoostName kAccessibilityBoostNames[] = {
    {AccessibilityBoost::ManualAccessibility, "manual"},
    {AccessibilityBoost::EnhancedUserInterface, "enhanced"},
    {AccessibilityBoost::Both, "both"},
};

struct ScrollMethodName {
    ScrollMethod method;
    const char* name;
};

constexpr ScrollMethodName kScrollMethodNames[] = {
    {ScrollMethod::WheelPixels, "wheel-pixels"},
    {ScrollMethod::WheelPixelsContinuous, "wheel-pixels-continuous"},
    {ScrollMethod::WheelDelta, "wheel-delta"},
    {ScrollMethod::PatternSmallIncrement, "pattern-small-increment"},
    {ScrollMethod::PatternSetPercent, "pattern-set-percent"},
};

template <typename T>
void insertOptional(QJsonObject& object, const char* key, const std::optional<T>& value)
{
    if (value.has_value()) {
        object.insert(QLatin1String(key), *value);
    }
}

QJsonObject rectToJson(const QRect& rect)
{
    return QJsonObject{
        {QStringLiteral("x"), rect.x()},
        {QStringLiteral("y"), rect.y()},
        {QStringLiteral("width"), rect.width()},
        {QStringLiteral("height"), rect.height()},
    };
}

} // namespace

QJsonObject ScrollAreaLocator::requestFullAccessibilityTree(qint64 /*processId*/, AccessibilityBoost /*boost*/)
{
    return QJsonObject{{QStringLiteral("supported"), false}};
}

void ScrollAreaLocator::restoreAccessibility()
{
}

const char* locateStatusName(LocateStatus status)
{
    for (const auto& entry : kLocateStatusNames) {
        if (entry.status == status) {
            return entry.name;
        }
    }
    return "unknown";
}

const char* accessibilityBoostName(AccessibilityBoost boost)
{
    for (const auto& entry : kAccessibilityBoostNames) {
        if (entry.boost == boost) {
            return entry.name;
        }
    }
    return "unknown";
}

std::optional<AccessibilityBoost> accessibilityBoostFromName(const QString& name)
{
    for (const auto& entry : kAccessibilityBoostNames) {
        if (name == QLatin1String(entry.name)) {
            return entry.boost;
        }
    }
    return std::nullopt;
}

const char* scrollMethodName(ScrollMethod method)
{
    for (const auto& entry : kScrollMethodNames) {
        if (entry.method == method) {
            return entry.name;
        }
    }
    return "unknown";
}

std::optional<ScrollMethod> scrollMethodFromName(const QString& name)
{
    for (const auto& entry : kScrollMethodNames) {
        if (name == QLatin1String(entry.name)) {
            return entry.method;
        }
    }
    return std::nullopt;
}

QJsonObject toJson(const ScrollPosition& position)
{
    QJsonObject object;
    insertOptional(object, "fraction", position.fraction);
    insertOptional(object, "viewportRatio", position.viewportRatio);
    insertOptional(object, "contentHeight", position.contentHeight);
    insertOptional(object, "offset", position.offset);
    insertOptional(object, "atTop", position.atTop);
    insertOptional(object, "atBottom", position.atBottom);
    object.insert(QStringLiteral("sources"), QJsonArray::fromStringList(position.sources));
    return object;
}

QJsonObject toJson(const ScrollAreaSnapshot& snapshot)
{
    return QJsonObject{
        {QStringLiteral("bounds"), rectToJson(snapshot.bounds)},
        {QStringLiteral("role"), snapshot.role},
        {QStringLiteral("level"), snapshot.level},
        {QStringLiteral("availableLevels"), snapshot.availableLevels},
        {QStringLiteral("processId"), snapshot.processId},
        {QStringLiteral("appName"), snapshot.appName},
        {QStringLiteral("appVersion"), snapshot.appVersion},
        {QStringLiteral("position"), toJson(snapshot.position)},
        {QStringLiteral("diagnostics"), snapshot.diagnostics},
    };
}

} // namespace snaptray::scrollcapture
