#ifndef OCRSETTINGSMANAGER_H
#define OCRSETTINGSMANAGER_H

#include <QStringList>

/**
 * @brief Defines the behavior after OCR recognition completes.
 */
enum class OCRBehavior {
    DirectCopy,   ///< Copy text directly to clipboard (default)
    ShowEditor    ///< Show editor dialog to review and edit
};

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

    /**
     * @brief Get the current OCR behavior setting.
     * @return The configured behavior after OCR recognition
     */
    OCRBehavior behavior() const;

    /**
     * @brief Set the OCR behavior.
     * @param behavior The behavior to use after OCR recognition
     */
    void setBehavior(OCRBehavior behavior);

private:
    OCRSettingsManager();
    ~OCRSettingsManager() = default;
    OCRSettingsManager(const OCRSettingsManager&) = delete;
    OCRSettingsManager& operator=(const OCRSettingsManager&) = delete;

    QStringList m_languages;
    OCRBehavior m_behavior = OCRBehavior::DirectCopy;
    static constexpr const char* SETTINGS_KEY = "OCR/languages";
    static constexpr const char* BEHAVIOR_KEY = "OCR/behavior";
    static constexpr const char* DEFAULT_LANGUAGE = "en-US";
};

#endif // OCRSETTINGSMANAGER_H
