#include "settings/RegionCaptureSettingsManager.h"
#include "settings/Settings.h"

#include <QStringList>

namespace {

using CursorCompanionStyle = RegionCaptureSettingsManager::CursorCompanionStyle;

CursorCompanionStyle cursorCompanionStyleFromInt(int value)
{
    switch (value) {
    case static_cast<int>(CursorCompanionStyle::None):
        return CursorCompanionStyle::None;
    case static_cast<int>(CursorCompanionStyle::Magnifier):
        return CursorCompanionStyle::Magnifier;
    case static_cast<int>(CursorCompanionStyle::Beaver):
        return CursorCompanionStyle::Beaver;
    default:
        return RegionCaptureSettingsManager::kDefaultCursorCompanionStyle;
    }
}

} // namespace

RegionCaptureSettingsManager& RegionCaptureSettingsManager::instance()
{
    static RegionCaptureSettingsManager instance;
    return instance;
}

RegionCaptureSettingsManager::CursorCompanionStyle
RegionCaptureSettingsManager::cursorCompanionStyle() const
{
    auto settings = SnapTray::getSettings();
    const QVariant storedValue = settings.value(kSettingsKeyCursorCompanionStyle);
    if (storedValue.isValid()) {
        bool ok = false;
        const int rawValue = storedValue.toInt(&ok);
        if (ok) {
            return cursorCompanionStyleFromInt(rawValue);
        }
    }
    return kDefaultCursorCompanionStyle;
}

void RegionCaptureSettingsManager::setCursorCompanionStyle(CursorCompanionStyle style)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kSettingsKeyCursorCompanionStyle, static_cast<int>(style));
}

bool RegionCaptureSettingsManager::isMagnifierEnabled() const
{
    return cursorCompanionStyle() == CursorCompanionStyle::Magnifier;
}

void RegionCaptureSettingsManager::setMagnifierEnabled(bool enabled)
{
    setCursorCompanionStyle(
        enabled ? CursorCompanionStyle::Magnifier : CursorCompanionStyle::None);
}

bool RegionCaptureSettingsManager::isShortcutHintsEnabled() const
{
    auto settings = SnapTray::getSettings();
    return settings.value(kSettingsKeyShowShortcutHints, kDefaultShortcutHintsEnabled).toBool();
}

void RegionCaptureSettingsManager::setShortcutHintsEnabled(bool enabled)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kSettingsKeyShowShortcutHints, enabled);
}

RegionCaptureSettingsManager::FullScreenCaptureAction
RegionCaptureSettingsManager::fullScreenCaptureAction() const
{
    auto settings = SnapTray::getSettings();
    bool ok = false;
    const int rawValue = settings.value(kSettingsKeyFullScreenCaptureAction).toInt(&ok);
    if (!ok
        || rawValue < static_cast<int>(FullScreenCaptureAction::CopyToClipboard)
        || rawValue > static_cast<int>(FullScreenCaptureAction::Edit)) {
        return kDefaultFullScreenCaptureAction;
    }
    return static_cast<FullScreenCaptureAction>(rawValue);
}

void RegionCaptureSettingsManager::setFullScreenCaptureAction(FullScreenCaptureAction action)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kSettingsKeyFullScreenCaptureAction, static_cast<int>(action));
}

int RegionCaptureSettingsManager::delayedCaptureSeconds() const
{
    auto settings = SnapTray::getSettings();
    bool ok = false;
    const int rawValue = settings.value(kSettingsKeyDelayedCaptureSeconds).toInt(&ok);
    if (!ok || !isValidDelayedCaptureSeconds(rawValue)) {
        return kDefaultDelayedCaptureSeconds;
    }
    return rawValue;
}

void RegionCaptureSettingsManager::setDelayedCaptureSeconds(int seconds)
{
    if (!isValidDelayedCaptureSeconds(seconds)) {
        seconds = kDefaultDelayedCaptureSeconds;
    }
    auto settings = SnapTray::getSettings();
    settings.setValue(kSettingsKeyDelayedCaptureSeconds, seconds);
}

bool RegionCaptureSettingsManager::isValidDelayedCaptureSeconds(int seconds)
{
    return seconds >= kMinDelayedCaptureSeconds && seconds <= kMaxDelayedCaptureSeconds;
}

std::optional<RegionCaptureSettingsManager::LastCaptureRegion>
RegionCaptureSettingsManager::lastCaptureRegion() const
{
    auto settings = SnapTray::getSettings();
    // Stored as "x,y,width,height" so every settings backend keeps it readable.
    const QStringList parts =
        settings.value(kSettingsKeyLastCaptureRegion).toString().split(QLatin1Char(','));
    constexpr int kRectFieldCount = 4;
    if (parts.size() != kRectFieldCount) {
        return std::nullopt;
    }

    int values[kRectFieldCount] = {};
    for (int i = 0; i < kRectFieldCount; ++i) {
        bool ok = false;
        values[i] = parts.at(i).trimmed().toInt(&ok);
        if (!ok) {
            return std::nullopt;
        }
    }

    const QRect rect(values[0], values[1], values[2], values[3]);
    if (rect.isEmpty()) {
        return std::nullopt;
    }
    return LastCaptureRegion{rect, settings.value(kSettingsKeyLastCaptureScreen).toString()};
}

void RegionCaptureSettingsManager::setLastCaptureRegion(const QRect& globalRect,
                                                        const QString& screenName)
{
    if (globalRect.isEmpty()) {
        return;
    }
    auto settings = SnapTray::getSettings();
    settings.setValue(kSettingsKeyLastCaptureRegion,
                      QStringLiteral("%1,%2,%3,%4")
                          .arg(globalRect.x())
                          .arg(globalRect.y())
                          .arg(globalRect.width())
                          .arg(globalRect.height()));
    settings.setValue(kSettingsKeyLastCaptureScreen, screenName);
}
