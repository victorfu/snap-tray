#include "settings/Settings.h"

#include <QDir>
#include <QTemporaryDir>
#include <QtGlobal>

namespace {

class TestSettingsIsolation
{
public:
    TestSettingsIsolation()
        : m_settingsDirectory(QDir::tempPath()
                              + QStringLiteral("/snaptray-test-settings-XXXXXX"))
    {
        if (!m_settingsDirectory.isValid()) {
            qFatal("Failed to create an isolated settings directory for SnapTray tests");
        }

        SnapTray::setSettingsPathOverrideForTests(
            m_settingsDirectory.filePath(QStringLiteral("settings.ini")));
    }

private:
    QTemporaryDir m_settingsDirectory;
};

const TestSettingsIsolation testSettingsIsolation;

} // namespace
