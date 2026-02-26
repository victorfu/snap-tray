#include "compose/ComposeSettingsManager.h"

#include "settings/Settings.h"

#include <QtGlobal>

ComposeSettingsManager& ComposeSettingsManager::instance()
{
    static ComposeSettingsManager instance;
    return instance;
}

QString ComposeSettingsManager::defaultTemplate() const
{
    auto settings = SnapTray::getSettings();
    return settings.value(kSettingsKeyDefaultTemplate, QStringLiteral("free")).toString();
}

void ComposeSettingsManager::setDefaultTemplate(const QString& templateId)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kSettingsKeyDefaultTemplate, templateId);
}

QString ComposeSettingsManager::defaultCopyFormat() const
{
    auto settings = SnapTray::getSettings();
    return settings.value(kSettingsKeyDefaultCopyFormat, QStringLiteral("html")).toString();
}

void ComposeSettingsManager::setDefaultCopyFormat(const QString& format)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kSettingsKeyDefaultCopyFormat, format);
}

bool ComposeSettingsManager::autoIncludeMetadata() const
{
    auto settings = SnapTray::getSettings();
    return settings.value(kSettingsKeyAutoIncludeMetadata, true).toBool();
}

void ComposeSettingsManager::setAutoIncludeMetadata(bool enabled)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kSettingsKeyAutoIncludeMetadata, enabled);
}

int ComposeSettingsManager::screenshotMaxWidth() const
{
    auto settings = SnapTray::getSettings();
    const int width = settings.value(kSettingsKeyScreenshotMaxWidth, 600).toInt();
    return qBound(kMinScreenshotWidth, width, kMaxScreenshotWidth);
}

void ComposeSettingsManager::setScreenshotMaxWidth(int width)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kSettingsKeyScreenshotMaxWidth, qBound(kMinScreenshotWidth, width, kMaxScreenshotWidth));
}
