#pragma once

#include <QString>

namespace SnapTray {

class WindowsPrintScreenSettingsManager
{
public:
    static WindowsPrintScreenSettingsManager& instance();

    bool isSnippingShortcutEnabled() const;
    bool disableSnippingShortcut();

    void setRegistryPathForTests(const QString& path);
    void clearRegistryPathForTests();

private:
    WindowsPrintScreenSettingsManager() = default;

    QString registryPath() const;

    QString m_registryPathForTests;
};

} // namespace SnapTray
