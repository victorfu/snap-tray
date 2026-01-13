#ifndef OCRSETTINGSMANAGER_H
#define OCRSETTINGSMANAGER_H

#include <QStringList>

/**
 * @brief Singleton manager for OCR language settings.
 *
 * Handles persistence of user's preferred OCR languages.
 * English (en-US) is always included as a fallback language.
 */
class OCRSettingsManager
{
public:
    static OCRSettingsManager& instance();

    // Get configured languages (order preserved, en-US always included)
    QStringList languages() const;

    // Set languages (order preserved, en-US will be appended if missing)
    void setLanguages(const QStringList &languages);

    // Save to QSettings
    void save();

    // Load from QSettings
    void load();

    // Default values
    static QStringList defaultLanguages() { return {DEFAULT_LANGUAGE}; }

private:
    OCRSettingsManager();
    ~OCRSettingsManager() = default;
    OCRSettingsManager(const OCRSettingsManager&) = delete;
    OCRSettingsManager& operator=(const OCRSettingsManager&) = delete;

    QStringList m_languages;
    static constexpr const char* SETTINGS_KEY = "OCR/languages";
    static constexpr const char* DEFAULT_LANGUAGE = "en-US";
};

#endif // OCRSETTINGSMANAGER_H
