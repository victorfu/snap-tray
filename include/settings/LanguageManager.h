#pragma once

#include <QString>
#include <QVector>

class QTranslator;

struct LanguageInfo {
    QString code;       // e.g., "zh_CN", "pt_PT", "en"
    QString nativeName; // e.g., "简体中文", "Português (Portugal)"
};

class LanguageManager
{
public:
    static LanguageManager& instance();

    QVector<LanguageInfo> availableLanguages() const;

    QString loadLanguage() const;
    void saveLanguage(const QString& languageCode);

    bool loadTranslation(const QString& languageCode);

    static QString defaultLanguage() { return QStringLiteral("en"); }

private:
    LanguageManager() = default;
    LanguageManager(const LanguageManager&) = delete;
    LanguageManager& operator=(const LanguageManager&) = delete;

    void loadSystemTranslation(const QString& languageCode);

    static constexpr const char* kSettingsKeyLanguage = "general/language";

    QTranslator* m_appTranslator = nullptr;
    QTranslator* m_qtTranslator = nullptr;
};
