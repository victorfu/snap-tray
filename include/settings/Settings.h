#pragma once

#include <QSettings>

namespace SnapTray {

inline QSettings getSettings()
{
    return QSettings("Victor Fu", "SnapTray");
}

} // namespace SnapTray
