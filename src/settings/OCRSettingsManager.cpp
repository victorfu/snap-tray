#include "settings/OCRSettingsManager.h"
#include "settings/Settings.h"
#include <QDebug>

OCRSettingsManager& OCRSettingsManager::instance()
{
    static OCRSettingsManager instance;
    return instance;
}

OCRSettingsManager::OCRSettingsManager()
    : m_languages{DEFAULT_LANGUAGE}
{
    load();
}

QStringList OCRSettingsManager::languages() const
{
    return m_languages;
}

void OCRSettingsManager::setLanguages(const QStringList &languages)
{
    m_languages = languages;
    m_languages.removeDuplicates();

    // Ensure en-US is always available, but preserve user order.
    if (!m_languages.contains(DEFAULT_LANGUAGE)) {
        m_languages.append(DEFAULT_LANGUAGE);
    }
}

void OCRSettingsManager::save()
{
    auto settings = SnapTray::getSettings();
    settings.setValue(SETTINGS_KEY, m_languages);
    qDebug() << "OCRSettingsManager: Saved languages:" << m_languages;
}

void OCRSettingsManager::load()
{
    auto settings = SnapTray::getSettings();
    QStringList defaultList = {DEFAULT_LANGUAGE};
    m_languages = settings.value(SETTINGS_KEY, defaultList).toStringList();
    m_languages.removeDuplicates();

    // Ensure en-US is always available, but preserve user order.
    if (!m_languages.contains(DEFAULT_LANGUAGE)) {
        m_languages.append(DEFAULT_LANGUAGE);
    }

    qDebug() << "OCRSettingsManager: Loaded languages:" << m_languages;
}
