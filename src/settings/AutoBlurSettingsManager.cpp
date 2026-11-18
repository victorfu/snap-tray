#include "settings/AutoBlurSettingsManager.h"
#include "settings/Settings.h"

#include <QSettings>

AutoBlurSettingsManager& AutoBlurSettingsManager::instance()
{
    static AutoBlurSettingsManager instance;
    return instance;
}

AutoBlurSettingsManager::Options AutoBlurSettingsManager::load() const
{
    Options options;
    options.sensitiveCategories = loadSensitiveCategories();
    options.enabled = loadEnabled();
    options.detectFaces = loadDetectFaces();
    options.blurIntensity = loadBlurIntensity();
    options.blurType = loadBlurType();
    return options;
}

void AutoBlurSettingsManager::save(const Options& options)
{
    saveSensitiveCategories(options.sensitiveCategories);
    saveEnabled(options.enabled);
    saveDetectFaces(options.detectFaces);
    saveBlurIntensity(options.blurIntensity);
    saveBlurType(options.blurType);
}

QStringList AutoBlurSettingsManager::loadSensitiveCategories() const
{
    return SnapTray::getSettings().value(kKeySensitiveCategories).toStringList();
}

void AutoBlurSettingsManager::saveSensitiveCategories(const QStringList& categories)
{
    SnapTray::getSettings().setValue(kKeySensitiveCategories, categories);
}

bool AutoBlurSettingsManager::loadEnabled() const
{
    auto settings = SnapTray::getSettings();
    return settings.value(kKeyEnabled, kDefaultEnabled).toBool();
}

void AutoBlurSettingsManager::saveEnabled(bool enabled)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kKeyEnabled, enabled);
}

bool AutoBlurSettingsManager::loadDetectFaces() const
{
    auto settings = SnapTray::getSettings();
    return settings.value(kKeyDetectFaces, kDefaultDetectFaces).toBool();
}

void AutoBlurSettingsManager::saveDetectFaces(bool detect)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kKeyDetectFaces, detect);
}

int AutoBlurSettingsManager::loadBlurIntensity() const
{
    auto settings = SnapTray::getSettings();
    return settings.value(kKeyBlurIntensity, kDefaultBlurIntensity).toInt();
}

void AutoBlurSettingsManager::saveBlurIntensity(int intensity)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kKeyBlurIntensity, intensity);
}

AutoBlurSettingsManager::BlurType AutoBlurSettingsManager::loadBlurType() const
{
    auto settings = SnapTray::getSettings();
    QString blurTypeStr = settings.value(kKeyBlurType, "pixelate").toString();
    return (blurTypeStr == "gaussian") ? BlurType::Gaussian : BlurType::Pixelate;
}

void AutoBlurSettingsManager::saveBlurType(BlurType type)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kKeyBlurType, type == BlurType::Gaussian ? "gaussian" : "pixelate");
}
