#pragma once

#include <QSettings>

namespace SnapTray {

inline constexpr const char* kOrganizationName = "Victor Fu";
inline constexpr const char* kApplicationName = "SnapTray";

inline QSettings getSettings()
{
    return QSettings(kOrganizationName, kApplicationName);
}

} // namespace SnapTray
