#include "settings/RegionCaptureSettingsManager.h"
#include "settings/Settings.h"

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
