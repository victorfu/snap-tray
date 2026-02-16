#include "settings/LanguageManager.h"
#include "settings/Settings.h"

#include <QApplication>
#include <QLibraryInfo>
#include <QStringList>
#include <QTranslator>

LanguageManager& LanguageManager::instance()
{
    static LanguageManager instance;
    return instance;
}

QVector<LanguageInfo> LanguageManager::availableLanguages() const
{
    return {
        {QStringLiteral("en"),    QStringLiteral("English")},
        {QStringLiteral("ar"),    QStringLiteral("\u0627\u0644\u0639\u0631\u0628\u064A\u0629")},
        {QStringLiteral("cs"),    QStringLiteral("\u010De\u0161tina")},
        {QStringLiteral("de"),    QStringLiteral("Deutsch")},
        {QStringLiteral("el"),    QStringLiteral("\u0395\u03BB\u03BB\u03B7\u03BD\u03B9\u03BA\u03AC")},
        {QStringLiteral("es_MX"), QStringLiteral("espa\u00F1ol de M\u00E9xico")},
        {QStringLiteral("fi"),    QStringLiteral("suomi")},
        {QStringLiteral("fr"),    QStringLiteral("fran\u00E7ais")},
        {QStringLiteral("it"),    QStringLiteral("italiano")},
        {QStringLiteral("ja"),    QStringLiteral("\u65E5\u672C\u8A9E")},
        {QStringLiteral("ko"),    QStringLiteral("\uD55C\uAD6D\uC5B4")},
        {QStringLiteral("lt"),    QStringLiteral("lietuvi\u0173")},
        {QStringLiteral("nl"),    QStringLiteral("Nederlands")},
        {QStringLiteral("pl"),    QStringLiteral("polski")},
        {QStringLiteral("pt"),    QStringLiteral("portugu\u00EAs")},
        {QStringLiteral("pt_PT"), QStringLiteral("portugu\u00EAs europeu")},
        {QStringLiteral("ru"),    QStringLiteral("\u0440\u0443\u0441\u0441\u043A\u0438\u0439")},
        {QStringLiteral("sr"),    QStringLiteral("\u0441\u0440\u043F\u0441\u043A\u0438")},
        {QStringLiteral("sv"),    QStringLiteral("svenska")},
        {QStringLiteral("tr"),    QStringLiteral("T\u00FCrk\u00E7e")},
        {QStringLiteral("vi"),    QStringLiteral("Ti\u1EBFng Vi\u1EC7t")},
        {QStringLiteral("zh_CN"), QStringLiteral("\u7B80\u4F53\u4E2D\u6587")},
        {QStringLiteral("zh_HK"), QStringLiteral("\u7E41\u9AD4\u4E2D\u6587\uFF08\u9999\u6E2F\uFF09")},
        {QStringLiteral("zh_TW"), QStringLiteral("\u7E41\u9AD4\u4E2D\u6587\uFF08\u53F0\u7063\uFF09")},
    };
}

QString LanguageManager::loadLanguage() const
{
    auto settings = SnapTray::getSettings();
    return settings.value(kSettingsKeyLanguage, defaultLanguage()).toString();
}

void LanguageManager::saveLanguage(const QString& languageCode)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kSettingsKeyLanguage, languageCode);
}

bool LanguageManager::loadTranslation(const QString& languageCode)
{
    auto* app = qApp;
    if (!app) {
        qWarning() << "LanguageManager: Cannot load translation - QApplication not created yet";
        return false;
    }

    // Remove existing translators
    if (m_appTranslator) {
        app->removeTranslator(m_appTranslator);
        delete m_appTranslator;
        m_appTranslator = nullptr;
    }
    if (m_qtTranslator) {
        app->removeTranslator(m_qtTranslator);
        delete m_qtTranslator;
        m_qtTranslator = nullptr;
    }

    // English is the source language, no translation needed
    if (languageCode == QLatin1String("en") || languageCode.isEmpty()) {
        return true;
    }

    // Load app translations from embedded resources
    m_appTranslator = new QTranslator(app);
    const QString qmPath = QStringLiteral(":/i18n/snaptray_%1").arg(languageCode);
    if (m_appTranslator->load(qmPath)) {
        app->installTranslator(m_appTranslator);
    }
    else {
        qWarning() << "LanguageManager: Failed to load app translation from:" << qmPath;
        delete m_appTranslator;
        m_appTranslator = nullptr;
        return false;
    }

    // Load Qt base translations for standard dialogs
    loadSystemTranslation(languageCode);

    return true;
}

void LanguageManager::loadSystemTranslation(const QString& languageCode)
{
    auto* app = qApp;
    if (!app) {
        return;
    }
    m_qtTranslator = new QTranslator(app);
    const QString qtTranslationsPath =
        QLibraryInfo::path(QLibraryInfo::TranslationsPath);

    const QString normalizedCode = QString(languageCode).trimmed().replace(QLatin1Char('-'),
                                                                            QLatin1Char('_'));
    QStringList candidateCodes;
    if (!normalizedCode.isEmpty()) {
        candidateCodes << normalizedCode;
        const int separatorIndex = normalizedCode.indexOf(QLatin1Char('_'));
        if (separatorIndex > 0) {
            candidateCodes << normalizedCode.left(separatorIndex);
        }
        candidateCodes.removeDuplicates();
    }

    const QStringList catalogPrefixes = {
        QStringLiteral("qtbase_"),
        QStringLiteral("qt_")
    };

    for (const QString& candidateCode : candidateCodes) {
        for (const QString& catalogPrefix : catalogPrefixes) {
            if (m_qtTranslator->load(catalogPrefix + candidateCode, qtTranslationsPath)) {
                app->installTranslator(m_qtTranslator);
                return;
            }
        }
    }

    qDebug() << "LanguageManager: Qt base translations not available for" << languageCode
             << "(candidates:" << candidateCodes << ", catalogs: qtbase_, qt_)";
    delete m_qtTranslator;
    m_qtTranslator = nullptr;
}
